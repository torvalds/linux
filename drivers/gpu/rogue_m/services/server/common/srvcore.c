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

#include "pdump_km.h"

#include "srvkm.h"
#include "allocmem.h"
#include "devicemem.h"

#include "srvcore.h"
#include "pvrsrv.h"
#include "power.h"
#include "lists.h"

#include "rgx_options_km.h"
#include "pvrversion.h"
#include "lock.h"
#include "osfunc.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "physmem_lma.h"
#include "services.h"
#endif

/* For the purpose of maintainability, it is intended that this file should not
 * contain any OS specific #ifdefs. Please find a way to add e.g.
 * an osfunc.c abstraction or override the entire function in question within
 * env,*,pvr_bridge_k.c
 */

PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];

static IMG_UINT16 g_BridgeDispatchTableStartOffsets[BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT] =
{
		[PVRSRV_BRIDGE_SRVCORE] = PVRSRV_BRIDGE_SRVCORE_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_SYNC] = PVRSRV_BRIDGE_SYNC_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_SYNCEXPORT] = PVRSRV_BRIDGE_SYNCEXPORT_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_SYNCSEXPORT] = PVRSRV_BRIDGE_SYNCSEXPORT_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_PDUMPCTRL] = PVRSRV_BRIDGE_PDUMPCTRL_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_MM] = PVRSRV_BRIDGE_MM_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_MMPLAT] = PVRSRV_BRIDGE_MMPLAT_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_CMM] = PVRSRV_BRIDGE_CMM_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_PDUMPMM] = PVRSRV_BRIDGE_PDUMPMM_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_PDUMP] = PVRSRV_BRIDGE_PDUMP_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_DMABUF] = PVRSRV_BRIDGE_DMABUF_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_DC] = PVRSRV_BRIDGE_DC_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_CACHEGENERIC] = PVRSRV_BRIDGE_CACHEGENERIC_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_SMM] = PVRSRV_BRIDGE_SMM_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_PVRTL] = PVRSRV_BRIDGE_PVRTL_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RI] = PVRSRV_BRIDGE_RI_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_VALIDATION] = PVRSRV_BRIDGE_VALIDATION_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_TUTILS] = PVRSRV_BRIDGE_TUTILS_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_DEVICEMEMHISTORY] = PVRSRV_BRIDGE_DEVICEMEMHISTORY_DISPATCH_FIRST,
#if defined(SUPPORT_RGX)
		/* Need a gap here to start next entry at element 150 */
		[PVRSRV_BRIDGE_RGXTQ] = PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXCMP] = PVRSRV_BRIDGE_RGXCMP_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXINIT] = PVRSRV_BRIDGE_RGXINIT_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXTA3D] = PVRSRV_BRIDGE_RGXTA3D_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_BREAKPOINT] = PVRSRV_BRIDGE_BREAKPOINT_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_DEBUGMISC] = PVRSRV_BRIDGE_DEBUGMISC_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXPDUMP] = PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXHWPERF] = PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_RGXRAY] = PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_REGCONFIG] = PVRSRV_BRIDGE_REGCONFIG_DISPATCH_FIRST,
		[PVRSRV_BRIDGE_TIMERQUERY] = PVRSRV_BRIDGE_TIMERQUERY_DISPATCH_FIRST,
#endif
};


#if defined(DEBUG_BRIDGE_KM)
PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif


#if defined(DEBUG_BRIDGE_KM)
PVRSRV_ERROR
CopyFromUserWrapper(CONNECTION_DATA *psConnection,
					IMG_UINT32 ui32DispatchTableEntry,
					IMG_VOID *pvDest,
					IMG_VOID *pvSrc,
					IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32DispatchTableEntry].ui32CopyFromUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+=ui32Size;
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  IMG_VOID *pvDest,
				  IMG_VOID *pvSrc,
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
					IMG_VOID *pvDest,
					IMG_VOID *pvSrc,
					IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER (ui32DispatchTableEntry);
	return OSBridgeCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
INLINE PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32DispatchTableEntry,
				  IMG_VOID *pvDest,
				  IMG_VOID *pvSrc,
				  IMG_UINT32 ui32Size)
{
	PVR_UNREFERENCED_PARAMETER (ui32DispatchTableEntry);
	return OSBridgeCopyToUser(psConnection, pvDest, pvSrc, ui32Size);
}
#endif

PVRSRV_ERROR
PVRSRVConnectKM(CONNECTION_DATA *psConnection,
				IMG_UINT32 ui32Flags,
				IMG_UINT32 ui32ClientBuildOptions,
				IMG_UINT32 ui32ClientDDKVersion,
				IMG_UINT32 ui32ClientDDKBuild,
				IMG_UINT8  *pui8KernelArch,
				IMG_UINT32 *ui32Log2PageSize)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32BuildOptions, ui32BuildOptionsMismatch;
	IMG_UINT32			ui32DDKVersion, ui32DDKBuild;
	
	*ui32Log2PageSize = GET_LOG2_PAGESIZE();

	psConnection->ui32ClientFlags = ui32Flags;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	IMG_UINT32	ui32OSid = 0, ui32OSidReg = 0;

	IMG_PID pIDCurrent = OSGetCurrentProcessID();

	ui32OSid    = (ui32Flags & (OSID_BITS_FLAGS_MASK<<(OSID_BITS_FLAGS_OFFSET  ))) >> (OSID_BITS_FLAGS_OFFSET);
	ui32OSidReg = (ui32Flags & (OSID_BITS_FLAGS_MASK<<(OSID_BITS_FLAGS_OFFSET+3))) >> (OSID_BITS_FLAGS_OFFSET+3);

	InsertPidOSidsCoupling(pIDCurrent, ui32OSid, ui32OSidReg);

	PVR_DPF((PVR_DBG_MESSAGE,"[GPU Virtualization Validation]: OSIDs: %d, %d\n",ui32OSid, ui32OSidReg));
}
#endif

	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	if(ui32Flags & SRV_FLAGS_INIT_PROCESS)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Connecting as init process", __func__));
		if ((OSProcHasPrivSrvInit() == IMG_FALSE) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Rejecting init process", __func__));
			eError = PVRSRV_ERROR_SRV_CONNECT_FAILED;
			goto chk_exit;
		}
#if defined (__linux__)
		PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_TRUE);
#endif
	}
	else
	{
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
	ui32ClientBuildOptions &= RGX_BUILD_OPTIONS_MASK_KM;
	/*
	 * Validate the build options
	 */
	ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);
	if (ui32BuildOptions != ui32ClientBuildOptions)
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ ui32ClientBuildOptions;
		if ( (ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in client-side driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in KM driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32BuildOptions & ui32BuildOptionsMismatch ));
		}
		eError = PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: Client-side and KM driver build options match. [ OK ]", __FUNCTION__));
	}

	/*
	 * Validate DDK version
	 */
	ui32DDKVersion = PVRVERSION_PACK(PVRVERSION_MAJ, PVRVERSION_MIN);
	if (ui32ClientDDKVersion != ui32DDKVersion)
	{
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK revision (%u.%u) / client DDK revision (%u.%u).",
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
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK revision (%u.%u) and client DDK revision (%u.%u) match. [ OK ]",
				__FUNCTION__,
				PVRVERSION_MAJ, PVRVERSION_MIN, PVRVERSION_MAJ, PVRVERSION_MIN));
	}
	
	/*
	 * Validate DDK build
	 */
	ui32DDKBuild = PVRVERSION_BUILD;
	if (ui32ClientDDKBuild != ui32DDKBuild)
	{
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK build (%d) / client DDK build (%d).",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
		eError = PVRSRV_ERROR_DDK_BUILD_MISMATCH;
		PVR_DBG_BREAK;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK build (%d) and client DDK build (%d) match. [ OK ]",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
	}

	/* Success so far so is it the PDump client that is connecting? */
	if (ui32Flags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpConnectionNotify();
	}

	PVR_ASSERT(pui8KernelArch != NULL);
	/* Can't use __SIZEOF_POINTER__ here as it is not defined on Windows */
	if (sizeof(IMG_PVOID) == 8)
	{
		*pui8KernelArch = 64;
	}
	else
	{
		*pui8KernelArch = 32;
	}


#if defined(DEBUG_BRIDGE_KM)
	{
		int ii;

		/* dump dispatch table offset lookup table */
		PVR_DPF((PVR_DBG_MESSAGE, "%s: g_BridgeDispatchTableStartOffsets[0-%lu] entries:", __FUNCTION__, BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT - 1));
		for (ii=0; ii < BRIDGE_DISPATCH_TABLE_START_ENTRY_COUNT; ii++)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "g_BridgeDispatchTableStartOffsets[%d]: %u", ii, g_BridgeDispatchTableStartOffsets[ii]));
		}
	}
#endif

chk_exit:
	return eError;
}

PVRSRV_ERROR
PVRSRVDisconnectKM(IMG_VOID)
{
	/* just return OK, per-process data is cleaned up by resmgr */

	return PVRSRV_OK;
}

/*
	PVRSRVDumpDebugInfoKM
*/
PVRSRV_ERROR
PVRSRVDumpDebugInfoKM(IMG_UINT32 ui32VerbLevel)
{
	if (ui32VerbLevel > DEBUG_REQUEST_VERBOSITY_MAX)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	PVR_LOG(("User requested PVR debug info"));

	PVRSRVDebugRequest(ui32VerbLevel, IMG_NULL);
									   
	return PVRSRV_OK;
}

/*
	PVRSRVGetDevClockSpeedKM
*/
PVRSRV_ERROR
PVRSRVGetDevClockSpeedKM(PVRSRV_DEVICE_NODE *psDeviceNode,
						 IMG_PUINT32  pui32RGXClockSpeed)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_ASSERT(psDeviceNode->pfnDeviceClockSpeed != IMG_NULL);

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
PVRSRVHWOpTimeoutKM(IMG_VOID)
{
#if defined(PVRSRV_RESET_ON_HWTIMEOUT)
	PVR_LOG(("User requested OS reset"));
	OSPanic();
#endif
	PVR_LOG(("HW operation timeout, dump server info"));
	PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_LOW,IMG_NULL);
	return PVRSRV_OK;
}

IMG_INT
DummyBW(IMG_UINT32 ui32DispatchTableEntry,
		IMG_VOID *psBridgeIn,
		IMG_VOID *psBridgeOut,
		CONNECTION_DATA *psConnection)
{
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(ui32DispatchTableEntry);
#endif
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
	return -ENOTTY;
}


/*
	PVRSRVSoftResetKM
*/
PVRSRV_ERROR
PVRSRVSoftResetKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                  IMG_UINT64 ui64ResetValue1,
                  IMG_UINT64 ui64ResetValue2)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if ((psDeviceNode == IMG_NULL) || (psDeviceNode->pfnSoftReset == IMG_NULL))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = psDeviceNode->pfnSoftReset(psDeviceNode, ui64ResetValue1, ui64ResetValue2);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVSoftResetKM: "
				"Failed to soft reset (error %d)",
				eError));
	}

	return eError;
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
IMG_VOID
_SetDispatchTableEntry(IMG_UINT32 ui32BridgeGroup,
					   IMG_UINT32 ui32Index,
					   const IMG_CHAR *pszIOCName,
					   BridgeWrapperFunction pfFunction,
					   const IMG_CHAR *pszFunctionName,
					   POS_LOCK hBridgeLock,
					   const IMG_CHAR *pszBridgeLockName,
					   IMG_BYTE* pbyBridgeBuffer,
					   IMG_UINT32 ui32BridgeInBufferSize,
					   IMG_UINT32 ui32BridgeOutBufferSize)
{
	static IMG_UINT32 ui32PrevIndex = IMG_UINT32_MAX;		/* -1 */
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(pszIOCName);
#endif
#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
	PVR_UNREFERENCED_PARAMETER(pszBridgeLockName);
#endif

	ui32Index += g_BridgeDispatchTableStartOffsets[ui32BridgeGroup];

#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
	/* Enable this to dump out the dispatch table entries */
	PVR_DPF((PVR_DBG_WARNING, "%s: g_BridgeDispatchTableStartOffsets[%d]=%d", __FUNCTION__, ui32BridgeGroup, g_BridgeDispatchTableStartOffsets[ui32BridgeGroup]));
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
	 * mistakes are exposed. This isn't run at at a performance critical time.
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
	 * mistakes are exposed. This isn't run at at a performance critical time.
	 */
	if(g_BridgeDispatchTable[ui32Index].pfFunction)
	{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry for %s",
				 __FUNCTION__, pszIOCName, g_BridgeDispatchTable[ui32Index].pszIOCName));
#else
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry (index=%u)",
				 __FUNCTION__, pszIOCName, ui32Index));
		PVR_DPF((PVR_DBG_ERROR, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
#endif
		OSPanic();
	}

	g_BridgeDispatchTable[ui32Index].pfFunction = pfFunction;
	g_BridgeDispatchTable[ui32Index].hBridgeLock = hBridgeLock;
	g_BridgeDispatchTable[ui32Index].pvBridgeBuffer = (IMG_PVOID) pbyBridgeBuffer;
	g_BridgeDispatchTable[ui32Index].ui32BridgeInBufferSize = ui32BridgeInBufferSize;
	g_BridgeDispatchTable[ui32Index].ui32BridgeOutBufferSize = ui32BridgeOutBufferSize;
#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32Index].pszIOCName = pszIOCName;
	g_BridgeDispatchTable[ui32Index].pszFunctionName = pszFunctionName;
	g_BridgeDispatchTable[ui32Index].pszBridgeLockName = pszBridgeLockName;
	g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
	g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
#endif

	ui32PrevIndex = ui32Index;
}

PVRSRV_ERROR
PVRSRVInitSrvDisconnectKM(CONNECTION_DATA *psConnection,
							IMG_BOOL bInitSuccesful,
							IMG_UINT32 ui32ClientBuildOptions)
{
	PVRSRV_ERROR eError;

	if(!(psConnection->ui32ClientFlags & SRV_FLAGS_INIT_PROCESS))
	{
		return PVRSRV_ERROR_SRV_DISCONNECT_FAILED;
	}

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_FALSE);
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RAN, IMG_TRUE);

	eError = PVRSRVFinaliseSystem(bInitSuccesful, ui32ClientBuildOptions);

	PVRSRVSetInitServerState( PVRSRV_INIT_SERVER_SUCCESSFUL ,
				(eError == PVRSRV_OK) && bInitSuccesful);

	return eError;
}

IMG_INT BridgedDispatchKM(CONNECTION_DATA * psConnection,
					  PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM)
{

	IMG_VOID   * psBridgeIn;
	IMG_VOID   * psBridgeOut;
	BridgeWrapperFunction pfBridgeHandler;
	IMG_UINT32   ui32DispatchTableEntry;
	IMG_INT      err          = -EFAULT;
	IMG_UINT32	ui32BridgeInBufferSize;
	IMG_UINT32	ui32BridgeOutBufferSize;

#if defined(DEBUG_BRIDGE_KM_STOP_AT_DISPATCH)
	PVR_DBG_BREAK;
#endif

	ui32DispatchTableEntry = g_BridgeDispatchTableStartOffsets[psBridgePackageKM->ui32BridgeID] + psBridgePackageKM->ui32FunctionID;

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

	if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock)
	{
		/* Acquire module specific bridge lock */
		OSLockAcquire(g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock);
		
		/* Use buffers which are allocated for this bridge module */
		psBridgeIn = g_BridgeDispatchTable[ui32DispatchTableEntry].pvBridgeBuffer;
		ui32BridgeInBufferSize = g_BridgeDispatchTable[ui32DispatchTableEntry].ui32BridgeInBufferSize;
		psBridgeOut = (IMG_PVOID)((IMG_PBYTE)psBridgeIn + ui32BridgeInBufferSize);
		ui32BridgeOutBufferSize = g_BridgeDispatchTable[ui32DispatchTableEntry].ui32BridgeOutBufferSize;
	}
	else
	{
		/* Acquire default global bridge lock if calling module has no independent lock */
		OSAcquireBridgeLock();

		/* Request for global bridge buffers */
		OSGetGlobalBridgeBuffers(&psBridgeIn,
					&ui32BridgeInBufferSize,
					&psBridgeOut,
					&ui32BridgeOutBufferSize);
	}

	if (psBridgePackageKM->ui32InBufferSize > ui32BridgeInBufferSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge input buffer too small "
		        "(data size %u, buffer size %u)!", __FUNCTION__,
		        psBridgePackageKM->ui32InBufferSize, ui32BridgeInBufferSize));
		err = PVRSRV_ERROR_BUFFER_TOO_SMALL;
		goto unlock_and_return_fault;
	}

	if (psBridgePackageKM->ui32OutBufferSize > ui32BridgeOutBufferSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Bridge output buffer too small "
		        "(data size %u, buffer size %u)!", __FUNCTION__,
		        psBridgePackageKM->ui32OutBufferSize, ui32BridgeOutBufferSize));
		err = PVRSRV_ERROR_BUFFER_TOO_SMALL;
		goto unlock_and_return_fault;
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
											(IMG_PVOID)((IMG_UINT32)psBridgePackageKM->pvParamIn + psBridgePackageKM->ui32InBufferSize),
											psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
#endif
		) /* end of if-condition */
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: CopyFromUserWrapper returned an error!", __FUNCTION__));
		goto unlock_and_return_fault;
	}

	if(ui32DispatchTableEntry > (BRIDGE_DISPATCH_TABLE_ENTRY_COUNT))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32DispatchTableEntry = %d is out if range!",
				 __FUNCTION__, ui32DispatchTableEntry));
		goto unlock_and_return_fault;
	}
	pfBridgeHandler =
		(BridgeWrapperFunction)g_BridgeDispatchTable[ui32DispatchTableEntry].pfFunction;
	
	if (pfBridgeHandler == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32DispatchTableEntry = %d is not a registered function!",
				 __FUNCTION__, ui32DispatchTableEntry));
		goto unlock_and_return_fault;
	}
	
	err = pfBridgeHandler(ui32DispatchTableEntry,
						  psBridgeIn,
						  psBridgeOut,
						  psConnection);
	if(err < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ...done (err=%d)", __FUNCTION__, err));
		goto unlock_and_return_fault;
	}

	/* 
	   This should always be true as a.t.m. all bridge calls have to
   	   return an error message, but this could change so we do this
   	   check to be safe.
   	*/
	if (psBridgePackageKM->ui32OutBufferSize > 0)
	{
		err = -EFAULT;
		if (CopyToUserWrapper (psConnection,
						ui32DispatchTableEntry,
						psBridgePackageKM->pvParamOut,
						psBridgeOut,
						psBridgePackageKM->ui32OutBufferSize) != PVRSRV_OK)
		{
			goto unlock_and_return_fault;
		}
	}

	err = 0;

unlock_and_return_fault:
	if (g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock)
	{
		OSLockRelease(g_BridgeDispatchTable[ui32DispatchTableEntry].hBridgeLock);
	}
	else
	{
		OSReleaseBridgeLock();
	}
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: returning (err = %d)", __FUNCTION__, err));
	}
	return err;
}
