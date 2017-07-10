/*************************************************************************/ /*!
@File
@Title          Global types for use by IMG APIs
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines type aliases for use by IMG APIs.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef __IMG_TYPES_H__
#define __IMG_TYPES_H__
#if defined (__cplusplus)
extern "C" {
#endif

/* To use C99 types and definitions, there are two special cases we need to
 * cater for:
 *
 * - Visual Studio: in VS2010 or later, some standard headers are available,
 *   and MSVC has its own built-in sized types. We can define the C99 types
 *   in terms of these.
 *
 * - Linux kernel code: C99 sized types are defined in <linux/types.h>, but
 *   some other features (like macros for constants or printf format
 *   strings) are missing, so we need to fill in the gaps ourselves.
 *
 * For other cases (userspace code under Linux, Android or Neutrino, or
 * firmware code), we can include the standard headers.
 */
#if defined(_MSC_VER)
	#include "msvc_types.h"
#elif defined(LINUX) && defined(__KERNEL__)
	#include <linux/types.h>
	#include "kernel_types.h"
#elif defined(LINUX) || defined(__METAG) || defined(__QNXNTO__) || defined(INTEGRITY_OS)
	#include <stddef.h>			/* NULL */
	#include <inttypes.h>		/* intX_t/uintX_t, format specifiers */
	#include <limits.h>			/* INT_MIN, etc */
#elif defined(__mips)
	#include <stddef.h>			/* NULL */
	#include <inttypes.h>		/* intX_t/uintX_t, format specifiers */
#else
	#error C99 support not set up for this build
#endif

typedef unsigned int	IMG_UINT,	*IMG_PUINT;
typedef int				IMG_INT,	*IMG_PINT;

typedef uint8_t			IMG_UINT8,	*IMG_PUINT8;
typedef uint8_t			IMG_BYTE,	*IMG_PBYTE;
typedef int8_t			IMG_INT8,	*IMG_PINT8;
typedef char			IMG_CHAR,	*IMG_PCHAR;

typedef uint16_t		IMG_UINT16,	*IMG_PUINT16;
typedef int16_t			IMG_INT16,	*IMG_PINT16;
typedef uint32_t		IMG_UINT32,	*IMG_PUINT32;
typedef int32_t			IMG_INT32,	*IMG_PINT32;

typedef uint64_t		IMG_UINT64,	*IMG_PUINT64;
typedef int64_t			IMG_INT64,	*IMG_PINT64;
#define IMG_INT64_C(c)	INT64_C(c)
#define IMG_UINT64_C(c)	UINT64_C(c)
#define IMG_UINT64_FMTSPEC PRIu64
#define IMG_UINT64_FMTSPECX PRIX64
#define IMG_UINT64_FMTSPECx PRIx64
#define IMG_UINT64_FMTSPECo PRIo64
#define IMG_INT64_FMTSPECd PRId64

#define IMG_UINT16_MAX	UINT16_MAX
#define IMG_UINT32_MAX	UINT32_MAX
#define IMG_UINT64_MAX	UINT64_MAX

#define IMG_INT16_MAX	INT16_MAX
#define IMG_INT32_MAX	INT32_MAX
#define IMG_INT64_MAX	INT64_MAX

/* Linux kernel mode does not use floating point */
typedef float			IMG_FLOAT,	*IMG_PFLOAT;
typedef double			IMG_DOUBLE, *IMG_PDOUBLE;

typedef union _IMG_UINT32_FLOAT_
{
	IMG_UINT32 ui32;
	IMG_FLOAT f;
} IMG_UINT32_FLOAT;

typedef int				IMG_SECURE_TYPE;

typedef	enum tag_img_bool
{
	IMG_FALSE		= 0,
	IMG_TRUE		= 1,
	IMG_FORCE_ALIGN = 0x7FFFFFFF
} IMG_BOOL, *IMG_PBOOL;

#if defined(UNDER_WDDM) || defined(WINDOWS_WDF)
typedef void            IMG_VOID, *IMG_PVOID;

typedef uintptr_t		IMG_UINTPTR_T;
typedef size_t			IMG_SIZE_T;

#define IMG_SIZE_T_MAX	SIZE_MAX
#define IMG_NULL		NULL

typedef IMG_CHAR const* IMG_PCCHAR;
#endif

#if defined(_MSC_VER)
#define IMG_SIZE_FMTSPEC  "%Iu"
#define IMG_SIZE_FMTSPECX "%Ix"
#else
#define IMG_SIZE_FMTSPEC  "%zu"
#define IMG_SIZE_FMTSPECX "%zx"
#endif

#if defined(LINUX) && defined(__KERNEL__)
/* prints the function name when used with printk */
#define IMG_PFN_FMTSPEC "%pf"
#else
#define IMG_PFN_FMTSPEC "%p"
#endif

typedef void           *IMG_HANDLE;

/* services/stream ID */
typedef IMG_UINT64      IMG_SID;

/* Process IDs */
typedef IMG_UINT32      IMG_PID;

/* OS connection type */
typedef int             IMG_OS_CONNECTION;


/*
 * Address types.
 * All types used to refer to a block of memory are wrapped in structures
 * to enforce some degree of type safety, i.e. a IMG_DEV_VIRTADDR cannot
 * be assigned to a variable of type IMG_DEV_PHYADDR because they are not the
 * same thing.
 *
 * There is an assumption that the system contains at most one non-cpu mmu,
 * and a memory block is only mapped by the MMU once.
 *
 * Different devices could have offset views of the physical address space.
 * 
 */


/*
 *
 * +------------+    +------------+      +------------+        +------------+
 * |    CPU     |    |    DEV     |      |    DEV     |        |    DEV     |
 * +------------+    +------------+      +------------+        +------------+
 *       |                 |                   |                     |
 *       | void *          |IMG_DEV_VIRTADDR   |IMG_DEV_VIRTADDR     |
 *       |                 \-------------------/                     |
 *       |                          |                                |
 * +------------+             +------------+                         |
 * |    MMU     |             |    MMU     |                         |
 * +------------+             +------------+                         |
 *       |                          |                                |
 *       |                          |                                |
 *       |                          |                                |
 *   +--------+                +---------+                      +--------+
 *   | Offset |                | (Offset)|                      | Offset |
 *   +--------+                +---------+                      +--------+
 *       |                          |                IMG_DEV_PHYADDR |
 *       |                          |                                |
 *       |                          | IMG_DEV_PHYADDR                |
 * +---------------------------------------------------------------------+
 * |                         System Address bus                          |
 * +---------------------------------------------------------------------+
 *
 */

#define IMG_DEV_VIRTADDR_FMTSPEC "0x%010" IMG_UINT64_FMTSPECX
#define IMG_DEVMEM_SIZE_FMTSPEC "0x%010" IMG_UINT64_FMTSPECX
#define IMG_DEVMEM_ALIGN_FMTSPEC "0x%010" IMG_UINT64_FMTSPECX
#define IMG_DEVMEM_OFFSET_FMTSPEC "0x%010" IMG_UINT64_FMTSPECX

/* cpu physical address */
typedef struct _IMG_CPU_PHYADDR
{
#if defined(UNDER_WDDM) || defined(WINDOWS_WDF)
	uintptr_t uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(uintptr_t)(var)
#elif defined(LINUX) && defined(__KERNEL__)
	phys_addr_t uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(phys_addr_t)(var)
#else
	IMG_UINT64 uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(IMG_UINT64)(var)
#endif
} IMG_CPU_PHYADDR;

/* device physical address */
typedef struct _IMG_DEV_PHYADDR
{
	IMG_UINT64 uiAddr;
} IMG_DEV_PHYADDR;

/* system physical address */
typedef struct _IMG_SYS_PHYADDR
{
#if defined(UNDER_WDDM) || defined(WINDOWS_WDF)
	uintptr_t uiAddr;
#else
	IMG_UINT64 uiAddr;
#endif
} IMG_SYS_PHYADDR;

/* 32-bit device virtual address (e.g. MSVDX) */
typedef struct _IMG_DEV_VIRTADDR32
{
	IMG_UINT32 uiAddr;
#define IMG_CAST_TO_DEVVADDR_UINT32(var) (IMG_UINT32)(var)
} IMG_DEV_VIRTADDR32;

/*
	rectangle structure
*/
typedef struct _IMG_RECT_
{
	IMG_INT32	x0;
	IMG_INT32	y0;
	IMG_INT32	x1;
	IMG_INT32	y1;
}IMG_RECT, *PIMG_RECT;

typedef struct _IMG_RECT_16_
{
	IMG_INT16	x0;
	IMG_INT16	y0;
	IMG_INT16	x1;
	IMG_INT16	y1;
}IMG_RECT_16, *PIMG_RECT_16;

typedef struct _IMG_RECT_32_
{
	IMG_FLOAT	x0;
	IMG_FLOAT	y0;
	IMG_FLOAT	x1;
	IMG_FLOAT	y1;
} IMG_RECT_F32, *PIMG_RECT_F32;

/*
 * box structure
 */
typedef struct _IMG_BOX_
{
	IMG_INT32	x0;
	IMG_INT32	y0;
	IMG_INT32	z0;
	IMG_INT32	x1;
	IMG_INT32	y1;
	IMG_INT32	z1;
} IMG_BOX, *PIMG_BOX;

#if defined (__cplusplus)
}
#endif

#include "img_defs.h"

#endif	/* __IMG_TYPES_H__ */
/******************************************************************************
 End of file (img_types.h)
******************************************************************************/

