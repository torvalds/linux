/*
 * Copyright (c) 2000-2001, 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: string.h,v 1.39 2013-11-22 20:51:31 ca Exp $
 */

/*
**  libsm string manipulation
*/

#ifndef SM_STRING_H
# define SM_STRING_H

# include <sm/gen.h>
# include <sm/varargs.h>
# include <string.h> /* strlc{py,at}, strerror */

/* return number of bytes left in a buffer */
#define SPACELEFT(buf, ptr)	(sizeof buf - ((ptr) - buf))

extern int PRINTFLIKE(3, 4)
sm_snprintf __P((char *, size_t, const char *, ...));

extern bool
sm_match __P((const char *_str, const char *_pattern));

extern char *
sm_strdup __P((char *));

extern char *
sm_strndup_x __P((const char *_str, size_t _len));

#if DO_NOT_USE_STRCPY
/* for "normal" data (free'd before end of process) */
extern char *
sm_strdup_x __P((const char *_str));

/* for data that is supposed to be persistent. */
extern char *
sm_pstrdup_x __P((const char *_str));

extern char *
sm_strdup_tagged_x __P((const char *str, char *file, int line, int group));

#else /* DO_NOT_USE_STRCPY */

/* for "normal" data (free'd before end of process) */
# define sm_strdup_x(str) strcpy(sm_malloc_x(strlen(str) + 1), str)

/* for data that is supposed to be persistent. */
# define sm_pstrdup_x(str) strcpy(sm_pmalloc_x(strlen(str) + 1), str)

# define sm_strdup_tagged_x(str, file, line, group) \
	strcpy(sm_malloc_tagged_x(strlen(str) + 1, file, line, group), str)
#endif /* DO_NOT_USE_STRCPY */

extern char *
sm_stringf_x __P((const char *_fmt, ...));

extern char *
sm_vstringf_x __P((const char *_fmt, va_list _ap));

extern size_t
sm_strlcpy __P((char *_dst, const char *_src, ssize_t _len));

extern size_t
sm_strlcat __P((char *_dst, const char *_src, ssize_t _len));

extern size_t
sm_strlcat2 __P((char *, const char *, const char *, ssize_t));

extern size_t
#ifdef __STDC__
sm_strlcpyn(char *dst, ssize_t len, int n, ...);
#else /* __STDC__ */
sm_strlcpyn __P((char *,
	ssize_t,
	int,
	va_dcl));
#endif /* __STDC__ */

# if !HASSTRERROR
extern char *
strerror __P((int _errno));
# endif /* !HASSTRERROR */

extern int
sm_strrevcmp __P((const char *, const char *));

extern int
sm_strrevcasecmp __P((const char *, const char *));

extern int
sm_strcasecmp __P((const char *, const char *));

extern int
sm_strncasecmp __P((const char *, const char *, size_t));

extern LONGLONG_T
sm_strtoll __P((const char *, char**, int));

extern ULONGLONG_T
sm_strtoull __P((const char *, char**, int));

extern void
stripquotes __P((char *));

#endif /* SM_STRING_H */
