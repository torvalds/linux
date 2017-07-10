/*************************************************************************/ /*!
@File           cache.h
@Title          CPU cache management header
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

#ifndef _CACHE_KM_H_
#define _CACHE_KM_H_

#if defined(LINUX)
#include <linux/version.h>
#endif

#include "pvrsrv_error.h"
#include "img_types.h"
#include "cache_ops.h"
#include "device.h"
#include "pmr.h"

typedef IMG_UINT32 PVRSRV_CACHE_OP_ADDR_TYPE;	/*!< Type represents address required for cache op. */
#define PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL	0x1	/*!< Operation requires virtual address only */
#define PVRSRV_CACHE_OP_ADDR_TYPE_PHYSICAL	0x2	/*!< Operation requires physical address only */
#define PVRSRV_CACHE_OP_ADDR_TYPE_BOTH		0x3	/*!< Operation requires both virtual & physical addresses */

#define CACHEFLUSH_KM_RANGEBASED_DEFERRED	0x1	/*!< Services KM using deferred (i.e asynchronous) range-based flush */
#define CACHEFLUSH_KM_RANGEBASED			0x2	/*!< Services KM using immediate (i.e synchronous) range-based flush */
#define CACHEFLUSH_KM_GLOBAL				0x3	/*!< Services KM using global flush */
#ifndef CACHEFLUSH_KM_TYPE						/*!< Type represents cache maintenance operation method */
	#if defined(__x86__)
		/* Default for x86/x86_64 is global */
		#define CACHEFLUSH_KM_TYPE CACHEFLUSH_KM_GLOBAL
	#elif defined(__aarch64__)
		#if defined(LINUX) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0))
			/* Default here is range-based (i.e. no linux global flush) */
			#define CACHEFLUSH_KM_TYPE CACHEFLUSH_KM_RANGEBASED
		#else
			/* Default here is global (i.e. OS supports global flush) */
			#define CACHEFLUSH_KM_TYPE CACHEFLUSH_KM_GLOBAL
		#endif
	#else
		/* Default for other architecture is range-based */
		#define CACHEFLUSH_KM_TYPE CACHEFLUSH_KM_RANGEBASED
	#endif
#else
	#if (CACHEFLUSH_KM_TYPE == CACHEFLUSH_KM_GLOBAL)
		#if defined(__mips__) 
			/* Architecture does not support global cache maintenance */
			#error "CACHEFLUSH_KM_GLOBAL is not supported on architecture"
		#elif defined(__aarch64__)
			#if defined(LINUX) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0))
				/* Linux revisions does not support global cache maintenance */
				#error "CACHEFLUSH_KM_GLOBAL is not supported on Linux v4.2 onwards"
			#endif
		#endif
	#endif
#endif

/*
	If we get multiple cache operations before the operation which will
	trigger the operation to happen then we need to make sure we do
	the right thing. Used for global cache maintenance
*/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SetCacheOp)
#endif
static INLINE PVRSRV_CACHE_OP SetCacheOp(PVRSRV_CACHE_OP uiCurrent, PVRSRV_CACHE_OP uiNew)
{
	PVRSRV_CACHE_OP uiRet;
	uiRet = uiCurrent | uiNew;
	return uiRet;
}

/*
	Cache maintenance framework API
*/
PVRSRV_ERROR CacheOpInit(void);
PVRSRV_ERROR CacheOpDeInit(void);

/* This interface is always guaranteed to be synchronous */
PVRSRV_ERROR CacheOpExec (PMR *psPMR,
						IMG_DEVMEM_OFFSET_T uiOffset,
						IMG_DEVMEM_SIZE_T uiSize,
						PVRSRV_CACHE_OP uiCacheOp);

/* This interface _may_ defer cache-ops (i.e. asynchronous) */
PVRSRV_ERROR CacheOpQueue (IMG_UINT32 ui32OpCount,
						PMR **ppsPMR,
						IMG_DEVMEM_OFFSET_T *puiOffset,
						IMG_DEVMEM_SIZE_T *puiSize,
						PVRSRV_CACHE_OP *puiCacheOp,
						IMG_UINT32 *pui32OpSeqNum);

/* This interface is used to log user-mode cache-ops */
PVRSRV_ERROR CacheOpLog (PMR *psPMR,
						IMG_DEVMEM_OFFSET_T uiOffset,
						IMG_DEVMEM_SIZE_T uiSize,
						IMG_UINT64 ui64QueuedTimeMs,
						IMG_UINT64 ui64ExecuteTimeMs,
						PVRSRV_CACHE_OP uiCacheOp);

/* This interface must be used to fence for pending cache-ops before kicks */
PVRSRV_ERROR CacheOpFence (RGXFWIF_DM eOpType, IMG_UINT32 ui32OpSeqNum);

/* This interface is used for notification of completed cache-ops */
PVRSRV_ERROR CacheOpSetTimeline (IMG_INT32 i32OpTimeline);

/* This interface is used for retrieving the processor d-cache line size */
PVRSRV_ERROR CacheOpGetLineSize (IMG_UINT32 *pui32L1DataCacheLineSize);
#endif	/* _CACHE_KM_H_ */

