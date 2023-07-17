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

#include <linux/atomic.h>
#include <linux/firmware.h>
#include <linux/kref.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>

#include "radeon.h"
#include "radeon_reg.h"
#include "radeon_trace.h"

/*
 * Fences
 * Fences mark an event in the GPUs pipeline and are used
 * for GPU/CPU synchronization.  When the fence is written,
 * it is expected that all buffers associated with that fence
 * are no longer in use by the associated ring on the GPU and
 * that the relevant GPU caches have been flushed.  Whether
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
		if (drv->cpu_addr) {
			*drv->cpu_addr = cpu_to_le32(seq);
		}
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
		if (drv->cpu_addr) {
			seq = le32_to_cpu(*drv->cpu_addr);
		} else {
			seq = lower_32_bits(atomic64_read(&drv->last_seq));
		}
	} else {
		seq = RREG32(drv->scratch_reg);
	}
	return seq;
}

/**
 * radeon_fence_schedule_check - schedule lockup check
 *
 * @rdev: radeon_device pointer
 * @ring: ring index we should work with
 *
 * Queues a delayed work item to check for lockups.
 */
static void radeon_fence_schedule_check(struct radeon_device *rdev, int ring)
{
	/*
	 * Do not reset the timer here with mod_delayed_work,
	 * this can livelock in an interaction with TTM delayed destroy.
	 */
	queue_delayed_work(system_power_efficient_wq,
			   &rdev->fence_drv[ring].lockup_work,
			   RADEON_FENCE_JIFFIES_TIMEOUT);
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
	u64 seq;

	/* we are protected by the ring emission mutex */
	*fence = kmalloc(sizeof(struct radeon_fence), GFP_KERNEL);
	if ((*fence) == NULL) {
		return -ENOMEM;
	}
	(*fence)->rdev = rdev;
	(*fence)->seq = seq = ++rdev->fence_drv[ring].sync_seq[ring];
	(*fence)->ring = ring;
	(*fence)->is_vm_update = false;
	dma_fence_init(&(*fence)->base, &radeon_fence_ops,
		       &rdev->fence_queue.lock,
		       rdev->fence_context + ring,
		       seq);
	radeon_fence_ring_emit(rdev, ring, *fence);
	trace_radeon_fence_emit(rdev->ddev, ring, (*fence)->seq);
	radeon_fence_schedule_check(rdev, ring);
	return 0;
}

/*
 * radeon_fence_check_signaled - callback from fence_queue
 *
 * this function is called with fence_queue lock held, which is also used
 * for the fence locking itself, so unlocked variants are used for
 * fence_signal, and remove_wait_queue.
 */
static int radeon_fence_check_signaled(wait_queue_entry_t *wait, unsigned mode, int flags, void *key)
{
	struct radeon_fence *fence;
	u64 seq;

	fence = container_of(wait, struct radeon_fence, fence_wake);

	/*
	 * We cannot use radeon_fence_process here because we're already
	 * in the waitqueue, in a call from wake_up_all.
	 */
	seq = atomic64_read(&fence->rdev->fence_drv[fence->ring].last_seq);
	if (seq >= fence->seq) {
		dma_fence_signal_locked(&fence->base);
		radeon_irq_kms_sw_irq_put(fence->rdev, fence->ring);
		__remove_wait_queue(&fence->rdev->fence_queue, &fence->fence_wake);
		dma_fence_put(&fence->base);
	}
	return 0;
}

/**
 * radeon_fence_activity - check for fence activity
 *
 * @rdev: radeon_device pointer
 * @ring: ring index the fence is associated with
 *
 * Checks the current fence value and calculates the last
 * signalled fence value. Returns true if activity occured
 * on the ring, and the fence_queue should be waken up.
 */
static bool radeon_fence_activity(struct radeon_device *rdev, int ring)
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

	if (seq < last_emitted)
		radeon_fence_schedule_check(rdev, ring);

	return wake;
}

/**
 * radeon_fence_check_lockup - check for hardware lockup
 *
 * @work: delayed work item
 *
 * Checks for fence activity and if there is none probe
 * the hardware if a lockup occured.
 */
static void radeon_fence_check_lockup(struct work_struct *work)
{
	struct radeon_fence_driver *fence_drv;
	struct radeon_device *rdev;
	int ring;

	fence_drv = container_of(work, struct radeon_fence_driver,
				 lockup_work.work);
	rdev = fence_drv->rdev;
	ring = fence_drv - &rdev->fence_drv[0];

	if (!down_read_trylock(&rdev->exclusive_lock)) {
		/* just reschedule the check if a reset is going on */
		radeon_fence_schedule_check(rdev, ring);
		return;
	}

	if (fence_drv->delayed_irq && rdev->irq.installed) {
		unsigned long irqflags;

		fence_drv->delayed_irq = false;
		spin_lock_irqsave(&rdev->irq.lock, irqflags);
		radeon_irq_set(rdev);
		spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	}

	if (radeon_fence_activity(rdev, ring))
		wake_up_all(&rdev->fence_queue);

	else if (radeon_ring_is_lockup(rdev, ring, &rdev->ring[ring])) {

		/* good news we believe it's a lockup */
		dev_warn(rdev->dev, "GPU lockup (current fence id "
			 "0x%016llx last fence id 0x%016llx on ring %d)\n",
			 (uint64_t)atomic64_read(&fence_drv->last_seq),
			 fence_drv->sync_seq[ring], ring);

		/* remember that we need an reset */
		rdev->needs_reset = true;
		wake_up_all(&rdev->fence_queue);
	}
	up_read(&rdev->exclusive_lock);
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
	if (radeon_fence_activity(rdev, ring))
		wake_up_all(&rdev->fence_queue);
}

/**
 * radeon_fence_seq_signaled - check if a fence sequence number has signaled
 *
 * @rdev: radeon device pointer
 * @seq: sequence number
 * @ring: ring index the fence is associated with
 *
 * Check if the last signaled fence sequnce number is >= the requested
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

static bool radeon_fence_is_signaled(struct dma_fence *f)
{
	struct radeon_fence *fence = to_radeon_fence(f);
	struct radeon_device *rdev = fence->rdev;
	unsigned ring = fence->ring;
	u64 seq = fence->seq;

	if (atomic64_read(&rdev->fence_drv[ring].last_seq) >= seq) {
		return true;
	}

	if (down_read_trylock(&rdev->exclusive_lock)) {
		radeon_fence_process(rdev, ring);
		up_read(&rdev->exclusive_lock);

		if (atomic64_read(&rdev->fence_drv[ring].last_seq) >= seq) {
			return true;
		}
	}
	return false;
}

/**
 * radeon_fence_enable_signaling - enable signalling on fence
 * @f: fence
 *
 * This function is called with fence_queue lock held, and adds a callback
 * to fence_queue that checks if this fence is signaled, and if so it
 * signals the fence and removes itself.
 */
static bool radeon_fence_enable_signaling(struct dma_fence *f)
{
	struct radeon_fence *fence = to_radeon_fence(f);
	struct radeon_device *rdev = fence->rdev;

	if (atomic64_read(&rdev->fence_drv[fence->ring].last_seq) >= fence->seq)
		return false;

	if (down_read_trylock(&rdev->exclusive_lock)) {
		radeon_irq_kms_sw_irq_get(rdev, fence->ring);

		if (radeon_fence_activity(rdev, fence->ring))
			wake_up_all_locked(&rdev->fence_queue);

		/* did fence get signaled after we enabled the sw irq? */
		if (atomic64_read(&rdev->fence_drv[fence->ring].last_seq) >= fence->seq) {
			radeon_irq_kms_sw_irq_put(rdev, fence->ring);
			up_read(&rdev->exclusive_lock);
			return false;
		}

		up_read(&rdev->exclusive_lock);
	} else {
		/* we're probably in a lockup, lets not fiddle too much */
		if (radeon_irq_kms_sw_irq_get_delayed(rdev, fence->ring))
			rdev->fence_drv[fence->ring].delayed_irq = true;
		radeon_fence_schedule_check(rdev, fence->ring);
	}

	fence->fence_wake.flags = 0;
	fence->fence_wake.private = NULL;
	fence->fence_wake.func = radeon_fence_check_signaled;
	__add_wait_queue(&rdev->fence_queue, &fence->fence_wake);
	dma_fence_get(f);
	return true;
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
	if (!fence)
		return true;

	if (radeon_fence_seq_signaled(fence->rdev, fence->seq, fence->ring)) {
		dma_fence_signal(&fence->base);
		return true;
	}
	return false;
}

/**
 * radeon_fence_any_seq_signaled - check if any sequence number is signaled
 *
 * @rdev: radeon device pointer
 * @seq: sequence numbers
 *
 * Check if the last signaled fence sequnce number is >= the requested
 * sequence number (all asics).
 * Returns true if any has signaled (current value is >= requested value)
 * or false if it has not. Helper function for radeon_fence_wait_seq.
 */
static bool radeon_fence_any_seq_signaled(struct radeon_device *rdev, u64 *seq)
{
	unsigned i;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (seq[i] && radeon_fence_seq_signaled(rdev, seq[i], i))
			return true;
	}
	return false;
}

/**
 * radeon_fence_wait_seq_timeout - wait for a specific sequence numbers
 *
 * @rdev: radeon device pointer
 * @target_seq: sequence number(s) we want to wait for
 * @intr: use interruptable sleep
 * @timeout: maximum time to wait, or MAX_SCHEDULE_TIMEOUT for infinite wait
 *
 * Wait for the requested sequence number(s) to be written by any ring
 * (all asics).  Sequnce number array is indexed by ring id.
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the sequence number.  Helper function
 * for radeon_fence_wait_*().
 * Returns remaining time if the sequence number has passed, 0 when
 * the wait timeout, or an error for all other cases.
 * -EDEADLK is returned when a GPU lockup has been detected.
 */
static long radeon_fence_wait_seq_timeout(struct radeon_device *rdev,
					  u64 *target_seq, bool intr,
					  long timeout)
{
	long r;
	int i;

	if (radeon_fence_any_seq_signaled(rdev, target_seq))
		return timeout;

	/* enable IRQs and tracing */
	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!target_seq[i])
			continue;

		trace_radeon_fence_wait_begin(rdev->ddev, i, target_seq[i]);
		radeon_irq_kms_sw_irq_get(rdev, i);
	}

	if (intr) {
		r = wait_event_interruptible_timeout(rdev->fence_queue, (
			radeon_fence_any_seq_signaled(rdev, target_seq)
			 || rdev->needs_reset), timeout);
	} else {
		r = wait_event_timeout(rdev->fence_queue, (
			radeon_fence_any_seq_signaled(rdev, target_seq)
			 || rdev->needs_reset), timeout);
	}

	if (rdev->needs_reset)
		r = -EDEADLK;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!target_seq[i])
			continue;

		radeon_irq_kms_sw_irq_put(rdev, i);
		trace_radeon_fence_wait_end(rdev->ddev, i, target_seq[i]);
	}

	return r;
}

/**
 * radeon_fence_wait_timeout - wait for a fence to signal with timeout
 *
 * @fence: radeon fence object
 * @intr: use interruptible sleep
 *
 * Wait for the requested fence to signal (all asics).
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the fence.
 * @timeout: maximum time to wait, or MAX_SCHEDULE_TIMEOUT for infinite wait
 * Returns remaining time if the sequence number has passed, 0 when
 * the wait timeout, or an error for all other cases.
 */
long radeon_fence_wait_timeout(struct radeon_fence *fence, bool intr, long timeout)
{
	uint64_t seq[RADEON_NUM_RINGS] = {};
	long r;

	/*
	 * This function should not be called on !radeon fences.
	 * If this is the case, it would mean this function can
	 * also be called on radeon fences belonging to another card.
	 * exclusive_lock is not held in that case.
	 */
	if (WARN_ON_ONCE(!to_radeon_fence(&fence->base)))
		return dma_fence_wait(&fence->base, intr);

	seq[fence->ring] = fence->seq;
	r = radeon_fence_wait_seq_timeout(fence->rdev, seq, intr, timeout);
	if (r <= 0) {
		return r;
	}

	dma_fence_signal(&fence->base);
	return r;
}

/**
 * radeon_fence_wait - wait for a fence to signal
 *
 * @fence: radeon fence object
 * @intr: use interruptible sleep
 *
 * Wait for the requested fence to signal (all asics).
 * @intr selects whether to use interruptable (true) or non-interruptable
 * (false) sleep when waiting for the fence.
 * Returns 0 if the fence has passed, error for all other cases.
 */
int radeon_fence_wait(struct radeon_fence *fence, bool intr)
{
	long r = radeon_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);
	if (r > 0) {
		return 0;
	} else {
		return r;
	}
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
	unsigned i, num_rings = 0;
	long r;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		seq[i] = 0;

		if (!fences[i]) {
			continue;
		}

		seq[i] = fences[i]->seq;
		++num_rings;
	}

	/* nothing to wait for ? */
	if (num_rings == 0)
		return -ENOENT;

	r = radeon_fence_wait_seq_timeout(rdev, seq, intr, MAX_SCHEDULE_TIMEOUT);
	if (r < 0) {
		return r;
	}
	return 0;
}

/**
 * radeon_fence_wait_next - wait for the next fence to signal
 *
 * @rdev: radeon device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for the next fence on the requested ring to signal (all asics).
 * Returns 0 if the next fence has passed, error for all other cases.
 * Caller must hold ring lock.
 */
int radeon_fence_wait_next(struct radeon_device *rdev, int ring)
{
	uint64_t seq[RADEON_NUM_RINGS] = {};
	long r;

	seq[ring] = atomic64_read(&rdev->fence_drv[ring].last_seq) + 1ULL;
	if (seq[ring] >= rdev->fence_drv[ring].sync_seq[ring]) {
		/* nothing to wait for, last_seq is
		   already the last emited fence */
		return -ENOENT;
	}
	r = radeon_fence_wait_seq_timeout(rdev, seq, false, MAX_SCHEDULE_TIMEOUT);
	if (r < 0)
		return r;
	return 0;
}

/**
 * radeon_fence_wait_empty - wait for all fences to signal
 *
 * @rdev: radeon device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns 0 if the fences have passed, error for all other cases.
 * Caller must hold ring lock.
 */
int radeon_fence_wait_empty(struct radeon_device *rdev, int ring)
{
	uint64_t seq[RADEON_NUM_RINGS] = {};
	long r;

	seq[ring] = rdev->fence_drv[ring].sync_seq[ring];
	if (!seq[ring])
		return 0;

	r = radeon_fence_wait_seq_timeout(rdev, seq, false, MAX_SCHEDULE_TIMEOUT);
	if (r < 0) {
		if (r == -EDEADLK)
			return -EDEADLK;

		dev_err(rdev->dev, "error waiting for ring[%d] to become idle (%ld)\n",
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
	dma_fence_get(&fence->base);
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
		dma_fence_put(&tmp->base);
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
		rdev->fence_drv[ring].scratch_reg = 0;
		if (ring != R600_RING_TYPE_UVD_INDEX) {
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
	dev_info(rdev->dev, "fence driver on ring %d use gpu addr 0x%016llx\n",
		 ring, rdev->fence_drv[ring].gpu_addr);
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
	rdev->fence_drv[ring].initialized = false;
	INIT_DELAYED_WORK(&rdev->fence_drv[ring].lockup_work,
			  radeon_fence_check_lockup);
	rdev->fence_drv[ring].rdev = rdev;
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
 */
void radeon_fence_driver_init(struct radeon_device *rdev)
{
	int ring;

	init_waitqueue_head(&rdev->fence_queue);
	for (ring = 0; ring < RADEON_NUM_RINGS; ring++) {
		radeon_fence_driver_init_ring(rdev, ring);
	}

	radeon_debugfs_fence_init(rdev);
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
		r = radeon_fence_wait_empty(rdev, ring);
		if (r) {
			/* no need to trigger GPU reset as we are unloading */
			radeon_fence_driver_force_completion(rdev, ring);
		}
		cancel_delayed_work_sync(&rdev->fence_drv[ring].lockup_work);
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
 * @ring: the ring to complete
 *
 * In case of GPU reset failure make sure no process keep waiting on fence
 * that will never complete.
 */
void radeon_fence_driver_force_completion(struct radeon_device *rdev, int ring)
{
	if (rdev->fence_drv[ring].initialized) {
		radeon_fence_write(rdev, rdev->fence_drv[ring].sync_seq[ring], ring);
		cancel_delayed_work_sync(&rdev->fence_drv[ring].lockup_work);
	}
}


/*
 * Fence debugfs
 */
#if defined(CONFIG_DEBUG_FS)
static int radeon_debugfs_fence_info_show(struct seq_file *m, void *data)
{
	struct radeon_device *rdev = m->private;
	int i, j;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		if (!rdev->fence_drv[i].initialized)
			continue;

		radeon_fence_process(rdev, i);

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

/*
 * radeon_debugfs_gpu_reset - manually trigger a gpu reset
 *
 * Manually trigger a gpu reset at the next fence wait.
 */
static int radeon_debugfs_gpu_reset(void *data, u64 *val)
{
	struct radeon_device *rdev = (struct radeon_device *)data;

	down_read(&rdev->exclusive_lock);
	*val = rdev->needs_reset;
	rdev->needs_reset = true;
	wake_up_all(&rdev->fence_queue);
	up_read(&rdev->exclusive_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(radeon_debugfs_fence_info);
DEFINE_DEBUGFS_ATTRIBUTE(radeon_debugfs_gpu_reset_fops,
			 radeon_debugfs_gpu_reset, NULL, "%lld\n");
#endif

void radeon_debugfs_fence_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	struct dentry *root = rdev->ddev->primary->debugfs_root;

	debugfs_create_file("radeon_gpu_reset", 0444, root, rdev,
			    &radeon_debugfs_gpu_reset_fops);
	debugfs_create_file("radeon_fence_info", 0444, root, rdev,
			    &radeon_debugfs_fence_info_fops);


#endif
}

static const char *radeon_fence_get_driver_name(struct dma_fence *fence)
{
	return "radeon";
}

static const char *radeon_fence_get_timeline_name(struct dma_fence *f)
{
	struct radeon_fence *fence = to_radeon_fence(f);
	switch (fence->ring) {
	case RADEON_RING_TYPE_GFX_INDEX: return "radeon.gfx";
	case CAYMAN_RING_TYPE_CP1_INDEX: return "radeon.cp1";
	case CAYMAN_RING_TYPE_CP2_INDEX: return "radeon.cp2";
	case R600_RING_TYPE_DMA_INDEX: return "radeon.dma";
	case CAYMAN_RING_TYPE_DMA1_INDEX: return "radeon.dma1";
	case R600_RING_TYPE_UVD_INDEX: return "radeon.uvd";
	case TN_RING_TYPE_VCE1_INDEX: return "radeon.vce1";
	case TN_RING_TYPE_VCE2_INDEX: return "radeon.vce2";
	default: WARN_ON_ONCE(1); return "radeon.unk";
	}
}

static inline bool radeon_test_signaled(struct radeon_fence *fence)
{
	return test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->base.flags);
}

struct radeon_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void
radeon_fence_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct radeon_wait_cb *wait =
		container_of(cb, struct radeon_wait_cb, base);

	wake_up_process(wait->task);
}

static signed long radeon_fence_default_wait(struct dma_fence *f, bool intr,
					     signed long t)
{
	struct radeon_fence *fence = to_radeon_fence(f);
	struct radeon_device *rdev = fence->rdev;
	struct radeon_wait_cb cb;

	cb.task = current;

	if (dma_fence_add_callback(f, &cb.base, radeon_fence_wait_cb))
		return t;

	while (t > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		/*
		 * radeon_test_signaled must be called after
		 * set_current_state to prevent a race with wake_up_process
		 */
		if (radeon_test_signaled(fence))
			break;

		if (rdev->needs_reset) {
			t = -EDEADLK;
			break;
		}

		t = schedule_timeout(t);

		if (t > 0 && intr && signal_pending(current))
			t = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);
	dma_fence_remove_callback(f, &cb.base);

	return t;
}

const struct dma_fence_ops radeon_fence_ops = {
	.get_driver_name = radeon_fence_get_driver_name,
	.get_timeline_name = radeon_fence_get_timeline_name,
	.enable_signaling = radeon_fence_enable_signaling,
	.signaled = radeon_fence_is_signaled,
	.wait = radeon_fence_default_wait,
	.release = NULL,
};
