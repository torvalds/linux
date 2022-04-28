/*************************************************************************/ /*!
@File
@Title          Device specific utility routines declarations
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Inline functions/structures specific to RGX
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

#include "device.h"
#include "rgxdevice.h"
#include "rgxdebug.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"

/*!
******************************************************************************

 @Function      RGXQueryAPMState

 @Description   Query the state of the APM configuration

 @Input         psDeviceNode : The device node

 @Input         pvPrivateData: Unused (required for AppHint callback)

 @Output        pui32State   : The APM configuration state

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXQueryAPMState(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_UINT32 *pui32State);

/*!
******************************************************************************

 @Function      RGXSetAPMState

 @Description   Set the APM configuration state. Currently only 'OFF' is
                supported

 @Input         psDeviceNode : The device node

 @Input         pvPrivateData: Unused (required for AppHint callback)

 @Input         ui32State    : The requested APM configuration state

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXSetAPMState(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_UINT32 ui32State);

/*!
******************************************************************************

 @Function      RGXQueryPdumpPanicDisable

 @Description   Get the PDump Panic Enable configuration state.

 @Input         psDeviceNode : The device node

 @Input         pvPrivateData: Unused (required for AppHint callback)

 @Input         pbDisabled    : IMG_TRUE if PDump Panic is disabled

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXQueryPdumpPanicDisable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL *pbDisabled);

/*!
******************************************************************************

 @Function      RGXSetPdumpPanicDisable

 @Description   Set the PDump Panic Enable flag

 @Input         psDeviceNode : The device node

 @Input         pvPrivateData: Unused (required for AppHint callback)

 @Input         bDisable      : The requested configuration state

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXSetPdumpPanicDisable(const PVRSRV_DEVICE_NODE *psDeviceNode,
	const void *pvPrivateData,
	IMG_BOOL bDisable);

/*!
******************************************************************************

 @Function      RGXGetDeviceFlags

 @Description   Get the device flags for a given device

 @Input         psDevInfo        : The device descriptor query

 @Output        pui32DeviceFlags : The current state of the device flags

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXGetDeviceFlags(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 *pui32DeviceFlags);

/*!
******************************************************************************

 @Function      RGXSetDeviceFlags

 @Description   Set the device flags for a given device

 @Input         psDevInfo : The device descriptor to modify

 @Input         ui32Config : The device flags to modify

 @Input         bSetNotClear : Set or clear the specified flags

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXSetDeviceFlags(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 ui32Config,
				IMG_BOOL bSetNotClear);

/*!
******************************************************************************

 @Function    RGXStringifyKickTypeDM

 @Description Gives the kick type DM name stringified

 @Input       Kick type DM

 @Return      Array containing the kick type DM name

******************************************************************************/
const char* RGXStringifyKickTypeDM(RGX_KICK_TYPE_DM eKickTypeDM);

#define RGX_STRINGIFY_KICK_TYPE_DM_IF_SET(bitmask, eKickTypeDM) bitmask & eKickTypeDM ? RGXStringifyKickTypeDM(eKickTypeDM) : ""
/******************************************************************************
 End of file (rgxutils.h)
******************************************************************************/
