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
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_trace.h"

static void radeon_fence_write(struct radeon_device *rdev, u32 seq, int ring)
{
	if (rdev->wb.enabled) {
		*rdev->fence_drv[ring].cpu_addr = cpu_to_le32(seq);
	} else {
		WREG32(rdev->fence_drv[ring].scratch_reg, seq);
	}
}

static u32 radeon_fence_read(struct radeon_device *rdev, int ring)
{
	u32 seq = 0;

	if (rdev->wb.enabled) {
		seq = le32_to_cpu(*rdev->fence_drv[ring].cpu_addr);
	} else {
		seq = RREG32(rdev->fence_drv[ring].scratch_reg);
	}
	return seq;
}

int radeon_fence_emit(struct radeon_device *rdev, struct radeon_fence *fence)
{
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	if (fence->seq && fence->seq < RADEON_FENCE_NOTEMITED_SEQ) {
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return 0;
	}
	/* we are protected by the ring emission mutex */
	fence->seq = ++rdev->fence_drv[fence->ring].seq;
	radeon_fence_ring_emit(rdev, fence->ring, fence);
	trace_radeon_fence_emit(rdev->ddev, fence->seq);
	/* are we the first fence on a previusly idle ring? */
	if (list_empty(&rdev->fence_drv[fence->ring].emitted)) {
		rdev->fence_drv[fence->ring].last_activity = jiffies;
	}
	list_move_tail(&fence->list, &rdev->fence_drv[fence->ring].emitted);
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	return 0;
}

static bool radeon_fence_poll_locked(struct radeon_device *rdev, int ring)
{
	struct radeon_fence *fence;
	struct list_head *i, *n;
	uint64_t seq, last_seq;
	unsigned count_loop = 0;
	bool wake = false;

	/* Note there is a scenario here for an infinite loop but it's
	 * very unlikely to happen. For it to happen, the current polling
	 * process need to be interrupted by another process and another
	 * process needs to update the last_seq btw the atomic read and
	 * xchg of the current process.
	 *
	 * More over for this to go in infinite loop there need to be
	 * continuously new fence signaled ie radeon_fence_read needs
	 * to return a different value each time for both the currently
	 * polling process and the other process that xchg the last_seq
	 * btw atomic read and xchg of the current process. And the
	 * value the other process set as last seq must be higher than
	 * the seq value we just read. Which means that current process
	 * need to be interrupted after radeon_fence_read and before
	 * atomic xchg.
	 *
	 * To be even more safe we count the number of time we loop and
	 * we bail after 10 loop just accepting the fact that we might
	 * have temporarly set the last_seq not to the true real last
	 * seq but to an older one.
	 */
	last_seq = atomic64_read(&rdev->fence_drv[ring].last_seq);
	do {
		seq = radeon_fence_read(rdev, ring);
		seq |= last_seq & 0xffffffff00000000LL;
		if (seq < last_seq) {
			seq += 0x100000000LL;
		}

		if (!wake && seq == last_seq) {
			return false;
		}
		/* If we loop over we don't want to return without
		 * checking if a fence is signaled as it means that the
		 * seq we just read is different from the previous on.
		 */
		wake = true;
		if ((count_loop++) > 10) {
			/* We looped over too many time leave with the
			 * fact that we might have set an older fence
			 * seq then the current real last seq as signaled
			 * by the hw.
			 */
			break;
		}
		last_seq = seq;
	} while (atomic64_xchg(&rdev->fence_drv[ring].last_seq, seq) > seq);

	/* reset wake to false */
	wake = false;
	rdev->fence_drv[ring].last_activity = jiffies;

	n = NULL;
	list_for_each(i, &rdev->fence_drv[ring].emitted) {
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
			list_move_tail(i, &rdev->fence_drv[ring].signaled);
			fence = list_entry(i, struct radeon_fence, list);
			fence->seq = RADEON_FENCE_SIGNALED_SEQ;
			i = n;
		} while (i != &rdev->fence_drv[ring].emitted);
		wake = true;
	}
	return wake;
}

static void radeon_fence_destroy(struct kref *kref)
{
	unsigned long irq_flags;
        struct radeon_fence *fence;

	fence = container_of(kref, struct radeon_fence, kref);
	write_lock_irqsave(&fence->rdev->fence_lock, irq_flags);
	list_del(&fence->list);
	fence->seq = RADEON_FENCE_NOTEMITED_SEQ;
	write_unlock_irqrestore(&fence->rdev->fence_lock, irq_flags);
	if (fence->semaphore)
		radeon_semaphore_free(fence->rdev, fence->semaphore);
	kfree(fence);
}

int radeon_fence_create(struct radeon_device *rdev,
			struct radeon_fence **fence,
			int ring)
{
	*fence = kmalloc(sizeof(struct radeon_fence), GFP_KERNEL);
	if ((*fence) == NULL) {
		return -ENOMEM;
	}
	kref_init(&((*fence)->kref));
	(*fence)->rdev = rdev;
	(*fence)->seq = RADEON_FENCE_NOTEMITED_SEQ;
	(*fence)->ring = ring;
	(*fence)->semaphore = NULL;
	INIT_LIST_HEAD(&(*fence)->list);
	return 0;
}

bool radeon_fence_signaled(struct radeon_fence *fence)
{
	unsigned long irq_flags;
	bool signaled = false;

	if (!fence)
		return true;

	write_lock_irqsave(&fence->rdev->fence_lock, irq_flags);
	signaled = (fence->seq == RADEON_FENCE_SIGNALED_SEQ);
	/* if we are shuting down report all fence as signaled */
	if (fence->rdev->shutdown) {
		signaled = true;
	}
	if (fence->seq == RADEON_FENCE_NOTEMITED_SEQ) {
		WARN(1, "Querying an unemitted fence : %p !\n", fence);
		signaled = true;
	}
	if (!signaled) {
		radeon_fence_poll_locked(fence->rdev, fence->ring);
		signaled = (fence->seq == RADEON_FENCE_SIGNALED_SEQ);
	}
	write_unlock_irqrestore(&fence->rdev->fence_lock, irq_flags);
	return signaled;
}

int radeon_fence_wait(struct radeon_fence *fence, bool intr)
{
	struct radeon_device *rdev;
	unsigned long irq_flags, timeout, last_activity;
	uint64_t seq;
	int i, r;
	bool signaled;

	if (fence == NULL) {
		WARN(1, "Querying an invalid fence : %p !\n", fence);
		return -EINVAL;
	}

	rdev = fence->rdev;
	signaled = radeon_fence_signaled(fence);
	while (!signaled) {
		read_lock_irqsave(&rdev->fence_lock, irq_flags);
		timeout = jiffies - RADEON_FENCE_JIFFIES_TIMEOUT;
		if (time_after(rdev->fence_drv[fence->ring].last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = rdev->fence_drv[fence->ring].last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup */
			timeout = 1;
		}
		/* save current sequence value used to check for GPU lockups */
		seq = atomic64_read(&rdev->fence_drv[fence->ring].last_seq);
		/* Save current last activity valuee, used to check for GPU lockups */
		last_activity = rdev->fence_drv[fence->ring].last_activity;
		read_unlock_irqrestore(&rdev->fence_lock, irq_flags);

		trace_radeon_fence_wait_begin(rdev->ddev, seq);
		radeon_irq_kms_sw_irq_get(rdev, fence->ring);
		if (intr) {
			r = wait_event_interruptible_timeout(
				rdev->fence_drv[fence->ring].queue,
				(signaled = radeon_fence_signaled(fence)), timeout);
		} else {
			r = wait_event_timeout(
				rdev->fence_drv[fence->ring].queue,
				(signaled = radeon_fence_signaled(fence)), timeout);
		}
		radeon_irq_kms_sw_irq_put(rdev, fence->ring);
		if (unlikely(r < 0)) {
			return r;
		}
		trace_radeon_fence_wait_end(rdev->ddev, seq);

		if (unlikely(!signaled)) {
			/* we were interrupted for some reason and fence
			 * isn't signaled yet, resume waiting */
			if (r) {
				continue;
			}

			write_lock_irqsave(&rdev->fence_lock, irq_flags);
			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != rdev->fence_drv[fence->ring].last_activity) {
				write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
				continue;
			}

			write_unlock_irqrestore(&rdev->fence_lock, irq_flags);

			if (radeon_ring_is_lockup(rdev, fence->ring, &rdev->ring[fence->ring])) {
				/* good news we believe it's a lockup */
				dev_warn(rdev->dev, "GPU lockup (waiting for 0x%016llx last fence id 0x%016llx)\n",
					 fence->seq, seq);

				/* change last activity so nobody else think there is a lockup */
				for (i = 0; i < RADEON_NUM_RINGS; ++i) {
					rdev->fence_drv[i].last_activity = jiffies;
				}

				/* mark the ring as not ready any more */
				rdev->ring[fence->ring].ready = false;
				return -EDEADLK;
			}
		}
	}
	return 0;
}

int radeon_fence_wait_next(struct radeon_device *rdev, int ring)
{
	unsigned long irq_flags;
	struct radeon_fence *fence;
	int r;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	if (!rdev->ring[ring].ready) {
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return -EBUSY;
	}
	if (list_empty(&rdev->fence_drv[ring].emitted)) {
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return -ENOENT;
	}
	fence = list_entry(rdev->fence_drv[ring].emitted.next,
			   struct radeon_fence, list);
	radeon_fence_ref(fence);
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	r = radeon_fence_wait(fence, false);
	radeon_fence_unref(&fence);
	return r;
}

int radeon_fence_wait_empty(struct radeon_device *rdev, int ring)
{
	unsigned long irq_flags;
	struct radeon_fence *fence;
	int r;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	if (!rdev->ring[ring].ready) {
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return -EBUSY;
	}
	if (list_empty(&rdev->fence_drv[ring].emitted)) {
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return 0;
	}
	fence = list_entry(rdev->fence_drv[ring].emitted.prev,
			   struct radeon_fence, list);
	radeon_fence_ref(fence);
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
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
		kref_put(&tmp->kref, radeon_fence_destroy);
	}
}

void radeon_fence_process(struct radeon_device *rdev, int ring)
{
	unsigned long irq_flags;
	bool wake;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	wake = radeon_fence_poll_locked(rdev, ring);
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	if (wake) {
		wake_up_all(&rdev->fence_drv[ring].queue);
	}
}

int radeon_fence_count_emitted(struct radeon_device *rdev, int ring)
{
	unsigned long irq_flags;
	int not_processed = 0;

	read_lock_irqsave(&rdev->fence_lock, irq_flags);
	if (!rdev->fence_drv[ring].initialized) {
		read_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		return 0;
	}

	if (!list_empty(&rdev->fence_drv[ring].emitted)) {
		struct list_head *ptr;
		list_for_each(ptr, &rdev->fence_drv[ring].emitted) {
			/* count up to 3, that's enought info */
			if (++not_processed >= 3)
				break;
		}
	}
	read_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	return not_processed;
}

int radeon_fence_driver_start_ring(struct radeon_device *rdev, int ring)
{
	unsigned long irq_flags;
	uint64_t index;
	int r;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
	if (rdev->wb.use_event) {
		rdev->fence_drv[ring].scratch_reg = 0;
		index = R600_WB_EVENT_OFFSET + ring * 4;
	} else {
		r = radeon_scratch_get(rdev, &rdev->fence_drv[ring].scratch_reg);
		if (r) {
			dev_err(rdev->dev, "fence failed to get scratch register\n");
			write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
			return r;
		}
		index = RADEON_WB_SCRATCH_OFFSET +
			rdev->fence_drv[ring].scratch_reg -
			rdev->scratch.reg_base;
	}
	rdev->fence_drv[ring].cpu_addr = &rdev->wb.wb[index/4];
	rdev->fence_drv[ring].gpu_addr = rdev->wb.gpu_addr + index;
	radeon_fence_write(rdev, rdev->fence_drv[ring].seq, ring);
	rdev->fence_drv[ring].initialized = true;
	DRM_INFO("fence driver on ring %d use gpu addr 0x%016llx and cpu addr 0x%p\n",
		 ring, rdev->fence_drv[ring].gpu_addr, rdev->fence_drv[ring].cpu_addr);
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	return 0;
}

static void radeon_fence_driver_init_ring(struct radeon_device *rdev, int ring)
{
	rdev->fence_drv[ring].scratch_reg = -1;
	rdev->fence_drv[ring].cpu_addr = NULL;
	rdev->fence_drv[ring].gpu_addr = 0;
	rdev->fence_drv[ring].seq = 0;
	atomic64_set(&rdev->fence_drv[ring].last_seq, 0);
	INIT_LIST_HEAD(&rdev->fence_drv[ring].emitted);
	INIT_LIST_HEAD(&rdev->fence_drv[ring].signaled);
	init_waitqueue_head(&rdev->fence_drv[ring].queue);
	rdev->fence_drv[ring].initialized = false;
}

int radeon_fence_driver_init(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	int ring;

	write_lock_irqsave(&rdev->fence_lock, irq_flags);
	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		radeon_fence_driver_init_ring(rdev, ring);
	}
	write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
	if (radeon_debugfs_fence_init(rdev)) {
		dev_err(rdev->dev, "fence debugfs file creation failed\n");
	}
	return 0;
}

void radeon_fence_driver_fini(struct radeon_device *rdev)
{
	unsigned long irq_flags;
	int ring;

	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		if (!rdev->fence_drv[ring].initialized)
			continue;
		radeon_fence_wait_empty(rdev, ring);
		wake_up_all(&rdev->fence_drv[ring].queue);
		write_lock_irqsave(&rdev->fence_lock, irq_flags);
		radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
		write_unlock_irqrestore(&rdev->fence_lock, irq_flags);
		rdev->fence_drv[ring].initialized = false;
	}
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
	int i;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!rdev->fence_drv[i].initialized)
			continue;

		seq_printf(m, "--- ring %d ---\n", i);
		seq_printf(m, "Last signaled fence 0x%016lx\n",
			   atomic64_read(&rdev->fence_drv[i].last_seq));
		if (!list_empty(&rdev->fence_drv[i].emitted)) {
			fence = list_entry(rdev->fence_drv[i].emitted.prev,
					   struct radeon_fence, list);
			seq_printf(m, "Last emitted fence %p with 0x%016llx\n",
				   fence,  fence->seq);
		}
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
