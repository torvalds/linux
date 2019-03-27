/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/libamu/strutil.c
 *
 */

/*
 * String Utilities.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>


char *
strnsave(const char *str, int len)
{
  char *sp = (char *) xmalloc(len + 1);
  memmove(sp, str, len);
  sp[len] = '\0';

  return sp;
}


/*
 * Concatenate three strings and store the result in the buffer pointed to
 * by p, making p large enough to hold the strings
 */
char *
str3cat(char *p, char *s1, char *s2, char *s3)
{
  int l1 = strlen(s1);
  int l2 = strlen(s2);
  int l3 = strlen(s3);

  p = (char *) xrealloc(p, l1 + l2 + l3 + 1);
  memmove(p, s1, l1);
  memmove(p + l1, s2, l2);
  memmove(p + l1 + l2, s3, l3 + 1);
  return p;
}


/*
 * Split s using ch as delimiter and qc as quote character
 */
char **
strsplit(char *s, int ch, int qc)
{
  char **ivec;
  int ic = 0;
  int done = 0;

  ivec = (char **) xmalloc((ic + 1) * sizeof(char *));

  while (!done) {
    char *v;

    /*
     * skip to split char
     */
    while (*s && (ch == ' ' ? (isascii((unsigned char)*s) && isspace((unsigned char)*s)) : *s == ch))
      *s++ = '\0';

    /*
     * End of string?
     */
    if (!*s)
      break;

    /*
     * remember start of string
     */
    v = s;

    /*
     * skip to split char
     */
    while (*s && !(ch == ' ' ? (isascii((unsigned char)*s) && isspace((unsigned char)*s)) : *s == ch)) {
      if (*s++ == qc) {
	/*
	 * Skip past string.
	 */
	s++;
	while (*s && *s != qc)
	  s++;
	if (*s == qc)
	  s++;
      }
    }

    if (!*s)
      done = 1;
    *s++ = '\0';

    /*
     * save string in new ivec slot
     */
    ivec[ic++] = v;
    ivec = (char **) xrealloc((voidp) ivec, (ic + 1) * sizeof(char *));
    if (amuDebug(D_STR))
      plog(XLOG_DEBUG, "strsplit saved \"%s\"", v);
  }

  if (amuDebug(D_STR))
    plog(XLOG_DEBUG, "strsplit saved a total of %d strings", ic);

  ivec[ic] = NULL;

  return ivec;
}


/*
 * Use generic strlcpy to copy a string more carefully, null-terminating it
 * as needed.  However, if the copied string was truncated due to lack of
 * space, then warn us.
 *
 * For now, xstrlcpy returns VOID because it doesn't look like anywhere in
 * the Amd code do we actually use the return value of strncpy/strlcpy.
 */
void
#ifdef DEBUG
_xstrlcpy(const char *filename, int lineno, char *dst, const char *src, size_t len)
#else /* not DEBUG */
xstrlcpy(char *dst, const char *src, size_t len)
#endif /* not DEBUG */
{
  if (len == 0)
    return;
  if (strlcpy(dst, src, len) >= len)
#ifdef DEBUG
    plog(XLOG_ERROR, "xstrlcpy(%s:%d): string \"%s\" truncated to \"%s\"",
	 filename, lineno, src, dst);
#else /* not DEBUG */
    plog(XLOG_ERROR, "xstrlcpy: string \"%s\" truncated to \"%s\"", src, dst);
#endif /* not DEBUG */
}


/*
 * Use generic strlcat to concatenate a string more carefully,
 * null-terminating it as needed.  However, if the copied string was
 * truncated due to lack of space, then warn us.
 *
 * For now, xstrlcat returns VOID because it doesn't look like anywhere in
 * the Amd code do we actually use the return value of strncat/strlcat.
 */
void
#ifdef DEBUG
_xstrlcat(const char *filename, int lineno, char *dst, const char *src, size_t len)
#else /* not DEBUG */
xstrlcat(char *dst, const char *src, size_t len)
#endif /* not DEBUG */
{
  if (len == 0)
    return;
  if (strlcat(dst, src, len) >= len) {
    /* strlcat does not null terminate if the size of src is equal to len. */
    dst[strlen(dst) - 1] = '\0';
#ifdef DEBUG
    plog(XLOG_ERROR, "xstrlcat(%s:%d): string \"%s\" truncated to \"%s\"",
	 filename, lineno, src, dst);
#else /* not DEBUG */
    plog(XLOG_ERROR, "xstrlcat: string \"%s\" truncated to \"%s\"", src, dst);
#endif /* not DEBUG */
  }
}


/* our version of snprintf */
int
#if defined(DEBUG) && (defined(HAVE_C99_VARARGS_MACROS) || defined(HAVE_GCC_VARARGS_MACROS))
_xsnprintf(const char *filename, int lineno, char *str, size_t size, const char *format, ...)
#else /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
xsnprintf(char *str, size_t size, const char *format, ...)
#endif /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
{
  va_list ap;
  int ret = 0;

  va_start(ap, format);
#if defined(DEBUG) && (defined(HAVE_C99_VARARGS_MACROS) || defined(HAVE_GCC_VARARGS_MACROS))
  ret = _xvsnprintf(filename, lineno, str, size, format, ap);
#else /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
  ret = xvsnprintf(str, size, format, ap);
#endif /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
  va_end(ap);

  return ret;
}


/* our version of vsnprintf */
int
#if defined(DEBUG) && (defined(HAVE_C99_VARARGS_MACROS) || defined(HAVE_GCC_VARARGS_MACROS))
_xvsnprintf(const char *filename, int lineno, char *str, size_t size, const char *format, va_list ap)
#else /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
xvsnprintf(char *str, size_t size, const char *format, va_list ap)
#endif /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
{
  int ret = 0;

#ifdef HAVE_VSNPRINTF
  ret = vsnprintf(str, size, format, ap);
#else /* not HAVE_VSNPRINTF */
  ret = vsprintf(str, format, ap); /* less secure version */
#endif /* not HAVE_VSNPRINTF */
  /*
   * If error or truncation, plog error.
   *
   * WARNING: we use the static 'maxtrunc' variable below to break out any
   * possible infinite recursion between plog() and xvsnprintf().  If it
   * ever happens, it'd indicate a bug in Amd.
   */
  if (ret < 0 || (size_t) ret >= size) { /* error or truncation occured */
    static int maxtrunc;        /* hack to avoid inifinite loop */
    if (++maxtrunc > 10)
#if defined(DEBUG) && (defined(HAVE_C99_VARARGS_MACROS) || defined(HAVE_GCC_VARARGS_MACROS))
      plog(XLOG_ERROR, "xvsnprintf(%s:%d): string %p truncated (ret=%d, format=\"%s\")",
           filename, lineno, str, ret, format);
#else /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
      plog(XLOG_ERROR, "xvsnprintf: string %p truncated (ret=%d, format=\"%s\")",
           str, ret, format);
#endif /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
  }

  return ret;
}

static size_t
vstrlen(const char *src, va_list ap)
{
  size_t len = strlen(src);
  while ((src = va_arg(ap, const char *)) != NULL)
    len += strlen(src);
  return len;
}

static void
vstrcpy(char *dst, const char *src, va_list ap)
{
  strcpy(dst, src);
  while ((src = va_arg(ap, const char *)) != NULL)
    strcat(dst, src);
}

char *
strvcat(const char *src, ...)
{
  size_t len;
  char *dst;
  va_list ap;

  va_start(ap, src);
  len = vstrlen(src, ap);
  va_end(ap);
  dst = xmalloc(len + 1);
  va_start(ap, src);
  vstrcpy(dst, src, ap);
  va_end(ap);
  return dst;
}
