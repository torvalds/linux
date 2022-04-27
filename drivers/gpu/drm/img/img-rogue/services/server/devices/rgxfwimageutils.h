/*************************************************************************/ /*!
@File
@Title          Header for Services Firmware image utilities used at init time
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for Services Firmware image utilities used at init time
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

#ifndef RGXFWIMAGEUTILS_H
#define RGXFWIMAGEUTILS_H

/* The routines declared here are built on top of an abstraction layer to
 * hide DDK/OS-specific details in case they are used outside of the DDK
 * (e.g. when DRM security is enabled).
 * Any new dependency should be added to rgxlayer.h.
 * Any new code should be built on top of the existing abstraction layer,
 * which should be extended when necessary.
 */
#include "rgxlayer.h"


typedef union _RGX_FW_BOOT_PARAMS_
{
	struct
	{
		IMG_DEV_VIRTADDR sFWCodeDevVAddr;
		IMG_DEV_VIRTADDR sFWDataDevVAddr;
		IMG_DEV_VIRTADDR sFWCorememCodeDevVAddr;
		RGXFWIF_DEV_VIRTADDR sFWCorememCodeFWAddr;
		IMG_DEVMEM_SIZE_T uiFWCorememCodeSize;
		IMG_DEV_VIRTADDR sFWCorememDataDevVAddr;
		RGXFWIF_DEV_VIRTADDR sFWCorememDataFWAddr;
		IMG_UINT32 ui32NumThreads;
	} sMeta;

#if defined(RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES)
	struct
	{
		IMG_DEV_PHYADDR sGPURegAddr;
		IMG_DEV_PHYADDR asFWPageTableAddr[RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES];
		IMG_DEV_PHYADDR sFWStackAddr;
		IMG_UINT32 ui32FWPageTableLog2PageSize;
		IMG_UINT32 ui32FWPageTableNumPages;
	} sMips;
#endif

	struct
	{
		IMG_DEV_VIRTADDR sFWCorememCodeDevVAddr;
		RGXFWIF_DEV_VIRTADDR sFWCorememCodeFWAddr;
		IMG_DEVMEM_SIZE_T uiFWCorememCodeSize;

		IMG_DEV_VIRTADDR sFWCorememDataDevVAddr;
		RGXFWIF_DEV_VIRTADDR sFWCorememDataFWAddr;
		IMG_DEVMEM_SIZE_T uiFWCorememDataSize;
	} sRISCV;

} RGX_FW_BOOT_PARAMS;

/*!
*******************************************************************************

 @Function     RGXGetFWImageSectionOffset

 @Input        hPrivate : Implementation specific data
 @Input        eId      : Section id

 @Description  Return offset of a Firmware section, relative to the beginning
               of the code or data allocation (depending on the section id)

******************************************************************************/
IMG_UINT32 RGXGetFWImageSectionOffset(const void *hPrivate,
                                      RGX_FW_SECTION_ID eId);

/*!
*******************************************************************************

 @Function     RGXGetFWImageSectionMaxSize

 @Input        hPrivate : Implementation specific data
 @Input        eId      : Section id

 @Description  Return maximum size (not allocation size) of a Firmware section

******************************************************************************/
IMG_UINT32 RGXGetFWImageSectionMaxSize(const void *hPrivate,
                                       RGX_FW_SECTION_ID eId);

/*!
*******************************************************************************

 @Function     RGXGetFWImageSectionAllocSize

 @Input        hPrivate : Implementation specific data
 @Input        eId      : Section id

 @Description  Return allocation size of a Firmware section

******************************************************************************/
IMG_UINT32 RGXGetFWImageSectionAllocSize(const void *hPrivate,
                                         RGX_FW_SECTION_ID eId);

/*!
*******************************************************************************

 @Function     RGXGetFWImageSectionAddress

 @Input        hPrivate : Implementation specific data
 @Input        eId      : Section id

 @Description  Return base address of a Firmware section

******************************************************************************/
IMG_UINT32 RGXGetFWImageSectionAddress(const void *hPrivate,
                                       RGX_FW_SECTION_ID eId);

/*!
*******************************************************************************

 @Function     RGXGetFWImageAllocSize

 @Description  Return size of Firmware code/data/coremem code allocations

 @Input        hPrivate            : Implementation specific data
 @Input        pbRGXFirmware       : Pointer to FW binary
 @Input        ui32RGXFirmwareSize : FW binary size
 @Output       puiFWCodeAllocSize  : Code size
 @Output       puiFWDataAllocSize  : Data size
 @Output       puiFWCorememCodeAllocSize : Coremem code size (0 if N/A)
 @Output       puiFWCorememDataAllocSize : Coremem data size (0 if N/A)

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXGetFWImageAllocSize(const void *hPrivate,
                                    const IMG_BYTE    *pbRGXFirmware,
                                    const IMG_UINT32  ui32RGXFirmwareSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWDataAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememDataAllocSize);

/*!
*******************************************************************************

 @Function      ProcessLDRCommandStream

 @Description   Process the output of the Meta toolchain in the .LDR format
                copying code and data sections into their final location and
                passing some information to the Meta bootloader

 @Input         hPrivate                 : Implementation specific data
 @Input         pbLDR                    : Pointer to FW blob
 @Input         pvHostFWCodeAddr         : Pointer to FW code
 @Input         pvHostFWDataAddr         : Pointer to FW data
 @Input         pvHostFWCorememCodeAddr  : Pointer to FW coremem code
 @Input         pvHostFWCorememDataAddr  : Pointer to FW coremem data
 @Input         ppui32BootConf           : Pointer to bootloader data

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR ProcessLDRCommandStream(const void *hPrivate,
                                     const IMG_BYTE* pbLDR,
                                     void* pvHostFWCodeAddr,
                                     void* pvHostFWDataAddr,
                                     void* pvHostFWCorememCodeAddr,
                                     void* pvHostFWCorememDataAddr,
                                     IMG_UINT32 **ppui32BootConf);

/*!
*******************************************************************************

 @Function      ProcessELFCommandStream

 @Description   Process a file in .ELF format copying code and data sections
                into their final location

 @Input         hPrivate                 : Implementation specific data
 @Input         pbELF                    : Pointer to FW blob
 @Input         pvHostFWCodeAddr         : Pointer to FW code
 @Input         pvHostFWDataAddr         : Pointer to FW data
 @Input         pvHostFWCorememCodeAddr  : Pointer to FW coremem code
 @Input         pvHostFWCorememDataAddr  : Pointer to FW coremem data

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR ProcessELFCommandStream(const void *hPrivate,
                                     const IMG_BYTE *pbELF,
                                     void *pvHostFWCodeAddr,
                                     void *pvHostFWDataAddr,
                                     void* pvHostFWCorememCodeAddr,
                                     void* pvHostFWCorememDataAddr);

/*!
*******************************************************************************

 @Function     RGXProcessFWImage

 @Description  Process the Firmware binary blob copying code and data
               sections into their final location and passing some
               information to the Firmware bootloader.
               If a pointer to the final memory location for FW code or data
               is not valid (NULL) then the relative section will not be
               processed.

 @Input        hPrivate        : Implementation specific data
 @Input        pbRGXFirmware   : Pointer to FW blob
 @Input        pvFWCode        : Pointer to FW code
 @Input        pvFWData        : Pointer to FW data
 @Input        pvFWCorememCode : Pointer to FW coremem code
 @Input        pvFWCorememData : Pointer to FW coremem data
 @Input        puFWParams      : Parameters used by the FW at boot time

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXProcessFWImage(const void *hPrivate,
                               const IMG_BYTE *pbRGXFirmware,
                               void *pvFWCode,
                               void *pvFWData,
                               void *pvFWCorememCode,
                               void *pvFWCorememData,
                               RGX_FW_BOOT_PARAMS *puFWParams);

#endif /* RGXFWIMAGEUTILS_H */
