/*************************************************************************/ /*!
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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <asm/io.h>
#include <asm/page.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
#include <asm/system.h>
#endif
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/hardirq.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#include "img_types.h"
#include "services_headers.h"
#include "mm.h"
#include "pvrmmap.h"
#include "mmap.h"
#include "env_data.h"
#include "proc.h"
#include "mutex.h"
#include "lock.h"
#include "event.h"

typedef struct PVRSRV_LINUX_EVENT_OBJECT_LIST_TAG
{
   rwlock_t		sLock;
   struct list_head	sList;
   
} PVRSRV_LINUX_EVENT_OBJECT_LIST;


typedef struct PVRSRV_LINUX_EVENT_OBJECT_TAG
{
   	atomic_t	sTimeStamp;
   	IMG_UINT32  ui32TimeStampPrevious;
#if defined(DEBUG)
	IMG_UINT	ui32Stats;
#endif
    wait_queue_head_t sWait;	
	struct list_head        sList;
	IMG_HANDLE		hResItem;				
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
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psEventObjectList;

	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP, sizeof(PVRSRV_LINUX_EVENT_OBJECT_LIST), 
		(IMG_VOID **)&psEventObjectList, IMG_NULL,
		"Linux Event Object List") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectCreate: failed to allocate memory for event list"));		
		return PVRSRV_ERROR_OUT_OF_MEMORY;	
	}

	INIT_LIST_HEAD(&psEventObjectList->sList);

	rwlock_init(&psEventObjectList->sLock);
	
	*phEventObjectList = (IMG_HANDLE *) psEventObjectList;

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

	PVRSRV_LINUX_EVENT_OBJECT_LIST *psEventObjectList = (PVRSRV_LINUX_EVENT_OBJECT_LIST *) hEventObjectList ;

	if(psEventObjectList)	
	{
		IMG_BOOL bListEmpty;

		read_lock(&psEventObjectList->sLock);
		bListEmpty = list_empty(&psEventObjectList->sList);
		read_unlock(&psEventObjectList->sLock);

		if (!bListEmpty) 
		{
			 PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectListDestroy: Event List is not empty"));
			 return PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
		}

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, sizeof(PVRSRV_LINUX_EVENT_OBJECT_LIST), psEventObjectList, IMG_NULL);
		/*not nulling pointer, copy on stack*/
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	LinuxEventObjectDelete
 
 @Description 
 
 Linux wait object removal
 
 @Input    hOSEventObjectList : Event object list handle 
 @Input    hOSEventObject : Event object handle 
 @Input    bResManCallback : Called from the resman
 
 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
PVRSRV_ERROR LinuxEventObjectDelete(IMG_HANDLE hOSEventObjectList, IMG_HANDLE hOSEventObject)
{
	if(hOSEventObjectList)
	{
		if(hOSEventObject)
		{
			PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)hOSEventObject; 
#if defined(DEBUG)
			PVR_DPF((PVR_DBG_MESSAGE, "LinuxEventObjectListDelete: Event object waits: %u", psLinuxEventObject->ui32Stats));
#endif
			if(ResManFreeResByPtr(psLinuxEventObject->hResItem, CLEANUP_WITH_POLL) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectDelete: ResManFreeResByPtr failed %d", PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT));
				PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psLinuxEventObject->hResItem));
				return PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
			}
			
			return PVRSRV_OK;
		}
	}
	return PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;

}

/*!
******************************************************************************

 @Function	LinuxEventObjectDeleteCallback
 
 @Description 
 
 Linux wait object removal
 
 @Input    hOSEventObject : Event object handle 
 
 @Return   PVRSRV_ERROR  :  Error code

******************************************************************************/
static PVRSRV_ERROR LinuxEventObjectDeleteCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bForceCleanup)
{
	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = pvParam;
	PVRSRV_LINUX_EVENT_OBJECT_LIST *psLinuxEventObjectList = psLinuxEventObject->psLinuxEventObjectList;
	unsigned long ulLockFlags;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bForceCleanup);

	write_lock_irqsave(&psLinuxEventObjectList->sLock, ulLockFlags);
	list_del(&psLinuxEventObject->sList);
	write_unlock_irqrestore(&psLinuxEventObjectList->sLock, ulLockFlags);

#if defined(DEBUG)
	PVR_DPF((PVR_DBG_MESSAGE, "LinuxEventObjectDeleteCallback: Event object waits: %u", psLinuxEventObject->ui32Stats));
#endif	

	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, sizeof(PVRSRV_LINUX_EVENT_OBJECT), psLinuxEventObject, IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return PVRSRV_OK;
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
	IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	unsigned long ulLockFlags;

	psPerProc = PVRSRVPerProcessData(ui32PID);
	if (psPerProc == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectAdd: Couldn't find per-process data"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* allocate completion variable */
	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP, sizeof(PVRSRV_LINUX_EVENT_OBJECT), 
		(IMG_VOID **)&psLinuxEventObject, IMG_NULL,
		"Linux Event Object") != PVRSRV_OK)
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

	psLinuxEventObject->hResItem = ResManRegisterRes(psPerProc->hResManContext,
													 RESMAN_TYPE_EVENT_OBJECT,
													 psLinuxEventObject,
													 0,
													 &LinuxEventObjectDeleteCallback);	

	write_lock_irqsave(&psLinuxEventObjectList->sLock, ulLockFlags);
	list_add(&psLinuxEventObject->sList, &psLinuxEventObjectList->sList);
	write_unlock_irqrestore(&psLinuxEventObjectList->sLock, ulLockFlags);
	
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
	struct list_head *psListEntry, *psList;

	psList = &psLinuxEventObjectList->sList;

	/*
	 * We don't take the write lock in interrupt context, so we don't
	 * need to use read_lock_irqsave.
	 */
	read_lock(&psLinuxEventObjectList->sLock);
	list_for_each(psListEntry, psList) 
	{       	

		psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *)list_entry(psListEntry, PVRSRV_LINUX_EVENT_OBJECT, sList);	
		
		atomic_inc(&psLinuxEventObject->sTimeStamp);	
	 	wake_up_interruptible(&psLinuxEventObject->sWait);
	}
	read_unlock(&psLinuxEventObjectList->sLock);

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
	DEFINE_WAIT(sWait);

	PVRSRV_LINUX_EVENT_OBJECT *psLinuxEventObject = (PVRSRV_LINUX_EVENT_OBJECT *) hOSEventObject;

	IMG_UINT32 ui32TimeOutJiffies = msecs_to_jiffies(ui32MSTimeout);
	
	do	
	{
		prepare_to_wait(&psLinuxEventObject->sWait, &sWait, TASK_INTERRUPTIBLE);
		ui32TimeStamp = (IMG_UINT32)atomic_read(&psLinuxEventObject->sTimeStamp);
   	
		if(psLinuxEventObject->ui32TimeStampPrevious != ui32TimeStamp)
		{
			break;
		}

		LinuxUnLockMutex(&gPVRSRVLock);		

		ui32TimeOutJiffies = (IMG_UINT32)schedule_timeout((IMG_INT32)ui32TimeOutJiffies);
		
		LinuxLockMutex(&gPVRSRVLock);
#if defined(DEBUG)
		psLinuxEventObject->ui32Stats++;
#endif			

		
	} while (ui32TimeOutJiffies);

	finish_wait(&psLinuxEventObject->sWait, &sWait);	

	psLinuxEventObject->ui32TimeStampPrevious = ui32TimeStamp;

	return ui32TimeOutJiffies ? PVRSRV_OK : PVRSRV_ERROR_TIMEOUT;

}

