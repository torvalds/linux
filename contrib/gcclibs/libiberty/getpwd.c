/* getpwd.c - get the working directory */

/*

@deftypefn Supplemental char* getpwd (void)

Returns the current working directory.  This implementation caches the
result on the assumption that the process will not call @code{chdir}
between calls to @code{getpwd}.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_LIMITS_H
#include <limits.h>
#endif

#include "libiberty.h"

/* Virtually every UN*X system now in common use (except for pre-4.3-tahoe
   BSD systems) now provides getcwd as called for by POSIX.  Allow for
   the few exceptions to the general rule here.  */

#if !defined(HAVE_GETCWD) && defined(HAVE_GETWD)
/* Prototype in case the system headers doesn't provide it. */
extern char *getwd ();
#define getcwd(buf,len) getwd(buf)
#endif

#ifdef MAXPATHLEN
#define GUESSPATHLEN (MAXPATHLEN + 1)
#else
#define GUESSPATHLEN 100
#endif

#if !(defined (VMS) || (defined(_WIN32) && !defined(__CYGWIN__)))

/* Get the working directory.  Use the PWD environment variable if it's
   set correctly, since this is faster and gives more uniform answers
   to the user.  Yield the working directory if successful; otherwise,
   yield 0 and set errno.  */

char *
getpwd (void)
{
  static char *pwd;
  static int failure_errno;

  char *p = pwd;
  size_t s;
  struct stat dotstat, pwdstat;

  if (!p && !(errno = failure_errno))
    {
      if (! ((p = getenv ("PWD")) != 0
	     && *p == '/'
	     && stat (p, &pwdstat) == 0
	     && stat (".", &dotstat) == 0
	     && dotstat.st_ino == pwdstat.st_ino
	     && dotstat.st_dev == pwdstat.st_dev))

	/* The shortcut didn't work.  Try the slow, ``sure'' way.  */
	for (s = GUESSPATHLEN;  !getcwd (p = XNEWVEC (char, s), s);  s *= 2)
	  {
	    int e = errno;
	    free (p);
#ifdef ERANGE
	    if (e != ERANGE)
#endif
	      {
		errno = failure_errno = e;
		p = 0;
		break;
	      }
	  }

      /* Cache the result.  This assumes that the program does
	 not invoke chdir between calls to getpwd.  */
      pwd = p;
    }
  return p;
}

#else	/* VMS || _WIN32 && !__CYGWIN__ */

#ifndef MAXPATHLEN
#define MAXPATHLEN 255
#endif

char *
getpwd (void)
{
  static char *pwd = 0;

  if (!pwd)
    pwd = getcwd (XNEWVEC (char, MAXPATHLEN + 1), MAXPATHLEN + 1
#ifdef VMS
		  , 0
#endif
		  );
  return pwd;
}

#endif	/* VMS || _WIN32 && !__CYGWIN__ */
