/* evconfig-private.h.  Generated from evconfig-private.h.in by configure.  */
/* evconfig-private.h template - see "Configuration Header Templates" */
/* in AC manual.  Kevin Bowling <kevin.bowling@kev009.com */
#ifndef EVCONFIG_PRIVATE_H_INCLUDED_
#define EVCONFIG_PRIVATE_H_INCLUDED_

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */
/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if on MINIX. */
#ifndef _MINIX
/* #undef _MINIX */
#endif

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
#ifndef _POSIX_1_SOURCE
/* #undef _POSIX_1_SOURCE */
#endif

/* Define to 1 if you need to in order for `stat' and other things to work. */
#ifndef _POSIX_SOURCE
/* #undef _POSIX_SOURCE */
#endif

#endif
