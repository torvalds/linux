/*
 * config.h -- configure various defines for tcsh
 *
 * All source files should #include this FIRST.
 *
 * Edit this to match your system type.
 */

#ifndef _h_config
#define _h_config
/****************** System dependant compilation flags ****************/
/*
 * POSIX	This system supports IEEE Std 1003.1-1988 (POSIX).
 */
#define POSIX

/*
 * POSIXJOBS	This system supports the optional IEEE Std 1003.1-1988 (POSIX)
 *		job control facilities.
 */
#define POSIXJOBS

/*
 * VFORK	This machine has a vfork().  
 *		It used to be that for job control to work, this define
 *		was mandatory. This is not the case any more.
 *		If you think you still need it, but you don't have vfork, 
 *		define this anyway and then do #define vfork fork.  
 *		I do this anyway on a Sun because of yellow pages brain damage,
 *		[should not be needed under 4.1]
 *		and on the iris4d cause	SGI's fork is sufficiently "virtual" 
 *		that vfork isn't necessary.  (Besides, SGI's vfork is weird).
 *		Note that some machines eg. rs6000 have a vfork, but not
 *		with the berkeley semantics, so we cannot use it there either.
 */
#define VFORK

/*
 * BSDJOBS	You have BSD-style job control (both process groups and
 *		a tty that deals correctly
 */
#define BSDJOBS

/*
 * BSDTIMES	You have BSD-style process time stuff (like rusage)
 *		This may or may not be true.  For example, Apple Unix
 *		(OREO) has BSDJOBS but not BSDTIMES.
 */
#define BSDTIMES

/*
 * BSDLIMIT	You have BSD-style resource limit stuff (getrlimit/setrlimit)
 */
#define BSDLIMIT

/*
 * TERMIO	You have struct termio instead of struct sgttyb.
 * 		This is usually the case for SYSV systems, where
 *		BSD uses sgttyb. POSIX systems should define this
 *		anyway, even though they use struct termios.
 */
#define TERMIO

/*
 * SYSVREL	Your machine is SYSV based (HPUX, A/UX)
 *		NOTE: don't do this if you are on a Pyramid -- tcsh is
 *		built in a BSD universe.
 *		Set SYSVREL to 1, 2, 3, or 4, depending the version of System V
 *		you are running. Or set it to 0 if you are not SYSV based
 */
#define SYSVREL	0

/*
 * YPBUGS	Work around Sun YP bugs that cause expansion of ~username
 *		to send command output to /dev/null
 */
#undef YPBUGS

/****************** local defines *********************/

#if defined(__FreeBSD__)
#define NLS_BUGS
#define BSD_STYLE_COLORLS
#endif

#if defined(__bsdi__)
/*
 * _PATH_TCSHELL      if you've change the installation location (vix)
 */
#include <sys/param.h>
# ifdef _BSDI_VERSION >= 199701
#  define _PATH_TCSHELL "/bin/tcsh"
#  undef SYSMALLOC
#  define SYSMALLOC
# else
#  define _PATH_TCSHELL "/usr/contrib/bin/tcsh"
# endif

#elif defined(__APPLE__)
# define SYSMALLOC
# define BSD_STYLE_COLORLS
#endif

#endif /* _h_config */
