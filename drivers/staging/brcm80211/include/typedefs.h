/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _TYPEDEFS_H_
#define _TYPEDEFS_H_

#include <linux/types.h>

/*
 * Infer the compile environment based on preprocessor symbols and pragmas.
 * Override type definitions as needed, and include configuration-dependent
 * header files to define types.
 */

#if defined(__x86_64__)
#define TYPEDEF_UINTPTR
typedef unsigned long long int uintptr;
#endif

#define TYPEDEF_UINT
#define TYPEDEF_USHORT
#define TYPEDEF_ULONG

/*
 * Default Typedefs
 */

/* define uchar, ushort, uint, ulong */

#ifndef TYPEDEF_UCHAR
typedef unsigned char uchar;
#endif

#ifndef TYPEDEF_USHORT
typedef unsigned short ushort;
#endif

#ifndef TYPEDEF_UINT
typedef unsigned int uint;
#endif

#ifndef TYPEDEF_ULONG
typedef unsigned long ulong;
#endif

/* define [u]int8/16/32/64, uintptr */

#ifndef TYPEDEF_UINT8
typedef unsigned char uint8;
#endif

#ifndef TYPEDEF_UINT16
typedef unsigned short uint16;
#endif

#ifndef TYPEDEF_UINT32
typedef unsigned int uint32;
#endif

#ifndef TYPEDEF_UINTPTR
typedef unsigned int uintptr;
#endif

#ifndef TYPEDEF_INT8
typedef signed char int8;
#endif

#ifndef TYPEDEF_INT16
typedef signed short int16;
#endif

#ifndef TYPEDEF_INT32
typedef signed int int32;
#endif

/* define macro values */

#ifndef FALSE
#define FALSE	0
#endif

#ifndef TRUE
#define TRUE	1		/* TRUE */
#endif

#ifndef NULL
#define	NULL	0
#endif

#ifndef OFF
#define	OFF	0
#endif

#ifndef ON
#define	ON	1		/* ON = 1 */
#endif

#define	AUTO	(-1)		/* Auto = -1 */

#undef TYPEDEF_UCHAR
#undef TYPEDEF_USHORT
#undef TYPEDEF_UINT
#undef TYPEDEF_ULONG
#undef TYPEDEF_UINT8
#undef TYPEDEF_UINT16
#undef TYPEDEF_UINT32
#undef TYPEDEF_UINTPTR
#undef TYPEDEF_INT8
#undef TYPEDEF_INT16
#undef TYPEDEF_INT32
#undef TYPEDEF_FLOAT32
#undef TYPEDEF_FLOAT64
#undef TYPEDEF_FLOAT_T

/*
 * Including the bcmdefs.h here, to make sure everyone including typedefs.h
 * gets this automatically
*/
#include <bcmdefs.h>

#endif				/* _TYPEDEFS_H_ */
