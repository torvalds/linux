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

#ifndef IMG_TYPES_H
#define IMG_TYPES_H
#if defined(__cplusplus)
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
	#include <stdbool.h>		/* bool */
	#include "msvc_types.h"
#elif defined(__linux__) && defined(__KERNEL__)
	#include <linux/version.h>
	#include <linux/types.h>
	#include "kernel_types.h"
#elif defined(__linux__) || defined(__METAG) || defined(__MINGW32__) || \
	defined(__QNXNTO__) || defined(INTEGRITY_OS) || defined(__riscv)
	#include <stddef.h>			/* NULL */
	#include <stdint.h>
	#include <inttypes.h>		/* intX_t/uintX_t, format specifiers */
	#include <limits.h>			/* INT_MIN, etc */
	#include <stdbool.h>		/* bool */
#elif defined(__mips)
	#include <stddef.h>			/* NULL */
	#include <inttypes.h>		/* intX_t/uintX_t, format specifiers */
	#include <stdbool.h>		/* bool */
#else
	#error C99 support not set up for this build
#endif

/*
 * Due to a Klocwork bug, 'true'/'false' constants are not recognized to be of
 * boolean type. This results in large number of false-positives being reported
 * (MISRA.ETYPE.ASSIGN.2012: "An expression value of essential type 'signed char'
 * is assigned to an object of essential type 'bool'"). Work around this by
 * redefining those constants with cast to bool added.
 */
#if defined(__KLOCWORK__) && !defined(__cplusplus)
#undef true
#undef false
#define true ((bool) 1)
#define false ((bool) 0)
#endif

typedef unsigned int	IMG_UINT;
typedef int				IMG_INT;

typedef uint8_t			IMG_UINT8,	*IMG_PUINT8;
typedef uint8_t			IMG_BYTE,	*IMG_PBYTE;
typedef int8_t			IMG_INT8;
typedef char			IMG_CHAR,	*IMG_PCHAR;

typedef uint16_t		IMG_UINT16,	*IMG_PUINT16;
typedef int16_t			IMG_INT16;
typedef uint32_t		IMG_UINT32,	*IMG_PUINT32;
typedef int32_t			IMG_INT32,	*IMG_PINT32;
#define IMG_UINT32_C(c) ((IMG_UINT32)UINT32_C(c))

typedef uint64_t		IMG_UINT64,	*IMG_PUINT64;
typedef int64_t			IMG_INT64;
#define IMG_INT64_C(c)	INT64_C(c)
#define IMG_UINT64_C(c)	UINT64_C(c)
#define IMG_UINT16_C(c)	UINT16_C(c)
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
typedef double			IMG_DOUBLE;

typedef union
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
typedef IMG_CHAR const* IMG_PCCHAR;
#endif

/* Format specifiers for 'size_t' type */
#if defined(_MSC_VER) || defined(__MINGW32__)
#define IMG_SIZE_FMTSPEC  "%Iu"
#define IMG_SIZE_FMTSPECX "%Ix"
#else
#define IMG_SIZE_FMTSPEC  "%zu"
#define IMG_SIZE_FMTSPECX "%zx"
#endif

#if defined(__linux__) && defined(__KERNEL__)
/* prints the function name when used with printk */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#define IMG_PFN_FMTSPEC "%ps"
#else
#define IMG_PFN_FMTSPEC "%pf"
#endif
#else
#define IMG_PFN_FMTSPEC "%p"
#endif

typedef void           *IMG_HANDLE;

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
typedef struct
{
#if defined(UNDER_WDDM) || defined(WINDOWS_WDF)
	uintptr_t uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(uintptr_t)(var)
#elif defined(__linux__) && defined(__KERNEL__)
	phys_addr_t uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(phys_addr_t)(var)
#else
	IMG_UINT64 uiAddr;
#define IMG_CAST_TO_CPUPHYADDR_UINT(var)		(IMG_UINT64)(var)
#endif
} IMG_CPU_PHYADDR;

/* device physical address */
typedef struct
{
	IMG_UINT64 uiAddr;
} IMG_DEV_PHYADDR;

/* dma address */
typedef struct
{
	IMG_UINT64 uiAddr;
} IMG_DMA_ADDR;

/*
	rectangle structure
*/
typedef struct
{
	IMG_INT32	x0;
	IMG_INT32	y0;
	IMG_INT32	x1;
	IMG_INT32	y1;
} IMG_RECT;

typedef struct
{
	IMG_INT16	x0;
	IMG_INT16	y0;
	IMG_INT16	x1;
	IMG_INT16	y1;
} IMG_RECT_16;

/*
 * box structure
 */
typedef struct
{
	IMG_INT32	x0;
	IMG_INT32	y0;
	IMG_INT32	z0;
	IMG_INT32	x1;
	IMG_INT32	y1;
	IMG_INT32	z1;
} IMG_BOX;

#if defined(__cplusplus)
}
#endif

#endif	/* IMG_TYPES_H */
/******************************************************************************
 End of file (img_types.h)
******************************************************************************/
