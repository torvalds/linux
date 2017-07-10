/*************************************************************************/ /*!
@File
@Title          RGX initialisation header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX initialisation
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

#if !defined(__RGXINIT_H__)
#define __RGXINIT_H__

#include "connection_server.h"
#include "pvrsrv_error.h"
#include "img_types.h"
#include "rgxscript.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgx_bridge.h"


/*!
*******************************************************************************

 @Function	PVRSRVRGXInitDevPart2KM

 @Description

 Second part of server-side RGX initialisation

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR PVRSRVRGXInitDevPart2KM (CONNECTION_DATA       *psConnection,
                                      PVRSRV_DEVICE_NODE	*psDeviceNode,
									  RGX_INIT_COMMAND		*psDbgScript,
									  IMG_UINT32			ui32DeviceFlags,
									  IMG_UINT32			ui32HWPerfHostBufSizeKB,
									  IMG_UINT32			ui32HWPerfHostFilter,
									  RGX_ACTIVEPM_CONF		eActivePMConf,
									  PMR					*psFWCodePMR,
									  PMR					*psFWDataPMR,
									  PMR					*psFWCorememPMR,
									  PMR					*psHWPerfPMR);

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitAllocFWImgMemKM(CONNECTION_DATA      *psConnection,
                                          PVRSRV_DEVICE_NODE   *psDeviceNode,
                                          IMG_DEVMEM_SIZE_T    ui32FWCodeLen,
                                          IMG_DEVMEM_SIZE_T    ui32FWDataLen,
                                          IMG_DEVMEM_SIZE_T    uiFWCorememLen,
                                          PMR                  **ppsFWCodePMR,
                                          IMG_DEV_VIRTADDR     *psFWCodeDevVAddrBase,
                                          PMR                  **ppsFWDataPMR,
                                          IMG_DEV_VIRTADDR     *psFWDataDevVAddrBase,
                                          PMR                  **ppsFWCorememPMR,
                                          IMG_DEV_VIRTADDR     *psFWCorememDevVAddrBase,
                                          RGXFWIF_DEV_VIRTADDR *psFWCorememMetaVAddrBase);

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitMipsWrapperRegistersKM(PVRSRV_DEVICE_NODE *psDeviceNode,
												 IMG_UINT32 ui32Remap1Config1Offset,
												 IMG_UINT32 ui32Remap1Config2Offset,
												 IMG_UINT32 ui32WrapperConfigOffset,
												 IMG_UINT32 ui32BootCodeOffset);

IMG_EXPORT 
PVRSRV_ERROR IMG_CALLCONV PVRSRVRGXInitGuestKM(CONNECTION_DATA			*psConnection,
												PVRSRV_DEVICE_NODE		*psDeviceNode,
												IMG_BOOL				bEnableSignatureChecks,
												IMG_UINT32				ui32SignatureChecksBufSize,
												IMG_UINT32				ui32RGXFWAlignChecksArrLength,
												IMG_UINT32				*pui32RGXFWAlignChecks,
												IMG_UINT32				ui32DeviceFlags,
												RGXFWIF_COMPCHECKS_BVNC *psClientBVNC);

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXPdumpBootldrDataInitKM(PVRSRV_DEVICE_NODE *psDeviceNode,
												 IMG_UINT32 ui32BootConfOffset,
												 IMG_UINT32 ui32ExceptionVectorsBaseAddress);


/*!
*******************************************************************************

 @Function	PVRSRVRGXInitFirmwareKM

 @Description

 Server-side RGX firmware initialisation

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVRGXInitFirmwareKM(CONNECTION_DATA          *psConnection,
                        PVRSRV_DEVICE_NODE       *psDeviceNode,
                        RGXFWIF_DEV_VIRTADDR     *psRGXFwInit,
                        IMG_BOOL                 bEnableSignatureChecks,
                        IMG_UINT32               ui32SignatureChecksBufSize,
                        IMG_UINT32               ui32HWPerfFWBufSizeKB,
                        IMG_UINT64               ui64HWPerfFilter,
                        IMG_UINT32               ui32RGXFWAlignChecksArrLength,
                        IMG_UINT32               *pui32RGXFWAlignChecks,
                        IMG_UINT32               ui32ConfigFlags,
                        IMG_UINT32               ui32LogType,
                        IMG_UINT32               ui32FilterFlags,
                        IMG_UINT32               ui32JonesDisableMask,
                        IMG_UINT32               ui32HWRDebugDumpLimit,
                        RGXFWIF_COMPCHECKS_BVNC  *psClientBVNC,
                        IMG_UINT32               ui32HWPerfCountersDataSize,
                        PMR                      **ppsHWPerfPMR,
                        RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandingConf,
                        FW_PERF_CONF             eFirmwarePerf);

/*!
*******************************************************************************

 @Function	PVRSRVRGXInitFirmwareExtendedKM

 @Description

 Server-side RGX firmware initialisation, extends PVRSRVRGXInitFirmwareKM

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVRGXInitFirmwareExtendedKM(CONNECTION_DATA        *psConnection,
                                PVRSRV_DEVICE_NODE     *psDeviceNode,
                                IMG_UINT32             ui32RGXFWAlignChecksArrLength,
                                IMG_UINT32             *pui32RGXFWAlignChecks,
                                RGXFWIF_DEV_VIRTADDR   *psRGXFwInit,
                                PMR                    **ppsHWPerfPMR,
                                RGX_FW_INIT_IN_PARAMS  *psInParams);

/*!
*******************************************************************************

 @Function  PVRSRVRGXInitFinaliseFWImageKM

 @Description

 Perform final steps of FW code setup when necessary

 @Input psDeviceNode - Device node

 @Return   PVRSRV_ERROR

******************************************************************************/

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXInitFinaliseFWImageKM(CONNECTION_DATA *psConnection,
                               PVRSRV_DEVICE_NODE *psDeviceNode);

/*!
*******************************************************************************

 @Function	PVRSRVRGXInitHWPerfCountersKM

 @Description

 Initialisation of the performance counters

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR PVRSRVRGXInitHWPerfCountersKM (PVRSRV_DEVICE_NODE	*psDeviceNode);

/*!
*******************************************************************************

 @Function	RGXRegisterDevice

 @Description

 Registers the device with the system

 @Input: 	psDeviceNode - device node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXRegisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode);


/*!
*******************************************************************************

 @Function	DevDeInitRGX

 @Description

 Reset and deinitialise Chip

 @Input pvDeviceNode - device info. structure

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR DevDeInitRGX(PVRSRV_DEVICE_NODE *psDeviceNode);


#if !defined(NO_HARDWARE)

void RGX_WaitForInterruptsTimeout(PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
*******************************************************************************

 @Function     RGXRegisterGpuUtilStats

 @Description  Initialise data used to compute GPU utilisation statistics
               for a particular user (identified by the handle passed as
               argument). This function must be called only once for each
               different user/handle.

 @Input        phGpuUtilUser - Pointer to handle used to identify a user of
                               RGXGetGpuUtilStats

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXRegisterGpuUtilStats(IMG_HANDLE *phGpuUtilUser);


/*!
*******************************************************************************

 @Function     RGXUnregisterGpuUtilStats

 @Description  Free data previously used to compute GPU utilisation statistics
               for a particular user (identified by the handle passed as
               argument).

 @Input        hGpuUtilUser - Handle used to identify a user of
                              RGXGetGpuUtilStats

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXUnregisterGpuUtilStats(IMG_HANDLE hGpuUtilUser);
#endif /* !defined(NO_HARDWARE) */


/*!
*******************************************************************************

 @Function		PVRSRVGPUVIRTPopulateLMASubArenasKM

 @Description	Populates the LMA arenas based on the min max values passed by
				the client during initialization. GPU Virtualisation Validation
				only.

 @Input			pvDeviceNode	: Pointer to a device info structure.
				ui32NumElements	: Total number of min / max values passed by
								  the client
				pui32Elements	: The array containing all the min / max values
								  passed by the client, all bundled together

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVGPUVIRTPopulateLMASubArenasKM(CONNECTION_DATA    * psConnection,
                                                 PVRSRV_DEVICE_NODE	* psDeviceNode,
                                                 IMG_UINT32         ui32NumElements,
                                                 IMG_UINT32         aui32Elements[],
                                                 IMG_BOOL bEnableTrustedDeviceAceConfig);

#endif /* __RGXINIT_H__ */
