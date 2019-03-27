///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_cpucores.c
/// \brief      Get the number of CPU cores online
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tuklib_cpucores.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#	ifndef _WIN32_WINNT
#		define _WIN32_WINNT 0x0500
#	endif
#	include <windows.h>

// glibc >= 2.9
#elif defined(TUKLIB_CPUCORES_SCHED_GETAFFINITY)
#	include <sched.h>

// FreeBSD
#elif defined(TUKLIB_CPUCORES_CPUSET)
#	include <sys/param.h>
#	include <sys/cpuset.h>

#elif defined(TUKLIB_CPUCORES_SYSCTL)
#	ifdef HAVE_SYS_PARAM_H
#		include <sys/param.h>
#	endif
#	include <sys/sysctl.h>

#elif defined(TUKLIB_CPUCORES_SYSCONF)
#	include <unistd.h>

// HP-UX
#elif defined(TUKLIB_CPUCORES_PSTAT_GETDYNAMIC)
#	include <sys/param.h>
#	include <sys/pstat.h>
#endif


extern uint32_t
tuklib_cpucores(void)
{
	uint32_t ret = 0;

#if defined(_WIN32) || defined(__CYGWIN__)
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	ret = sysinfo.dwNumberOfProcessors;

#elif defined(TUKLIB_CPUCORES_SCHED_GETAFFINITY)
	cpu_set_t cpu_mask;
	if (sched_getaffinity(0, sizeof(cpu_mask), &cpu_mask) == 0)
		ret = CPU_COUNT(&cpu_mask);

#elif defined(TUKLIB_CPUCORES_CPUSET)
	cpuset_t set;
	if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
			sizeof(set), &set) == 0) {
#	ifdef CPU_COUNT
		ret = CPU_COUNT(&set);
#	else
		for (unsigned i = 0; i < CPU_SETSIZE; ++i)
			if (CPU_ISSET(i, &set))
				++ret;
#	endif
	}

#elif defined(TUKLIB_CPUCORES_SYSCTL)
	int name[2] = { CTL_HW, HW_NCPU };
	int cpus;
	size_t cpus_size = sizeof(cpus);
	if (sysctl(name, 2, &cpus, &cpus_size, NULL, 0) != -1
			&& cpus_size == sizeof(cpus) && cpus > 0)
		ret = cpus;

#elif defined(TUKLIB_CPUCORES_SYSCONF)
#	ifdef _SC_NPROCESSORS_ONLN
	// Most systems
	const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
#	else
	// IRIX
	const long cpus = sysconf(_SC_NPROC_ONLN);
#	endif
	if (cpus > 0)
		ret = cpus;

#elif defined(TUKLIB_CPUCORES_PSTAT_GETDYNAMIC)
	struct pst_dynamic pst;
	if (pstat_getdynamic(&pst, sizeof(pst), 1, 0) != -1)
		ret = pst.psd_proc_cnt;
#endif

	return ret;
}
