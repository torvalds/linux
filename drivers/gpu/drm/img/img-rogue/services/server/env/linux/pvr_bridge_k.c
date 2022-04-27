/*************************************************************************/ /*!
@File
@Title          PVR Bridge Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Receives calls from the user portion of services and
                despatches them to functions in the kernel portion.
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

#include <linux/mm_types.h>

#include "img_defs.h"
#include "pvr_bridge.h"
#include "pvr_bridge_k.h"
#include "connection_server.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "di_server.h"
#include "private_data.h"
#include "linkage.h"
#include "pmr.h"
#include "rgx_bvnc_defs_km.h"
#include "pvrsrv_bridge_init.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_print.h>
#else
#include <drm/drmP.h>
#endif

#include "pvr_drm.h"
#include "pvr_drv.h"

#include "env_connection.h"
#include <linux/sched.h>
#include <linux/freezer.h>

/* RGX: */
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif

#include "srvcore.h"
#include "common_srvcore_bridge.h"

PVRSRV_ERROR InitDMABUFBridge(void);
PVRSRV_ERROR DeinitDMABUFBridge(void);

#if defined(MODULE_TEST)
/************************************************************************/
// additional includes for services testing
/************************************************************************/
#include "pvr_test_bridge.h"
#include "kern_test.h"
/************************************************************************/
// end of additional includes
/************************************************************************/
#endif

/* The mmap code has its own mutex, to prevent possible re-entrant issues
 * when the same PMR is mapped from two different connections/processes.
 */
static DEFINE_MUTEX(g_sMMapMutex);

#define _DRIVER_SUSPENDED 1
#define _DRIVER_NOT_SUSPENDED 0
static ATOMIC_T g_iDriverSuspended;
static ATOMIC_T g_iNumActiveDriverThreads;
static ATOMIC_T g_iNumActiveKernelThreads;
static IMG_HANDLE g_hDriverThreadEventObject;

#if defined(DEBUG_BRIDGE_KM)
static DI_ENTRY *gpsDIBridgeStatsEntry;

static void *BridgeStatsDIStart(OSDI_IMPL_ENTRY *psEntry, IMG_UINT64 *pui64Pos)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = DIGetPrivData(psEntry);

	BridgeGlobalStatsLock();

	if (psDispatchTable == NULL || *pui64Pos > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		return NULL;
	}

	if (*pui64Pos == 0)
	{
		return DI_START_TOKEN;
	}

	return &(psDispatchTable[*pui64Pos - 1]);
}

static void BridgeStatsDIStop(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psEntry);
	PVR_UNREFERENCED_PARAMETER(pvData);

	BridgeGlobalStatsUnlock();
}

static void *BridgeStatsDINext(OSDI_IMPL_ENTRY *psEntry, void *pvData,
                               IMG_UINT64 *pui64Pos)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = DIGetPrivData(psEntry);
	IMG_UINT64 uiItemAskedFor = *pui64Pos; /* pui64Pos on entry is the index to return */

	PVR_UNREFERENCED_PARAMETER(pvData);

	/* Is the item asked for (starts at 0) a valid table index? */
	if (uiItemAskedFor < BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		(*pui64Pos)++; /* on exit it is the next DI index to ask for */
		return &(psDispatchTable[uiItemAskedFor]);
	}

	/* Now passed the end of the table to indicate stop */
	return NULL;
}

static int BridgeStatsDIShow(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	if (pvData == DI_START_TOKEN)
	{
		DIPrintf(psEntry,
		         "Total ioctl call count = %u\n"
		         "Total number of bytes copied via copy_from_user = %u\n"
		         "Total number of bytes copied via copy_to_user = %u\n"
		         "Total number of bytes copied via copy_*_user = %u\n\n"
		         "%3s: %-60s | %-48s | %10s | %20s | %20s | %20s | %20s\n",
		         g_BridgeGlobalStats.ui32IOCTLCount,
		         g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
		         g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
		         g_BridgeGlobalStats.ui32TotalCopyFromUserBytes +
		             g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
		         "#",
		         "Bridge Name",
		         "Wrapper Function",
		         "Call Count",
		         "copy_from_user (B)",
		         "copy_to_user (B)",
		         "Total Time (us)",
		         "Max Time (us)");
	}
	else if (pvData != NULL)
	{
		PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psTableEntry = pvData;
		IMG_UINT32 ui32Remainder;

		DIPrintf(psEntry,
		         "%3d: %-60s   %-48s   %-10u   %-20u   %-20u   %-20" IMG_UINT64_FMTSPEC "   %-20" IMG_UINT64_FMTSPEC "\n",
		         (IMG_UINT32)(((size_t)psTableEntry-(size_t)g_BridgeDispatchTable)/sizeof(*g_BridgeDispatchTable)),
		         psTableEntry->pszIOCName,
		         (psTableEntry->pfFunction != NULL) ? psTableEntry->pszFunctionName : "(null)",
		         psTableEntry->ui32CallCount,
		         psTableEntry->ui32CopyFromUserTotalBytes,
		         psTableEntry->ui32CopyToUserTotalBytes,
		         OSDivide64r64(psTableEntry->ui64TotalTimeNS, 1000, &ui32Remainder),
		         OSDivide64r64(psTableEntry->ui64MaxTimeNS, 1000, &ui32Remainder));
	}

	return 0;
}

static IMG_INT64 BridgeStatsWrite(const IMG_CHAR *pcBuffer,
                                  IMG_UINT64 ui64Count, IMG_UINT64 *pui64Pos,
                                  void *pvData)
{
	IMG_UINT32 i;

	PVR_RETURN_IF_FALSE(pcBuffer != NULL, -EIO);
	PVR_RETURN_IF_FALSE(pui64Pos != NULL && *pui64Pos == 0, -EIO);
	PVR_RETURN_IF_FALSE(ui64Count >= 1, -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[0] == '0', -EINVAL);
	PVR_RETURN_IF_FALSE(pcBuffer[ui64Count - 1] == '\0', -EINVAL);

	/* Reset stats. */

	BridgeGlobalStatsLock();

	g_BridgeGlobalStats.ui32IOCTLCount = 0;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes = 0;
	g_BridgeGlobalStats.ui32TotalCopyToUserBytes = 0;

	for (i = 0; i < ARRAY_SIZE(g_BridgeDispatchTable); i++)
	{
		g_BridgeDispatchTable[i].ui32CallCount = 0;
		g_BridgeDispatchTable[i].ui32CopyFromUserTotalBytes = 0;
		g_BridgeDispatchTable[i].ui32CopyToUserTotalBytes = 0;
		g_BridgeDispatchTable[i].ui64TotalTimeNS = 0;
		g_BridgeDispatchTable[i].ui64MaxTimeNS = 0;
	}

	BridgeGlobalStatsUnlock();

	return ui64Count;
}

#endif /* defined(DEBUG_BRIDGE_KM) */

PVRSRV_ERROR OSPlatformBridgeInit(void)
{
	PVRSRV_ERROR eError;

	eError = InitDMABUFBridge();
	PVR_LOG_IF_ERROR(eError, "InitDMABUFBridge");

	OSAtomicWrite(&g_iDriverSuspended, _DRIVER_NOT_SUSPENDED);
	OSAtomicWrite(&g_iNumActiveDriverThreads, 0);
	OSAtomicWrite(&g_iNumActiveKernelThreads, 0);

	eError = OSEventObjectCreate("Global driver thread event object",
	                             &g_hDriverThreadEventObject);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", error_);

#if defined(DEBUG_BRIDGE_KM)
	{
		DI_ITERATOR_CB sIter = {
			.pfnStart = BridgeStatsDIStart,
			.pfnStop = BridgeStatsDIStop,
			.pfnNext = BridgeStatsDINext,
			.pfnShow = BridgeStatsDIShow,
			.pfnWrite = BridgeStatsWrite
		};

		eError = DICreateEntry("bridge_stats", NULL, &sIter,
		                       &g_BridgeDispatchTable[0],
		                       DI_ENTRY_TYPE_GENERIC,
		                       &gpsDIBridgeStatsEntry);
		PVR_LOG_GOTO_IF_ERROR(eError, "DICreateEntry", error_);
	}
#endif

	return PVRSRV_OK;

error_:
	if (g_hDriverThreadEventObject) {
		OSEventObjectDestroy(g_hDriverThreadEventObject);
		g_hDriverThreadEventObject = NULL;
	}

	return eError;
}

PVRSRV_ERROR OSPlatformBridgeDeInit(void)
{
	PVRSRV_ERROR eError;

#if defined(DEBUG_BRIDGE_KM)
	if (gpsDIBridgeStatsEntry != NULL)
	{
		DIDestroyEntry(gpsDIBridgeStatsEntry);
	}
#endif

	eError = DeinitDMABUFBridge();
	PVR_LOG_RETURN_IF_ERROR(eError, "DeinitDMABUFBridge");

	if (g_hDriverThreadEventObject != NULL) {
		OSEventObjectDestroy(g_hDriverThreadEventObject);
		g_hDriverThreadEventObject = NULL;
	}

	return eError;
}

PVRSRV_ERROR LinuxBridgeBlockClientsAccess(IMG_BOOL bShutdown)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hEvent;

	eError = OSEventObjectOpen(g_hDriverThreadEventObject, &hEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to open event object", __func__));
		return eError;
	}

	if (OSAtomicCompareExchange(&g_iDriverSuspended, _DRIVER_NOT_SUSPENDED,
	                            _DRIVER_SUSPENDED) == _DRIVER_SUSPENDED)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Driver is already suspended", __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto out_put;
	}

	/* now wait for any threads currently in the server to exit */
	while (OSAtomicRead(&g_iNumActiveDriverThreads) != 0 ||
		   (OSAtomicRead(&g_iNumActiveKernelThreads) != 0 && !bShutdown))
	{
		if (OSAtomicRead(&g_iNumActiveDriverThreads) != 0)
		{
			PVR_LOG(("%s: waiting for user threads (%d)", __func__,
			        OSAtomicRead(&g_iNumActiveDriverThreads)));
		}
		if (OSAtomicRead(&g_iNumActiveKernelThreads) != 0)
		{
			PVR_LOG(("%s: waiting for kernel threads (%d)", __func__,
			        OSAtomicRead(&g_iNumActiveKernelThreads)));
		}
		/* Regular wait is called here (and not OSEventObjectWaitKernel) because
		 * this code is executed by the caller of .suspend/.shutdown callbacks
		 * which is most likely PM (or other actor responsible for suspend
		 * process). Because of that this thread shouldn't and most likely
		 * event cannot be frozen. */
		OSEventObjectWait(hEvent);
	}

out_put:
	OSEventObjectClose(hEvent);

	return eError;
}

PVRSRV_ERROR LinuxBridgeUnblockClientsAccess(void)
{
	PVRSRV_ERROR eError;

	/* resume the driver and then signal so any waiting threads wake up */
	if (OSAtomicCompareExchange(&g_iDriverSuspended, _DRIVER_SUSPENDED,
	                            _DRIVER_NOT_SUSPENDED) == _DRIVER_NOT_SUSPENDED)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Driver is not suspended", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = OSEventObjectSignal(g_hDriverThreadEventObject);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSEventObjectSignal failed: %s",
		        __func__, PVRSRVGetErrorString(eError)));
	}

	return eError;
}

static PVRSRV_ERROR LinuxBridgeSignalIfSuspended(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (OSAtomicRead(&g_iDriverSuspended) == _DRIVER_SUSPENDED)
	{
		PVRSRV_ERROR eError = OSEventObjectSignal(g_hDriverThreadEventObject);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to signal driver thread event"
			        " object: %s", __func__, PVRSRVGetErrorString(eError)));
		}
	}

	return eError;
}

void LinuxBridgeNumActiveKernelThreadsIncrement(void)
{
	OSAtomicIncrement(&g_iNumActiveKernelThreads);
}

void LinuxBridgeNumActiveKernelThreadsDecrement(void)
{
	OSAtomicDecrement(&g_iNumActiveKernelThreads);
	PVR_ASSERT(OSAtomicRead(&g_iNumActiveKernelThreads) >= 0);

	/* Signal on every decrement in case LinuxBridgeBlockClientsAccess() is
	 * waiting for the threads to freeze.
	 * (error is logged in called function so ignore, we can't do much with
	 * it anyway) */
	(void) LinuxBridgeSignalIfSuspended();
}

static PVRSRV_ERROR _WaitForDriverUnsuspend(void)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hEvent;

	eError = OSEventObjectOpen(g_hDriverThreadEventObject, &hEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to open event object", __func__));
		return eError;
	}

	while (OSAtomicRead(&g_iDriverSuspended) == _DRIVER_SUSPENDED)
	{
		/* we should be able to use normal (not kernel) wait here since
		 * we were just unfrozen and most likely we're not going to
		 * be frozen again (?) */
		OSEventObjectWait(hEvent);
	}

	OSEventObjectClose(hEvent);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVDriverThreadEnter(void)
{
	PVRSRV_ERROR eError;

	/* increment first so there is no race between this value and
	 * g_iDriverSuspended in LinuxBridgeBlockClientsAccess() */
	OSAtomicIncrement(&g_iNumActiveDriverThreads);

	if (OSAtomicRead(&g_iDriverSuspended) == _DRIVER_SUSPENDED)
	{
		/* decrement here because the driver is going to be suspended and
		 * this thread is going to be frozen so we don't want to wait for
		 * it in LinuxBridgeBlockClientsAccess() */
		OSAtomicDecrement(&g_iNumActiveDriverThreads);

		/* during suspend procedure this will put the current thread to
		 * the freezer but during shutdown this will just return */
		try_to_freeze();

		/* if the thread was unfrozen but the flag is not yet set to
		 * _DRIVER_NOT_SUSPENDED wait for it
		 * in case this is a shutdown the thread was not frozen so we'll
		 * wait here indefinitely but this is ok (and this is in fact what
		 * we want) because no thread should be entering the driver in such
		 * case */
		eError = _WaitForDriverUnsuspend();

		/* increment here because that means that the thread entered the
		 * driver */
		OSAtomicIncrement(&g_iNumActiveDriverThreads);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to wait for driver"
			        " unsuspend: %s", __func__,
			        PVRSRVGetErrorString(eError)));
			return eError;
		}
	}

	return PVRSRV_OK;
}

void PVRSRVDriverThreadExit(void)
{
	OSAtomicDecrement(&g_iNumActiveDriverThreads);
	/* if the driver is being suspended then we need to signal the
	 * event object as the thread suspending the driver is waiting
	 * for active threads to exit
	 * error is logged in called function so ignore returned error
	 */
	(void) LinuxBridgeSignalIfSuspended();
}

int
PVRSRV_BridgeDispatchKM(struct drm_device __maybe_unused *dev, void *arg, struct drm_file *pDRMFile)
{
	struct drm_pvr_srvkm_cmd *psSrvkmCmd = (struct drm_pvr_srvkm_cmd *) arg;
	PVRSRV_BRIDGE_PACKAGE sBridgePackageKM = { 0 };
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pDRMFile->filp);
	PVRSRV_ERROR error;

	if (psConnection == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Connection is closed", __func__));
		return -EFAULT;
	}

	PVR_ASSERT(psSrvkmCmd != NULL);

	DRM_DEBUG("tgid=%d, tgid_connection=%d, bridge_id=%d, func_id=%d",
			  task_tgid_nr(current),
			  ((ENV_CONNECTION_DATA *)PVRSRVConnectionPrivateData(psConnection))->owner,
			  psSrvkmCmd->bridge_id,
			  psSrvkmCmd->bridge_func_id);

	error = PVRSRVDriverThreadEnter();
	PVR_LOG_GOTO_IF_ERROR(error, "PVRSRVDriverThreadEnter", e0);

	sBridgePackageKM.ui32BridgeID = psSrvkmCmd->bridge_id;
	sBridgePackageKM.ui32FunctionID = psSrvkmCmd->bridge_func_id;
	sBridgePackageKM.ui32Size = sizeof(sBridgePackageKM);
	sBridgePackageKM.pvParamIn = (void __user *)(uintptr_t)psSrvkmCmd->in_data_ptr;
	sBridgePackageKM.ui32InBufferSize = psSrvkmCmd->in_data_size;
	sBridgePackageKM.pvParamOut = (void __user *)(uintptr_t)psSrvkmCmd->out_data_ptr;
	sBridgePackageKM.ui32OutBufferSize = psSrvkmCmd->out_data_size;

	error = BridgedDispatchKM(psConnection, &sBridgePackageKM);

	PVRSRVDriverThreadExit();

e0:
	return OSPVRSRVToNativeError(error);
}

int
PVRSRV_MMap(struct file *pFile, struct vm_area_struct *ps_vma)
{
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pFile);
	IMG_HANDLE hSecurePMRHandle = (IMG_HANDLE)((uintptr_t)ps_vma->vm_pgoff);
	PMR *psPMR;
	PVRSRV_ERROR eError;

	if (psConnection == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Invalid connection data"));
		return -ENOENT;
	}

	eError = PVRSRVDriverThreadEnter();
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVDriverThreadEnter", e0);

	/*
	 * The bridge lock used here to protect PVRSRVLookupHandle is replaced
	 * by a specific lock considering that the handle functions have now
	 * their own lock. This change was necessary to solve the lockdep issues
	 * related with the PVRSRV_MMap.
	 */

	eError = PVRSRVLookupHandle(psConnection->psHandleBase,
								(void **)&psPMR,
								hSecurePMRHandle,
								PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
								IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	mutex_lock(&g_sMMapMutex);
	/* Note: PMRMMapPMR will take a reference on the PMR.
	 * Unref the handle immediately, because we have now done
	 * the required operation on the PMR (whether it succeeded or not)
	 */
	eError = PMRMMapPMR(psPMR, ps_vma);
	mutex_unlock(&g_sMMapMutex);
	PVRSRVReleaseHandle(psConnection->psHandleBase, hSecurePMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: PMRMMapPMR failed (%s)",
				__func__, PVRSRVGetErrorString(eError)));
		goto e0;
	}

	PVRSRVDriverThreadExit();

	return 0;

e0:
	PVRSRVDriverThreadExit();

	PVR_DPF((PVR_DBG_ERROR, "Unable to translate error %d", eError));
	PVR_ASSERT(eError != PVRSRV_OK);

	return -ENOENT; // -EAGAIN // or what?
}
