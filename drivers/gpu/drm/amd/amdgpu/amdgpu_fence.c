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

struct amdgpu_fence {
	struct fence base;

	/* RB, DMA, etc. */
	struct amdgpu_ring		*ring;
};

static struct kmem_cache *amdgpu_fence_slab;
static atomic_t amdgpu_fence_slab_ref = ATOMIC_INIT(0);

/*
 * Cast helper
 */
static const struct fence_ops amdgpu_fence_ops;
static inline struct amdgpu_fence *to_amdgpu_fence(struct fence *f)
{
	struct amdgpu_fence *__f = container_of(f, struct amdgpu_fence, base);

	if (__f->base.ops == &amdgpu_fence_ops)
		return __f;

	return NULL;
}

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
		seq = atomic_read(&drv->last_seq);

	return seq;
}

/**
 * amdgpu_fence_emit - emit a fence on the requested ring
 *
 * @ring: ring the fence is associated with
 * @f: resulting fence object
 *
 * Emits a fence command on the requested ring (all asics).
 * Returns 0 on success, -ENOMEM on failure.
 */
int amdgpu_fence_emit(struct amdgpu_ring *ring, struct fence **f)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_fence *fence;
	struct fence *old, **ptr;
	uint32_t seq;

	fence = kmem_cache_alloc(amdgpu_fence_slab, GFP_KERNEL);
	if (fence == NULL)
		return -ENOMEM;

	seq = ++ring->fence_drv.sync_seq;
	fence->ring = ring;
	fence_init(&fence->base, &amdgpu_fence_ops,
		   &ring->fence_drv.lock,
		   adev->fence_context + ring->idx,
		   seq);
	amdgpu_ring_emit_fence(ring, ring->fence_drv.gpu_addr,
			       seq, AMDGPU_FENCE_FLAG_INT);

	ptr = &ring->fence_drv.fences[seq & ring->fence_drv.num_fences_mask];
	/* This function can't be called concurrently anyway, otherwise
	 * emitting the fence would mess up the hardware ring buffer.
	 */
	old = rcu_dereference_protected(*ptr, 1);
	if (old && !fence_is_signaled(old)) {
		DRM_INFO("rcu slot is busy\n");
		fence_wait(old, false);
	}

	rcu_assign_pointer(*ptr, fence_get(&fence->base));

	*f = &fence->base;

	return 0;
}

/**
 * amdgpu_fence_schedule_fallback - schedule fallback check
 *
 * @ring: pointer to struct amdgpu_ring
 *
 * Start a timer as fallback to our interrupts.
 */
static void amdgpu_fence_schedule_fallback(struct amdgpu_ring *ring)
{
	mod_timer(&ring->fence_drv.fallback_timer,
		  jiffies + AMDGPU_FENCE_JIFFIES_TIMEOUT);
}

/**
 * amdgpu_fence_process - check for fence activity
 *
 * @ring: pointer to struct amdgpu_ring
 *
 * Checks the current fence value and calculates the last
 * signalled fence value. Wakes the fence queue if the
 * sequence number has increased.
 */
void amdgpu_fence_process(struct amdgpu_ring *ring)
{
	struct amdgpu_fence_driver *drv = &ring->fence_drv;
	uint32_t seq, last_seq;
	int r;

	do {
		last_seq = atomic_read(&ring->fence_drv.last_seq);
		seq = amdgpu_fence_read(ring);

	} while (atomic_cmpxchg(&drv->last_seq, last_seq, seq) != last_seq);

	if (seq != ring->fence_drv.sync_seq)
		amdgpu_fence_schedule_fallback(ring);

	while (last_seq != seq) {
		struct fence *fence, **ptr;

		ptr = &drv->fences[++last_seq & drv->num_fences_mask];

		/* There is always exactly one thread signaling this fence slot */
		fence = rcu_dereference_protected(*ptr, 1);
		rcu_assign_pointer(*ptr, NULL);

		BUG_ON(!fence);

		r = fence_signal(fence);
		if (!r)
			FENCE_TRACE(fence, "signaled from irq context\n");
		else
			BUG();

		fence_put(fence);
	}
}

/**
 * amdgpu_fence_fallback - fallback for hardware interrupts
 *
 * @work: delayed work item
 *
 * Checks for fence activity.
 */
static void amdgpu_fence_fallback(unsigned long arg)
{
	struct amdgpu_ring *ring = (void *)arg;

	amdgpu_fence_process(ring);
}

/**
 * amdgpu_fence_wait_empty - wait for all fences to signal
 *
 * @adev: amdgpu device pointer
 * @ring: ring index the fence is associated with
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns 0 if the fences have passed, error for all other cases.
 */
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring)
{
	uint64_t seq = ACCESS_ONCE(ring->fence_drv.sync_seq);
	struct fence *fence, **ptr;
	int r;

	if (!seq)
		return 0;

	ptr = &ring->fence_drv.fences[seq & ring->fence_drv.num_fences_mask];
	rcu_read_lock();
	fence = rcu_dereference(*ptr);
	if (!fence || !fence_get_rcu(fence)) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	r = fence_wait(fence, false);
	fence_put(fence);
	return r;
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
	emitted = 0x100000000ull;
	emitted -= atomic_read(&ring->fence_drv.last_seq);
	emitted += ACCESS_ONCE(ring->fence_drv.sync_seq);
	return lower_32_bits(emitted);
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
	amdgpu_fence_write(ring, atomic_read(&ring->fence_drv.last_seq));
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
 * @num_hw_submission: number of entries on the hardware queue
 *
 * Init the fence driver for the requested ring (all asics).
 * Helper function for amdgpu_fence_driver_init().
 */
int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring,
				  unsigned num_hw_submission)
{
	long timeout;
	int r;

	/* Check that num_hw_submission is a power of two */
	if ((num_hw_submission & (num_hw_submission - 1)) != 0)
		return -EINVAL;

	ring->fence_drv.cpu_addr = NULL;
	ring->fence_drv.gpu_addr = 0;
	ring->fence_drv.sync_seq = 0;
	atomic_set(&ring->fence_drv.last_seq, 0);
	ring->fence_drv.initialized = false;

	setup_timer(&ring->fence_drv.fallback_timer, amdgpu_fence_fallback,
		    (unsigned long)ring);

	ring->fence_drv.num_fences_mask = num_hw_submission * 2 - 1;
	spin_lock_init(&ring->fence_drv.lock);
	ring->fence_drv.fences = kcalloc(num_hw_submission * 2, sizeof(void *),
					 GFP_KERNEL);
	if (!ring->fence_drv.fences)
		return -ENOMEM;

	timeout = msecs_to_jiffies(amdgpu_lockup_timeout);
	if (timeout == 0) {
		/*
		 * FIXME:
		 * Delayed workqueue cannot use it directly,
		 * so the scheduler will not use delayed workqueue if
		 * MAX_SCHEDULE_TIMEOUT is set.
		 * Currently keep it simple and silly.
		 */
		timeout = MAX_SCHEDULE_TIMEOUT;
	}
	r = amd_sched_init(&ring->sched, &amdgpu_sched_ops,
			   num_hw_submission,
			   timeout, ring->name);
	if (r) {
		DRM_ERROR("Failed to create scheduler on ring %s.\n",
			  ring->name);
		return r;
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
	if (atomic_inc_return(&amdgpu_fence_slab_ref) == 1) {
		amdgpu_fence_slab = kmem_cache_create(
			"amdgpu_fence", sizeof(struct amdgpu_fence), 0,
			SLAB_HWCACHE_ALIGN, NULL);
		if (!amdgpu_fence_slab)
			return -ENOMEM;
	}
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
	unsigned i, j;
	int r;

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->fence_drv.initialized)
			continue;
		r = amdgpu_fence_wait_empty(ring);
		if (r) {
			/* no need to trigger GPU reset as we are unloading */
			amdgpu_fence_driver_force_completion(adev);
		}
		amdgpu_irq_put(adev, ring->fence_drv.irq_src,
			       ring->fence_drv.irq_type);
		amd_sched_fini(&ring->sched);
		del_timer_sync(&ring->fence_drv.fallback_timer);
		for (j = 0; j <= ring->fence_drv.num_fences_mask; ++j)
			fence_put(ring->fence_drv.fences[i]);
		kfree(ring->fence_drv.fences);
		ring->fence_drv.initialized = false;
	}

	if (atomic_dec_and_test(&amdgpu_fence_slab_ref))
		kmem_cache_destroy(amdgpu_fence_slab);
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

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		/* enable the interrupt */
		amdgpu_irq_get(adev, ring->fence_drv.irq_src,
			       ring->fence_drv.irq_type);
	}
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

		amdgpu_fence_write(ring, ring->fence_drv.sync_seq);
	}
}

/*
 * Common fence implementation
 */

static const char *amdgpu_fence_get_driver_name(struct fence *fence)
{
	return "amdgpu";
}

static const char *amdgpu_fence_get_timeline_name(struct fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	return (const char *)fence->ring->name;
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

	if (!timer_pending(&ring->fence_drv.fallback_timer))
		amdgpu_fence_schedule_fallback(ring);

	FENCE_TRACE(&fence->base, "armed on ring %i!\n", ring->idx);

	return true;
}

/**
 * amdgpu_fence_free - free up the fence memory
 *
 * @rcu: RCU callback head
 *
 * Free up the fence memory after the RCU grace period.
 */
static void amdgpu_fence_free(struct rcu_head *rcu)
{
	struct fence *f = container_of(rcu, struct fence, rcu);
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	kmem_cache_free(amdgpu_fence_slab, fence);
}

/**
 * amdgpu_fence_release - callback that fence can be freed
 *
 * @fence: fence
 *
 * This function is called when the reference count becomes zero.
 * It just RCU schedules freeing up the fence.
 */
static void amdgpu_fence_release(struct fence *f)
{
	call_rcu(&f->rcu, amdgpu_fence_free);
}

static const struct fence_ops amdgpu_fence_ops = {
	.get_driver_name = amdgpu_fence_get_driver_name,
	.get_timeline_name = amdgpu_fence_get_timeline_name,
	.enable_signaling = amdgpu_fence_enable_signaling,
	.wait = fence_default_wait,
	.release = amdgpu_fence_release,
};

/*
 * Fence debugfs
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_fence_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int i;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		amdgpu_fence_process(ring);

		seq_printf(m, "--- ring %d (%s) ---\n", i, ring->name);
		seq_printf(m, "Last signaled fence 0x%08x\n",
			   atomic_read(&ring->fence_drv.last_seq));
		seq_printf(m, "Last emitted        0x%08x\n",
			   ring->fence_drv.sync_seq);
	}
	return 0;
}

/**
 * amdgpu_debugfs_gpu_reset - manually trigger a gpu reset
 *
 * Manually trigger a gpu reset at the next fence wait.
 */
static int amdgpu_debugfs_gpu_reset(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;

	seq_printf(m, "gpu reset\n");
	amdgpu_gpu_reset(adev);

	return 0;
}

static const struct drm_info_list amdgpu_debugfs_fence_list[] = {
	{"amdgpu_fence_info", &amdgpu_debugfs_fence_info, 0, NULL},
	{"amdgpu_gpu_reset", &amdgpu_debugfs_gpu_reset, 0, NULL}
};
#endif

int amdgpu_debugfs_fence_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_debugfs_fence_list, 2);
#else
	return 0;
#endif
}

