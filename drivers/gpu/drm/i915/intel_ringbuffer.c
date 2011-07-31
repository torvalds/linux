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

static void
render_ring_flush(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		u32	invalidate_domains,
		u32	flush_domains)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 cmd;

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
		intel_ring_begin(dev, ring, 2);
		intel_ring_emit(dev, ring, cmd);
		intel_ring_emit(dev, ring, MI_NOOP);
		intel_ring_advance(dev, ring);
	}
}

static unsigned int render_ring_get_head(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return I915_READ(PRB0_HEAD) & HEAD_ADDR;
}

static unsigned int render_ring_get_tail(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return I915_READ(PRB0_TAIL) & TAIL_ADDR;
}

static unsigned int render_ring_get_active_head(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 acthd_reg = IS_I965G(dev) ? ACTHD_I965 : ACTHD;

	return I915_READ(acthd_reg);
}

static void render_ring_advance_ring(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	I915_WRITE(PRB0_TAIL, ring->tail);
}

static int init_ring_common(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	u32 head;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	obj_priv = to_intel_bo(ring->gem_object);

	/* Stop the ring if it's running. */
	I915_WRITE(ring->regs.ctl, 0);
	I915_WRITE(ring->regs.head, 0);
	I915_WRITE(ring->regs.tail, 0);

	/* Initialize the ring. */
	I915_WRITE(ring->regs.start, obj_priv->gtt_offset);
	head = ring->get_head(dev, ring);

	/* G45 ring initialization fails to reset head to zero */
	if (head != 0) {
		DRM_ERROR("%s head not reset to zero "
				"ctl %08x head %08x tail %08x start %08x\n",
				ring->name,
				I915_READ(ring->regs.ctl),
				I915_READ(ring->regs.head),
				I915_READ(ring->regs.tail),
				I915_READ(ring->regs.start));

		I915_WRITE(ring->regs.head, 0);

		DRM_ERROR("%s head forced to zero "
				"ctl %08x head %08x tail %08x start %08x\n",
				ring->name,
				I915_READ(ring->regs.ctl),
				I915_READ(ring->regs.head),
				I915_READ(ring->regs.tail),
				I915_READ(ring->regs.start));
	}

	I915_WRITE(ring->regs.ctl,
			((ring->gem_object->size - PAGE_SIZE) & RING_NR_PAGES)
			| RING_NO_REPORT | RING_VALID);

	head = I915_READ(ring->regs.head) & HEAD_ADDR;
	/* If the head is still not zero, the ring is dead */
	if (head != 0) {
		DRM_ERROR("%s initialization failed "
				"ctl %08x head %08x tail %08x start %08x\n",
				ring->name,
				I915_READ(ring->regs.ctl),
				I915_READ(ring->regs.head),
				I915_READ(ring->regs.tail),
				I915_READ(ring->regs.start));
		return -EIO;
	}

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_kernel_lost_context(dev);
	else {
		ring->head = ring->get_head(dev, ring);
		ring->tail = ring->get_tail(dev, ring);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->size;
	}
	return 0;
}

static int init_render_ring(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = init_ring_common(dev, ring);
	int mode;

	if (IS_I9XX(dev) && !IS_GEN3(dev)) {
		mode = VS_TIMER_DISPATCH << 16 | VS_TIMER_DISPATCH;
		if (IS_GEN6(dev))
			mode |= MI_FLUSH_ENABLE << 16 | MI_FLUSH_ENABLE;
		I915_WRITE(MI_MODE, mode);
	}
	return ret;
}

#define PIPE_CONTROL_FLUSH(addr)					\
do {									\
	OUT_RING(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_QW_WRITE |		\
		 PIPE_CONTROL_DEPTH_STALL | 2);				\
	OUT_RING(addr | PIPE_CONTROL_GLOBAL_GTT);			\
	OUT_RING(0);							\
	OUT_RING(0);							\
} while (0)

/**
 * Creates a new sequence number, emitting a write of it to the status page
 * plus an interrupt, which will trigger i915_user_interrupt_handler.
 *
 * Must be called with struct_lock held.
 *
 * Returned sequence numbers are nonzero on success.
 */
static u32
render_ring_add_request(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		struct drm_file *file_priv,
		u32 flush_domains)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 seqno;

	seqno = i915_gem_get_seqno(dev);

	if (IS_GEN6(dev)) {
		BEGIN_LP_RING(6);
		OUT_RING(GFX_OP_PIPE_CONTROL | 3);
		OUT_RING(PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_WC_FLUSH | PIPE_CONTROL_IS_FLUSH |
			 PIPE_CONTROL_NOTIFY);
		OUT_RING(dev_priv->seqno_gfx_addr | PIPE_CONTROL_GLOBAL_GTT);
		OUT_RING(seqno);
		OUT_RING(0);
		OUT_RING(0);
		ADVANCE_LP_RING();
	} else if (HAS_PIPE_CONTROL(dev)) {
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

static u32
render_ring_get_gem_seqno(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	if (HAS_PIPE_CONTROL(dev))
		return ((volatile u32 *)(dev_priv->seqno_page))[0];
	else
		return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static void
render_ring_get_user_irq(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (dev->irq_enabled && (++ring->user_irq_refcount == 1)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_enable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_enable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

static void
render_ring_put_user_irq(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	BUG_ON(dev->irq_enabled && ring->user_irq_refcount <= 0);
	if (dev->irq_enabled && (--ring->user_irq_refcount == 0)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_disable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_disable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

static void render_setup_status_page(struct drm_device *dev,
	struct	intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	if (IS_GEN6(dev)) {
		I915_WRITE(HWS_PGA_GEN6, ring->status_page.gfx_addr);
		I915_READ(HWS_PGA_GEN6); /* posting read */
	} else {
		I915_WRITE(HWS_PGA, ring->status_page.gfx_addr);
		I915_READ(HWS_PGA); /* posting read */
	}

}

void
bsd_ring_flush(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		u32     invalidate_domains,
		u32     flush_domains)
{
	intel_ring_begin(dev, ring, 2);
	intel_ring_emit(dev, ring, MI_FLUSH);
	intel_ring_emit(dev, ring, MI_NOOP);
	intel_ring_advance(dev, ring);
}

static inline unsigned int bsd_ring_get_head(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return I915_READ(BSD_RING_HEAD) & HEAD_ADDR;
}

static inline unsigned int bsd_ring_get_tail(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return I915_READ(BSD_RING_TAIL) & TAIL_ADDR;
}

static inline unsigned int bsd_ring_get_active_head(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	return I915_READ(BSD_RING_ACTHD);
}

static inline void bsd_ring_advance_ring(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	I915_WRITE(BSD_RING_TAIL, ring->tail);
}

static int init_bsd_ring(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	return init_ring_common(dev, ring);
}

static u32
bsd_ring_add_request(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		struct drm_file *file_priv,
		u32 flush_domains)
{
	u32 seqno;

	seqno = i915_gem_get_seqno(dev);

	intel_ring_begin(dev, ring, 4);
	intel_ring_emit(dev, ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(dev, ring,
			I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(dev, ring, seqno);
	intel_ring_emit(dev, ring, MI_USER_INTERRUPT);
	intel_ring_advance(dev, ring);

	DRM_DEBUG_DRIVER("%s %d\n", ring->name, seqno);

	return seqno;
}

static void bsd_setup_status_page(struct drm_device *dev,
		struct  intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	I915_WRITE(BSD_HWS_PGA, ring->status_page.gfx_addr);
	I915_READ(BSD_HWS_PGA);
}

static void
bsd_ring_get_user_irq(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	/* do nothing */
}
static void
bsd_ring_put_user_irq(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	/* do nothing */
}

static u32
bsd_ring_get_gem_seqno(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static int
bsd_ring_dispatch_gem_execbuffer(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		struct drm_i915_gem_execbuffer2 *exec,
		struct drm_clip_rect *cliprects,
		uint64_t exec_offset)
{
	uint32_t exec_start;
	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	intel_ring_begin(dev, ring, 2);
	intel_ring_emit(dev, ring, MI_BATCH_BUFFER_START |
			(2 << 6) | MI_BATCH_NON_SECURE_I965);
	intel_ring_emit(dev, ring, exec_start);
	intel_ring_advance(dev, ring);
	return 0;
}


static int
render_ring_dispatch_gem_execbuffer(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		struct drm_i915_gem_execbuffer2 *exec,
		struct drm_clip_rect *cliprects,
		uint64_t exec_offset)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int nbox = exec->num_cliprects;
	int i = 0, count;
	uint32_t exec_start, exec_len;
	exec_start = (uint32_t) exec_offset + exec->batch_start_offset;
	exec_len = (uint32_t) exec->batch_len;

	trace_i915_gem_request_submit(dev, dev_priv->next_seqno + 1);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, cliprects, i,
						exec->DR1, exec->DR4);
			if (ret)
				return ret;
		}

		if (IS_I830(dev) || IS_845G(dev)) {
			intel_ring_begin(dev, ring, 4);
			intel_ring_emit(dev, ring, MI_BATCH_BUFFER);
			intel_ring_emit(dev, ring,
					exec_start | MI_BATCH_NON_SECURE);
			intel_ring_emit(dev, ring, exec_start + exec_len - 4);
			intel_ring_emit(dev, ring, 0);
		} else {
			intel_ring_begin(dev, ring, 4);
			if (IS_I965G(dev)) {
				intel_ring_emit(dev, ring,
						MI_BATCH_BUFFER_START | (2 << 6)
						| MI_BATCH_NON_SECURE_I965);
				intel_ring_emit(dev, ring, exec_start);
			} else {
				intel_ring_emit(dev, ring, MI_BATCH_BUFFER_START
						| (2 << 6));
				intel_ring_emit(dev, ring, exec_start |
						MI_BATCH_NON_SECURE);
			}
		}
		intel_ring_advance(dev, ring);
	}

	if (IS_G4X(dev) || IS_IRONLAKE(dev)) {
		intel_ring_begin(dev, ring, 2);
		intel_ring_emit(dev, ring, MI_FLUSH |
				MI_NO_WRITE_FLUSH |
				MI_INVALIDATE_ISP );
		intel_ring_emit(dev, ring, MI_NOOP);
		intel_ring_advance(dev, ring);
	}
	/* XXX breadcrumb */

	return 0;
}

static void cleanup_status_page(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	obj = ring->status_page.obj;
	if (obj == NULL)
		return;
	obj_priv = to_intel_bo(obj);

	kunmap(obj_priv->pages[0]);
	i915_gem_object_unpin(obj);
	drm_gem_object_unreference(obj);
	ring->status_page.obj = NULL;

	memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
}

static int init_status_page(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

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
		goto err_unref;
	}

	ring->status_page.gfx_addr = obj_priv->gtt_offset;
	ring->status_page.page_addr = kmap(obj_priv->pages[0]);
	if (ring->status_page.page_addr == NULL) {
		memset(&dev_priv->hws_map, 0, sizeof(dev_priv->hws_map));
		goto err_unpin;
	}
	ring->status_page.obj = obj;
	memset(ring->status_page.page_addr, 0, PAGE_SIZE);

	ring->setup_status_page(dev, ring);
	DRM_DEBUG_DRIVER("%s hws offset: 0x%08x\n",
			ring->name, ring->status_page.gfx_addr);

	return 0;

err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(obj);
err:
	return ret;
}


int intel_init_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_object *obj_priv;
	struct drm_gem_object *obj;
	int ret;

	ring->dev = dev;

	if (I915_NEED_GFX_HWS(dev)) {
		ret = init_status_page(dev, ring);
		if (ret)
			return ret;
	}

	obj = i915_gem_alloc_object(dev, ring->size);
	if (obj == NULL) {
		DRM_ERROR("Failed to allocate ringbuffer\n");
		ret = -ENOMEM;
		goto err_hws;
	}

	ring->gem_object = obj;

	ret = i915_gem_object_pin(obj, ring->alignment);
	if (ret)
		goto err_unref;

	obj_priv = to_intel_bo(obj);
	ring->map.size = ring->size;
	ring->map.offset = dev->agp->base + obj_priv->gtt_offset;
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
	ret = ring->init(dev, ring);
	if (ret)
		goto err_unmap;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_kernel_lost_context(dev);
	else {
		ring->head = ring->get_head(dev, ring);
		ring->tail = ring->get_tail(dev, ring);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->size;
	}
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	return ret;

err_unmap:
	drm_core_ioremapfree(&ring->map, dev);
err_unpin:
	i915_gem_object_unpin(obj);
err_unref:
	drm_gem_object_unreference(obj);
	ring->gem_object = NULL;
err_hws:
	cleanup_status_page(dev, ring);
	return ret;
}

void intel_cleanup_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	if (ring->gem_object == NULL)
		return;

	drm_core_ioremapfree(&ring->map, dev);

	i915_gem_object_unpin(ring->gem_object);
	drm_gem_object_unreference(ring->gem_object);
	ring->gem_object = NULL;
	cleanup_status_page(dev, ring);
}

int intel_wrap_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	unsigned int *virt;
	int rem;
	rem = ring->size - ring->tail;

	if (ring->space < rem) {
		int ret = intel_wait_ring_buffer(dev, ring, rem);
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

int intel_wait_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring, int n)
{
	unsigned long end;

	trace_i915_ring_wait_begin (dev);
	end = jiffies + 3 * HZ;
	do {
		ring->head = ring->get_head(dev, ring);
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->size;
		if (ring->space >= n) {
			trace_i915_ring_wait_end (dev);
			return 0;
		}

		if (dev->primary->master) {
			struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
		}

		yield();
	} while (!time_after(jiffies, end));
	trace_i915_ring_wait_end (dev);
	return -EBUSY;
}

void intel_ring_begin(struct drm_device *dev,
		struct intel_ring_buffer *ring, int num_dwords)
{
	int n = 4*num_dwords;
	if (unlikely(ring->tail + n > ring->size))
		intel_wrap_ring_buffer(dev, ring);
	if (unlikely(ring->space < n))
		intel_wait_ring_buffer(dev, ring, n);

	ring->space -= n;
}

void intel_ring_advance(struct drm_device *dev,
		struct intel_ring_buffer *ring)
{
	ring->tail &= ring->size - 1;
	ring->advance_ring(dev, ring);
}

void intel_fill_struct(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		void *data,
		unsigned int len)
{
	unsigned int *virt = ring->virtual_start + ring->tail;
	BUG_ON((len&~(4-1)) != 0);
	intel_ring_begin(dev, ring, len/4);
	memcpy(virt, data, len);
	ring->tail += len;
	ring->tail &= ring->size - 1;
	ring->space -= len;
	intel_ring_advance(dev, ring);
}

struct intel_ring_buffer render_ring = {
	.name			= "render ring",
	.regs                   = {
		.ctl = PRB0_CTL,
		.head = PRB0_HEAD,
		.tail = PRB0_TAIL,
		.start = PRB0_START
	},
	.ring_flag		= I915_EXEC_RENDER,
	.size			= 32 * PAGE_SIZE,
	.alignment		= PAGE_SIZE,
	.virtual_start		= NULL,
	.dev			= NULL,
	.gem_object		= NULL,
	.head			= 0,
	.tail			= 0,
	.space			= 0,
	.user_irq_refcount	= 0,
	.irq_gem_seqno		= 0,
	.waiting_gem_seqno	= 0,
	.setup_status_page	= render_setup_status_page,
	.init			= init_render_ring,
	.get_head		= render_ring_get_head,
	.get_tail		= render_ring_get_tail,
	.get_active_head	= render_ring_get_active_head,
	.advance_ring		= render_ring_advance_ring,
	.flush			= render_ring_flush,
	.add_request		= render_ring_add_request,
	.get_gem_seqno		= render_ring_get_gem_seqno,
	.user_irq_get		= render_ring_get_user_irq,
	.user_irq_put		= render_ring_put_user_irq,
	.dispatch_gem_execbuffer = render_ring_dispatch_gem_execbuffer,
	.status_page		= {NULL, 0, NULL},
	.map			= {0,}
};

/* ring buffer for bit-stream decoder */

struct intel_ring_buffer bsd_ring = {
	.name                   = "bsd ring",
	.regs			= {
		.ctl = BSD_RING_CTL,
		.head = BSD_RING_HEAD,
		.tail = BSD_RING_TAIL,
		.start = BSD_RING_START
	},
	.ring_flag		= I915_EXEC_BSD,
	.size			= 32 * PAGE_SIZE,
	.alignment		= PAGE_SIZE,
	.virtual_start		= NULL,
	.dev			= NULL,
	.gem_object		= NULL,
	.head			= 0,
	.tail			= 0,
	.space			= 0,
	.user_irq_refcount	= 0,
	.irq_gem_seqno		= 0,
	.waiting_gem_seqno	= 0,
	.setup_status_page	= bsd_setup_status_page,
	.init			= init_bsd_ring,
	.get_head		= bsd_ring_get_head,
	.get_tail		= bsd_ring_get_tail,
	.get_active_head	= bsd_ring_get_active_head,
	.advance_ring		= bsd_ring_advance_ring,
	.flush			= bsd_ring_flush,
	.add_request		= bsd_ring_add_request,
	.get_gem_seqno		= bsd_ring_get_gem_seqno,
	.user_irq_get		= bsd_ring_get_user_irq,
	.user_irq_put		= bsd_ring_put_user_irq,
	.dispatch_gem_execbuffer = bsd_ring_dispatch_gem_execbuffer,
	.status_page		= {NULL, 0, NULL},
	.map			= {0,}
};
