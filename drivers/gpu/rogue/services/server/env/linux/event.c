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

#include <linux/version.h>
#include <asm/io.h>
#include <asm/page.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)) )
#include <asm/system.h>
#endif
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/hardirq.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#include "img_types.h"
#include "pvrsrv_error.h"
#include "allocmem.h"
#include "mm.h"
#include "mmap.h"
#include "env_data.h"
#include "driverlock.h"
#include "event.h"
#include "pvr_debug.h"
#include "pvrsrv.h"

#include "osfunc.h"

/* Returns pointer to task_struct that belongs to thread which acquired
 * bridge lock. */
extern struct task_struct *OSGetBridgeLockOwner(void);

typedef struct PVRSRV_LINUX_EVENT_OBJECT_LIST_TAG
{
	rwlock_t sLock;
	struct list_head sList;

} PVRSRV_LINUX_EVENT_OBJECT_LIST;


typedef struct PVRSRV_LINUX_EVENT_OBJECT_TAG
{
	atomic_t sTimeStamp;
	IMG_UINT32 ui32TimeStampPrevious;
#if defined(DEBUG)
	IMG_UINT ui32Stats;
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

	psEvenObjectList = OSAllocMem(sizeof(PVRSRV_LINUX_EVENT_OBJECT_LIST));
	if (psEvenObjectList == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectCreate: failed to allocate memory for event list"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	INIT_LIST_HEAD(&psEvenObjectList->sList);

	rwlock_init(&psEvenObjectList->sLock);

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

	PVRSRV_LINUX_EVENT_OBJECT_LIST *psEvenObjectList = (PVRSRV_LINUX_EVENT_OBJECT_LIST *) hEventObjectList ;

	if(psEvenObjectList)
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
	if(hOSEventObject)
	{
		PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)hOSEventObject;
		PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = psLinuxEventObject->psLinuxEventObjectList;

		write_lock_bh(&psLinuxEventObjectList->sLock);
		list_del(&psLinuxEventObject->sList);
		write_unlock_bh(&psLinuxEventObjectList->sLock);

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
	psLinuxEventObject = OSAllocMem(sizeof(PVRSRV_LINUX_EVENT_OBJECT));
	if (psLinuxEventObject == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectAdd: failed to allocate memory "));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	INIT_LIST_HEAD(&psLinuxEventObject->sList);

	atomic_set(&psLinuxEventObject->sTimeStamp, 0);
	psLinuxEventObject->ui32TimeStampPrevious = 0;

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

	read_lock_bh(&psLinuxEventObjectList->sLock);
	list_for_each_safe(psListEntry, psListEntryTemp, psList)
	{

		psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)list_entry(psListEntry, PVRSRV_LINUX_EVENT_OBJECT, sList);

		atomic_inc(&psLinuxEventObject->sTimeStamp);
		wake_up_interruptible(&psLinuxEventObject->sWait);
	}
	read_unlock_bh(&psLinuxEventObjectList->sLock);

	return 	PVRSRV_OK;

}

/*!
******************************************************************************

 @Function	LinuxEventObjectWait

 @Description

 Linux wait object routine

 @Input    hOSEventObject : Event object handle

 @Input   ui32MSTimeout : Time out value in msec

 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectWait(IMG_HANDLE hOSEventObject, IMG_UINT32 ui32MSTimeout)
{
	IMG_UINT32 ui32TimeStamp;
	IMG_BOOL bReleasePVRLock;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	DEFINE_WAIT(sWait);

	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *) hOSEventObject;

	IMG_UINT32 ui32TimeOutJiffies = msecs_to_jiffies(ui32MSTimeout);

	/* Check if the driver is good shape */
	if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	do
	{
		prepare_to_wait(&psLinuxEventObject->sWait, &sWait, TASK_INTERRUPTIBLE);
		ui32TimeStamp = (IMG_UINT32)atomic_read(&psLinuxEventObject->sTimeStamp);

		if(psLinuxEventObject->ui32TimeStampPrevious != ui32TimeStamp)
		{
			break;
		}

		/* Check thread holds the current PVR/bridge lock before obeying the
		 * 'release before deschedule' behaviour. Some threads choose not to
		 * hold the bridge lock in their implementation.
		 */
		bReleasePVRLock = (OSGetReleasePVRLock() && mutex_is_locked(&gPVRSRVLock) && current == OSGetBridgeLockOwner());
		if (bReleasePVRLock == IMG_TRUE)
		{
			OSReleaseBridgeLock();
		}

		ui32TimeOutJiffies = (IMG_UINT32)schedule_timeout((IMG_INT32)ui32TimeOutJiffies);

		if (bReleasePVRLock == IMG_TRUE)
		{
			OSAcquireBridgeLock();
		}

#if defined(DEBUG)
		psLinuxEventObject->ui32Stats++;
#endif


	} while (ui32TimeOutJiffies);

	finish_wait(&psLinuxEventObject->sWait, &sWait);

	psLinuxEventObject->ui32TimeStampPrevious = ui32TimeStamp;

	return ui32TimeOutJiffies ? PVRSRV_OK : PVRSRV_ERROR_TIMEOUT;

}
