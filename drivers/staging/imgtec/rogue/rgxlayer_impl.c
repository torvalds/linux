/*************************************************************************/ /*!
@File
@Title          DDK implementation of the Services abstraction layer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    DDK implementation of the Services abstraction layer
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

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/kernel.h>
#include "pdump_km.h"
#else
#include <stdio.h>
#include <stdarg.h>
#include "pdump_um.h"
#endif

#include "rgxlayer_impl.h"
#include "srvinit_osfunc.h"
#include "pvr_debug.h"

#if defined(SUPPORT_KERNEL_SRVINIT)
#include "device.h"
#include "rgxdevice.h"
#endif

#if defined(PDUMP)
#include "client_pdump_bridge.h"
#endif


void RGXMemCopy(const void *hPrivate,
                void *pvDst,
                void *pvSrc,
                size_t uiSize)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	SRVINITDeviceMemCopy(pvDst, pvSrc, uiSize);
}

void RGXMemSet(const void *hPrivate,
               void *pvDst,
               IMG_UINT8 ui8Value,
               size_t uiSize)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	SRVINITDeviceMemSet(pvDst, ui8Value, uiSize);
}

void RGXCommentLogInit(const void *hPrivate,
                       const IMG_CHAR *pszString,
                       ...)
{
#if defined(PDUMP)
	IMG_CHAR szBuffer[PVRSRV_PDUMP_MAX_COMMENT_SIZE];
	va_list argList;
	SHARED_DEV_CONNECTION hServices;

	PVR_ASSERT(hPrivate != NULL);
	hServices = ((RGX_INIT_LAYER_PARAMS*)hPrivate)->hServices;

	va_start(argList, pszString);
	vsnprintf(szBuffer, sizeof(szBuffer), pszString, argList);
	va_end(argList);

	(void) BridgePVRSRVPDumpComment(hServices,
	                                szBuffer,
	                                PDUMP_FLAGS_CONTINUOUS);
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	PVR_UNREFERENCED_PARAMETER(pszString);
#endif
}

void RGXErrorLogInit(const void *hPrivate,
                     const IMG_CHAR *pszString,
                     ...)
{
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list argList;

	PVR_UNREFERENCED_PARAMETER(hPrivate);

	va_start(argList, pszString);
	vsnprintf(szBuffer, sizeof(szBuffer), pszString, argList);
	va_end(argList);

	PVR_DPF((PVR_DBG_ERROR, "%s", szBuffer));
}

#if defined(SUPPORT_KERNEL_SRVINIT)
IMG_BOOL RGXDeviceHasFeatureInit(const void *hPrivate, IMG_UINT64 ui64Feature)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);

	psDeviceNode = (PVRSRV_DEVICE_NODE *)(((RGX_INIT_LAYER_PARAMS *)hPrivate)->hServices);
	psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	return (psDevInfo->sDevFeatureCfg.ui64Features & ui64Feature) != 0;
}
#endif

IMG_UINT32 RGXGetFWCorememSize(const void *hPrivate)
{
#if defined(SUPPORT_KERNEL_SRVINIT)
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);

	psDeviceNode = (PVRSRV_DEVICE_NODE *)(((RGX_INIT_LAYER_PARAMS *)hPrivate)->hServices);
	psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	return psDevInfo->sDevFeatureCfg.ui32MCMS;
#elif defined(RGX_META_COREMEM_CODE) || defined(RGX_META_COREMEM_DATA)
	PVR_UNREFERENCED_PARAMETER(hPrivate);

	return RGX_META_COREMEM_SIZE;
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);

	return 0;
#endif
}

