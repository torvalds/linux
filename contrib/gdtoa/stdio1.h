/****************************************************************
Copyright (C) 1997-1999 Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

/* stdio1.h -- for using Printf, Fprintf, Sprintf while
 * retaining the system-supplied printf, fprintf, sprintf.
 */

#ifndef STDIO1_H_included
#define STDIO1_H_included
#ifndef STDIO_H_included	/* allow suppressing stdio.h */
#include <stdio.h>		/* in case it's already included, */
#endif				/* e.g., by cplex.h */

#ifdef KR_headers
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif
#define ANSI(x) ()
#include "varargs.h"
#ifndef Char
#define Char char
#endif
#else
#define ANSI(x) x
#include "stdarg.h"
#ifndef Char
#define Char void
#endif
#endif

#ifndef NO_STDIO1

#ifdef __cplusplus
extern "C" {
#endif

extern int Fprintf ANSI((FILE*, const char*, ...));
extern int Printf ANSI((const char*, ...));
extern int Sprintf ANSI((char*, const char*, ...));
extern int Snprintf ANSI((char*, size_t, const char*, ...));
extern void Perror ANSI((const char*));
extern int Vfprintf ANSI((FILE*, const char*, va_list));
extern int Vsprintf ANSI((char*, const char*, va_list));
extern int Vsnprintf ANSI((char*, size_t, const char*, va_list));

#ifdef PF_BUF
extern FILE *stderr_ASL;
extern void (*pfbuf_print_ASL) ANSI((char*));
extern char *pfbuf_ASL;
extern void fflush_ASL ANSI((FILE*));
#ifdef fflush
#define old_fflush_ASL fflush
#undef  fflush
#endif
#define fflush fflush_ASL
#endif

#ifdef __cplusplus
	}
#endif

#undef printf
#undef fprintf
#undef sprintf
#undef perror
#undef vfprintf
#undef vsprintf
#define printf Printf
#define fprintf Fprintf
#undef snprintf		/* for MacOSX */
#undef vsnprintf	/* for MacOSX */
#define snprintf Snprintf
#define sprintf Sprintf
#define perror Perror
#define vfprintf Vfprintf
#define vsnprintf Vsnprintf
#define vsprintf Vsprintf

#endif /* NO_STDIO1 */

#endif /* STDIO1_H_included */
