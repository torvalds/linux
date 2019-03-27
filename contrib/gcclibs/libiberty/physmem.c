/* Calculate the size of physical memory.
   Copyright 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by Paul Eggert.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_SYS_PSTAT_H
# include <sys/pstat.h>
#endif

#if HAVE_SYS_SYSMP_H
# include <sys/sysmp.h>
#endif

#if HAVE_SYS_SYSINFO_H && HAVE_MACHINE_HAL_SYSINFO_H
# include <sys/sysinfo.h>
# include <machine/hal_sysinfo.h>
#endif

#if HAVE_SYS_TABLE_H
# include <sys/table.h>
#endif

#include <sys/types.h>

#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#if HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#if HAVE_SYS_SYSTEMCFG_H
# include <sys/systemcfg.h>
#endif

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
/*  MEMORYSTATUSEX is missing from older windows headers, so define
    a local replacement.  */
typedef struct
{
  DWORD dwLength;
  DWORD dwMemoryLoad;
  DWORDLONG ullTotalPhys;
  DWORDLONG ullAvailPhys;
  DWORDLONG ullTotalPageFile;
  DWORDLONG ullAvailPageFile;
  DWORDLONG ullTotalVirtual;
  DWORDLONG ullAvailVirtual;
  DWORDLONG ullAvailExtendedVirtual;
} lMEMORYSTATUSEX;
typedef WINBOOL (WINAPI *PFN_MS_EX) (lMEMORYSTATUSEX*);
#endif

#include "libiberty.h"

/* Return the total amount of physical memory.  */
double
physmem_total (void)
{
#if defined _SC_PHYS_PAGES && defined _SC_PAGESIZE
  { /* This works on linux-gnu, solaris2 and cygwin.  */
    double pages = sysconf (_SC_PHYS_PAGES);
    double pagesize = sysconf (_SC_PAGESIZE);
    if (0 <= pages && 0 <= pagesize)
      return pages * pagesize;
  }
#endif

#if HAVE_PSTAT_GETSTATIC
  { /* This works on hpux11.  */
    struct pst_static pss;
    if (0 <= pstat_getstatic (&pss, sizeof pss, 1, 0))
      {
	double pages = pss.physical_memory;
	double pagesize = pss.page_size;
	if (0 <= pages && 0 <= pagesize)
	  return pages * pagesize;
      }
  }
#endif

#if HAVE_SYSMP && defined MP_SAGET && defined MPSA_RMINFO && defined _SC_PAGESIZE
  { /* This works on irix6. */
    struct rminfo realmem;
    if (sysmp (MP_SAGET, MPSA_RMINFO, &realmem, sizeof realmem) == 0)
      {
	double pagesize = sysconf (_SC_PAGESIZE);
	double pages = realmem.physmem;
	if (0 <= pages && 0 <= pagesize)
	  return pages * pagesize;
      }
  }
#endif

#if HAVE_GETSYSINFO && defined GSI_PHYSMEM
  { /* This works on Tru64 UNIX V4/5.  */
    int physmem;

    if (getsysinfo (GSI_PHYSMEM, (caddr_t) &physmem, sizeof (physmem),
		    NULL, NULL, NULL) == 1)
      {
	double kbytes = physmem;

	if (0 <= kbytes)
	  return kbytes * 1024.0;
      }
  }
#endif

#if HAVE_SYSCTL && defined HW_PHYSMEM
  { /* This works on *bsd and darwin.  */
    unsigned int physmem;
    size_t len = sizeof physmem;
    static int mib[2] = { CTL_HW, HW_PHYSMEM };

    if (sysctl (mib, ARRAY_SIZE (mib), &physmem, &len, NULL, 0) == 0
	&& len == sizeof (physmem))
      return (double) physmem;
  }
#endif

#if HAVE__SYSTEM_CONFIGURATION
  /* This works on AIX 4.3.3+.  */
  return _system_configuration.physmem;
#endif

#if defined _WIN32
  { /* this works on windows */
    PFN_MS_EX pfnex;
    HMODULE h = GetModuleHandle ("kernel32.dll");

    if (!h)
      return 0.0;

    /*  Use GlobalMemoryStatusEx if available.  */
    if ((pfnex = (PFN_MS_EX) GetProcAddress (h, "GlobalMemoryStatusEx")))
      {
	lMEMORYSTATUSEX lms_ex;
	lms_ex.dwLength = sizeof lms_ex;
	if (!pfnex (&lms_ex))
	  return 0.0;
	return (double) lms_ex.ullTotalPhys;
      }

    /*  Fall back to GlobalMemoryStatus which is always available.
        but returns wrong results for physical memory > 4GB.  */
    else
      {
	MEMORYSTATUS ms;
	GlobalMemoryStatus (&ms);
	return (double) ms.dwTotalPhys;
      }
  }
#endif

  /* Return 0 if we can't determine the value.  */
  return 0;
}

/* APPLE LOCAL begin retune gc params 6124839 */
unsigned int
ncpu_available (void)
{
#if HAVE_SYSCTL && defined HW_AVAILCPU
  { /* This works on *bsd and darwin.  */
    unsigned int ncpu;
    size_t len = sizeof ncpu;
    static int mib[2] = { CTL_HW, HW_AVAILCPU };

    if (sysctl (mib, ARRAY_SIZE (mib), &ncpu, &len, NULL, 0) == 0
	&& len == sizeof (ncpu))
      return ncpu;
  }
#endif
#if HAVE_SYSCTL && defined HW_NCPU
  { /* This works on *bsd and darwin.  */
    unsigned int ncpu;
    size_t len = sizeof ncpu;
    static int mib[2] = { CTL_HW, HW_NCPU };

    if (sysctl (mib, ARRAY_SIZE (mib), &ncpu, &len, NULL, 0) == 0
	&& len == sizeof (ncpu))
      return ncpu;
  }
#endif
  return 1;
}
/* APPLE LOCAL end retune gc params 6124839 */

/* Return the amount of physical memory available.  */
double
physmem_available (void)
{
#if defined _SC_AVPHYS_PAGES && defined _SC_PAGESIZE
  { /* This works on linux-gnu, solaris2 and cygwin.  */
    double pages = sysconf (_SC_AVPHYS_PAGES);
    double pagesize = sysconf (_SC_PAGESIZE);
    if (0 <= pages && 0 <= pagesize)
      return pages * pagesize;
  }
#endif

#if HAVE_PSTAT_GETSTATIC && HAVE_PSTAT_GETDYNAMIC
  { /* This works on hpux11.  */
    struct pst_static pss;
    struct pst_dynamic psd;
    if (0 <= pstat_getstatic (&pss, sizeof pss, 1, 0)
	&& 0 <= pstat_getdynamic (&psd, sizeof psd, 1, 0))
      {
	double pages = psd.psd_free;
	double pagesize = pss.page_size;
	if (0 <= pages && 0 <= pagesize)
	  return pages * pagesize;
      }
  }
#endif

#if HAVE_SYSMP && defined MP_SAGET && defined MPSA_RMINFO && defined _SC_PAGESIZE
  { /* This works on irix6. */
    struct rminfo realmem;
    if (sysmp (MP_SAGET, MPSA_RMINFO, &realmem, sizeof realmem) == 0)
      {
	double pagesize = sysconf (_SC_PAGESIZE);
	double pages = realmem.availrmem;
	if (0 <= pages && 0 <= pagesize)
	  return pages * pagesize;
      }
  }
#endif

#if HAVE_TABLE && defined TBL_VMSTATS
  { /* This works on Tru64 UNIX V4/5.  */
    struct tbl_vmstats vmstats;

    if (table (TBL_VMSTATS, 0, &vmstats, 1, sizeof (vmstats)) == 1)
      {
	double pages = vmstats.free_count;
	double pagesize = vmstats.pagesize;

	if (0 <= pages && 0 <= pagesize)
	  return pages * pagesize;
      }
  }
#endif

#if HAVE_SYSCTL && defined HW_USERMEM
  { /* This works on *bsd and darwin.  */
    unsigned int usermem;
    size_t len = sizeof usermem;
    static int mib[2] = { CTL_HW, HW_USERMEM };

    if (sysctl (mib, ARRAY_SIZE (mib), &usermem, &len, NULL, 0) == 0
	&& len == sizeof (usermem))
      return (double) usermem;
  }
#endif

#if defined _WIN32
  { /* this works on windows */
    PFN_MS_EX pfnex;
    HMODULE h = GetModuleHandle ("kernel32.dll");

    if (!h)
      return 0.0;

    /*  Use GlobalMemoryStatusEx if available.  */
    if ((pfnex = (PFN_MS_EX) GetProcAddress (h, "GlobalMemoryStatusEx")))
      {
	lMEMORYSTATUSEX lms_ex;
	lms_ex.dwLength = sizeof lms_ex;
	if (!pfnex (&lms_ex))
	  return 0.0;
	return (double) lms_ex.ullAvailPhys;
      }

    /*  Fall back to GlobalMemoryStatus which is always available.
        but returns wrong results for physical memory > 4GB  */
    else
      {
	MEMORYSTATUS ms;
	GlobalMemoryStatus (&ms);
	return (double) ms.dwAvailPhys;
      }
  }
#endif

  /* Guess 25% of physical memory.  */
  return physmem_total () / 4;
}


#if DEBUG

# include <stdio.h>
# include <stdlib.h>

int
main (void)
{
  printf ("%12.f %12.f\n", physmem_total (), physmem_available ());
  exit (0);
}

#endif /* DEBUG */

/*
Local Variables:
compile-command: "gcc -DDEBUG -DHAVE_CONFIG_H -I.. -g -O -Wall -W physmem.c"
End:
*/
