/* Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "config.h"

#ifndef HAVE_SOCKLEN_T
#define socklen_t int
#endif

/* These attempt to coax various unix flavours to declare all our
   needed tidbits in the system headers.  */
#if !defined(__FreeBSD__) && !defined(__APPLE__)
#define _POSIX_SOURCE
#endif /* Some BSDs break <sys/socket.h> if this is defined. */
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define _BSD_TYPES
#define __EXTENSIONS__
#define _ALL_SOURCE
#define _LARGE_FILE_API
#define _LARGEFILE64_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifdef HAVE_SYS_SEM_H
#include <sys/sem.h>
#endif
#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "mf-runtime.h"
#include "mf-impl.h"

#ifdef _MUDFLAP
#error "Do not compile this file with -fmudflap!"
#endif


/* A bunch of independent stdlib/unistd hook functions, all
   intercepted by mf-runtime.h macros.  */

#ifndef HAVE_STRNLEN
static inline size_t (strnlen) (const char* str, size_t n)
{
  const char *s;

  for (s = str; n && *s; ++s, --n)
    ;
  return (s - str);
}
#endif


/* str*,mem*,b* */

WRAPPER2(void *, memcpy, void *dest, const void *src, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(src, n, __MF_CHECK_READ, "memcpy source");
  MF_VALIDATE_EXTENT(dest, n, __MF_CHECK_WRITE, "memcpy dest");
  return memcpy (dest, src, n);
}


WRAPPER2(void *, memmove, void *dest, const void *src, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(src, n, __MF_CHECK_READ, "memmove src");
  MF_VALIDATE_EXTENT(dest, n, __MF_CHECK_WRITE, "memmove dest");
  return memmove (dest, src, n);
}


WRAPPER2(void *, memset, void *s, int c, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, n, __MF_CHECK_WRITE, "memset dest");
  return memset (s, c, n);
}


WRAPPER2(int, memcmp, const void *s1, const void *s2, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s1, n, __MF_CHECK_READ, "memcmp 1st arg");
  MF_VALIDATE_EXTENT(s2, n, __MF_CHECK_READ, "memcmp 2nd arg");
  return memcmp (s1, s2, n);
}


WRAPPER2(void *, memchr, const void *s, int c, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, n, __MF_CHECK_READ, "memchr region");
  return memchr (s, c, n);
}


#ifdef HAVE_MEMRCHR
WRAPPER2(void *, memrchr, const void *s, int c, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, n, __MF_CHECK_READ, "memrchr region");
  return memrchr (s, c, n);
}
#endif


WRAPPER2(char *, strcpy, char *dest, const char *src)
{
  /* nb: just because strlen(src) == n doesn't mean (src + n) or (src + n +
     1) are valid pointers. the allocated object might have size < n.
     check anyways. */

  size_t n = strlen (src);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(src, CLAMPADD(n, 1), __MF_CHECK_READ, "strcpy src");
  MF_VALIDATE_EXTENT(dest, CLAMPADD(n, 1), __MF_CHECK_WRITE, "strcpy dest");
  return strcpy (dest, src);
}


#ifdef HAVE_STRNCPY
WRAPPER2(char *, strncpy, char *dest, const char *src, size_t n)
{
  size_t len = strnlen (src, n);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(src, len, __MF_CHECK_READ, "strncpy src");
  MF_VALIDATE_EXTENT(dest, len, __MF_CHECK_WRITE, "strncpy dest"); /* nb: strNcpy */
  return strncpy (dest, src, n);
}
#endif


WRAPPER2(char *, strcat, char *dest, const char *src)
{
  size_t dest_sz;
  size_t src_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  dest_sz = strlen (dest);
  src_sz = strlen (src);
  MF_VALIDATE_EXTENT(src, CLAMPADD(src_sz, 1), __MF_CHECK_READ, "strcat src");
  MF_VALIDATE_EXTENT(dest, CLAMPADD(dest_sz, CLAMPADD(src_sz, 1)),
		     __MF_CHECK_WRITE, "strcat dest");
  return strcat (dest, src);
}


WRAPPER2(char *, strncat, char *dest, const char *src, size_t n)
{

  /* nb: validating the extents (s,n) might be a mistake for two reasons.

  (1) the string s might be shorter than n chars, and n is just a
  poor choice by the programmer. this is not a "true" error in the
  sense that the call to strncat would still be ok.

  (2) we could try to compensate for case (1) by calling strlen(s) and
  using that as a bound for the extent to verify, but strlen might fall off
  the end of a non-terminated string, leading to a false positive.

  so we will call strnlen(s,n) and use that as a bound.

  if strnlen returns a length beyond the end of the registered extent
  associated with s, there is an error: the programmer's estimate for n is
  too large _AND_ the string s is unterminated, in which case they'd be
  about to touch memory they don't own while calling strncat.

  this same logic applies to further uses of strnlen later down in this
  file. */

  size_t src_sz;
  size_t dest_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  src_sz = strnlen (src, n);
  dest_sz = strnlen (dest, n);
  MF_VALIDATE_EXTENT(src, src_sz, __MF_CHECK_READ, "strncat src");
  MF_VALIDATE_EXTENT(dest, (CLAMPADD(dest_sz, CLAMPADD(src_sz, 1))),
		     __MF_CHECK_WRITE, "strncat dest");
  return strncat (dest, src, n);
}


WRAPPER2(int, strcmp, const char *s1, const char *s2)
{
  size_t s1_sz;
  size_t s2_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  s1_sz = strlen (s1);
  s2_sz = strlen (s2);
  MF_VALIDATE_EXTENT(s1, CLAMPADD(s1_sz, 1), __MF_CHECK_READ, "strcmp 1st arg");
  MF_VALIDATE_EXTENT(s2, CLAMPADD(s2_sz, 1), __MF_CHECK_WRITE, "strcmp 2nd arg");
  return strcmp (s1, s2);
}


WRAPPER2(int, strcasecmp, const char *s1, const char *s2)
{
  size_t s1_sz;
  size_t s2_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  s1_sz = strlen (s1);
  s2_sz = strlen (s2);
  MF_VALIDATE_EXTENT(s1, CLAMPADD(s1_sz, 1), __MF_CHECK_READ, "strcasecmp 1st arg");
  MF_VALIDATE_EXTENT(s2, CLAMPADD(s2_sz, 1), __MF_CHECK_READ, "strcasecmp 2nd arg");
  return strcasecmp (s1, s2);
}


WRAPPER2(int, strncmp, const char *s1, const char *s2, size_t n)
{
  size_t s1_sz;
  size_t s2_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  s1_sz = strnlen (s1, n);
  s2_sz = strnlen (s2, n);
  MF_VALIDATE_EXTENT(s1, s1_sz, __MF_CHECK_READ, "strncmp 1st arg");
  MF_VALIDATE_EXTENT(s2, s2_sz, __MF_CHECK_READ, "strncmp 2nd arg");
  return strncmp (s1, s2, n);
}


WRAPPER2(int, strncasecmp, const char *s1, const char *s2, size_t n)
{
  size_t s1_sz;
  size_t s2_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  s1_sz = strnlen (s1, n);
  s2_sz = strnlen (s2, n);
  MF_VALIDATE_EXTENT(s1, s1_sz, __MF_CHECK_READ, "strncasecmp 1st arg");
  MF_VALIDATE_EXTENT(s2, s2_sz, __MF_CHECK_READ, "strncasecmp 2nd arg");
  return strncasecmp (s1, s2, n);
}


WRAPPER2(char *, strdup, const char *s)
{
  DECLARE(void *, malloc, size_t sz);
  char *result;
  size_t n = strlen (s);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, CLAMPADD(n,1), __MF_CHECK_READ, "strdup region");
  result = (char *)CALL_REAL(malloc,
			     CLAMPADD(CLAMPADD(n,1),
				      CLAMPADD(__mf_opts.crumple_zone,
					       __mf_opts.crumple_zone)));

  if (UNLIKELY(! result)) return result;

  result += __mf_opts.crumple_zone;
  memcpy (result, s, n);
  result[n] = '\0';

  __mf_register (result, CLAMPADD(n,1), __MF_TYPE_HEAP_I, "strdup region");
  return result;
}


WRAPPER2(char *, strndup, const char *s, size_t n)
{
  DECLARE(void *, malloc, size_t sz);
  char *result;
  size_t sz = strnlen (s, n);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, sz, __MF_CHECK_READ, "strndup region"); /* nb: strNdup */

  /* note: strndup still adds a \0, even with the N limit! */
  result = (char *)CALL_REAL(malloc,
			     CLAMPADD(CLAMPADD(n,1),
				      CLAMPADD(__mf_opts.crumple_zone,
					       __mf_opts.crumple_zone)));

  if (UNLIKELY(! result)) return result;

  result += __mf_opts.crumple_zone;
  memcpy (result, s, n);
  result[n] = '\0';

  __mf_register (result, CLAMPADD(n,1), __MF_TYPE_HEAP_I, "strndup region");
  return result;
}


WRAPPER2(char *, strchr, const char *s, int c)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (s);
  MF_VALIDATE_EXTENT(s, CLAMPADD(n,1), __MF_CHECK_READ, "strchr region");
  return strchr (s, c);
}


WRAPPER2(char *, strrchr, const char *s, int c)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (s);
  MF_VALIDATE_EXTENT(s, CLAMPADD(n,1), __MF_CHECK_READ, "strrchr region");
  return strrchr (s, c);
}


WRAPPER2(char *, strstr, const char *haystack, const char *needle)
{
  size_t haystack_sz;
  size_t needle_sz;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  haystack_sz = strlen (haystack);
  needle_sz = strlen (needle);
  MF_VALIDATE_EXTENT(haystack, CLAMPADD(haystack_sz, 1), __MF_CHECK_READ, "strstr haystack");
  MF_VALIDATE_EXTENT(needle, CLAMPADD(needle_sz, 1), __MF_CHECK_READ, "strstr needle");
  return strstr (haystack, needle);
}


#ifdef HAVE_MEMMEM
WRAPPER2(void *, memmem,
	const void *haystack, size_t haystacklen,
	const void *needle, size_t needlelen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(haystack, haystacklen, __MF_CHECK_READ, "memmem haystack");
  MF_VALIDATE_EXTENT(needle, needlelen, __MF_CHECK_READ, "memmem needle");
  return memmem (haystack, haystacklen, needle, needlelen);
}
#endif


WRAPPER2(size_t, strlen, const char *s)
{
  size_t result = strlen (s);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, CLAMPADD(result, 1), __MF_CHECK_READ, "strlen region");
  return result;
}


WRAPPER2(size_t, strnlen, const char *s, size_t n)
{
  size_t result = strnlen (s, n);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, result, __MF_CHECK_READ, "strnlen region");
  return result;
}


WRAPPER2(void, bzero, void *s, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, n, __MF_CHECK_WRITE, "bzero region");
  bzero (s, n);
}


#undef bcopy
WRAPPER2(void, bcopy, const void *src, void *dest, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(src, n, __MF_CHECK_READ, "bcopy src");
  MF_VALIDATE_EXTENT(dest, n, __MF_CHECK_WRITE, "bcopy dest");
  bcopy (src, dest, n);
}


#undef bcmp
WRAPPER2(int, bcmp, const void *s1, const void *s2, size_t n)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s1, n, __MF_CHECK_READ, "bcmp 1st arg");
  MF_VALIDATE_EXTENT(s2, n, __MF_CHECK_READ, "bcmp 2nd arg");
  return bcmp (s1, s2, n);
}


WRAPPER2(char *, index, const char *s, int c)
{
  size_t n = strlen (s);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, CLAMPADD(n, 1), __MF_CHECK_READ, "index region");
  return index (s, c);
}


WRAPPER2(char *, rindex, const char *s, int c)
{
  size_t n = strlen (s);
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(s, CLAMPADD(n, 1), __MF_CHECK_READ, "rindex region");
  return rindex (s, c);
}

/* XXX:  stpcpy, memccpy */

/* XXX: *printf,*scanf */

/* XXX: setjmp, longjmp */

WRAPPER2(char *, asctime, struct tm *tm)
{
  static char *reg_result = NULL;
  char *result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(tm, sizeof (struct tm), __MF_CHECK_READ, "asctime tm");
  result = asctime (tm);
  if (reg_result == NULL)
    {
      __mf_register (result, strlen (result)+1, __MF_TYPE_STATIC, "asctime string");
      reg_result = result;
    }
  return result;
}


WRAPPER2(char *, ctime, const time_t *timep)
{
  static char *reg_result = NULL;
  char *result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(timep, sizeof (time_t), __MF_CHECK_READ, "ctime time");
  result = ctime (timep);
  if (reg_result == NULL)
    {
      /* XXX: what if asctime and ctime return the same static ptr? */
      __mf_register (result, strlen (result)+1, __MF_TYPE_STATIC, "ctime string");
      reg_result = result;
    }
  return result;
}


WRAPPER2(struct tm*, localtime, const time_t *timep)
{
  static struct tm *reg_result = NULL;
  struct tm *result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(timep, sizeof (time_t), __MF_CHECK_READ, "localtime time");
  result = localtime (timep);
  if (reg_result == NULL)
    {
      __mf_register (result, sizeof (struct tm), __MF_TYPE_STATIC, "localtime tm");
      reg_result = result;
    }
  return result;
}


WRAPPER2(struct tm*, gmtime, const time_t *timep)
{
  static struct tm *reg_result = NULL;
  struct tm *result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT(timep, sizeof (time_t), __MF_CHECK_READ, "gmtime time");
  result = gmtime (timep);
  if (reg_result == NULL)
    {
      __mf_register (result, sizeof (struct tm), __MF_TYPE_STATIC, "gmtime tm");
      reg_result = result;
    }
  return result;
}


/* EL start */

/* The following indicate if the result of the corresponding function
 * should be explicitly un/registered by the wrapper
*/

#ifdef __FreeBSD__
#define MF_REGISTER_fopen		__MF_TYPE_STATIC
#else
#undef  MF_REGISTER_fopen
#endif
#define MF_RESULT_SIZE_fopen		(sizeof (FILE))

#undef  MF_REGISTER_opendir
#define MF_RESULT_SIZE_opendir		0	/* (sizeof (DIR)) */
#undef  MF_REGISTER_readdir
#define MF_REGISTER_gethostbyname	__MF_TYPE_STATIC
#undef  MF_REGISTER_gethostbyname_items
#undef  MF_REGISTER_dlopen
#undef  MF_REGISTER_dlerror
#undef  MF_REGISTER_dlsym
#define MF_REGISTER_shmat		__MF_TYPE_GUESS


#include <time.h>
WRAPPER2(time_t, time, time_t *timep)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  if (NULL != timep)
    MF_VALIDATE_EXTENT (timep, sizeof (*timep), __MF_CHECK_WRITE,
      "time timep");
  return time (timep);
}


WRAPPER2(char *, strerror, int errnum)
{
  char *p;
  static char * last_strerror = NULL;

  TRACE ("%s\n", __PRETTY_FUNCTION__);
  p = strerror (errnum);
  if (last_strerror != NULL)
    __mf_unregister (last_strerror, 0, __MF_TYPE_STATIC);
  if (NULL != p)
    __mf_register (p, strlen (p) + 1, __MF_TYPE_STATIC, "strerror result");
  last_strerror = p;
  return p;
}



/* An auxiliary data structure for tracking the hand-made stdio
   buffers we generate during the fopen/fopen64 hooks.  In a civilized
   language, this would be a simple dynamically sized FILE*->char*
   lookup table, but this is C and we get to do it by hand.  */
struct mf_filebuffer
{
  FILE *file;
  char *buffer;
  struct mf_filebuffer *next;
};
static struct mf_filebuffer *mf_filebuffers = NULL;

static void
mkbuffer (FILE *f)
{
  /* Reset any buffer automatically provided by libc, since this may
     have been done via mechanisms that libmudflap couldn't
     intercept.  */
  int rc;
  size_t bufsize = BUFSIZ;
  int bufmode;
  char *buffer = malloc (bufsize);
  struct mf_filebuffer *b = malloc (sizeof (struct mf_filebuffer));
  assert ((buffer != NULL) && (b != NULL));

  /* Link it into list.  */
  b->file = f;
  b->buffer = buffer;
  b->next = mf_filebuffers;
  mf_filebuffers = b;

  /* Determine how the file is supposed to be buffered at the moment.  */
  bufmode = fileno (f) == 2 ? _IONBF : (isatty (fileno (f)) ? _IOLBF : _IOFBF);

  rc = setvbuf (f, buffer, bufmode, bufsize);
  assert (rc == 0);
}

static void
unmkbuffer (FILE *f)
{
  struct mf_filebuffer *b = mf_filebuffers;
  struct mf_filebuffer **pb = & mf_filebuffers;
  while (b != NULL)
    {
      if (b->file == f)
        {
          *pb = b->next;
          free (b->buffer);
          free (b);
          return;
        }
      pb = & b->next;
      b = b->next;
    }
}



WRAPPER2(FILE *, fopen, const char *path, const char *mode)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "fopen path");

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "fopen mode");

  p = fopen (path, mode);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "fopen result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "fopen result");

    mkbuffer (p);
  }

  return p;
}


WRAPPER2(int, setvbuf, FILE *stream, char *buf, int mode, size_t size)
{
  int rc = 0;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE, "setvbuf stream");

  unmkbuffer (stream);

  if (buf != NULL)
    MF_VALIDATE_EXTENT (buf, size, __MF_CHECK_WRITE, "setvbuf buffer");

  /* Override the user only if it's an auto-allocated buffer request.  Otherwise
     assume that the supplied buffer is already known to libmudflap.  */
  if ((buf == NULL) && ((mode == _IOFBF) || (mode == _IOLBF)))
    mkbuffer (stream);
  else
    rc = setvbuf (stream, buf, mode, size);

  return rc;
}


#ifdef HAVE_SETBUF
WRAPPER2(int, setbuf, FILE* stream, char *buf)
{
  return __mfwrap_setvbuf (stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}
#endif

#ifdef HAVE_SETBUFFER
WRAPPER2(int, setbuffer, FILE* stream, char *buf, size_t sz)
{
  return __mfwrap_setvbuf (stream, buf, buf ? _IOFBF : _IONBF, sz);
}
#endif

#ifdef HAVE_SETLINEBUF
WRAPPER2(int, setlinebuf, FILE* stream)
{
  return __mfwrap_setvbuf(stream, NULL, _IOLBF, 0);
}
#endif



WRAPPER2(FILE *, fdopen, int fd, const char *mode)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "fdopen mode");

  p = fdopen (fd, mode);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "fdopen result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "fdopen result");

    mkbuffer (p);
  }

  return p;
}


WRAPPER2(FILE *, freopen, const char *path, const char *mode, FILE *s)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "freopen path");

  MF_VALIDATE_EXTENT (s, (sizeof (*s)), __MF_CHECK_WRITE, "freopen stream");
  unmkbuffer (s);

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "freopen mode");

  p = freopen (path, mode, s);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "freopen result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "freopen result");

    mkbuffer (p);
  }

  return p;
}


#ifdef HAVE_FOPEN64
WRAPPER2(FILE *, fopen64, const char *path, const char *mode)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "fopen64 path");

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "fopen64 mode");

  p = fopen64 (path, mode);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "fopen64 result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "fopen64 result");

    mkbuffer (p);
  }

  return p;
}
#endif


#ifdef HAVE_FREOPEN64
WRAPPER2(FILE *, freopen64, const char *path, const char *mode, FILE *s)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "freopen64 path");

  MF_VALIDATE_EXTENT (s, (sizeof (*s)), __MF_CHECK_WRITE, "freopen64 stream");
  unmkbuffer (s);

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "freopen64 mode");

  p = freopen (path, mode, s);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "freopen64 result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "freopen64 result");

    mkbuffer (p);
  }

  return p;
}
#endif


WRAPPER2(int, fclose, FILE *stream)
{
  int resp;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fclose stream");
  resp = fclose (stream);
#ifdef MF_REGISTER_fopen
  __mf_unregister (stream, sizeof (*stream), MF_REGISTER_fopen);
#endif
  unmkbuffer (stream);

  return resp;
}


WRAPPER2(size_t, fread, void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fread stream");
  MF_VALIDATE_EXTENT (ptr, size * nmemb, __MF_CHECK_WRITE, "fread buffer");
  return fread (ptr, size, nmemb, stream);
}


WRAPPER2(size_t, fwrite, const void *ptr, size_t size, size_t nmemb,
	FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fwrite stream");
  MF_VALIDATE_EXTENT (ptr, size * nmemb, __MF_CHECK_READ, "fwrite buffer");
  return fwrite (ptr, size, nmemb, stream);
}


WRAPPER2(int, fgetc, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fgetc stream");
  return fgetc (stream);
}


WRAPPER2(char *, fgets, char *s, int size, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fgets stream");
  MF_VALIDATE_EXTENT (s, size, __MF_CHECK_WRITE, "fgets buffer");
  return fgets (s, size, stream);
}


WRAPPER2(int, getc, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "getc stream");
  return getc (stream);
}


WRAPPER2(char *, gets, char *s)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (s, 1, __MF_CHECK_WRITE, "gets buffer");
  /* Avoid link-time warning... */
  s = fgets (s, INT_MAX, stdin);
  if (NULL != s) {	/* better late than never */
    size_t n = strlen (s);
    MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_WRITE, "gets buffer");
  }
  return s;
}


WRAPPER2(int, ungetc, int c, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
     "ungetc stream");
  return ungetc (c, stream);
}


WRAPPER2(int, fputc, int c, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fputc stream");
  return fputc (c, stream);
}


WRAPPER2(int, fputs, const char *s, FILE *stream)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (s);
  MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "fputs buffer");
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fputs stream");
  return fputs (s, stream);
}


WRAPPER2(int, putc, int c, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "putc stream");
  return putc (c, stream);
}


WRAPPER2(int, puts, const char *s)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (s);
  MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "puts buffer");
  return puts (s);
}


WRAPPER2(void, clearerr, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "clearerr stream");
  clearerr (stream);
}


WRAPPER2(int, feof, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "feof stream");
  return feof (stream);
}


WRAPPER2(int, ferror, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "ferror stream");
  return ferror (stream);
}


WRAPPER2(int, fileno, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fileno stream");
  return fileno (stream);
}


WRAPPER2(int, printf, const char *format, ...)
{
  size_t n;
  va_list ap;
  int result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "printf format");
  va_start (ap, format);
  result = vprintf (format, ap);
  va_end (ap);
  return result;
}


WRAPPER2(int, fprintf, FILE *stream, const char *format, ...)
{
  size_t n;
  va_list ap;
  int result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fprintf stream");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "fprintf format");
  va_start (ap, format);
  result = vfprintf (stream, format, ap);
  va_end (ap);
  return result;
}


WRAPPER2(int, sprintf, char *str, const char *format, ...)
{
  size_t n;
  va_list ap;
  int result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (str, 1, __MF_CHECK_WRITE, "sprintf str");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "sprintf format");
  va_start (ap, format);
  result = vsprintf (str, format, ap);
  va_end (ap);
  n = strlen (str);
  MF_VALIDATE_EXTENT (str, CLAMPADD(n, 1), __MF_CHECK_WRITE, "sprintf str");
  return result;
}


WRAPPER2(int, snprintf, char *str, size_t size, const char *format, ...)
{
  size_t n;
  va_list ap;
  int result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (str, size, __MF_CHECK_WRITE, "snprintf str");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "snprintf format");
  va_start (ap, format);
  result = vsnprintf (str, size, format, ap);
  va_end (ap);
  return result;
}


WRAPPER2(int, vprintf,  const char *format, va_list ap)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "vprintf format");
  return vprintf (format, ap);
}


WRAPPER2(int, vfprintf, FILE *stream, const char *format, va_list ap)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "vfprintf stream");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "vfprintf format");
  return vfprintf (stream, format, ap);
}


WRAPPER2(int, vsprintf, char *str, const char *format, va_list ap)
{
  size_t n;
  int result;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (str, 1, __MF_CHECK_WRITE, "vsprintf str");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "vsprintf format");
  result = vsprintf (str, format, ap);
  n = strlen (str);
  MF_VALIDATE_EXTENT (str, CLAMPADD(n, 1), __MF_CHECK_WRITE, "vsprintf str");
  return result;
}


WRAPPER2(int, vsnprintf, char *str, size_t size, const char *format,
	va_list ap)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (str, size, __MF_CHECK_WRITE, "vsnprintf str");
  n = strlen (format);
  MF_VALIDATE_EXTENT (format, CLAMPADD(n, 1), __MF_CHECK_READ,
    "vsnprintf format");
  return vsnprintf (str, size, format, ap);
}


WRAPPER2(int , access, const char *path, int mode)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "access path");
  return access (path, mode);
}


WRAPPER2(int , remove, const char *path)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "remove path");
  return remove (path);
}


WRAPPER2(int, fflush, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  if (stream != NULL)
    MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
                        "fflush stream");
  return fflush (stream);
}


WRAPPER2(int, fseek, FILE *stream, long offset, int whence)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fseek stream");
  return fseek (stream, offset, whence);
}


#ifdef HAVE_FSEEKO64
WRAPPER2(int, fseeko64, FILE *stream, off64_t offset, int whence)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fseeko64 stream");
  return fseeko64 (stream, offset, whence);
}
#endif


WRAPPER2(long, ftell, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "ftell stream");
  return ftell (stream);
}


#ifdef HAVE_FTELLO64
WRAPPER2(off64_t, ftello64, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "ftello64 stream");
  return ftello64 (stream);
}
#endif


WRAPPER2(void, rewind, FILE *stream)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "rewind stream");
  rewind (stream);
}


WRAPPER2(int, fgetpos, FILE *stream, fpos_t *pos)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fgetpos stream");
  MF_VALIDATE_EXTENT (pos, sizeof (*pos), __MF_CHECK_WRITE, "fgetpos pos");
  return fgetpos (stream, pos);
}


WRAPPER2(int, fsetpos, FILE *stream, fpos_t *pos)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "fsetpos stream");
  MF_VALIDATE_EXTENT (pos, sizeof (*pos), __MF_CHECK_READ, "fsetpos pos");
  return fsetpos (stream, pos);
}


WRAPPER2(int , stat, const char *path, struct stat *buf)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "stat path");
  MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_READ, "stat buf");
  return stat (path, buf);
}


#ifdef HAVE_STAT64
WRAPPER2(int , stat64, const char *path, struct stat64 *buf)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "stat64 path");
  MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_READ, "stat64 buf");
  return stat64 (path, buf);
}
#endif


WRAPPER2(int , fstat, int filedes, struct stat *buf)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_READ, "fstat buf");
  return fstat (filedes, buf);
}


WRAPPER2(int , lstat, const char *path, struct stat *buf)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "lstat path");
  MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_READ, "lstat buf");
  return lstat (path, buf);
}


WRAPPER2(int , mkfifo, const char *path, mode_t mode)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "mkfifo path");
  return mkfifo (path, mode);
}


#ifdef HAVE_DIRENT_H
WRAPPER2(DIR *, opendir, const char *path)
{
  DIR *p;
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "opendir path");

  p = opendir (path);
  if (NULL != p) {
#ifdef MF_REGISTER_opendir
    __mf_register (p, MF_RESULT_SIZE_opendir, MF_REGISTER_opendir,
      "opendir result");
#endif
    MF_VALIDATE_EXTENT (p, MF_RESULT_SIZE_opendir, __MF_CHECK_WRITE,
      "opendir result");
  }
  return p;
}


WRAPPER2(int, closedir, DIR *dir)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (dir, 0, __MF_CHECK_WRITE, "closedir dir");
#ifdef MF_REGISTER_opendir
  __mf_unregister (dir, MF_RESULT_SIZE_opendir, MF_REGISTER_opendir);
#endif
  return closedir (dir);
}


WRAPPER2(struct dirent *, readdir, DIR *dir)
{
  struct dirent *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (dir, 0, __MF_CHECK_READ, "readdir dir");
  p = readdir (dir);
  if (NULL != p) {
#ifdef MF_REGISTER_readdir
    __mf_register (p, sizeof (*p), MF_REGISTER_readdir, "readdir result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "readdir result");
  }
  return p;
}
#endif


#ifdef HAVE_SYS_SOCKET_H

WRAPPER2(int, recv, int s, void *buf, size_t len, int flags)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (buf, len, __MF_CHECK_WRITE, "recv buf");
  return recv (s, buf, len, flags);
}


WRAPPER2(int, recvfrom, int s, void *buf, size_t len, int flags,
		struct sockaddr *from, socklen_t *fromlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (buf, len, __MF_CHECK_WRITE, "recvfrom buf");
  MF_VALIDATE_EXTENT (from, (size_t)*fromlen, __MF_CHECK_WRITE,
    "recvfrom from");
  return recvfrom (s, buf, len, flags, from, fromlen);
}


WRAPPER2(int, recvmsg, int s, struct msghdr *msg, int flags)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (msg, sizeof (*msg), __MF_CHECK_WRITE, "recvmsg msg");
  return recvmsg (s, msg, flags);
}


WRAPPER2(int, send, int s, const void *msg, size_t len, int flags)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (msg, len, __MF_CHECK_READ, "send msg");
  return send (s, msg, len, flags);
}


WRAPPER2(int, sendto, int s, const void *msg, size_t len, int flags,
		const struct sockaddr *to, socklen_t tolen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (msg, len, __MF_CHECK_READ, "sendto msg");
  MF_VALIDATE_EXTENT (to, (size_t)tolen, __MF_CHECK_WRITE, "sendto to");
  return sendto (s, msg, len, flags, to, tolen);
}


WRAPPER2(int, sendmsg, int s, const void *msg, int flags)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (msg, sizeof (*msg), __MF_CHECK_READ, "sendmsg msg");
  return sendmsg (s, msg, flags);
}


WRAPPER2(int, setsockopt, int s, int level, int optname, const void *optval,
	socklen_t optlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (optval, (size_t)optlen, __MF_CHECK_READ,
    "setsockopt optval");
  return setsockopt (s, level, optname, optval, optlen);
}


WRAPPER2(int, getsockopt, int s, int level, int optname, void *optval,
		socklen_t *optlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (optval, (size_t)*optlen, __MF_CHECK_WRITE,
    "getsockopt optval");
  return getsockopt (s, level, optname, optval, optlen);
}


WRAPPER2(int, accept, int s, struct  sockaddr *addr, socklen_t *addrlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  if (addr != NULL)
    MF_VALIDATE_EXTENT (addr, (size_t)*addrlen, __MF_CHECK_WRITE, "accept addr");
  return accept (s, addr, addrlen);
}


WRAPPER2(int, bind, int sockfd, struct  sockaddr *addr, socklen_t addrlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (addr, (size_t)addrlen, __MF_CHECK_WRITE, "bind addr");
  return bind (sockfd, addr, addrlen);
}


WRAPPER2(int, connect, int sockfd, const struct sockaddr  *addr,
	socklen_t addrlen)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (addr, (size_t)addrlen, __MF_CHECK_READ,
    "connect addr");
  return connect (sockfd, addr, addrlen);
}

#endif /* HAVE_SYS_SOCKET_H */


WRAPPER2(int, gethostname, char *name, size_t len)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (name, len, __MF_CHECK_WRITE, "gethostname name");
  return gethostname (name, len);
}


#ifdef HAVE_SETHOSTNAME
WRAPPER2(int, sethostname, const char *name, size_t len)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (name, len, __MF_CHECK_READ, "sethostname name");
  return sethostname (name, len);
}
#endif


#ifdef HAVE_NETDB_H

WRAPPER2(struct hostent *, gethostbyname, const char *name)
{
  struct hostent *p;
  char **ss;
  char *s;
  size_t n;
  int nreg;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (name);
  MF_VALIDATE_EXTENT (name, CLAMPADD(n, 1), __MF_CHECK_READ,
    "gethostbyname name");
  p = gethostbyname (name);
  if (NULL != p) {
#ifdef MF_REGISTER_gethostbyname
    __mf_register (p, sizeof (*p), MF_REGISTER_gethostbyname,
      "gethostbyname result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE,
      "gethostbyname result");
    if (NULL != (s = p->h_name)) {
      n = strlen (s);
      n = CLAMPADD(n, 1);
#ifdef MF_REGISTER_gethostbyname_items
      __mf_register (s, n, MF_REGISTER_gethostbyname_items,
        "gethostbyname result->h_name");
#endif
      MF_VALIDATE_EXTENT (s, n, __MF_CHECK_WRITE,
        "gethostbyname result->h_name");
    }

    if (NULL != (ss = p->h_aliases)) {
      for (nreg = 1;; ++nreg) {
        s = *ss++;
        if (NULL == s)
          break;
        n = strlen (s);
        n = CLAMPADD(n, 1);
#ifdef MF_REGISTER_gethostbyname_items
        __mf_register (s, n, MF_REGISTER_gethostbyname_items,
          "gethostbyname result->h_aliases[]");
#endif
        MF_VALIDATE_EXTENT (s, n, __MF_CHECK_WRITE,
          "gethostbyname result->h_aliases[]");
      }
      nreg *= sizeof (*p->h_aliases);
#ifdef MF_REGISTER_gethostbyname_items
      __mf_register (p->h_aliases, nreg, MF_REGISTER_gethostbyname_items,
        "gethostbyname result->h_aliases");
#endif
      MF_VALIDATE_EXTENT (p->h_aliases, nreg, __MF_CHECK_WRITE,
        "gethostbyname result->h_aliases");
    }

    if (NULL != (ss = p->h_addr_list)) {
      for (nreg = 1;; ++nreg) {
        s = *ss++;
        if (NULL == s)
          break;
#ifdef MF_REGISTER_gethostbyname_items
        __mf_register (s, p->h_length, MF_REGISTER_gethostbyname_items,
          "gethostbyname result->h_addr_list[]");
#endif
        MF_VALIDATE_EXTENT (s, p->h_length, __MF_CHECK_WRITE,
          "gethostbyname result->h_addr_list[]");
      }
      nreg *= sizeof (*p->h_addr_list);
#ifdef MF_REGISTER_gethostbyname_items
      __mf_register (p->h_addr_list, nreg, MF_REGISTER_gethostbyname_items,
        "gethostbyname result->h_addr_list");
#endif
      MF_VALIDATE_EXTENT (p->h_addr_list, nreg, __MF_CHECK_WRITE,
        "gethostbyname result->h_addr_list");
    }
  }
  return p;
}

#endif /* HAVE_NETDB_H */


#ifdef HAVE_SYS_WAIT_H

WRAPPER2(pid_t, wait, int *status)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  if (NULL != status)
    MF_VALIDATE_EXTENT (status, sizeof (*status), __MF_CHECK_WRITE,
      "wait status");
  return wait (status);
}


WRAPPER2(pid_t, waitpid, pid_t pid, int *status, int options)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  if (NULL != status)
    MF_VALIDATE_EXTENT (status, sizeof (*status), __MF_CHECK_WRITE,
      "waitpid status");
  return waitpid (pid, status, options);
}

#endif /* HAVE_SYS_WAIT_H */


WRAPPER2(FILE *, popen, const char *command, const char *mode)
{
  size_t n;
  FILE *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (command);
  MF_VALIDATE_EXTENT (command, CLAMPADD(n, 1), __MF_CHECK_READ, "popen path");

  n = strlen (mode);
  MF_VALIDATE_EXTENT (mode, CLAMPADD(n, 1), __MF_CHECK_READ, "popen mode");

  p = popen (command, mode);
  if (NULL != p) {
#ifdef MF_REGISTER_fopen
    __mf_register (p, sizeof (*p), MF_REGISTER_fopen, "popen result");
#endif
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_WRITE, "popen result");
  }
  return p;
}


WRAPPER2(int, pclose, FILE *stream)
{
  int resp;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (stream, sizeof (*stream), __MF_CHECK_WRITE,
    "pclose stream");
  resp = pclose (stream);
#ifdef MF_REGISTER_fopen
  __mf_unregister (stream, sizeof (*stream), MF_REGISTER_fopen);
#endif
  return resp;
}


WRAPPER2(int, execve, const char *path, char *const argv [],
	char *const envp[])
{
  size_t n;
  char *const *p;
  const char *s;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "execve path");

  for (p = argv;;) {
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_READ, "execve *argv");
    s = *p++;
    if (NULL == s)
      break;
    n = strlen (s);
    MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "execve **argv");
  }

  for (p = envp;;) {
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_READ, "execve *envp");
    s = *p++;
    if (NULL == s)
      break;
    n = strlen (s);
    MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "execve **envp");
  }
  return execve (path, argv, envp);
}


WRAPPER2(int, execv, const char *path, char *const argv [])
{
  size_t n;
  char *const *p;
  const char *s;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "execv path");

  for (p = argv;;) {
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_READ, "execv *argv");
    s = *p++;
    if (NULL == s)
      break;
    n = strlen (s);
    MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "execv **argv");
  }
  return execv (path, argv);
}


WRAPPER2(int, execvp, const char *path, char *const argv [])
{
  size_t n;
  char *const *p;
  const char *s;
  TRACE ("%s\n", __PRETTY_FUNCTION__);

  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "execvp path");

  for (p = argv;;) {
    MF_VALIDATE_EXTENT (p, sizeof (*p), __MF_CHECK_READ, "execvp *argv");
    s = *p++;
    if (NULL == s)
      break;
    n = strlen (s);
    MF_VALIDATE_EXTENT (s, CLAMPADD(n, 1), __MF_CHECK_READ, "execvp **argv");
  }
  return execvp (path, argv);
}


WRAPPER2(int, system, const char *string)
{
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (string);
  MF_VALIDATE_EXTENT (string, CLAMPADD(n, 1), __MF_CHECK_READ,
    "system string");
  return system (string);
}


WRAPPER2(void *, dlopen, const char *path, int flags)
{
  void *p;
  size_t n;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  n = strlen (path);
  MF_VALIDATE_EXTENT (path, CLAMPADD(n, 1), __MF_CHECK_READ, "dlopen path");
  p = dlopen (path, flags);
  if (NULL != p) {
#ifdef MF_REGISTER_dlopen
    __mf_register (p, 0, MF_REGISTER_dlopen, "dlopen result");
#endif
    MF_VALIDATE_EXTENT (p, 0, __MF_CHECK_WRITE, "dlopen result");
  }
  return p;
}


WRAPPER2(int, dlclose, void *handle)
{
  int resp;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (handle, 0, __MF_CHECK_READ, "dlclose handle");
  resp = dlclose (handle);
#ifdef MF_REGISTER_dlopen
  __mf_unregister (handle, 0, MF_REGISTER_dlopen);
#endif
  return resp;
}


WRAPPER2(char *, dlerror)
{
  char *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  p = dlerror ();
  if (NULL != p) {
    size_t n;
    n = strlen (p);
    n = CLAMPADD(n, 1);
#ifdef MF_REGISTER_dlerror
    __mf_register (p, n, MF_REGISTER_dlerror, "dlerror result");
#endif
    MF_VALIDATE_EXTENT (p, n, __MF_CHECK_WRITE, "dlerror result");
  }
  return p;
}


WRAPPER2(void *, dlsym, void *handle, char *symbol)
{
  size_t n;
  void *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (handle, 0, __MF_CHECK_READ, "dlsym handle");
  n = strlen (symbol);
  MF_VALIDATE_EXTENT (symbol, CLAMPADD(n, 1), __MF_CHECK_READ, "dlsym symbol");
  p = dlsym (handle, symbol);
  if (NULL != p) {
#ifdef MF_REGISTER_dlsym
    __mf_register (p, 0, MF_REGISTER_dlsym, "dlsym result");
#endif
    MF_VALIDATE_EXTENT (p, 0, __MF_CHECK_WRITE, "dlsym result");
  }
  return p;
}


#if defined (HAVE_SYS_IPC_H) && defined (HAVE_SYS_SEM_H) && defined (HAVE_SYS_SHM_H)

WRAPPER2(int, semop, int semid, struct sembuf *sops, unsigned nsops)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  MF_VALIDATE_EXTENT (sops, sizeof (*sops) * nsops, __MF_CHECK_READ,
    "semop sops");
  return semop (semid, sops, nsops);
}


#ifndef HAVE_UNION_SEMUN
union semun {
	int val;			/* value for SETVAL */
	struct semid_ds *buf;		/* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;	/* array for GETALL, SETALL */
	struct seminfo *__buf;		/* buffer for IPC_INFO */
};
#endif
WRAPPER2(int, semctl, int semid, int semnum, int cmd, union semun arg)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  switch (cmd) {
  case IPC_STAT:
    MF_VALIDATE_EXTENT (arg.buf, sizeof (*arg.buf), __MF_CHECK_WRITE,
      "semctl buf");
    break;
  case IPC_SET:
    MF_VALIDATE_EXTENT (arg.buf, sizeof (*arg.buf), __MF_CHECK_READ,
      "semctl buf");
    break;
  case GETALL:
    MF_VALIDATE_EXTENT (arg.array, sizeof (*arg.array), __MF_CHECK_WRITE,
      "semctl array");
  case SETALL:
    MF_VALIDATE_EXTENT (arg.array, sizeof (*arg.array), __MF_CHECK_READ,
      "semctl array");
    break;
#ifdef IPC_INFO
  /* FreeBSD 5.1 And Cygwin headers include IPC_INFO but not the __buf field.  */
#if !defined(__FreeBSD__) && !defined(__CYGWIN__)
  case IPC_INFO:
    MF_VALIDATE_EXTENT (arg.__buf, sizeof (*arg.__buf), __MF_CHECK_WRITE,
      "semctl __buf");
    break;
#endif
#endif
  default:
    break;
  }
  return semctl (semid, semnum, cmd, arg);
}


WRAPPER2(int, shmctl, int shmid, int cmd, struct shmid_ds *buf)
{
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  switch (cmd) {
  case IPC_STAT:
    MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_WRITE,
      "shmctl buf");
    break;
  case IPC_SET:
    MF_VALIDATE_EXTENT (buf, sizeof (*buf), __MF_CHECK_READ,
      "shmctl buf");
    break;
  default:
    break;
  }
  return shmctl (shmid, cmd, buf);
}


WRAPPER2(void *, shmat, int shmid, const void *shmaddr, int shmflg)
{
  void *p;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  p = shmat (shmid, shmaddr, shmflg);
#ifdef MF_REGISTER_shmat
  if (NULL != p) {
    struct shmid_ds buf;
    __mf_register (p, shmctl (shmid, IPC_STAT, &buf) ? 0 : buf.shm_segsz,
      MF_REGISTER_shmat, "shmat result");
  }
#endif
  return p;
}


WRAPPER2(int, shmdt, const void *shmaddr)
{
  int resp;
  TRACE ("%s\n", __PRETTY_FUNCTION__);
  resp = shmdt (shmaddr);
#ifdef MF_REGISTER_shmat
  __mf_unregister ((void *)shmaddr, 0, MF_REGISTER_shmat);
#endif
  return resp;
}


#endif /* HAVE_SYS_IPC/SEM/SHM_H */



/* ctype stuff.  This is host-specific by necessity, as the arrays
   that is used by most is*()/to*() macros are implementation-defined.  */

/* GLIBC 2.3 */
#ifdef HAVE___CTYPE_B_LOC
WRAPPER2(unsigned short **, __ctype_b_loc, void)
{
  static unsigned short * last_buf = (void *) 0;
  static unsigned short ** last_ptr = (void *) 0;
  unsigned short ** ptr = (unsigned short **) __ctype_b_loc ();
  unsigned short * buf = * ptr;
  if (ptr != last_ptr)
    {
      /* XXX: unregister last_ptr? */
      last_ptr = ptr;
      __mf_register (last_ptr, sizeof(last_ptr), __MF_TYPE_STATIC, "ctype_b_loc **");
    }
  if (buf != last_buf)
    {
      last_buf = buf;
      __mf_register ((void *) (last_buf - 128), 384 * sizeof(unsigned short), __MF_TYPE_STATIC,
                     "ctype_b_loc []");
    }
  return ptr;
}
#endif

#ifdef HAVE___CTYPE_TOUPPER_LOC
WRAPPER2(int **, __ctype_toupper_loc, void)
{
  static int * last_buf = (void *) 0;
  static int ** last_ptr = (void *) 0;
  int ** ptr = (int **) __ctype_toupper_loc ();
  int * buf = * ptr;
  if (ptr != last_ptr)
    {
      /* XXX: unregister last_ptr? */
      last_ptr = ptr;
      __mf_register (last_ptr, sizeof(last_ptr), __MF_TYPE_STATIC, "ctype_toupper_loc **");
    }
  if (buf != last_buf)
    {
      last_buf = buf;
      __mf_register ((void *) (last_buf - 128), 384 * sizeof(int), __MF_TYPE_STATIC,
                     "ctype_toupper_loc []");
    }
  return ptr;
}
#endif

#ifdef HAVE___CTYPE_TOLOWER_LOC
WRAPPER2(int **, __ctype_tolower_loc, void)
{
  static int * last_buf = (void *) 0;
  static int ** last_ptr = (void *) 0;
  int ** ptr = (int **) __ctype_tolower_loc ();
  int * buf = * ptr;
  if (ptr != last_ptr)
    {
      /* XXX: unregister last_ptr? */
      last_ptr = ptr;
      __mf_register (last_ptr, sizeof(last_ptr), __MF_TYPE_STATIC, "ctype_tolower_loc **");
    }
  if (buf != last_buf)
    {
      last_buf = buf;
      __mf_register ((void *) (last_buf - 128), 384 * sizeof(int), __MF_TYPE_STATIC,
                     "ctype_tolower_loc []");
    }
  return ptr;
}
#endif


/* passwd/group related functions.  These register every (static) pointer value returned,
   and rely on libmudflap's quiet toleration of duplicate static registrations.  */

#ifdef HAVE_GETLOGIN
WRAPPER2(char *, getlogin, void)
{
  char *buf = getlogin ();
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getlogin() return");
  return buf;
}
#endif


#ifdef HAVE_CUSERID
WRAPPER2(char *, cuserid, char * buf)
{
  if (buf != NULL)
    {
      MF_VALIDATE_EXTENT(buf, L_cuserid, __MF_CHECK_WRITE,
                         "cuserid destination");
      return cuserid (buf);
    }
  buf = cuserid (NULL);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getcuserid() return");
  return buf;
}
#endif


#ifdef HAVE_GETPWNAM
WRAPPER2(struct passwd *, getpwnam, const char *name)
{
  struct passwd *buf;
  MF_VALIDATE_EXTENT(name, strlen(name)+1, __MF_CHECK_READ,
                     "getpwnam name");
  buf = getpwnam (name);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getpw*() return");
  return buf;
}
#endif


#ifdef HAVE_GETPWUID
WRAPPER2(struct passwd *, getpwuid, uid_t uid)
{
  struct passwd *buf;
  buf = getpwuid (uid);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getpw*() return");
  return buf;
}
#endif


#ifdef HAVE_GETGRNAM
WRAPPER2(struct group *, getgrnam, const char *name)
{
  struct group *buf;
  MF_VALIDATE_EXTENT(name, strlen(name)+1, __MF_CHECK_READ,
                     "getgrnam name");
  buf = getgrnam (name);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getgr*() return");
  return buf;
}
#endif


#ifdef HAVE_GETGRGID
WRAPPER2(struct group *, getgrgid, uid_t uid)
{
  struct group *buf;
  buf = getgrgid (uid);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getgr*() return");
  return buf;
}
#endif


#ifdef HAVE_GETSERVENT
WRAPPER2(struct servent *, getservent, void)
{
  struct servent *buf;
  buf = getservent ();
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getserv*() return");
  return buf;
}
#endif


#ifdef HAVE_GETSERVBYNAME
WRAPPER2(struct servent *, getservbyname, const char *name, const char *proto)
{
  struct servent *buf;
  MF_VALIDATE_EXTENT(name, strlen(name)+1, __MF_CHECK_READ,
                     "getservbyname name");
  MF_VALIDATE_EXTENT(proto, strlen(proto)+1, __MF_CHECK_READ,
                     "getservbyname proto");
  buf = getservbyname (name, proto);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getserv*() return");
  return buf;
}
#endif


#ifdef HAVE_GETSERVBYPORT
WRAPPER2(struct servent *, getservbyport, int port, const char *proto)
{
  struct servent *buf;
  MF_VALIDATE_EXTENT(proto, strlen(proto)+1, __MF_CHECK_READ,
                     "getservbyport proto");
  buf = getservbyport (port, proto);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getserv*() return");
  return buf;
}
#endif


#ifdef HAVE_GAI_STRERROR
WRAPPER2(const char *, gai_strerror, int errcode)
{
  const char *buf;
  buf = gai_strerror (errcode);
  if (buf != NULL)
    __mf_register ((void *) buf, strlen(buf)+1, __MF_TYPE_STATIC,
                   "gai_strerror() return");
  return buf;
}
#endif


#ifdef HAVE_GETMNTENT
WRAPPER2(struct mntent *, getmntent, FILE *filep)
{
  struct mntent *m;
  static struct mntent *last = NULL;

  MF_VALIDATE_EXTENT (filep, sizeof (*filep), __MF_CHECK_WRITE,
    "getmntent stream");
#define UR(field) __mf_unregister(last->field, strlen (last->field)+1, __MF_TYPE_STATIC)
  if (last)
    {
      UR (mnt_fsname);
      UR (mnt_dir);
      UR (mnt_type);
      UR (mnt_opts);
      __mf_unregister (last, sizeof (*last), __MF_TYPE_STATIC);
    }
#undef UR

  m = getmntent (filep);
  last = m;

#define R(field) __mf_register(last->field, strlen (last->field)+1, __MF_TYPE_STATIC, "mntent " #field)
  if (m)
    {
      R (mnt_fsname);
      R (mnt_dir);
      R (mnt_type);
      R (mnt_opts);
      __mf_register (last, sizeof (*last), __MF_TYPE_STATIC, "getmntent result");
    }
#undef R

  return m;
}
#endif


#ifdef HAVE_INET_NTOA
WRAPPER2(char *, inet_ntoa, struct in_addr in)
{
  static char *last_buf = NULL;
  char *buf;
  if (last_buf)
    __mf_unregister (last_buf, strlen (last_buf)+1, __MF_TYPE_STATIC);
  buf = inet_ntoa (in);
  last_buf = buf;
  if (buf)
    __mf_register (last_buf, strlen (last_buf)+1, __MF_TYPE_STATIC, "inet_ntoa result");
  return buf;
}
#endif


#ifdef HAVE_GETPROTOENT
WRAPPER2(struct protoent *, getprotoent, void)
{
  struct protoent *buf;
  buf = getprotoent ();
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC, "getproto*() return");
  return buf;
}
#endif


#ifdef HAVE_GETPROTOBYNAME
WRAPPER2(struct protoent *, getprotobyname, const char *name)
{
  struct protoent *buf;
  MF_VALIDATE_EXTENT(name, strlen(name)+1, __MF_CHECK_READ,
                     "getprotobyname name");
  buf = getprotobyname (name);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getproto*() return");
  return buf;
}
#endif


#ifdef HAVE_GETPROTOBYNUMBER
WRAPPER2(struct protoent *, getprotobynumber, int port)
{
  struct protoent *buf;
  buf = getprotobynumber (port);
  if (buf != NULL)
    __mf_register (buf, sizeof(*buf), __MF_TYPE_STATIC,
                   "getproto*() return");
  return buf;
}
#endif
