/*
 * @File        pvr_sync_file.c
 * @Title       Kernel driver for Android's sync mechanism
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "services_kernel_client.h"
#include "pvr_drv.h"
#include "pvr_sync.h"
#include "pvr_fence.h"
#include "pvr_counting_timeline.h"

#include "linux_sw_sync.h"

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sync_file.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

/* This header must always be included last */
#include "kernel_compatibility.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)) && !defined(CHROMIUMOS_KERNEL)
#define sync_file_user_name(s)	((s)->name)
#else
#define sync_file_user_name(s)	((s)->user_name)
#endif

#define PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile, fmt, ...) \
	do {                                                             \
		if (pfnDumpDebugPrintf)                                  \
			pfnDumpDebugPrintf(pvDumpDebugFile, fmt,         \
					   ## __VA_ARGS__);              \
		else                                                     \
			pr_err(fmt "\n", ## __VA_ARGS__);                \
	} while (0)

#define	FILE_NAME "pvr_sync_file"

struct sw_sync_create_fence_data {
	__u32 value;
	char name[32];
	__s32 fence;
};
#define SW_SYNC_IOC_MAGIC 'W'
#define SW_SYNC_IOC_CREATE_FENCE \
	(_IOWR(SW_SYNC_IOC_MAGIC, 0, struct sw_sync_create_fence_data))
#define SW_SYNC_IOC_INC _IOW(SW_SYNC_IOC_MAGIC, 1, __u32)

/* Global data for the sync driver */
static struct {
	void *dev_cookie;
	struct workqueue_struct *fence_status_wq;
	struct pvr_fence_context *foreign_fence_context;
	PFN_SYNC_CHECKPOINT_STRUCT sync_checkpoint_ops;
} pvr_sync_data;

#if defined(NO_HARDWARE)
static DEFINE_MUTEX(pvr_timeline_active_list_lock);
static struct list_head pvr_timeline_active_list;
#endif

static const struct file_operations pvr_sync_fops;

/* This is the actual timeline metadata. We might keep this around after the
 * base sync driver has destroyed the pvr_sync_timeline_wrapper object.
 */
struct pvr_sync_timeline {
	char name[32];
	struct file *file;
	bool is_sw;
	/* Fence context used for hw fences */
	struct pvr_fence_context *hw_fence_context;
	/* Timeline and context for sw fences */
	struct pvr_counting_fence_timeline *sw_fence_timeline;
#if defined(NO_HARDWARE)
	/* List of all timelines (used to advance all timelines in nohw builds) */
	struct list_head list;
#endif
};

static
void pvr_sync_free_checkpoint_list_mem(void *mem_ptr)
{
	kfree(mem_ptr);
}

#if defined(NO_HARDWARE)
/* function used to signal pvr fence in nohw builds */
static
void pvr_sync_nohw_signal_fence(void *fence_data_to_signal)
{
	struct pvr_sync_timeline *this_timeline;

	mutex_lock(&pvr_timeline_active_list_lock);
	list_for_each_entry(this_timeline, &pvr_timeline_active_list, list) {
		pvr_fence_context_signal_fences_nohw(this_timeline->hw_fence_context);
	}
	mutex_unlock(&pvr_timeline_active_list_lock);
}
#endif

static bool is_pvr_timeline(struct file *file)
{
	return file->f_op == &pvr_sync_fops;
}

static struct pvr_sync_timeline *pvr_sync_timeline_fget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return NULL;

	if (!is_pvr_timeline(file)) {
		fput(file);
		return NULL;
	}

	return file->private_data;
}

static void pvr_sync_timeline_fput(struct pvr_sync_timeline *timeline)
{
	fput(timeline->file);
}

/* ioctl and fops handling */

static int pvr_sync_open(struct inode *inode, struct file *file)
{
	struct pvr_sync_timeline *timeline;
	char task_comm[TASK_COMM_LEN];
	int err = -ENOMEM;

	get_task_comm(task_comm, current);

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		goto err_out;

	strlcpy(timeline->name, task_comm, sizeof(timeline->name));
	timeline->file = file;
	timeline->is_sw = false;

	file->private_data = timeline;
	err = 0;
err_out:
	return err;
}

static int pvr_sync_close(struct inode *inode, struct file *file)
{
	struct pvr_sync_timeline *timeline = file->private_data;

	if (timeline->sw_fence_timeline) {
		/* This makes sure any outstanding SW syncs are marked as
		 * complete at timeline close time. Otherwise it'll leak the
		 * timeline (as outstanding fences hold a ref) and possibly
		 * wedge the system if something is waiting on one of those
		 * fences
		 */
		pvr_counting_fence_timeline_force_complete(
			timeline->sw_fence_timeline);
		pvr_counting_fence_timeline_put(timeline->sw_fence_timeline);
	}

	if (timeline->hw_fence_context) {
#if defined(NO_HARDWARE)
		mutex_lock(&pvr_timeline_active_list_lock);
		list_del(&timeline->list);
		mutex_unlock(&pvr_timeline_active_list_lock);
#endif
		pvr_fence_context_destroy(timeline->hw_fence_context);
	}

	kfree(timeline);

	return 0;
}

/*
 * This is the function that kick code will call in order to 'finalise' a
 * created output fence just prior to returning from the kick function.
 * The OS native sync code needs to implement a function meeting this
 * specification - the implementation may be a nop if the OS does not need
 * to perform any actions at this point.
 *
 * Input: fence_fd            The PVRSRV_FENCE to be 'finalised'. This value
 *                            will have been returned by an earlier call to
 *                            pvr_sync_create_fence().
 * Input: finalise_data       The finalise data returned by an earlier call
 *                            to pvr_sync_create_fence().
 */
static enum PVRSRV_ERROR_TAG
pvr_sync_finalise_fence(PVRSRV_FENCE fence_fd, void *finalise_data)
{
	struct sync_file *sync_file = finalise_data;
	struct pvr_fence *pvr_fence;

	if (!sync_file || (fence_fd < 0)) {
		pr_err(FILE_NAME ": %s: Invalid input fence\n", __func__);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pvr_fence = to_pvr_fence(sync_file->fence);

	/* pvr fences can be signalled any time after creation */
	dma_fence_enable_sw_signaling(&pvr_fence->base);

	fd_install(fence_fd, sync_file->file);

	return PVRSRV_OK;
}

/*
 * This is the function that kick code will call in order to obtain a new
 * PVRSRV_FENCE from the OS native sync code and the PSYNC_CHECKPOINT used
 * in that fence. The OS native sync code needs to implement a function
 * meeting this specification.
 *
 * Input: fence_name               A string to annotate the fence with (for
 *                                 debug).
 * Input: timeline                 The timeline on which the new fence is to be
 *                                 created.
 * Output: new_fence               The new PVRSRV_FENCE to be returned by the
 *                                 kick call.
 * Output: fence_uid               Unique ID of the update fence.
 * Output: fence_finalise_data     Pointer to data needed to finalise the fence.
 * Output: new_checkpoint_handle   The PSYNC_CHECKPOINT used by the new fence.
 */
static enum PVRSRV_ERROR_TAG
pvr_sync_create_fence(const char *fence_name,
		      PVRSRV_TIMELINE new_fence_timeline,
		      PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
		      PVRSRV_FENCE *new_fence, u64 *fence_uid,
		      void **fence_finalise_data,
		      PSYNC_CHECKPOINT *new_checkpoint_handle,
		      void **timeline_update_sync,
		      __u32 *timeline_update_value)
{
	PVRSRV_ERROR err = PVRSRV_OK;
	PVRSRV_FENCE new_fence_fd = -1;
	struct pvr_sync_timeline *timeline;
	struct pvr_fence *pvr_fence;
	PSYNC_CHECKPOINT checkpoint;
	struct sync_file *sync_file;

	if (new_fence_timeline < 0 || !new_fence || !new_checkpoint_handle
		|| !fence_finalise_data) {
		pr_err(FILE_NAME ": %s: Invalid input params\n", __func__);
		err =  PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	/* We reserve the new fence FD before taking any operations
	 * as we do not want to fail (e.g. run out of FDs)
	 */
	new_fence_fd = get_unused_fd_flags(O_CLOEXEC);
	if (new_fence_fd < 0) {
		pr_err(FILE_NAME ": %s: Failed to get fd\n", __func__);
		err = PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
		goto err_out;
	}

	timeline = pvr_sync_timeline_fget(new_fence_timeline);
	if (!timeline) {
		pr_err(FILE_NAME ": %s: Failed to open supplied timeline fd (%d)\n",
			__func__, new_fence_timeline);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_put_fd;
	}

	if (timeline->is_sw) {
		/* This should never happen! */
		pr_err(FILE_NAME ": %s: Request to create a pvr fence on sw timeline (%d)\n",
			__func__, new_fence_timeline);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_put_timeline;
	}

	if (!timeline->hw_fence_context) {
		/* First time we use this timeline, so create a context. */
		timeline->hw_fence_context =
			pvr_fence_context_create(pvr_sync_data.dev_cookie,
						 pvr_sync_data.fence_status_wq,
						 timeline->name);
		if (!timeline->hw_fence_context) {
			pr_err(FILE_NAME ": %s: Failed to create fence context (%d)\n",
			       __func__, new_fence_timeline);
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_put_timeline;
		}
#if defined(NO_HARDWARE)
		/* Add timeline to active list */
		INIT_LIST_HEAD(&timeline->list);
		mutex_lock(&pvr_timeline_active_list_lock);
		list_add_tail(&timeline->list, &pvr_timeline_active_list);
		mutex_unlock(&pvr_timeline_active_list_lock);
#endif
	}

	pvr_fence = pvr_fence_create(timeline->hw_fence_context,
								 psSyncCheckpointContext,
								 new_fence_timeline,
								 fence_name);
	if (!pvr_fence) {
		pr_err(FILE_NAME ": %s: Failed to create new pvr_fence\n",
			__func__);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_put_timeline;
	}

	checkpoint = pvr_fence_get_checkpoint(pvr_fence);
	if (!checkpoint) {
		pr_err(FILE_NAME ": %s: Failed to get fence checkpoint\n",
			__func__);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_destroy_fence;
	}

	sync_file = sync_file_create(&pvr_fence->base);
	if (!sync_file) {
		pr_err(FILE_NAME ": %s: Failed to create sync_file\n",
			__func__);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_destroy_fence;
	}
	strlcpy(sync_file_user_name(sync_file),
		pvr_fence->name,
		sizeof(sync_file_user_name(sync_file)));
	dma_fence_put(&pvr_fence->base);

	*new_fence = new_fence_fd;
	*fence_finalise_data = sync_file;
	*new_checkpoint_handle = checkpoint;
	*fence_uid = OSGetCurrentClientProcessIDKM();
	*fence_uid = (*fence_uid << 32) | (new_fence_fd & U32_MAX);
	/* not used but don't want to return dangling pointers */
	*timeline_update_sync = NULL;
	*timeline_update_value = 0;

	pvr_sync_timeline_fput(timeline);
err_out:
	return err;

err_destroy_fence:
	pvr_fence_destroy(pvr_fence);
err_put_timeline:
	pvr_sync_timeline_fput(timeline);
err_put_fd:
	put_unused_fd(new_fence_fd);
	*fence_uid = PVRSRV_NO_FENCE;
	goto err_out;
}

/*
 * This is the function that kick code will call in order to 'rollback' a
 * created output fence should an error occur when submitting the kick.
 * The OS native sync code needs to implement a function meeting this
 * specification.
 *
 * Input: fence_to_rollback The PVRSRV_FENCE to be 'rolled back'. The fence
 *                          should be destroyed and any actions taken due to
 *                          its creation that need to be undone should be
 *                          reverted.
 * Input: finalise_data     The finalise data for the fence to be 'rolled back'.
 */
static enum PVRSRV_ERROR_TAG
pvr_sync_rollback_fence_data(PVRSRV_FENCE fence_to_rollback,
			     void *fence_data_to_rollback)
{
	struct sync_file *sync_file = fence_data_to_rollback;
	struct pvr_fence *pvr_fence;

	if (!sync_file || fence_to_rollback < 0) {
		pr_err(FILE_NAME ": %s: Invalid fence (%d)\n", __func__,
			fence_to_rollback);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pvr_fence = to_pvr_fence(sync_file->fence);
	if (!pvr_fence) {
		pr_err(FILE_NAME
			": %s: Non-PVR fence (%p)\n",
			__func__, sync_file->fence);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	fput(sync_file->file);

	put_unused_fd(fence_to_rollback);

	return PVRSRV_OK;
}

/*
 * This is the function that kick code will call in order to obtain a list of
 * the PSYNC_CHECKPOINTs for a given PVRSRV_FENCE passed to a kick function.
 * The OS native sync code will allocate the memory to hold the returned list
 * of PSYNC_CHECKPOINT ptrs. The caller will free this memory once it has
 * finished referencing it.
 *
 * Input: fence                     The input (check) fence
 * Output: nr_checkpoints           The number of PVRSRV_SYNC_CHECKPOINT ptrs
 *                                  returned in the checkpoint_handles
 *                                  parameter.
 * Output: fence_uid                Unique ID of the check fence
 * Input/Output: checkpoint_handles The returned list of PVRSRV_SYNC_CHECKPOINTs.
 */
static enum PVRSRV_ERROR_TAG
pvr_sync_resolve_fence(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
		       PVRSRV_FENCE fence_to_resolve, u32 *nr_checkpoints,
		       PSYNC_CHECKPOINT **checkpoint_handles, u64 *fence_uid)
{
	PSYNC_CHECKPOINT *checkpoints = NULL;
	unsigned int i, num_fences, num_used_fences = 0;
	struct dma_fence **fences = NULL;
	struct dma_fence *fence;
	PVRSRV_ERROR err = PVRSRV_OK;

	if (!nr_checkpoints || !checkpoint_handles || !fence_uid) {
		pr_err(FILE_NAME ": %s: Invalid input checkpoint pointer\n",
			__func__);
		err =  PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	*nr_checkpoints = 0;
	*checkpoint_handles = NULL;
	*fence_uid = 0;

	if (fence_to_resolve < 0)
		goto err_out;

	fence = sync_file_get_fence(fence_to_resolve);
	if (!fence) {
		pr_err(FILE_NAME ": %s: Failed to read sync private data for fd %d\n",
			__func__, fence_to_resolve);
		err = PVRSRV_ERROR_HANDLE_NOT_FOUND;
		goto err_out;
	}

	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);

		fences = array->fences;
		num_fences = array->num_fences;
	} else {
		fences = &fence;
		num_fences = 1;
	}

	checkpoints = kmalloc_array(num_fences, sizeof(PSYNC_CHECKPOINT),
			      GFP_KERNEL);
	if (!checkpoints) {
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_put_fence;
	}
	for (i = 0; i < num_fences; i++) {
		/* Only return the checkpoint if the fence is still active. */
		if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			      &fences[i]->flags)) {
			struct pvr_fence *pvr_fence =
				pvr_fence_create_from_fence(
					pvr_sync_data.foreign_fence_context,
					psSyncCheckpointContext,
					fences[i],
					fence_to_resolve,
					"foreign");
			if (!pvr_fence) {
				pr_err(FILE_NAME ": %s: Failed to create fence\n",
				       __func__);
				err = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto err_free_checkpoints;
			}
			checkpoints[num_used_fences] =
				pvr_fence_get_checkpoint(pvr_fence);
			SyncCheckpointTakeRef(checkpoints[num_used_fences]);
			++num_used_fences;
			dma_fence_put(&pvr_fence->base);
		}
	}
	/* If we don't return any checkpoints, delete the array because
	 * the caller will not.
	 */
	if (num_used_fences == 0) {
		kfree(checkpoints);
		checkpoints = NULL;
	}

	*checkpoint_handles = checkpoints;
	*nr_checkpoints = num_used_fences;
	*fence_uid = OSGetCurrentClientProcessIDKM();
	*fence_uid = (*fence_uid << 32) | (fence_to_resolve & U32_MAX);

err_put_fence:
	dma_fence_put(fence);
err_out:
	return err;

err_free_checkpoints:
	for (i = 0; i < num_used_fences; i++) {
		if (checkpoints[i])
			SyncCheckpointDropRef(checkpoints[i]);
	}
	kfree(checkpoints);
	goto err_put_fence;
}

/*
 * This is the function that driver code will call in order to request the
 * sync implementation to output debug information relating to any sync
 * checkpoints it may have created which appear in the provided array of
 * FW addresses of Unified Fence Objects (UFOs).
 *
 * Input: nr_ufos             The number of FW addresses provided in the
 *                            vaddrs parameter.
 * Input: vaddrs              The array of FW addresses of UFOs. The sync
 *                            implementation should check each of these to
 *                            see if any relate to sync checkpoints it has
 *                            created and where they do output debug information
 *                            pertaining to the native/fallback sync with
 *                            which it is associated.
 */
static u32
pvr_sync_dump_info_on_stalled_ufos(u32 nr_ufos, u32 *vaddrs)
{
	return pvr_fence_dump_info_on_stalled_ufos(pvr_sync_data.foreign_fence_context,
						   nr_ufos,
						   vaddrs);
}

#if defined(PDUMP)
static enum PVRSRV_ERROR_TAG
pvr_sync_fence_get_checkpoints(PVRSRV_FENCE fence_to_pdump, u32 *nr_checkpoints,
				struct _SYNC_CHECKPOINT ***checkpoint_handles)
{
	struct dma_fence **fences = NULL;
	struct dma_fence *fence;
	struct pvr_fence *pvr_fence;
	struct _SYNC_CHECKPOINT **checkpoints = NULL;
	unsigned int i, num_fences, num_used_fences = 0;
	enum PVRSRV_ERROR_TAG err;

	if (fence_to_pdump < 0) {
		err =  PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	if (!nr_checkpoints || !checkpoint_handles) {
		pr_err(FILE_NAME ": %s: Invalid input checkpoint pointer\n",
			__func__);
		err =  PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	fence = sync_file_get_fence(fence_to_pdump);
	if (!fence) {
		pr_err(FILE_NAME ": %s: Failed to read sync private data for fd %d\n",
			__func__, fence_to_pdump);
		err = PVRSRV_ERROR_HANDLE_NOT_FOUND;
		goto err_out;
	}

	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);

		fences = array->fences;
		num_fences = array->num_fences;
	} else {
		fences = &fence;
		num_fences = 1;
	}

	checkpoints = kmalloc_array(num_fences, sizeof(*checkpoints),
			      GFP_KERNEL);
	if (!checkpoints) {
		pr_err("pvr_sync_file: %s: Failed to alloc memory for returned list of sync checkpoints\n",
			__func__);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_put_fence;
	}

	for (i = 0; i < num_fences; i++) {
		pvr_fence = to_pvr_fence(fences[i]);
		if (!pvr_fence)
			continue;
		checkpoints[num_used_fences] = pvr_fence_get_checkpoint(pvr_fence);
		++num_used_fences;
	}

	*checkpoint_handles = checkpoints;
	*nr_checkpoints = num_used_fences;
	err =  PVRSRV_OK;

err_put_fence:
	dma_fence_put(fence);
err_out:
	return err;
}
#endif

static long pvr_sync_ioctl_rename(struct pvr_sync_timeline *timeline,
	void __user *user_data)
{
	int err = 0;
	struct pvr_sync_rename_ioctl_data data;

	if (!access_ok(user_data, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	if (copy_from_user(&data, user_data, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	data.szName[sizeof(data.szName) - 1] = '\0';
	strlcpy(timeline->name, data.szName, sizeof(timeline->name));
	if (timeline->hw_fence_context)
		strlcpy(timeline->hw_fence_context->name, data.szName,
			sizeof(timeline->hw_fence_context->name));

err:
	return err;
}

static long pvr_sync_ioctl_force_sw_only(struct pvr_sync_timeline *timeline,
	void **private_data)
{
	/* Already in SW mode? */
	if (timeline->sw_fence_timeline)
		return 0;

	/* Create a sw_sync timeline with the old GPU timeline's name */
	timeline->sw_fence_timeline = pvr_counting_fence_timeline_create(
		pvr_sync_data.dev_cookie,
		timeline->name);
	if (!timeline->sw_fence_timeline)
		return -ENOMEM;

	timeline->is_sw = true;

	return 0;
}

static long pvr_sync_ioctl_sw_create_fence(struct pvr_sync_timeline *timeline,
	void __user *user_data)
{
	struct pvr_sw_sync_create_fence_data data;
	struct sync_file *sync_file;
	int fd = get_unused_fd_flags(O_CLOEXEC);
	struct dma_fence *fence;
	int err = -EFAULT;

	if (fd < 0) {
		pr_err(FILE_NAME ": %s: Failed to find unused fd (%d)\n",
		       __func__, fd);
		err = -EMFILE;
		goto err_out;
	}

	if (copy_from_user(&data, user_data, sizeof(data))) {
		pr_err(FILE_NAME ": %s: Failed copy from user\n", __func__);
		goto err_put_fd;
	}

	fence = pvr_counting_fence_create(timeline->sw_fence_timeline, &data.sync_pt_idx);
	if (!fence) {
		pr_err(FILE_NAME ": %s: Failed to create a sync point (%d)\n",
		       __func__, fd);
		err = -ENOMEM;
		goto err_put_fd;
	}

	sync_file = sync_file_create(fence);
	if (!sync_file) {
		pr_err(FILE_NAME ": %s: Failed to create a sync point (%d)\n",
			__func__, fd);
		err = -ENOMEM;
		goto err_put_fence;
	}

	data.fence = fd;

	if (copy_to_user(user_data, &data, sizeof(data))) {
		pr_err(FILE_NAME ": %s: Failed copy to user\n", __func__);
		goto err_put_fence;
	}

	fd_install(fd, sync_file->file);
	err = 0;

	dma_fence_put(fence);
err_out:
	return err;

err_put_fence:
	dma_fence_put(fence);
err_put_fd:
	put_unused_fd(fd);
	goto err_out;
}

static long pvr_sync_ioctl_sw_inc(struct pvr_sync_timeline *timeline,
				  void __user *user_data)
{
	bool res;
	struct pvr_sw_timeline_advance_data data;

	res = pvr_counting_fence_timeline_inc(timeline->sw_fence_timeline, &data.sync_pt_idx);

	/* pvr_counting_fence_timeline_inc won't allow sw timeline to be
	 * advanced beyond the last defined point
	 */
	if (!res) {
		pr_err("pvr_sync_file: attempt to advance SW timeline beyond last defined point\n");
		return -EPERM;
	}

	if (copy_to_user(user_data, &data, sizeof(data))) {
		pr_err(FILE_NAME ": %s: Failed copy to user\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static long
pvr_sync_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user_data = (void __user *)arg;
	long err = -ENOTTY;
	struct pvr_sync_timeline *timeline = file->private_data;

	if (!timeline->is_sw) {

		switch (cmd) {
		case PVR_SYNC_IOC_RENAME:
			err = pvr_sync_ioctl_rename(timeline, user_data);
			break;
		case PVR_SYNC_IOC_FORCE_SW_ONLY:
			err = pvr_sync_ioctl_force_sw_only(timeline,
				&file->private_data);
			break;
		default:
			break;
		}
	} else {

		switch (cmd) {
		case PVR_SW_SYNC_IOC_CREATE_FENCE:
			err = pvr_sync_ioctl_sw_create_fence(timeline,
							     user_data);
			break;
		case PVR_SW_SYNC_IOC_INC:
			err = pvr_sync_ioctl_sw_inc(timeline, user_data);
			break;
		default:
			break;
		}
	}

	return err;
}

static const struct file_operations pvr_sync_fops = {
	.owner          = THIS_MODULE,
	.open           = pvr_sync_open,
	.release        = pvr_sync_close,
	.unlocked_ioctl = pvr_sync_ioctl,
	.compat_ioctl   = pvr_sync_ioctl,
};

static struct miscdevice pvr_sync_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = PVRSYNC_MODNAME,
	.fops           = &pvr_sync_fops,
};

static void
pvr_sync_debug_request_heading(void *data, u32 verbosity,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	if (DD_VERB_LVL_ENABLED(verbosity, DEBUG_REQUEST_VERBOSITY_MEDIUM))
		PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
				  "------[ Native Fence Sync: timelines ]------");
}

enum PVRSRV_ERROR_TAG pvr_sync_register_functions(void)
{
	/* Register the resolve fence and create fence functions with
	 * sync_checkpoint.c
	 * The pvr_fence context registers its own EventObject callback to
	 * update sync status
	 */
	/* Initialise struct and register with sync_checkpoint.c */
	pvr_sync_data.sync_checkpoint_ops.pfnFenceResolve = pvr_sync_resolve_fence;
	pvr_sync_data.sync_checkpoint_ops.pfnFenceCreate = pvr_sync_create_fence;
	pvr_sync_data.sync_checkpoint_ops.pfnFenceDataRollback = pvr_sync_rollback_fence_data;
	pvr_sync_data.sync_checkpoint_ops.pfnFenceFinalise = pvr_sync_finalise_fence;
#if defined(NO_HARDWARE)
	pvr_sync_data.sync_checkpoint_ops.pfnNoHWUpdateTimelines = pvr_sync_nohw_signal_fence;
#else
	pvr_sync_data.sync_checkpoint_ops.pfnNoHWUpdateTimelines = NULL;
#endif
	pvr_sync_data.sync_checkpoint_ops.pfnFreeCheckpointListMem =
		pvr_sync_free_checkpoint_list_mem;
	pvr_sync_data.sync_checkpoint_ops.pfnDumpInfoOnStalledUFOs =
		pvr_sync_dump_info_on_stalled_ufos;
	strlcpy(pvr_sync_data.sync_checkpoint_ops.pszImplName, "pvr_sync_file",
		SYNC_CHECKPOINT_IMPL_MAX_STRLEN);
#if defined(PDUMP)
	pvr_sync_data.sync_checkpoint_ops.pfnSyncFenceGetCheckpoints =
		pvr_sync_fence_get_checkpoints;
#endif

	return SyncCheckpointRegisterFunctions(&pvr_sync_data.sync_checkpoint_ops);
}

int pvr_sync_init(void)
{
	int err;

	err = misc_register(&pvr_sync_device);
	if (err) {
		pr_err(FILE_NAME ": %s: Failed to register pvr_sync device (%d)\n",
		       __func__, err);
	}
	return err;
}

void pvr_sync_deinit(void)
{
	misc_deregister(&pvr_sync_device);
}

enum PVRSRV_ERROR_TAG pvr_sync_device_init(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;
	enum PVRSRV_ERROR_TAG error;

	error = PVRSRVRegisterDbgRequestNotify(
				&priv->sync_debug_notify_handle,
				priv->dev_node,
				pvr_sync_debug_request_heading,
				DEBUG_REQUEST_LINUXFENCE,
				NULL);
	if (error != PVRSRV_OK) {
		pr_err("%s: failed to register debug request callback (%s)\n",
		       __func__, PVRSRVGetErrorString(error));
		goto err_out;
	}

	pvr_sync_data.dev_cookie = priv->dev_node;
	pvr_sync_data.fence_status_wq = priv->fence_status_wq;

	pvr_sync_data.foreign_fence_context =
		pvr_fence_context_create(pvr_sync_data.dev_cookie,
					 pvr_sync_data.fence_status_wq,
					 "foreign_sync");
	if (!pvr_sync_data.foreign_fence_context) {
		pr_err(FILE_NAME ": %s: Failed to create foreign sync context\n",
			__func__);
		error = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

#if defined(NO_HARDWARE)
	INIT_LIST_HEAD(&pvr_timeline_active_list);
#endif

err_out:
	return error;
}

void pvr_sync_device_deinit(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;

	pvr_fence_context_destroy(pvr_sync_data.foreign_fence_context);
	PVRSRVUnregisterDbgRequestNotify(priv->sync_debug_notify_handle);
}

enum PVRSRV_ERROR_TAG pvr_sync_fence_wait(void *fence, u32 timeout_in_ms)
{
	long timeout = msecs_to_jiffies(timeout_in_ms);
	int err;

	err = dma_fence_wait_timeout(fence, true, timeout);
	/*
	 * dma_fence_wait_timeout returns:
	 * - the remaining timeout on success
	 * - 0 on timeout
	 * - -ERESTARTSYS if interrupted
	 */
	if (err > 0)
		return PVRSRV_OK;
	else if (err == 0)
		return PVRSRV_ERROR_TIMEOUT;

	return PVRSRV_ERROR_FAILED_DEPENDENCIES;
}

enum PVRSRV_ERROR_TAG pvr_sync_fence_release(void *fence)
{
	dma_fence_put(fence);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR_TAG pvr_sync_fence_get(int fence_fd, void **fence_out)
{
	struct dma_fence *fence;

	fence = sync_file_get_fence(fence_fd);
	if (fence == NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*fence_out = fence;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR_TAG pvr_sync_sw_timeline_fence_create(int timeline_fd,
						    const char *fence_name,
						    int *fence_fd_out,
						    u64 *sync_pt_idx)
{
	enum PVRSRV_ERROR_TAG srv_err;
	struct pvr_sync_timeline *timeline;
	struct dma_fence *fence = NULL;
	struct sync_file *sync_file = NULL;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;

	timeline = pvr_sync_timeline_fget(timeline_fd);
	if (!timeline) {
		/* unrecognised timeline */
		srv_err = PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		goto err_put_fd;
	}

	fence = pvr_counting_fence_create(timeline->sw_fence_timeline, sync_pt_idx);
	pvr_sync_timeline_fput(timeline);
	if (!fence) {
		srv_err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_put_fd;
	}

	sync_file = sync_file_create(fence);
	if (!sync_file) {
		srv_err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_put_fence;
	}

	fd_install(fd, sync_file->file);

	*fence_fd_out = fd;

	return PVRSRV_OK;

err_put_fence:
	dma_fence_put(fence);
err_put_fd:
	put_unused_fd(fd);
	return srv_err;
}

enum PVRSRV_ERROR_TAG pvr_sync_sw_timeline_advance(void *timeline, u64 *sync_pt_idx)
{
	if (timeline == NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	pvr_counting_fence_timeline_inc(timeline, sync_pt_idx);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR_TAG pvr_sync_sw_timeline_release(void *timeline)
{
	if (timeline == NULL)
		return PVRSRV_ERROR_INVALID_PARAMS;

	pvr_counting_fence_timeline_put(timeline);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR_TAG pvr_sync_sw_timeline_get(int timeline_fd,
					   void **timeline_out)
{
	struct pvr_counting_fence_timeline *sw_timeline;
	struct pvr_sync_timeline *timeline;

	timeline = pvr_sync_timeline_fget(timeline_fd);
	if (!timeline)
		return PVRSRV_ERROR_INVALID_PARAMS;

	sw_timeline =
		pvr_counting_fence_timeline_get(timeline->sw_fence_timeline);
	pvr_sync_timeline_fput(timeline);
	if (!sw_timeline)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*timeline_out = sw_timeline;

	return PVRSRV_OK;
}
static void _dump_sync_point(struct dma_fence *fence,
							  DUMPDEBUG_PRINTF_FUNC *dump_debug_printf,
							  void *dump_debug_file)
{
	const struct dma_fence_ops *fence_ops = fence->ops;
	bool signaled = dma_fence_is_signaled(fence);
	char time[16] = { '\0' };

	fence_ops->timeline_value_str(fence, time, sizeof(time));

	PVR_DUMPDEBUG_LOG(dump_debug_printf,
					  dump_debug_file,
					  "<%p> Seq#=%llu TS=%s State=%s TLN=%s",
					  fence,
					  (u64) fence->seqno,
					  time,
					  (signaled) ? "Signalled" : "Active",
					  fence_ops->get_timeline_name(fence));
}

static void _dump_fence(struct dma_fence *fence,
			DUMPDEBUG_PRINTF_FUNC *dump_debug_printf,
			void *dump_debug_file)
{
	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *fence_array = to_dma_fence_array(fence);
		int i;

		PVR_DUMPDEBUG_LOG(dump_debug_printf,
				  dump_debug_file,
				  "Fence: [%p] Sync Points:\n",
				  fence_array);

		for (i = 0; i < fence_array->num_fences; i++)
			_dump_sync_point(fence_array->fences[i],
					 dump_debug_printf,
					 dump_debug_file);

	} else {
		_dump_sync_point(fence, dump_debug_printf, dump_debug_file);
	}
}

enum PVRSRV_ERROR_TAG
sync_dump_fence(void *sw_fence_obj,
		DUMPDEBUG_PRINTF_FUNC *dump_debug_printf,
		void *dump_debug_file)
{
	struct dma_fence *fence = (struct dma_fence *) sw_fence_obj;

	_dump_fence(fence, dump_debug_printf, dump_debug_file);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR_TAG
sync_sw_dump_timeline(void *sw_timeline_obj,
		      DUMPDEBUG_PRINTF_FUNC *dump_debug_printf,
		      void *dump_debug_file)
{
	pvr_counting_fence_timeline_dump_timeline(sw_timeline_obj,
						  dump_debug_printf,
						  dump_debug_file);

	return PVRSRV_OK;
}
