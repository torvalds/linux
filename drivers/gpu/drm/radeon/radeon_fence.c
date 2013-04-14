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
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_trace.h"

/*
 * Fences
 * Fences mark an event in the GPUs pipeline and are used
 * for GPU/CPU synchronization.  When the fence is written,
 * it is expected that all buffers associated with that fence
 * are no longer in use by the associated ring on the GPU and
 * that the the relevant GPU caches have been flushed.  Whether
 * we use a scratch register or memory location depends on the asic
 * and whether writeback is enabled.
 */

/**
 * radeon_fence_write - write a fence value
 *
 * @rdev: radeon_device pointer
 * @seq: sequence number to write
 * @ring: ring index the fence is associated with
 *
 * Writes a fence value to memory or a scratch register (all asics).
 */
static void radeon_fence_write(struct radeon_device *rdev, u32 seq, int ring)
{
	struct radeon_fence_driver *drv = &rdev->fence_drv[ring];
	if (likely(rdev->wb.enabled || !drv->scratch_reg)) {
		*drv->cpu_addr = cpu_to_le32(seq);
	} else {
		WREG32(drv->scratch_reg, seq);
	}
}

/**
 * radeon_fence_read - read a fence value
 *
 * @rdev: radeon_device pointer
 * @ring: ring index the fence is associated with
 *
 * Reads a fence value from memory or a scratch register (all asics).
 * Returns the value of the fence read from memory or register.
 */
static u32 radeon_fence_read(struct radeon_device *rdev, int ring)
{
	struct radeon_fence_driver *drv = &rdev->fence_drv[ring];
	u32 seq = 0;

	if (likely(rdev->wb.enabled || !drv->scratch_reg)) {
		seq = le32_to_cpu(*drv->cpu_addr);
	} else {
		seq = RREG32(drv->scratch_reg);
	}
	return seq;
}

/**
 * radeon_fence_emit - emit a fence on the requested ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 * @ring: ring index the fence is associated with
 *
 * Emits a fence command on the requested ring (all asics).
 * Returns 0 on success, -ENOMEM on failure.
 */
int radeon_fence_emit(struct radeon_device *rdev,
		      struct radeon_fence **fence,
		      int ring)
{
	/* we are protected by the ring emission mutex */
	*fence = kmalloc(sizeof(struct radeon_fence), GFP_KERNEL);
	if ((*fence) == NULL) {
		return -ENOMEM;
	}
	kref_init(&((*fence)->kref));
	(*fence)->rdev = rdev;
	(*fence)->seq = ++rdev->fence_drv[ring].sync_seq[ring];
	(*fence)->ring = ring;
	radeon_fence_ring_emit(rdev, ring, *fence);
	trace_radeon_fence_emit(rdev->ddev, (*fence)->seq);
	return 0;
}

/**
 * radeon_fence_process - process a fence
 *
 * @rdev: radeon_device pointer
 * @ring: ring index the fence is associated with
 *
 * Checks the current fence value and wakes the fence queue
 * if the sequence number has increased (all asics).
 */
void radeon_fence_process(struct radeon_device *rdev, int ring)
{
	uint64_t seq, last_seq, last_emitted;
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
		last_emitted = rdev->fence_drv[ring].sync_seq[ring];
		seq = radeon_fence_read(rdev, ring);
		seq |= last_seq & 0xffffffff00000000LL;
		if (seq < last_seq) {
			seq &= 0xffffffff;
			seq |= last_emitted & 0xffffffff00000000LL;
		}

		if (seq <= last_seq || seq > last_emitted) {
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

/**
 * radeon_fence_destroy - destroy a fence
 *
 * @kref: fence kref
 *
 * Frees the fence object (all asics).
 */
static void radeon_fence_destroy(struct kref *kref)
{
	struct radeon_fence *fence;

	fence = container_of(kref, struct radeon_fence, kref);
	kfree(fence);
}

/**
 * radeon_fence_seq_signaled - check if a fence sequeuce number has signaled
 *
 * @rdev: radeon device pointer
 * @seq: sequence number
 * @ring: ring index the fence is associated with
 *
 * Check if the last singled fence sequnce number is >= the requested
 * sequence number (all asics).
 * Returns true if the fence has signaled (current fence value
 * is >= requested value) or false if it has not (current fence
 * value is < the requested value.  Helper function for
 * radeon_fence_signaled().
 */
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

/**
 * radeon_fence_signaled - check if a fence has signaled
 *
 * @fence: radeon fence object
 *
 * Check if the requested fence has signaled (all asics).
 * Returns true if the fence has signaled or false if it has not.
 */
bool radeon_fence_signaled(struct radeon_fence *fence)
{
	if (!fence) {
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

/**
 * radeon_fence_wait_seq - wait for a specific sequence number
 *
 * @rdev: radeon device pointer
 * @target_seq: sequence number we want to wait for
 * @ring: ring index the fence is associated with
 * @intr: use interruptable sleep
 * @lock_ring: whether the ring should be locked or not
 *
 * Wait for the requested sequence number to be written (all asics).
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the sequence number.  Helper function
 * for radeon_fence_wait(), et al.
 * Returns 0 if the sequence number has passed, error for all other cases.
 * -EDEADLK is returned when a GPU lockup has been detected and the ring is
 * marked as not ready so no further jobs get scheduled until a successful
 * reset.
 */
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

/**
 * radeon_fence_wait - wait for a fence to signal
 *
 * @fence: radeon fence object
 * @intr: use interruptable sleep
 *
 * Wait for the requested fence to signal (all asics).
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the fence.
 * Returns 0 if the fence has passed, error for all other cases.
 */
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

static bool radeon_fence_any_seq_signaled(struct radeon_device *rdev, u64 *seq)
{
	unsigned i;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (seq[i] && radeon_fence_seq_signaled(rdev, seq[i], i)) {
			return true;
		}
	}
	return false;
}

/**
 * radeon_fence_wait_any_seq - wait for a sequence number on any ring
 *
 * @rdev: radeon device pointer
 * @target_seq: sequence number(s) we want to wait for
 * @intr: use interruptable sleep
 *
 * Wait for the requested sequence number(s) to be written by any ring
 * (all asics).  Sequnce number array is indexed by ring id.
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the sequence number.  Helper function
 * for radeon_fence_wait_any(), et al.
 * Returns 0 if the sequence number has passed, error for all other cases.
 */
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
		return -ENOENT;
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

/**
 * radeon_fence_wait_any - wait for a fence to signal on any ring
 *
 * @rdev: radeon device pointer
 * @fences: radeon fence object(s)
 * @intr: use interruptable sleep
 *
 * Wait for any requested fence to signal (all asics).  Fence
 * array is indexed by ring id.  @intr selects whether to use
 * interruptable (true) or non-interruptable (false) sleep when
 * waiting for the fences. Used by the suballocator.
 * Returns 0 if any fence has passed, error for all other cases.
 */
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

		seq[i] = fences[i]->seq;
	}

	r = radeon_fence_wait_any_seq(rdev, seq, intr);
	if (r) {
		return r;
	}
	return 0;
}

/**
 * radeon_fence_wait_next_locked - wait for the next fence to signal
 *
 * @rdev: radeon device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for the next fence on the requested ring to signal (all asics).
 * Returns 0 if the next fence has passed, error for all other cases.
 * Caller must hold ring lock.
 */
int radeon_fence_wait_next_locked(struct radeon_device *rdev, int ring)
{
	uint64_t seq;

	seq = atomic64_read(&rdev->fence_drv[ring].last_seq) + 1ULL;
	if (seq >= rdev->fence_drv[ring].sync_seq[ring]) {
		/* nothing to wait for, last_seq is
		   already the last emited fence */
		return -ENOENT;
	}
	return radeon_fence_wait_seq(rdev, seq, ring, false, false);
}

/**
 * radeon_fence_wait_empty_locked - wait for all fences to signal
 *
 * @rdev: radeon device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns 0 if the fences have passed, error for all other cases.
 * Caller must hold ring lock.
 */
int radeon_fence_wait_empty_locked(struct radeon_device *rdev, int ring)
{
	uint64_t seq = rdev->fence_drv[ring].sync_seq[ring];
	int r;

	r = radeon_fence_wait_seq(rdev, seq, ring, false, false);
	if (r) {
		if (r == -EDEADLK) {
			return -EDEADLK;
		}
		dev_err(rdev->dev, "error waiting for ring[%d] to become idle (%d)\n",
			ring, r);
	}
	return 0;
}

/**
 * radeon_fence_ref - take a ref on a fence
 *
 * @fence: radeon fence object
 *
 * Take a reference on a fence (all asics).
 * Returns the fence.
 */
struct radeon_fence *radeon_fence_ref(struct radeon_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

/**
 * radeon_fence_unref - remove a ref on a fence
 *
 * @fence: radeon fence object
 *
 * Remove a reference on a fence (all asics).
 */
void radeon_fence_unref(struct radeon_fence **fence)
{
	struct radeon_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, radeon_fence_destroy);
	}
}

/**
 * radeon_fence_count_emitted - get the count of emitted fences
 *
 * @rdev: radeon device pointer
 * @ring: ring index the fence is associated with
 *
 * Get the number of fences emitted on the requested ring (all asics).
 * Returns the number of emitted fences on the ring.  Used by the
 * dynpm code to ring track activity.
 */
unsigned radeon_fence_count_emitted(struct radeon_device *rdev, int ring)
{
	uint64_t emitted;

	/* We are not protected by ring lock when reading the last sequence
	 * but it's ok to report slightly wrong fence count here.
	 */
	radeon_fence_process(rdev, ring);
	emitted = rdev->fence_drv[ring].sync_seq[ring]
		- atomic64_read(&rdev->fence_drv[ring].last_seq);
	/* to avoid 32bits warp around */
	if (emitted > 0x10000000) {
		emitted = 0x10000000;
	}
	return (unsigned)emitted;
}

/**
 * radeon_fence_need_sync - do we need a semaphore
 *
 * @fence: radeon fence object
 * @dst_ring: which ring to check against
 *
 * Check if the fence needs to be synced against another ring
 * (all asics).  If so, we need to emit a semaphore.
 * Returns true if we need to sync with another ring, false if
 * not.
 */
bool radeon_fence_need_sync(struct radeon_fence *fence, int dst_ring)
{
	struct radeon_fence_driver *fdrv;

	if (!fence) {
		return false;
	}

	if (fence->ring == dst_ring) {
		return false;
	}

	/* we are protected by the ring mutex */
	fdrv = &fence->rdev->fence_drv[dst_ring];
	if (fence->seq <= fdrv->sync_seq[fence->ring]) {
		return false;
	}

	return true;
}

/**
 * radeon_fence_note_sync - record the sync point
 *
 * @fence: radeon fence object
 * @dst_ring: which ring to check against
 *
 * Note the sequence number at which point the fence will
 * be synced with the requested ring (all asics).
 */
void radeon_fence_note_sync(struct radeon_fence *fence, int dst_ring)
{
	struct radeon_fence_driver *dst, *src;
	unsigned i;

	if (!fence) {
		return;
	}

	if (fence->ring == dst_ring) {
		return;
	}

	/* we are protected by the ring mutex */
	src = &fence->rdev->fence_drv[fence->ring];
	dst = &fence->rdev->fence_drv[dst_ring];
	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (i == dst_ring) {
			continue;
		}
		dst->sync_seq[i] = max(dst->sync_seq[i], src->sync_seq[i]);
	}
}

/**
 * radeon_fence_driver_start_ring - make the fence driver
 * ready for use on the requested ring.
 *
 * @rdev: radeon device pointer
 * @ring: ring index to start the fence driver on
 *
 * Make the fence driver ready for processing (all asics).
 * Not all asics have all rings, so each asic will only
 * start the fence driver on the rings it has.
 * Returns 0 for success, errors for failure.
 */
int radeon_fence_driver_start_ring(struct radeon_device *rdev, int ring)
{
	uint64_t index;
	int r;

	radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
	if (rdev->wb.use_event || !radeon_ring_supports_scratch_reg(rdev, &rdev->ring[ring])) {
		if (ring != R600_RING_TYPE_UVD_INDEX) {
			rdev->fence_drv[ring].scratch_reg = 0;
			index = R600_WB_EVENT_OFFSET + ring * 4;
			rdev->fence_drv[ring].cpu_addr = &rdev->wb.wb[index/4];
			rdev->fence_drv[ring].gpu_addr = rdev->wb.gpu_addr +
							 index;

		} else {
			/* put fence directly behind firmware */
			index = ALIGN(rdev->uvd_fw->size, 8);
			rdev->fence_drv[ring].cpu_addr = rdev->uvd.cpu_addr + index;
			rdev->fence_drv[ring].gpu_addr = rdev->uvd.gpu_addr + index;
		}

	} else {
		r = radeon_scratch_get(rdev, &rdev->fence_drv[ring].scratch_reg);
		if (r) {
			dev_err(rdev->dev, "fence failed to get scratch register\n");
			return r;
		}
		index = RADEON_WB_SCRATCH_OFFSET +
			rdev->fence_drv[ring].scratch_reg -
			rdev->scratch.reg_base;
		rdev->fence_drv[ring].cpu_addr = &rdev->wb.wb[index/4];
		rdev->fence_drv[ring].gpu_addr = rdev->wb.gpu_addr + index;
	}
	radeon_fence_write(rdev, atomic64_read(&rdev->fence_drv[ring].last_seq), ring);
	rdev->fence_drv[ring].initialized = true;
	dev_info(rdev->dev, "fence driver on ring %d use gpu addr 0x%016llx and cpu addr 0x%p\n",
		 ring, rdev->fence_drv[ring].gpu_addr, rdev->fence_drv[ring].cpu_addr);
	return 0;
}

/**
 * radeon_fence_driver_init_ring - init the fence driver
 * for the requested ring.
 *
 * @rdev: radeon device pointer
 * @ring: ring index to start the fence driver on
 *
 * Init the fence driver for the requested ring (all asics).
 * Helper function for radeon_fence_driver_init().
 */
static void radeon_fence_driver_init_ring(struct radeon_device *rdev, int ring)
{
	int i;

	rdev->fence_drv[ring].scratch_reg = -1;
	rdev->fence_drv[ring].cpu_addr = NULL;
	rdev->fence_drv[ring].gpu_addr = 0;
	for (i = 0; i < RADEON_NUM_RINGS; ++i)
		rdev->fence_drv[ring].sync_seq[i] = 0;
	atomic64_set(&rdev->fence_drv[ring].last_seq, 0);
	rdev->fence_drv[ring].last_activity = jiffies;
	rdev->fence_drv[ring].initialized = false;
}

/**
 * radeon_fence_driver_init - init the fence driver
 * for all possible rings.
 *
 * @rdev: radeon device pointer
 *
 * Init the fence driver for all possible rings (all asics).
 * Not all asics have all rings, so each asic will only
 * start the fence driver on the rings it has using
 * radeon_fence_driver_start_ring().
 * Returns 0 for success.
 */
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

/**
 * radeon_fence_driver_fini - tear down the fence driver
 * for all possible rings.
 *
 * @rdev: radeon device pointer
 *
 * Tear down the fence driver for all possible rings (all asics).
 */
void radeon_fence_driver_fini(struct radeon_device *rdev)
{
	int ring, r;

	mutex_lock(&rdev->ring_lock);
	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		if (!rdev->fence_drv[ring].initialized)
			continue;
		r = radeon_fence_wait_empty_locked(rdev, ring);
		if (r) {
			/* no need to trigger GPU reset as we are unloading */
			radeon_fence_driver_force_completion(rdev);
		}
		wake_up_all(&rdev->fence_queue);
		radeon_scratch_free(rdev, rdev->fence_drv[ring].scratch_reg);
		rdev->fence_drv[ring].initialized = false;
	}
	mutex_unlock(&rdev->ring_lock);
}

/**
 * radeon_fence_driver_force_completion - force all fence waiter to complete
 *
 * @rdev: radeon device pointer
 *
 * In case of GPU reset failure make sure no process keep waiting on fence
 * that will never complete.
 */
void radeon_fence_driver_force_completion(struct radeon_device *rdev)
{
	int ring;

	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		if (!rdev->fence_drv[ring].initialized)
			continue;
		radeon_fence_write(rdev, rdev->fence_drv[ring].sync_seq[ring], ring);
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
	int i, j;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!rdev->fence_drv[i].initialized)
			continue;

		seq_printf(m, "--- ring %d ---\n", i);
		seq_printf(m, "Last signaled fence 0x%016llx\n",
			   (unsigned long long)atomic64_read(&rdev->fence_drv[i].last_seq));
		seq_printf(m, "Last emitted        0x%016llx\n",
			   rdev->fence_drv[i].sync_seq[i]);

		for (j = 0; j < RADEON_NUM_RINGS; ++j) {
			if (i != j && rdev->fence_drv[j].initialized)
				seq_printf(m, "Last sync to ring %d 0x%016llx\n",
					   j, rdev->fence_drv[i].sync_seq[j]);
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
