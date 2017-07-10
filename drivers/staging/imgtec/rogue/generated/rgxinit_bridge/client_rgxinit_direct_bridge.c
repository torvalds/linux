/*************************************************************************/ /*!
@Title          Direct client bridge for rgxinit
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

#include "client_rgxinit_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "rgx_bridge.h"
#include "rgxscript.h"
#include "devicemem_typedefs.h"
#include "rgx_fwif.h"

#include "rgxinit.h"
#include "pmr.h"


IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitAllocFWImgMem(IMG_HANDLE hBridge,
								  IMG_DEVMEM_SIZE_T uiFWCodeLen,
								  IMG_DEVMEM_SIZE_T uiFWDataLen,
								  IMG_DEVMEM_SIZE_T uiFWCoremem,
								  IMG_HANDLE *phFWCodePMR,
								  IMG_DEV_VIRTADDR *psFWCodeDevVAddrBase,
								  IMG_HANDLE *phFWDataPMR,
								  IMG_DEV_VIRTADDR *psFWDataDevVAddrBase,
								  IMG_HANDLE *phFWCorememPMR,
								  IMG_DEV_VIRTADDR *psFWCorememDevVAddrBase,
								  RGXFWIF_DEV_VIRTADDR *psFWCorememMetaVAddrBase)
{
	PVRSRV_ERROR eError;
	PMR * psFWCodePMRInt;
	PMR * psFWDataPMRInt;
	PMR * psFWCorememPMRInt;


	eError =
		PVRSRVRGXInitAllocFWImgMemKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					uiFWCodeLen,
					uiFWDataLen,
					uiFWCoremem,
					&psFWCodePMRInt,
					psFWCodeDevVAddrBase,
					&psFWDataPMRInt,
					psFWDataDevVAddrBase,
					&psFWCorememPMRInt,
					psFWCorememDevVAddrBase,
					psFWCorememMetaVAddrBase);

	*phFWCodePMR = psFWCodePMRInt;
	*phFWDataPMR = psFWDataPMRInt;
	*phFWCorememPMR = psFWCorememPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitFirmware(IMG_HANDLE hBridge,
							     RGXFWIF_DEV_VIRTADDR *pspsRGXFwInit,
							     IMG_BOOL bEnableSignatureChecks,
							     IMG_UINT32 ui32SignatureChecksBufSize,
							     IMG_UINT32 ui32HWPerfFWBufSizeKB,
							     IMG_UINT64 ui64HWPerfFilter,
							     IMG_UINT32 ui32RGXFWAlignChecksArrLength,
							     IMG_UINT32 *pui32RGXFWAlignChecks,
							     IMG_UINT32 ui32ConfigFlags,
							     IMG_UINT32 ui32LogType,
							     IMG_UINT32 ui32FilterFlags,
							     IMG_UINT32 ui32JonesDisableMask,
							     IMG_UINT32 ui32ui32HWRDebugDumpLimit,
							     RGXFWIF_COMPCHECKS_BVNC *psClientBVNC,
							     IMG_UINT32 ui32HWPerfCountersDataSize,
							     IMG_HANDLE *phHWPerfPMR,
							     RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf,
							     FW_PERF_CONF eFirmwarePerf)
{
	PVRSRV_ERROR eError;
	PMR * psHWPerfPMRInt;


	eError =
		PVRSRVRGXInitFirmwareKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					pspsRGXFwInit,
					bEnableSignatureChecks,
					ui32SignatureChecksBufSize,
					ui32HWPerfFWBufSizeKB,
					ui64HWPerfFilter,
					ui32RGXFWAlignChecksArrLength,
					pui32RGXFWAlignChecks,
					ui32ConfigFlags,
					ui32LogType,
					ui32FilterFlags,
					ui32JonesDisableMask,
					ui32ui32HWRDebugDumpLimit,
					psClientBVNC,
					ui32HWPerfCountersDataSize,
					&psHWPerfPMRInt,
					eRGXRDPowerIslandConf,
					eFirmwarePerf);

	*phHWPerfPMR = psHWPerfPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitFinaliseFWImage(IMG_HANDLE hBridge)
{
	PVRSRV_ERROR eError;


	eError =
		PVRSRVRGXInitFinaliseFWImageKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
					);
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitDevPart2(IMG_HANDLE hBridge,
							     RGX_INIT_COMMAND *psDbgScript,
							     IMG_UINT32 ui32DeviceFlags,
							     IMG_UINT32 ui32HWPerfHostBufSize,
							     IMG_UINT32 ui32HWPerfHostFilter,
							     IMG_UINT32 ui32RGXActivePMConf,
							     IMG_HANDLE hFWCodePMR,
							     IMG_HANDLE hFWDataPMR,
							     IMG_HANDLE hFWCorememPMR,
							     IMG_HANDLE hHWPerfPMR)
{
	PVRSRV_ERROR eError;
	PMR * psFWCodePMRInt;
	PMR * psFWDataPMRInt;
	PMR * psFWCorememPMRInt;
	PMR * psHWPerfPMRInt;

	psFWCodePMRInt = (PMR *) hFWCodePMR;
	psFWDataPMRInt = (PMR *) hFWDataPMR;
	psFWCorememPMRInt = (PMR *) hFWCorememPMR;
	psHWPerfPMRInt = (PMR *) hHWPerfPMR;

	eError =
		PVRSRVRGXInitDevPart2KM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					psDbgScript,
					ui32DeviceFlags,
					ui32HWPerfHostBufSize,
					ui32HWPerfHostFilter,
					ui32RGXActivePMConf,
					psFWCodePMRInt,
					psFWDataPMRInt,
					psFWCorememPMRInt,
					psHWPerfPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeGPUVIRTPopulateLMASubArenas(IMG_HANDLE hBridge,
									 IMG_UINT32 ui32NumElements,
									 IMG_UINT32 *pui32Elements,
									 IMG_BOOL bEnableTrustedDeviceAceConfig)
{
	PVRSRV_ERROR eError;


	eError =
		PVRSRVGPUVIRTPopulateLMASubArenasKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					ui32NumElements,
					pui32Elements,
					bEnableTrustedDeviceAceConfig);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitGuest(IMG_HANDLE hBridge,
							  IMG_BOOL bEnableSignatureChecks,
							  IMG_UINT32 ui32SignatureChecksBufSize,
							  IMG_UINT32 ui32RGXFWAlignChecksArrLength,
							  IMG_UINT32 *pui32RGXFWAlignChecks,
							  IMG_UINT32 ui32DeviceFlags,
							  RGXFWIF_COMPCHECKS_BVNC *psClientBVNC)
{
	PVRSRV_ERROR eError;


	eError =
		PVRSRVRGXInitGuestKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					bEnableSignatureChecks,
					ui32SignatureChecksBufSize,
					ui32RGXFWAlignChecksArrLength,
					pui32RGXFWAlignChecks,
					ui32DeviceFlags,
					psClientBVNC);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRGXInitFirmwareExtended(IMG_HANDLE hBridge,
								     IMG_UINT32 ui32RGXFWAlignChecksArrLength,
								     IMG_UINT32 *pui32RGXFWAlignChecks,
								     RGXFWIF_DEV_VIRTADDR *pspsRGXFwInit,
								     IMG_HANDLE *phHWPerfPMR2,
								     RGX_FW_INIT_IN_PARAMS *pspsInParams)
{
	PVRSRV_ERROR eError;
	PMR * psHWPerfPMR2Int;


	eError =
		PVRSRVRGXInitFirmwareExtendedKM(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					ui32RGXFWAlignChecksArrLength,
					pui32RGXFWAlignChecks,
					pspsRGXFwInit,
					&psHWPerfPMR2Int,
					pspsInParams);

	*phHWPerfPMR2 = psHWPerfPMR2Int;
	return eError;
}

