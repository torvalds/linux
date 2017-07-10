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

#include <linux/mm_types.h>

#include "img_defs.h"
#include "pvr_bridge.h"
#include "connection_server.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "pvr_debugfs.h"
#include "private_data.h"
#include "linkage.h"
#include "pmr.h"
#include "rgx_bvnc_defs_km.h"

#include <drm/drmP.h>
#include "pvr_drm.h"
#include "pvr_drv.h"

#include "env_connection.h"
#include <linux/sched.h>

/* RGX: */
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif

#include "srvcore.h"
#include "common_srvcore_bridge.h"

#if defined(SUPPORT_DRM_EXT)
#define CAST_BRIDGE_CMD_PTR_TO_PTR(ptr) (ptr)
#else
#define CAST_BRIDGE_CMD_PTR_TO_PTR(ptr) (void *)(uintptr_t)(ptr)
#endif

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

/* WARNING!
 * The mmap code has its own mutex, to prevent a possible deadlock,
 * when using gPVRSRVLock.
 * The Linux kernel takes the mm->mmap_sem before calling the mmap
 * entry points (PVRMMap, MMapVOpen, MMapVClose), but the ioctl
 * entry point may take mm->mmap_sem during fault handling, or 
 * before calling get_user_pages.  If gPVRSRVLock was used in the
 * mmap entry points, a deadlock could result, due to the ioctl
 * and mmap code taking the two locks in different orders.
 * As a corollary to this, the mmap entry points must not call
 * any driver code that relies on gPVRSRVLock is held.
 */
static DEFINE_MUTEX(g_sMMapMutex);

#if defined(DEBUG_BRIDGE_KM)
static PVR_DEBUGFS_ENTRY_DATA *gpsPVRDebugFSBridgeStatsEntry = NULL;
static struct seq_operations gsBridgeStatsReadOps;
#endif

/* These will go when full bridge gen comes in */
#if defined(PDUMP)
PVRSRV_ERROR InitPDUMPCTRLBridge(void);
PVRSRV_ERROR DeinitPDUMPCTRLBridge(void);
PVRSRV_ERROR InitPDUMPBridge(void);
PVRSRV_ERROR DeinitPDUMPBridge(void);
PVRSRV_ERROR InitRGXPDUMPBridge(void);
PVRSRV_ERROR DeinitRGXPDUMPBridge(void);
#endif
#if defined(SUPPORT_DISPLAY_CLASS)
PVRSRV_ERROR InitDCBridge(void);
PVRSRV_ERROR DeinitDCBridge(void);
#endif
PVRSRV_ERROR InitMMBridge(void);
PVRSRV_ERROR DeinitMMBridge(void);
#if !defined(EXCLUDE_CMM_BRIDGE)
PVRSRV_ERROR InitCMMBridge(void);
PVRSRV_ERROR DeinitCMMBridge(void);
#endif
PVRSRV_ERROR InitPDUMPMMBridge(void);
PVRSRV_ERROR DeinitPDUMPMMBridge(void);
PVRSRV_ERROR InitSRVCOREBridge(void);
PVRSRV_ERROR DeinitSRVCOREBridge(void);
PVRSRV_ERROR InitSYNCBridge(void);
PVRSRV_ERROR DeinitSYNCBridge(void);

#if defined(SUPPORT_SERVER_SYNC)
#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR InitSYNCEXPORTBridge(void);
PVRSRV_ERROR DeinitSYNCEXPORTBridge(void);
#endif
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR InitSYNCSEXPORTBridge(void);
PVRSRV_ERROR DeinitSYNCSEXPORTBridge(void);
#endif
#endif /* defined(SUPPORT_SERVER_SYNC) */

#if defined (SUPPORT_RGX)
#if !defined(SUPPORT_KERNEL_SRVINIT)
PVRSRV_ERROR InitRGXINITBridge(void);
PVRSRV_ERROR DeinitRGXINITBridge(void);
#endif
PVRSRV_ERROR InitRGXTA3DBridge(void);
PVRSRV_ERROR DeinitRGXTA3DBridge(void);
PVRSRV_ERROR InitRGXTQBridge(void);
PVRSRV_ERROR DeinitRGXTQBridge(void);
PVRSRV_ERROR InitRGXTQ2Bridge(void);
PVRSRV_ERROR DeinitRGXTQ2Bridge(void);
PVRSRV_ERROR InitRGXCMPBridge(void);
PVRSRV_ERROR DeinitRGXCMPBridge(void);
#if !defined(EXCLUDE_BREAKPOINT_BRIDGE)
PVRSRV_ERROR InitBREAKPOINTBridge(void);
PVRSRV_ERROR DeinitBREAKPOINTBridge(void);
#endif
PVRSRV_ERROR InitDEBUGMISCBridge(void);
PVRSRV_ERROR DeinitDEBUGMISCBridge(void);
PVRSRV_ERROR InitRGXHWPERFBridge(void);
PVRSRV_ERROR DeinitRGXHWPERFBridge(void);
PVRSRV_ERROR InitRGXRAYBridge(void);
PVRSRV_ERROR DeinitRGXRAYBridge(void);
#if !defined(EXCLUDE_REGCONFIG_BRIDGE)
PVRSRV_ERROR InitREGCONFIGBridge(void);
PVRSRV_ERROR DeinitREGCONFIGBridge(void);
#endif
PVRSRV_ERROR InitTIMERQUERYBridge(void);
PVRSRV_ERROR DeinitTIMERQUERYBridge(void);
PVRSRV_ERROR InitRGXKICKSYNCBridge(void);
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void);
PVRSRV_ERROR InitRGXSIGNALSBridge(void);
PVRSRV_ERROR DeinitRGXSIGNALSBridge(void);
#endif /* SUPPORT_RGX */
PVRSRV_ERROR InitCACHEBridge(void);
PVRSRV_ERROR DeinitCACHEBridge(void);
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR InitSMMBridge(void);
PVRSRV_ERROR DeinitSMMBridge(void);
#endif
#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
PVRSRV_ERROR InitHTBUFFERBridge(void);
PVRSRV_ERROR DeinitHTBUFFERBridge(void);
#endif
PVRSRV_ERROR InitPVRTLBridge(void);
PVRSRV_ERROR DeinitPVRTLBridge(void);
#if defined(PVR_RI_DEBUG)
PVRSRV_ERROR InitRIBridge(void);
PVRSRV_ERROR DeinitRIBridge(void);
#endif
#if defined(SUPPORT_PAGE_FAULT_DEBUG)
PVRSRV_ERROR InitDEVICEMEMHISTORYBridge(void);
PVRSRV_ERROR DeinitDEVICEMEMHISTORYBridge(void);
#endif
PVRSRV_ERROR InitDMABUFBridge(void);
PVRSRV_ERROR DeinitDMABUFBridge(void);
#if defined(SUPPORT_VALIDATION_BRIDGE)
PVRSRV_ERROR InitVALIDATIONBridge(void);
#endif

#if defined(PVR_TESTING_UTILS)
PVRSRV_ERROR InitTUTILSBridge(void);
PVRSRV_ERROR DeinitTUTILSBridge(void);
#endif
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
PVRSRV_ERROR InitSYNCTRACKINGBridge(void);
PVRSRV_ERROR DeinitSYNCTRACKINGBridge(void);
#endif
#if defined(SUPPORT_WRAP_EXTMEM)
PVRSRV_ERROR InitMMEXTMEMBridge(void);
PVRSRV_ERROR DeinitMMEXTMEMBridge(void);
#endif

PVRSRV_ERROR
DeviceDepBridgeInit(IMG_UINT64 ui64Features)
{
	PVRSRV_ERROR eError;

	if(ui64Features & RGX_FEATURE_COMPUTE_BIT_MASK)
	{
		eError = InitRGXCMPBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}


	if(ui64Features & RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK)
	{
		eError = InitRGXSIGNALSBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if(ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
	{
		eError = InitRGXRAYBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if(ui64Features & RGX_FEATURE_FASTRENDER_DM_BIT_MASK)
	{
		eError = InitRGXTQ2Bridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR
DeviceDepBridgeDeInit(IMG_UINT64 ui64Features)
{
	PVRSRV_ERROR eError;

	if(ui64Features & RGX_FEATURE_COMPUTE_BIT_MASK)
	{
		eError = DeinitRGXCMPBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}


	if(ui64Features & RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK)
	{
		eError = DeinitRGXSIGNALSBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if(ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
	{
		eError = DeinitRGXRAYBridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if(ui64Features & RGX_FEATURE_FASTRENDER_DM_BIT_MASK)
	{
		eError = DeinitRGXTQ2Bridge();
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	return PVRSRV_OK;
}



PVRSRV_ERROR
LinuxBridgeInit(void)
{
	PVRSRV_ERROR eError;
#if defined(DEBUG_BRIDGE_KM)
	IMG_INT iResult;

	iResult = PVRDebugFSCreateEntry("bridge_stats",
					NULL,
					&gsBridgeStatsReadOps,
					NULL,
					NULL,
					NULL,
					&g_BridgeDispatchTable[0],
					&gpsPVRDebugFSBridgeStatsEntry);
	if (iResult != 0)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#endif

	BridgeDispatchTableStartOffsetsInit();

	eError = InitSRVCOREBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = InitSYNCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_SERVER_SYNC)
#if defined(SUPPORT_INSECURE_EXPORT)
	eError = InitSYNCEXPORTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif
#if defined(SUPPORT_SECURE_EXPORT)
	eError = InitSYNCSEXPORTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif
#endif /* defined(SUPPORT_SERVER_SYNC) */

#if defined(PDUMP)
	eError = InitPDUMPCTRLBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#if !defined(EXCLUDE_CMM_BRIDGE)
	eError = InitCMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(PDUMP)
	eError = InitPDUMPMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = InitPDUMPBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitDMABUFBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_DISPLAY_CLASS)
	eError = InitDCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitCACHEBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_SECURE_EXPORT)
	eError = InitSMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
	eError = InitHTBUFFERBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitPVRTLBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	#if defined(PVR_RI_DEBUG)
	eError = InitRIBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	#endif

#if defined(SUPPORT_VALIDATION_BRIDGE)
	eError = InitVALIDATIONBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(PVR_TESTING_UTILS)
	eError = InitTUTILSBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	eError = InitDEVICEMEMHISTORYBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif


#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	eError = InitSYNCTRACKINGBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	#if defined (SUPPORT_RGX)

	eError = InitRGXTQBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if !defined(SUPPORT_KERNEL_SRVINIT)
	eError = InitRGXINITBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitRGXTA3DBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if !defined(EXCLUDE_BREAKPOINT_BRIDGE)
	eError = InitBREAKPOINTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitDEBUGMISCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(PDUMP)
	eError = InitRGXPDUMPBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitRGXHWPERFBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if !defined(EXCLUDE_REGCONFIG_BRIDGE)
	eError = InitREGCONFIGBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = InitTIMERQUERYBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = InitRGXKICKSYNCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#endif /* SUPPORT_RGX */

#if defined(SUPPORT_WRAP_EXTMEM)
	eError = InitMMEXTMEMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	return eError;
}

PVRSRV_ERROR
LinuxBridgeDeInit(void)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_WRAP_EXTMEM)
	eError = DeinitMMEXTMEMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(DEBUG_BRIDGE_KM)
	if (gpsPVRDebugFSBridgeStatsEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsPVRDebugFSBridgeStatsEntry);
	}
#endif

	eError = DeinitSRVCOREBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = DeinitSYNCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_SERVER_SYNC)
#if defined(SUPPORT_INSECURE_EXPORT)
	eError = DeinitSYNCEXPORTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif
#if defined(SUPPORT_SECURE_EXPORT)
	eError = DeinitSYNCSEXPORTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif
#endif /* defined(SUPPORT_SERVER_SYNC) */

#if defined(PDUMP)
	eError = DeinitPDUMPCTRLBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#if !defined(EXCLUDE_CMM_BRIDGE)
	eError = DeinitCMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(PDUMP)
	eError = DeinitPDUMPMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = DeinitPDUMPBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitDMABUFBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(PVR_TESTING_UTILS)
	eError = DeinitTUTILSBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
	eError = DeinitDCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitCACHEBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_SECURE_EXPORT)
	eError = DeinitSMMBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
	eError = DeinitHTBUFFERBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitPVRTLBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	#if defined(PVR_RI_DEBUG)
	eError = DeinitRIBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	#endif

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	eError = DeinitDEVICEMEMHISTORYBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif


#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	eError = DeinitSYNCTRACKINGBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	#if defined (SUPPORT_RGX)

	eError = DeinitRGXTQBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}


#if !defined(SUPPORT_KERNEL_SRVINIT)
	eError = DeinitRGXINITBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitRGXTA3DBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if !defined(EXCLUDE_BREAKPOINT_BRIDGE)
	eError = DeinitBREAKPOINTBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitDEBUGMISCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(PDUMP)
	eError = DeinitRGXPDUMPBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitRGXHWPERFBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if !defined(EXCLUDE_REGCONFIG_BRIDGE)
	eError = DeinitREGCONFIGBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = DeinitTIMERQUERYBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = DeinitRGXKICKSYNCBridge();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#endif /* SUPPORT_RGX */

	return eError;
}

#if defined(DEBUG_BRIDGE_KM)
static void *BridgeStatsSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = (PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)psSeqFile->private;

	OSAcquireBridgeLock();

	if (psDispatchTable == NULL || (*puiPosition) > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		return NULL;
	}

	if ((*puiPosition) == 0) 
	{
		return SEQ_START_TOKEN;
	}

	return &(psDispatchTable[(*puiPosition) - 1]);
}

static void BridgeStatsSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);

	OSReleaseBridgeLock();
}

static void *BridgeStatsSeqNext(struct seq_file *psSeqFile,
			       void *pvData,
			       loff_t *puiPosition)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = (PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)psSeqFile->private;
	loff_t uiItemAskedFor = *puiPosition; /* puiPosition on entry is the index to return */

	PVR_UNREFERENCED_PARAMETER(pvData);

	/* Is the item asked for (starts at 0) a valid table index? */
	if (uiItemAskedFor < BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		(*puiPosition)++; /* on exit it is the next seq index to ask for */
		return &(psDispatchTable[uiItemAskedFor]);
	}

	/* Now passed the end of the table to indicate stop */
	return NULL;
}

static int BridgeStatsSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData == SEQ_START_TOKEN)
	{
		seq_printf(psSeqFile,
			   "Total ioctl call count = %u\n"
			   "Total number of bytes copied via copy_from_user = %u\n"
			   "Total number of bytes copied via copy_to_user = %u\n"
			   "Total number of bytes copied via copy_*_user = %u\n\n"
			   "%3s: %-60s | %-48s | %10s | %20s | %20s | %20s | %20s \n",
			   g_BridgeGlobalStats.ui32IOCTLCount,
			   g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
			   g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
			   g_BridgeGlobalStats.ui32TotalCopyFromUserBytes + g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
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
		PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry = (	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)pvData;
		IMG_UINT32 ui32Remainder;

		seq_printf(psSeqFile,
			   "%3d: %-60s   %-48s   %-10u   %-20u   %-20u   %-20llu   %-20llu\n",
			   (IMG_UINT32)(((size_t)psEntry-(size_t)g_BridgeDispatchTable)/sizeof(*g_BridgeDispatchTable)),
			   psEntry->pszIOCName,
			   (psEntry->pfFunction != NULL) ? psEntry->pszFunctionName : "(null)",
			   psEntry->ui32CallCount,
			   psEntry->ui32CopyFromUserTotalBytes,
			   psEntry->ui32CopyToUserTotalBytes,
			   (unsigned long long) OSDivide64r64(psEntry->ui64TotalTimeNS, 1000, &ui32Remainder),
			   (unsigned long long) OSDivide64r64(psEntry->ui64MaxTimeNS, 1000, &ui32Remainder));
	}

	return 0;
}

static struct seq_operations gsBridgeStatsReadOps =
{
	.start = BridgeStatsSeqStart,
	.stop = BridgeStatsSeqStop,
	.next = BridgeStatsSeqNext,
	.show = BridgeStatsSeqShow,
};
#endif /* defined(DEBUG_BRIDGE_KM) */

int
PVRSRV_BridgeDispatchKM(struct drm_device __maybe_unused *dev, void *arg, struct drm_file *pDRMFile)
{
	struct drm_pvr_srvkm_cmd *psSrvkmCmd = (struct drm_pvr_srvkm_cmd *) arg;
	PVRSRV_BRIDGE_PACKAGE sBridgePackageKM = { 0 };
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pDRMFile->filp);
	PVRSRV_ERROR error;

	if(psConnection == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Connection is closed", __FUNCTION__));
		return -EFAULT;
	}

	if(OSGetDriverSuspended())
	{
		return -EINTR;
	}

	PVR_ASSERT(psSrvkmCmd != NULL);

	DRM_DEBUG("tgid=%d, tgid_connection=%d, bridge_id=%d, func_id=%d",
			  task_tgid_nr(current),
			  ((ENV_CONNECTION_DATA *)PVRSRVConnectionPrivateData(psConnection))->owner,
			  psSrvkmCmd->bridge_id,
			  psSrvkmCmd->bridge_func_id);

	sBridgePackageKM.ui32BridgeID = psSrvkmCmd->bridge_id;
	sBridgePackageKM.ui32FunctionID = psSrvkmCmd->bridge_func_id;
	sBridgePackageKM.ui32Size = sizeof(sBridgePackageKM);
	sBridgePackageKM.pvParamIn = CAST_BRIDGE_CMD_PTR_TO_PTR(psSrvkmCmd->in_data_ptr);
	sBridgePackageKM.ui32InBufferSize = psSrvkmCmd->in_data_size;
	sBridgePackageKM.pvParamOut = CAST_BRIDGE_CMD_PTR_TO_PTR(psSrvkmCmd->out_data_ptr);
	sBridgePackageKM.ui32OutBufferSize = psSrvkmCmd->out_data_size;

	error =  BridgedDispatchKM(psConnection, &sBridgePackageKM);
	return OSPVRSRVToNativeError(error);
}

int
PVRSRV_MMap(struct file *pFile, struct vm_area_struct *ps_vma)
{
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pFile);
	IMG_HANDLE hSecurePMRHandle = (IMG_HANDLE)((uintptr_t)ps_vma->vm_pgoff);
	PMR *psPMR;
	PVRSRV_ERROR eError;

	if(psConnection == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Invalid connection data"));
		return -ENOENT;
	}

	/*
	 * The bridge lock used here to protect PVRSRVLookupHandle is replaced
	 * by a specific lock considering that the handle functions have now
	 * their own lock. This change was necessary to solve the lockdep issues
	 * related with the PVRSRV_MMap.
	 */
	mutex_lock(&g_sMMapMutex);

	eError = PVRSRVLookupHandle(psConnection->psHandleBase,
								(void **)&psPMR,
								hSecurePMRHandle,
								PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
								IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	/* Note: PMRMMapPMR will take a reference on the PMR.
	 * Unref the handle immediately, because we have now done
	 * the required operation on the PMR (whether it succeeded or not)
	 */
	eError = PMRMMapPMR(psPMR, ps_vma);
	PVRSRVReleaseHandle(psConnection->psHandleBase, hSecurePMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	mutex_unlock(&g_sMMapMutex);

	return 0;

e1:
	PMRUnrefPMR(psPMR);
	goto em1;
e0:
	PVR_DPF((PVR_DBG_ERROR, "Error in mmap critical section"));
em1:
	mutex_unlock(&g_sMMapMutex);

	PVR_DPF((PVR_DBG_ERROR, "Unable to translate error %d", eError));
	PVR_ASSERT(eError != PVRSRV_OK);

	return -ENOENT; // -EAGAIN // or what?
}
