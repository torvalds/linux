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
#include "i915_drv.h"
#include "i915_drm.h"
#include "i915_trace.h"
#include "intel_drv.h"

static u32 i915_gem_get_seqno(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 seqno;

	seqno = dev_priv->next_seqno;

	/* reserve 0 for non-seqno */
	if (++dev_priv->next_seqno == 0)
		dev_priv->next_seqno = 1;

	return seqno;
}

static int
render_ring_flush(struct intel_ring_buffer *ring,
		  u32	invalidate_domains,
		  u32	flush_domains)
{
	struct drm_device *dev = ring->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 cmd;
	int ret;

#if WATCH_EXEC
	DRM_INFO("%s: invalidate %08x flush %08x\n", __func__,
		  invalidate_domains, flush_domains);
#endif

	trace_i915_gem_request_flush(dev, dev_priv->next_seqno,
				     invalidate_domains, flush_domains);

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
		if (INTEL_INFO(dev)->gen < 4) {
			/*
			 * On the 965, the sampler cache always gets flushed
			 * and this bit is reserved.
			 */
			if (invalidate_domains & I915_GEM_DOMAIN_SAMPLER)
				cmd |= MI_READ_FLUSH;
		}
		if (invalidate_domains & I915_GEM_DOMAIN_INSTRUCTION)
			cmd |= MI_EXE_FLUSH;

		if (invalidate_domains & I915_GEM_DOMAIN_COMMAND &&
		    (IS_G4X(dev) || IS_GEN5(dev)))
			cmd |= MI_INVALIDATE_ISP;

#if WATCH_EXEC
		DRM_INFO("%s: queue flush %08x to ring\n", __func__, cmd);
#endif
		ret = intel_ring_begin(ring, 2);
		if (ret)
			return ret;

		intel_ring_emit(ring, cmd);
		intel_ring_emit(ring, MI_NOOP);
		intel_ring_advance(ring);
	}

	return 0;
}

static void ring_write_tail(struct intel_ring_buffer *ring,
			    u32 value)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	I915_WRITE_TAIL(ring, value);
}

u32 intel_ring_get_active_head(struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	u32 acthd_reg = INTEL_INFO(ring->dev)->gen >= 4 ?
			RING_ACTHD(ring->mmio_base) : ACTHD;

	return I915_READ(acthd_reg);
}

static int init_ring_common(struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_object *obj = ring->obj;
	u32 head;

	/* Stop the ring if it's running. */
	I915_WRITE_CTL(ring, 0);
	I915_WRITE_HEAD(ring, 0);
	ring->write_tail(ring, 0);

	/* Initialize the ring. */
	I915_WRITE_START(ring, obj->gtt_offset);
	head = I915_READ_HEAD(ring) & HEAD_ADDR;

	/* G45 ring initialization fails to reset head to zero */
	if (head != 0) {
		DRM_DEBUG_KMS("%s head not reset to zero "
			      "ctl %08x head %08x tail %08x start %08x\n",
			      ring->name,
			      I915_READ_CTL(ring),
			      I915_READ_HEAD(ring),
			      I915_READ_TAIL(ring),
			      I915_READ_START(ring));

		I915_WRITE_HEAD(ring, 0);

		if (I915_READ_HEAD(ring) & HEAD_ADDR) {
			DRM_ERROR("failed to set %s head to zero "
				  "ctl %08x head %08x tail %08x start %08x\n",
				  ring->name,
				  I915_READ_CTL(ring),
				  I915_READ_HEAD(ring),
				  I915_READ_TAIL(ring),
				  I915_READ_START(ring));
		}
	}

	I915_WRITE_CTL(ring,
			((ring->size - PAGE_SIZE) & RING_NR_PAGES)
			| RING_REPORT_64K | RING_VALID);

	/* If the head is still not zero, the ring is dead */
	if ((I915_READ_CTL(ring) & RING_VALID) == 0 ||
	    I915_READ_START(ring) != obj->gtt_offset ||
	    (I915_READ_HEAD(ring) & HEAD_ADDR) != 0) {
		DRM_ERROR("%s initialization failed "
				"ctl %08x head %08x tail %08x start %08x\n",
				ring->name,
				I915_READ_CTL(ring),
				I915_READ_HEAD(ring),
				I915_READ_TAIL(ring),
				I915_READ_START(ring));
		return -EIO;
	}

	if (!drm_core_check_feature(ring->dev, DRIVER_MODESET))
		i915_kernel_lost_context(ring->dev);
	else {
		ring->head = I915_READ_HEAD(ring) & HEAD_ADDR;
		ring->tail = I915_READ_TAIL(ring) & TAIL_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->size;
	}

	return 0;
}

/*
 * 965+ support PIPE_CONTROL commands, which provide finer grained control
 * over cache flushing.
 */
struct pipe_control {
	struct drm_i915_gem_object *obj;
	volatile u32 *cpu_page;
	u32 gtt_offset;
};

static int
init_pipe_control(struct intel_ring_buffer *ring)
{
	struct pipe_control *pc;
	struct drm_i915_gem_object *obj;
	int ret;

	if (ring->private)
		return 0;

	pc = kmalloc(sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	obj = i915_gem_alloc_object(ring->dev, 4096);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate seqno page\n");
		ret = -ENOMEM;
		goto err;
	}
	obj->agp_type = AGP_USER_CACHED_MEMORY;

	ret = i915_gem_object_pin(obj, 4096, true);
	if (ret)
		goto err_unref;

	pc->gtt_offset = obj->gtt_offset;
	pc->cpu_page =  kmap(obj->pages[0]);
	if (pc->cpu_page == NULL)
		goto err_unpin;

	pc->obj = obj;
	ring->private = pc;
	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(&obj->base);
err:
	kfree(pc);
	return ret;
}

static void
cleanup_pipe_control(struct intel_ring_buffer *ring)
{
	struct pipe_control *pc = ring->private;
	struct drm_i915_gem_object *obj;

	if (!ring->private)
		return;

	obj = pc->obj;
	kunmap(obj->pages[0]);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(&obj->base);

	kfree(pc);
	ring->private = NULL;
}

static int init_render_ring(struct intel_ring_buffer *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = init_ring_common(ring);

	if (INTEL_INFO(dev)->gen > 3) {
		int mode = VS_TIMER_DISPATCH << 16 | VS_TIMER_DISPATCH;
		if (IS_GEN6(dev))
			mode |= MI_FLUSH_ENABLE << 16 | MI_FLUSH_ENABLE;
		I915_WRITE(MI_MODE, mode);
	}

	if (INTEL_INFO(dev)->gen >= 6) {
	} else if (IS_GEN5(dev)) {
		ret = init_pipe_control(ring);
		if (ret)
			return ret;
	}

	return ret;
}

static void render_ring_cleanup(struct intel_ring_buffer *ring)
{
	if (!ring->private)
		return;

	cleanup_pipe_control(ring);
}

static void
update_semaphore(struct intel_ring_buffer *ring, int i, u32 seqno)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int id;

	/*
	 * cs -> 1 = vcs, 0 = bcs
	 * vcs -> 1 = bcs, 0 = cs,
	 * bcs -> 1 = cs, 0 = vcs.
	 */
	id = ring - dev_priv->ring;
	id += 2 - i;
	id %= 3;

	intel_ring_emit(ring,
			MI_SEMAPHORE_MBOX |
			MI_SEMAPHORE_REGISTER |
			MI_SEMAPHORE_UPDATE);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring,
			RING_SYNC_0(dev_priv->ring[id].mmio_base) + 4*i);
}

static int
gen6_add_request(struct intel_ring_buffer *ring,
		 u32 *result)
{
	u32 seqno;
	int ret;

	ret = intel_ring_begin(ring, 10);
	if (ret)
		return ret;

	seqno = i915_gem_get_seqno(ring->dev);
	update_semaphore(ring, 0, seqno);
	update_semaphore(ring, 1, seqno);

	intel_ring_emit(ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(ring, I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, MI_USER_INTERRUPT);
	intel_ring_advance(ring);

	*result = seqno;
	return 0;
}

int
intel_ring_sync(struct intel_ring_buffer *ring,
		struct intel_ring_buffer *to,
		u32 seqno)
{
	int ret;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring,
			MI_SEMAPHORE_MBOX |
			MI_SEMAPHORE_REGISTER |
			intel_ring_sync_index(ring, to) << 17 |
			MI_SEMAPHORE_COMPARE);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

#define PIPE_CONTROL_FLUSH(ring__, addr__)					\
do {									\
	intel_ring_emit(ring__, GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |		\
		 PIPE_CONTROL_DEPTH_STALL | 2);				\
	intel_ring_emit(ring__, (addr__) | PIPE_CONTROL_GLOBAL_GTT);			\
	intel_ring_emit(ring__, 0);							\
	intel_ring_emit(ring__, 0);							\
} while (0)

static int
pc_render_add_request(struct intel_ring_buffer *ring,
		      u32 *result)
{
	struct drm_device *dev = ring->dev;
	u32 seqno = i915_gem_get_seqno(dev);
	struct pipe_control *pc = ring->private;
	u32 scratch_addr = pc->gtt_offset + 128;
	int ret;

	/* For Ironlake, MI_USER_INTERRUPT was deprecated and apparently
	 * incoherent with writes to memory, i.e. completely fubar,
	 * so we need to use PIPE_NOTIFY instead.
	 *
	 * However, we also need to workaround the qword write
	 * incoherence by flushing the 6 PIPE_NOTIFY buffers out to
	 * memory before requesting an interrupt.
	 */
	ret = intel_ring_begin(ring, 32);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH);
	intel_ring_emit(ring, pc->gtt_offset | PIPE_CONTROL_GLOBAL_GTT);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, 0);
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 128; /* write to separate cachelines */
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 128;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 128;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 128;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 128;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |
			PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_TC_FLUSH |
			PIPE_CONTROL_NOTIFY);
	intel_ring_emit(ring, pc->gtt_offset | PIPE_CONTROL_GLOBAL_GTT);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);

	*result = seqno;
	return 0;
}

static int
render_ring_add_request(struct intel_ring_buffer *ring,
			u32 *result)
{
	struct drm_device *dev = ring->dev;
	u32 seqno = i915_gem_get_seqno(dev);
	int ret;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(ring, I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, MI_USER_INTERRUPT);
	intel_ring_advance(ring);

	*result = seqno;
	return 0;
}

static u32
ring_get_seqno(struct intel_ring_buffer *ring)
{
	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static u32
pc_render_get_seqno(struct intel_ring_buffer *ring)
{
	struct pipe_control *pc = ring->private;
	return pc->cpu_page[0];
}

static bool
render_ring_get_irq(struct intel_ring_buffer *ring)
{
	struct drm_device *dev = ring->dev;

	if (!dev->irq_enabled)
		return false;

	if (atomic_inc_return(&ring->irq_refcount) == 1) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		unsigned long irqflags;

		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		if (HAS_PCH_SPLIT(dev))
			ironlake_enable_graphics_irq(dev_priv,
						     GT_PIPE_NOTIFY | GT_USER_INTERRUPT);
		else
			i915_enable_irq(dev_priv, I915_USER_INTERRUPT);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}

	return true;
}

static void
render_ring_put_irq(struct intel_ring_buffer *ring)
{
	struct drm_device *dev = ring->dev;

	if (atomic_dec_and_test(&ring->irq_refcount)) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		unsigned long irqflags;

		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		if (HAS_PCH_SPLIT(dev))
			ironlake_disable_graphics_irq(dev_priv,
						      GT_USER_INTERRUPT |
						      GT_PIPE_NOTIFY);
		else
			i915_disable_irq(dev_priv, I915_USER_INTERRUPT);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}
}

void intel_ring_setup_status_page(struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	u32 mmio = IS_GEN6(ring->dev) ?
		RING_HWS_PGA_GEN6(ring->mmio_base) :
		RING_HWS_PGA(ring->mmio_base);
	I915_WRITE(mmio, (u32)ring->status_page.gfx_addr);
	POSTING_READ(mmio);
}

static int
bsd_ring_flush(struct intel_ring_buffer *ring,
	       u32     invalidate_domains,
	       u32     flush_domains)
{
	int ret;

	if ((flush_domains & I915_GEM_DOMAIN_RENDER) == 0)
		return 0;

	ret = intel_ring_begin(ring, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_FLUSH);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);
	return 0;
}

static int
ring_add_request(struct intel_ring_buffer *ring,
		 u32 *result)
{
	u32 seqno;
	int ret;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		return ret;

	seqno = i915_gem_get_seqno(ring->dev);

	intel_ring_emit(ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(ring, I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(ring, seqno);
	intel_ring_emit(ring, MI_USER_INTERRUPT);
	intel_ring_advance(ring);

	DRM_DEBUG_DRIVER("%s %d\n", ring->name, seqno);
	*result = seqno;
	return 0;
}

static bool
ring_get_irq(struct intel_ring_buffer *ring, u32 flag)
{
	struct drm_device *dev = ring->dev;

	if (!dev->irq_enabled)
	       return false;

	if (atomic_inc_return(&ring->irq_refcount) == 1) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		unsigned long irqflags;

		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		ironlake_enable_graphics_irq(dev_priv, flag);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}

	return true;
}

static void
ring_put_irq(struct intel_ring_buffer *ring, u32 flag)
{
	struct drm_device *dev = ring->dev;

	if (atomic_dec_and_test(&ring->irq_refcount)) {
		drm_i915_private_t *dev_priv = dev->dev_private;
		unsigned long irqflags;

		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		ironlake_disable_graphics_irq(dev_priv, flag);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}
}

static bool
bsd_ring_get_irq(struct intel_ring_buffer *ring)
{
	return ring_get_irq(ring, GT_BSD_USER_INTERRUPT);
}
static void
bsd_ring_put_irq(struct intel_ring_buffer *ring)
{
	ring_put_irq(ring, GT_BSD_USER_INTERRUPT);
}

static int
ring_dispatch_execbuffer(struct intel_ring_buffer *ring, u32 offset, u32 length)
{
	int ret;

	ret = intel_ring_begin(ring, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring,
			MI_BATCH_BUFFER_START | (2 << 6) |
			MI_BATCH_NON_SECURE_I965);
	intel_ring_emit(ring, offset);
	intel_ring_advance(ring);

	return 0;
}

static int
render_ring_dispatch_execbuffer(struct intel_ring_buffer *ring,
				u32 offset, u32 len)
{
	struct drm_device *dev = ring->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	trace_i915_gem_request_submit(dev, dev_priv->next_seqno + 1);

	if (IS_I830(dev) || IS_845G(dev)) {
		ret = intel_ring_begin(ring, 4);
		if (ret)
			return ret;

		intel_ring_emit(ring, MI_BATCH_BUFFER);
		intel_ring_emit(ring, offset | MI_BATCH_NON_SECURE);
		intel_ring_emit(ring, offset + len - 8);
		intel_ring_emit(ring, 0);
	} else {
		ret = intel_ring_begin(ring, 2);
		if (ret)
			return ret;

		if (INTEL_INFO(dev)->gen >= 4) {
			intel_ring_emit(ring,
					MI_BATCH_BUFFER_START | (2 << 6) |
					MI_BATCH_NON_SECURE_I965);
			intel_ring_emit(ring, offset);
		} else {
			intel_ring_emit(ring,
					MI_BATCH_BUFFER_START | (2 << 6));
			intel_ring_emit(ring, offset | MI_BATCH_NON_SECURE);
		}
	}
	intel_ring_advance(ring);

	return 0;
}

static void cleanup_status_page(struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_object *obj;

	obj = ring->status_page.obj;
	if (obj == NULL)
		return;

	kunmap(obj->pages[0]);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(&obj->base);
	ring->status_page.obj = NULL;

	memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
}

static int init_status_page(struct intel_ring_buffer *ring)
{
	struct drm_device *dev = ring->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int ret;

	obj = i915_gem_alloc_object(dev, 4096);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate status page\n");
		ret = -ENOMEM;
		goto err;
	}
	obj->agp_type = AGP_USER_CACHED_MEMORY;

	ret = i915_gem_object_pin(obj, 4096, true);
	if (ret != 0) {
		goto err_unref;
	}

	ring->status_page.gfx_addr = obj->gtt_offset;
	ring->status_page.page_addr = kmap(obj->pages[0]);
	if (ring->status_page.page_addr == NULL) {
		memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
		goto err_unpin;
	}
	ring->status_page.obj = obj;
	memset(ring->status_page.page_addr, 0, PAGE_SIZE);

	intel_ring_setup_status_page(ring);
	DRM_DEBUG_DRIVER("%s hws offset: 0x%08x\n",
			ring->name, ring->status_page.gfx_addr);

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(&obj->base);
err:
	return ret;
}

int intel_init_ring_buffer(struct drm_device *dev,
			   struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_object *obj;
	int ret;

	ring->dev = dev;
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	INIT_LIST_HEAD(&ring->gpu_write_list);

	if (I915_NEED_GFX_HWS(dev)) {
		ret = init_status_page(ring);
		if (ret)
			return ret;
	}

	obj = i915_gem_alloc_object(dev, ring->size);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate ringbuffer\n");
		ret = -ENOMEM;
		goto err_hws;
	}

	ring->obj = obj;

	ret = i915_gem_object_pin(obj, PAGE_SIZE, true);
	if (ret)
		goto err_unref;

	ring->map.size = ring->size;
	ring->map.offset = dev->agp->base + obj->gtt_offset;
	ring->map.type = 0;
	ring->map.flags = 0;
	ring->map.mtrr = 0;

	drm_core_ioremap_wc(&ring->map, dev);
	if (ring->map.handle == NULL) {
		DRM_ERROR("Failed to map ringbuffer.\n");
		ret = -EINVAL;
		goto err_unpin;
	}

	ring->virtual_start = ring->map.handle;
	ret = ring->init(ring);
	if (ret)
		goto err_unmap;

	/* Workaround an erratum on the i830 which causes a hang if
	 * the TAIL pointer points to within the last 2 cachelines
	 * of the buffer.
	 */
	ring->effective_size = ring->size;
	if (IS_I830(ring->dev))
		ring->effective_size -= 128;

	return 0;

err_unmap:
	drm_core_ioremapfree(&ring->map, dev);
err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(&obj->base);
	ring->obj = NULL;
err_hws:
	cleanup_status_page(ring);
	return ret;
}

void intel_cleanup_ring_buffer(struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv;
	int ret;

	if (ring->obj == NULL)
		return;

	/* Disable the ring buffer. The ring must be idle at this point */
	dev_priv = ring->dev->dev_private;
	ret = intel_wait_ring_buffer(ring, ring->size - 8);
	I915_WRITE_CTL(ring, 0);

	drm_core_ioremapfree(&ring->map, ring->dev);

	i915_gem_object_unpin(ring->obj);
	drm_gem_object_unreference(&ring->obj->base);
	ring->obj = NULL;

	if (ring->cleanup)
		ring->cleanup(ring);

	cleanup_status_page(ring);
}

static int intel_wrap_ring_buffer(struct intel_ring_buffer *ring)
{
	unsigned int *virt;
	int rem = ring->size - ring->tail;

	if (ring->space < rem) {
		int ret = intel_wait_ring_buffer(ring, rem);
		if (ret)
			return ret;
	}

	virt = (unsigned int *)(ring->virtual_start + ring->tail);
	rem /= 8;
	while (rem--) {
		*virt++ = MI_NOOP;
		*virt++ = MI_NOOP;
	}

	ring->tail = 0;
	ring->space = ring->head - 8;

	return 0;
}

int intel_wait_ring_buffer(struct intel_ring_buffer *ring, int n)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long end;
	u32 head;

	trace_i915_ring_wait_begin (dev);
	end = jiffies + 3 * HZ;
	do {
		/* If the reported head position has wrapped or hasn't advanced,
		 * fallback to the slow and accurate path.
		 */
		head = intel_read_status_page(ring, 4);
		if (head < ring->actual_head)
			head = I915_READ_HEAD(ring);
		ring->actual_head = head;
		ring->head = head & HEAD_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->size;
		if (ring->space >= n) {
			trace_i915_ring_wait_end(dev);
			return 0;
		}

		if (dev->primary->master) {
			struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
		}

		msleep(1);
		if (atomic_read(&dev_priv->mm.wedged))
			return -EAGAIN;
	} while (!time_after(jiffies, end));
	trace_i915_ring_wait_end (dev);
	return -EBUSY;
}

int intel_ring_begin(struct intel_ring_buffer *ring,
		     int num_dwords)
{
	int n = 4*num_dwords;
	int ret;

	if (unlikely(ring->tail + n > ring->effective_size)) {
		ret = intel_wrap_ring_buffer(ring);
		if (unlikely(ret))
			return ret;
	}

	if (unlikely(ring->space < n)) {
		ret = intel_wait_ring_buffer(ring, n);
		if (unlikely(ret))
			return ret;
	}

	ring->space -= n;
	return 0;
}

void intel_ring_advance(struct intel_ring_buffer *ring)
{
	ring->tail &= ring->size - 1;
	ring->write_tail(ring, ring->tail);
}

static const struct intel_ring_buffer render_ring = {
	.name			= "render ring",
	.id			= RING_RENDER,
	.mmio_base		= RENDER_RING_BASE,
	.size			= 32 * PAGE_SIZE,
	.init			= init_render_ring,
	.write_tail		= ring_write_tail,
	.flush			= render_ring_flush,
	.add_request		= render_ring_add_request,
	.get_seqno		= ring_get_seqno,
	.irq_get		= render_ring_get_irq,
	.irq_put		= render_ring_put_irq,
	.dispatch_execbuffer	= render_ring_dispatch_execbuffer,
       .cleanup			= render_ring_cleanup,
};

/* ring buffer for bit-stream decoder */

static const struct intel_ring_buffer bsd_ring = {
	.name                   = "bsd ring",
	.id			= RING_BSD,
	.mmio_base		= BSD_RING_BASE,
	.size			= 32 * PAGE_SIZE,
	.init			= init_ring_common,
	.write_tail		= ring_write_tail,
	.flush			= bsd_ring_flush,
	.add_request		= ring_add_request,
	.get_seqno		= ring_get_seqno,
	.irq_get		= bsd_ring_get_irq,
	.irq_put		= bsd_ring_put_irq,
	.dispatch_execbuffer	= ring_dispatch_execbuffer,
};


static void gen6_bsd_ring_write_tail(struct intel_ring_buffer *ring,
				     u32 value)
{
       drm_i915_private_t *dev_priv = ring->dev->dev_private;

       /* Every tail move must follow the sequence below */
       I915_WRITE(GEN6_BSD_SLEEP_PSMI_CONTROL,
	       GEN6_BSD_SLEEP_PSMI_CONTROL_RC_ILDL_MESSAGE_MODIFY_MASK |
	       GEN6_BSD_SLEEP_PSMI_CONTROL_RC_ILDL_MESSAGE_DISABLE);
       I915_WRITE(GEN6_BSD_RNCID, 0x0);

       if (wait_for((I915_READ(GEN6_BSD_SLEEP_PSMI_CONTROL) &
                               GEN6_BSD_SLEEP_PSMI_CONTROL_IDLE_INDICATOR) == 0,
                       50))
               DRM_ERROR("timed out waiting for IDLE Indicator\n");

       I915_WRITE_TAIL(ring, value);
       I915_WRITE(GEN6_BSD_SLEEP_PSMI_CONTROL,
	       GEN6_BSD_SLEEP_PSMI_CONTROL_RC_ILDL_MESSAGE_MODIFY_MASK |
	       GEN6_BSD_SLEEP_PSMI_CONTROL_RC_ILDL_MESSAGE_ENABLE);
}

static int gen6_ring_flush(struct intel_ring_buffer *ring,
			   u32 invalidate_domains,
			   u32 flush_domains)
{
	int ret;

	if ((flush_domains & I915_GEM_DOMAIN_RENDER) == 0)
		return 0;

	ret = intel_ring_begin(ring, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_FLUSH_DW);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);
	return 0;
}

static int
gen6_ring_dispatch_execbuffer(struct intel_ring_buffer *ring,
			      u32 offset, u32 len)
{
       int ret;

       ret = intel_ring_begin(ring, 2);
       if (ret)
	       return ret;

       intel_ring_emit(ring, MI_BATCH_BUFFER_START | MI_BATCH_NON_SECURE_I965);
       /* bit0-7 is the length on GEN6+ */
       intel_ring_emit(ring, offset);
       intel_ring_advance(ring);

       return 0;
}

static bool
gen6_bsd_ring_get_irq(struct intel_ring_buffer *ring)
{
	return ring_get_irq(ring, GT_GEN6_BSD_USER_INTERRUPT);
}

static void
gen6_bsd_ring_put_irq(struct intel_ring_buffer *ring)
{
	ring_put_irq(ring, GT_GEN6_BSD_USER_INTERRUPT);
}

/* ring buffer for Video Codec for Gen6+ */
static const struct intel_ring_buffer gen6_bsd_ring = {
	.name			= "gen6 bsd ring",
	.id			= RING_BSD,
	.mmio_base		= GEN6_BSD_RING_BASE,
	.size			= 32 * PAGE_SIZE,
	.init			= init_ring_common,
	.write_tail		= gen6_bsd_ring_write_tail,
	.flush			= gen6_ring_flush,
	.add_request		= gen6_add_request,
	.get_seqno		= ring_get_seqno,
	.irq_get		= gen6_bsd_ring_get_irq,
	.irq_put		= gen6_bsd_ring_put_irq,
	.dispatch_execbuffer	= gen6_ring_dispatch_execbuffer,
};

/* Blitter support (SandyBridge+) */

static bool
blt_ring_get_irq(struct intel_ring_buffer *ring)
{
	return ring_get_irq(ring, GT_BLT_USER_INTERRUPT);
}

static void
blt_ring_put_irq(struct intel_ring_buffer *ring)
{
	ring_put_irq(ring, GT_BLT_USER_INTERRUPT);
}


/* Workaround for some stepping of SNB,
 * each time when BLT engine ring tail moved,
 * the first command in the ring to be parsed
 * should be MI_BATCH_BUFFER_START
 */
#define NEED_BLT_WORKAROUND(dev) \
	(IS_GEN6(dev) && (dev->pdev->revision < 8))

static inline struct drm_i915_gem_object *
to_blt_workaround(struct intel_ring_buffer *ring)
{
	return ring->private;
}

static int blt_ring_init(struct intel_ring_buffer *ring)
{
	if (NEED_BLT_WORKAROUND(ring->dev)) {
		struct drm_i915_gem_object *obj;
		u32 *ptr;
		int ret;

		obj = i915_gem_alloc_object(ring->dev, 4096);
		if (obj == NULL)
			return -ENOMEM;

		ret = i915_gem_object_pin(obj, 4096, true);
		if (ret) {
			drm_gem_object_unreference(&obj->base);
			return ret;
		}

		ptr = kmap(obj->pages[0]);
		*ptr++ = MI_BATCH_BUFFER_END;
		*ptr++ = MI_NOOP;
		kunmap(obj->pages[0]);

		ret = i915_gem_object_set_to_gtt_domain(obj, false);
		if (ret) {
			i915_gem_object_unpin(obj);
			drm_gem_object_unreference(&obj->base);
			return ret;
		}

		ring->private = obj;
	}

	return init_ring_common(ring);
}

static int blt_ring_begin(struct intel_ring_buffer *ring,
			  int num_dwords)
{
	if (ring->private) {
		int ret = intel_ring_begin(ring, num_dwords+2);
		if (ret)
			return ret;

		intel_ring_emit(ring, MI_BATCH_BUFFER_START);
		intel_ring_emit(ring, to_blt_workaround(ring)->gtt_offset);

		return 0;
	} else
		return intel_ring_begin(ring, 4);
}

static int blt_ring_flush(struct intel_ring_buffer *ring,
			   u32 invalidate_domains,
			   u32 flush_domains)
{
	int ret;

	if ((flush_domains & I915_GEM_DOMAIN_RENDER) == 0)
		return 0;

	ret = blt_ring_begin(ring, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_FLUSH_DW);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);
	return 0;
}

static void blt_ring_cleanup(struct intel_ring_buffer *ring)
{
	if (!ring->private)
		return;

	i915_gem_object_unpin(ring->private);
	drm_gem_object_unreference(ring->private);
	ring->private = NULL;
}

static const struct intel_ring_buffer gen6_blt_ring = {
       .name			= "blt ring",
       .id			= RING_BLT,
       .mmio_base		= BLT_RING_BASE,
       .size			= 32 * PAGE_SIZE,
       .init			= blt_ring_init,
       .write_tail		= ring_write_tail,
       .flush			= blt_ring_flush,
       .add_request		= gen6_add_request,
       .get_seqno		= ring_get_seqno,
       .irq_get			= blt_ring_get_irq,
       .irq_put			= blt_ring_put_irq,
       .dispatch_execbuffer	= gen6_ring_dispatch_execbuffer,
       .cleanup			= blt_ring_cleanup,
};

int intel_init_render_ring_buffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = &dev_priv->ring[RCS];

	*ring = render_ring;
	if (INTEL_INFO(dev)->gen >= 6) {
		ring->add_request = gen6_add_request;
	} else if (IS_GEN5(dev)) {
		ring->add_request = pc_render_add_request;
		ring->get_seqno = pc_render_get_seqno;
	}

	if (!I915_NEED_GFX_HWS(dev)) {
		ring->status_page.page_addr = dev_priv->status_page_dmah->vaddr;
		memset(ring->status_page.page_addr, 0, PAGE_SIZE);
	}

	return intel_init_ring_buffer(dev, ring);
}

int intel_init_bsd_ring_buffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = &dev_priv->ring[VCS];

	if (IS_GEN6(dev))
		*ring = gen6_bsd_ring;
	else
		*ring = bsd_ring;

	return intel_init_ring_buffer(dev, ring);
}

int intel_init_blt_ring_buffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = &dev_priv->ring[BCS];

	*ring = gen6_blt_ring;

	return intel_init_ring_buffer(dev, ring);
}
