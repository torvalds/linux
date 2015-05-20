/*************************************************************************/ /*!
@File
@Title          RGX debug header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX debugging functions
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

#if !defined(__RGXDEBUG_H__)
#define __RGXDEBUG_H__

#include "pvrsrv_error.h"
#include "img_types.h"
#include "device.h"
#include "pvrsrv.h"
#include "rgxdevice.h"


/*!
*******************************************************************************

 @Function	RGXPanic

 @Description

 Called when an unrecoverable situation is detected. Dumps RGX debug
 information and tells the OS to panic.

 @Input psDevInfo - RGX device info

 @Return IMG_VOID

******************************************************************************/
IMG_VOID RGXPanic(PVRSRV_RGXDEV_INFO	*psDevInfo);

/*!
*******************************************************************************

 @Function	RGXDumpDebugInfo

 @Description

 Dump useful debugging info. Dumps lesser information than PVRSRVDebugRequest.
 Does not dump debugging information for all requester types.(SysDebug, ServerSync info)

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input psDevInfo	        - RGX device info

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID RGXDumpDebugInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                          PVRSRV_RGXDEV_INFO	*psDevInfo);

/*!
*******************************************************************************

 @Function	RGXDebugRequestProcess

 @Description

 This function will print out the debug for the specificed level of
 verbosity

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input psDevInfo	        - RGX device info
 @Input ui32VerbLevel       - Verbosity level

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID RGXDebugRequestProcess(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                PVRSRV_RGXDEV_INFO	*psDevInfo,
                                IMG_UINT32			ui32VerbLevel);


#if defined(PVRSRV_ENABLE_FW_TRACE_DEBUGFS)
/*!
*******************************************************************************

 @Function	RGXDumpFirmwareTrace

 @Description Dumps the decoded version of the firmware trace buffer.

 Dump useful debugging info

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input psDevInfo	        - RGX device info

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID RGXDumpFirmwareTrace(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                              PVRSRV_RGXDEV_INFO	*psDevInfo);
#endif


/*!
*******************************************************************************

 @Function	RGXQueryDMState

 @Description

 Query DM state

 @Input  psDevInfo	 		- RGX device info
 @Input  eDM 				- DM number for which to return status
 @Output peState			- RGXFWIF_DM_STATE
 @Output psComCtxDevVAddr   - If DM is locked-up, Firmware address of Firmware Common Context, otherwise IMG_NULL

 @Return PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR RGXQueryDMState(PVRSRV_RGXDEV_INFO *psDevInfo, RGXFWIF_DM eDM, RGXFWIF_DM_STATE *peState, RGXFWIF_DEV_VIRTADDR *psComCtxDevVAddr);


#endif /* __RGXDEBUG_H__ */
