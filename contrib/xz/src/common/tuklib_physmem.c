///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_physmem.c
/// \brief      Get the amount of physical memory
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tuklib_physmem.h"

// We want to use Windows-specific code on Cygwin, which also has memory
// information available via sysconf(), but on Cygwin 1.5 and older it
// gives wrong results (from our point of view).
#if defined(_WIN32) || defined(__CYGWIN__)
#	ifndef _WIN32_WINNT
#		define _WIN32_WINNT 0x0500
#	endif
#	include <windows.h>

#elif defined(__OS2__)
#	define INCL_DOSMISC
#	include <os2.h>

#elif defined(__DJGPP__)
#	include <dpmi.h>

#elif defined(__VMS)
#	include <lib$routines.h>
#	include <syidef.h>
#	include <ssdef.h>

#elif defined(AMIGA) || defined(__AROS__)
#	define __USE_INLINE__
#	include <proto/exec.h>

#elif defined(__QNX__)
#	include <sys/syspage.h>
#	include <string.h>

#elif defined(TUKLIB_PHYSMEM_AIX)
#	include <sys/systemcfg.h>

#elif defined(TUKLIB_PHYSMEM_SYSCONF)
#	include <limits.h>
#	include <unistd.h>

#elif defined(TUKLIB_PHYSMEM_SYSCTL)
#	ifdef HAVE_SYS_PARAM_H
#		include <sys/param.h>
#	endif
#	include <sys/sysctl.h>

// Tru64
#elif defined(TUKLIB_PHYSMEM_GETSYSINFO)
#	include <sys/sysinfo.h>
#	include <machine/hal_sysinfo.h>

// HP-UX
#elif defined(TUKLIB_PHYSMEM_PSTAT_GETSTATIC)
#	include <sys/param.h>
#	include <sys/pstat.h>

// IRIX
#elif defined(TUKLIB_PHYSMEM_GETINVENT_R)
#	include <invent.h>

// This sysinfo() is Linux-specific.
#elif defined(TUKLIB_PHYSMEM_SYSINFO)
#	include <sys/sysinfo.h>
#endif


extern uint64_t
tuklib_physmem(void)
{
	uint64_t ret = 0;

#if defined(_WIN32) || defined(__CYGWIN__)
	if ((GetVersion() & 0xFF) >= 5) {
		// Windows 2000 and later have GlobalMemoryStatusEx() which
		// supports reporting values greater than 4 GiB. To keep the
		// code working also on older Windows versions, use
		// GlobalMemoryStatusEx() conditionally.
		HMODULE kernel32 = GetModuleHandle("kernel32.dll");
		if (kernel32 != NULL) {
			typedef BOOL (WINAPI *gmse_type)(LPMEMORYSTATUSEX);
			gmse_type gmse = (gmse_type)GetProcAddress(
					kernel32, "GlobalMemoryStatusEx");
			if (gmse != NULL) {
				MEMORYSTATUSEX meminfo;
				meminfo.dwLength = sizeof(meminfo);
				if (gmse(&meminfo))
					ret = meminfo.ullTotalPhys;
			}
		}
	}

	if (ret == 0) {
		// GlobalMemoryStatus() is supported by Windows 95 and later,
		// so it is fine to link against it unconditionally. Note that
		// GlobalMemoryStatus() has no return value.
		MEMORYSTATUS meminfo;
		meminfo.dwLength = sizeof(meminfo);
		GlobalMemoryStatus(&meminfo);
		ret = meminfo.dwTotalPhys;
	}

#elif defined(__OS2__)
	unsigned long mem;
	if (DosQuerySysInfo(QSV_TOTPHYSMEM, QSV_TOTPHYSMEM,
			&mem, sizeof(mem)) == 0)
		ret = mem;

#elif defined(__DJGPP__)
	__dpmi_free_mem_info meminfo;
	if (__dpmi_get_free_memory_information(&meminfo) == 0
			&& meminfo.total_number_of_physical_pages
				!= (unsigned long)-1)
		ret = (uint64_t)meminfo.total_number_of_physical_pages * 4096;

#elif defined(__VMS)
	int vms_mem;
	int val = SYI$_MEMSIZE;
	if (LIB$GETSYI(&val, &vms_mem, 0, 0, 0, 0) == SS$_NORMAL)
		ret = (uint64_t)vms_mem * 8192;

#elif defined(AMIGA) || defined(__AROS__)
	ret = AvailMem(MEMF_TOTAL);

#elif defined(__QNX__)
	const struct asinfo_entry *entries = SYSPAGE_ENTRY(asinfo);
	size_t count = SYSPAGE_ENTRY_SIZE(asinfo) / sizeof(struct asinfo_entry);
	const char *strings = SYSPAGE_ENTRY(strings)->data;

	for (size_t i = 0; i < count; ++i)
		if (strcmp(strings + entries[i].name, "ram") == 0)
			ret += entries[i].end - entries[i].start + 1;

#elif defined(TUKLIB_PHYSMEM_AIX)
	ret = _system_configuration.physmem;

#elif defined(TUKLIB_PHYSMEM_SYSCONF)
	const long pagesize = sysconf(_SC_PAGESIZE);
	const long pages = sysconf(_SC_PHYS_PAGES);
	if (pagesize != -1 && pages != -1) {
		// According to docs, pagesize * pages can overflow.
		// Simple case is 32-bit box with 4 GiB or more RAM,
		// which may report exactly 4 GiB of RAM, and "long"
		// being 32-bit will overflow. Casting to uint64_t
		// hopefully avoids overflows in the near future.
		ret = (uint64_t)pagesize * (uint64_t)pages;
		if (ret > SIZE_T_MAX)
			ret = SIZE_T_MAX;
	}

#elif defined(TUKLIB_PHYSMEM_SYSCTL)
	int name[2] = {
		CTL_HW,
#ifdef HW_PHYSMEM64
		HW_PHYSMEM64
#else
		HW_PHYSMEM
#endif
	};
	union {
		uint32_t u32;
		uint64_t u64;
	} mem;
	size_t mem_ptr_size = sizeof(mem.u64);
	if (sysctl(name, 2, &mem.u64, &mem_ptr_size, NULL, 0) != -1) {
		// IIRC, 64-bit "return value" is possible on some 64-bit
		// BSD systems even with HW_PHYSMEM (instead of HW_PHYSMEM64),
		// so support both.
		if (mem_ptr_size == sizeof(mem.u64))
			ret = mem.u64;
		else if (mem_ptr_size == sizeof(mem.u32))
			ret = mem.u32;
	}

#elif defined(TUKLIB_PHYSMEM_GETSYSINFO)
	// Docs are unclear if "start" is needed, but it doesn't hurt
	// much to have it.
	int memkb;
	int start = 0;
	if (getsysinfo(GSI_PHYSMEM, (caddr_t)&memkb, sizeof(memkb), &start)
			!= -1)
		ret = (uint64_t)memkb * 1024;

#elif defined(TUKLIB_PHYSMEM_PSTAT_GETSTATIC)
	struct pst_static pst;
	if (pstat_getstatic(&pst, sizeof(pst), 1, 0) != -1)
		ret = (uint64_t)pst.physical_memory * (uint64_t)pst.page_size;

#elif defined(TUKLIB_PHYSMEM_GETINVENT_R)
	inv_state_t *st = NULL;
	if (setinvent_r(&st) != -1) {
		inventory_t *i;
		while ((i = getinvent_r(st)) != NULL) {
			if (i->inv_class == INV_MEMORY
					&& i->inv_type == INV_MAIN_MB) {
				ret = (uint64_t)i->inv_state << 20;
				break;
			}
		}

		endinvent_r(st);
	}

#elif defined(TUKLIB_PHYSMEM_SYSINFO)
	struct sysinfo si;
	if (sysinfo(&si) == 0)
		ret = (uint64_t)si.totalram * si.mem_unit;
#endif

	return ret;
}
