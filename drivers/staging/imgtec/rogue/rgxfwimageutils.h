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

#ifndef __RGXFWIMAGEUTILS_H__
#define __RGXFWIMAGEUTILS_H__

/* The routines declared here are built on top of an abstraction layer to
 * hide DDK/OS-specific details in case they are used outside of the DDK
 * (e.g. when DRM security is enabled).
 * Any new dependency should be added to rgxlayer.h.
 * Any new code should be built on top of the existing abstraction layer,
 * which should be extended when necessary. */
#include "rgxlayer.h"


/*!
*******************************************************************************

 @Function     RGXGetFWImageAllocSize

 @Description  Return size of Firmware code/data/coremem code allocations

 @Input        puiFWCodeAllocSize : Returned code size
 @Input        puiFWDataAllocSize : Returned data size
 @Input        puiFWCorememCodeAllocSize : Returned coremem code size (0 if N/A)

 @Return       PVRSRV_ERROR

******************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR RGXGetFWImageAllocSize(const void *hPrivate,
                                    IMG_DEVMEM_SIZE_T *puiFWCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWDataAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememCodeAllocSize);

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
 @Input        psFWCodeDevVAddrBase    : FW code base device virtual address
 @Input        psFWDataDevVAddrBase    : FW data base device virtual address
 @Input        psFWCorememDevVAddrBase : FW coremem code base device virtual address
 @Input        psFWCorememFWAddr    : FW coremem code allocation 32 bit (FW) address
 @Input        psRGXFwInit          : FW init structure 32 bit (FW) address
 @Input        ui32NumThreads       : Number of FW threads in use
 @Input        ui32MainThreadID     : ID of the FW thread in use
                                      (only meaningful if ui32NumThreads == 1)

 @Return       PVRSRV_ERROR

******************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR RGXProcessFWImage(const void           *hPrivate,
                               const IMG_BYTE       *pbRGXFirmware,
                               void                 *pvFWCode,
                               void                 *pvFWData,
                               void                 *pvFWCorememCode,
                               IMG_DEV_VIRTADDR     *psFWCodeDevVAddrBase,
                               IMG_DEV_VIRTADDR     *psFWDataDevVAddrBase,
                               IMG_DEV_VIRTADDR     *psFWCorememDevVAddrBase,
                               RGXFWIF_DEV_VIRTADDR *psFWCorememFWAddr,
                               RGXFWIF_DEV_VIRTADDR *psRGXFwInit,
                               IMG_UINT32           ui32NumThreads,
                               IMG_UINT32           ui32MainThreadID);

#endif /* __RGXFWIMAGEUTILS_H__ */

