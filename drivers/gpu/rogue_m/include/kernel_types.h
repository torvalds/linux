/*************************************************************************/ /*!
@Title          C99-compatible types and definitions for Linux kernel code
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include <linux/kernel.h>

/* Limits of specified-width integer types */

/* S8_MIN, etc were added in kernel version 3.14. The other versions are for
 * earlier kernels. They can be removed once older kernels don't need to be
 * supported.
 */
#ifdef S8_MIN
	#define INT8_MIN	S8_MIN
#else
	#define INT8_MIN	(-128)
#endif

#ifdef S8_MAX
	#define INT8_MAX	S8_MAX
#else
	#define INT8_MAX	127
#endif

#ifdef U8_MAX
	#define UINT8_MAX	U8_MAX
#else
	#define UINT8_MAX	0xFF
#endif

#ifdef S16_MIN
	#define INT16_MIN	S16_MIN
#else
	#define INT16_MIN	(-32768)
#endif

#ifdef S16_MAX
	#define INT16_MAX	S16_MAX
#else
	#define INT16_MAX	32767
#endif

#ifdef U16_MAX
	#define UINT16_MAX	U16_MAX
#else
	#define UINT16_MAX	0xFFFF
#endif

#ifdef S32_MIN
	#define INT32_MIN	S32_MIN
#else
	#define INT32_MIN	(-2147483647 - 1)
#endif

#ifdef S32_MAX
	#define INT32_MAX	S32_MAX
#else
	#define INT32_MAX	2147483647
#endif

#ifdef U32_MAX
	#define UINT32_MAX	U32_MAX
#else
	#define UINT32_MAX	0xFFFFFFFF
#endif

#ifdef S64_MIN
	#define INT64_MIN	S64_MIN
#else
	#define INT64_MIN	(-9223372036854775807LL)
#endif

#ifdef S64_MAX
	#define INT64_MAX	S64_MAX
#else
	#define INT64_MAX	9223372036854775807LL
#endif

#ifdef U64_MAX
	#define UINT64_MAX	U64_MAX
#else
	#define UINT64_MAX	0xFFFFFFFFFFFFFFFFULL
#endif

/* Macros for integer constants */
#define INT8_C			S8_C
#define UINT8_C			U8_C
#define INT16_C			S16_C
#define UINT16_C		U16_C
#define INT32_C			S32_C
#define UINT32_C		U32_C
#define INT64_C			S64_C
#define UINT64_C		U64_C

/* Format conversion of integer types <inttypes.h> */
/* Only define PRIX64 for the moment, as this is the only format macro that
 * img_types.h needs.
 */
#define PRIX64		"llX"
