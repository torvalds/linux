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
#include <linux/pm_runtime.h>

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
	struct dma_fence base;

	/* RB, DMA, etc. */
	struct amdgpu_ring		*ring;
};

static struct kmem_cache *amdgpu_fence_slab;

int amdgpu_fence_slab_init(void)
{
	amdgpu_fence_slab = kmem_cache_create(
		"amdgpu_fence", sizeof(struct amdgpu_fence), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!amdgpu_fence_slab)
		return -ENOMEM;
	return 0;
}

void amdgpu_fence_slab_fini(void)
{
	rcu_barrier();
	kmem_cache_destroy(amdgpu_fence_slab);
}
/*
 * Cast helper
 */
static const struct dma_fence_ops amdgpu_fence_ops;
static inline struct amdgpu_fence *to_amdgpu_fence(struct dma_fence *f)
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
 * @flags: flags to pass into the subordinate .emit_fence() call
 *
 * Emits a fence command on the requested ring (all asics).
 * Returns 0 on success, -ENOMEM on failure.
 */
int amdgpu_fence_emit(struct amdgpu_ring *ring, struct dma_fence **f,
		      unsigned flags)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_fence *fence;
	struct dma_fence __rcu **ptr;
	uint32_t seq;
	int r;

	fence = kmem_cache_alloc(amdgpu_fence_slab, GFP_KERNEL);
	if (fence == NULL)
		return -ENOMEM;

	seq = ++ring->fence_drv.sync_seq;
	fence->ring = ring;
	dma_fence_init(&fence->base, &amdgpu_fence_ops,
		       &ring->fence_drv.lock,
		       adev->fence_context + ring->idx,
		       seq);
	amdgpu_ring_emit_fence(ring, ring->fence_drv.gpu_addr,
			       seq, flags | AMDGPU_FENCE_FLAG_INT);
	pm_runtime_get_noresume(adev_to_drm(adev)->dev);
	ptr = &ring->fence_drv.fences[seq & ring->fence_drv.num_fences_mask];
	if (unlikely(rcu_dereference_protected(*ptr, 1))) {
		struct dma_fence *old;

		rcu_read_lock();
		old = dma_fence_get_rcu_safe(ptr);
		rcu_read_unlock();

		if (old) {
			r = dma_fence_wait(old, false);
			dma_fence_put(old);
			if (r)
				return r;
		}
	}

	/* This function can't be called concurrently anyway, otherwise
	 * emitting the fence would mess up the hardware ring buffer.
	 */
	rcu_assign_pointer(*ptr, dma_fence_get(&fence->base));

	*f = &fence->base;

	return 0;
}

/**
 * amdgpu_fence_emit_polling - emit a fence on the requeste ring
 *
 * @ring: ring the fence is associated with
 * @s: resulting sequence number
 * @timeout: the timeout for waiting in usecs
 *
 * Emits a fence command on the requested ring (all asics).
 * Used For polling fence.
 * Returns 0 on success, -ENOMEM on failure.
 */
int amdgpu_fence_emit_polling(struct amdgpu_ring *ring, uint32_t *s,
			      uint32_t timeout)
{
	uint32_t seq;
	signed long r;

	if (!s)
		return -EINVAL;

	seq = ++ring->fence_drv.sync_seq;
	r = amdgpu_fence_wait_polling(ring,
				      seq - ring->fence_drv.num_fences_mask,
				      timeout);
	if (r < 1)
		return -ETIMEDOUT;

	amdgpu_ring_emit_fence(ring, ring->fence_drv.gpu_addr,
			       seq, 0);

	*s = seq;

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
 *
 * Returns true if fence was processed
 */
bool amdgpu_fence_process(struct amdgpu_ring *ring)
{
	struct amdgpu_fence_driver *drv = &ring->fence_drv;
	struct amdgpu_device *adev = ring->adev;
	uint32_t seq, last_seq;
	int r;

	do {
		last_seq = atomic_read(&ring->fence_drv.last_seq);
		seq = amdgpu_fence_read(ring);

	} while (atomic_cmpxchg(&drv->last_seq, last_seq, seq) != last_seq);

	if (del_timer(&ring->fence_drv.fallback_timer) &&
	    seq != ring->fence_drv.sync_seq)
		amdgpu_fence_schedule_fallback(ring);

	if (unlikely(seq == last_seq))
		return false;

	last_seq &= drv->num_fences_mask;
	seq &= drv->num_fences_mask;

	do {
		struct dma_fence *fence, **ptr;

		++last_seq;
		last_seq &= drv->num_fences_mask;
		ptr = &drv->fences[last_seq];

		/* There is always exactly one thread signaling this fence slot */
		fence = rcu_dereference_protected(*ptr, 1);
		RCU_INIT_POINTER(*ptr, NULL);

		if (!fence)
			continue;

		r = dma_fence_signal(fence);
		if (!r)
			DMA_FENCE_TRACE(fence, "signaled from irq context\n");
		else
			BUG();

		dma_fence_put(fence);
		pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
	} while (last_seq != seq);

	return true;
}

/**
 * amdgpu_fence_fallback - fallback for hardware interrupts
 *
 * @t: timer context used to obtain the pointer to ring structure
 *
 * Checks for fence activity.
 */
static void amdgpu_fence_fallback(struct timer_list *t)
{
	struct amdgpu_ring *ring = from_timer(ring, t,
					      fence_drv.fallback_timer);

	if (amdgpu_fence_process(ring))
		DRM_WARN("Fence fallback timer expired on ring %s\n", ring->name);
}

/**
 * amdgpu_fence_wait_empty - wait for all fences to signal
 *
 * @ring: ring index the fence is associated with
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns 0 if the fences have passed, error for all other cases.
 */
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring)
{
	uint64_t seq = READ_ONCE(ring->fence_drv.sync_seq);
	struct dma_fence *fence, **ptr;
	int r;

	if (!seq)
		return 0;

	ptr = &ring->fence_drv.fences[seq & ring->fence_drv.num_fences_mask];
	rcu_read_lock();
	fence = rcu_dereference(*ptr);
	if (!fence || !dma_fence_get_rcu(fence)) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	r = dma_fence_wait(fence, false);
	dma_fence_put(fence);
	return r;
}

/**
 * amdgpu_fence_wait_polling - busy wait for givn sequence number
 *
 * @ring: ring index the fence is associated with
 * @wait_seq: sequence number to wait
 * @timeout: the timeout for waiting in usecs
 *
 * Wait for all fences on the requested ring to signal (all asics).
 * Returns left time if no timeout, 0 or minus if timeout.
 */
signed long amdgpu_fence_wait_polling(struct amdgpu_ring *ring,
				      uint32_t wait_seq,
				      signed long timeout)
{
	uint32_t seq;

	do {
		seq = amdgpu_fence_read(ring);
		udelay(5);
		timeout -= 5;
	} while ((int32_t)(wait_seq - seq) > 0 && timeout > 0);

	return timeout > 0 ? timeout : 0;
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
	emitted += READ_ONCE(ring->fence_drv.sync_seq);
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

	if (ring->funcs->type != AMDGPU_RING_TYPE_UVD) {
		ring->fence_drv.cpu_addr = &adev->wb.wb[ring->fence_offs];
		ring->fence_drv.gpu_addr = adev->wb.gpu_addr + (ring->fence_offs * 4);
	} else {
		/* put fence directly behind firmware */
		index = ALIGN(adev->uvd.fw->size, 8);
		ring->fence_drv.cpu_addr = adev->uvd.inst[ring->me].cpu_addr + index;
		ring->fence_drv.gpu_addr = adev->uvd.inst[ring->me].gpu_addr + index;
	}
	amdgpu_fence_write(ring, atomic_read(&ring->fence_drv.last_seq));

	if (irq_src)
		amdgpu_irq_get(adev, irq_src, irq_type);

	ring->fence_drv.irq_src = irq_src;
	ring->fence_drv.irq_type = irq_type;
	ring->fence_drv.initialized = true;

	DRM_DEV_DEBUG(adev->dev, "fence driver on ring %s use gpu addr 0x%016llx\n",
		      ring->name, ring->fence_drv.gpu_addr);
	return 0;
}

/**
 * amdgpu_fence_driver_init_ring - init the fence driver
 * for the requested ring.
 *
 * @ring: ring to init the fence driver on
 * @num_hw_submission: number of entries on the hardware queue
 * @sched_score: optional score atomic shared with other schedulers
 *
 * Init the fence driver for the requested ring (all asics).
 * Helper function for amdgpu_fence_driver_init().
 */
int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring,
				  unsigned num_hw_submission,
				  atomic_t *sched_score)
{
	struct amdgpu_device *adev = ring->adev;
	long timeout;
	int r;

	if (!adev)
		return -EINVAL;

	if (!is_power_of_2(num_hw_submission))
		return -EINVAL;

	ring->fence_drv.cpu_addr = NULL;
	ring->fence_drv.gpu_addr = 0;
	ring->fence_drv.sync_seq = 0;
	atomic_set(&ring->fence_drv.last_seq, 0);
	ring->fence_drv.initialized = false;

	timer_setup(&ring->fence_drv.fallback_timer, amdgpu_fence_fallback, 0);

	ring->fence_drv.num_fences_mask = num_hw_submission * 2 - 1;
	spin_lock_init(&ring->fence_drv.lock);
	ring->fence_drv.fences = kcalloc(num_hw_submission * 2, sizeof(void *),
					 GFP_KERNEL);
	if (!ring->fence_drv.fences)
		return -ENOMEM;

	/* No need to setup the GPU scheduler for rings that don't need it */
	if (ring->no_scheduler)
		return 0;

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		timeout = adev->gfx_timeout;
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		timeout = adev->compute_timeout;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		timeout = adev->sdma_timeout;
		break;
	default:
		timeout = adev->video_timeout;
		break;
	}

	r = drm_sched_init(&ring->sched, &amdgpu_sched_ops,
			   num_hw_submission, amdgpu_job_hang_limit,
			   timeout, sched_score, ring->name);
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
		if (!ring->no_scheduler)
			drm_sched_fini(&ring->sched);
		r = amdgpu_fence_wait_empty(ring);
		if (r) {
			/* no need to trigger GPU reset as we are unloading */
			amdgpu_fence_driver_force_completion(ring);
		}
		if (ring->fence_drv.irq_src)
			amdgpu_irq_put(adev, ring->fence_drv.irq_src,
				       ring->fence_drv.irq_type);

		del_timer_sync(&ring->fence_drv.fallback_timer);
		for (j = 0; j <= ring->fence_drv.num_fences_mask; ++j)
			dma_fence_put(ring->fence_drv.fences[j]);
		kfree(ring->fence_drv.fences);
		ring->fence_drv.fences = NULL;
		ring->fence_drv.initialized = false;
	}
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
			amdgpu_fence_driver_force_completion(ring);
		}

		/* disable the interrupt */
		if (ring->fence_drv.irq_src)
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
		if (ring->fence_drv.irq_src)
			amdgpu_irq_get(adev, ring->fence_drv.irq_src,
				       ring->fence_drv.irq_type);
	}
}

/**
 * amdgpu_fence_driver_force_completion - force signal latest fence of ring
 *
 * @ring: fence of the ring to signal
 *
 */
void amdgpu_fence_driver_force_completion(struct amdgpu_ring *ring)
{
	amdgpu_fence_write(ring, ring->fence_drv.sync_seq);
	amdgpu_fence_process(ring);
}

/*
 * Common fence implementation
 */

static const char *amdgpu_fence_get_driver_name(struct dma_fence *fence)
{
	return "amdgpu";
}

static const char *amdgpu_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	return (const char *)fence->ring->name;
}

/**
 * amdgpu_fence_enable_signaling - enable signalling on fence
 * @f: fence
 *
 * This function is called with fence_queue lock held, and adds a callback
 * to fence_queue that checks if this fence is signaled, and if so it
 * signals the fence and removes itself.
 */
static bool amdgpu_fence_enable_signaling(struct dma_fence *f)
{
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	struct amdgpu_ring *ring = fence->ring;

	if (!timer_pending(&ring->fence_drv.fallback_timer))
		amdgpu_fence_schedule_fallback(ring);

	DMA_FENCE_TRACE(&fence->base, "armed on ring %i!\n", ring->idx);

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
	struct dma_fence *f = container_of(rcu, struct dma_fence, rcu);
	struct amdgpu_fence *fence = to_amdgpu_fence(f);
	kmem_cache_free(amdgpu_fence_slab, fence);
}

/**
 * amdgpu_fence_release - callback that fence can be freed
 *
 * @f: fence
 *
 * This function is called when the reference count becomes zero.
 * It just RCU schedules freeing up the fence.
 */
static void amdgpu_fence_release(struct dma_fence *f)
{
	call_rcu(&f->rcu, amdgpu_fence_free);
}

static const struct dma_fence_ops amdgpu_fence_ops = {
	.get_driver_name = amdgpu_fence_get_driver_name,
	.get_timeline_name = amdgpu_fence_get_timeline_name,
	.enable_signaling = amdgpu_fence_enable_signaling,
	.release = amdgpu_fence_release,
};

/*
 * Fence debugfs
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_fence_info_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	int i;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		if (!ring || !ring->fence_drv.initialized)
			continue;

		amdgpu_fence_process(ring);

		seq_printf(m, "--- ring %d (%s) ---\n", i, ring->name);
		seq_printf(m, "Last signaled fence          0x%08x\n",
			   atomic_read(&ring->fence_drv.last_seq));
		seq_printf(m, "Last emitted                 0x%08x\n",
			   ring->fence_drv.sync_seq);

		if (ring->funcs->type == AMDGPU_RING_TYPE_GFX ||
		    ring->funcs->type == AMDGPU_RING_TYPE_SDMA) {
			seq_printf(m, "Last signaled trailing fence 0x%08x\n",
				   le32_to_cpu(*ring->trail_fence_cpu_addr));
			seq_printf(m, "Last emitted                 0x%08x\n",
				   ring->trail_seq);
		}

		if (ring->funcs->type != AMDGPU_RING_TYPE_GFX)
			continue;

		/* set in CP_VMID_PREEMPT and preemption occurred */
		seq_printf(m, "Last preempted               0x%08x\n",
			   le32_to_cpu(*(ring->fence_drv.cpu_addr + 2)));
		/* set in CP_VMID_RESET and reset occurred */
		seq_printf(m, "Last reset                   0x%08x\n",
			   le32_to_cpu(*(ring->fence_drv.cpu_addr + 4)));
		/* Both preemption and reset occurred */
		seq_printf(m, "Last both                    0x%08x\n",
			   le32_to_cpu(*(ring->fence_drv.cpu_addr + 6)));
	}
	return 0;
}

/*
 * amdgpu_debugfs_gpu_recover - manually trigger a gpu reset & recover
 *
 * Manually trigger a gpu reset at the next fence wait.
 */
static int gpu_recover_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	struct drm_device *dev = adev_to_drm(adev);
	int r;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return 0;
	}

	*val = amdgpu_device_gpu_recover(adev, NULL);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_fence_info);
DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_debugfs_gpu_recover_fops, gpu_recover_get, NULL,
			 "%lld\n");

#endif

void amdgpu_debugfs_fence_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	debugfs_create_file("amdgpu_fence_info", 0444, root, adev,
			    &amdgpu_debugfs_fence_info_fops);

	if (!amdgpu_sriov_vf(adev))
		debugfs_create_file("amdgpu_gpu_recover", 0444, root, adev,
				    &amdgpu_debugfs_gpu_recover_fops);
#endif
}

