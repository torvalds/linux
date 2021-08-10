// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Code for supporting explicit Android fences (CONFIG_SYNC)
 * Known to be good for kernels 4.5 and earlier.
 * Replaced with CONFIG_SYNC_FILE for 4.9 and later kernels
 * (see mali_kbase_sync_file.c)
 */

#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/anon_inodes.h>
#include <linux/version.h>
#include "sync.h"
#include <mali_kbase.h>
#include <mali_kbase_sync.h>

struct mali_sync_timeline {
	struct sync_timeline timeline;
	atomic_t counter;
	atomic_t signaled;
};

struct mali_sync_pt {
	struct sync_pt pt;
	int order;
	int result;
};

static struct mali_sync_timeline *to_mali_sync_timeline(
						struct sync_timeline *timeline)
{
	return container_of(timeline, struct mali_sync_timeline, timeline);
}

static struct mali_sync_pt *to_mali_sync_pt(struct sync_pt *pt)
{
	return container_of(pt, struct mali_sync_pt, pt);
}

static struct sync_pt *timeline_dup(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt = to_mali_sync_pt(pt);
	struct mali_sync_pt *new_mpt;
	struct sync_pt *new_pt = sync_pt_create(sync_pt_parent(pt),
						sizeof(struct mali_sync_pt));

	if (!new_pt)
		return NULL;

	new_mpt = to_mali_sync_pt(new_pt);
	new_mpt->order = mpt->order;
	new_mpt->result = mpt->result;

	return new_pt;
}

static int timeline_has_signaled(struct sync_pt *pt)
{
	struct mali_sync_pt *mpt = to_mali_sync_pt(pt);
	struct mali_sync_timeline *mtl = to_mali_sync_timeline(
							sync_pt_parent(pt));
	int result = mpt->result;

	int diff = atomic_read(&mtl->signaled) - mpt->order;

	if (diff >= 0)
		return (result < 0) ? result : 1;

	return 0;
}

static int timeline_compare(struct sync_pt *a, struct sync_pt *b)
{
	struct mali_sync_pt *ma = container_of(a, struct mali_sync_pt, pt);
	struct mali_sync_pt *mb = container_of(b, struct mali_sync_pt, pt);

	int diff = ma->order - mb->order;

	if (diff == 0)
		return 0;

	return (diff < 0) ? -1 : 1;
}

static void timeline_value_str(struct sync_timeline *timeline, char *str,
			       int size)
{
	struct mali_sync_timeline *mtl = to_mali_sync_timeline(timeline);

	snprintf(str, size, "%d", atomic_read(&mtl->signaled));
}

static void pt_value_str(struct sync_pt *pt, char *str, int size)
{
	struct mali_sync_pt *mpt = to_mali_sync_pt(pt);

	snprintf(str, size, "%d(%d)", mpt->order, mpt->result);
}

static struct sync_timeline_ops mali_timeline_ops = {
	.driver_name = "Mali",
	.dup = timeline_dup,
	.has_signaled = timeline_has_signaled,
	.compare = timeline_compare,
	.timeline_value_str = timeline_value_str,
	.pt_value_str       = pt_value_str,
};

/* Allocates a timeline for Mali
 *
 * One timeline should be allocated per API context.
 */
static struct sync_timeline *mali_sync_timeline_alloc(const char *name)
{
	struct sync_timeline *tl;
	struct mali_sync_timeline *mtl;

	tl = sync_timeline_create(&mali_timeline_ops,
				  sizeof(struct mali_sync_timeline), name);
	if (!tl)
		return NULL;

	/* Set the counter in our private struct */
	mtl = to_mali_sync_timeline(tl);
	atomic_set(&mtl->counter, 0);
	atomic_set(&mtl->signaled, 0);

	return tl;
}

static int kbase_stream_close(struct inode *inode, struct file *file)
{
	struct sync_timeline *tl;

	tl = (struct sync_timeline *)file->private_data;
	sync_timeline_destroy(tl);
	return 0;
}

static const struct file_operations stream_fops = {
	.owner = THIS_MODULE,
	.release = kbase_stream_close,
};

int kbase_sync_fence_stream_create(const char *name, int *const out_fd)
{
	struct sync_timeline *tl;

	if (!out_fd)
		return -EINVAL;

	tl = mali_sync_timeline_alloc(name);
	if (!tl)
		return -EINVAL;

	*out_fd = anon_inode_getfd(name, &stream_fops, tl, O_RDONLY|O_CLOEXEC);

	if (*out_fd < 0) {
		sync_timeline_destroy(tl);
		return -EINVAL;
	}

	return 0;
}

#if !MALI_USE_CSF
/* Allocates a sync point within the timeline.
 *
 * The timeline must be the one allocated by kbase_sync_timeline_alloc
 *
 * Sync points must be triggered in *exactly* the same order as they are
 * allocated.
 */
static struct sync_pt *kbase_sync_pt_alloc(struct sync_timeline *parent)
{
	struct sync_pt *pt = sync_pt_create(parent,
					    sizeof(struct mali_sync_pt));
	struct mali_sync_timeline *mtl = to_mali_sync_timeline(parent);
	struct mali_sync_pt *mpt;

	if (!pt)
		return NULL;

	mpt = to_mali_sync_pt(pt);
	mpt->order = atomic_inc_return(&mtl->counter);
	mpt->result = 0;

	return pt;
}

int kbase_sync_fence_out_create(struct kbase_jd_atom *katom, int tl_fd)
{
	struct sync_timeline *tl;
	struct sync_pt *pt;
	struct sync_fence *fence;
	int fd;
	struct file *tl_file;

	tl_file = fget(tl_fd);
	if (tl_file == NULL)
		return -EBADF;

	if (tl_file->f_op != &stream_fops) {
		fd = -EBADF;
		goto out;
	}

	tl = tl_file->private_data;

	pt = kbase_sync_pt_alloc(tl);
	if (!pt) {
		fd = -EFAULT;
		goto out;
	}

	fence = sync_fence_create("mali_fence", pt);
	if (!fence) {
		sync_pt_free(pt);
		fd = -EFAULT;
		goto out;
	}

	/* from here the fence owns the sync_pt */

	/* create a fd representing the fence */
	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		sync_fence_put(fence);
		goto out;
	}

	/* bind fence to the new fd */
	sync_fence_install(fence, fd);

	katom->fence = sync_fence_fdget(fd);
	if (katom->fence == NULL) {
		/* The only way the fence can be NULL is if userspace closed it
		 * for us, so we don't need to clear it up
		 */
		fd = -EINVAL;
		goto out;
	}

out:
	fput(tl_file);

	return fd;
}

int kbase_sync_fence_in_from_fd(struct kbase_jd_atom *katom, int fd)
{
	katom->fence = sync_fence_fdget(fd);
	return katom->fence ? 0 : -ENOENT;
}
#endif /* !MALI_USE_CSF */

int kbase_sync_fence_validate(int fd)
{
	struct sync_fence *fence;

	fence = sync_fence_fdget(fd);
	if (!fence)
		return -EINVAL;

	sync_fence_put(fence);
	return 0;
}

#if !MALI_USE_CSF
/* Returns true if the specified timeline is allocated by Mali */
static int kbase_sync_timeline_is_ours(struct sync_timeline *timeline)
{
	return timeline->ops == &mali_timeline_ops;
}

/* Signals a particular sync point
 *
 * Sync points must be triggered in *exactly* the same order as they are
 * allocated.
 *
 * If they are signaled in the wrong order then a message will be printed in
 * debug builds and otherwise attempts to signal order sync_pts will be ignored.
 *
 * result can be negative to indicate error, any other value is interpreted as
 * success.
 */
static void kbase_sync_signal_pt(struct sync_pt *pt, int result)
{
	struct mali_sync_pt *mpt = to_mali_sync_pt(pt);
	struct mali_sync_timeline *mtl = to_mali_sync_timeline(
							sync_pt_parent(pt));
	int signaled;
	int diff;

	mpt->result = result;

	do {
		signaled = atomic_read(&mtl->signaled);

		diff = signaled - mpt->order;

		if (diff > 0) {
			/* The timeline is already at or ahead of this point.
			 * This should not happen unless userspace has been
			 * signaling fences out of order, so warn but don't
			 * violate the sync_pt API.
			 * The warning is only in debug builds to prevent
			 * a malicious user being able to spam dmesg.
			 */
#ifdef CONFIG_MALI_BIFROST_DEBUG
			pr_err("Fences were triggered in a different order to allocation!");
#endif				/* CONFIG_MALI_BIFROST_DEBUG */
			return;
		}
	} while (atomic_cmpxchg(&mtl->signaled,
				signaled, mpt->order) != signaled);
}

enum base_jd_event_code
kbase_sync_fence_out_trigger(struct kbase_jd_atom *katom, int result)
{
	struct sync_pt *pt;
	struct sync_timeline *timeline;

	if (!katom->fence)
		return BASE_JD_EVENT_JOB_CANCELLED;

	if (katom->fence->num_fences != 1) {
		/* Not exactly one item in the list - so it didn't (directly)
		 * come from us
		 */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	pt = container_of(katom->fence->cbs[0].sync_pt, struct sync_pt, base);
	timeline = sync_pt_parent(pt);

	if (!kbase_sync_timeline_is_ours(timeline)) {
		/* Fence has a sync_pt which isn't ours! */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	kbase_sync_signal_pt(pt, result);

	sync_timeline_signal(timeline);

	kbase_sync_fence_out_remove(katom);

	return (result < 0) ? BASE_JD_EVENT_JOB_CANCELLED : BASE_JD_EVENT_DONE;
}

static inline int kbase_fence_get_status(struct sync_fence *fence)
{
	if (!fence)
		return -ENOENT;

	return atomic_read(&fence->status);
}

static void kbase_fence_wait_callback(struct sync_fence *fence,
				      struct sync_fence_waiter *waiter)
{
	struct kbase_jd_atom *katom = container_of(waiter,
					struct kbase_jd_atom, sync_waiter);
	struct kbase_context *kctx = katom->kctx;

	/* Propagate the fence status to the atom.
	 * If negative then cancel this atom and its dependencies.
	 */
	if (kbase_fence_get_status(fence) < 0)
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	/* To prevent a potential deadlock we schedule the work onto the
	 * job_done_wq workqueue
	 *
	 * The issue is that we may signal the timeline while holding
	 * kctx->jctx.lock and the callbacks are run synchronously from
	 * sync_timeline_signal. So we simply defer the work.
	 */

	INIT_WORK(&katom->work, kbase_sync_fence_wait_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

int kbase_sync_fence_in_wait(struct kbase_jd_atom *katom)
{
	int ret;

	sync_fence_waiter_init(&katom->sync_waiter, kbase_fence_wait_callback);

	ret = sync_fence_wait_async(katom->fence, &katom->sync_waiter);

	if (ret == 1) {
		/* Already signaled */
		return 0;
	}

	if (ret < 0) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		/* We should cause the dependent jobs in the bag to be failed,
		 * to do this we schedule the work queue to complete this job
		 */
		INIT_WORK(&katom->work, kbase_sync_fence_wait_worker);
		queue_work(katom->kctx->jctx.job_done_wq, &katom->work);
	}

	return 1;
}

void kbase_sync_fence_in_cancel_wait(struct kbase_jd_atom *katom)
{
	if (sync_fence_cancel_async(katom->fence, &katom->sync_waiter) != 0) {
		/* The wait wasn't cancelled - leave the cleanup for
		 * kbase_fence_wait_callback
		 */
		return;
	}

	/* Wait was cancelled - zap the atoms */
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	kbasep_remove_waiting_soft_job(katom);
	kbase_finish_soft_job(katom);

	if (jd_done_nolock(katom, NULL))
		kbase_js_sched_all(katom->kctx->kbdev);
}

void kbase_sync_fence_out_remove(struct kbase_jd_atom *katom)
{
	if (katom->fence) {
		sync_fence_put(katom->fence);
		katom->fence = NULL;
	}
}

void kbase_sync_fence_in_remove(struct kbase_jd_atom *katom)
{
	if (katom->fence) {
		sync_fence_put(katom->fence);
		katom->fence = NULL;
	}
}

int kbase_sync_fence_in_info_get(struct kbase_jd_atom *katom,
				 struct kbase_sync_fence_info *info)
{
	if (!katom->fence)
		return -ENOENT;

	info->fence = katom->fence;
	info->status = kbase_fence_get_status(katom->fence);
	strlcpy(info->name, katom->fence->name, sizeof(info->name));

	return 0;
}

int kbase_sync_fence_out_info_get(struct kbase_jd_atom *katom,
				 struct kbase_sync_fence_info *info)
{
	if (!katom->fence)
		return -ENOENT;

	info->fence = katom->fence;
	info->status = kbase_fence_get_status(katom->fence);
	strlcpy(info->name, katom->fence->name, sizeof(info->name));

	return 0;
}

#ifdef CONFIG_MALI_BIFROST_FENCE_DEBUG
void kbase_sync_fence_in_dump(struct kbase_jd_atom *katom)
{
	/* Dump out the full state of all the Android sync fences.
	 * The function sync_dump() isn't exported to modules, so force
	 * sync_fence_wait() to time out to trigger sync_dump().
	 */
	if (katom->fence)
		sync_fence_wait(katom->fence, 1);
}
#endif
#endif /* !MALI_USE_CSF */
