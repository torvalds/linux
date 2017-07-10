/*************************************************************************/ /*!
@File
@Title          Header for Services abstraction layer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declaration of an interface layer used to abstract code that
                can be compiled outside of the DDK, potentially in a
                completely different OS.
                All the headers included by this file must also be copied to
                the alternative source tree.
                All the functions declared here must have a DDK implementation
                inside the DDK source tree (e.g. rgxlayer_impl.h/.c) and
                another different implementation in case they are used outside
                of the DDK.
                All of the functions accept as a first parameter a
                "const void *hPrivate" argument. It should be used to pass
                around any implementation specific data required.
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

#if !defined (__RGXLAYER_H__)
#define __RGXLAYER_H__

#if defined (__cplusplus)
extern "C" {
#endif


#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h" /* includes pvrsrv_errors.h */
#if defined(SUPPORT_KERNEL_SRVINIT)
#include "rgx_bvnc_defs_km.h"
#endif

#include "rgx_firmware_processor.h"
/* includes:
 * rgx_meta.h and rgx_mips.h,
 * rgxdefs_km.h,
 * rgx_cr_defs_km.h (under SUPPORT_KERNEL_SRVINIT),
 * RGX_BVNC_CORE_KM_HEADER (rgxcore_km_B.V.N.C.h),
 * RGX_BNC_CONFIG_KM_HEADER (rgxconfig_km_B.V.N.C.h)
 */

#include "rgx_fwif_shared.h"
/* FIXME required because of RGXFWIF_DEV_VIRTADDR but this header
 * includes a lot of other headers..  RGXFWIF_DEV_VIRTADDR must be moved
 * somewhere else (either img_types.h or a new header) */


/*!
*******************************************************************************

 @Function       RGXMemCopy

 @Description    MemCopy implementation

 @Input          hPrivate   : Implementation specific data
 @Input          pvDst      : Pointer to the destination
 @Input          pvSrc      : Pointer to the source location
 @Input          uiSize     : The amount of memory to copy in bytes

 @Return         void

******************************************************************************/
IMG_INTERNAL
void RGXMemCopy(const void *hPrivate,
                void *pvDst,
                void *pvSrc,
                size_t uiSize);

/*!
*******************************************************************************

 @Function       RGXMemSet

 @Description    MemSet implementation

 @Input          hPrivate   : Implementation specific data
 @Input          pvDst      : Pointer to the start of the memory region
 @Input          ui8Value   : The value to be written
 @Input          uiSize     : The number of bytes to be set to ui8Value

 @Return         void

******************************************************************************/
IMG_INTERNAL
void RGXMemSet(const void *hPrivate,
               void *pvDst,
               IMG_UINT8 ui8Value,
               size_t uiSize);

/*!
*******************************************************************************

 @Function       RGXCommentLogInit

 @Description    Generic log function used for debugging or other purposes

 @Input          hPrivate   : Implementation specific data
 @Input          pszString  : Message to be printed
 @Input          ...        : Variadic arguments

 @Return         void

******************************************************************************/
IMG_INTERNAL
void RGXCommentLogInit(const void *hPrivate,
                       const IMG_CHAR *pszString,
                       ...) __printf(2, 3);

/*!
*******************************************************************************

 @Function       RGXErrorLogInit

 @Description    Generic error log function used for debugging or other purposes

 @Input          hPrivate   : Implementation specific data
 @Input          pszString  : Message to be printed
 @Input          ...        : Variadic arguments

 @Return         void

******************************************************************************/
IMG_INTERNAL
void RGXErrorLogInit(const void *hPrivate,
                     const IMG_CHAR *pszString,
                     ...) __printf(2, 3);

#if defined(SUPPORT_KERNEL_SRVINIT)
/*!
*******************************************************************************

 @Function       RGXDeviceHasFeatureInit

 @Description    Checks if a device has a particular feature

 @Input          hPrivate     : Implementation specific data
 @Input          ui64Feature  : Feature to check

 @Return         IMG_TRUE if the given feature is available, IMG_FALSE otherwise

******************************************************************************/
IMG_INTERNAL
IMG_BOOL RGXDeviceHasFeatureInit(const void *hPrivate, IMG_UINT64 ui64Feature);
#endif

/*!
*******************************************************************************

 @Function       RGXGetFWCorememSize

 @Description    Get the FW coremem size

 @Input          hPrivate   : Implementation specific data

 @Return         FW coremem size

******************************************************************************/
IMG_INTERNAL
IMG_UINT32 RGXGetFWCorememSize(const void *hPrivate);


#if defined (__cplusplus)
}
#endif

#endif /* !defined (__RGXLAYER_H__) */

