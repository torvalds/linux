/*************************************************************************/ /*!
@File
@Title          Services cache management header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines for cache management which are visible internally
                and externally
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

#ifndef _CACHE_OPS_H_
#define _CACHE_OPS_H_
#include "img_types.h"

typedef IMG_UINT32 PVRSRV_CACHE_OP;				/*!< Type represents cache maintenance operation */
#define PVRSRV_CACHE_OP_NONE				0x0	/*!< No operation */
#define PVRSRV_CACHE_OP_CLEAN				0x1	/*!< Flush w/o invalidate */
#define PVRSRV_CACHE_OP_INVALIDATE			0x2	/*!< Invalidate w/o flush */
#define PVRSRV_CACHE_OP_FLUSH				0x3	/*!< Flush w/ invalidate */

#define CACHEFLUSH_UM_X86					0x1	/*!< Intel x86/x64 specific UM range-based cache flush */
#define CACHEFLUSH_UM_ARM64					0x2	/*!< ARM Aarch64 specific UM range-based cache flush */
#define CACHEFLUSH_UM_GENERIC				0x3	/*!< Generic UM/KM cache flush (i.e. CACHEFLUSH_KM_TYPE) */
#define CACHEFLUSH_UM_X86_ONLY				0x4	/*!< Force x86/x64 UM flush exclusively */
#define CACHEFLUSH_UM_ARM64_ONLY			0x5	/*!< Force ARM Aarch64 UM flush exclusively */
#ifndef CACHEFLUSH_UM_TYPE
	#if defined(__i386__) || defined(__x86_64__)
		#define CACHEFLUSH_UM_TYPE CACHEFLUSH_UM_X86
	#elif defined(__aarch64__)
		#define CACHEFLUSH_UM_TYPE CACHEFLUSH_UM_ARM64
	#else
		#define CACHEFLUSH_UM_TYPE CACHEFLUSH_UM_GENERIC
	#endif
#endif

#endif	/* _CACHE_OPS_H_ */
