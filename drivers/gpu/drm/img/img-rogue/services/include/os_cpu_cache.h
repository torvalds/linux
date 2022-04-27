/*************************************************************************/ /*!
@File
@Title          OS and CPU d-cache maintenance mechanisms
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines for cache management which are visible internally only
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

#ifndef OS_CPU_CACHE_H
#define OS_CPU_CACHE_H

#include "info_page_defs.h"

#define PVRSRV_CACHE_OP_TIMELINE			0x8 /*!< Request SW_SYNC timeline notification when executed */
#define PVRSRV_CACHE_OP_FORCE_SYNCHRONOUS	0x10 /*!< Force all batch members to be executed synchronously */

#define CACHEFLUSH_ISA_X86					0x1	/*!< x86/x64 specific UM range-based cache flush */
#define CACHEFLUSH_ISA_ARM64				0x2	/*!< Aarch64 specific UM range-based cache flush */
#define CACHEFLUSH_ISA_GENERIC				0x3	/*!< Other ISA's without UM range-based cache flush */
#ifndef CACHEFLUSH_ISA_TYPE
	#if defined(__i386__) || defined(__x86_64__)
		#define CACHEFLUSH_ISA_TYPE CACHEFLUSH_ISA_X86
	#elif defined(__arm64__) || defined(__aarch64__)
		#define CACHEFLUSH_ISA_TYPE CACHEFLUSH_ISA_ARM64
	#else
		#define CACHEFLUSH_ISA_TYPE CACHEFLUSH_ISA_GENERIC
	#endif
#endif

#if (CACHEFLUSH_ISA_TYPE == CACHEFLUSH_ISA_X86) || (CACHEFLUSH_ISA_TYPE == CACHEFLUSH_ISA_ARM64)
#define CACHEFLUSH_ISA_SUPPORTS_UM_FLUSH		/*!< x86/x86_64/ARM64 supports user-mode d-cache flush */
#endif

#endif	/* OS_CPU_CACHE_H */
