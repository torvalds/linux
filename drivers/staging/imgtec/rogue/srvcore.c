/*************************************************************************/ /*!
@File
@Title          PVR Common Bridge Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements core PVRSRV API, server side
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

#include <stddef.h>

#include "img_defs.h"
#include "pvr_debug.h"
#include "ra.h"
#include "pvr_bridge.h"
#include "connection_server.h"
#include "device.h"
#include "htbuffer.h"

#include "pdump_km.h"

#include "srvkm.h"
#include "allocmem.h"
#include "devicemem.h"

#include "srvcore.h"
#include "rgxinit.h"
#include "pvrsrv.h"
#include "power.h"
#include "lists.h"
#include "rgxdevice.h"

#include "rgx_options.h"
#include "pvrversion.h"
#include "lock.h"
#include "osfunc.h"
#include "device_connection.h"

#include "rgxdevice.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "physmem_lma.h"
#include "services_km.h"
#endif

#include "pvrsrv_tlstreams.h"
#include "tlstream.h"

/* For the purpose of maintainability, it is intended that this file should not
 * contain any OS specific #ifdefs. Please find a way to add e.g.
 * an osfunc.c abstraction or override the entire function in question within
 * env,*,pvr_bridge_k.c
 */

PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT] = { {.pfFunction = DummyBW,} ,};

#define		PVR_DISPATCH_OFFSET_FIRST_FUNC 			0
#define 	PVR_DISPATCH_OFFSET_LAST_FUNC 			1
#define		PVR_DISPATCH_OFFSET_ARRAY_MAX 			2

#define PVR_BUFFER_POOL_MAX 10

typedef struct
{
	IMG_BOOL bTaken;
	void *pvBuffer;
} PVR_POOL_BUFFER;

static struct
{
	POS_LOCK hLock;
	IMG_UINT uiCount;
	PVR_POOL_BUFFER asPool[PVR_BUFFER_POOL_MAX];
} *g_psBridgePool = NULL;

static IMG_UINT16 g_BridgeDispatchTableStartOffsets[BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT][PVR_DISPATCH_OFFSET_ARRAY_MAX];

#if defined(DEBUG_BRIDGE_KM)
/* a lock used for protecting bridge call timing calculations
 * for calls which do not acquire a lock
 */
POS_LOCK g_hStatsLock;
PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif

void BridgeDispatchTableStartOffsetsInit(void)
{
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEFAULT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DEFAULT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEFAULT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DEFAULT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SRVCORE][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SRVCORE_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SRVCORE][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SRVCORE_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCEXPORT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNCEXPORT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCEXPORT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNCEXPORT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCSEXPORT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNCSEXPORT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCSEXPORT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNCSEXPORT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMPCTRL][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMPCTRL][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MM][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_MM_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MM][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_MM_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MMPLAT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_MMPLAT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MMPLAT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_MMPLAT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_CMM][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_CMM_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_CMM][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_CMM_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMPMM][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_PDUMPMM_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMPMM][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_PDUMPMM_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMP][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_PDUMP_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PDUMP][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_PDUMP_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DMABUF][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DMABUF_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DMABUF][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DMABUF_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_CACHE][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_CACHE_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_CACHE][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_CACHE_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SMM][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SMM_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SMM][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SMM_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PVRTL][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_PVRTL_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_PVRTL][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_PVRTL_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RI][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RI_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RI][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RI_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_VALIDATION][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_VALIDATION_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_VALIDATION][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_TUTILS][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_TUTILS_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_TUTILS][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEVICEMEMHISTORY][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEVICEMEMHISTORY][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_HTBUFFER][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_HTBUFFER_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_HTBUFFER][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_HTBUFFER_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DCPLAT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DCPLAT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DCPLAT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DCPLAT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MMEXTMEM][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_MMEXTMEM][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_MMEXTMEM_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCTRACKING][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCTRACKING][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNCTRACKING_DISPATCH_LAST;
#if defined(SUPPORT_RGX)
	/* Need a gap here to start next entry at element 128 */
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXCMP][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXCMP_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXCMP][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXINIT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXINIT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXINIT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXINIT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTA3D][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTA3D_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTA3D][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_BREAKPOINT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_BREAKPOINT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_BREAKPOINT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_BREAKPOINT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEBUGMISC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DEBUGMISC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEBUGMISC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DEBUGMISC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXPDUMP][PVR_DISPATCH_OFFSET_FIRST_FUNC]= PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXPDUMP][PVR_DISPATCH_OFFSET_LAST_FUNC]= PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXHWPERF][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXHWPERF][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXRAY][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXRAY][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_REGCONFIG][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_REGCONFIG_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_REGCONFIG][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_REGCONFIG_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_TIMERQUERY][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_TIMERQUERY_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_TIMERQUERY][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_TIMERQUERY_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXKICKSYNC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXKICKSYNC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXSIGNALS][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXSIGNALS_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXSIGNALS][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXSIGNALS_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ2][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTQ2_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ2][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTQ2_DISPATCH_LAST;
#endif
}

#if defined(DEBUG_BRIDGE_KM)
PVRSRV_ERROR
CopyFromUserWrapper(CONNECTION_DATA *psConnection,
					IMG_UINT32 ui32DispatchTableEntry,
					void *pvDest,
					void *pvSrc,
					IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32DispatchTableEntry].ui32CopyFromUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+=ui32Size;
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  void *pvDest,
				  void *pvSrc,
				  IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32DispatchTableEntry].ui32CopyToUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyToUserBytes+=ui32Size;
	return OSBridgeCopyToUser(psConnection, pvDest, pvSrc, ui32Size);
}
#else
INLINE PVRSRV_ERROR
CopyFromUserWrapper(CONNECTION_DATA *psConnection,
					IMG_UINT32 ui32DispatchTableEntry,
					void *pvDest,
					void *pvSrc,
					IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER (ui32DispatchTableEntry);
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
INLINE PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  void *pvDest,
				  void *pvSrc,
				  IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER (ui32DispatchTableEntry);
	return OSBridgeCopyToUser(psConnection, pvDest, pvSrc, ui32Size);
}
#endif

PVRSRV_ERROR
PVRSRVConnectKM(CONNECTION_DATA *psConnection,
                PVRSRV_DEVICE_NODE * psDeviceNode,
				IMG_UINT32 ui32Flags,
				IMG_UINT32 ui32ClientBuildOptions,
				IMG_UINT32 ui32ClientDDKVersion,
				IMG_UINT32 ui32ClientDDKBuild,
				IMG_UINT8  *pui8KernelArch,
				IMG_UINT32 *pui32CapabilityFlags,
				IMG_UINT32 *ui32PVRBridges,
				IMG_UINT32 *ui32RGXBridges)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32BuildOptions, ui32BuildOptionsMismatch;
	IMG_UINT32			ui32DDKVersion, ui32DDKBuild;
	PVRSRV_DATA			*psSRVData = NULL;
	IMG_UINT64			ui64ProcessVASpaceSize = OSGetCurrentProcessVASpaceSize();
	static IMG_BOOL		bIsFirstConnection=IMG_FALSE;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	/* Clear the flags */
	*pui32CapabilityFlags = 0;
	psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;
	
	psSRVData = PVRSRVGetPVRSRVData();

	psConnection->ui32ClientFlags = ui32Flags;

	/* output the available bridges */
	*ui32PVRBridges = gui32PVRBridges;
	*ui32RGXBridges = gui32RGXBridges;

	/*Set flags to pass back to the client showing which cache coherency is available.*/
	/*Is the system CPU cache coherent?*/
	if (PVRSRVSystemSnoopingOfCPUCache(psDeviceNode->psDevConfig))
	{
		*pui32CapabilityFlags |= PVRSRV_CACHE_COHERENT_DEVICE_FLAG;
	}
	/*Is the system device cache coherent?*/
	if (PVRSRVSystemSnoopingOfDeviceCache(psDeviceNode->psDevConfig))
	{
		*pui32CapabilityFlags |= PVRSRV_CACHE_COHERENT_CPU_FLAG;
	}
	/* Has the system device non-mappable local memory?*/
	if (PVRSRVSystemHasNonMappableLocalMemory(psDeviceNode->psDevConfig))
	{
		*pui32CapabilityFlags |= PVRSRV_NONMAPPABLE_MEMORY_PRESENT_FLAG;
	}

	/* Set flags to indicate shared-virtual-memory (SVM) allocation availability */
	if (! psDeviceNode->ui64GeneralSVMHeapSize || ! ui64ProcessVASpaceSize)
	{
		*pui32CapabilityFlags |= PVRSRV_DEVMEM_SVM_ALLOC_UNSUPPORTED;
	}
	else
	{
		if (ui64ProcessVASpaceSize <= psDeviceNode->ui64GeneralSVMHeapSize)
		{
			*pui32CapabilityFlags |= PVRSRV_DEVMEM_SVM_ALLOC_SUPPORTED;
		}
		else
		{
			/* This can happen when processor has more virtual address bits
			   than device (i.e. alloc is not always guaranteed to succeed) */
			*pui32CapabilityFlags |= PVRSRV_DEVMEM_SVM_ALLOC_CANFAIL;
		}
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	IMG_UINT32	ui32OSid = 0, ui32OSidReg = 0;
    IMG_BOOL    bOSidAxiProtReg = IMG_FALSE;

	IMG_PID pIDCurrent = OSGetCurrentClientProcessIDKM();

    ui32OSid    = (ui32Flags & SRV_VIRTVAL_FLAG_OSID_MASK)    >> (VIRTVAL_FLAG_OSID_SHIFT);
    ui32OSidReg = (ui32Flags & SRV_VIRTVAL_FLAG_OSIDREG_MASK) >> (VIRTVAL_FLAG_OSIDREG_SHIFT);

#if defined(EMULATOR)

    if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_AXI_ACELITE_BIT_MASK)
    {
    	IMG_UINT32 ui32OSidAxiProtReg = 0, ui32OSidAxiProtTD = 0;

    	ui32OSidAxiProtReg = (ui32Flags & SRV_VIRTVAL_FLAG_AXIPREG_MASK) >> (VIRTVAL_FLAG_AXIPREG_SHIFT);
    	ui32OSidAxiProtTD  = (ui32Flags & SRV_VIRTVAL_FLAG_AXIPTD_MASK)  >> (VIRTVAL_FLAG_AXIPTD_SHIFT);

    	PVR_DPF((PVR_DBG_MESSAGE,
    			"[AxiProt & Virt]: Setting bOSidAxiProt of Emulator's Trusted Device for Catbase %d to %s",
				ui32OSidReg,
				(ui32OSidAxiProtTD == 1)?"TRUE":"FALSE"));

    	bOSidAxiProtReg = ui32OSidAxiProtReg == 1;
    	PVR_DPF((PVR_DBG_MESSAGE,
    			"[AxiProt & Virt]: Setting bOSidAxiProt of FW's Register for Catbase %d to %s",
				ui32OSidReg,
				bOSidAxiProtReg?"TRUE":"FALSE"));

    	SetAxiProtOSid(ui32OSidReg, ui32OSidAxiProtTD);
    }

#endif

    InsertPidOSidsCoupling(pIDCurrent, ui32OSid, ui32OSidReg, bOSidAxiProtReg);

    PVR_DPF((PVR_DBG_MESSAGE,"[GPU Virtualization Validation]: OSIDs: %d, %d\n",ui32OSid, ui32OSidReg));
}
#endif

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	/* Only enable if enabled in the UM */
	if(ui32Flags & SRV_WORKEST_ENABLED)
	{
		psDevInfo->bWorkEstEnabled = IMG_TRUE;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVConnectKM: Workload Estimation disabled. Not enabled in UM."));
	}
#endif

#if defined(SUPPORT_PDVFS)
	/* Only enable if enabled in the UM */
	if(ui32Flags & SRV_PDVFS_ENABLED)
	{
		psDevInfo->bPDVFSEnabled = IMG_TRUE;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVConnectKM: Proactive DVFS disabled. Not enabled in UM."));
	}
#endif

	if (ui32Flags & SRV_FLAGS_INIT_PROCESS)
#if defined(SUPPORT_KERNEL_SRVINIT)
	{
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}
#else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Connecting as init process", __func__));
		if ((OSProcHasPrivSrvInit() == IMG_FALSE) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Rejecting init process", __func__));
			eError = PVRSRV_ERROR_SRV_CONNECT_FAILED;
			goto chk_exit;
		}
#if defined (__linux__) || defined(INTEGRITY_OS)
		PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_TRUE);
#endif
	}
	else
	{
		/*
		 * This check has to be done here (before the client against kernel check)
		 * while the client options have not yet been modified
		 */
#if !defined(PVRSRV_GPUVIRT_GUESTDRV)
		eError = RGXClientConnectCompatCheck_ClientAgainstFW(psDeviceNode, ui32ClientBuildOptions);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation failed. Mismatch between client and firmware build options.",
					__FUNCTION__));
			eError = PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
			goto chk_exit;
		}
#endif

		if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			if (!PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation failed.  Driver unusable.",
					__FUNCTION__));
				eError = PVRSRV_ERROR_INIT_FAILURE;
				goto chk_exit;
			}
		}
		else
		{
			if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation is in progress",
						 __FUNCTION__));
				eError = PVRSRV_ERROR_RETRY;
				goto chk_exit;
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Driver initialisation not completed yet.",
						 __FUNCTION__));
				eError = PVRSRV_ERROR_RETRY;
				goto chk_exit;
			}
		}
	}
#endif /* defined(SUPPORT_KERNEL_SRVINIT) */

	ui32DDKVersion = PVRVERSION_PACK(PVRVERSION_MAJ, PVRVERSION_MIN);
	ui32DDKBuild = PVRVERSION_BUILD;

	if(IMG_FALSE == bIsFirstConnection)
	{
		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildOptions = ui32ClientBuildOptions;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildVersion = ui32DDKVersion;
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildVersion = ui32ClientDDKVersion;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildRevision = ui32DDKBuild;
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildRevision = ui32ClientDDKBuild;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildType = ((RGX_BUILD_OPTIONS_KM) & OPTIONS_DEBUG_MASK)? \
																	BUILD_TYPE_DEBUG:BUILD_TYPE_RELEASE;
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildType = (ui32ClientBuildOptions & OPTIONS_DEBUG_MASK)? \
																	BUILD_TYPE_DEBUG:BUILD_TYPE_RELEASE;
	}

	/* Masking out every option that is not kernel specific*/
	ui32ClientBuildOptions &= RGX_BUILD_OPTIONS_MASK_KM;

	/*
	 * Validate the build options
	 */
	ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);
	if (ui32BuildOptions != ui32ClientBuildOptions)
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ ui32ClientBuildOptions;
#if !defined(PVRSRV_STRICT_COMPAT_CHECK)
		/*Mask the debug flag option out as we do support combinations of debug vs release in um & km*/
		ui32BuildOptionsMismatch &= ~OPTIONS_DEBUG_MASK;
#endif
		if ( (ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in client-side driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
			eError = PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
			goto chk_exit;
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in KM driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32BuildOptions & ui32BuildOptionsMismatch ));
			eError = PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
			goto chk_exit;
		}
		if(IMG_FALSE == bIsFirstConnection)
		{
			PVR_LOG(("%s: COMPAT_TEST: Client-side (0x%04x) (%s) and KM driver (0x%04x) (%s) build options differ.",
																			__FUNCTION__,
																			ui32ClientBuildOptions,
																			(psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildType)?"release":"debug",
																			ui32BuildOptions,
																			(psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildType)?"release":"debug"));
		}else{
			PVR_DPF((PVR_DBG_WARNING, "%s: COMPAT_TEST: Client-side (0x%04x) and KM driver (0x%04x) build options differ.",
																		__FUNCTION__,
																		ui32ClientBuildOptions,
																		ui32BuildOptions));

		}
		if(!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: Client-side and KM driver build options match. [ OK ]", __FUNCTION__));
	}

	/*
	 * Validate DDK version
	 */
	if (ui32ClientDDKVersion != ui32DDKVersion)
	{
		if(!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK version (%u.%u) / client DDK version (%u.%u).",
				__FUNCTION__,
				PVRVERSION_MAJ, PVRVERSION_MIN,
				PVRVERSION_UNPACK_MAJ(ui32ClientDDKVersion),
				PVRVERSION_UNPACK_MIN(ui32ClientDDKVersion)));
		eError = PVRSRV_ERROR_DDK_VERSION_MISMATCH;
		PVR_DBG_BREAK;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK version (%u.%u) and client DDK version (%u.%u) match. [ OK ]",
				__FUNCTION__,
				PVRVERSION_MAJ, PVRVERSION_MIN, PVRVERSION_MAJ, PVRVERSION_MIN));
	}

	/* Create stream for every connection except for the special clients
	 * that doesn't need it e.g.: recipients of HWPerf data. */
	if (!(psConnection->ui32ClientFlags & SRV_NO_HWPERF_CLIENT_STREAM))
	{
		IMG_CHAR acStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
		OSSNPrintf(acStreamName, PRVSRVTL_MAX_STREAM_NAME_SIZE,
		           PVRSRV_TL_HWPERF_HOST_CLIENT_STREAM_FMTSPEC,
		           psConnection->pid);
		eError = TLStreamCreate(&psConnection->hClientTLStream, acStreamName,
		                        131072, TL_FLAG_ALLOCATE_ON_FIRST_OPEN, NULL,
		                        NULL, NULL, NULL);
		if (eError != PVRSRV_OK && eError != PVRSRV_ERROR_ALREADY_EXISTS)
		{
			PVR_DPF((PVR_DBG_ERROR, "Could not create private TL stream (%s)",
					PVRSRVGetErrorStringKM(eError)));
			psConnection->hClientTLStream = NULL;
		}
		/* Reset error status. We don't want to propagate any errors from here. */
		eError = PVRSRV_OK;

		PVR_DPF((PVR_DBG_MESSAGE, "Created stream \"%s\".", acStreamName));
	}

	/*
	 * Validate DDK build
	 */
	if (ui32ClientDDKBuild != ui32DDKBuild)
	{
		if(!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
		PVR_DPF((PVR_DBG_WARNING, "%s: Mismatch in driver DDK revision (%d) / client DDK revision (%d).",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
#if defined(PVRSRV_STRICT_COMPAT_CHECK)
		eError = PVRSRV_ERROR_DDK_BUILD_MISMATCH;
		PVR_DBG_BREAK;
		goto chk_exit;
#endif
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK revision (%d) and client DDK revision (%d) match. [ OK ]",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
	}

	/* Success so far so is it the PDump client that is connecting? */
	if (ui32Flags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpConnectionNotify();
	}

	PVR_ASSERT(pui8KernelArch != NULL);
	/* Can't use __SIZEOF_POINTER__ here as it is not defined on Windows */
	if (sizeof(void *) == 8)
	{
		*pui8KernelArch = 64;
	}
	else
	{
		*pui8KernelArch = 32;
	}

	bIsFirstConnection = IMG_TRUE;

#if defined(DEBUG_BRIDGE_KM)
	{
		int ii;

		/* dump dispatch table offset lookup table */
		PVR_DPF((PVR_DBG_MESSAGE, "%s: g_BridgeDispatchTableStartOffsets[0-%lu] entries:", __FUNCTION__, BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT - 1));
		for (ii=0; ii < BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT; ii++)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "g_BridgeDispatchTableStartOffsets[%d]: %u", ii, g_BridgeDispatchTableStartOffsets[ii][PVR_DISPATCH_OFFSET_FIRST_FUNC]));
		}
	}
#endif

chk_exit:
	return eError;
}

PVRSRV_ERROR
PVRSRVDisconnectKM(void)
{
	/* just return OK, per-process data is cleaned up by resmgr */

	return PVRSRV_OK;
}

/**************************************************************************/ /*!
@Function       PVRSRVAcquireGlobalEventObjectKM
@Description    Acquire the global event object.
@Output         phGlobalEventObject    On success, points to the global event
                                       object handle
@Return         PVRSRV_ERROR           PVRSRV_OK on success or an error
                                       otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVAcquireGlobalEventObjectKM(IMG_HANDLE *phGlobalEventObject)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	*phGlobalEventObject = psPVRSRVData->hGlobalEventObject;

	return PVRSRV_OK;
}

/**************************************************************************/ /*!
@Function       PVRSRVReleaseGlobalEventObjectKM
@Description    Release the global event object.
@Output         hGlobalEventObject    Global event object handle
@Return         PVRSRV_ERROR          PVRSRV_OK on success or an error otherwise
*/ /***************************************************************************/
PVRSRV_ERROR
PVRSRVReleaseGlobalEventObjectKM(IMG_HANDLE hGlobalEventObject)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_ASSERT(psPVRSRVData->hGlobalEventObject == hGlobalEventObject);

	return PVRSRV_OK;
}

/*
	PVRSRVDumpDebugInfoKM
*/
PVRSRV_ERROR
PVRSRVDumpDebugInfoKM(CONNECTION_DATA *psConnection,
					  PVRSRV_DEVICE_NODE *psDeviceNode,
					  IMG_UINT32 ui32VerbLevel)
{
	if (ui32VerbLevel > DEBUG_REQUEST_VERBOSITY_MAX)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	PVR_LOG(("User requested PVR debug info"));

	PVRSRVDebugRequest(psDeviceNode, ui32VerbLevel, NULL, NULL);
	
	return PVRSRV_OK;
}

/*
	PVRSRVGetDevClockSpeedKM
*/
PVRSRV_ERROR
PVRSRVGetDevClockSpeedKM(CONNECTION_DATA * psConnection,
                         PVRSRV_DEVICE_NODE *psDeviceNode,
						 IMG_PUINT32  pui32RGXClockSpeed)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_ASSERT(psDeviceNode->pfnDeviceClockSpeed != NULL);

	PVR_UNREFERENCED_PARAMETER(psConnection);

	eError = psDeviceNode->pfnDeviceClockSpeed(psDeviceNode, pui32RGXClockSpeed);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetDevClockSpeedKM: "
				"Could not get device clock speed (%d)!",
				eError));
	}

	return eError;
}


/*
	PVRSRVHWOpTimeoutKM
*/
PVRSRV_ERROR
PVRSRVHWOpTimeoutKM(CONNECTION_DATA *psConnection,
					PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(PVRSRV_RESET_ON_HWTIMEOUT)
	PVR_LOG(("User requested OS reset"));
	OSPanic();
#endif
	PVR_LOG(("HW operation timeout, dump server info"));
	PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MEDIUM, NULL, NULL);
	return PVRSRV_OK;
}


IMG_INT
DummyBW(IMG_UINT32 ui32DispatchTableEntry,
		void *psBridgeIn,
		void *psBridgeOut,
		CONNECTION_DATA *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psBridgeOut);
	PVR_UNREFERENCED_PARAMETER(psConnection);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: ui32DispatchTableEntry %u (%s) mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32DispatchTableEntry, g_BridgeDispatchTable[ui32DispatchTableEntry].pszIOCName));
#else
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: ui32DispatchTableEntry %u mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32DispatchTableEntry));
#endif
	return PVRSRV_ERROR_BRIDGE_ENOTTY;
}

#if defined(SUPPORT_KERNEL_SRVINIT)
PVRSRV_ERROR PVRSRVAlignmentCheckKM(CONNECTION_DATA *psConnection,
                                    PVRSRV_DEVICE_NODE *psDeviceNode,
                                    IMG_UINT32 ui32AlignChecksSize,
                                    IMG_UINT32 aui32AlignChecks[])
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

#if !defined(NO_HARDWARE) && defined(RGXFW_ALIGNCHECKS)

	PVR_ASSERT(psDeviceNode->pfnAlignmentCheck != NULL);
	return psDeviceNode->pfnAlignmentCheck(psDeviceNode, ui32AlignChecksSize,
	                                       aui32AlignChecks);

#else

	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32AlignChecksSize);
	PVR_UNREFERENCED_PARAMETER(aui32AlignChecks);

	return PVRSRV_OK;

#endif /* !defined(NO_HARDWARE) */

}
#endif /* defined(SUPPORT_KERNEL_SRVINIT) */

PVRSRV_ERROR PVRSRVGetDeviceStatusKM(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_UINT32 *pui32DeviceStatus)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* First try to update the status. */
	if (psDeviceNode->pfnUpdateHealthStatus != NULL)
	{
		PVRSRV_ERROR eError = psDeviceNode->pfnUpdateHealthStatus(psDeviceNode,
		                                                          IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetDeviceStatusKM: Failed to"
					 "check for device status (%d)", eError));

			/* Return unknown status and error because we don't know what
			 * happened and if the status is valid. */
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_UNKNOWN;
			return eError;
		}
	}

	switch (OSAtomicRead(&psDeviceNode->eHealthStatus))
	{
		case PVRSRV_DEVICE_HEALTH_STATUS_OK:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_OK;
			return PVRSRV_OK;
		case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_NOT_RESPONDING;
			return PVRSRV_OK;
		case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_DEVICE_ERROR;
			return PVRSRV_OK;
		default:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_UNKNOWN;
			return PVRSRV_ERROR_INTERNAL_ERROR;
	}
}

/*!
 * *****************************************************************************
 * @brief A wrapper for filling in the g_BridgeDispatchTable array that does
 * 		  error checking.
 *
 * @param ui32Index
 * @param pszIOCName
 * @param pfFunction
 * @param pszFunctionName
 *
 * @return
 ********************************************************************************/
void
_SetDispatchTableEntry(IMG_UINT32 ui32BridgeGroup,
					   IMG_UINT32 ui32Index,
					   const IMG_CHAR *pszIOCName,
					   BridgeWrapperFunction pfFunction,
					   const IMG_CHAR *pszFunctionName,
					   POS_LOCK hBridgeLock,
					   const IMG_CHAR *pszBridgeLockName,
					   IMG_BOOL bUseLock)
{
	static IMG_UINT32 ui32PrevIndex = IMG_UINT32_MAX;		/* -1 */

#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
	PVR_UNREFERENCED_PARAMETER(pszBridgeLockName);
#endif

	ui32Index += g_BridgeDispatchTableStartOffsets[ui32BridgeGroup][PVR_DISPATCH_OFFSET_FIRST_FUNC];

#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
	/* Enable this to dump out the dispatch table entries */
	PVR_DPF((PVR_DBG_WARNING, "%s: g_BridgeDispatchTableStartOffsets[%d]=%d", __FUNCTION__, ui32BridgeGroup, g_BridgeDispatchTableStartOffsets[ui32BridgeGroup][PVR_DISPATCH_OFFSET_FIRST_FUNC]));
	PVR_DPF((PVR_DBG_WARNING, "%s: %d %s %s %s", __FUNCTION__, ui32Index, pszIOCName, pszFunctionName, pszBridgeLockName));
#endif

	/* Any gaps are sub-optimal in-terms of memory usage, but we are mainly
	 * interested in spotting any large gap of wasted memory that could be
	 * accidentally introduced.
	 *
	 * This will currently flag up any gaps > 5 entries.
	 *
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likely to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at a performance critical time.
	 */
	if((ui32PrevIndex != IMG_UINT32_MAX) &&
	   ((ui32Index >= ui32PrevIndex + DISPATCH_TABLE_GAP_THRESHOLD) ||
		(ui32Index <= ui32PrevIndex)))
	{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: There is a gap in the dispatch table between indices %u (%s) and %u (%s)",
				 __FUNCTION__, ui32PrevIndex, g_BridgeDispatchTable[ui32PrevIndex].pszIOCName,
				 ui32Index, pszIOCName));
#else
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: There is a gap in the dispatch table between indices %u and %u (%s)",
				 __FUNCTION__, (IMG_UINT)ui32PrevIndex, (IMG_UINT)ui32Index, pszIOCName));
#endif
	}

	if (ui32Index >= BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Index %u (%s) out of range",
				 __FUNCTION__, (IMG_UINT)ui32Index, pszIOCName));

#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE_DISPATCH_TABLE_ENTRY_COUNT = %lu",
				 __FUNCTION__, BRIDGE_DISPATCH_TABLE_ENTRY_COUNT));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_TIMERQUERY_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_TIMERQUERY_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_REGCONFIG_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_REGCONFIG_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_DEBUGMISC_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_DEBUGMISC_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_BREAKPOINT_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_BREAKPOINT_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXINIT_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXINIT_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST = %lu\n",
				 __FUNCTION__, PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST));

		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGX_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGX_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGX_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_RGX_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST = %lu",
				 __FUNCTION__, PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST));
#endif

		OSPanic();
	}

	/* Panic if the previous entry has been overwritten as this is not allowed!
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likely to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at a performance critical time.
	 */
	if(g_BridgeDispatchTable[ui32Index].pfFunction)
	{
		if(g_BridgeDispatchTable[ui32Index].pfFunction != pfFunction)
		{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: Adding dispatch table entry for %s clobbers an existing entry for %s (current pfn=<%p>, new pfn=<%p>)",
				 __FUNCTION__, pszIOCName, g_BridgeDispatchTable[ui32Index].pszIOCName),
				 (void*)g_BridgeDispatchTable[ui32Index].pfFunction, (void*)pfFunction));
#else
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: Adding dispatch table entry for %s clobbers an existing entry (index=%u). (current pfn=<%p>, new pfn=<%p>)",
				 __FUNCTION__, pszIOCName, ui32Index,
				 (void*)g_BridgeDispatchTable[ui32Index].pfFunction, (void*)pfFunction));
			PVR_DPF((PVR_DBG_WARNING, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
#endif
			OSPanic();
		}
	}
	else
	{
		g_BridgeDispatchTable[ui32Index].pfFunction = pfFunction;
		g_BridgeDispatchTable[ui32Index].hBridgeLock = hBridgeLock;
		g_BridgeDispatchTable[ui32Index].bUseLock = bUseLock;
#if defined(DEBUG_BRIDGE_KM)
		g_BridgeDispatchTable[ui32Index].pszIOCName = pszIOCName;
		g_BridgeDispatchTable[ui32Index].pszFunctionName = pszFunctionName;
		g_BridgeDispatchTable[ui32Index].pszBridgeLockName = pszBridgeLockName;
		g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
		g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
		g_BridgeDispatchTable[ui32Index].ui64TotalTimeNS = 0;
		g_BridgeDispatchTable[ui32Index].ui64MaxTimeNS = 0;
#endif
	}

	ui32PrevIndex = ui32Index;
}

PVRSRV_ERROR
PVRSRVInitSrvDisconnectKM(CONNECTION_DATA *psConnection,
                          PVRSRV_DEVICE_NODE * psDeviceNode,
                          IMG_BOOL bInitSuccesful,
                          IMG_UINT32 ui32ClientBuildOptions)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_KERNEL_SRVINIT)
	if (psConnection)
	{
		/* Assume this is being called by a user space process */
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}
#else
	if (!(psConnection->ui32ClientFlags & SRV_FLAGS_INIT_PROCESS))
	{
		return PVRSRV_ERROR_SRV_DISCONNECT_FAILED;
	}

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_FALSE);
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RAN, IMG_TRUE);
#endif

	eError = PVRSRVDeviceFinalise(psDeviceNode, bInitSuccesful);

#if !defined(SUPPORT_KERNEL_SRVINIT)
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL,
				(eError == PVRSRV_OK) && bInitSuccesful);
#endif

	return eError;
}

static PVRSRV_ERROR _BridgeBufferPoolCreate(void)
{
	PVRSRV_ERROR eError;

	PVR_DPF((PVR_DBG_VERBOSE, "BridgePoolCreate: Creating bridge buffer pool."));

	g_psBridgePool = OSAllocZMemNoStats(sizeof(*g_psBridgePool));
	if (g_psBridgePool == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "BridgePoolCreate: Failed to allocate memory "
				"for the bridge buffer pool."));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = OSLockCreate(&g_psBridgePool->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BridgePoolCreate: Failed to create lock "
				"for the bridge buffer pool."));
		OSFreeMemNoStats(g_psBridgePool);
		return eError;
	}

	return PVRSRV_OK;
}

static void _BridgeBufferPoolDestroy(void)
{
	IMG_UINT i;

	PVR_DPF((PVR_DBG_VERBOSE, "Destroying bridge buffer pool."));

	for (i = 0; i < g_psBridgePool->uiCount; i++)
		OSFreeMem(g_psBridgePool->asPool[i].pvBuffer);

	OSLockDestroy(g_psBridgePool->hLock);
	OSFreeMemNoStats(g_psBridgePool);
}

PVRSRV_ERROR BridgeInit(void)
{
	PVRSRV_ERROR eError;

	eError = _BridgeBufferPoolCreate();

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to create bridge buffer pool"));
		return eError;
	}

#if defined(DEBUG_BRIDGE_KM)
	eError = OSLockCreate(&g_hStatsLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to create bridge stats lock"));
		return eError;
	}
#endif

	return PVRSRV_OK;
}

void BridgeDeinit(void)
{
#if defined(DEBUG_BRIDGE_KM)
	if(g_hStatsLock)
	{
		OSLockDestroy(g_hStatsLock);
		g_hStatsLock = NULL;
	}
#endif

	_BridgeBufferPoolDestroy();
}

static PVR_POOL_BUFFER *_BridgePoolAcquireBuffer(void **ppvBridgeIn,
                                                 void **ppvBridgeOut)
{
	PVR_POOL_BUFFER *psPoolBuffer = NULL;
	IMG_UINT i;

	PVR_ASSERT(g_psBridgePool != NULL);
	PVR_ASSERT(ppvBridgeIn != NULL && ppvBridgeOut != NULL);

	OSLockAcquire(g_psBridgePool->hLock);

	for (i = 0; i < PVR_BUFFER_POOL_MAX; i++)
	{
		PVR_POOL_BUFFER *psBuffer = &g_psBridgePool->asPool[i];

		if (psBuffer->pvBuffer != NULL)
		{
			if (psBuffer->bTaken)
				continue;

			PVR_DPF((PVR_DBG_VERBOSE, "_BridgePoolAcquireBuffer: "
			        "Reusing buffer %p.", psBuffer->pvBuffer));

			psBuffer->bTaken = IMG_TRUE;
			*ppvBridgeIn = psBuffer->pvBuffer;
			*ppvBridgeOut = ((IMG_BYTE *) psBuffer->pvBuffer) +
			        PVRSRV_MAX_BRIDGE_IN_SIZE;

			psPoolBuffer = psBuffer;
			goto return_;
		}
		else
		{
			PVR_DPF((PVR_DBG_VERBOSE, "_BridgePoolAcquireBuffer: "
			        "Allocating new bridge buffer."));

			psBuffer->pvBuffer = OSAllocZMemNoStats(PVRSRV_MAX_BRIDGE_IN_SIZE +
			                                PVRSRV_MAX_BRIDGE_OUT_SIZE);
			if (psBuffer->pvBuffer == NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "_BridgePoolAcquireBuffer: "
				        "Out of memory! Could not allocate new buffer."));
				goto return_;
			}

			*ppvBridgeIn = psBuffer->pvBuffer;
			*ppvBridgeOut = ((IMG_BYTE *) psBuffer->pvBuffer) +
			        PVRSRV_MAX_BRIDGE_IN_SIZE;
			g_psBridgePool->uiCount++;

			psPoolBuffer = psBuffer;
			goto return_;
		}
	}

	PVR_DPF((PVR_DBG_ERROR, "_BridgePoolAcquireBuffer: "
	        "Not enough buffers in the pool."));

return_:
	OSLockRelease(g_psBridgePool->hLock);

	return psPoolBuffer;
}

static void _BridgePoolReleaseBuffers(PVR_POOL_BUFFER *psBuffer)
{
	PVR_ASSERT(g_psBridgePool != NULL);

	if (psBuffer == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Called release on NULL buffer",
		        __FUNCTION__));
		return;
	}

	OSLockAcquire(g_psBridgePool->hLock);

	PVR_DPF((PVR_DBG_VERBOSE, "_BridgePoolReleaseBuffers: "
	        "Releasing buffer %p.", psBuffer->pvBuffer));
	psBuffer->bTaken = IMG_FALSE;

	OSLockRelease(g_psBridgePool->hLock);
}

PVRSRV_ERROR BridgedDispatchKM(CONNECTION_DATA * psConnection,
                          PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM)
{

	void       * psBridgeIn=NULL;
	void       * psBridgeOut=NULL;
	BridgeWrapperFunction pfBridgeHandler;
	IMG_UINT32   ui32DispatchTableEntry, ui32GroupBoundary;
	PVRSRV_ERROR err = PVRSRV_OK;
	PVR_POOL_BUFFER *psPoolBuffer = NULL;
	IMG_UINT32 ui32Timestamp = OSClockus();
#if defined(DEBUG_BRIDGE_KM)
	IMG_UINT64	ui64TimeStart;
	IMG_UINT64	ui64TimeEnd;
	IMG_UINT64	ui64TimeDiff;
#endif

#if defined(DEBUG_BRIDGE_KM_STOP_AT_DISPATCH)
	PVR_DBG_BREAK;
#endif

	if(BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT <= psBridgePackageKM->ui32BridgeID)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Out of range dispatch table group ID: %d",
		        __FUNCTION__, psBridgePackageKM->ui32BridgeID));
		err = PVRSRV_ERROR_BRIDGE_EINVAL;
		goto return_error;
	}
	ui32DispatchTableEntry = g_BridgeDispatchTableStartOffsets[psBridgePackageKM->ui32BridgeID][PVR_DISPATCH_OFFSET_FIRST_FUNC];
	ui32GroupBoundary = g_BridgeDispatchTableStartOffsets[psBridgePackageKM->ui32BridgeID][PVR_DISPATCH_OFFSET_LAST_FUNC];

	/* bridge function is not implemented in this build */
	if(0 == ui32DispatchTableEntry)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Dispatch table entry=%d, boundary = %d, (bridge module %d, function %d)",
					__FUNCTION__,
					ui32DispatchTableEntry,ui32GroupBoundary, psBridgePackageKM->ui32BridgeID, psBridgePackageKM->ui32FunctionID));
		/* this points to DummyBW() which returns PVRSRV_ERROR_ENOTTY */
		err = g_BridgeDispatchTable[ui32DispatchTableEntry].pfFunction(ui32DispatchTableEntry,
				  psBridgeIn,
				  psBridgeOut,
				  psConnection);
		goto return_error;
	}
	else
	{
		ui32DispatchTableEntry +=  psBridgePackageKM->ui32FunctionID;
	}
	if(ui32DispatchTableEntry > ui32GroupBoundary)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Dispatch table entry=%d, boundary = %d, (bridge module %d, function %d)",
					__FUNCTION__,
					ui32DispatchTableEntry,ui32GroupBoundary, psBridgePackageKM->ui32BridgeID, psBridgePackageKM->ui32FunctionID));
		err = PVRSRV_ERROR_BRIDGE_EINVAL;
		goto return_error;
	}
	if(BRIDGE_DISPATCH_TABLE_ENTRY_COUNT <= ui32DispatchTableEntry)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Dispatch table entry=%d, entry count = %lu,"
		        " (bridge module %d, function %d)", __FUNCTION__,
		        ui32DispatchTableEntry, BRIDGE_DISPATCH_TABLE_ENTRY_COUNT,
		        psBridgePackageKM->ui32BridgeID,
		        psBridgePackageKM->ui32FunctionID));
		err = PVRSRV_ERROR_BRIDGE_EINVAL;
		goto return_error;
	}
#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "%s: Dispatch table entry=%d, (bridge module %d, function %d)",
			__FUNCTION__,
			ui32DispatchTableEntry, psBridgePackageKM->ui32BridgeID, psBridgePackageKM->ui32FunctionID));
	PVR_DPF((PVR_DBG_MESSAGE, "%s: %s",
			 __FUNCTION__,
			 g_BridgeDispatchTable[ui32DispatchTableEntry].pszIOCName));
	g_BridgeDispatchTable[ui32DispatchTableEntry].ui32CallCount++;
	g_BridgeGlobalStats.ui32IOCTLCount++;
#endif

	if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock == NULL &&
	    g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
	{
		/* Acquire default global bridge lock if calling module has no independent lock */
		OSAcquireBridgeLock();

		/* Request for global bridge buffers */
		OSGetGlobalBridgeBuffers(&psBridgeIn,
		                         &psBridgeOut);
	}
	else
	{
		if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock != NULL &&
		    g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
		{
			OSLockAcquire(g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock);
		}

		psPoolBuffer = _BridgePoolAcquireBuffer(&psBridgeIn,
		                                        &psBridgeOut);
		if (psPoolBuffer == NULL)
		{
			err = PVRSRV_ERROR_BRIDGE_ENOMEM;
			goto unlock_and_return_error;
		}
	}

#if defined(DEBUG_BRIDGE_KM)
	ui64TimeStart = OSClockns64();
#endif

	if (psBridgePackageKM->ui32InBufferSize > PVRSRV_MAX_BRIDGE_IN_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge input buffer too small "
		        "(data size %u, buffer size %u)!", __FUNCTION__,
		        psBridgePackageKM->ui32InBufferSize, PVRSRV_MAX_BRIDGE_IN_SIZE));
		err = PVRSRV_ERROR_BRIDGE_ERANGE;
		goto unlock_and_return_error;
	}
	
#if !defined(INTEGRITY_OS)
	if (psBridgePackageKM->ui32OutBufferSize > PVRSRV_MAX_BRIDGE_OUT_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge output buffer too small "
		        "(data size %u, buffer size %u)!", __FUNCTION__,
		        psBridgePackageKM->ui32OutBufferSize, PVRSRV_MAX_BRIDGE_OUT_SIZE));
		err = PVRSRV_ERROR_BRIDGE_ERANGE;
		goto unlock_and_return_error;
	}

	if((CopyFromUserWrapper (psConnection,
					ui32DispatchTableEntry,
					psBridgeIn,
					psBridgePackageKM->pvParamIn,
					psBridgePackageKM->ui32InBufferSize) != PVRSRV_OK)
#if defined __QNXNTO__
/* For Neutrino, the output bridge buffer acts as an input as well */
					|| (CopyFromUserWrapper(psConnection,
											ui32DispatchTableEntry,
											psBridgeOut,
											(void *)((IMG_UINT32)psBridgePackageKM->pvParamIn + psBridgePackageKM->ui32InBufferSize),
											psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
#endif
		) /* end of if-condition */
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: CopyFromUserWrapper returned an error!", __FUNCTION__));
		err = PVRSRV_ERROR_BRIDGE_EFAULT;
		goto unlock_and_return_error;
	}
#else
	psBridgeIn = psBridgePackageKM->pvParamIn;
	psBridgeOut = psBridgePackageKM->pvParamOut;
#endif

	pfBridgeHandler =
		(BridgeWrapperFunction)g_BridgeDispatchTable[ui32DispatchTableEntry].pfFunction;
	
	if (pfBridgeHandler == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32DispatchTableEntry = %d is not a registered function!",
				 __FUNCTION__, ui32DispatchTableEntry));
		err = PVRSRV_ERROR_BRIDGE_EFAULT;
		goto unlock_and_return_error;
	}
	
	/* pfBridgeHandler functions do not fail and return an IMG_INT.
	 * The value returned is either 0 or PVRSRV_OK (0).
	 * In the event this changes an error may be +ve or -ve,
	 * so try to return something consistent here.
	 */
	if (0 != pfBridgeHandler(ui32DispatchTableEntry,
						  psBridgeIn,
						  psBridgeOut,
						  psConnection)
		)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: pfBridgeHandler returned an error", __FUNCTION__));
		err = PVRSRV_ERROR_BRIDGE_EPERM;
		goto unlock_and_return_error;
	}

	/*
	   This should always be true as a.t.m. all bridge calls have to
	   return an error message, but this could change so we do this
	   check to be safe.
	*/
	if (psBridgePackageKM->ui32OutBufferSize > 0)
	{
#if !defined(INTEGRITY_OS)
		if (CopyToUserWrapper (psConnection,
						ui32DispatchTableEntry,
						psBridgePackageKM->pvParamOut,
						psBridgeOut,
						psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
		{
			err = PVRSRV_ERROR_BRIDGE_EFAULT;
			goto unlock_and_return_error;
		}
#endif
	}

#if defined(DEBUG_BRIDGE_KM)
	ui64TimeEnd = OSClockns64();

	ui64TimeDiff = ui64TimeEnd - ui64TimeStart;

	/* if there is no lock held then acquire the stats lock to
	 * ensure the calculations are done safely
	 */
	if(!g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
	{
		OSLockAcquire(g_hStatsLock);
	}

	g_BridgeDispatchTable[ui32DispatchTableEntry].ui64TotalTimeNS += ui64TimeDiff;

	if(ui64TimeDiff > g_BridgeDispatchTable[ui32DispatchTableEntry].ui64MaxTimeNS)
	{
		g_BridgeDispatchTable[ui32DispatchTableEntry].ui64MaxTimeNS = ui64TimeDiff;
	}

	if(!g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
	{
		OSLockRelease(g_hStatsLock);
	}
#endif

unlock_and_return_error:
	if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock == NULL &&
	    g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
	{
		OSReleaseBridgeLock();
	}
	else
	{
		if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock != NULL &&
		    g_BridgeDispatchTable[ui32DispatchTableEntry].bUseLock)
		{
			OSLockRelease(g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock);
		}

		_BridgePoolReleaseBuffers(psPoolBuffer);
	}

return_error:
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: returning (err = %d)", __FUNCTION__, err));
	}
	/* ignore transport layer bridge to avoid HTB flooding */
	if (psBridgePackageKM->ui32BridgeID != PVRSRV_BRIDGE_PVRTL)
	{
		if (err)
		{
			HTBLOGK(HTB_SF_BRG_BRIDGE_CALL_ERR, ui32Timestamp,
			        psBridgePackageKM->ui32BridgeID,
			        psBridgePackageKM->ui32FunctionID, err);
		}
		else
		{
			HTBLOGK(HTB_SF_BRG_BRIDGE_CALL, ui32Timestamp,
			        psBridgePackageKM->ui32BridgeID,
			        psBridgePackageKM->ui32FunctionID);
		}
	}
	return err;
}
