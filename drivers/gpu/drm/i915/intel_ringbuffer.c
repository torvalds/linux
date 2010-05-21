/*
 * Copyright © 2008-2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Zou Nan hai <nanhai.zou@intel.com>
 *    Xiang Hai hao<haihao.xiang@intel.com>
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

void
i915_gem_flush(struct drm_device *dev,
	       uint32_t invalidate_domains,
	       uint32_t flush_domains)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t cmd;
	RING_LOCALS;

#if WATCH_EXEC
	DRM_INFO("%s: invalidate %08x flush %08x\n", __func__,
		  invalidate_domains, flush_domains);
#endif
	trace_i915_gem_request_flush(dev, dev_priv->mm.next_gem_seqno,
				     invalidate_domains, flush_domains);

	if (flush_domains & I915_GEM_DOMAIN_CPU)
		drm_agp_chipset_flush(dev);

	if ((invalidate_domains | flush_domains) & I915_GEM_GPU_DOMAINS) {
		/*
		 * read/write caches:
		 *
		 * I915_GEM_DOMAIN_RENDER is always invalidated, but is
		 * only flushed if MI_NO_WRITE_FLUSH is unset.  On 965, it is
		 * also flushed at 2d versus 3d pipeline switches.
		 *
		 * read-only caches:
		 *
		 * I915_GEM_DOMAIN_SAMPLER is flushed on pre-965 if
		 * MI_READ_FLUSH is set, and is always flushed on 965.
		 *
		 * I915_GEM_DOMAIN_COMMAND may not exist?
		 *
		 * I915_GEM_DOMAIN_INSTRUCTION, which exists on 965, is
		 * invalidated when MI_EXE_FLUSH is set.
		 *
		 * I915_GEM_DOMAIN_VERTEX, which exists on 965, is
		 * invalidated with every MI_FLUSH.
		 *
		 * TLBs:
		 *
		 * On 965, TLBs associated with I915_GEM_DOMAIN_COMMAND
		 * and I915_GEM_DOMAIN_CPU in are invalidated at PTE write and
		 * I915_GEM_DOMAIN_RENDER and I915_GEM_DOMAIN_SAMPLER
		 * are flushed at any MI_FLUSH.
		 */

		cmd = MI_FLUSH | MI_NO_WRITE_FLUSH;
		if ((invalidate_domains|flush_domains) &
		    I915_GEM_DOMAIN_RENDER)
			cmd &= ~MI_NO_WRITE_FLUSH;
		if (!IS_I965G(dev)) {
			/*
			 * On the 965, the sampler cache always gets flushed
			 * and this bit is reserved.
			 */
			if (invalidate_domains & I915_GEM_DOMAIN_SAMPLER)
				cmd |= MI_READ_FLUSH;
		}
		if (invalidate_domains & I915_GEM_DOMAIN_INSTRUCTION)
			cmd |= MI_EXE_FLUSH;

#if WATCH_EXEC
		DRM_INFO("%s: queue flush %08x to ring\n", __func__, cmd);
#endif
		BEGIN_LP_RING(2);
		OUT_RING(cmd);
		OUT_RING(MI_NOOP);
		ADVANCE_LP_RING();
	}

}
#define PIPE_CONTROL_FLUSH(addr)					\
	OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |		\
		 PIPE_CONTROL_DEPTH_STALL);				\
	OUT_RING(addr | PIPE_CONTROL_GLOBAL_GTT);			\
	OUT_RING(0);							\
	OUT_RING(0);							\

/**
 * Creates a new sequence number, emitting a write of it to the status page
 * plus an interrupt, which will trigger i915_user_interrupt_handler.
 *
 * Must be called with struct_lock held.
 *
 * Returned sequence numbers are nonzero on success.
 */
uint32_t
i915_ring_add_request(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t seqno;
	RING_LOCALS;

	/* Grab the seqno we're going to make this request be, and bump the
	 * next (skipping 0 so it can be the reserved no-seqno value).
	 */
	seqno = dev_priv->mm.next_gem_seqno;
	dev_priv->mm.next_gem_seqno++;
	if (dev_priv->mm.next_gem_seqno == 0)
		dev_priv->mm.next_gem_seqno++;

	if (HAS_PIPE_CONTROL(dev)) {
		u32 scratch_addr = dev_priv->seqno_gfx_addr + 128;

		/*
		 * Workaround qword write incoherence by flushing the
		 * PIPE_NOTIFY buffers out to memory before requesting
		 * an interrupt.
		 */
		BEGIN_LP_RING(32);
		OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128; /* write to separate cachelines */
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		scratch_addr += 128;
		PIPE_CONTROL_FLUSH(scratch_addr);
		OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH |
			 PIPE_CONTROL_NOTIFY);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(4);
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(seqno);

		OUT_RING(MI_USER_INTERRUPT);
		ADVANCE_LP_RING();
	}
	return seqno;
}

void i915_user_irq_get(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (dev->irq_enabled && (++dev_priv->user_irq_refcount == 1)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_enable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_enable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

void i915_user_irq_put(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	BUG_ON(dev->irq_enabled && dev_priv->user_irq_refcount <= 0);
	if (dev->irq_enabled && (--dev_priv->user_irq_refcount == 0)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_disable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_disable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

/** Dispatch a batchbuffer to the ring
 */
int
i915_dispatch_gem_execbuffer(struct drm_device *dev,
			      struct drm_i915_gem_execbuffer2 *exec,
			      struct drm_clip_rect *cliprects,
			      uint64_t exec_offset)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int nbox = exec->num_cliprects;
	int i = 0, count;
	uint32_t exec_start, exec_len;
	RING_LOCALS;

	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	exec_len = (uint32_t) exec->batch_len;

	trace_i915_gem_request_submit(dev, dev_priv->mm.next_gem_seqno + 1);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, cliprects, i,
						exec->DR1, exec->DR4);
			if (ret)
				return ret;
		}

		if (IS_I830(dev) || IS_845G(dev)) {
			BEGIN_LP_RING(4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(exec_start | MI_BATCH_NON_SECURE);
			OUT_RING(exec_start + exec_len - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();
		} else {
			BEGIN_LP_RING(2);
			if (IS_I965G(dev)) {
				OUT_RING(MI_BATCH_BUFFER_START |
					 (2 << 6) |
					 MI_BATCH_NON_SECURE_I965);
				OUT_RING(exec_start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START |
					 (2 << 6));
				OUT_RING(exec_start | MI_BATCH_NON_SECURE);
			}
			ADVANCE_LP_RING();
		}
	}

	/* XXX breadcrumb */
	return 0;
}

static void
i915_gem_cleanup_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	if (dev_priv->hws_obj == NULL)
		return;

	obj = dev_priv->hws_obj;
	obj_priv = to_intel_bo(obj);

	kunmap(obj_priv->pages[0]);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(obj);
	dev_priv->hws_obj = NULL;

	memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
	dev_priv->hw_status_page = NULL;

	if (HAS_PIPE_CONTROL(dev))
		i915_gem_cleanup_pipe_control(dev);

	/* Write high address into HWS_PGA when disabling. */
	I915_WRITE(HWS_PGA, 0x1ffff000);
}

static int
i915_gem_init_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	/* If we need a physical address for the status page, it's already
	 * initialized at driver load time.
	 */
	if (!I915_NEED_GFX_HWS(dev))
		return 0;

	obj = i915_gem_alloc_object(dev, 4096);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate status page\n");
		ret = -ENOMEM;
		goto err;
	}
	obj_priv = to_intel_bo(obj);
	obj_priv->agp_type = AGP_USER_CACHED_MEMORY;

	ret = i915_gem_object_pin(obj, 4096);
	if (ret != 0) {
		drm_gem_object_unreference(obj);
		goto err_unref;
	}

	dev_priv->status_gfx_addr = obj_priv->gtt_offset;

	dev_priv->hw_status_page = kmap(obj_priv->pages[0]);
	if (dev_priv->hw_status_page == NULL) {
		DRM_ERROR("Failed to map status page.\n");
		memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
		ret = -EINVAL;
		goto err_unpin;
	}

	if (HAS_PIPE_CONTROL(dev)) {
		ret = i915_gem_init_pipe_control(dev);
		if (ret)
			goto err_unpin;
	}

	dev_priv->hws_obj = obj;
	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	if (IS_GEN6(dev)) {
		I915_WRITE(HWS_PGA_GEN6, dev_priv->status_gfx_addr);
		I915_READ(HWS_PGA_GEN6); /* posting read */
	} else {
		I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
		I915_READ(HWS_PGA); /* posting read */
	}
	DRM_DEBUG_DRIVER("hws offset: 0x%08x\n", dev_priv->status_gfx_addr);

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(obj);
err:
	return 0;
}

int
i915_gem_init_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	drm_i915_ring_buffer_t *ring = &dev_priv->ring;
	int ret;
	u32 head;

	ret = i915_gem_init_hws(dev);
	if (ret != 0)
		return ret;

	obj = i915_gem_alloc_object(dev, 128 * 1024);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate ringbuffer\n");
		i915_gem_cleanup_hws(dev);
		return -ENOMEM;
	}
	obj_priv = to_intel_bo(obj);

	ret = i915_gem_object_pin(obj, 4096);
	if (ret != 0) {
		drm_gem_object_unreference(obj);
		i915_gem_cleanup_hws(dev);
		return ret;
	}

	/* Set up the kernel mapping for the ring. */
	ring->Size = obj->size;

	ring->map.offset = dev->agp->base + obj_priv->gtt_offset;
	ring->map.size = obj->size;
	ring->map.type = 0;
	ring->map.flags = 0;
	ring->map.mtrr = 0;

	drm_core_ioremap_wc(&ring->map, dev);
	if (ring->map.handle == NULL) {
		DRM_ERROR("Failed to map ringbuffer.\n");
		memset(&dev_priv->ring, 0, sizeof(dev_priv->ring));
		i915_gem_object_unpin(obj);
		drm_gem_object_unreference(obj);
		i915_gem_cleanup_hws(dev);
		return -EINVAL;
	}
	ring->ring_obj = obj;
	ring->virtual_start = ring->map.handle;

	/* Stop the ring if it's running. */
	I915_WRITE(PRB0_CTL, 0);
	I915_WRITE(PRB0_TAIL, 0);
	I915_WRITE(PRB0_HEAD, 0);

	/* Initialize the ring. */
	I915_WRITE(PRB0_START, obj_priv->gtt_offset);
	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;

	/* G45 ring initialization fails to reset head to zero */
	if (head != 0) {
		DRM_ERROR("Ring head not reset to zero "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
		I915_WRITE(PRB0_HEAD, 0);

		DRM_ERROR("Ring head forced to zero "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
	}

	I915_WRITE(PRB0_CTL,
		   ((obj->size - 4096) & RING_NR_PAGES) |
		   RING_NO_REPORT |
		   RING_VALID);

	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;

	/* If the head is still not zero, the ring is dead */
	if (head != 0) {
		DRM_ERROR("Ring initialization failed "
			  "ctl %08x head %08x tail %08x start %08x\n",
			  I915_READ(PRB0_CTL),
			  I915_READ(PRB0_HEAD),
			  I915_READ(PRB0_TAIL),
			  I915_READ(PRB0_START));
		return -EIO;
	}

	/* Update our cache of the ring state */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_kernel_lost_context(dev);
	else {
		ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
		ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
	}

	if (IS_I9XX(dev) && !IS_GEN3(dev)) {
		I915_WRITE(MI_MODE,
			   (VS_TIMER_DISPATCH) << 16 | VS_TIMER_DISPATCH);
	}

	return 0;
}

void
i915_gem_cleanup_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (dev_priv->ring.ring_obj == NULL)
		return;

	drm_core_ioremapfree(&dev_priv->ring.map, dev);

	i915_gem_object_unpin(dev_priv->ring.ring_obj);
	drm_gem_object_unreference(dev_priv->ring.ring_obj);
	dev_priv->ring.ring_obj = NULL;
	memset(&dev_priv->ring, 0, sizeof(dev_priv->ring));

	i915_gem_cleanup_hws(dev);
}

/* As a ringbuffer is only allowed to wrap between instructions, fill
 * the tail with NOOPs.
 */
int i915_wrap_ring(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	volatile unsigned int *virt;
	int rem;

	rem = dev_priv->ring.Size - dev_priv->ring.tail;
	if (dev_priv->ring.space < rem) {
		int ret = i915_wait_ring(dev, rem, __func__);
		if (ret)
			return ret;
	}
	dev_priv->ring.space -= rem;

	virt = (unsigned int *)
		(dev_priv->ring.virtual_start + dev_priv->ring.tail);
	rem /= 4;
	while (rem--)
		*virt++ = MI_NOOP;

	dev_priv->ring.tail = 0;

	return 0;
}

int i915_wait_ring(struct drm_device * dev, int n, const char *caller)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);
	u32 acthd_reg = IS_I965G(dev) ? ACTHD_I965 : ACTHD;
	u32 last_acthd = I915_READ(acthd_reg);
	u32 acthd;
	u32 last_head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	int i;

	trace_i915_ring_wait_begin (dev);

	for (i = 0; i < 100000; i++) {
		ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
		acthd = I915_READ(acthd_reg);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n) {
			trace_i915_ring_wait_end (dev);
			return 0;
		}

		if (dev->primary->master) {
			struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
		}


		if (ring->head != last_head)
			i = 0;
		if (acthd != last_acthd)
			i = 0;

		last_head = ring->head;
		last_acthd = acthd;
		msleep_interruptible(10);

	}

	trace_i915_ring_wait_end (dev);
	return -EBUSY;
}
