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
	/* we are protected by the ring emission mutex */
	if (fence->seq && fence->seq < RADEON_FENCE_NOTEMITED_SEQ) {
		return 0;
	}
	fence->seq = ++rdev->fence_drv[fence->ring].seq;
	radeon_fence_ring_emit(rdev, fence->ring, fence);
	trace_radeon_fence_emit(rdev->ddev, fence->seq);
	return 0;
}

void radeon_fence_process(struct radeon_device *rdev, int ring)
{
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

		if (seq == last_seq) {
			break;
		}
		/* If we loop over we don't want to return without
		 * checking if a fence is signaled as it means that the
		 * seq we just read is different from the previous on.
		 */
		wake = true;
		last_seq = seq;
		if ((count_loop++) > 10) {
			/* We looped over too many time leave with the
			 * fact that we might have set an older fence
			 * seq then the current real last seq as signaled
			 * by the hw.
			 */
			break;
		}
	} while (atomic64_xchg(&rdev->fence_drv[ring].last_seq, seq) > seq);

	if (wake) {
		rdev->fence_drv[ring].last_activity = jiffies;
		wake_up_all(&rdev->fence_queue);
	}
}

static void radeon_fence_destroy(struct kref *kref)
{
	struct radeon_fence *fence;

	fence = container_of(kref, struct radeon_fence, kref);
	fence->seq = RADEON_FENCE_NOTEMITED_SEQ;
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
	return 0;
}

static bool radeon_fence_seq_signaled(struct radeon_device *rdev,
				      u64 seq, unsigned ring)
{
	if (atomic64_read(&rdev->fence_drv[ring].last_seq) >= seq) {
		return true;
	}
	/* poll new last sequence at least once */
	radeon_fence_process(rdev, ring);
	if (atomic64_read(&rdev->fence_drv[ring].last_seq) >= seq) {
		return true;
	}
	return false;
}

bool radeon_fence_signaled(struct radeon_fence *fence)
{
	if (!fence) {
		return true;
	}
	if (fence->seq == RADEON_FENCE_NOTEMITED_SEQ) {
		WARN(1, "Querying an unemitted fence : %p !\n", fence);
		return true;
	}
	if (fence->seq == RADEON_FENCE_SIGNALED_SEQ) {
		return true;
	}
	if (radeon_fence_seq_signaled(fence->rdev, fence->seq, fence->ring)) {
		fence->seq = RADEON_FENCE_SIGNALED_SEQ;
		return true;
	}
	return false;
}

static int radeon_fence_wait_seq(struct radeon_device *rdev, u64 target_seq,
				 unsigned ring, bool intr, bool lock_ring)
{
	unsigned long timeout, last_activity;
	uint64_t seq;
	unsigned i;
	bool signaled;
	int r;

	while (target_seq > atomic64_read(&rdev->fence_drv[ring].last_seq)) {
		if (!rdev->ring[ring].ready) {
			return -EBUSY;
		}

		timeout = jiffies - RADEON_FENCE_JIFFIES_TIMEOUT;
		if (time_after(rdev->fence_drv[ring].last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = rdev->fence_drv[ring].last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup
			 */
			timeout = 1;
		}
		seq = atomic64_read(&rdev->fence_drv[ring].last_seq);
		/* Save current last activity valuee, used to check for GPU lockups */
		last_activity = rdev->fence_drv[ring].last_activity;

		trace_radeon_fence_wait_begin(rdev->ddev, seq);
		radeon_irq_kms_sw_irq_get(rdev, ring);
		if (intr) {
			r = wait_event_interruptible_timeout(rdev->fence_queue,
				(signaled = radeon_fence_seq_signaled(rdev, target_seq, ring)),
				timeout);
                } else {
			r = wait_event_timeout(rdev->fence_queue,
				(signaled = radeon_fence_seq_signaled(rdev, target_seq, ring)),
				timeout);
		}
		radeon_irq_kms_sw_irq_put(rdev, ring);
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

			/* check if sequence value has changed since last_activity */
			if (seq != atomic64_read(&rdev->fence_drv[ring].last_seq)) {
				continue;
			}

			if (lock_ring) {
				mutex_lock(&rdev->ring_lock);
			}

			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != rdev->fence_drv[ring].last_activity) {
				if (lock_ring) {
					mutex_unlock(&rdev->ring_lock);
				}
				continue;
			}

			if (radeon_ring_is_lockup(rdev, ring, &rdev->ring[ring])) {
				/* good news we believe it's a lockup */
				dev_warn(rdev->dev, "GPU lockup (waiting for 0x%016llx last fence id 0x%016llx)\n",
					 target_seq, seq);

				/* change last activity so nobody else think there is a lockup */
				for (i = 0; i < RADEON_NUM_RINGS; ++i) {
					rdev->fence_drv[i].last_activity = jiffies;
				}

				/* mark the ring as not ready any more */
				rdev->ring[ring].ready = false;
				if (lock_ring) {
					mutex_unlock(&rdev->ring_lock);
				}
				return -EDEADLK;
			}

			if (lock_ring) {
				mutex_unlock(&rdev->ring_lock);
			}
		}
	}
	return 0;
}

int radeon_fence_wait(struct radeon_fence *fence, bool intr)
{
	int r;

	if (fence == NULL) {
		WARN(1, "Querying an invalid fence : %p !\n", fence);
		return -EINVAL;
	}

	r = radeon_fence_wait_seq(fence->rdev, fence->seq,
				  fence->ring, intr, true);
	if (r) {
		return r;
	}
	fence->seq = RADEON_FENCE_SIGNALED_SEQ;
	return 0;
}

bool radeon_fence_any_seq_signaled(struct radeon_device *rdev, u64 *seq)
{
	unsigned i;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (seq[i] && radeon_fence_seq_signaled(rdev, seq[i], i)) {
			return true;
		}
	}
	return false;
}

static int radeon_fence_wait_any_seq(struct radeon_device *rdev,
				     u64 *target_seq, bool intr)
{
	unsigned long timeout, last_activity, tmp;
	unsigned i, ring = RADEON_NUM_RINGS;
	bool signaled;
	int r;

	for (i = 0, last_activity = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!target_seq[i]) {
			continue;
		}

		/* use the most recent one as indicator */
		if (time_after(rdev->fence_drv[i].last_activity, last_activity)) {
			last_activity = rdev->fence_drv[i].last_activity;
		}

		/* For lockup detection just pick the lowest ring we are
		 * actively waiting for
		 */
		if (i < ring) {
			ring = i;
		}
	}

	/* nothing to wait for ? */
	if (ring == RADEON_NUM_RINGS) {
		return 0;
	}

	while (!radeon_fence_any_seq_signaled(rdev, target_seq)) {
		timeout = jiffies - RADEON_FENCE_JIFFIES_TIMEOUT;
		if (time_after(last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup
			 */
			timeout = 1;
		}

		trace_radeon_fence_wait_begin(rdev->ddev, target_seq[ring]);
		for (i = 0; i < RADEON_NUM_RINGS; ++i) {
			if (target_seq[i]) {
				radeon_irq_kms_sw_irq_get(rdev, i);
			}
		}
		if (intr) {
			r = wait_event_interruptible_timeout(rdev->fence_queue,
				(signaled = radeon_fence_any_seq_signaled(rdev, target_seq)),
				timeout);
		} else {
			r = wait_event_timeout(rdev->fence_queue,
				(signaled = radeon_fence_any_seq_signaled(rdev, target_seq)),
				timeout);
		}
		for (i = 0; i < RADEON_NUM_RINGS; ++i) {
			if (target_seq[i]) {
				radeon_irq_kms_sw_irq_put(rdev, i);
			}
		}
		if (unlikely(r < 0)) {
			return r;
		}
		trace_radeon_fence_wait_end(rdev->ddev, target_seq[ring]);

		if (unlikely(!signaled)) {
			/* we were interrupted for some reason and fence
			 * isn't signaled yet, resume waiting */
			if (r) {
				continue;
			}

			mutex_lock(&rdev->ring_lock);
			for (i = 0, tmp = 0; i < RADEON_NUM_RINGS; ++i) {
				if (time_after(rdev->fence_drv[i].last_activity, tmp)) {
					tmp = rdev->fence_drv[i].last_activity;
				}
			}
			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != tmp) {
				last_activity = tmp;
				mutex_unlock(&rdev->ring_lock);
				continue;
			}

			if (radeon_ring_is_lockup(rdev, ring, &rdev->ring[ring])) {
				/* good news we believe it's a lockup */
				dev_warn(rdev->dev, "GPU lockup (waiting for 0x%016llx)\n",
					 target_seq[ring]);

				/* change last activity so nobody else think there is a lockup */
				for (i = 0; i < RADEON_NUM_RINGS; ++i) {
					rdev->fence_drv[i].last_activity = jiffies;
				}

				/* mark the ring as not ready any more */
				rdev->ring[ring].ready = false;
				mutex_unlock(&rdev->ring_lock);
				return -EDEADLK;
			}
			mutex_unlock(&rdev->ring_lock);
		}
	}
	return 0;
}

int radeon_fence_wait_any(struct radeon_device *rdev,
			  struct radeon_fence **fences,
			  bool intr)
{
	uint64_t seq[RADEON_NUM_RINGS];
	unsigned i;
	int r;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		seq[i] = 0;

		if (!fences[i]) {
			continue;
		}

		if (fences[i]->seq == RADEON_FENCE_SIGNALED_SEQ) {
			/* something was allready signaled */
			return 0;
		}

		if (fences[i]->seq < RADEON_FENCE_NOTEMITED_SEQ) {
			seq[i] = fences[i]->seq;
		}
	}

	r = radeon_fence_wait_any_seq(rdev, seq, intr);
	if (r) {
		return r;
	}
	return 0;
}

int radeon_fence_wait_next_locked(struct radeon_device *rdev, int ring)
{
	uint64_t seq;

	/* We are not protected by ring lock when reading current seq but
	 * it's ok as worst case is we return to early while we could have
	 * wait.
	 */
	seq = atomic64_read(&rdev->fence_drv[ring].last_seq) + 1ULL;
	if (seq >= rdev->fence_drv[ring].seq) {
		/* nothing to wait for, last_seq is
		   already the last emited fence */
		return -ENOENT;
	}
	return radeon_fence_wait_seq(rdev, seq, ring, false, false);
}

int radeon_fence_wait_empty_locked(struct radeon_device *rdev, int ring)
{
	/* We are not protected by ring lock when reading current seq
	 * but it's ok as wait empty is call from place where no more
	 * activity can be scheduled so there won't be concurrent access
	 * to seq value.
	 */
	return radeon_fence_wait_seq(rdev, rdev->fence_drv[ring].seq,
				     ring, false, false);
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

unsigned radeon_fence_count_emitted(struct radeon_device *rdev, int ring)
{
	uint64_t emitted;

	/* We are not protected by ring lock when reading the last sequence
	 * but it's ok to report slightly wrong fence count here.
	 */
	radeon_fence_process(rdev, ring);
	emitted = rdev->fence_drv[ring].seq - atomic64_read(&rdev->fence_drv[ring].last_seq);
	/* to avoid 32bits warp around */
	if (emitted > 0x10000000) {
		emitted = 0x10000000;
	}
	return (unsigned)emitted;
}

int radeon_fence_driver_start_ring(struct radeon_device *rdev, int ring)
{
	uint64_t index;
	int r;

	radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
	if (rdev->wb.use_event) {
		rdev->fence_drv[ring].scratch_reg = 0;
		index = R600_WB_EVENT_OFFSET + ring * 4;
	} else {
		r = radeon_scratch_get(rdev, &rdev->fence_drv[ring].scratch_reg);
		if (r) {
			dev_err(rdev->dev, "fence failed to get scratch register\n");
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
	dev_info(rdev->dev, "fence driver on ring %d use gpu addr 0x%016llx and cpu addr 0x%p\n",
		 ring, rdev->fence_drv[ring].gpu_addr, rdev->fence_drv[ring].cpu_addr);
	return 0;
}

static void radeon_fence_driver_init_ring(struct radeon_device *rdev, int ring)
{
	rdev->fence_drv[ring].scratch_reg = -1;
	rdev->fence_drv[ring].cpu_addr = NULL;
	rdev->fence_drv[ring].gpu_addr = 0;
	rdev->fence_drv[ring].seq = 0;
	atomic64_set(&rdev->fence_drv[ring].last_seq, 0);
	rdev->fence_drv[ring].last_activity = jiffies;
	rdev->fence_drv[ring].initialized = false;
}

int radeon_fence_driver_init(struct radeon_device *rdev)
{
	int ring;

	init_waitqueue_head(&rdev->fence_queue);
	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		radeon_fence_driver_init_ring(rdev, ring);
	}
	if (radeon_debugfs_fence_init(rdev)) {
		dev_err(rdev->dev, "fence debugfs file creation failed\n");
	}
	return 0;
}

void radeon_fence_driver_fini(struct radeon_device *rdev)
{
	int ring;

	mutex_lock(&rdev->ring_lock);
	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		if (!rdev->fence_drv[ring].initialized)
			continue;
		radeon_fence_wait_empty_locked(rdev, ring);
		wake_up_all(&rdev->fence_queue);
		radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
		rdev->fence_drv[ring].initialized = false;
	}
	mutex_unlock(&rdev->ring_lock);
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
	int i;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!rdev->fence_drv[i].initialized)
			continue;

		seq_printf(m, "--- ring %d ---\n", i);
		seq_printf(m, "Last signaled fence 0x%016llx\n",
			   (unsigned long long)atomic64_read(&rdev->fence_drv[i].last_seq));
		seq_printf(m, "Last emitted  0x%016llx\n",
			   rdev->fence_drv[i].seq);
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
