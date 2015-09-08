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
#include "amdgpu.h"
#include "amdgpu_trace.h"

/*
 * Fences
 * Fences mark an event in the GPUs pipeline and are used
 * for GPU/CPU synchronization.  When the fence is written,
 * it is expected that all buffers associated with that fence
 * are no longer in use by the associated ring on the GPU and
 * that the the relevant GPU caches have been flushed.
 */

/**
 * amdgpu_fence_write - write a fence value
 *
 * @ring: ring the fence is associated with
 * @seq: sequence number to write
 *
 * Writes a fence value to memory (all asics).
 */
static void amdgpu_fence_write(struct amdgpu_ring *ring, u32 seq)
{
	struct amdgpu_fence_driver *drv = &ring->fence_drv;

	if (drv->cpu_addr)
		*drv->cpu_addr = cpu_to_le32(seq);
}

/**
 * amdgpu_fence_read - read a fence value
 *
 * @ring: ring the fence is associated with
 *
 * Reads a fence value from memory (all asics).
 * Returns the value of the fence read from memory.
 */
static u32 amdgpu_fence_read(struct amdgpu_ring *ring)
{
	struct amdgpu_fence_driver *drv = &ring->fence_drv;
	u32 seq = 0;

	if (drv->cpu_addr)
		seq = le32_to_cpu(*drv->cpu_addr);
	else
		seq = lower_32_bits(atomic64_read(&drv->last_seq));

	return seq;
}

/**
 * amdgpu_fence_schedule_check - schedule lockup check
 *
 * @ring: pointer to struct amdgpu_ring
 *
 * Queues a delayed work item to check for lockups.
 */
static void amdgpu_fence_schedule_check(struct amdgpu_ring *ring)
{
	/*
	 * Do not reset the timer here with mod_delayed_work,
	 * this can livelock in an interaction with TTM delayed destroy.
	 */
	queue_delayed_work(system_power_efficient_wq,
		&ring->fence_drv.lockup_work,
		AMDGPU_FENCE_JIFFIES_TIMEOUT);
}

/**
 * amdgpu_fence_emit - emit a fence on the requested ring
 *
 * @ring: ring the fence is associated with
 * @owner: creator of the fence
 * @fence: amdgpu fence object
 *
 * Emits a fence command on the requested ring (all asics).
 * Returns 0 on success, -ENOMEM on failure.
 */
int amdgpu_fence_emit(struct amdgpu_ring *ring, void *owner,
		      struct amdgpu_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;

	/* we are protected by the ring emission mutex */
	*fence = kmalloc(sizeof(struct amdgpu_fence), GFP_KERNEL);
	if ((*fence) == NULL) {
		return -ENOMEM;
	}
	(*fence)->seq = ++ring->fence_drv.sync_seq[ring->idx];
	(*fence)->ring = ring;
	(*fence)->owner = owner;
	fence_init(&(*fence)->base, &amdgpu_fence_ops,
		&ring->fence_drv.fence_queue.lock,
		adev->fence_context + ring->idx,
		(*fence)->seq);
	amdgpu_ring_emit_fence(ring, ring->fence_drv.gpu_addr,
			       (*fence)->seq,
			       AMDGPU_FENCE_FLAG_INT);
	trace_amdgpu_fence_emit(ring->adev->ddev, ring->idx, (*fence)->seq);
	return 0;
}

/**
 * amdgpu_fence_check_signaled - callback from fence_queue
 *
 * this function is called with fence_queue lock held, which is also used
 * for the fence locking itself, so unlocked variants are used for
 * fence_signal, and remove_wait_queue.
 */
static int amdgpu_fence_check_signaled(wait_queue_t *wait, unsigned mode, int flags, void *key)
{
	struct amdgpu_fence *fence;
	struct amdgpu_device *adev;
	u64 seq;
	int ret;

	fence = container_of(wait, struct amdgpu_fence, fence_wake);
	adev = fence->ring->adev;

	/*
	 * We cannot use amdgpu_fence_process here because we're already
	 * in the waitqueue, in a call from wake_up_all.
	 */
	seq = atomic64_read(&fence->ring->fence_drv.last_seq);
	if (seq >= fence->seq) {
		ret = fence_signal_locked(&fence->base);
		if (!ret)
			FENCE_TRACE(&fence->base, "signaled from irq context\n");
		else
			FENCE_TRACE(&fence->base, "was already signaled\n");

		__remove_wait_queue(&fence->ring->fence_drv.fence_queue, &fence->fence_wake);
		fence_put(&fence->base);
	} else
		FENCE_TRACE(&fence->base, "pending\n");
	return 0;
}

/**
 * amdgpu_fence_activity - check for fence activity
 *
 * @ring: pointer to struct amdgpu_ring
 *
 * Checks the current fence value and calculates the last
 * signalled fence value. Returns true if activity occured
 * on the ring, and the fence_queue should be waken up.
 */
static bool amdgpu_fence_activity(struct amdgpu_ring *ring)
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
	 * continuously new fence signaled ie amdgpu_fence_read needs
	 * to return a different value each time for both the currently
	 * polling process and the other process that xchg the last_seq
	 * btw atomic read and xchg of the current process. And the
	 * value the other process set as last seq must be higher than
	 * the seq value we just read. Which means that current process
	 * need to be interrupted after amdgpu_fence_read and before
	 * atomic xchg.
	 *
	 * To be even more safe we count the number of time we loop and
	 * we bail after 10 loop just accepting the fact that we might
	 * have temporarly set the last_seq not to the true real last
	 * seq but to an older one.
	 */
	last_seq = atomic64_read(&ring->fence_drv.last_seq);
	do {
		last_emitted = ring->fence_drv.sync_seq[ring->idx];
		seq = amdgpu_fence_read(ring);
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
	} while (atomic64_xchg(&ring->fence_drv.last_seq, seq) > seq);

	if (seq < last_emitted)
		amdgpu_fence_schedule_check(ring);

	return wake;
}

/**
 * amdgpu_fence_check_lockup - check for hardware lockup
 *
 * @work: delayed work item
 *
 * Checks for fence activity and if there is none probe
 * the hardware if a lockup occured.
 */
static void amdgpu_fence_check_lockup(struct work_struct *work)
{
	struct amdgpu_fence_driver *fence_drv;
	struct amdgpu_ring *ring;

	fence_drv = container_of(work, struct amdgpu_fence_driver,
				lockup_work.work);
	ring = fence_drv->ring;

	if (!down_read_trylock(&ring->adev->exclusive_lock)) {
		/* just reschedule the check if a reset is going on */
		amdgpu_fence_schedule_check(ring);
		return;
	}

	if (amdgpu_fence_activity(ring)) {
		wake_up_all(&ring->fence_drv.fence_queue);
	}
	else if (amdgpu_ring_is_lockup(ring)) {
		/* good news we believe it's a lockup */
		dev_warn(ring->adev->dev, "GPU lockup (current fence id "
			"0x%016llx last fence id 0x%016llx on ring %d)\n",
			(uint64_t)atomic64_read(&fence_drv->last_seq),
			fence_drv->sync_seq[ring->idx], ring->idx);

		/* remember that we need an reset */
		ring->adev->needs_reset = true;
		wake_up_all(&ring->fence_drv.fence_queue);
	}
	up_read(&ring->adev->exclusive_lock);
}

/**
 * amdgpu_fence_process - process a fence
 *
 * @adev: amdgpu_device pointer
 * @ring: ring index the fence is associated with
 *
 * Checks the current fence value and wakes the fence queue
 * if the sequence number has increased (all asics).
 */
void amdgpu_fence_process(struct amdgpu_ring *ring)
{
	if (amdgpu_fence_activity(ring))
		wake_up_all(&ring->fence_drv.fence_queue);
}

/**
 * amdgpu_fence_seq_signaled - check if a fence sequence number has signaled
 *
 * @ring: ring the fence is associated with
 * @seq: sequence number
 *
 * Check if the last signaled fence sequnce number is >= the requested
 * sequence number (all asics).
 * Returns true if the fence has signaled (current fence value
 * is >= requested value) or false if it has not (current fence
 * value is < the requested value.  Helper function for
 * amdgpu_fence_signaled().
 */
static bool amdgpu_fence_seq_signaled(struct amdgpu_ring *ring, u64 seq)
{
	if (atomic64_read(&ring->fence_drv.last_seq) >= seq)
		return true;

	/* poll new last sequence at least once */
	amdgpu_fence_process(ring);
	if (atomic64_read(&ring->fence_drv.last_seq) >= seq)
		return true;

	return false;
}

static bool amdgpu_fence_is_signaled(struct fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	struct amdgpu_ring *ring = fence->ring;
	struct amdgpu_device *adev = ring->adev;

	if (atomic64_read(&ring->fence_drv.last_seq) >= fence->seq)
		return true;

	if (down_read_trylock(&adev->exclusive_lock)) {
		amdgpu_fence_process(ring);
		up_read(&adev->exclusive_lock);

		if (atomic64_read(&ring->fence_drv.last_seq) >= fence->seq)
			return true;
	}
	return false;
}

/**
 * amdgpu_fence_enable_signaling - enable signalling on fence
 * @fence: fence
 *
 * This function is called with fence_queue lock held, and adds a callback
 * to fence_queue that checks if this fence is signaled, and if so it
 * signals the fence and removes itself.
 */
static bool amdgpu_fence_enable_signaling(struct fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	struct amdgpu_ring *ring = fence->ring;

	if (atomic64_read(&ring->fence_drv.last_seq) >= fence->seq)
		return false;

	fence->fence_wake.flags = 0;
	fence->fence_wake.private = NULL;
	fence->fence_wake.func = amdgpu_fence_check_signaled;
	__add_wait_queue(&ring->fence_drv.fence_queue, &fence->fence_wake);
	fence_get(f);
	FENCE_TRACE(&fence->base, "armed on ring %i!\n", ring->idx);
	return true;
}

/*
 * amdgpu_ring_wait_seq_timeout - wait for seq of the specific ring to signal
 * @ring: ring to wait on for the seq number
 * @seq: seq number wait for
 *
 * return value:
 * 0: seq signaled, and gpu not hang
 * -EDEADL: GPU hang detected
 * -EINVAL: some paramter is not valid
 */
static int amdgpu_fence_ring_wait_seq(struct amdgpu_ring *ring, uint64_t seq)
{
	struct amdgpu_device *adev = ring->adev;
	bool signaled = false;

	BUG_ON(!ring);
	if (seq > ring->fence_drv.sync_seq[ring->idx])
		return -EINVAL;

	if (atomic64_read(&ring->fence_drv.last_seq) >= seq)
		return 0;

	wait_event(ring->fence_drv.fence_queue, (
		   (signaled = amdgpu_fence_seq_signaled(ring, seq))
		   || adev->needs_reset));

	if (signaled)
		return 0;
	else
		return -EDEADLK;
}

/**
 * amdgpu_fence_wait_next - wait for the next fence to signal
 *
 * @adev: amdgpu device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for the next fence on the requested ring to signal (all asics).
 * Returns 0 if the next fence has passed, error for all other cases.
 * Caller must hold ring lock.
 */
int amdgpu_fence_wait_next(struct amdgpu_ring *ring)
{
	uint64_t seq = atomic64_read(&ring->fence_drv.last_seq) + 1ULL;

	if (seq >= ring->fence_drv.sync_seq[ring->idx])
		return -ENOENT;

	return amdgpu_fence_ring_wait_seq(ring, seq);
}

/**
 * amdgpu_fence_wait_empty - wait for all fences to signal
 *
 * @adev: amdgpu device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns 0 if the fences have passed, error for all other cases.
 * Caller must hold ring lock.
 */
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring)
{
	uint64_t seq = ring->fence_drv.sync_seq[ring->idx];

	if (!seq)
		return 0;

	return amdgpu_fence_ring_wait_seq(ring, seq);
}

/**
 * amdgpu_fence_ref - take a ref on a fence
 *
 * @fence: amdgpu fence object
 *
 * Take a reference on a fence (all asics).
 * Returns the fence.
 */
struct amdgpu_fence *amdgpu_fence_ref(struct amdgpu_fence *fence)
{
	fence_get(&fence->base);
	return fence;
}

/**
 * amdgpu_fence_unref - remove a ref on a fence
 *
 * @fence: amdgpu fence object
 *
 * Remove a reference on a fence (all asics).
 */
void amdgpu_fence_unref(struct amdgpu_fence **fence)
{
	struct amdgpu_fence *tmp = *fence;

	*fence = NULL;
	if (tmp)
		fence_put(&tmp->base);
}

/**
 * amdgpu_fence_count_emitted - get the count of emitted fences
 *
 * @ring: ring the fence is associated with
 *
 * Get the number of fences emitted on the requested ring (all asics).
 * Returns the number of emitted fences on the ring.  Used by the
 * dynpm code to ring track activity.
 */
unsigned amdgpu_fence_count_emitted(struct amdgpu_ring *ring)
{
	uint64_t emitted;

	/* We are not protected by ring lock when reading the last sequence
	 * but it's ok to report slightly wrong fence count here.
	 */
	amdgpu_fence_process(ring);
	emitted = ring->fence_drv.sync_seq[ring->idx]
		- atomic64_read(&ring->fence_drv.last_seq);
	/* to avoid 32bits warp around */
	if (emitted > 0x10000000)
		emitted = 0x10000000;

	return (unsigned)emitted;
}

/**
 * amdgpu_fence_need_sync - do we need a semaphore
 *
 * @fence: amdgpu fence object
 * @dst_ring: which ring to check against
 *
 * Check if the fence needs to be synced against another ring
 * (all asics).  If so, we need to emit a semaphore.
 * Returns true if we need to sync with another ring, false if
 * not.
 */
bool amdgpu_fence_need_sync(struct amdgpu_fence *fence,
			    struct amdgpu_ring *dst_ring)
{
	struct amdgpu_fence_driver *fdrv;

	if (!fence)
		return false;

	if (fence->ring == dst_ring)
		return false;

	/* we are protected by the ring mutex */
	fdrv = &dst_ring->fence_drv;
	if (fence->seq <= fdrv->sync_seq[fence->ring->idx])
		return false;

	return true;
}

/**
 * amdgpu_fence_note_sync - record the sync point
 *
 * @fence: amdgpu fence object
 * @dst_ring: which ring to check against
 *
 * Note the sequence number at which point the fence will
 * be synced with the requested ring (all asics).
 */
void amdgpu_fence_note_sync(struct amdgpu_fence *fence,
			    struct amdgpu_ring *dst_ring)
{
	struct amdgpu_fence_driver *dst, *src;
	unsigned i;

	if (!fence)
		return;

	if (fence->ring == dst_ring)
		return;

	/* we are protected by the ring mutex */
	src = &fence->ring->fence_drv;
	dst = &dst_ring->fence_drv;
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		if (i == dst_ring->idx)
			continue;

		dst->sync_seq[i] = max(dst->sync_seq[i], src->sync_seq[i]);
	}
}

/**
 * amdgpu_fence_driver_start_ring - make the fence driver
 * ready for use on the requested ring.
 *
 * @ring: ring to start the fence driver on
 * @irq_src: interrupt source to use for this ring
 * @irq_type: interrupt type to use for this ring
 *
 * Make the fence driver ready for processing (all asics).
 * Not all asics have all rings, so each asic will only
 * start the fence driver on the rings it has.
 * Returns 0 for success, errors for failure.
 */
int amdgpu_fence_driver_start_ring(struct amdgpu_ring *ring,
				   struct amdgpu_irq_src *irq_src,
				   unsigned irq_type)
{
	struct amdgpu_device *adev = ring->adev;
	uint64_t index;

	if (ring != &adev->uvd.ring) {
		ring->fence_drv.cpu_addr = &adev->wb.wb[ring->fence_offs];
		ring->fence_drv.gpu_addr = adev->wb.gpu_addr + (ring->fence_offs * 4);
	} else {
		/* put fence directly behind firmware */
		index = ALIGN(adev->uvd.fw->size, 8);
		ring->fence_drv.cpu_addr = adev->uvd.cpu_addr + index;
		ring->fence_drv.gpu_addr = adev->uvd.gpu_addr + index;
	}
	amdgpu_fence_write(ring, atomic64_read(&ring->fence_drv.last_seq));
	amdgpu_irq_get(adev, irq_src, irq_type);

	ring->fence_drv.irq_src = irq_src;
	ring->fence_drv.irq_type = irq_type;
	ring->fence_drv.initialized = true;

	dev_info(adev->dev, "fence driver on ring %d use gpu addr 0x%016llx, "
		 "cpu addr 0x%p\n", ring->idx,
		 ring->fence_drv.gpu_addr, ring->fence_drv.cpu_addr);
	return 0;
}

/**
 * amdgpu_fence_driver_init_ring - init the fence driver
 * for the requested ring.
 *
 * @ring: ring to init the fence driver on
 *
 * Init the fence driver for the requested ring (all asics).
 * Helper function for amdgpu_fence_driver_init().
 */
int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring)
{
	int i, r;

	ring->fence_drv.cpu_addr = NULL;
	ring->fence_drv.gpu_addr = 0;
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		ring->fence_drv.sync_seq[i] = 0;

	atomic64_set(&ring->fence_drv.last_seq, 0);
	ring->fence_drv.initialized = false;

	INIT_DELAYED_WORK(&ring->fence_drv.lockup_work,
			amdgpu_fence_check_lockup);
	ring->fence_drv.ring = ring;

	init_waitqueue_head(&ring->fence_drv.fence_queue);

	if (amdgpu_enable_scheduler) {
		r = amd_sched_init(&ring->sched, &amdgpu_sched_ops,
				   amdgpu_sched_hw_submission, ring->name);
		if (r) {
			DRM_ERROR("Failed to create scheduler on ring %s.\n",
				  ring->name);
			return r;
		}
	}

	return 0;
}

/**
 * amdgpu_fence_driver_init - init the fence driver
 * for all possible rings.
 *
 * @adev: amdgpu device pointer
 *
 * Init the fence driver for all possible rings (all asics).
 * Not all asics have all rings, so each asic will only
 * start the fence driver on the rings it has using
 * amdgpu_fence_driver_start_ring().
 * Returns 0 for success.
 */
int amdgpu_fence_driver_init(struct amdgpu_device *adev)
{
	if (amdgpu_debugfs_fence_init(adev))
		dev_err(adev->dev, "fence debugfs file creation failed\n");

	return 0;
}

/**
 * amdgpu_fence_driver_fini - tear down the fence driver
 * for all possible rings.
 *
 * @adev: amdgpu device pointer
 *
 * Tear down the fence driver for all possible rings (all asics).
 */
void amdgpu_fence_driver_fini(struct amdgpu_device *adev)
{
	int i, r;

	mutex_lock(&adev->ring_lock);
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;
		r = amdgpu_fence_wait_empty(ring);
		if (r) {
			/* no need to trigger GPU reset as we are unloading */
			amdgpu_fence_driver_force_completion(adev);
		}
		wake_up_all(&ring->fence_drv.fence_queue);
		amdgpu_irq_put(adev, ring->fence_drv.irq_src,
			       ring->fence_drv.irq_type);
		amd_sched_fini(&ring->sched);
		ring->fence_drv.initialized = false;
	}
	mutex_unlock(&adev->ring_lock);
}

/**
 * amdgpu_fence_driver_suspend - suspend the fence driver
 * for all possible rings.
 *
 * @adev: amdgpu device pointer
 *
 * Suspend the fence driver for all possible rings (all asics).
 */
void amdgpu_fence_driver_suspend(struct amdgpu_device *adev)
{
	int i, r;

	mutex_lock(&adev->ring_lock);
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		/* wait for gpu to finish processing current batch */
		r = amdgpu_fence_wait_empty(ring);
		if (r) {
			/* delay GPU reset to resume */
			amdgpu_fence_driver_force_completion(adev);
		}

		/* disable the interrupt */
		amdgpu_irq_put(adev, ring->fence_drv.irq_src,
			       ring->fence_drv.irq_type);
	}
	mutex_unlock(&adev->ring_lock);
}

/**
 * amdgpu_fence_driver_resume - resume the fence driver
 * for all possible rings.
 *
 * @adev: amdgpu device pointer
 *
 * Resume the fence driver for all possible rings (all asics).
 * Not all asics have all rings, so each asic will only
 * start the fence driver on the rings it has using
 * amdgpu_fence_driver_start_ring().
 * Returns 0 for success.
 */
void amdgpu_fence_driver_resume(struct amdgpu_device *adev)
{
	int i;

	mutex_lock(&adev->ring_lock);
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		/* enable the interrupt */
		amdgpu_irq_get(adev, ring->fence_drv.irq_src,
			       ring->fence_drv.irq_type);
	}
	mutex_unlock(&adev->ring_lock);
}

/**
 * amdgpu_fence_driver_force_completion - force all fence waiter to complete
 *
 * @adev: amdgpu device pointer
 *
 * In case of GPU reset failure make sure no process keep waiting on fence
 * that will never complete.
 */
void amdgpu_fence_driver_force_completion(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		amdgpu_fence_write(ring, ring->fence_drv.sync_seq[i]);
	}
}


/*
 * Fence debugfs
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_fence_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int i, j;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		amdgpu_fence_process(ring);

		seq_printf(m, "--- ring %d (%s) ---\n", i, ring->name);
		seq_printf(m, "Last signaled fence 0x%016llx\n",
			   (unsigned long long)atomic64_read(&ring->fence_drv.last_seq));
		seq_printf(m, "Last emitted        0x%016llx\n",
			   ring->fence_drv.sync_seq[i]);

		for (j = 0; j < AMDGPU_MAX_RINGS; ++j) {
			struct amdgpu_ring *other = adev->rings[j];
			if (i != j && other && other->fence_drv.initialized &&
			    ring->fence_drv.sync_seq[j])
				seq_printf(m, "Last sync to ring %d 0x%016llx\n",
					   j, ring->fence_drv.sync_seq[j]);
		}
	}
	return 0;
}

static struct drm_info_list amdgpu_debugfs_fence_list[] = {
	{"amdgpu_fence_info", &amdgpu_debugfs_fence_info, 0, NULL},
};
#endif

int amdgpu_debugfs_fence_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_debugfs_fence_list, 1);
#else
	return 0;
#endif
}

static const char *amdgpu_fence_get_driver_name(struct fence *fence)
{
	return "amdgpu";
}

static const char *amdgpu_fence_get_timeline_name(struct fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	return (const char *)fence->ring->name;
}

static inline bool amdgpu_test_signaled(struct amdgpu_fence *fence)
{
	return test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->base.flags);
}

static bool amdgpu_test_signaled_any(struct fence **fences, uint32_t count)
{
	int idx;
	struct fence *fence;

	for (idx = 0; idx < count; ++idx) {
		fence = fences[idx];
		if (fence) {
			if (test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->flags))
				return true;
		}
	}
	return false;
}

struct amdgpu_wait_cb {
	struct fence_cb base;
	struct task_struct *task;
};

static void amdgpu_fence_wait_cb(struct fence *fence, struct fence_cb *cb)
{
	struct amdgpu_wait_cb *wait =
		container_of(cb, struct amdgpu_wait_cb, base);
	wake_up_process(wait->task);
}

static signed long amdgpu_fence_default_wait(struct fence *f, bool intr,
					     signed long t)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	struct amdgpu_device *adev = fence->ring->adev;

	return amdgpu_fence_wait_any(adev, &f, 1, intr, t);
}

/**
 * Wait the fence array with timeout
 *
 * @adev:     amdgpu device
 * @array:    the fence array with amdgpu fence pointer
 * @count:    the number of the fence array
 * @intr:     when sleep, set the current task interruptable or not
 * @t:        timeout to wait
 *
 * It will return when any fence is signaled or timeout.
 */
signed long amdgpu_fence_wait_any(struct amdgpu_device *adev,
				  struct fence **array, uint32_t count,
				  bool intr, signed long t)
{
	struct amdgpu_wait_cb *cb;
	struct fence *fence;
	unsigned idx;

	BUG_ON(!array);

	cb = kcalloc(count, sizeof(struct amdgpu_wait_cb), GFP_KERNEL);
	if (cb == NULL) {
		t = -ENOMEM;
		goto err_free_cb;
	}

	for (idx = 0; idx < count; ++idx) {
		fence = array[idx];
		if (fence) {
			cb[idx].task = current;
			if (fence_add_callback(fence,
					&cb[idx].base, amdgpu_fence_wait_cb)) {
				/* The fence is already signaled */
				goto fence_rm_cb;
			}
		}
	}

	while (t > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		/*
		 * amdgpu_test_signaled_any must be called after
		 * set_current_state to prevent a race with wake_up_process
		 */
		if (amdgpu_test_signaled_any(array, count))
			break;

		if (adev->needs_reset) {
			t = -EDEADLK;
			break;
		}

		t = schedule_timeout(t);

		if (t > 0 && intr && signal_pending(current))
			t = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

fence_rm_cb:
	for (idx = 0; idx < count; ++idx) {
		fence = array[idx];
		if (fence && cb[idx].base.func)
			fence_remove_callback(fence, &cb[idx].base);
	}

err_free_cb:
	kfree(cb);

	return t;
}

const struct fence_ops amdgpu_fence_ops = {
	.get_driver_name = amdgpu_fence_get_driver_name,
	.get_timeline_name = amdgpu_fence_get_timeline_name,
	.enable_signaling = amdgpu_fence_enable_signaling,
	.signaled = amdgpu_fence_is_signaled,
	.wait = amdgpu_fence_default_wait,
	.release = NULL,
};
