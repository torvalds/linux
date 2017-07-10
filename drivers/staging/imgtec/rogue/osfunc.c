/*************************************************************************/ /*!
@File
@Title          Environment related functions
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

#include <linux/version.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/div64.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h> 
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/hardirq.h>
#include <asm/tlbflush.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <asm/uaccess.h>
#include <linux/spinlock.h>
#if defined(PVR_LINUX_MISR_USING_WORKQUEUE) || \
	defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE) || \
	defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || \
	defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE) || \
	defined(PVR_LINUX_USING_WORKQUEUES)
#include <linux/workqueue.h>
#endif
#include <linux/kthread.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
#include <linux/pfn_t.h>
#include <linux/pfn.h>
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */

#include "log2.h"
#include "osfunc.h"
#include "img_types.h"
#include "allocmem.h"
#include "devicemem_server_utils.h"
#include "pvr_debugfs.h"
#include "event.h"
#include "linkage.h"
#include "pvr_uaccess.h"
#include "pvr_debug.h"
#include "pvrsrv_memallocflags.h"
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif
#include "physmem_osmem_linux.h"

#if defined(SUPPORT_PVRSRV_GPUVIRT)
#include "dma_support.h"
#endif

#include "kernel_compatibility.h"

#if defined(VIRTUAL_PLATFORM)
#define EVENT_OBJECT_TIMEOUT_US		(120000000ULL)
#else
#if defined(EMULATOR)
#define EVENT_OBJECT_TIMEOUT_US		(2000000ULL)
#else
#define EVENT_OBJECT_TIMEOUT_US		(100000ULL)
#endif /* EMULATOR */
#endif

/*
 * Main driver lock, used to ensure driver code is single threaded. There are
 * some places where this lock must not be taken, such as in the mmap related
 * driver entry points.
 */
static DEFINE_MUTEX(gPVRSRVLock);

static void *g_pvBridgeBuffers = NULL;
static atomic_t g_DriverSuspended;

struct task_struct *BridgeLockGetOwner(void);
IMG_BOOL BridgeLockIsLocked(void);


PVRSRV_ERROR OSPhyContigPagesAlloc(PVRSRV_DEVICE_NODE *psDevNode, size_t uiSize,
							PG_HANDLE *psMemHandle, IMG_DEV_PHYADDR *psDevPAddr)
{
	IMG_CPU_PHYADDR sCpuPAddr;
	struct page *psPage;
	IMG_UINT32	ui32Order=0;

	PVR_ASSERT(uiSize != 0);
	/*Align the size to the page granularity */
	uiSize = PAGE_ALIGN(uiSize);

	/*Get the order to be used with the allocation */
	ui32Order = get_order(uiSize);

	/*allocate the pages */
	psPage = alloc_pages(GFP_KERNEL, ui32Order);
	if (psPage == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	uiSize = (1 << ui32Order) * PAGE_SIZE;

	psMemHandle->u.pvHandle = psPage;
	psMemHandle->ui32Order = ui32Order;
	sCpuPAddr.uiAddr =  IMG_CAST_TO_CPUPHYADDR_UINT(page_to_phys(psPage));

	/*
	 * Even when more pages are allocated as base MMU object we still need one single physical address because
	 * they are physically contiguous.
	 */
	PhysHeapCpuPAddrToDevPAddr(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL], 1, psDevPAddr, &sCpuPAddr);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	    PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA,
	                                        uiSize,
	                                        (IMG_UINT64)(uintptr_t) psPage);
#else
	PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA,
	                             psPage,
								 sCpuPAddr,
								 uiSize,
								 NULL);
#endif
#endif

	return PVRSRV_OK;
}

void OSPhyContigPagesFree(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle)
{
	struct page *psPage = (struct page*) psMemHandle->u.pvHandle;
	IMG_UINT32	uiSize, uiPageCount=0;

	uiPageCount = (1 << psMemHandle->ui32Order);
	uiSize = (uiPageCount * PAGE_SIZE);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA,
	                                      (IMG_UINT64)(uintptr_t) psPage);
#else
	PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT64)(uintptr_t) psPage);
#endif
#endif

	__free_pages(psPage, psMemHandle->ui32Order);
	psMemHandle->ui32Order = 0;
}

PVRSRV_ERROR OSPhyContigPagesMap(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle,
						size_t uiSize, IMG_DEV_PHYADDR *psDevPAddr,
						void **pvPtr)
{
	size_t actualSize = 1 << (PAGE_SHIFT + psMemHandle->ui32Order);
	*pvPtr = kmap((struct page*)psMemHandle->u.pvHandle);

	PVR_UNREFERENCED_PARAMETER(psDevPAddr);

	PVR_UNREFERENCED_PARAMETER(actualSize); /* If we don't take an #ifdef path */
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA, actualSize);
#else
	{
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA,
									 *pvPtr,
									 sCpuPAddr,
									 actualSize,
									 NULL);
	}
#endif
#endif

	return PVRSRV_OK;
}

void OSPhyContigPagesUnmap(PVRSRV_DEVICE_NODE *psDevNode, PG_HANDLE *psMemHandle, void *pvPtr)
{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	/* Mapping is done a page at a time */
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA, (1 << (PAGE_SHIFT + psMemHandle->ui32Order)));
#else
	PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA, (IMG_UINT64)(uintptr_t)pvPtr);
#endif
#endif

	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(pvPtr);

	kunmap((struct page*) psMemHandle->u.pvHandle);
}

PVRSRV_ERROR OSPhyContigPagesClean(PVRSRV_DEVICE_NODE *psDevNode,
                                   PG_HANDLE *psMemHandle,
                                   IMG_UINT32 uiOffset,
                                   IMG_UINT32 uiLength)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	struct page* psPage = (struct page*) psMemHandle->u.pvHandle;

	void* pvVirtAddrStart = kmap(psPage) + uiOffset;
	IMG_CPU_PHYADDR sPhysStart, sPhysEnd;

	if (uiLength == 0)
	{
		goto e0;
	}

	if ((uiOffset + uiLength) > ((1 << psMemHandle->ui32Order) * PAGE_SIZE))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid size params, uiOffset %u, uiLength %u",
				__FUNCTION__,
				uiOffset,
				uiLength));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	sPhysStart.uiAddr = page_to_phys(psPage) + uiOffset;
	sPhysEnd.uiAddr = sPhysStart.uiAddr + uiLength;

	OSCleanCPUCacheRangeKM(psDevNode,
	                       pvVirtAddrStart,
	                       pvVirtAddrStart + uiLength,
	                       sPhysStart,
	                       sPhysEnd);

e0:
	kunmap(psPage);

	return eError;
}

#if defined(__GNUC__)
#define PVRSRV_MEM_ALIGN __attribute__ ((aligned (0x8)))
#define PVRSRV_MEM_ALIGN_MASK (0x7)
#else
#error "PVRSRV Alignment macros need to be defined for this compiler"
#endif

IMG_UINT32 OSCPUCacheAttributeSize(IMG_DCACHE_ATTRIBUTE eCacheAttribute)
{
	IMG_UINT32 uiSize = 0;

	switch(eCacheAttribute)
	{
		case PVR_DCACHE_LINE_SIZE:
			uiSize = cache_line_size();
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR, "%s: Invalid cache attribute type %d",
					__FUNCTION__, (IMG_UINT32)eCacheAttribute));
			PVR_ASSERT(0);
			break;
	}

	return uiSize;
}

IMG_UINT32 OSVSScanf(IMG_CHAR *pStr, const IMG_CHAR *pszFormat, ...)
{
	va_list argList;
	IMG_INT32 iCount = 0;

	va_start(argList, pszFormat);
	iCount = vsscanf(pStr, pszFormat, argList);
	va_end(argList);

	return iCount;
}

IMG_INT OSMemCmp(void *pvBufA, void *pvBufB, size_t uiLen)
{
	return (IMG_INT) memcmp(pvBufA, pvBufB, uiLen);
}

IMG_CHAR *OSStringNCopy(IMG_CHAR *pszDest, const IMG_CHAR *pszSrc, size_t uSize)
{
	return strncpy(pszDest, pszSrc, uSize);
}

IMG_INT32 OSSNPrintf(IMG_CHAR *pStr, size_t ui32Size, const IMG_CHAR *pszFormat, ...)
{
	va_list argList;
	IMG_INT32 iCount;

	va_start(argList, pszFormat);
	iCount = vsnprintf(pStr, (size_t)ui32Size, pszFormat, argList);
	va_end(argList);

	return iCount;
}

size_t OSStringLength(const IMG_CHAR *pStr)
{
	return strlen(pStr);
}

size_t OSStringNLength(const IMG_CHAR *pStr, size_t uiCount)
{
	return strnlen(pStr, uiCount);
}

IMG_INT32 OSStringCompare(const IMG_CHAR *pStr1, const IMG_CHAR *pStr2)
{
	return strcmp(pStr1, pStr2);
}

IMG_INT32 OSStringNCompare(const IMG_CHAR *pStr1, const IMG_CHAR *pStr2,
                          size_t uiSize)
{
	return strncmp(pStr1, pStr2, uiSize);
}

PVRSRV_ERROR OSStringToUINT32(const IMG_CHAR *pStr, IMG_UINT32 ui32Base,
                              IMG_UINT32 *ui32Result)
{
	if (kstrtou32(pStr, ui32Base, ui32Result) != 0)
		return PVRSRV_ERROR_CONVERSION_FAILED;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSInitEnvData(void)
{
	/* allocate memory for the bridge buffers to be used during an ioctl */
	g_pvBridgeBuffers = OSAllocMem(PVRSRV_MAX_BRIDGE_IN_SIZE + PVRSRV_MAX_BRIDGE_OUT_SIZE);
	if (g_pvBridgeBuffers == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	atomic_set(&g_DriverSuspended, 0);

	LinuxInitPhysmem();

	return PVRSRV_OK;
}


void OSDeInitEnvData(void)
{

	LinuxDeinitPhysmem();

	if (g_pvBridgeBuffers)
	{
		/* free-up the memory allocated for bridge buffers */
		OSFreeMem(g_pvBridgeBuffers);
		g_pvBridgeBuffers = NULL;
	}
}

PVRSRV_ERROR OSGetGlobalBridgeBuffers(void **ppvBridgeInBuffer,
									  void **ppvBridgeOutBuffer)
{
	PVR_ASSERT (ppvBridgeInBuffer && ppvBridgeOutBuffer);

	*ppvBridgeInBuffer = g_pvBridgeBuffers;
	*ppvBridgeOutBuffer = *ppvBridgeInBuffer + PVRSRV_MAX_BRIDGE_IN_SIZE;

	return PVRSRV_OK;
}

IMG_BOOL OSSetDriverSuspended(void)
{
	int suspend_level = atomic_inc_return(&g_DriverSuspended);
	return (1 != suspend_level)? IMG_FALSE: IMG_TRUE;
}

IMG_BOOL OSClearDriverSuspended(void)
{
	int suspend_level = atomic_dec_return(&g_DriverSuspended);
	return (0 != suspend_level)? IMG_FALSE: IMG_TRUE;
}

IMG_BOOL OSGetDriverSuspended(void)
{
	return (0 < atomic_read(&g_DriverSuspended))? IMG_TRUE: IMG_FALSE;
}

void OSReleaseThreadQuanta(void)
{
	schedule();
}

/* Not matching/aligning this API to the Clockus() API above to avoid necessary
 * multiplication/division operations in calling code.
 */
static inline IMG_UINT64 Clockns64(void)
{
	IMG_UINT64 timenow;

	/* Kernel thread preempt protection. Some architecture implementations 
	 * (ARM) of sched_clock are not preempt safe when the kernel is configured 
	 * as such e.g. CONFIG_PREEMPT and others.
	 */
	preempt_disable();

	/* Using sched_clock instead of ktime_get since we need a time stamp that
	 * correlates with that shown in kernel logs and trace data not one that
	 * is a bit behind. */
	timenow = sched_clock();

	preempt_enable();

	return timenow;
}

IMG_UINT64 OSClockns64(void)
{
	return Clockns64();	
}

IMG_UINT64 OSClockus64(void)
{
	IMG_UINT64 timenow = Clockns64();
	IMG_UINT32 remainder;

	return OSDivide64r64(timenow, 1000, &remainder);
}

IMG_UINT32 OSClockus(void)
{
	return (IMG_UINT32) OSClockus64();
}

IMG_UINT32 OSClockms(void)
{
	IMG_UINT64 timenow = Clockns64();
	IMG_UINT32 remainder;

	return OSDivide64(timenow, 1000000, &remainder);
}

static inline IMG_UINT64 KClockns64(void)
{
	ktime_t sTime = ktime_get();

	return sTime.tv64;
}

PVRSRV_ERROR OSClockMonotonicns64(IMG_UINT64 *pui64Time)
{
	*pui64Time = KClockns64();
	return PVRSRV_OK;
}

PVRSRV_ERROR OSClockMonotonicus64(IMG_UINT64 *pui64Time)
{
	IMG_UINT64 timenow = KClockns64();
	IMG_UINT32 remainder;

	*pui64Time = OSDivide64r64(timenow, 1000, &remainder);
	return PVRSRV_OK;
}

IMG_UINT64 OSClockMonotonicRawns64(void)
{
	struct timespec ts;

	getrawmonotonic(&ts);
	return (IMG_UINT64) ts.tv_sec * 1000000000 + ts.tv_nsec;
}

IMG_UINT64 OSClockMonotonicRawus64(void)
{
	IMG_UINT32 rem;
	return OSDivide64r64(OSClockMonotonicRawns64(), 1000, &rem);
}

/*
	OSWaitus
*/
void OSWaitus(IMG_UINT32 ui32Timeus)
{
	udelay(ui32Timeus);
}


/*
	OSSleepms
*/
void OSSleepms(IMG_UINT32 ui32Timems)
{
	msleep(ui32Timems);
}


INLINE IMG_UINT64 OSGetCurrentProcessVASpaceSize(void)
{
	return (IMG_UINT64)TASK_SIZE;
}

INLINE IMG_PID OSGetCurrentProcessID(void)
{
	if (in_interrupt())
	{
		return KERNEL_ID;
	}

	return (IMG_PID)task_tgid_nr(current);
}

INLINE IMG_CHAR *OSGetCurrentProcessName(void)
{
	return current->comm;
}

INLINE uintptr_t OSGetCurrentThreadID(void)
{
	if (in_interrupt())
	{
		return KERNEL_ID;
	}

	return current->pid;
}

IMG_PID OSGetCurrentClientProcessIDKM(void)
{
	return OSGetCurrentProcessID();
}

IMG_CHAR *OSGetCurrentClientProcessNameKM(void)
{
	return OSGetCurrentProcessName();
}

uintptr_t OSGetCurrentClientThreadIDKM(void)
{
	return OSGetCurrentThreadID();
}

size_t OSGetPageSize(void)
{
	return PAGE_SIZE;
}

size_t OSGetPageShift(void)
{
	return PAGE_SHIFT;
}

size_t OSGetPageMask(void)
{
	return (OSGetPageSize()-1);
}

size_t OSGetOrder(size_t uSize)
{
	return get_order(PAGE_ALIGN(uSize));
}

typedef struct
{
	int os_error;
	PVRSRV_ERROR pvr_error;
} error_map_t;

/* return -ve versions of POSIX errors as they are used in this form */
static const error_map_t asErrorMap[] =
{
	{-EFAULT, PVRSRV_ERROR_BRIDGE_EFAULT},
	{-EINVAL, PVRSRV_ERROR_BRIDGE_EINVAL},
	{-ENOMEM, PVRSRV_ERROR_BRIDGE_ENOMEM},
	{-ERANGE, PVRSRV_ERROR_BRIDGE_ERANGE},
	{-EPERM,  PVRSRV_ERROR_BRIDGE_EPERM},
	{-ENOTTY, PVRSRV_ERROR_BRIDGE_ENOTTY},
	{-ENOTTY, PVRSRV_ERROR_BRIDGE_CALL_FAILED},
	{-ERANGE, PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL},
	{-ENOMEM, PVRSRV_ERROR_OUT_OF_MEMORY},
	{-EINVAL, PVRSRV_ERROR_INVALID_PARAMS},

	{0,       PVRSRV_OK}
};

#define num_rows(a) (sizeof(a)/sizeof(a[0]))

int PVRSRVToNativeError(PVRSRV_ERROR e)
{
	int os_error = -EFAULT;
	int i;
	for (i = 0; i < num_rows(asErrorMap); i++)
	{
		if (e == asErrorMap[i].pvr_error)
		{
			os_error = asErrorMap[i].os_error;
			break;
		}
	}
	return os_error;
}

#if defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE)
typedef struct  _MISR_DATA_ {
	struct workqueue_struct *psWorkQueue;
	struct work_struct sMISRWork;
	PFN_MISR pfnMISR;
	void *hData;
} MISR_DATA;

/*
	MISRWrapper
*/
static void MISRWrapper(struct work_struct *data)
{
	MISR_DATA *psMISRData = container_of(data, MISR_DATA, sMISRWork);

	psMISRData->pfnMISR(psMISRData->hData);
}

/*
	OSInstallMISR
*/
PVRSRV_ERROR OSInstallMISR(IMG_HANDLE *hMISRData, PFN_MISR pfnMISR,
							void *hData)
{
	MISR_DATA *psMISRData;

	psMISRData = OSAllocMem(sizeof(*psMISRData));
	if (psMISRData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psMISRData->hData = hData;
	psMISRData->pfnMISR = pfnMISR;

	PVR_TRACE(("Installing MISR with cookie %p", psMISRData));

	psMISRData->psWorkQueue = create_singlethread_workqueue("pvr_workqueue");

	if (psMISRData->psWorkQueue == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "OSInstallMISR: create_singlethreaded_workqueue failed"));
		OSFreeMem(psMISRData);
		return PVRSRV_ERROR_UNABLE_TO_CREATE_THREAD;
	}

	INIT_WORK(&psMISRData->sMISRWork, MISRWrapper);

	*hMISRData = (IMG_HANDLE) psMISRData;

	return PVRSRV_OK;
}

/*
	OSUninstallMISR
*/
PVRSRV_ERROR OSUninstallMISR(IMG_HANDLE hMISRData)
{
	MISR_DATA *psMISRData = (MISR_DATA *) hMISRData;

	PVR_TRACE(("Uninstalling MISR with cookie %p", psMISRData));

	destroy_workqueue(psMISRData->psWorkQueue);
	OSFreeMem(psMISRData);

	return PVRSRV_OK;
}

/*
	OSScheduleMISR
*/
PVRSRV_ERROR OSScheduleMISR(IMG_HANDLE hMISRData)
{
	MISR_DATA *psMISRData = (MISR_DATA *) hMISRData;

	/*
		Note:

		In the case of NO_HARDWARE we want the driver to be synchronous so
		that we don't have to worry about waiting for previous operations
		to complete
	*/
#if defined(NO_HARDWARE)
	psMISRData->pfnMISR(psMISRData->hData);
	return PVRSRV_OK;
#else
	{
		bool rc = queue_work(psMISRData->psWorkQueue, &psMISRData->sMISRWork);
		return (rc ? PVRSRV_OK : PVRSRV_ERROR_ALREADY_EXISTS);
	}
#endif
}
#else	/* defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE) */
#if defined(PVR_LINUX_MISR_USING_WORKQUEUE)
typedef struct  _MISR_DATA_ {
	struct work_struct sMISRWork;
	PFN_MISR pfnMISR;
	void *hData;
} MISR_DATA;

/*
	MISRWrapper
*/
static void MISRWrapper(struct work_struct *data)
{
	MISR_DATA *psMISRData = container_of(data, MISR_DATA, sMISRWork);

	psMISRData->pfnMISR(psMISRData->hData);
}

/*
	OSInstallMISR
*/
PVRSRV_ERROR OSInstallMISR(IMG_HANDLE *hMISRData, PFN_MISR pfnMISR, void *hData)
{
	MISR_DATA *psMISRData;

	psMISRData = OSAllocMem(sizeof(*psMISRData));
	if (psMISRData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psMISRData->hData = hData;
	psMISRData->pfnMISR = pfnMISR;

	PVR_TRACE(("Installing MISR with cookie %p", psMISRData));

	INIT_WORK(&psMISRData->sMISRWork, MISRWrapper);

	*hMISRData = (IMG_HANDLE) psMISRData;

	return PVRSRV_OK;
}


/*
	OSUninstallMISR
*/
PVRSRV_ERROR OSUninstallMISR(IMG_HANDLE hMISRData)
{
	PVR_TRACE(("Uninstalling MISR with cookie %p", psMISRData));

	flush_scheduled_work();

	OSFreeMem(hMISRData);

	return PVRSRV_OK;
}

/*
	OSScheduleMISR
*/
PVRSRV_ERROR OSScheduleMISR(IMG_HANDLE hMISRData)
{
	MISR_DATA *psMISRData = hMISRData;
#if defined(NO_HARDWARE)
	psMISRData->pfnMISR(psMISRData->hData);
#else
	schedule_work(&psMISRData->sMISRWork);
#endif
	return PVRSRV_OK;
}

#else	/* #if defined(PVR_LINUX_MISR_USING_WORKQUEUE) */
typedef struct _MISR_DATA_ {
	struct tasklet_struct sMISRTasklet;
	PFN_MISR pfnMISR;
	void *hData;
} MISR_DATA;

/*
	MISRWrapper
*/
static void MISRWrapper(unsigned long data)
{
	MISR_DATA *psMISRData = (MISR_DATA *) data;

	psMISRData->pfnMISR(psMISRData->hData);
}

/*
	OSInstallMISR
*/
PVRSRV_ERROR OSInstallMISR(IMG_HANDLE *hMISRData, PFN_MISR pfnMISR, void *hData)
{
	MISR_DATA *psMISRData;

	psMISRData = OSAllocMem(sizeof(*psMISRData));
	if (psMISRData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psMISRData->hData = hData;
	psMISRData->pfnMISR = pfnMISR;

	PVR_TRACE(("Installing MISR with cookie %p", psMISRData));

	tasklet_init(&psMISRData->sMISRTasklet, MISRWrapper, (unsigned long)psMISRData);

	*hMISRData = (IMG_HANDLE) psMISRData;

	return PVRSRV_OK;
}

/*
	OSUninstallMISR
*/
PVRSRV_ERROR OSUninstallMISR(IMG_HANDLE hMISRData)
{
	MISR_DATA *psMISRData = (MISR_DATA *) hMISRData;

	PVR_TRACE(("Uninstalling MISR with cookie %p", psMISRData));

	tasklet_kill(&psMISRData->sMISRTasklet);

	return PVRSRV_OK;
}

/*
	OSScheduleMISR
*/
PVRSRV_ERROR OSScheduleMISR(IMG_HANDLE hMISRData)
{
	MISR_DATA *psMISRData = (MISR_DATA *) hMISRData;

#if defined(NO_HARDWARE)
	psMISRData->pfnMISR(psMISRData->hData);
#else
	tasklet_schedule(&psMISRData->sMISRTasklet);
#endif
	return PVRSRV_OK;
}

#endif /* #if defined(PVR_LINUX_MISR_USING_WORKQUEUE) */
#endif /* #if defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE) */

/* OS specific values for thread priority */
static const IMG_INT32 ai32OSPriorityValues[OS_THREAD_LAST_PRIORITY] =
{
	-20, /* OS_THREAD_HIGHEST_PRIORITY */
	-10, /* OS_THREAD_HIGH_PRIORITY */
	  0, /* OS_THREAD_NORMAL_PRIORITY */
	  9, /* OS_THREAD_LOW_PRIORITY */
	 19, /* OS_THREAD_LOWEST_PRIORITY */
	-22, /* OS_THREAD_NOSET_PRIORITY */
};

typedef struct {
	struct task_struct *kthread;
	PFN_THREAD pfnThread;
	void *hData;
	OS_THREAD_LEVEL eThreadPriority;
} OSThreadData;

static int OSThreadRun(void *data)
{
	OSThreadData *psOSThreadData = data;

	/* If i32NiceValue is acceptable, set the nice value for the new thread */
	if (psOSThreadData->eThreadPriority != OS_THREAD_NOSET_PRIORITY &&
	         psOSThreadData->eThreadPriority < OS_THREAD_LAST_PRIORITY)
		set_user_nice(current, ai32OSPriorityValues[psOSThreadData->eThreadPriority]);

	/* Call the client's kernel thread with the client's data pointer */
	psOSThreadData->pfnThread(psOSThreadData->hData);

	/* Wait for OSThreadDestroy() to call kthread_stop() */
	while (!kthread_should_stop())
	{
		 schedule();
	}

	return 0;
}

PVRSRV_ERROR OSThreadCreate(IMG_HANDLE *phThread,
							IMG_CHAR *pszThreadName,
							PFN_THREAD pfnThread,
							void *hData)
{
	return OSThreadCreatePriority(phThread, pszThreadName, pfnThread, hData, OS_THREAD_NOSET_PRIORITY);
}

PVRSRV_ERROR OSThreadCreatePriority(IMG_HANDLE *phThread,
									IMG_CHAR *pszThreadName,
									PFN_THREAD pfnThread,
									void *hData,
									OS_THREAD_LEVEL eThreadPriority)
{
	OSThreadData *psOSThreadData;
	PVRSRV_ERROR eError;

	psOSThreadData = OSAllocMem(sizeof(*psOSThreadData));
	if (psOSThreadData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	psOSThreadData->pfnThread = pfnThread;
	psOSThreadData->hData = hData;
	psOSThreadData->eThreadPriority= eThreadPriority;
	psOSThreadData->kthread = kthread_run(OSThreadRun, psOSThreadData, pszThreadName);

	if (IS_ERR(psOSThreadData->kthread))
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_kthread;
	}

	*phThread = psOSThreadData;

	return PVRSRV_OK;

fail_kthread:
	OSFreeMem(psOSThreadData);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR OSThreadDestroy(IMG_HANDLE hThread)
{
	OSThreadData *psOSThreadData = hThread;
	int ret;

	/* Let the thread know we are ready for it to end and wait for it. */
	ret = kthread_stop(psOSThreadData->kthread);
	if (0 != ret)
	{
		PVR_DPF((PVR_DBG_WARNING, "kthread_stop failed(%d)", ret));
		return PVRSRV_ERROR_RETRY;
	}

	OSFreeMem(psOSThreadData);

	return PVRSRV_OK;
}

void OSPanic(void)
{
	BUG();

#if defined(__KLOCWORK__)
	/* Klocworks does not understand that BUG is terminal... */
	abort();
#endif
}

PVRSRV_ERROR OSSetThreadPriority(IMG_HANDLE hThread,
								 IMG_UINT32  nThreadPriority,
								 IMG_UINT32  nThreadWeight)
{
	PVR_UNREFERENCED_PARAMETER(hThread);
	PVR_UNREFERENCED_PARAMETER(nThreadPriority);
	PVR_UNREFERENCED_PARAMETER(nThreadWeight);
 	/* Default priorities used on this platform */
	
	return PVRSRV_OK;
}

void *
OSMapPhysToLin(IMG_CPU_PHYADDR BasePAddr,
			   size_t ui32Bytes,
			   IMG_UINT32 ui32MappingFlags)
{
	void *pvLinAddr;

	if (ui32MappingFlags & ~(PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK))
	{
		PVR_ASSERT(!"Found non-cpu cache mode flag when mapping to the cpu");
		return NULL;
	}

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	/*
	 * This is required to support DMA physheaps for GPU virtualization.
	 * Unfortunately, if a region of kernel managed memory is turned into
	 * a DMA buffer, conflicting mappings can come about easily on Linux
	 * as the original memory is mapped by the kernel as normal cached
	 * memory whilst DMA buffers are mapped mostly as uncached device or
	 * cache-coherent device memory. In both cases the system will have
	 * two conflicting mappings for the same memory region and will have
	 * "undefined behaviour" for most processors notably ARMv6 onwards
	 * and some x86 micro-architectures
	 *
	 * As a result we perform ioremapping manually, for DMA physheap
	 * allocations, by translating from CPU/VA <-> BUS/PA.
	 */
	pvLinAddr = SysDmaDevPAddrToCpuVAddr(BasePAddr.uiAddr, ui32Bytes);
	if (pvLinAddr != NULL)
	{
		return pvLinAddr;
	}
#endif

	switch (ui32MappingFlags)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
			pvLinAddr = (void *)ioremap_nocache(BasePAddr.uiAddr, ui32Bytes);
			break;
		case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
#if defined(CONFIG_X86) || defined(CONFIG_ARM) || defined(CONFIG_ARM64)
			pvLinAddr = (void *)ioremap_wc(BasePAddr.uiAddr, ui32Bytes);
#else
			pvLinAddr = (void *)ioremap_nocache(BasePAddr.uiAddr, ui32Bytes);
#endif
			break;
		case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
#if defined(CONFIG_X86) || defined(CONFIG_ARM)
			pvLinAddr = (void *)ioremap_cache(BasePAddr.uiAddr, ui32Bytes);
#else
			pvLinAddr = (void *)ioremap(BasePAddr.uiAddr, ui32Bytes);
#endif
			break;
		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT:
		case PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT:
			PVR_ASSERT(!"Unexpected cpu cache mode");
			pvLinAddr = NULL;
			break;
		default:
			PVR_ASSERT(!"Unsupported cpu cache mode");
			pvLinAddr = NULL;
			break;
	}

	return pvLinAddr;
}


IMG_BOOL
OSUnMapPhysToLin(void *pvLinAddr, size_t ui32Bytes, IMG_UINT32 ui32MappingFlags)
{
	PVR_UNREFERENCED_PARAMETER(ui32Bytes);

	if (ui32MappingFlags & ~(PVRSRV_MEMALLOCFLAG_CPU_CACHE_MODE_MASK))
	{
		PVR_ASSERT(!"Found non-cpu cache mode flag when unmapping from the cpu");
		return IMG_FALSE;
	}

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	if (SysDmaCpuVAddrToDevPAddr(pvLinAddr))
	{
		return IMG_TRUE;
	}
#endif

	iounmap(pvLinAddr);

	return IMG_TRUE;
}

#define	OS_MAX_TIMERS	8

/* Timer callback strucure used by OSAddTimer */
typedef struct TIMER_CALLBACK_DATA_TAG
{
	IMG_BOOL			bInUse;
	PFN_TIMER_FUNC		pfnTimerFunc;
	void				*pvData;
	struct timer_list	sTimer;
	IMG_UINT32			ui32Delay;
	IMG_BOOL			bActive;
#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	struct work_struct	sWork;
#endif
}TIMER_CALLBACK_DATA;

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
static struct workqueue_struct	*psTimerWorkQueue;
#endif

static TIMER_CALLBACK_DATA sTimers[OS_MAX_TIMERS];

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
static DEFINE_MUTEX(sTimerStructLock);
#else
/* The lock is used to control access to sTimers */
static DEFINE_SPINLOCK(sTimerStructLock);
#endif

static void OSTimerCallbackBody(TIMER_CALLBACK_DATA *psTimerCBData)
{
	if (!psTimerCBData->bActive)
		return;

	/* call timer callback */
	psTimerCBData->pfnTimerFunc(psTimerCBData->pvData);

	/* reset timer */
	mod_timer(&psTimerCBData->sTimer, psTimerCBData->ui32Delay + jiffies);
}


/*************************************************************************/ /*!
@Function       OSTimerCallbackWrapper
@Description    OS specific timer callback wrapper function
@Input          uData    Timer callback data
*/ /**************************************************************************/
static void OSTimerCallbackWrapper(uintptr_t uData)
{
	TIMER_CALLBACK_DATA	*psTimerCBData = (TIMER_CALLBACK_DATA*)uData;

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	int res;

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
	res = queue_work(psTimerWorkQueue, &psTimerCBData->sWork);
#else
	res = schedule_work(&psTimerCBData->sWork);
#endif
	if (res == 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "OSTimerCallbackWrapper: work already queued"));
	}
#else
	OSTimerCallbackBody(psTimerCBData);
#endif
}


#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
static void OSTimerWorkQueueCallBack(struct work_struct *psWork)
{
	TIMER_CALLBACK_DATA *psTimerCBData = container_of(psWork, TIMER_CALLBACK_DATA, sWork);

	OSTimerCallbackBody(psTimerCBData);
}
#endif

IMG_HANDLE OSAddTimer(PFN_TIMER_FUNC pfnTimerFunc, void *pvData, IMG_UINT32 ui32MsTimeout)
{
	TIMER_CALLBACK_DATA	*psTimerCBData;
	IMG_UINT32		ui32i;
#if !(defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE))
	unsigned long		ulLockFlags;
#endif

	/* check callback */
	if(!pfnTimerFunc)
	{
		PVR_DPF((PVR_DBG_ERROR, "OSAddTimer: passed invalid callback"));
		return NULL;
	}

	/* Allocate timer callback data structure */
#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	mutex_lock(&sTimerStructLock);
#else
	spin_lock_irqsave(&sTimerStructLock, ulLockFlags);
#endif
	for (ui32i = 0; ui32i < OS_MAX_TIMERS; ui32i++)
	{
		psTimerCBData = &sTimers[ui32i];
		if (!psTimerCBData->bInUse)
		{
			psTimerCBData->bInUse = IMG_TRUE;
			break;
		}
	}
#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	mutex_unlock(&sTimerStructLock);
#else
	spin_unlock_irqrestore(&sTimerStructLock, ulLockFlags);
#endif
	if (ui32i >= OS_MAX_TIMERS)
	{
		PVR_DPF((PVR_DBG_ERROR, "OSAddTimer: all timers are in use"));
		return NULL;
	}

	psTimerCBData->pfnTimerFunc = pfnTimerFunc;
	psTimerCBData->pvData = pvData;
	psTimerCBData->bActive = IMG_FALSE;

	/*
		HZ = ticks per second
		ui32MsTimeout = required ms delay
		ticks = (Hz * ui32MsTimeout) / 1000
	*/
	psTimerCBData->ui32Delay = ((HZ * ui32MsTimeout) < 1000)
								?	1
								:	((HZ * ui32MsTimeout) / 1000);
	/* initialise object */
	init_timer(&psTimerCBData->sTimer);

	/* setup timer object */
	psTimerCBData->sTimer.function = (void *)OSTimerCallbackWrapper;
	psTimerCBData->sTimer.data = (uintptr_t)psTimerCBData;

	return (IMG_HANDLE)(uintptr_t)(ui32i + 1);
}


static inline TIMER_CALLBACK_DATA *GetTimerStructure(IMG_HANDLE hTimer)
{
	IMG_UINT32 ui32i = (IMG_UINT32)((uintptr_t)hTimer) - 1;

	PVR_ASSERT(ui32i < OS_MAX_TIMERS);

	return &sTimers[ui32i];
}

PVRSRV_ERROR OSRemoveTimer (IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = GetTimerStructure(hTimer);

	PVR_ASSERT(psTimerCBData->bInUse);
	PVR_ASSERT(!psTimerCBData->bActive);

	/* free timer callback data struct */
	psTimerCBData->bInUse = IMG_FALSE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSEnableTimer (IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = GetTimerStructure(hTimer);

	PVR_ASSERT(psTimerCBData->bInUse);
	PVR_ASSERT(!psTimerCBData->bActive);

	/* Start timer arming */
	psTimerCBData->bActive = IMG_TRUE;

	/* set the expire time */
	psTimerCBData->sTimer.expires = psTimerCBData->ui32Delay + jiffies;

	/* Add the timer to the list */
	add_timer(&psTimerCBData->sTimer);

	return PVRSRV_OK;
}


PVRSRV_ERROR OSDisableTimer (IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = GetTimerStructure(hTimer);

	PVR_ASSERT(psTimerCBData->bInUse);
	PVR_ASSERT(psTimerCBData->bActive);

	/* Stop timer from arming */
	psTimerCBData->bActive = IMG_FALSE;
	smp_mb();

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
	flush_workqueue(psTimerWorkQueue);
#endif
#if defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	flush_scheduled_work();
#endif

	/* remove timer */
	del_timer_sync(&psTimerCBData->sTimer);

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
	/*
	 * This second flush is to catch the case where the timer ran
	 * before we managed to delete it, in which case, it will have
	 * queued more work for the workqueue.	Since the bActive flag
	 * has been cleared, this second flush won't result in the
	 * timer being rearmed.
	 */
	flush_workqueue(psTimerWorkQueue);
#endif
#if defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	flush_scheduled_work();
#endif

	return PVRSRV_OK;
}


PVRSRV_ERROR OSEventObjectCreate(const IMG_CHAR *pszName, IMG_HANDLE *hEventObject)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_UNREFERENCED_PARAMETER(pszName);

	if(hEventObject)
	{
		if(LinuxEventObjectListCreate(hEventObject) != PVRSRV_OK)
		{
			 eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}

	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectCreate: hEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_UNABLE_TO_CREATE_EVENT;
	}

	return eError;
}


PVRSRV_ERROR OSEventObjectDestroy(IMG_HANDLE hEventObject)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if(hEventObject)
	{
		LinuxEventObjectListDestroy(hEventObject);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectDestroy: hEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

/*
 * EventObjectWaitTimeout()
 */
static PVRSRV_ERROR EventObjectWaitTimeout(IMG_HANDLE hOSEventKM,
										   IMG_UINT64 uiTimeoutus,
										   IMG_BOOL bHoldBridgeLock)
{
	PVRSRV_ERROR eError;

	if(hOSEventKM && uiTimeoutus > 0)
	{
		eError = LinuxEventObjectWait(hOSEventKM, uiTimeoutus, bHoldBridgeLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectWait: invalid arguments %p, %lld", hOSEventKM, uiTimeoutus));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectWaitTimeout(IMG_HANDLE hOSEventKM, IMG_UINT64 uiTimeoutus)
{
	return EventObjectWaitTimeout(hOSEventKM, uiTimeoutus, IMG_FALSE);
}

PVRSRV_ERROR OSEventObjectWait(IMG_HANDLE hOSEventKM)
{
	return OSEventObjectWaitTimeout(hOSEventKM, EVENT_OBJECT_TIMEOUT_US);
}

PVRSRV_ERROR OSEventObjectWaitTimeoutAndHoldBridgeLock(IMG_HANDLE hOSEventKM, IMG_UINT64 uiTimeoutus)
{
	return EventObjectWaitTimeout(hOSEventKM, uiTimeoutus, IMG_TRUE);
}

PVRSRV_ERROR OSEventObjectWaitAndHoldBridgeLock(IMG_HANDLE hOSEventKM)
{
	return OSEventObjectWaitTimeoutAndHoldBridgeLock(hOSEventKM, EVENT_OBJECT_TIMEOUT_US);
}

PVRSRV_ERROR OSEventObjectOpen(IMG_HANDLE hEventObject,
											IMG_HANDLE *phOSEvent)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if(hEventObject)
	{
		if(LinuxEventObjectAdd(hEventObject, phOSEvent) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectAdd: failed"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectOpen: hEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectClose(IMG_HANDLE hOSEventKM)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if(hOSEventKM)
	{
		if(LinuxEventObjectDelete(hOSEventKM) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectDelete: failed"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}

	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectDestroy: hEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectSignal(IMG_HANDLE hEventObject)
{
	PVRSRV_ERROR eError;

	if(hEventObject)
	{
		eError = LinuxEventObjectSignal(hEventObject);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectSignal: hOSEventKM is not a valid handle"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

IMG_BOOL OSProcHasPrivSrvInit(void)
{
	return capable(CAP_SYS_ADMIN) != 0;
}

PVRSRV_ERROR OSCopyToUser(void *pvProcess,
						  void *pvDest,
						  const void *pvSrc,
						  size_t ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if(pvr_copy_to_user(pvDest, pvSrc, ui32Bytes)==0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_FAILED_TO_COPY_VIRT_MEMORY;
}

PVRSRV_ERROR OSCopyFromUser(void *pvProcess,
							void *pvDest,
							const void *pvSrc,
							size_t ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if(pvr_copy_from_user(pvDest, pvSrc, ui32Bytes)==0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_FAILED_TO_COPY_VIRT_MEMORY;
}

IMG_BOOL OSAccessOK(IMG_VERIFY_TEST eVerification, void *pvUserPtr, size_t ui32Bytes)
{
	IMG_INT linuxType;

	if (eVerification == PVR_VERIFY_READ)
	{
		linuxType = VERIFY_READ;
	}
	else
	{
		PVR_ASSERT(eVerification == PVR_VERIFY_WRITE);
		linuxType = VERIFY_WRITE;
	}

	return access_ok(linuxType, pvUserPtr, ui32Bytes);
}

IMG_UINT64 OSDivide64r64(IMG_UINT64 ui64Divident, IMG_UINT32 ui32Divisor, IMG_UINT32 *pui32Remainder)
{
	*pui32Remainder = do_div(ui64Divident, ui32Divisor);

	return ui64Divident;
}

IMG_UINT32 OSDivide64(IMG_UINT64 ui64Divident, IMG_UINT32 ui32Divisor, IMG_UINT32 *pui32Remainder)
{
	*pui32Remainder = do_div(ui64Divident, ui32Divisor);

	return (IMG_UINT32) ui64Divident;
}

/* One time osfunc initialisation */
PVRSRV_ERROR PVROSFuncInit(void)
{
#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
	{
		PVR_ASSERT(!psTimerWorkQueue);

		psTimerWorkQueue = create_workqueue("pvr_timer");
		if (psTimerWorkQueue == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: couldn't create timer workqueue", __FUNCTION__));
			return PVRSRV_ERROR_UNABLE_TO_CREATE_THREAD;
		}
	}
#endif

#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES) || defined(PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE)
	{
		IMG_UINT32 ui32i;

		for (ui32i = 0; ui32i < OS_MAX_TIMERS; ui32i++)
		{
			TIMER_CALLBACK_DATA *psTimerCBData = &sTimers[ui32i];

			INIT_WORK(&psTimerCBData->sWork, OSTimerWorkQueueCallBack);
		}
	}
#endif
	return PVRSRV_OK;
}

/*
 * Osfunc deinitialisation.
 * Note that PVROSFuncInit may not have been called
 */
void PVROSFuncDeInit(void)
{
#if defined(PVR_LINUX_TIMERS_USING_WORKQUEUES)
	if (psTimerWorkQueue != NULL)
	{
		destroy_workqueue(psTimerWorkQueue);
		psTimerWorkQueue = NULL;
	}
#endif
}

void OSDumpStack(void)
{
	dump_stack();
}

static struct task_struct *gsOwner;

void OSAcquireBridgeLock(void)
{
	mutex_lock(&gPVRSRVLock);
	gsOwner = current;
}

void OSReleaseBridgeLock(void)
{
	gsOwner = NULL;
	mutex_unlock(&gPVRSRVLock);
}

struct task_struct *BridgeLockGetOwner(void)
{
	return gsOwner;
}

IMG_BOOL BridgeLockIsLocked(void)
{
	return OSLockIsLocked(&gPVRSRVLock);
}

/*************************************************************************/ /*!
@Function		OSCreateStatisticEntry
@Description	Create a statistic entry in the specified folder.
@Input			pszName		   String containing the name for the entry.
@Input			pvFolder	   Reference from OSCreateStatisticFolder() of the
							   folder to create the entry in, or NULL for the
							   root.
@Input			pfnStatsPrint  Pointer to function that can be used to print the
							   values of all the statistics.
@Input			pfnIncMemRefCt Pointer to function that can be used to take a
							   reference on the memory backing the statistic
							   entry.
@Input			pfnDecMemRefCt Pointer to function that can be used to drop a
							   reference on the memory backing the statistic
							   entry.
@Input			pvData		   OS specific reference that can be used by
							   pfnGetElement.
@Return			Pointer void reference to the entry created, which can be
				passed to OSRemoveStatisticEntry() to remove the entry.
*/ /**************************************************************************/
void *OSCreateStatisticEntry(IMG_CHAR* pszName, void *pvFolder,
							 OS_STATS_PRINT_FUNC* pfnStatsPrint,
							 OS_INC_STATS_MEM_REFCOUNT_FUNC* pfnIncMemRefCt,
							 OS_DEC_STATS_MEM_REFCOUNT_FUNC* pfnDecMemRefCt,
							 void *pvData)
{
	return (void *)PVRDebugFSCreateStatisticEntry(pszName, (PVR_DEBUGFS_DIR_DATA *)pvFolder, pfnStatsPrint, pfnIncMemRefCt, pfnDecMemRefCt, pvData);
} /* OSCreateStatisticEntry */


/*************************************************************************/ /*!
@Function		OSRemoveStatisticEntry
@Description	Removes a statistic entry.
@Input			pvEntry  Pointer void reference to the entry created by
						 OSCreateStatisticEntry().
*/ /**************************************************************************/
void OSRemoveStatisticEntry(void *pvEntry)
{
	PVRDebugFSRemoveStatisticEntry((PVR_DEBUGFS_DRIVER_STAT *)pvEntry);
} /* OSRemoveStatisticEntry */

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
void *OSCreateRawStatisticEntry(const IMG_CHAR *pszFileName, void *pvParentDir,
                                OS_STATS_PRINT_FUNC *pfStatsPrint)
{
	return (void *) PVRDebugFSCreateRawStatisticEntry(pszFileName, pvParentDir,
	                                                  pfStatsPrint);
}

void OSRemoveRawStatisticEntry(void *pvEntry)
{
	PVRDebugFSRemoveRawStatisticEntry(pvEntry);
}
#endif

/*************************************************************************/ /*!
@Function		OSCreateStatisticFolder
@Description	Create a statistic folder to hold statistic entries.
@Input			pszName   String containing the name for the folder.
@Input			pvFolder  Reference from OSCreateStatisticFolder() of the folder
						  to create the folder in, or NULL for the root.
@Return			Pointer void reference to the folder created, which can be
				passed to OSRemoveStatisticFolder() to remove the folder.
*/ /**************************************************************************/
void *OSCreateStatisticFolder(IMG_CHAR *pszName, void *pvFolder)
{
	PVR_DEBUGFS_DIR_DATA *psNewStatFolder = NULL;
	int iResult;

	iResult = PVRDebugFSCreateEntryDir(pszName, (PVR_DEBUGFS_DIR_DATA *)pvFolder, &psNewStatFolder);
	return (iResult == 0) ? (void *)psNewStatFolder : NULL;
} /* OSCreateStatisticFolder */


/*************************************************************************/ /*!
@Function		OSRemoveStatisticFolder
@Description	Removes a statistic folder.
@Input          ppvFolder  Reference from OSCreateStatisticFolder() of the
                           folder that should be removed.
                           This needs to be double pointer because it has to
                           be NULLed right after memory is freed to avoid
                           possible races and use-after-free situations.
*/ /**************************************************************************/
void OSRemoveStatisticFolder(void **ppvFolder)
{
	PVRDebugFSRemoveEntryDir((PVR_DEBUGFS_DIR_DATA **)ppvFolder);
} /* OSRemoveStatisticFolder */


PVRSRV_ERROR OSChangeSparseMemCPUAddrMap(void **psPageArray,
                                         IMG_UINT64 sCpuVAddrBase,
                                         IMG_CPU_PHYADDR sCpuPAHeapBase,
                                         IMG_UINT32 ui32AllocPageCount,
                                         IMG_UINT32 *pai32AllocIndices,
                                         IMG_UINT32 ui32FreePageCount,
                                         IMG_UINT32 *pai32FreeIndices,
                                         IMG_BOOL bIsLMA)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
	pfn_t sPFN;
#else
	IMG_UINT64 uiPFN;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */

	PVRSRV_ERROR eError;

	struct mm_struct  *psMM = current->mm;
	struct vm_area_struct *psVMA = NULL;
	struct address_space *psMapping = NULL;
	struct page *psPage = NULL;

	IMG_UINT64 uiCPUVirtAddr = 0;
	IMG_UINT32 ui32Loop = 0;
	IMG_UINT32 ui32PageSize = OSGetPageSize();
	IMG_BOOL bMixedMap = IMG_FALSE;

	/*
	 * Acquire the lock before manipulating the VMA
	 * In this case only mmap_sem lock would suffice as the pages associated with this VMA
	 * are never meant to be swapped out.
	 *
	 * In the future, in case the pages are marked as swapped, page_table_lock needs
	 * to be acquired in conjunction with this to disable page swapping.
	 */

	/* Find the Virtual Memory Area associated with the user base address */
	psVMA = find_vma(psMM, (uintptr_t)sCpuVAddrBase);
	if (NULL == psVMA)
	{
		eError = PVRSRV_ERROR_PMR_NO_CPU_MAP_FOUND;
		return eError;
	}

	/* Acquire the memory sem */
	down_write(&psMM->mmap_sem);

	psMapping = psVMA->vm_file->f_mapping;
	
	/* Set the page offset to the correct value as this is disturbed in MMAP_PMR func */
	psVMA->vm_pgoff = (psVMA->vm_start >>  PAGE_SHIFT);

	/* Delete the entries for the pages that got freed */
	if (ui32FreePageCount && (pai32FreeIndices != NULL))
	{
		for (ui32Loop = 0; ui32Loop < ui32FreePageCount; ui32Loop++)
		{
			uiCPUVirtAddr = (uintptr_t)(sCpuVAddrBase + (pai32FreeIndices[ui32Loop] * ui32PageSize));

			unmap_mapping_range(psMapping, uiCPUVirtAddr, ui32PageSize, 1);

#ifndef PVRSRV_UNMAP_ON_SPARSE_CHANGE
			/*
			 * Still need to map pages in case remap flag is set.
			 * That is not done until the remap case succeeds
			 */
#endif
		}
		eError = PVRSRV_OK;
	}

	if ((psVMA->vm_flags & VM_MIXEDMAP) || bIsLMA)
	{
		psVMA->vm_flags |=  VM_MIXEDMAP;
		bMixedMap = IMG_TRUE;
	}
	else
	{
		if (ui32AllocPageCount && (NULL != pai32AllocIndices))
		{
			for (ui32Loop = 0; ui32Loop < ui32AllocPageCount; ui32Loop++)
			{

				psPage = (struct page *)psPageArray[pai32AllocIndices[ui32Loop]];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
				sPFN = page_to_pfn_t(psPage);

				if (!pfn_t_valid(sPFN) || page_count(pfn_t_to_page(sPFN)) == 0)
#else
				uiPFN = page_to_pfn(psPage);

				if (!pfn_valid(uiPFN) || (page_count(pfn_to_page(uiPFN)) == 0))
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
				{
					bMixedMap = IMG_TRUE;
					psVMA->vm_flags |= VM_MIXEDMAP;
					break;
				}
			}
		}
	}

	/* Map the pages that got allocated */
	if (ui32AllocPageCount && (NULL != pai32AllocIndices))
	{
		for (ui32Loop = 0; ui32Loop < ui32AllocPageCount; ui32Loop++)
		{
			int err;

			uiCPUVirtAddr = (uintptr_t)(sCpuVAddrBase + (pai32AllocIndices[ui32Loop] * ui32PageSize));
			unmap_mapping_range(psMapping, uiCPUVirtAddr, ui32PageSize, 1);

			if (bIsLMA)
			{
				phys_addr_t uiAddr = sCpuPAHeapBase.uiAddr +
				                     ((IMG_DEV_PHYADDR *)psPageArray)[pai32AllocIndices[ui32Loop]].uiAddr;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
				sPFN = phys_to_pfn_t(uiAddr, 0);
				psPage = pfn_t_to_page(sPFN);
#else
				uiPFN = uiAddr >> PAGE_SHIFT;
				psPage = pfn_to_page(uiPFN);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
			}
			else
			{
				psPage = (struct page *)psPageArray[pai32AllocIndices[ui32Loop]];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
				sPFN = page_to_pfn_t(psPage);
#else
				uiPFN = page_to_pfn(psPage);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
			}

			if (bMixedMap)
			{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
				err = vm_insert_mixed(psVMA, uiCPUVirtAddr, sPFN);
#else
				err = vm_insert_mixed(psVMA, uiCPUVirtAddr, uiPFN);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
			}
			else
			{
				err = vm_insert_page(psVMA, uiCPUVirtAddr, psPage);
			}

			if (err)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "Remap failure error code: %d", err));
				eError = PVRSRV_ERROR_PMR_CPU_PAGE_MAP_FAILED;
				goto eFailed;
			}
		}
	}

	eError = PVRSRV_OK;
	eFailed:
	up_write(&psMM->mmap_sem);

	return eError;
}

/*************************************************************************/ /*!
@Function       OSDebugSignalPID
@Description    Sends a SIGTRAP signal to a specific PID in user mode for
                debugging purposes. The user mode process can register a handler
                against this signal.
                This is necessary to support the Rogue debugger. If the Rogue
                debugger is not used then this function may be implemented as
                a stub.
@Input          ui32PID    The PID for the signal.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR OSDebugSignalPID(IMG_UINT32 ui32PID)
{
	int err;
	struct pid *psPID;

	psPID = find_vpid(ui32PID);
	if (psPID == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get PID struct.", __func__));
		return PVRSRV_ERROR_NOT_FOUND;
	}

	err = kill_pid(psPID, SIGTRAP, 0);
	if (err != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Signal Failure %d", __func__, err));
		return PVRSRV_ERROR_SIGNAL_FAILED;
	}

	return PVRSRV_OK;
}
