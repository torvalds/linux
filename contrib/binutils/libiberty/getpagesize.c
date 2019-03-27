/* Emulation of getpagesize() for systems that need it. */

/*

@deftypefn Supplemental int getpagesize (void)

Returns the number of bytes in a page of memory.  This is the
granularity of many of the system memory management routines.  No
guarantee is made as to whether or not it is the same as the basic
memory management hardware page size.

@end deftypefn

BUGS

	Is intended as a reasonable replacement for systems where this
	is not provided as a system call.  The value of 4096 may or may
	not be correct for the systems where it is returned as the default
	value.

*/

#ifndef VMS

#include "config.h"

#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#undef GNU_OUR_PAGESIZE
#if defined (HAVE_SYSCONF) && defined (HAVE_UNISTD_H)
#include <unistd.h>
#ifdef _SC_PAGESIZE
#define GNU_OUR_PAGESIZE sysconf(_SC_PAGESIZE)
#endif
#endif

#ifndef GNU_OUR_PAGESIZE
# ifdef	PAGESIZE
#  define	GNU_OUR_PAGESIZE PAGESIZE
# else	/* no PAGESIZE */
#  ifdef	EXEC_PAGESIZE
#   define	GNU_OUR_PAGESIZE EXEC_PAGESIZE
#  else	/* no EXEC_PAGESIZE */
#   ifdef	NBPG
#    define	GNU_OUR_PAGESIZE (NBPG * CLSIZE)
#    ifndef	CLSIZE
#     define	CLSIZE 1
#    endif	/* CLSIZE */
#   else	/* no NBPG */
#    ifdef	NBPC
#     define	GNU_OUR_PAGESIZE NBPC
#    else	/* no NBPC */
#     define	GNU_OUR_PAGESIZE 4096	/* Just punt and use reasonable value */
#    endif /* NBPC */
#   endif /* NBPG */
#  endif /* EXEC_PAGESIZE */
# endif /* PAGESIZE */
#endif /* GNU_OUR_PAGESIZE */

int
getpagesize (void)
{
  return (GNU_OUR_PAGESIZE);
}

#else /* VMS */

#if 0	/* older distributions of gcc-vms are missing <syidef.h> */
#include <syidef.h>
#endif
#ifndef SYI$_PAGE_SIZE	/* VMS V5.4 and earlier didn't have this yet */
#define SYI$_PAGE_SIZE 4452
#endif
extern unsigned long lib$getsyi(const unsigned short *,...);

int getpagesize (void)
{
  long pagsiz = 0L;
  unsigned short itmcod = SYI$_PAGE_SIZE;

  (void) lib$getsyi (&itmcod, (void *) &pagsiz);
  if (pagsiz == 0L)
    pagsiz = 512L;	/* VAX default */
  return (int) pagsiz;
}

#endif /* VMS */
