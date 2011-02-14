/*
 * Copyright 2009 Jerome Glisse.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 *    Dave Airlie
 */
#include <linux/seq_file.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_trace.h"

int radeon_fence_emit(struct radeon_device *rdev, struct radeon_fence *fence)
{
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	if (fence->emited) {
		write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
		return 0;
	}
	fence->seq = atomic_add_return(1, &rdev->fence_drv.seq);
	if (!rdev->cp.ready) {
		/* FIXME: cp is not running assume everythings is done right
		 * away
		 */
		WREG32(rdev->fence_drv.scratch_reg, fence->seq);
	} else
		radeon_fence_ring_emit(rdev, fence);

	trace_radeon_fence_emit(rdev->ddev, fence->seq);
	fence->emited = true;
	list_del(&fence->list);
	list_add_tail(&fence->list, &rdev->fence_drv.emited);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	return 0;
}

static bool radeon_fence_poll_locked(struct radeon_device *rdev)
{
	struct radeon_fence *fence;
	struct list_head *i, *n;
	uint32_t seq;
	bool wake = false;
	unsigned long cjiffies;

	if (rdev->wb.enabled) {
		u32 scratch_index;
		if (rdev->wb.use_event)
			scratch_index = R600_WB_EVENT_OFFSET + rdev->fence_drv.scratch_reg - rdev->scratch.reg_base;
		else
			scratch_index = RADEON_WB_SCRATCH_OFFSET + rdev->fence_drv.scratch_reg - rdev->scratch.reg_base;
		seq = rdev->wb.wb[scratch_index/4];
	} else
		seq = RREG32(rdev->fence_drv.scratch_reg);
	if (seq != rdev->fence_drv.last_seq) {
		rdev->fence_drv.last_seq = seq;
		rdev->fence_drv.last_jiffies = jiffies;
		rdev->fence_drv.last_timeout = RADEON_FENCE_JIFFIES_TIMEOUT;
	} else {
		cjiffies = jiffies;
		if (time_after(cjiffies, rdev->fence_drv.last_jiffies)) {
			cjiffies -= rdev->fence_drv.last_jiffies;
			if (time_after(rdev->fence_drv.last_timeout, cjiffies)) {
				/* update the timeout */
				rdev->fence_drv.last_timeout -= cjiffies;
			} else {
				/* the 500ms timeout is elapsed we should test
				 * for GPU lockup
				 */
				rdev->fence_drv.last_timeout = 1;
			}
		} else {
			/* wrap around update last jiffies, we will just wait
			 * a little longer
			 */
			rdev->fence_drv.last_jiffies = cjiffies;
		}
		return false;
	}
	n = NULL;
	list_for_each(i, &rdev->fence_drv.emited) {
		fence = list_entry(i, struct radeon_fence, list);
		if (fence->seq == seq) {
			n = i;
			break;
		}
	}
	/* all fence previous to this one are considered as signaled */
	if (n) {
		i = n;
		do {
			n = i->prev;
			list_del(i);
			list_add_tail(i, &rdev->fence_drv.signaled);
			fence = list_entry(i, struct radeon_fence, list);
			fence->signaled = true;
			i = n;
		} while (i != &rdev->fence_drv.emited);
		wake = true;
	}
	return wake;
}

static void radeon_fence_destroy(struct kref *kref)
{
	unsigned long irq_flags;
        struct radeon_fence *fence;

	fence = container_of(kref, struct radeon_fence, kref);
	write_lock_irqsave(&fence->rdev->fence_drv.lock, irq_flags);
	list_del(&fence->list);
	fence->emited = false;
	write_unlock_irqrestore(&fence->rdev->fence_drv.lock, irq_flags);
	kfree(fence);
}

int radeon_fence_create(struct radeon_device *rdev, struct radeon_fence **fence)
{
	unsigned long irq_flags;

	*fence = kmalloc(sizeof(struct radeon_fence), GFP_KERNEL);
	if ((*fence) == NULL) {
		return -ENOMEM;
	}
	kref_init(&((*fence)->kref));
	(*fence)->rdev = rdev;
	(*fence)->emited = false;
	(*fence)->signaled = false;
	(*fence)->seq = 0;
	INIT_LIST_HEAD(&(*fence)->list);

	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	list_add_tail(&(*fence)->list, &rdev->fence_drv.created);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	return 0;
}


bool radeon_fence_signaled(struct radeon_fence *fence)
{
	unsigned long irq_flags;
	bool signaled = false;

	if (!fence)
		return true;

	if (fence->rdev->gpu_lockup)
		return true;

	write_lock_irqsave(&fence->rdev->fence_drv.lock, irq_flags);
	signaled = fence->signaled;
	/* if we are shuting down report all fence as signaled */
	if (fence->rdev->shutdown) {
		signaled = true;
	}
	if (!fence->emited) {
		WARN(1, "Querying an unemited fence : %p !\n", fence);
		signaled = true;
	}
	if (!signaled) {
		radeon_fence_poll_locked(fence->rdev);
		signaled = fence->signaled;
	}
	write_unlock_irqrestore(&fence->rdev->fence_drv.lock, irq_flags);
	return signaled;
}

int radeon_fence_wait(struct radeon_fence *fence, bool intr)
{
	struct radeon_device *rdev;
	unsigned long irq_flags, timeout;
	u32 seq;
	int r;

	if (fence == NULL) {
		WARN(1, "Querying an invalid fence : %p !\n", fence);
		return 0;
	}
	rdev = fence->rdev;
	if (radeon_fence_signaled(fence)) {
		return 0;
	}
	timeout = rdev->fence_drv.last_timeout;
retry:
	/* save current sequence used to check for GPU lockup */
	seq = rdev->fence_drv.last_seq;
	trace_radeon_fence_wait_begin(rdev->ddev, seq);
	if (intr) {
		radeon_irq_kms_sw_irq_get(rdev);
		r = wait_event_interruptible_timeout(rdev->fence_drv.queue,
				radeon_fence_signaled(fence), timeout);
		radeon_irq_kms_sw_irq_put(rdev);
		if (unlikely(r < 0)) {
			return r;
		}
	} else {
		radeon_irq_kms_sw_irq_get(rdev);
		r = wait_event_timeout(rdev->fence_drv.queue,
			 radeon_fence_signaled(fence), timeout);
		radeon_irq_kms_sw_irq_put(rdev);
	}
	trace_radeon_fence_wait_end(rdev->ddev, seq);
	if (unlikely(!radeon_fence_signaled(fence))) {
		/* we were interrupted for some reason and fence isn't
		 * isn't signaled yet, resume wait
		 */
		if (r) {
			timeout = r;
			goto retry;
		}
		/* don't protect read access to rdev->fence_drv.last_seq
		 * if we experiencing a lockup the value doesn't change
		 */
		if (seq == rdev->fence_drv.last_seq && radeon_gpu_is_lockup(rdev)) {
			/* good news we believe it's a lockup */
			WARN(1, "GPU lockup (waiting for 0x%08X last fence id 0x%08X)\n",
			     fence->seq, seq);
			/* FIXME: what should we do ? marking everyone
			 * as signaled for now
			 */
			rdev->gpu_lockup = true;
			r = radeon_gpu_reset(rdev);
			if (r)
				return r;
			WREG32(rdev->fence_drv.scratch_reg, fence->seq);
			rdev->gpu_lockup = false;
		}
		timeout = RADEON_FENCE_JIFFIES_TIMEOUT;
		write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
		rdev->fence_drv.last_timeout = RADEON_FENCE_JIFFIES_TIMEOUT;
		rdev->fence_drv.last_jiffies = jiffies;
		write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
		goto retry;
	}
	return 0;
}

int radeon_fence_wait_next(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	struct radeon_fence *fence;
	int r;

	if (rdev->gpu_lockup) {
		return 0;
	}
	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	if (list_empty(&rdev->fence_drv.emited)) {
		write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
		return 0;
	}
	fence = list_entry(rdev->fence_drv.emited.next,
			   struct radeon_fence, list);
	radeon_fence_ref(fence);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	r = radeon_fence_wait(fence, false);
	radeon_fence_unref(&fence);
	return r;
}

int radeon_fence_wait_last(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	struct radeon_fence *fence;
	int r;

	if (rdev->gpu_lockup) {
		return 0;
	}
	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	if (list_empty(&rdev->fence_drv.emited)) {
		write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
		return 0;
	}
	fence = list_entry(rdev->fence_drv.emited.prev,
			   struct radeon_fence, list);
	radeon_fence_ref(fence);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	r = radeon_fence_wait(fence, false);
	radeon_fence_unref(&fence);
	return r;
}

struct radeon_fence *radeon_fence_ref(struct radeon_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

void radeon_fence_unref(struct radeon_fence **fence)
{
	struct radeon_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, &radeon_fence_destroy);
	}
}

void radeon_fence_process(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	bool wake;

	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	wake = radeon_fence_poll_locked(rdev);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	if (wake) {
		wake_up_all(&rdev->fence_drv.queue);
	}
}

int radeon_fence_driver_init(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	int r;

	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	r = radeon_scratch_get(rdev, &rdev->fence_drv.scratch_reg);
	if (r) {
		dev_err(rdev->dev, "fence failed to get scratch register\n");
		write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
		return r;
	}
	WREG32(rdev->fence_drv.scratch_reg, 0);
	atomic_set(&rdev->fence_drv.seq, 0);
	INIT_LIST_HEAD(&rdev->fence_drv.created);
	INIT_LIST_HEAD(&rdev->fence_drv.emited);
	INIT_LIST_HEAD(&rdev->fence_drv.signaled);
	init_waitqueue_head(&rdev->fence_drv.queue);
	rdev->fence_drv.initialized = true;
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	if (radeon_debugfs_fence_init(rdev)) {
		dev_err(rdev->dev, "fence debugfs file creation failed\n");
	}
	return 0;
}

void radeon_fence_driver_fini(struct radeon_device *rdev)
{
	unsigned long irq_flags;

	if (!rdev->fence_drv.initialized)
		return;
	wake_up_all(&rdev->fence_drv.queue);
	write_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
	radeon_scratch_free(rdev, rdev->fence_drv.scratch_reg);
	write_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);
	rdev->fence_drv.initialized = false;
}


/*
 * Fence debugfs
 */
#if defined(CONFIG_DEBUG_FS)
static int radeon_debugfs_fence_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_fence *fence;

	seq_printf(m, "Last signaled fence 0x%08X\n",
		   RREG32(rdev->fence_drv.scratch_reg));
	if (!list_empty(&rdev->fence_drv.emited)) {
		   fence = list_entry(rdev->fence_drv.emited.prev,
				      struct radeon_fence, list);
		   seq_printf(m, "Last emited fence %p with 0x%08X\n",
			      fence,  fence->seq);
	}
	return 0;
}

static struct drm_info_list radeon_debugfs_fence_list[] = {
	{"radeon_fence_info", &radeon_debugfs_fence_info, 0, NULL},
};
#endif

int radeon_debugfs_fence_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, radeon_debugfs_fence_list, 1);
#else
	return 0;
#endif
}
