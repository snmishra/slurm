/*****************************************************************************\
 *  src/plugins/task/affinity/affinity.c - task affinity plugin
 *  $Id: affinity.c,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#include "affinity.h"

void slurm_chkaffinity(cpu_set_t *mask, slurmd_job_t *job, int statval)
{
	char bind_type[42];
	char status[42];
	char prefix[42];
	char suffix[42];
	char mstr[1 + CPU_SETSIZE / 4];
	int task_id = job->envtp->procid;
	pid_t mypid = job->envtp->task_pid;

	if (!(job->cpu_bind_type & CPU_BIND_VERBOSE)) return;

	status[0] = '\0';
	prefix[0] = '\0';
	suffix[0] = '\0';
	if (statval) { strcpy(status, "FAILED "); }

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		strcpy(bind_type, "set to NO");
		strcpy(prefix, "current ");
		sprintf(suffix, "is mask 0x");
	} else {
		strcpy(prefix, "setting ");
		sprintf(suffix, "to mask 0x");
		if (job->cpu_bind_type & CPU_BIND_RANK) {
			strcpy(bind_type, "set to RANK");
		} else if (job->cpu_bind_type & CPU_BIND_MAPCPU) {
			strcpy(bind_type, "set to MAP_CPU");
		} else if (job->cpu_bind_type & CPU_BIND_MASKCPU) {
			strcpy(bind_type, "set to MASK_CPU");
		} else if (job->cpu_bind_type & (~CPU_BIND_VERBOSE)) {
			strcpy(bind_type, "set to UNKNOWN");
		} else {
			strcpy(bind_type, "not set");
			strcpy(prefix, "current ");
			sprintf(suffix, "is mask 0x");
		}
	}

	fprintf(stderr, "SLURM_CPU_BIND_TYPE %s, "
			"%s%saffinity of task %u pid %u on host %s %s%s\n",
			bind_type,
			status,
			prefix,
			task_id,
			mypid,
			conf->hostname,
			suffix,
			cpuset_to_str(mask, mstr));
}

int get_cpuset(cpu_set_t *mask, slurmd_job_t *job)
{
	int nummasks, maskid, i;
	char *curstr, *selstr;
	char mstr[1 + CPU_SETSIZE / 4];
	int local_id = job->envtp->localid;

	debug3("get_cpuset (%d) %s\n", job->cpu_bind_type, job->cpu_bind);
	CPU_ZERO(mask);

	if (job->cpu_bind_type & CPU_BIND_NONE) {
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_RANK) {
		CPU_SET(job->envtp->localid % job->cpus, mask);
		return true;
	}

	if (!job->cpu_bind)
		return false;

	nummasks = 1;
	maskid = 0;
	selstr = NULL;

	/* get number of strings present in cpu_bind */
	curstr = job->cpu_bind;
	while (*curstr) {
		if (nummasks == local_id+1) {
			selstr = curstr;
			maskid = local_id;
			break;
		}
		if (*curstr == ',')
			nummasks++;
		curstr++;
	}

	/* if we didn't already find the mask... */
	if (!selstr) {
		/* ...select mask string by wrapping task ID into list */
		maskid = local_id % nummasks;
		i = maskid;
		curstr = job->cpu_bind;
		while (*curstr && i) {
			if (*curstr == ',')
			    	i--;
			curstr++;
		}
		if (!*curstr) {
			return false;
		}
		selstr = curstr;
	}

	/* extract the selected mask from the list */
	i = 0;
	curstr = mstr;
	while (*selstr && *selstr != ',' && i++ < (CPU_SETSIZE/4))
		*curstr++ = *selstr++;
	*curstr = '\0';

	if (job->cpu_bind_type & CPU_BIND_MASKCPU) {
		/* convert mask string into cpu_set_t mask */
		if (str_to_cpuset(mask, mstr) < 0) {
			error("str_to_cpuset %s", mstr);
			return false;
		}
		return true;
	}

	if (job->cpu_bind_type & CPU_BIND_MAPCPU) {
		unsigned int mycpu = 0;
		if (strncmp(mstr, "0x", 2) == 0) {
			mycpu = strtoul (&(mstr[2]), NULL, 16);
		} else {
			mycpu = strtoul (mstr, NULL, 10);
		}
		CPU_SET(mycpu, mask);
		return true;
	}

	return false;
}

int slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask)
{
	int rval;
	char mstr[1 + CPU_SETSIZE / 4];

#ifdef HAVE_SCHED_SETAFFINITY
	rval = sched_setaffinity(pid, size, mask);
#else
	rval = sched_setaffinity(pid, mask);
#endif
	if (rval)
		verbose("sched_setaffinity(%d,%d,0x%s) failed with status %d",
				pid, size, cpuset_to_str(mask, mstr), rval);
	return (rval);
}

int slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask)
{
	int rval;
	char mstr[1 + CPU_SETSIZE / 4];

	CPU_ZERO(mask);
#ifdef SCHED_GETAFFINITY_THREE_ARGS
	rval = sched_getaffinity(pid, size, mask);
#else
	rval = sched_getaffinity(pid, mask);
#endif
	if (rval)
		verbose("sched_getaffinity(%d,%d,0x%s) failed with status %d",
				pid, size, cpuset_to_str(mask, mstr), rval);

	debug3("sched_getaffinity(%d) = 0x%s", pid, cpuset_to_str(mask, mstr));
	return (rval);
}

