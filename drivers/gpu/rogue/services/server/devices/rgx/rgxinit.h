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

#include "pvrsrv_error.h"
#include "img_types.h"
#include "rgxscript.h"
#include "device.h"
#include "rgxdevice.h"


/*!
*******************************************************************************

 @Function	PVRSRVRGXInitDevPart2KM

 @Description

 Second part of server-side RGX initialisation

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR PVRSRVRGXInitDevPart2KM (PVRSRV_DEVICE_NODE	*psDeviceNode,
									  RGX_INIT_COMMAND		*psInitScript,
									  RGX_INIT_COMMAND		*psDbgScript,
									  RGX_INIT_COMMAND		*psDbgBusScript,
									  RGX_INIT_COMMAND		*psDeinitScript,
									  IMG_UINT32			ui32KernelCatBaseIdReg,
									  IMG_UINT32			ui32KernelCatBaseId,
									  IMG_UINT32			ui32KernelCatBaseReg,
									  IMG_UINT32			ui32KernelCatBaseWordSize,
									  IMG_UINT32			ui32KernelCatBaseAlignShift,
									  IMG_UINT32			ui32KernelCatBaseShift,
									  IMG_UINT64			ui64KernelCatBaseMask,
									  IMG_UINT32			ui32DeviceFlags,
									  RGX_ACTIVEPM_CONF		eActivePMConf,
								 	  DEVMEM_EXPORTCOOKIE	*psFWCodeAllocServerExportCookie,
								 	  DEVMEM_EXPORTCOOKIE	*psFWDataAllocServerExportCookie,
								 	  DEVMEM_EXPORTCOOKIE	*psFWCorememAllocServerExportCookie);

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitAllocFWImgMemKM(PVRSRV_DEVICE_NODE    *psDeviceNode,
										  IMG_DEVMEM_SIZE_T     ui32FWCodeLen,
									 	  IMG_DEVMEM_SIZE_T     ui32FWDataLen,
									 	  IMG_DEVMEM_SIZE_T     uiFWCorememLen,
									 	  DEVMEM_EXPORTCOOKIE   **ppsFWCodeAllocServerExportCookie,
									 	  IMG_DEV_VIRTADDR      *psFWCodeDevVAddrBase,
									 	  DEVMEM_EXPORTCOOKIE   **ppsFWDataAllocServerExportCookie,
									 	  IMG_DEV_VIRTADDR      *psFWDataDevVAddrBase,
										  DEVMEM_EXPORTCOOKIE   **ppsFWCorememAllocServerExportCookie,
										  IMG_DEV_VIRTADDR      *psFWCorememDevVAddrBase,
										  RGXFWIF_DEV_VIRTADDR  *psFWCorememMetaVAddrBase);



/*!
*******************************************************************************

 @Function	PVRSRVRGXInitFirmwareKM

 @Description

 Server-side RGX firmware initialisation

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR PVRSRVRGXInitFirmwareKM(PVRSRV_DEVICE_NODE			*psDeviceNode, 
									    RGXFWIF_DEV_VIRTADDR		*psRGXFwInit,
									    IMG_BOOL					bEnableSignatureChecks,
									    IMG_UINT32					ui32SignatureChecksBufSize,
									    IMG_UINT32					ui32HWPerfFWBufSizeKB,
									    IMG_UINT64					ui64HWPerfFilter,
									    IMG_UINT32					ui32RGXFWAlignChecksSize,
									    IMG_UINT32					*pui32RGXFWAlignChecks,
									    IMG_UINT32					ui32ConfigFlags,
									    IMG_UINT32					ui32LogType,
										IMG_UINT32					ui32FilterMode,
									    RGXFWIF_COMPCHECKS_BVNC     *psClientBVNC);


/*!
*******************************************************************************

 @Function	PVRSRVRGXInitLoadFWImageKM

 @Description

 Load the firmware image into place.

 @Input psFWImgDestPMR - PMR holding destination memory buffer for firmware

 @input psFWImgSrcPMR - PMR holding firmware image data to load

 @input ui64FWImgLen - number of bytes in Src/Dst memory buffers

 @input psFWImgSigPMR - a buffer holding a signature for Src, which is used for validation

 @input ui64FWSigLen - number of bytes contained in the signature buffer.

 @Return   PVRSRV_ERROR

******************************************************************************/

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXInitLoadFWImageKM(PMR *psFWImgDestPMR,
                                        PMR *psFWImgSrcPMR,
                                        IMG_UINT64 ui64FWImgLen,
										PMR *psFWImgSigPMR,
                                        IMG_UINT64 ui64FWSigLen);


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


#endif /* __RGXINIT_H__ */
