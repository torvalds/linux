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

#include "dllist.h"

/**************************************************************************/ /*!
@Brief          CLEANUP_THREAD_FN

@Description    This is the function prototype for the pfnFree member found in
                the structure PVRSRV_CLEANUP_THREAD_WORK. The function is
                responsible for carrying out the clean up work and if successful
                freeing the memory originally supplied to the call
                PVRSRVCleanupThreadAddWork().

@Input          pvParam  This is private data originally supplied by the caller
                         to PVRSRVCleanupThreadAddWork() when registering the
                         clean up work item, psDAta->pvData. Itr can be cast
                         to a relevant type within the using module.

@Return         PVRSRV_OK if the cleanup operation was successful and the
                callback has freed the PVRSRV_CLEANUP_THREAD_WORK* work item
                memory original supplied to PVRSRVCleanupThreadAddWork()
                Any other error code will lead to the work item
                being re-queued and hence the original
                PVRSRV_CLEANUP_THREAD_WORK* must not be freed.
*/ /***************************************************************************/

typedef PVRSRV_ERROR (*CLEANUP_THREAD_FN)(void *pvParam);


/* Typical number of times a caller should want the work to be retried in case
 * of the callback function (pfnFree) returning an error.
 * Callers to PVRSRVCleanupThreadAddWork should provide this value as the retry
 * count (ui32RetryCount) unless there are special requirements.
 * A value of 200 corresponds to around ~20s (200 * 100ms). If it is not
 * successful by then give up as an unrecoverable problem has occurred.
 */
#define CLEANUP_THREAD_RETRY_COUNT_DEFAULT 200u
/* Like for CLEANUP_THREAD_RETRY_COUNT_DEFAULT but call will wait for
 * a specified amount of time rather than number of retries.
 */
#define CLEANUP_THREAD_RETRY_TIMEOUT_MS_DEFAULT 20000u /* 20s */

/* Use to set retry count on a cleanup item.
 * _item - pointer to the PVRSRV_CLEANUP_THREAD_WORK
 * _count - retry count
 */
#define CLEANUP_THREAD_SET_RETRY_COUNT(_item,_count) \
	do { \
		(_item)->ui32RetryCount = (_count); \
		(_item)->ui32TimeStart = 0; \
		(_item)->ui32TimeEnd = 0; \
	} while (0)

/* Use to set timeout deadline on a cleanup item.
 * _item - pointer to the PVRSRV_CLEANUP_THREAD_WORK
 * _timeout - timeout in milliseconds, if 0
 *            CLEANUP_THREAD_RETRY_TIMEOUT_MS_DEFAULT is used
 */
#define CLEANUP_THREAD_SET_RETRY_TIMEOUT(_item,_timeout) \
	do { \
		(_item)->ui32RetryCount = 0; \
		(_item)->ui32TimeStart = OSClockms(); \
		(_item)->ui32TimeEnd = (_item)->ui32TimeStart + ((_timeout) > 0 ? \
				(_timeout) : CLEANUP_THREAD_RETRY_TIMEOUT_MS_DEFAULT); \
	} while (0)

/* Indicates if the timeout on a given item has been reached.
 * _item - pointer to the PVRSRV_CLEANUP_THREAD_WORK
 */
#define CLEANUP_THREAD_RETRY_TIMEOUT_REACHED(_item) \
	((_item)->ui32TimeEnd - (_item)->ui32TimeStart >= \
			OSClockms() - (_item)->ui32TimeStart)

/* Indicates if the current item is waiting on timeout or retry count.
 * _item - pointer to the PVRSRV_CLEANUP_THREAD_WORK
 * */
#define CLEANUP_THREAD_IS_RETRY_TIMEOUT(_item) \
	((_item)->ui32TimeStart != (_item->ui32TimeEnd))

/* Clean up work item specifics so that the task can be managed by the
 * pvr_defer_free cleanup thread in the Server.
 */
typedef struct _PVRSRV_CLEANUP_THREAD_WORK_
{
	DLLIST_NODE sNode;             /*!< List node used internally by the cleanup
	                                    thread */
	CLEANUP_THREAD_FN pfnFree;     /*!< Pointer to the function to be called to
	                                    carry out the deferred cleanup */
	void *pvData;                  /*!< private data for pfnFree, usually a way back
	                                    to the original PVRSRV_CLEANUP_THREAD_WORK*
	                                    pointer supplied in the call to
	                                    PVRSRVCleanupThreadAddWork(). */
	IMG_UINT32 ui32TimeStart;      /*!< Timestamp in ms of the moment when
	                                    cleanup item has been created. */
	IMG_UINT32 ui32TimeEnd;        /*!< Time in ms after which no further retry
	                                    attempts will be made, item discard and
	                                    error logged when this is reached. */
	IMG_UINT32 ui32RetryCount;     /*!< Number of times the callback should be
	                                    re-tried when it returns error. */
	IMG_BOOL bDependsOnHW;         /*!< Retry again after the RGX interrupt signals
	                                    the global event object */
} PVRSRV_CLEANUP_THREAD_WORK;


/**************************************************************************/ /*!
@Function       PVRSRVCleanupThreadAddWork

@Description    Add a work item to be called from the cleanup thread

@Input          psData : The function pointer and private data for the callback

@Return         None
*/ /***************************************************************************/
void PVRSRVCleanupThreadAddWork(PVRSRV_CLEANUP_THREAD_WORK *psData);

#endif /* PVRSRV_CLEANUP_H */
