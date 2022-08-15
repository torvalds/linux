/*************************************************************************/ /*!
@File           cache_km.h
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

#ifndef CACHE_KM_H
#define CACHE_KM_H

#if defined(__linux__)
#include <linux/version.h>
#else
#define KERNEL_VERSION
#endif

#include "pvrsrv_error.h"
#include "os_cpu_cache.h"
#include "img_types.h"
#include "cache_ops.h"
#include "device.h"
#include "pmr.h"

typedef IMG_UINT32 PVRSRV_CACHE_OP_ADDR_TYPE;	/*!< Represents CPU address type required for CPU d-cache maintenance */
#define PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL	0x1	/*!< Operation requires CPU virtual address only */
#define PVRSRV_CACHE_OP_ADDR_TYPE_PHYSICAL	0x2	/*!< Operation requires CPU physical address only */
#define PVRSRV_CACHE_OP_ADDR_TYPE_BOTH		0x3	/*!< Operation requires both CPU virtual & physical addresses */

#include "connection_server.h"

/*
 * CacheOpInit() & CacheOpDeInit()
 *
 * This must be called to initialise the KM cache maintenance framework.
 * This is called early during the driver/module (un)loading phase.
 */
PVRSRV_ERROR CacheOpInit(void);
void CacheOpDeInit(void);

/*
 * CacheOpInit2() & CacheOpDeInit2()
 *
 * This must be called to initialise the UM cache maintenance framework.
 * This is called when the driver is loaded/unloaded from the kernel.
 */
PVRSRV_ERROR CacheOpInit2(void);
void CacheOpDeInit2(void);

/*
 * CacheOpExec()
 *
 * This is the primary CPU data-cache maintenance interface and it is
 * always guaranteed to be synchronous; the arguments supplied must be
 * pre-validated for performance reasons else the d-cache maintenance
 * operation might cause the underlying OS kernel to fault.
 */
PVRSRV_ERROR CacheOpExec(PPVRSRV_DEVICE_NODE psDevNode,
						void *pvVirtStart,
						void *pvVirtEnd,
						IMG_CPU_PHYADDR sCPUPhysStart,
						IMG_CPU_PHYADDR sCPUPhysEnd,
						PVRSRV_CACHE_OP uiCacheOp);

/*
 * CacheOpValExec()
 *
 * Same as CacheOpExec(), except arguments are _Validated_ before being
 * presented to the underlying OS kernel for CPU data-cache maintenance.
 * The uiAddress is the start CPU virtual address for the to-be d-cache
 * maintained PMR, it can be NULL in which case a remap will be performed
 * internally, if required for cache maintenance. This is primarily used
 * as the services client bridge call handler for synchronous user-mode
 * cache maintenance requests.
 */
PVRSRV_ERROR CacheOpValExec(PMR *psPMR,
							IMG_UINT64 uiAddress,
							IMG_DEVMEM_OFFSET_T uiOffset,
							IMG_DEVMEM_SIZE_T uiSize,
							PVRSRV_CACHE_OP uiCacheOp);

/*
 * CacheOpQueue()
 *
 * This is the secondary cache maintenance interface and it is not
 * guaranteed to be synchronous in that requests could be deferred
 * and executed asynchronously. This interface is primarily meant
 * as services client bridge call handler. Both uiInfoPgGFSeqNum
 * and ui32[Current,Next]FenceSeqNum implements an internal client
 * server queueing protocol so making use of this interface outside
 * of services client is not recommended and should not be done.
 */
PVRSRV_ERROR CacheOpQueue(CONNECTION_DATA *psConnection,
						PPVRSRV_DEVICE_NODE psDevNode,
						IMG_UINT32 ui32OpCount,
						PMR **ppsPMR,
						IMG_UINT64 *puiAddress,
						IMG_DEVMEM_OFFSET_T *puiOffset,
						IMG_DEVMEM_SIZE_T *puiSize,
						PVRSRV_CACHE_OP *puiCacheOp,
						IMG_UINT32 ui32OpTimeline);

/*
 * CacheOpLog()
 *
 * This is used for logging client cache maintenance operations that
 * was executed in user-space.
 */
PVRSRV_ERROR CacheOpLog(PMR *psPMR,
						IMG_UINT64 uiAddress,
						IMG_DEVMEM_OFFSET_T uiOffset,
						IMG_DEVMEM_SIZE_T uiSize,
						IMG_UINT64 ui64StartTime,
						IMG_UINT64 ui64EndTime,
						PVRSRV_CACHE_OP uiCacheOp);

#endif	/* CACHE_KM_H */
