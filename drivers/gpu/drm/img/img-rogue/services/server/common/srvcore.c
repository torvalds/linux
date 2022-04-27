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

#include "img_defs.h"
#include "img_types_check.h"
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
#include "log2.h"

#include "srvcore.h"
#include "pvrsrv.h"
#include "power.h"

#if defined(SUPPORT_RGX)
#include "rgxdevice.h"
#include "rgxinit.h"
#include "rgx_compat_bvnc.h"
#endif

#include "rgx_options.h"
#include "pvrversion.h"
#include "lock.h"
#include "osfunc.h"
#include "device_connection.h"
#include "process_stats.h"
#include "pvrsrv_pool.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "physmem_lma.h"
#include "services_km.h"
#endif

#include "pvrsrv_tlstreams.h"
#include "tlstream.h"

#if defined(PVRSRV_MISSING_NO_SPEC_IMPL)
#pragma message ("There is no implementation of OSConfineArrayIndexNoSpeculation() - see osfunc.h")
#endif

/* For the purpose of maintainability, it is intended that this file should not
 * contain any OS specific #ifdefs. Please find a way to add e.g.
 * an osfunc.c abstraction or override the entire function in question within
 * env,*,pvr_bridge_k.c
 */

PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT] = { {.pfFunction = DummyBW,} ,};

#define PVR_DISPATCH_OFFSET_FIRST_FUNC			0
#define PVR_DISPATCH_OFFSET_LAST_FUNC			1
#define PVR_DISPATCH_OFFSET_ARRAY_MAX			2

#define PVRSRV_CLIENT_TL_STREAM_SIZE_DEFAULT PVRSRV_APPHINT_HWPERFCLIENTBUFFERSIZE

static IMG_UINT16 g_BridgeDispatchTableStartOffsets[BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT][PVR_DISPATCH_OFFSET_ARRAY_MAX];


#define PVRSRV_MAX_POOLED_BRIDGE_BUFFERS 8	/*!< Initial number of pooled bridge buffers */

static PVRSRV_POOL *g_psBridgeBufferPool;	/*! Pool of bridge buffers */


#if defined(DEBUG_BRIDGE_KM)
/* a lock used for protecting bridge call timing calculations
 * for calls which do not acquire a lock
 */
static POS_LOCK g_hStatsLock;
PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;

void BridgeGlobalStatsLock(void)
{
	OSLockAcquire(g_hStatsLock);
}

void BridgeGlobalStatsUnlock(void)
{
	OSLockRelease(g_hStatsLock);
}
#endif

void BridgeDispatchTableStartOffsetsInit(void)
{
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEFAULT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DEFAULT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DEFAULT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DEFAULT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SRVCORE][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SRVCORE_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SRVCORE][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SRVCORE_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RESERVED1][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RESERVED1_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RESERVED1][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RESERVED1_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RESERVED2][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RESERVED2_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RESERVED2][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RESERVED2_DISPATCH_LAST;
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
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCFALLBACK][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_SYNCFALLBACK][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_SYNCFALLBACK_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DI][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DI_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DI][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DI_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DMA][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_DMA_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_DMA][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_DMA_DISPATCH_LAST;
#if defined(SUPPORT_RGX)
	/* Need a gap here to start next entry at element 128 */
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXCMP][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXCMP_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXCMP][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTA3D][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTA3D_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTA3D][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXBREAKPOINT][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXBREAKPOINT][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXFWDBG][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXFWDBG][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXPDUMP][PVR_DISPATCH_OFFSET_FIRST_FUNC]= PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXPDUMP][PVR_DISPATCH_OFFSET_LAST_FUNC]= PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXHWPERF][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXHWPERF][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXREGCONFIG][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXREGCONFIG][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXKICKSYNC][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXKICKSYNC][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXSIGNALS][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXSIGNALS_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXSIGNALS][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXSIGNALS_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ2][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTQ2_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTQ2][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTQ2_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTIMERQUERY][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXTIMERQUERY][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXRAY][PVR_DISPATCH_OFFSET_FIRST_FUNC] = PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST;
	g_BridgeDispatchTableStartOffsets[PVRSRV_BRIDGE_RGXRAY][PVR_DISPATCH_OFFSET_LAST_FUNC] = PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST;
#endif
}

#if defined(DEBUG_BRIDGE_KM)

#if defined(INTEGRITY_OS)
PVRSRV_ERROR PVRSRVPrintBridgeStats()
{
	IMG_UINT32 ui32Index;
	IMG_UINT32 ui32Remainder;

	printf("Total Bridge call count = %u\n"
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

	/* Is the item asked for (starts at 0) a valid table index? */
	for ( ui32Index=0; ui32Index < BRIDGE_DISPATCH_TABLE_ENTRY_COUNT; ui32Index++ )
	{
		PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry = &g_BridgeDispatchTable[ui32Index];
		printf("%3d: %-60s   %-48s   %-10u   %-20u   %-20u   %-20llu   %-20llu\n",
			   (IMG_UINT32)(((size_t)psEntry-(size_t)g_BridgeDispatchTable)/sizeof(*g_BridgeDispatchTable)),
			   psEntry->pszIOCName,
			   (psEntry->pfFunction != NULL) ? psEntry->pszFunctionName : "(null)",
			   psEntry->ui32CallCount,
			   psEntry->ui32CopyFromUserTotalBytes,
			   psEntry->ui32CopyToUserTotalBytes,
			   (unsigned long long) OSDivide64r64(psEntry->ui64TotalTimeNS, 1000, &ui32Remainder),
			   (unsigned long long) OSDivide64r64(psEntry->ui64MaxTimeNS, 1000, &ui32Remainder));


	}
}
#endif

PVRSRV_ERROR
CopyFromUserWrapper(CONNECTION_DATA *psConnection,
					IMG_UINT32 ui32DispatchTableEntry,
					void *pvDest,
					void __user *pvSrc,
					IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32DispatchTableEntry].ui32CopyFromUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+=ui32Size;
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  void __user *pvDest,
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
					void __user *pvSrc,
					IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER (ui32DispatchTableEntry);
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
INLINE PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  void __user *pvDest,
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
				IMG_UINT64 *ui64PackedBvnc)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32BuildOptions, ui32BuildOptionsMismatch;
	IMG_UINT32			ui32DDKVersion, ui32DDKBuild;
	PVRSRV_DATA			*psSRVData = NULL;
	IMG_UINT64			ui64ProcessVASpaceSize = OSGetCurrentProcessVASpaceSize();
	static IMG_BOOL		bIsFirstConnection=IMG_FALSE;

#if defined(SUPPORT_RGX)
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	/* Gather BVNC information to output to UM */

	*ui64PackedBvnc = rgx_bvnc_pack(psDevInfo->sDevFeatureCfg.ui32B,
	                       psDevInfo->sDevFeatureCfg.ui32V,
	                       psDevInfo->sDevFeatureCfg.ui32N,
	                       psDevInfo->sDevFeatureCfg.ui32C);
#else
	*ui64PackedBvnc = 0;
#endif /* defined(SUPPORT_RGX)*/

	/* Clear the flags */
	*pui32CapabilityFlags = 0;

	psSRVData = PVRSRVGetPVRSRVData();

	psConnection->ui32ClientFlags = ui32Flags;

	/*Set flags to pass back to the client showing which cache coherency is available.*/
	/* Is the system snooping of caches emulated in software? */
	if (PVRSRVSystemSnoopingIsEmulated(psDeviceNode->psDevConfig))
	{
		*pui32CapabilityFlags |= PVRSRV_CACHE_COHERENT_EMULATE_FLAG;
	}
	else
	{
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
	}

	/* Has the system device non-mappable local memory?*/
	if (PVRSRVSystemHasNonMappableLocalMemory(psDeviceNode->psDevConfig))
	{
		*pui32CapabilityFlags |= PVRSRV_NONMAPPABLE_MEMORY_PRESENT_FLAG;
	}

	/* Is system using FBCDC v31? */
	if (psDeviceNode->pfnHasFBCDCVersion31(psDeviceNode))
	{
		*pui32CapabilityFlags |= PVRSRV_FBCDC_V3_1_USED;
	}

	/* Set flags to indicate shared-virtual-memory (SVM) allocation availability */
	if (! psDeviceNode->ui64GeneralSVMHeapTopVA || ! ui64ProcessVASpaceSize)
	{
		*pui32CapabilityFlags |= PVRSRV_DEVMEM_SVM_ALLOC_UNSUPPORTED;
	}
	else
	{
		if (ui64ProcessVASpaceSize <= psDeviceNode->ui64GeneralSVMHeapTopVA)
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

	/* Is the system DMA capable? */
	if (psDeviceNode->bHasSystemDMA)
	{
		*pui32CapabilityFlags |= PVRSRV_SYSTEM_DMA_USED;
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	IMG_UINT32 ui32OSid = 0, ui32OSidReg = 0;
	IMG_BOOL   bOSidAxiProtReg = IMG_FALSE;

	ui32OSid    = (ui32Flags & SRV_VIRTVAL_FLAG_OSID_MASK)    >> (VIRTVAL_FLAG_OSID_SHIFT);
	ui32OSidReg = (ui32Flags & SRV_VIRTVAL_FLAG_OSIDREG_MASK) >> (VIRTVAL_FLAG_OSIDREG_SHIFT);

#if defined(EMULATOR)
{
    /* AXI_ACELITE is only supported on rogue cores - volcanic cores all support full ACE
     * and don't want to compile the code below (RGX_FEATURE_AXI_ACELITE_BIT_MASK is not
     * defined for volcanic cores).
     */

     PVRSRV_RGXDEV_INFO *psDevInfo;
     psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;

#if defined(RGX_FEATURE_AXI_ACELITE_BIT_MASK)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, AXI_ACELITE))
#else
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, AXI_ACE))
#endif
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
}
#endif /* defined(EMULATOR) */

	/* We now know the OSid, OSidReg and bOSidAxiProtReg setting for this
	 * connection. We can access these from wherever we have a connection
	 * reference and do not need to traverse an arbitrary linked-list to
	 * obtain them. The settings are process-specific.
	 */
	psConnection->ui32OSid = ui32OSid;
	psConnection->ui32OSidReg = ui32OSidReg;
	psConnection->bOSidAxiProtReg = bOSidAxiProtReg;

	PVR_DPF((PVR_DBG_MESSAGE,
	         "[GPU Virtualization Validation]: OSIDs: %d, %d",
	         ui32OSid,
	         ui32OSidReg));
}
#endif	/* defined(SUPPORT_GPUVIRT_VALIDATION) */

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	/* Only enabled if enabled in the UM */
	if (!(ui32ClientBuildOptions & RGX_BUILD_OPTIONS_KM & OPTIONS_WORKLOAD_ESTIMATION_MASK))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Workload Estimation disabled. Not enabled in UM",
				__func__));
	}
#endif

#if defined(SUPPORT_PDVFS)
	/* Only enabled if enabled in the UM */
	if (!(ui32ClientBuildOptions & RGX_BUILD_OPTIONS_KM & OPTIONS_PDVFS_MASK))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Proactive DVFS disabled. Not enabled in UM",
		         __func__));
	}
#endif

	ui32DDKVersion = PVRVERSION_PACK(PVRVERSION_MAJ, PVRVERSION_MIN);
	ui32DDKBuild = PVRVERSION_BUILD;

	if (ui32Flags & SRV_FLAGS_CLIENT_64BIT_COMPAT)
	{
		psSRVData->sDriverInfo.ui8UMSupportedArch |= BUILD_ARCH_64BIT;
	}
	else
	{
		psSRVData->sDriverInfo.ui8UMSupportedArch |= BUILD_ARCH_32BIT;
	}

	if (IMG_FALSE == bIsFirstConnection)
	{
		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildOptions = ui32ClientBuildOptions;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildVersion = ui32DDKVersion;
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildVersion = ui32ClientDDKVersion;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildRevision = ui32DDKBuild;
		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildRevision = ui32ClientDDKBuild;

		psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildType =
				((RGX_BUILD_OPTIONS_KM) & OPTIONS_DEBUG_MASK) ? BUILD_TYPE_DEBUG : BUILD_TYPE_RELEASE;

		psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildType =
				(ui32ClientBuildOptions & OPTIONS_DEBUG_MASK) ? BUILD_TYPE_DEBUG : BUILD_TYPE_RELEASE;

		if (sizeof(void *) == POINTER_SIZE_64BIT)
		{
			psSRVData->sDriverInfo.ui8KMBitArch |= BUILD_ARCH_64BIT;
		}
		else
		{
			psSRVData->sDriverInfo.ui8KMBitArch |= BUILD_ARCH_32BIT;
		}
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
		ui32BuildOptionsMismatch &= OPTIONS_STRICT;
#endif
		if ( (ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in client-side driver: (0x%x). Please check rgx_options.h",
				__func__,
				ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH, chk_exit);
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in KM driver: (0x%x). Please check rgx_options.h",
				__func__,
				ui32BuildOptions & ui32BuildOptionsMismatch ));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH, chk_exit);
		}
		if (IMG_FALSE == bIsFirstConnection)
		{
			PVR_LOG(("%s: COMPAT_TEST: Client-side (0x%04x) (%s) and KM driver (0x%04x) (%s) build options differ.",
																			__func__,
																			ui32ClientBuildOptions,
																			(psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildType)?"release":"debug",
																			ui32BuildOptions,
																			(psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildType)?"release":"debug"));
		}else{
			PVR_DPF((PVR_DBG_WARNING, "%s: COMPAT_TEST: Client-side (0x%04x) and KM driver (0x%04x) build options differ.",
																		__func__,
																		ui32ClientBuildOptions,
																		ui32BuildOptions));

		}
		if (!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: Client-side and KM driver build options match. [ OK ]", __func__));
	}

	/*
	 * Validate DDK version
	 */
	if (ui32ClientDDKVersion != ui32DDKVersion)
	{
		if (!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK version (%u.%u) / client DDK version (%u.%u).",
				__func__,
				PVRVERSION_MAJ, PVRVERSION_MIN,
				PVRVERSION_UNPACK_MAJ(ui32ClientDDKVersion),
				PVRVERSION_UNPACK_MIN(ui32ClientDDKVersion)));
		PVR_DBG_BREAK;
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DDK_VERSION_MISMATCH, chk_exit);
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK version (%u.%u) and client DDK version (%u.%u) match. [ OK ]",
				__func__,
				PVRVERSION_MAJ, PVRVERSION_MIN, PVRVERSION_MAJ, PVRVERSION_MIN));
	}

	/* Create stream for every connection except for the special clients
	 * that don't need it e.g.: recipients of HWPerf data. */
	if (!(psConnection->ui32ClientFlags & SRV_NO_HWPERF_CLIENT_STREAM))
	{
		IMG_CHAR acStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
		OSSNPrintf(acStreamName, PRVSRVTL_MAX_STREAM_NAME_SIZE,
		           PVRSRV_TL_HWPERF_HOST_CLIENT_STREAM_FMTSPEC,
		           psDeviceNode->sDevId.i32OsDeviceID,
		           psConnection->pid);

		eError = TLStreamCreate(&psConnection->hClientTLStream,
		                        acStreamName,
		                        PVRSRV_CLIENT_TL_STREAM_SIZE_DEFAULT,
		                        TL_OPMODE_DROP_NEWER |
		                        TL_FLAG_ALLOCATE_ON_FIRST_OPEN,
		                        NULL, NULL, NULL, NULL);
		if (eError != PVRSRV_OK && eError != PVRSRV_ERROR_ALREADY_EXISTS)
		{
			PVR_LOG_ERROR(eError, "TLStreamCreate");
			psConnection->hClientTLStream = NULL;
		}
		else if (eError == PVRSRV_OK)
		{
			/* Set "tlctrl" stream as a notification channel. This channel is
			 * is used to notify recipients about stream open/close (by writer)
			 * actions (and possibly other actions in the future). */
			eError = TLStreamSetNotifStream(psConnection->hClientTLStream,
			                                psSRVData->hTLCtrlStream);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "TLStreamSetNotifStream");
				TLStreamClose(psConnection->hClientTLStream);
				psConnection->hClientTLStream = NULL;
			}
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
		if (!psSRVData->sDriverInfo.bIsNoMatch)
			psSRVData->sDriverInfo.bIsNoMatch = IMG_TRUE;
		PVR_DPF((PVR_DBG_WARNING, "%s: Mismatch in driver DDK revision (%d) / client DDK revision (%d).",
				__func__, ui32DDKBuild, ui32ClientDDKBuild));
#if defined(PVRSRV_STRICT_COMPAT_CHECK)
		PVR_DBG_BREAK;
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DDK_BUILD_MISMATCH, chk_exit);
#endif
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK revision (%d) and client DDK revision (%d) match. [ OK ]",
				__func__, ui32DDKBuild, ui32ClientDDKBuild));
	}

	/* Success so far so is it the PDump client that is connecting? */
	if (ui32Flags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpConnectionNotify();
	}

	PVR_ASSERT(pui8KernelArch != NULL);

	if (psSRVData->sDriverInfo.ui8KMBitArch & BUILD_ARCH_64BIT)
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
		PVR_DPF((PVR_DBG_MESSAGE, "%s: g_BridgeDispatchTableStartOffsets[0-%lu] entries:", __func__, BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT - 1));
		for (ii=0; ii < BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT; ii++)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "g_BridgeDispatchTableStartOffsets[%d]: %u", ii, g_BridgeDispatchTableStartOffsets[ii][PVR_DISPATCH_OFFSET_FIRST_FUNC]));
		}
	}
#endif

#if defined(PDUMP)
	if (!(ui32Flags & SRV_FLAGS_PDUMPCTRL))
	{
		IMG_UINT64 ui64PDumpState = 0;

		PDumpGetStateKM(&ui64PDumpState);
		if (ui64PDumpState & PDUMP_STATE_CONNECTED)
		{
			*pui32CapabilityFlags |= PVRSRV_PDUMP_IS_RECORDING;
		}
	}
#endif

chk_exit:
	return eError;
}

PVRSRV_ERROR
PVRSRVDisconnectKM(void)
{
#if defined(INTEGRITY_OS) && defined(DEBUG_BRIDGE_KM)
	PVRSRVPrintBridgeStats();
#endif
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
	PVR_ASSERT(PVRSRVGetPVRSRVData()->hGlobalEventObject == hGlobalEventObject);

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
	PVR_WARN_IF_ERROR(eError, "pfnDeviceClockSpeed");

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
	PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX, NULL, NULL);
	return PVRSRV_OK;
}


IMG_INT
DummyBW(IMG_UINT32 ui32DispatchTableEntry,
		IMG_UINT8 *psBridgeIn,
		IMG_UINT8 *psBridgeOut,
		CONNECTION_DATA *psConnection)
{
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psBridgeOut);
	PVR_UNREFERENCED_PARAMETER(psConnection);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: ui32DispatchTableEntry %u (%s) mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __func__, ui32DispatchTableEntry, g_BridgeDispatchTable[ui32DispatchTableEntry].pszIOCName));
#else
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: ui32DispatchTableEntry %u mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __func__, ui32DispatchTableEntry));
#endif
	return PVRSRV_ERROR_BRIDGE_ENOTTY;
}

PVRSRV_ERROR PVRSRVAlignmentCheckKM(CONNECTION_DATA *psConnection,
                                    PVRSRV_DEVICE_NODE *psDeviceNode,
                                    IMG_UINT32 ui32AlignChecksSize,
                                    IMG_UINT32 aui32AlignChecks[])
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

#if !defined(NO_HARDWARE)

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
			PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetDeviceStatusKM: Failed to "
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
		case PVRSRV_DEVICE_HEALTH_STATUS_FAULT:
		case PVRSRV_DEVICE_HEALTH_STATUS_UNDEFINED:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_DEVICE_ERROR;
			return PVRSRV_OK;
		default:
			*pui32DeviceStatus = PVRSRV_DEVICE_STATUS_UNKNOWN;
			return PVRSRV_ERROR_INTERNAL_ERROR;
	}
}

PVRSRV_ERROR PVRSRVGetMultiCoreInfoKM(CONNECTION_DATA *psConnection,
                                      PVRSRV_DEVICE_NODE *psDeviceNode,
                                      IMG_UINT32 ui32CapsSize,
                                      IMG_UINT32 *pui32NumCores,
                                      IMG_UINT64 *pui64Caps)
{
	PVRSRV_ERROR eError = PVRSRV_ERROR_NOT_SUPPORTED;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	if (psDeviceNode->pfnGetMultiCoreInfo != NULL)
	{
		eError = psDeviceNode->pfnGetMultiCoreInfo(psDeviceNode, ui32CapsSize, pui32NumCores, pui64Caps);
	}
	return eError;
}


/*!
 * *****************************************************************************
 * @brief A wrapper for removing entries in the g_BridgeDispatchTable array.
 *		  All this does is zero the entry to allow for a full table re-population
 *		  later.
 *
 * @param ui32BridgeGroup
 * @param ui32Index
 *
 * @return
 ********************************************************************************/
void
UnsetDispatchTableEntry(IMG_UINT32 ui32BridgeGroup, IMG_UINT32 ui32Index)
{
	ui32Index += g_BridgeDispatchTableStartOffsets[ui32BridgeGroup][PVR_DISPATCH_OFFSET_FIRST_FUNC];

	g_BridgeDispatchTable[ui32Index].pfFunction = NULL;
	g_BridgeDispatchTable[ui32Index].hBridgeLock = NULL;
#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32Index].pszIOCName = NULL;
	g_BridgeDispatchTable[ui32Index].pszFunctionName = NULL;
	g_BridgeDispatchTable[ui32Index].pszBridgeLockName = NULL;
	g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
	g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
	g_BridgeDispatchTable[ui32Index].ui64TotalTimeNS = 0;
	g_BridgeDispatchTable[ui32Index].ui64MaxTimeNS = 0;
#endif
}

/*!
 * *****************************************************************************
 * @brief A wrapper for filling in the g_BridgeDispatchTable array that does
 *		  error checking.
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
					   const IMG_CHAR *pszBridgeLockName)
{
	static IMG_UINT32 ui32PrevIndex = IMG_UINT32_MAX;		/* -1 */

#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
	PVR_UNREFERENCED_PARAMETER(pszBridgeLockName);
#endif

	ui32Index += g_BridgeDispatchTableStartOffsets[ui32BridgeGroup][PVR_DISPATCH_OFFSET_FIRST_FUNC];

#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
	/* Enable this to dump out the dispatch table entries */
	PVR_DPF((PVR_DBG_WARNING, "%s: g_BridgeDispatchTableStartOffsets[%d]=%d", __func__, ui32BridgeGroup, g_BridgeDispatchTableStartOffsets[ui32BridgeGroup][PVR_DISPATCH_OFFSET_FIRST_FUNC]));
	PVR_DPF((PVR_DBG_WARNING, "%s: %d %s %s %s", __func__, ui32Index, pszIOCName, pszFunctionName, pszBridgeLockName));
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
	if ((ui32PrevIndex != IMG_UINT32_MAX) &&
		((ui32Index >= ui32PrevIndex + DISPATCH_TABLE_GAP_THRESHOLD) ||
		 (ui32Index <= ui32PrevIndex)))
	{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: There is a gap in the dispatch table between indices %u (%s) and %u (%s)",
				 __func__, ui32PrevIndex, g_BridgeDispatchTable[ui32PrevIndex].pszIOCName,
				 ui32Index, pszIOCName));
#else
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: There is a gap in the dispatch table between indices %u and %u (%s)",
				 __func__, (IMG_UINT)ui32PrevIndex, (IMG_UINT)ui32Index, pszIOCName));
#endif
	}

	if (ui32Index >= BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Index %u (%s) out of range",
				 __func__, (IMG_UINT)ui32Index, pszIOCName));

#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE_DISPATCH_TABLE_ENTRY_COUNT = %lu",
				 __func__, BRIDGE_DISPATCH_TABLE_ENTRY_COUNT));
#if defined(SUPPORT_RGX)
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST));

		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGX_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGX_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_RGX_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_RGX_LAST));
#endif
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_TUTILS_DISPATCH_LAST));
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST = %lu",
				 __func__, PVRSRV_BRIDGE_VALIDATION_DISPATCH_LAST));
#endif

		OSPanic();
	}

	/* Panic if the previous entry has been overwritten as this is not allowed!
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likely to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at a performance critical time.
	 */
	if (g_BridgeDispatchTable[ui32Index].pfFunction)
	{
		if (g_BridgeDispatchTable[ui32Index].pfFunction != pfFunction)
		{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: Adding dispatch table entry for %s clobbers an existing entry for %s (current pfn=<%p>, new pfn=<%p>)",
				 __func__, pszIOCName, g_BridgeDispatchTable[ui32Index].pszIOCName),
				 (void*)g_BridgeDispatchTable[ui32Index].pfFunction, (void*)pfFunction));
#else
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: Adding dispatch table entry for %s clobbers an existing entry (index=%u). (current pfn=<%p>, new pfn=<%p>)",
				 __func__, pszIOCName, ui32Index,
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

static PVRSRV_ERROR _BridgeBufferAlloc(void *pvPrivData, void **pvOut)
{
	PVR_UNREFERENCED_PARAMETER(pvPrivData);

	*pvOut = OSAllocZMem(PVRSRV_MAX_BRIDGE_IN_SIZE +
	                     PVRSRV_MAX_BRIDGE_OUT_SIZE);
	PVR_RETURN_IF_NOMEM(*pvOut);

	return PVRSRV_OK;
}

static void _BridgeBufferFree(void *pvPrivData, void *pvFreeData)
{
	PVR_UNREFERENCED_PARAMETER(pvPrivData);

	OSFreeMem(pvFreeData);
}

PVRSRV_ERROR BridgeDispatcherInit(void)
{
	PVRSRV_ERROR eError;

#if defined(DEBUG_BRIDGE_KM)
	eError = OSLockCreate(&g_hStatsLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", errorLockCreateFailed);
#endif

	eError = PVRSRVPoolCreate(_BridgeBufferAlloc,
	                          _BridgeBufferFree,
	                          PVRSRV_MAX_POOLED_BRIDGE_BUFFERS,
	                          "Bridge buffer pool",
	                          NULL,
	                          &g_psBridgeBufferPool);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVPoolCreate", erroPoolCreateFailed);

	return PVRSRV_OK;

erroPoolCreateFailed:
#if defined(DEBUG_BRIDGE_KM)
	OSLockDestroy(g_hStatsLock);
	g_hStatsLock = NULL;
errorLockCreateFailed:
#endif
	return eError;
}

void BridgeDispatcherDeinit(void)
{
	if (g_psBridgeBufferPool)
	{
		PVRSRVPoolDestroy(g_psBridgeBufferPool);
		g_psBridgeBufferPool = NULL;
	}

#if defined(DEBUG_BRIDGE_KM)
	if (g_hStatsLock)
	{
		OSLockDestroy(g_hStatsLock);
		g_hStatsLock = NULL;
	}
#endif
}

PVRSRV_ERROR BridgedDispatchKM(CONNECTION_DATA * psConnection,
                          PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM)
{

	void       * psBridgeIn=NULL;
	void       * psBridgeOut=NULL;
	BridgeWrapperFunction pfBridgeHandler;
	IMG_UINT32   ui32DispatchTableEntry, ui32GroupBoundary;
	PVRSRV_ERROR err = PVRSRV_OK;
	PVRSRV_POOL_TOKEN hBridgeBufferPoolToken = NULL;
	IMG_UINT32 ui32Timestamp = OSClockus();
#if defined(DEBUG_BRIDGE_KM)
	IMG_UINT64	ui64TimeStart;
	IMG_UINT64	ui64TimeEnd;
	IMG_UINT64	ui64TimeDiff;
#endif
	IMG_UINT32	ui32DispatchTableIndex, ui32DispatchTableEntryIndex;

#if defined(DEBUG_BRIDGE_KM_STOP_AT_DISPATCH)
	PVR_DBG_BREAK;
#endif

	if (psBridgePackageKM->ui32BridgeID >= BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Out of range dispatch table group ID: %d",
		        __func__, psBridgePackageKM->ui32BridgeID));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_EINVAL, return_error);
	}

	ui32DispatchTableIndex = OSConfineArrayIndexNoSpeculation(psBridgePackageKM->ui32BridgeID, BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT);

	ui32DispatchTableEntry = g_BridgeDispatchTableStartOffsets[ui32DispatchTableIndex][PVR_DISPATCH_OFFSET_FIRST_FUNC];
	ui32GroupBoundary = g_BridgeDispatchTableStartOffsets[ui32DispatchTableIndex][PVR_DISPATCH_OFFSET_LAST_FUNC];

	/* bridge function is not implemented in this build */
	if (0 == ui32DispatchTableEntry)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Dispatch table entry=%d, boundary = %d, (bridge module %d, function %d)",
		         __func__,
		         ui32DispatchTableEntry,
		         ui32GroupBoundary,
		         psBridgePackageKM->ui32BridgeID,
		         psBridgePackageKM->ui32FunctionID));
		/* this points to DummyBW() which returns PVRSRV_ERROR_ENOTTY */
		err = g_BridgeDispatchTable[ui32DispatchTableEntry].pfFunction(ui32DispatchTableEntry,
				  psBridgeIn,
				  psBridgeOut,
				  psConnection);
		goto return_error;
	}
	if ((ui32DispatchTableEntry + psBridgePackageKM->ui32FunctionID) > ui32GroupBoundary)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Dispatch table entry=%d, boundary = %d, (bridge module %d, function %d)",
		         __func__,
		         ui32DispatchTableEntry,
		         ui32GroupBoundary,
		         psBridgePackageKM->ui32BridgeID,
		         psBridgePackageKM->ui32FunctionID));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_EINVAL, return_error);
	}
	ui32DispatchTableEntry += psBridgePackageKM->ui32FunctionID;
	ui32DispatchTableEntryIndex = OSConfineArrayIndexNoSpeculation(ui32DispatchTableEntry, ui32GroupBoundary+1);
	if (BRIDGE_DISPATCH_TABLE_ENTRY_COUNT <= ui32DispatchTableEntry)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Dispatch table entry=%d, entry count = %lu,"
		        " (bridge module %d, function %d)", __func__,
		        ui32DispatchTableEntry, BRIDGE_DISPATCH_TABLE_ENTRY_COUNT,
		        psBridgePackageKM->ui32BridgeID,
		        psBridgePackageKM->ui32FunctionID));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_EINVAL, return_error);
	}
#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "%s: Dispatch table entry index=%d, (bridge module %d, function %d)",
			__func__,
			ui32DispatchTableEntryIndex, psBridgePackageKM->ui32BridgeID, psBridgePackageKM->ui32FunctionID));
	PVR_DPF((PVR_DBG_MESSAGE, "%s: %s",
			 __func__,
			 g_BridgeDispatchTable[ui32DispatchTableEntryIndex].pszIOCName));
	g_BridgeDispatchTable[ui32DispatchTableEntryIndex].ui32CallCount++;
	g_BridgeGlobalStats.ui32IOCTLCount++;
#endif

	if (g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock != NULL)
	{
		OSLockAcquire(g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock);
	}
#if !defined(INTEGRITY_OS)
	/* try to acquire a bridge buffer from the pool */

	err = PVRSRVPoolGet(g_psBridgeBufferPool,
			&hBridgeBufferPoolToken,
			&psBridgeIn);
	PVR_LOG_GOTO_IF_ERROR(err, "PVRSRVPoolGet", unlock_and_return_error);

	psBridgeOut = ((IMG_BYTE *) psBridgeIn) + PVRSRV_MAX_BRIDGE_IN_SIZE;
#endif

#if defined(DEBUG_BRIDGE_KM)
	ui64TimeStart = OSClockns64();
#endif

	if (psBridgePackageKM->ui32InBufferSize > PVRSRV_MAX_BRIDGE_IN_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge input buffer too small "
		        "(data size %u, buffer size %u)!", __func__,
		        psBridgePackageKM->ui32InBufferSize, PVRSRV_MAX_BRIDGE_IN_SIZE));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_ERANGE, unlock_and_return_error);
	}

#if !defined(INTEGRITY_OS)
	if (psBridgePackageKM->ui32OutBufferSize > PVRSRV_MAX_BRIDGE_OUT_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge output buffer too small "
		        "(data size %u, buffer size %u)!", __func__,
		        psBridgePackageKM->ui32OutBufferSize, PVRSRV_MAX_BRIDGE_OUT_SIZE));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_ERANGE, unlock_and_return_error);
	}

	if ((CopyFromUserWrapper (psConnection,
							  ui32DispatchTableEntryIndex,
							  psBridgeIn,
							  psBridgePackageKM->pvParamIn,
							  psBridgePackageKM->ui32InBufferSize) != PVRSRV_OK)
#if defined(__QNXNTO__)
/* For Neutrino, the output bridge buffer acts as an input as well */
					|| (CopyFromUserWrapper(psConnection,
											ui32DispatchTableEntryIndex,
											psBridgeOut,
											(void *)((uintptr_t)psBridgePackageKM->pvParamIn + psBridgePackageKM->ui32InBufferSize),
											psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
#endif
		) /* end of if-condition */
	{
		PVR_LOG_GOTO_WITH_ERROR("CopyFromUserWrapper", err, PVRSRV_ERROR_BRIDGE_EFAULT, unlock_and_return_error);
	}
#else
	psBridgeIn = psBridgePackageKM->pvParamIn;
	psBridgeOut = psBridgePackageKM->pvParamOut;
#endif

	pfBridgeHandler =
		(BridgeWrapperFunction)g_BridgeDispatchTable[ui32DispatchTableEntryIndex].pfFunction;

	if (pfBridgeHandler == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32DispatchTableEntry = %d is not a registered function!",
				 __func__, ui32DispatchTableEntry));
		PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_EFAULT, unlock_and_return_error);
	}

	/* pfBridgeHandler functions do not fail and return an IMG_INT.
	 * The value returned is either 0 or PVRSRV_OK (0).
	 * In the event this changes an error may be +ve or -ve,
	 * so try to return something consistent here.
	 */
	if (0 != pfBridgeHandler(ui32DispatchTableEntryIndex,
						  psBridgeIn,
						  psBridgeOut,
						  psConnection)
		)
	{
		PVR_LOG_GOTO_WITH_ERROR("pfBridgeHandler", err, PVRSRV_ERROR_BRIDGE_EPERM, unlock_and_return_error);
	}

	/*
	   This should always be true as a.t.m. all bridge calls have to
	   return an error message, but this could change so we do this
	   check to be safe.
	*/
#if !defined(INTEGRITY_OS)
	if (psBridgePackageKM->ui32OutBufferSize > 0)
	{
		if (CopyToUserWrapper (psConnection,
						ui32DispatchTableEntryIndex,
						psBridgePackageKM->pvParamOut,
						psBridgeOut,
						psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
		{
			PVR_GOTO_WITH_ERROR(err, PVRSRV_ERROR_BRIDGE_EFAULT, unlock_and_return_error);
		}
	}
#endif

#if defined(DEBUG_BRIDGE_KM)
	ui64TimeEnd = OSClockns64();

	ui64TimeDiff = ui64TimeEnd - ui64TimeStart;

	/* if there is no lock held then acquire the stats lock to
	 * ensure the calculations are done safely
	 */
	if (g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock == NULL)
	{
		BridgeGlobalStatsLock();
	}

	g_BridgeDispatchTable[ui32DispatchTableEntryIndex].ui64TotalTimeNS += ui64TimeDiff;

	if (ui64TimeDiff > g_BridgeDispatchTable[ui32DispatchTableEntryIndex].ui64MaxTimeNS)
	{
		g_BridgeDispatchTable[ui32DispatchTableEntryIndex].ui64MaxTimeNS = ui64TimeDiff;
	}

	if (g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock == NULL)
	{
		BridgeGlobalStatsUnlock();
	}
#endif

unlock_and_return_error:

	if (g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock != NULL)
	{
		OSLockRelease(g_BridgeDispatchTable[ui32DispatchTableEntryIndex].hBridgeLock);
	}

#if !defined(INTEGRITY_OS)
	if (hBridgeBufferPoolToken != NULL)
	{
		err = PVRSRVPoolPut(g_psBridgeBufferPool,
				hBridgeBufferPoolToken);
		PVR_LOG_IF_ERROR(err, "PVRSRVPoolPut");
	}
#endif

return_error:
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: returning (err = %d)", __func__, err));
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

PVRSRV_ERROR PVRSRVFindProcessMemStatsKM(IMG_PID pid, IMG_UINT32 ui32ArrSize, IMG_BOOL bAllProcessStats, IMG_UINT32 *pui32MemStatArray)
{
#if !defined(__QNXNTO__)
	return PVRSRVFindProcessMemStats(pid,
					ui32ArrSize,
					bAllProcessStats,
					pui32MemStatArray);
#else
	PVR_DPF((PVR_DBG_ERROR, "This functionality is not yet implemented for this platform"));

	return PVRSRV_ERROR_NOT_SUPPORTED;
#endif

}
