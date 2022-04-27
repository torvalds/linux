/*************************************************************************/ /*!
@File
@Title          SO Interface header file for common PVR functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Contains SO interface functions. These functions are defined in
                the common layer and are called from the env layer OS specific
                implementation.
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

#if !defined(SOFUNC_PVR_H_)
#define SOFUNC_PVR_H_

#include "img_types.h"
#include "pvrsrv_error.h"

#include "device.h"
#include "pvr_notifier.h"


/**************************************************************************/ /*!
 @Function     SOPvrDbgRequestNotifyRegister
 @Description  SO Interface function called from the OS layer implementation.
               Register a callback function that is called when a debug request
               is made via a call PVRSRVDebugRequest. There are a number of
               verbosity levels ranging from DEBUG_REQUEST_VERBOSITY_LOW up to
               DEBUG_REQUEST_VERBOSITY_MAX. The callback will be called once
               for each level up to the highest level specified to
               PVRSRVDebugRequest.
@Output        phNotify             On success, points to debug notifier handle
@Input         psDevNode            Device node for which the debug callback
                                    should be registered
@Input         pfnDbgRequestNotify  Function callback
@Input         ui32RequesterID      Requester ID. This is used to determine
                                    the order in which callbacks are called,
                                    see DEBUG_REQUEST_*
@Input         hDbgReqeustHandle    Data to be passed back to the caller via
                                    the callback function
@Return        PVRSRV_ERROR         PVRSRV_OK on success and an error otherwise
*/ /******************************************************************** ******/
PVRSRV_ERROR SOPvrDbgRequestNotifyRegister(IMG_HANDLE *phNotify,
							  PVRSRV_DEVICE_NODE *psDevNode,
							  PFN_DBGREQ_NOTIFY pfnDbgRequestNotify,
							  IMG_UINT32 ui32RequesterID,
							  PVRSRV_DBGREQ_HANDLE hDbgRequestHandle);

/**************************************************************************/ /*!
 @Function     SOPvrDbgRequestNotifyUnregister
 @Description  SO Interface function called from the OS layer implementation.
               Remove and clean up the specified notifier registration so that
               it does not receive any further callbacks.
 @Input        hNotify     Handle returned to caller from
                           SOPvrDbgRequestNotifyRegister().
 @Return       PVRSRV_ERROR
*/ /***************************************************************************/
PVRSRV_ERROR SOPvrDbgRequestNotifyUnregister(IMG_HANDLE hNotify);


#endif /* SOFUNC_PVR_H_ */
