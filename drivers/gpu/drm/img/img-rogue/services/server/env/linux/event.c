/*************************************************************************/ /*!
@File
@Title          Event Object
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
*/ /**************************************************************************/

#include <asm/page.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <linux/version.h>
#include <linux/string.h>
#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/sched/signal.h>
#endif
#include <linux/interrupt.h>
#include <asm/hardirq.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "allocmem.h"
#include "event.h"
#include "pvr_debug.h"
#include "pvrsrv.h"
#include "pvr_bridge_k.h"

#include "osfunc.h"

/* Uncomment to enable event object stats that are useful for debugging.
 * The stats can be gotten at any time (during lifetime of event object)
 * using OSEventObjectDumpdebugInfo API */
// #define LINUX_EVENT_OBJECT_STATS


typedef struct PVRSRV_LINUX_EVENT_OBJECT_LIST_TAG
{
	rwlock_t sLock;
	/* Counts how many times event object was signalled i.e. how many times
	 * LinuxEventObjectSignal() was called on a given event object.
	 * Used for detecting pending signals.
	 * Note that this is in no way related to OS signals. */
	atomic_t sEventSignalCount;
	struct list_head sList;
} PVRSRV_LINUX_EVENT_OBJECT_LIST;


typedef struct PVRSRV_LINUX_EVENT_OBJECT_TAG
{
	IMG_UINT32 ui32EventSignalCountPrevious;
#if defined(DEBUG)
	IMG_UINT ui32Stats;
#endif

#ifdef LINUX_EVENT_OBJECT_STATS
	POS_LOCK hLock;
	IMG_UINT32 ui32ScheduleAvoided;
	IMG_UINT32 ui32ScheduleCalled;
	IMG_UINT32 ui32ScheduleSleptFully;
	IMG_UINT32 ui32ScheduleSleptPartially;
	IMG_UINT32 ui32ScheduleReturnedImmediately;
#endif
	wait_queue_head_t sWait;
	struct list_head sList;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList;
} PVRSRV_LINUX_EVENT_OBJECT;

/*!
******************************************************************************

 @Function	LinuxEventObjectListCreate

 @Description

 Linux wait object list creation

 @Output    hOSEventKM : Pointer to the event object list handle

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectListCreate(IMG_HANDLE *phEventObjectList)
{
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psEvenObjectList;

	psEvenObjectList = OSAllocMem(sizeof(*psEvenObjectList));
	if (psEvenObjectList == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectCreate: failed to allocate memory for event list"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	INIT_LIST_HEAD(&psEvenObjectList->sList);

	rwlock_init(&psEvenObjectList->sLock);
	atomic_set(&psEvenObjectList->sEventSignalCount, 0);

	*phEventObjectList = (IMG_HANDLE *) psEvenObjectList;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	LinuxEventObjectListDestroy

 @Description

 Linux wait object list destruction

 @Input    hOSEventKM : Event object list handle

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectListDestroy(IMG_HANDLE hEventObjectList)
{
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psEvenObjectList = (PVRSRV_LINUX_EVENT_OBJECT_LIST *) hEventObjectList;

	if (psEvenObjectList)
	{
		if (!list_empty(&psEvenObjectList->sList))
		{
			 PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectListDestroy: Event List is not empty"));
			 return PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
		}
		OSFreeMem(psEvenObjectList);
		/*not nulling pointer, copy on stack*/
	}
	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	LinuxEventObjectDelete

 @Description

 Linux wait object removal

 @Input    hOSEventObject : Event object handle

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectDelete(IMG_HANDLE hOSEventObject)
{
	if (hOSEventObject)
	{
		PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)hOSEventObject;
		PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = psLinuxEventObject->psLinuxEventObjectList;

		write_lock_bh(&psLinuxEventObjectList->sLock);
		list_del(&psLinuxEventObject->sList);
		write_unlock_bh(&psLinuxEventObjectList->sLock);

#ifdef LINUX_EVENT_OBJECT_STATS
		OSLockDestroy(psLinuxEventObject->hLock);
#endif

#if defined(DEBUG)
//		PVR_DPF((PVR_DBG_MESSAGE, "LinuxEventObjectDelete: Event object waits: %u", psLinuxEventObject->ui32Stats));
#endif

		OSFreeMem(psLinuxEventObject);
		/*not nulling pointer, copy on stack*/

		return PVRSRV_OK;
	}
	return PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
}

/*!
******************************************************************************

 @Function	LinuxEventObjectAdd

 @Description

 Linux wait object addition

 @Input    hOSEventObjectList : Event object list handle
 @Output   phOSEventObject : Pointer to the event object handle

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectAdd(IMG_HANDLE hOSEventObjectList, IMG_HANDLE *phOSEventObject)
 {
	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = (PVRSRV_LINUX_EVENT_OBJECT_LIST*)hOSEventObjectList;

	/* allocate completion variable */
	psLinuxEventObject = OSAllocMem(sizeof(*psLinuxEventObject));
	if (psLinuxEventObject == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectAdd: failed to allocate memory"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	INIT_LIST_HEAD(&psLinuxEventObject->sList);

	/* Start with the timestamp at which event object was added to the list */
	psLinuxEventObject->ui32EventSignalCountPrevious = atomic_read(&psLinuxEventObjectList->sEventSignalCount);

#ifdef LINUX_EVENT_OBJECT_STATS
	PVR_LOG_RETURN_IF_ERROR(OSLockCreate(&psLinuxEventObject->hLock), "OSLockCreate");
	psLinuxEventObject->ui32ScheduleAvoided = 0;
	psLinuxEventObject->ui32ScheduleCalled = 0;
	psLinuxEventObject->ui32ScheduleSleptFully = 0;
	psLinuxEventObject->ui32ScheduleSleptPartially = 0;
	psLinuxEventObject->ui32ScheduleReturnedImmediately = 0;
#endif

#if defined(DEBUG)
	psLinuxEventObject->ui32Stats = 0;
#endif
	init_waitqueue_head(&psLinuxEventObject->sWait);

	psLinuxEventObject->psLinuxEventObjectList = psLinuxEventObjectList;

	write_lock_bh(&psLinuxEventObjectList->sLock);
	list_add(&psLinuxEventObject->sList, &psLinuxEventObjectList->sList);
	write_unlock_bh(&psLinuxEventObjectList->sLock);

	*phOSEventObject = psLinuxEventObject;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	LinuxEventObjectSignal

 @Description

 Linux wait object signaling function

 @Input    hOSEventObjectList : Event object list handle

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectSignal(IMG_HANDLE hOSEventObjectList)
{
	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = (PVRSRV_LINUX_EVENT_OBJECT_LIST*)hOSEventObjectList;
	struct list_head *psListEntry, *psListEntryTemp, *psList;
	psList = &psLinuxEventObjectList->sList;

	/* Move the timestamp ahead for this call, so a potential "Wait" from any
	 * EventObject/s doesn't wait for the signal to occur before returning. Early
	 * setting/incrementing of timestamp reduces the window where a concurrent
	 * "Wait" call might block while "this" Signal call is being processed */
	atomic_inc(&psLinuxEventObjectList->sEventSignalCount);

	read_lock_bh(&psLinuxEventObjectList->sLock);
	list_for_each_safe(psListEntry, psListEntryTemp, psList)
	{
		psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)list_entry(psListEntry, PVRSRV_LINUX_EVENT_OBJECT, sList);
		wake_up_interruptible(&psLinuxEventObject->sWait);
	}
	read_unlock_bh(&psLinuxEventObjectList->sLock);

	return PVRSRV_OK;
}

static void _TryToFreeze(void)
{
	/* if we reach zero it means that all of the threads called try_to_freeze */
	LinuxBridgeNumActiveKernelThreadsDecrement();

	/* Returns true if the thread was frozen, should we do anything with this
	* information? What do we return? Which one is the error case? */
	try_to_freeze();

	LinuxBridgeNumActiveKernelThreadsIncrement();
}

void LinuxEventObjectDumpDebugInfo(IMG_HANDLE hOSEventObject)
{
#ifdef LINUX_EVENT_OBJECT_STATS
	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)hOSEventObject;

	OSLockAcquire(psLinuxEventObject->hLock);
	PVR_LOG(("%s: EvObj(%p) schedule: Avoided(%u) Called(%u) ReturnedImmediately(%u) SleptFully(%u) SleptPartially(%u)",
	         __func__, psLinuxEventObject, psLinuxEventObject->ui32ScheduleAvoided,
			 psLinuxEventObject->ui32ScheduleCalled, psLinuxEventObject->ui32ScheduleReturnedImmediately,
			 psLinuxEventObject->ui32ScheduleSleptFully, psLinuxEventObject->ui32ScheduleSleptPartially));
	OSLockRelease(psLinuxEventObject->hLock);
#else
	PVR_LOG(("%s: LINUX_EVENT_OBJECT_STATS disabled!", __func__));
#endif
}

/*!
******************************************************************************

 @Function	LinuxEventObjectWait

 @Description

 Linux wait object routine

 @Input    hOSEventObject : Event object handle

 @Input   ui64Timeoutus : Time out value in usec

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectWait(IMG_HANDLE hOSEventObject,
                                  IMG_UINT64 ui64Timeoutus,
                                  IMG_BOOL bFreezable)
{
	IMG_UINT32 ui32EventSignalCount;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_UINT32 ui32Remainder;
	long timeOutJiffies;
#ifdef LINUX_EVENT_OBJECT_STATS
	long totalTimeoutJiffies;
	IMG_BOOL bScheduleCalled = IMG_FALSE;
#endif

	DEFINE_WAIT(sWait);

	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *) hOSEventObject;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = psLinuxEventObject->psLinuxEventObjectList;

	/* Check if the driver is good shape */
	if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	/* usecs_to_jiffies only takes an uint. So if our timeout is bigger than an
	 * uint use the msec version. With such a long timeout we really don't need
	 * the high resolution of usecs. */
	if (ui64Timeoutus > 0xffffffffULL)
		timeOutJiffies = msecs_to_jiffies(OSDivide64(ui64Timeoutus, 1000, &ui32Remainder));
	else
		timeOutJiffies = usecs_to_jiffies(ui64Timeoutus);

#ifdef LINUX_EVENT_OBJECT_STATS
	totalTimeoutJiffies = timeOutJiffies;
#endif

	do
	{
		prepare_to_wait(&psLinuxEventObject->sWait, &sWait, TASK_INTERRUPTIBLE);
		ui32EventSignalCount = (IMG_UINT32) atomic_read(&psLinuxEventObjectList->sEventSignalCount);

		if (psLinuxEventObject->ui32EventSignalCountPrevious != ui32EventSignalCount)
		{
			/* There is a pending event signal i.e. LinuxEventObjectSignal()
			 * was called on the event object since the last time we checked.
			 * Return without waiting. */
			break;
		}

		if (signal_pending(current))
		{
			/* There is an OS signal pending so return.
			 * This allows to kill/interrupt user space processes which
			 * are waiting on this event object. */
			break;
		}

#ifdef LINUX_EVENT_OBJECT_STATS
		bScheduleCalled = IMG_TRUE;
#endif
		timeOutJiffies = schedule_timeout(timeOutJiffies);

		if (bFreezable)
		{
			_TryToFreeze();
		}

#if defined(DEBUG)
		psLinuxEventObject->ui32Stats++;
#endif


	} while (timeOutJiffies);

	finish_wait(&psLinuxEventObject->sWait, &sWait);

	psLinuxEventObject->ui32EventSignalCountPrevious = ui32EventSignalCount;

#ifdef LINUX_EVENT_OBJECT_STATS
	OSLockAcquire(psLinuxEventObject->hLock);
	if (bScheduleCalled)
	{
		psLinuxEventObject->ui32ScheduleCalled++;
		if (totalTimeoutJiffies == timeOutJiffies)
		{
			psLinuxEventObject->ui32ScheduleReturnedImmediately++;
		}
		else if (timeOutJiffies == 0)
		{
			psLinuxEventObject->ui32ScheduleSleptFully++;
		}
		else
		{
			psLinuxEventObject->ui32ScheduleSleptPartially++;
		}
	}
	else
	{
		psLinuxEventObject->ui32ScheduleAvoided++;
	}
	OSLockRelease(psLinuxEventObject->hLock);
#endif

	if (signal_pending(current))
	{
		return PVRSRV_ERROR_INTERRUPTED;
	}
	else
	{
		return timeOutJiffies ? PVRSRV_OK : PVRSRV_ERROR_TIMEOUT;
	}
}

#if defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP)

PVRSRV_ERROR LinuxEventObjectWaitUntilSignalled(IMG_HANDLE hOSEventObject)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	DEFINE_WAIT(sWait);

	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject =
			(PVRSRV_LINUX_EVENT_OBJECT *) hOSEventObject;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList =
			psLinuxEventObject->psLinuxEventObjectList;

	/* Check if the driver is in good shape */
	if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	prepare_to_wait(&psLinuxEventObject->sWait, &sWait, TASK_INTERRUPTIBLE);

	if (psLinuxEventObject->ui32EventSignalCountPrevious !=
	    (IMG_UINT32) atomic_read(&psLinuxEventObjectList->sEventSignalCount))
	{
		/* There is a pending signal, so return without waiting */
		goto finish;
	}

	schedule();

	_TryToFreeze();

finish:
	finish_wait(&psLinuxEventObject->sWait, &sWait);

	psLinuxEventObject->ui32EventSignalCountPrevious =
			(IMG_UINT32) atomic_read(&psLinuxEventObjectList->sEventSignalCount);

	return PVRSRV_OK;
}

#endif /* defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP) */
