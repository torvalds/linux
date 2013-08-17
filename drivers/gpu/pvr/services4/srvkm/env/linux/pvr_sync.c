/*************************************************************************/ /*!
@File           pvr_sync.c
@Title          Kernel sync driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Version numbers and strings for PVR Consumer services
				components.
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

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "pvr_sync.h"
#include "mutex.h"
#include "lock.h"

/* The OS event object does not know which timeline a sync object is triggered
 * so we must keep a list to traverse all syncs for completion */
/* FIXME - Can this be avoided? */
static LIST_HEAD(gTimelineList);
static DEFINE_SPINLOCK(gTimelineListLock);

/* Keep track of any exports we made of the fences. To stop the handle
 * tables getting filled with dead fences, we need to remove the sync
 * handles from the per process data when the fence is released.
 */
static LIST_HEAD(gExportList);

struct ServicesExportCtx
{
	IMG_HANDLE			hKernelServices;
	IMG_HANDLE			hSyncInfo;
	struct sync_fence	*psFence;
	struct list_head	sExportList;
};

static int
PVRSyncCompareSyncInfos(IMG_INT64 i64TimestampA, IMG_INT64 i64TimestampB)
{
	/* FIXME: The timestamp may not be a reliable method of determining
	 *        sync finish order between processes
	 */
	if (i64TimestampA == i64TimestampB)
		return 0;
	if (i64TimestampA < i64TimestampB)
		return 1;
	else
		return -1;
}

struct sync_pt *PVRSyncCreateSync(struct PVR_SYNC_TIMELINE *obj,
								  PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo)
{
	struct PVR_SYNC *psPt = (struct PVR_SYNC *)
		sync_pt_create(&obj->obj, sizeof(struct PVR_SYNC));

	psPt->psKernelSyncInfo = psKernelSyncInfo;
	/* S.LSI */
	psPt->magic = 0x3141592;
	PVRSRVAcquireSyncInfoKM(psKernelSyncInfo);

	return (struct sync_pt *)psPt;
}

static void PVRSyncFreeSync(struct sync_pt *sync_pt)
{
	struct PVR_SYNC *psPt = (struct PVR_SYNC *)sync_pt;
	struct sync_fence *psFence = sync_pt->fence;
    struct list_head *psEntry, *psTmp;

	LinuxLockMutex(&gPVRSRVLock);

    list_for_each_safe(psEntry, psTmp, &gExportList)
	{
		struct ServicesExportCtx *psExportCtx =
            container_of(psEntry, struct ServicesExportCtx, sExportList);
		PVRSRV_PER_PROCESS_DATA *psPerProc;
		PVRSRV_ERROR eError;

		/* Not this fence -- don't care */
		if(psExportCtx->psFence != psFence)
			continue;

		eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
									(IMG_PVOID *)&psPerProc,
									psExportCtx->hKernelServices,
									PVRSRV_HANDLE_TYPE_PERPROC_DATA);

		/* The handle's probably invalid because the context has already
	 	 * had resman run over it. In that case, the handle table should
		 * have been freed, and there's nothing for us to do.
		 */
		if(eError == PVRSRV_OK)
		{
			eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
										 psExportCtx->hSyncInfo,
										 PVRSRV_HANDLE_TYPE_SYNC_INFO);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: PVRSRVReleaseHandle failed",
										  __func__));
			}
		}

		list_del(&psExportCtx->sExportList);
		kfree(psExportCtx);
	}

	LinuxUnLockMutex(&gPVRSRVLock);

	PVRSRVReleaseSyncInfoKM(psPt->psKernelSyncInfo);
}

static struct sync_pt *PVRSyncDup(struct sync_pt *sync_pt)
{
	struct PVR_SYNC *psPt = (struct PVR_SYNC *)sync_pt;
	struct PVR_SYNC_TIMELINE *psObj =
		(struct PVR_SYNC_TIMELINE *)sync_pt->parent;

	return (struct sync_pt *)PVRSyncCreateSync(psObj, psPt->psKernelSyncInfo);
}

static int PVRSyncHasSignaled(struct sync_pt *sync_pt)
{
	struct PVR_SYNC *psPt = (struct PVR_SYNC *)sync_pt;
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo = psPt->psKernelSyncInfo;

	return psKernelSyncInfo->psSyncData->ui32WriteOpsPending ==
		   psKernelSyncInfo->psSyncData->ui32WriteOpsComplete ? 1 : 0;
}

static int PVRSyncCompare(struct sync_pt *a, struct sync_pt *b)
{
	return PVRSyncCompareSyncInfos(ktime_to_ns(a->timestamp),
								   ktime_to_ns(b->timestamp));
}

static void PVRSyncPrintTimeline(struct seq_file *s, struct sync_timeline *sync_timeline)
{
	/* FIXME: Implement? */
}

static void PVRSyncPrint(struct seq_file *s, struct sync_pt *sync_pt)
{
	/* FIXME: Implement? */
}

static int
PVRSyncFillDriverData(struct sync_pt *sync_pt, void *data, int size)
{
	struct PVR_SYNC *psPt = (struct PVR_SYNC *)sync_pt;

	if (size < sizeof(psPt->psKernelSyncInfo))
		return -ENOMEM;

	memcpy(data, &psPt->psKernelSyncInfo, sizeof(psPt->psKernelSyncInfo));

	return sizeof(psPt->psKernelSyncInfo);
}

struct sync_timeline_ops PVR_SYNC_TIMELINE_ops =
{
	.driver_name		= "pvr_sync",
	.dup				= PVRSyncDup,
	.has_signaled		= PVRSyncHasSignaled,
	.compare			= PVRSyncCompare,
	.print_obj			= PVRSyncPrintTimeline,
	.print_pt			= PVRSyncPrint,
	.fill_driver_data	= PVRSyncFillDriverData,
	.free_pt			= PVRSyncFreeSync,
};

struct PVR_SYNC_TIMELINE *PVRSyncCreateTimeline(const IMG_CHAR *pszName)
{
	return (struct PVR_SYNC_TIMELINE *)
				sync_timeline_create(&PVR_SYNC_TIMELINE_ops,
								     sizeof(struct PVR_SYNC_TIMELINE),
		    						 pszName);
}

static int PVRSyncOpen(struct inode *inode, struct file *file)
{
	struct PVR_SYNC_TIMELINE *psTimeline;
	IMG_CHAR task_comm[TASK_COMM_LEN];
	unsigned long ulFlags;

	get_task_comm(task_comm, current);

	psTimeline = PVRSyncCreateTimeline(task_comm);
	if (!psTimeline)
		return -ENOMEM;

	spin_lock_irqsave(&gTimelineListLock, ulFlags);
	list_add_tail(&psTimeline->sTimelineList, &gTimelineList);
	spin_unlock_irqrestore(&gTimelineListLock, ulFlags);

	file->private_data = psTimeline;
	return 0;
}

static int PVRSyncRelease(struct inode *inode, struct file *file)
{
	struct PVR_SYNC_TIMELINE *psTimeline = file->private_data;
	unsigned long ulFlags;

	spin_lock_irqsave(&gTimelineListLock, ulFlags);
	list_del(&psTimeline->sTimelineList);
	spin_unlock_irqrestore(&gTimelineListLock, ulFlags);
	sync_timeline_destroy(&psTimeline->obj);

	return 0;
}

static long
PVRSyncIOCTLCreate(struct PVR_SYNC_TIMELINE *psObj, unsigned long ulArg)
{
	IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;
	struct PVR_SYNC_CREATE_IOCTL_DATA sData;
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	int err = 0, iFd = get_unused_fd();
	struct sync_fence *psFence;
	struct sync_pt *psPt;
	PVRSRV_ERROR eError;

	if (iFd <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to find unused fd (%d)",
								__func__, iFd));
		err = -EFAULT;
		goto err_out;
	}

	/* FIXME: Add AccessOK checks */

	if (copy_from_user(&sData, (void __user *)ulArg, sizeof(sData)))
	{
		err = -EFAULT;
		goto err_out;
	}

	LinuxLockMutex(&gPVRSRVLock);
		
	eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
								(IMG_PVOID *)&psPerProc,
								sData.hKernelServices,
								PVRSRV_HANDLE_TYPE_PERPROC_DATA);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid kernel services handle (%d)",
								__func__, eError));
		err = -EFAULT;
		goto err_unlock;
	}

	if(psPerProc->ui32PID != ui32PID)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Process %d tried to access data "
								"belonging to process %d", __func__,
								ui32PID, psPerProc->ui32PID));
		err = -EFAULT;
		goto err_unlock;
	}

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
								(IMG_PVOID *)&psKernelSyncInfo,
								sData.hSyncInfo,
								PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to lookup SYNC_INFO handle", __func__));
		err = -EFAULT;
		goto err_unlock;
	}

	LinuxUnLockMutex(&gPVRSRVLock);

	psPt = PVRSyncCreateSync(psObj, psKernelSyncInfo);
	if (!psPt) {
		err = -ENOMEM;
		goto err_out;
	}

	sData.name[sizeof(sData.name) - 1] = '\0';
	psFence = sync_fence_create(sData.name, psPt);
	if (!psFence)
	{
		sync_pt_free(psPt);
		err = -ENOMEM;
		goto err_out;
	}

	sData.iFenceFD = iFd;
	if (copy_to_user((void __user *)ulArg, &sData, sizeof(sData)))
	{
		sync_fence_put(psFence);
		err = -EFAULT;
		goto err_out;
	}

	sync_fence_install(psFence, iFd);

err_out:
	return err;

err_unlock:
	LinuxUnLockMutex(&gPVRSRVLock);
	goto err_out;
}

static long
PVRSyncIOCTLImport(struct PVR_SYNC_TIMELINE *psObj, unsigned long ulArg)
{
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo = NULL;
	IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();
	struct PVR_SYNC_IMPORT_IOCTL_DATA sData;
	struct ServicesExportCtx *psExportCtx;
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	struct sync_fence *psFence;
	IMG_HANDLE hSyncInfo;
	PVRSRV_ERROR eError;
	int err = -EFAULT;

	/* FIXME: Add AccessOK checks */

	if (copy_from_user(&sData, (void __user *)ulArg, sizeof(sData)))
		goto err_out;

	psFence = sync_fence_fdget(sData.iFenceFD);
	if(!psFence)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get fence from fd", __func__));
		goto err_out;
	}

	/* FIXME: We shouldn't be using the sync driver's list heads
	 * like this, but currently there's no other way to get back
	 * to a sync_pt from a sync_fence.
	 */
	{
	    struct list_head *psEntry;

	    list_for_each(psEntry, &psFence->pt_list_head)
		{
			struct PVR_SYNC *psPt = (struct PVR_SYNC *)
	            container_of(psEntry, struct sync_pt, pt_list);
			psKernelSyncInfo = psPt->psKernelSyncInfo;
			break;
		}
	}

	/* We can put the fence now because the psKernelSyncInfo is
	 * independently refcounted.
	 */
	sync_fence_put(psFence);

	LinuxLockMutex(&gPVRSRVLock);

	eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
								(IMG_PVOID *)&psPerProc,
								sData.hKernelServices,
								PVRSRV_HANDLE_TYPE_PERPROC_DATA);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid kernel services handle (%d)",
								__func__, eError));
		goto err_unlock;
	}

	if(psPerProc->ui32PID != ui32PID)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Process %d tried to access data "
								"belonging to process %d", __func__,
								ui32PID, psPerProc->ui32PID));
		goto err_unlock;
	}

	/* Use MULTI in case userspace keeps exporting the same fence. */
	eError = PVRSRVAllocHandle(psPerProc->psHandleBase,
							   &hSyncInfo,
							   psKernelSyncInfo,
							   PVRSRV_HANDLE_TYPE_SYNC_INFO,
							   PVRSRV_HANDLE_ALLOC_FLAG_MULTI);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to alloc handle for SYNC_INFO",
								__func__));
		goto err_unlock;
	}

	sData.hSyncInfo = hSyncInfo;
	if (copy_to_user((void __user *)ulArg, &sData, sizeof(sData)))
		goto err_release_handle;

	psExportCtx = kmalloc(sizeof(*psExportCtx), GFP_KERNEL);
	if(!psExportCtx)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate psExportCtx",
								__func__));
		err = -ENOMEM;
		goto err_release_handle;
	}

	psExportCtx->hKernelServices = sData.hKernelServices;
	psExportCtx->hSyncInfo = hSyncInfo;

	list_add_tail(&psExportCtx->sExportList, &gExportList);

	err = 0;
err_unlock:
	LinuxUnLockMutex(&gPVRSRVLock);
err_out:
	return err;

err_release_handle:
	PVRSRVReleaseHandle(psPerProc->psHandleBase,
						hSyncInfo,
						PVRSRV_HANDLE_TYPE_SYNC_INFO);
	goto err_unlock;
}

static long
PVRSyncIOCTL(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct PVR_SYNC_TIMELINE *psTimeline = file->private_data;

	switch (cmd)
	{
		case PVR_SYNC_IOC_CREATE_FENCE:
			return PVRSyncIOCTLCreate(psTimeline, arg);
		case PVR_SYNC_IOC_IMPORT_FENCE:
			return PVRSyncIOCTLImport(psTimeline, arg);
		default:
			return -ENOTTY;
	}
}

static const struct file_operations sPVRSyncFOps =
{
	.owner			= THIS_MODULE,
	.open			= PVRSyncOpen,
	.release		= PVRSyncRelease,
	.unlocked_ioctl	= PVRSyncIOCTL,
};

static struct miscdevice sPVRSyncDev =
{
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "pvr_sync",
	.fops	= &sPVRSyncFOps,
};

int PVRSyncDeviceInit(void)
{
	return misc_register(&sPVRSyncDev);
}

void PVRSyncDeviceDeInit(void)
{
	misc_deregister(&sPVRSyncDev);
}

void PVRSyncUpdateAllSyncs(void)
{
	struct list_head *psEntry;
	unsigned long ulFlags;

	spin_lock_irqsave(&gTimelineListLock, ulFlags);

	list_for_each(psEntry, &gTimelineList)
	{
		struct PVR_SYNC_TIMELINE *psTimeline =
			container_of(psEntry, struct PVR_SYNC_TIMELINE, sTimelineList);
		sync_timeline_signal((struct sync_timeline *)psTimeline);
	}

	spin_unlock_irqrestore(&gTimelineListLock, ulFlags);
}
