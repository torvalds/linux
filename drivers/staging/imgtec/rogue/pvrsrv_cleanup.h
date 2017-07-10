/**************************************************************************/ /*!
@File
@Title          PowerVR SrvKM cleanup thread deferred work interface
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
*/ /***************************************************************************/

#ifndef PVRSRV_CLEANUP_H
#define PVRSRV_CLEANUP_H

typedef PVRSRV_ERROR (*CLEANUP_THREAD_FN)(void *pvParam);

/* typical number of times a caller should want the work to be retried in case
 * of the callback function (pfnFree) returning an error.
 * Callers to PVRSRVCleanupThreadAddWork should provide this value as the retry
 * count (ui32RetryCount) unless there are special requirements.
 * A value of 6000 corresponds to around 10 minutes.
 */
#define CLEANUP_THREAD_RETRY_COUNT_DEFAULT 6000

typedef struct _PVRSRV_CLEANUP_THREAD_WORK_
{
	DLLIST_NODE sNode; /*!< list node to attach to the cleanup thread work list */
	CLEANUP_THREAD_FN pfnFree; /*!< function to be called */
	void *pvData; /*!< private data for pfnFree */
	IMG_UINT32 ui32RetryCount; /*!< number of times the callback should be re-tried when it returns error */
	IMG_BOOL bDependsOnHW;
} PVRSRV_CLEANUP_THREAD_WORK;

/*!
******************************************************************************
 @Function                PVRSRVCleanupThreadAddWork

 @Description             Add a work item to be called from the cleanup thread

 @Input psData          : The function pointer and private data for the callback

 @Return                  None
******************************************************************************/
void PVRSRVCleanupThreadAddWork(PVRSRV_CLEANUP_THREAD_WORK *psData);

#endif /* PVRSRV_CLEANUP_H */
