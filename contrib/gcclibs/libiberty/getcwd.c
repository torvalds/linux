/* Emulate getcwd using getwd.
   This function is in the public domain. */

/*

@deftypefn Supplemental char* getcwd (char *@var{pathname}, int @var{len})

Copy the absolute pathname for the current working directory into
@var{pathname}, which is assumed to point to a buffer of at least
@var{len} bytes, and return a pointer to the buffer.  If the current
directory's path doesn't fit in @var{len} characters, the result is
@code{NULL} and @code{errno} is set.  If @var{pathname} is a null pointer,
@code{getcwd} will obtain @var{len} bytes of space using
@code{malloc}.

@end deftypefn

*/

#include "config.h"

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <errno.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

extern char *getwd ();
extern int errno;

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

char *
getcwd (char *buf, size_t len)
{
  char ourbuf[MAXPATHLEN];
  char *result;

  result = getwd (ourbuf);
  if (result) {
    if (strlen (ourbuf) >= len) {
      errno = ERANGE;
      return 0;
    }
    if (!buf) {
       buf = (char*)malloc(len);
       if (!buf) {
           errno = ENOMEM;
	   return 0;
       }
    }
    strcpy (buf, ourbuf);
  }
  return buf;
}
