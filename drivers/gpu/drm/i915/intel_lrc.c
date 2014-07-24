/*
 * Copyright © 2014 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Michel Thierry <michel.thierry@intel.com>
 *    Thomas Daniel <thomas.daniel@intel.com>
 *    Oscar Mateo <oscar.mateo@intel.com>
 *
 */

/*
 * GEN8 brings an expansion of the HW contexts: "Logical Ring Contexts".
 * These expanded contexts enable a number of new abilities, especially
 * "Execlists" (also implemented in this file).
 *
 * Execlists are the new method by which, on gen8+ hardware, workloads are
 * submitted for execution (as opposed to the legacy, ringbuffer-based, method).
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

#define GEN8_LR_CONTEXT_RENDER_SIZE (20 * PAGE_SIZE)
#define GEN8_LR_CONTEXT_OTHER_SIZE (2 * PAGE_SIZE)

#define GEN8_LR_CONTEXT_ALIGN 4096

#define RING_ELSP(ring)			((ring)->mmio_base+0x230)
#define RING_CONTEXT_CONTROL(ring)	((ring)->mmio_base+0x244)

#define CTX_LRI_HEADER_0		0x01
#define CTX_CONTEXT_CONTROL		0x02
#define CTX_RING_HEAD			0x04
#define CTX_RING_TAIL			0x06
#define CTX_RING_BUFFER_START		0x08
#define CTX_RING_BUFFER_CONTROL		0x0a
#define CTX_BB_HEAD_U			0x0c
#define CTX_BB_HEAD_L			0x0e
#define CTX_BB_STATE			0x10
#define CTX_SECOND_BB_HEAD_U		0x12
#define CTX_SECOND_BB_HEAD_L		0x14
#define CTX_SECOND_BB_STATE		0x16
#define CTX_BB_PER_CTX_PTR		0x18
#define CTX_RCS_INDIRECT_CTX		0x1a
#define CTX_RCS_INDIRECT_CTX_OFFSET	0x1c
#define CTX_LRI_HEADER_1		0x21
#define CTX_CTX_TIMESTAMP		0x22
#define CTX_PDP3_UDW			0x24
#define CTX_PDP3_LDW			0x26
#define CTX_PDP2_UDW			0x28
#define CTX_PDP2_LDW			0x2a
#define CTX_PDP1_UDW			0x2c
#define CTX_PDP1_LDW			0x2e
#define CTX_PDP0_UDW			0x30
#define CTX_PDP0_LDW			0x32
#define CTX_LRI_HEADER_2		0x41
#define CTX_R_PWR_CLK_STATE		0x42
#define CTX_GPGPU_CSR_BASE_ADDRESS	0x44

int intel_sanitize_enable_execlists(struct drm_device *dev, int enable_execlists)
{
	WARN_ON(i915.enable_ppgtt == -1);

	if (enable_execlists == 0)
		return 0;

	if (HAS_LOGICAL_RING_CONTEXTS(dev) && USES_PPGTT(dev) &&
	    i915.use_mmio_flip >= 0)
		return 1;

	return 0;
}

static int logical_ring_invalidate_all_caches(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	uint32_t flush_domains;
	int ret;

	flush_domains = 0;
	if (ring->gpu_caches_dirty)
		flush_domains = I915_GEM_GPU_DOMAINS;

	ret = ring->emit_flush(ringbuf, I915_GEM_GPU_DOMAINS, flush_domains);
	if (ret)
		return ret;

	ring->gpu_caches_dirty = false;
	return 0;
}

static int execlists_move_to_gpu(struct intel_ringbuffer *ringbuf,
				 struct list_head *vmas)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct i915_vma *vma;
	uint32_t flush_domains = 0;
	bool flush_chipset = false;
	int ret;

	list_for_each_entry(vma, vmas, exec_list) {
		struct drm_i915_gem_object *obj = vma->obj;

		ret = i915_gem_object_sync(obj, ring);
		if (ret)
			return ret;

		if (obj->base.write_domain & I915_GEM_DOMAIN_CPU)
			flush_chipset |= i915_gem_clflush_object(obj, false);

		flush_domains |= obj->base.write_domain;
	}

	if (flush_domains & I915_GEM_DOMAIN_GTT)
		wmb();

	/* Unconditionally invalidate gpu caches and ensure that we do flush
	 * any residual writes from the previous batch.
	 */
	return logical_ring_invalidate_all_caches(ringbuf);
}

int intel_execlists_submission(struct drm_device *dev, struct drm_file *file,
			       struct intel_engine_cs *ring,
			       struct intel_context *ctx,
			       struct drm_i915_gem_execbuffer2 *args,
			       struct list_head *vmas,
			       struct drm_i915_gem_object *batch_obj,
			       u64 exec_start, u32 flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ringbuffer *ringbuf = ctx->engine[ring->id].ringbuf;
	int instp_mode;
	u32 instp_mask;
	int ret;

	instp_mode = args->flags & I915_EXEC_CONSTANTS_MASK;
	instp_mask = I915_EXEC_CONSTANTS_MASK;
	switch (instp_mode) {
	case I915_EXEC_CONSTANTS_REL_GENERAL:
	case I915_EXEC_CONSTANTS_ABSOLUTE:
	case I915_EXEC_CONSTANTS_REL_SURFACE:
		if (instp_mode != 0 && ring != &dev_priv->ring[RCS]) {
			DRM_DEBUG("non-0 rel constants mode on non-RCS\n");
			return -EINVAL;
		}

		if (instp_mode != dev_priv->relative_constants_mode) {
			if (instp_mode == I915_EXEC_CONSTANTS_REL_SURFACE) {
				DRM_DEBUG("rel surface constants mode invalid on gen5+\n");
				return -EINVAL;
			}

			/* The HW changed the meaning on this bit on gen6 */
			instp_mask &= ~I915_EXEC_CONSTANTS_REL_SURFACE;
		}
		break;
	default:
		DRM_DEBUG("execbuf with unknown constants: %d\n", instp_mode);
		return -EINVAL;
	}

	if (args->num_cliprects != 0) {
		DRM_DEBUG("clip rectangles are only valid on pre-gen5\n");
		return -EINVAL;
	} else {
		if (args->DR4 == 0xffffffff) {
			DRM_DEBUG("UXA submitting garbage DR4, fixing up\n");
			args->DR4 = 0;
		}

		if (args->DR1 || args->DR4 || args->cliprects_ptr) {
			DRM_DEBUG("0 cliprects but dirt in cliprects fields\n");
			return -EINVAL;
		}
	}

	if (args->flags & I915_EXEC_GEN7_SOL_RESET) {
		DRM_DEBUG("sol reset is gen7 only\n");
		return -EINVAL;
	}

	ret = execlists_move_to_gpu(ringbuf, vmas);
	if (ret)
		return ret;

	if (ring == &dev_priv->ring[RCS] &&
	    instp_mode != dev_priv->relative_constants_mode) {
		ret = intel_logical_ring_begin(ringbuf, 4);
		if (ret)
			return ret;

		intel_logical_ring_emit(ringbuf, MI_NOOP);
		intel_logical_ring_emit(ringbuf, MI_LOAD_REGISTER_IMM(1));
		intel_logical_ring_emit(ringbuf, INSTPM);
		intel_logical_ring_emit(ringbuf, instp_mask << 16 | instp_mode);
		intel_logical_ring_advance(ringbuf);

		dev_priv->relative_constants_mode = instp_mode;
	}

	ret = ring->emit_bb_start(ringbuf, exec_start, flags);
	if (ret)
		return ret;

	i915_gem_execbuffer_move_to_active(vmas, ring);
	i915_gem_execbuffer_retire_commands(dev, file, ring, batch_obj);

	return 0;
}

void intel_logical_ring_stop(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	int ret;

	if (!intel_ring_initialized(ring))
		return;

	ret = intel_ring_idle(ring);
	if (ret && !i915_reset_in_progress(&to_i915(ring->dev)->gpu_error))
		DRM_ERROR("failed to quiesce %s whilst cleaning up: %d\n",
			  ring->name, ret);

	/* TODO: Is this correct with Execlists enabled? */
	I915_WRITE_MODE(ring, _MASKED_BIT_ENABLE(STOP_RING));
	if (wait_for_atomic((I915_READ_MODE(ring) & MODE_IDLE) != 0, 1000)) {
		DRM_ERROR("%s :timed out trying to stop ring\n", ring->name);
		return;
	}
	I915_WRITE_MODE(ring, _MASKED_BIT_DISABLE(STOP_RING));
}

void intel_logical_ring_advance_and_submit(struct intel_ringbuffer *ringbuf)
{
	intel_logical_ring_advance(ringbuf);

	if (intel_ring_stopped(ringbuf->ring))
		return;

	/* TODO: how to submit a context to the ELSP is not here yet */
}

static int logical_ring_alloc_seqno(struct intel_engine_cs *ring)
{
	if (ring->outstanding_lazy_seqno)
		return 0;

	if (ring->preallocated_lazy_request == NULL) {
		struct drm_i915_gem_request *request;

		request = kmalloc(sizeof(*request), GFP_KERNEL);
		if (request == NULL)
			return -ENOMEM;

		ring->preallocated_lazy_request = request;
	}

	return i915_gem_get_seqno(ring->dev, &ring->outstanding_lazy_seqno);
}

static int logical_ring_wait_request(struct intel_ringbuffer *ringbuf,
				     int bytes)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_i915_gem_request *request;
	u32 seqno = 0;
	int ret;

	if (ringbuf->last_retired_head != -1) {
		ringbuf->head = ringbuf->last_retired_head;
		ringbuf->last_retired_head = -1;

		ringbuf->space = intel_ring_space(ringbuf);
		if (ringbuf->space >= bytes)
			return 0;
	}

	list_for_each_entry(request, &ring->request_list, list) {
		if (__intel_ring_space(request->tail, ringbuf->tail,
				       ringbuf->size) >= bytes) {
			seqno = request->seqno;
			break;
		}
	}

	if (seqno == 0)
		return -ENOSPC;

	ret = i915_wait_seqno(ring, seqno);
	if (ret)
		return ret;

	/* TODO: make sure we update the right ringbuffer's last_retired_head
	 * when retiring requests */
	i915_gem_retire_requests_ring(ring);
	ringbuf->head = ringbuf->last_retired_head;
	ringbuf->last_retired_head = -1;

	ringbuf->space = intel_ring_space(ringbuf);
	return 0;
}

static int logical_ring_wait_for_space(struct intel_ringbuffer *ringbuf,
				       int bytes)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long end;
	int ret;

	ret = logical_ring_wait_request(ringbuf, bytes);
	if (ret != -ENOSPC)
		return ret;

	/* Force the context submission in case we have been skipping it */
	intel_logical_ring_advance_and_submit(ringbuf);

	/* With GEM the hangcheck timer should kick us out of the loop,
	 * leaving it early runs the risk of corrupting GEM state (due
	 * to running on almost untested codepaths). But on resume
	 * timers don't work yet, so prevent a complete hang in that
	 * case by choosing an insanely large timeout. */
	end = jiffies + 60 * HZ;

	do {
		ringbuf->head = I915_READ_HEAD(ring);
		ringbuf->space = intel_ring_space(ringbuf);
		if (ringbuf->space >= bytes) {
			ret = 0;
			break;
		}

		msleep(1);

		if (dev_priv->mm.interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		ret = i915_gem_check_wedge(&dev_priv->gpu_error,
					   dev_priv->mm.interruptible);
		if (ret)
			break;

		if (time_after(jiffies, end)) {
			ret = -EBUSY;
			break;
		}
	} while (1);

	return ret;
}

static int logical_ring_wrap_buffer(struct intel_ringbuffer *ringbuf)
{
	uint32_t __iomem *virt;
	int rem = ringbuf->size - ringbuf->tail;

	if (ringbuf->space < rem) {
		int ret = logical_ring_wait_for_space(ringbuf, rem);

		if (ret)
			return ret;
	}

	virt = ringbuf->virtual_start + ringbuf->tail;
	rem /= 4;
	while (rem--)
		iowrite32(MI_NOOP, virt++);

	ringbuf->tail = 0;
	ringbuf->space = intel_ring_space(ringbuf);

	return 0;
}

static int logical_ring_prepare(struct intel_ringbuffer *ringbuf, int bytes)
{
	int ret;

	if (unlikely(ringbuf->tail + bytes > ringbuf->effective_size)) {
		ret = logical_ring_wrap_buffer(ringbuf);
		if (unlikely(ret))
			return ret;
	}

	if (unlikely(ringbuf->space < bytes)) {
		ret = logical_ring_wait_for_space(ringbuf, bytes);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

int intel_logical_ring_begin(struct intel_ringbuffer *ringbuf, int num_dwords)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_check_wedge(&dev_priv->gpu_error,
				   dev_priv->mm.interruptible);
	if (ret)
		return ret;

	ret = logical_ring_prepare(ringbuf, num_dwords * sizeof(uint32_t));
	if (ret)
		return ret;

	/* Preallocate the olr before touching the ring */
	ret = logical_ring_alloc_seqno(ring);
	if (ret)
		return ret;

	ringbuf->space -= num_dwords * sizeof(uint32_t);
	return 0;
}

static int gen8_init_common_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE_IMR(ring, ~(ring->irq_enable_mask | ring->irq_keep_mask));
	I915_WRITE(RING_HWSTAM(ring->mmio_base), 0xffffffff);

	I915_WRITE(RING_MODE_GEN7(ring),
		   _MASKED_BIT_DISABLE(GFX_REPLAY_MODE) |
		   _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE));
	POSTING_READ(RING_MODE_GEN7(ring));
	DRM_DEBUG_DRIVER("Execlists enabled for %s\n", ring->name);

	memset(&ring->hangcheck, 0, sizeof(ring->hangcheck));

	return 0;
}

static int gen8_init_render_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = gen8_init_common_ring(ring);
	if (ret)
		return ret;

	/* We need to disable the AsyncFlip performance optimisations in order
	 * to use MI_WAIT_FOR_EVENT within the CS. It should already be
	 * programmed to '1' on all products.
	 *
	 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv,bdw,chv
	 */
	I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(ASYNC_FLIP_PERF_DISABLE));

	ret = intel_init_pipe_control(ring);
	if (ret)
		return ret;

	I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_FORCE_ORDERING));

	return ret;
}

static int gen8_emit_bb_start(struct intel_ringbuffer *ringbuf,
			      u64 offset, unsigned flags)
{
	bool ppgtt = !(flags & I915_DISPATCH_SECURE);
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 4);
	if (ret)
		return ret;

	/* FIXME(BDW): Address space and security selectors. */
	intel_logical_ring_emit(ringbuf, MI_BATCH_BUFFER_START_GEN8 | (ppgtt<<8));
	intel_logical_ring_emit(ringbuf, lower_32_bits(offset));
	intel_logical_ring_emit(ringbuf, upper_32_bits(offset));
	intel_logical_ring_emit(ringbuf, MI_NOOP);
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static bool gen8_logical_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (!dev->irq_enabled)
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		I915_WRITE_IMR(ring, ~(ring->irq_enable_mask | ring->irq_keep_mask));
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void gen8_logical_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		I915_WRITE_IMR(ring, ~ring->irq_keep_mask);
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static int gen8_emit_flush(struct intel_ringbuffer *ringbuf,
			   u32 invalidate_domains,
			   u32 unused)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t cmd;
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 4);
	if (ret)
		return ret;

	cmd = MI_FLUSH_DW + 1;

	if (ring == &dev_priv->ring[VCS]) {
		if (invalidate_domains & I915_GEM_GPU_DOMAINS)
			cmd |= MI_INVALIDATE_TLB | MI_INVALIDATE_BSD |
				MI_FLUSH_DW_STORE_INDEX |
				MI_FLUSH_DW_OP_STOREDW;
	} else {
		if (invalidate_domains & I915_GEM_DOMAIN_RENDER)
			cmd |= MI_INVALIDATE_TLB | MI_FLUSH_DW_STORE_INDEX |
				MI_FLUSH_DW_OP_STOREDW;
	}

	intel_logical_ring_emit(ringbuf, cmd);
	intel_logical_ring_emit(ringbuf,
				I915_GEM_HWS_SCRATCH_ADDR |
				MI_FLUSH_DW_USE_GTT);
	intel_logical_ring_emit(ringbuf, 0); /* upper addr */
	intel_logical_ring_emit(ringbuf, 0); /* value */
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static int gen8_emit_flush_render(struct intel_ringbuffer *ringbuf,
				  u32 invalidate_domains,
				  u32 flush_domains)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	u32 flags = 0;
	int ret;

	flags |= PIPE_CONTROL_CS_STALL;

	if (flush_domains) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
	}

	if (invalidate_domains) {
		flags |= PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_QW_WRITE;
		flags |= PIPE_CONTROL_GLOBAL_GTT_IVB;
	}

	ret = intel_logical_ring_begin(ringbuf, 6);
	if (ret)
		return ret;

	intel_logical_ring_emit(ringbuf, GFX_OP_PIPE_CONTROL(6));
	intel_logical_ring_emit(ringbuf, flags);
	intel_logical_ring_emit(ringbuf, scratch_addr);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static u32 gen8_get_seqno(struct intel_engine_cs *ring, bool lazy_coherency)
{
	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static void gen8_set_seqno(struct intel_engine_cs *ring, u32 seqno)
{
	intel_write_status_page(ring, I915_GEM_HWS_INDEX, seqno);
}

static int gen8_emit_request(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	u32 cmd;
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 6);
	if (ret)
		return ret;

	cmd = MI_STORE_DWORD_IMM_GEN8;
	cmd |= MI_GLOBAL_GTT;

	intel_logical_ring_emit(ringbuf, cmd);
	intel_logical_ring_emit(ringbuf,
				(ring->status_page.gfx_addr +
				(I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT)));
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, ring->outstanding_lazy_seqno);
	intel_logical_ring_emit(ringbuf, MI_USER_INTERRUPT);
	intel_logical_ring_emit(ringbuf, MI_NOOP);
	intel_logical_ring_advance_and_submit(ringbuf);

	return 0;
}

void intel_logical_ring_cleanup(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

	if (!intel_ring_initialized(ring))
		return;

	intel_logical_ring_stop(ring);
	WARN_ON((I915_READ_MODE(ring) & MODE_IDLE) == 0);
	ring->preallocated_lazy_request = NULL;
	ring->outstanding_lazy_seqno = 0;

	if (ring->cleanup)
		ring->cleanup(ring);

	i915_cmd_parser_fini_ring(ring);

	if (ring->status_page.obj) {
		kunmap(sg_page(ring->status_page.obj->pages->sgl));
		ring->status_page.obj = NULL;
	}
}

static int logical_ring_init(struct drm_device *dev, struct intel_engine_cs *ring)
{
	int ret;
	struct intel_context *dctx = ring->default_context;
	struct drm_i915_gem_object *dctx_obj;

	/* Intentionally left blank. */
	ring->buffer = NULL;

	ring->dev = dev;
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	init_waitqueue_head(&ring->irq_queue);

	ret = intel_lr_context_deferred_create(dctx, ring);
	if (ret)
		return ret;

	/* The status page is offset 0 from the context object in LRCs. */
	dctx_obj = dctx->engine[ring->id].state;
	ring->status_page.gfx_addr = i915_gem_obj_ggtt_offset(dctx_obj);
	ring->status_page.page_addr = kmap(sg_page(dctx_obj->pages->sgl));
	if (ring->status_page.page_addr == NULL)
		return -ENOMEM;
	ring->status_page.obj = dctx_obj;

	ret = i915_cmd_parser_init_ring(ring);
	if (ret)
		return ret;

	if (ring->init) {
		ret = ring->init(ring);
		if (ret)
			return ret;
	}

	return 0;
}

static int logical_render_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[RCS];

	ring->name = "render ring";
	ring->id = RCS;
	ring->mmio_base = RENDER_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_RCS_IRQ_SHIFT;
	if (HAS_L3_DPF(dev))
		ring->irq_keep_mask |= GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	ring->init = gen8_init_render_ring;
	ring->cleanup = intel_fini_pipe_control;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush_render;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_bsd_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS];

	ring->name = "bsd ring";
	ring->id = VCS;
	ring->mmio_base = GEN6_BSD_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VCS1_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS1_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_bsd2_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS2];

	ring->name = "bds2 ring";
	ring->id = VCS2;
	ring->mmio_base = GEN8_BSD2_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VCS2_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS2_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_blt_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[BCS];

	ring->name = "blitter ring";
	ring->id = BCS;
	ring->mmio_base = BLT_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_BCS_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_vebox_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VECS];

	ring->name = "video enhancement ring";
	ring->id = VECS;
	ring->mmio_base = VEBOX_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VECS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VECS_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

int intel_logical_rings_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = logical_render_ring_init(dev);
	if (ret)
		return ret;

	if (HAS_BSD(dev)) {
		ret = logical_bsd_ring_init(dev);
		if (ret)
			goto cleanup_render_ring;
	}

	if (HAS_BLT(dev)) {
		ret = logical_blt_ring_init(dev);
		if (ret)
			goto cleanup_bsd_ring;
	}

	if (HAS_VEBOX(dev)) {
		ret = logical_vebox_ring_init(dev);
		if (ret)
			goto cleanup_blt_ring;
	}

	if (HAS_BSD2(dev)) {
		ret = logical_bsd2_ring_init(dev);
		if (ret)
			goto cleanup_vebox_ring;
	}

	ret = i915_gem_set_seqno(dev, ((u32)~0 - 0x1000));
	if (ret)
		goto cleanup_bsd2_ring;

	return 0;

cleanup_bsd2_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VCS2]);
cleanup_vebox_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VECS]);
cleanup_blt_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[BCS]);
cleanup_bsd_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VCS]);
cleanup_render_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[RCS]);

	return ret;
}

static int
populate_lr_context(struct intel_context *ctx, struct drm_i915_gem_object *ctx_obj,
		    struct intel_engine_cs *ring, struct intel_ringbuffer *ringbuf)
{
	struct drm_i915_gem_object *ring_obj = ringbuf->obj;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt;
	struct page *page;
	uint32_t *reg_state;
	int ret;

	ret = i915_gem_object_set_to_cpu_domain(ctx_obj, true);
	if (ret) {
		DRM_DEBUG_DRIVER("Could not set to CPU domain\n");
		return ret;
	}

	ret = i915_gem_object_get_pages(ctx_obj);
	if (ret) {
		DRM_DEBUG_DRIVER("Could not get object pages\n");
		return ret;
	}

	i915_gem_object_pin_pages(ctx_obj);

	/* The second page of the context object contains some fields which must
	 * be set up prior to the first execution. */
	page = i915_gem_object_get_page(ctx_obj, 1);
	reg_state = kmap_atomic(page);

	/* A context is actually a big batch buffer with several MI_LOAD_REGISTER_IMM
	 * commands followed by (reg, value) pairs. The values we are setting here are
	 * only for the first context restore: on a subsequent save, the GPU will
	 * recreate this batchbuffer with new values (including all the missing
	 * MI_LOAD_REGISTER_IMM commands that we are not initializing here). */
	if (ring->id == RCS)
		reg_state[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(14);
	else
		reg_state[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(11);
	reg_state[CTX_LRI_HEADER_0] |= MI_LRI_FORCE_POSTED;
	reg_state[CTX_CONTEXT_CONTROL] = RING_CONTEXT_CONTROL(ring);
	reg_state[CTX_CONTEXT_CONTROL+1] =
			_MASKED_BIT_ENABLE((1<<3) | MI_RESTORE_INHIBIT);
	reg_state[CTX_RING_HEAD] = RING_HEAD(ring->mmio_base);
	reg_state[CTX_RING_HEAD+1] = 0;
	reg_state[CTX_RING_TAIL] = RING_TAIL(ring->mmio_base);
	reg_state[CTX_RING_TAIL+1] = 0;
	reg_state[CTX_RING_BUFFER_START] = RING_START(ring->mmio_base);
	reg_state[CTX_RING_BUFFER_START+1] = i915_gem_obj_ggtt_offset(ring_obj);
	reg_state[CTX_RING_BUFFER_CONTROL] = RING_CTL(ring->mmio_base);
	reg_state[CTX_RING_BUFFER_CONTROL+1] =
			((ringbuf->size - PAGE_SIZE) & RING_NR_PAGES) | RING_VALID;
	reg_state[CTX_BB_HEAD_U] = ring->mmio_base + 0x168;
	reg_state[CTX_BB_HEAD_U+1] = 0;
	reg_state[CTX_BB_HEAD_L] = ring->mmio_base + 0x140;
	reg_state[CTX_BB_HEAD_L+1] = 0;
	reg_state[CTX_BB_STATE] = ring->mmio_base + 0x110;
	reg_state[CTX_BB_STATE+1] = (1<<5);
	reg_state[CTX_SECOND_BB_HEAD_U] = ring->mmio_base + 0x11c;
	reg_state[CTX_SECOND_BB_HEAD_U+1] = 0;
	reg_state[CTX_SECOND_BB_HEAD_L] = ring->mmio_base + 0x114;
	reg_state[CTX_SECOND_BB_HEAD_L+1] = 0;
	reg_state[CTX_SECOND_BB_STATE] = ring->mmio_base + 0x118;
	reg_state[CTX_SECOND_BB_STATE+1] = 0;
	if (ring->id == RCS) {
		/* TODO: according to BSpec, the register state context
		 * for CHV does not have these. OTOH, these registers do
		 * exist in CHV. I'm waiting for a clarification */
		reg_state[CTX_BB_PER_CTX_PTR] = ring->mmio_base + 0x1c0;
		reg_state[CTX_BB_PER_CTX_PTR+1] = 0;
		reg_state[CTX_RCS_INDIRECT_CTX] = ring->mmio_base + 0x1c4;
		reg_state[CTX_RCS_INDIRECT_CTX+1] = 0;
		reg_state[CTX_RCS_INDIRECT_CTX_OFFSET] = ring->mmio_base + 0x1c8;
		reg_state[CTX_RCS_INDIRECT_CTX_OFFSET+1] = 0;
	}
	reg_state[CTX_LRI_HEADER_1] = MI_LOAD_REGISTER_IMM(9);
	reg_state[CTX_LRI_HEADER_1] |= MI_LRI_FORCE_POSTED;
	reg_state[CTX_CTX_TIMESTAMP] = ring->mmio_base + 0x3a8;
	reg_state[CTX_CTX_TIMESTAMP+1] = 0;
	reg_state[CTX_PDP3_UDW] = GEN8_RING_PDP_UDW(ring, 3);
	reg_state[CTX_PDP3_LDW] = GEN8_RING_PDP_LDW(ring, 3);
	reg_state[CTX_PDP2_UDW] = GEN8_RING_PDP_UDW(ring, 2);
	reg_state[CTX_PDP2_LDW] = GEN8_RING_PDP_LDW(ring, 2);
	reg_state[CTX_PDP1_UDW] = GEN8_RING_PDP_UDW(ring, 1);
	reg_state[CTX_PDP1_LDW] = GEN8_RING_PDP_LDW(ring, 1);
	reg_state[CTX_PDP0_UDW] = GEN8_RING_PDP_UDW(ring, 0);
	reg_state[CTX_PDP0_LDW] = GEN8_RING_PDP_LDW(ring, 0);
	reg_state[CTX_PDP3_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[3]);
	reg_state[CTX_PDP3_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[3]);
	reg_state[CTX_PDP2_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[2]);
	reg_state[CTX_PDP2_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[2]);
	reg_state[CTX_PDP1_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[1]);
	reg_state[CTX_PDP1_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[1]);
	reg_state[CTX_PDP0_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[0]);
	reg_state[CTX_PDP0_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[0]);
	if (ring->id == RCS) {
		reg_state[CTX_LRI_HEADER_2] = MI_LOAD_REGISTER_IMM(1);
		reg_state[CTX_R_PWR_CLK_STATE] = 0x20c8;
		reg_state[CTX_R_PWR_CLK_STATE+1] = 0;
	}

	kunmap_atomic(reg_state);

	ctx_obj->dirty = 1;
	set_page_dirty(page);
	i915_gem_object_unpin_pages(ctx_obj);

	return 0;
}

void intel_lr_context_free(struct intel_context *ctx)
{
	int i;

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct drm_i915_gem_object *ctx_obj = ctx->engine[i].state;
		struct intel_ringbuffer *ringbuf = ctx->engine[i].ringbuf;

		if (ctx_obj) {
			intel_destroy_ringbuffer_obj(ringbuf);
			kfree(ringbuf);
			i915_gem_object_ggtt_unpin(ctx_obj);
			drm_gem_object_unreference(&ctx_obj->base);
		}
	}
}

static uint32_t get_lr_context_size(struct intel_engine_cs *ring)
{
	int ret = 0;

	WARN_ON(INTEL_INFO(ring->dev)->gen != 8);

	switch (ring->id) {
	case RCS:
		ret = GEN8_LR_CONTEXT_RENDER_SIZE;
		break;
	case VCS:
	case BCS:
	case VECS:
	case VCS2:
		ret = GEN8_LR_CONTEXT_OTHER_SIZE;
		break;
	}

	return ret;
}

int intel_lr_context_deferred_create(struct intel_context *ctx,
				     struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_gem_object *ctx_obj;
	uint32_t context_size;
	struct intel_ringbuffer *ringbuf;
	int ret;

	WARN_ON(ctx->legacy_hw_ctx.rcs_state != NULL);
	if (ctx->engine[ring->id].state)
		return 0;

	context_size = round_up(get_lr_context_size(ring), 4096);

	ctx_obj = i915_gem_alloc_context_obj(dev, context_size);
	if (IS_ERR(ctx_obj)) {
		ret = PTR_ERR(ctx_obj);
		DRM_DEBUG_DRIVER("Alloc LRC backing obj failed: %d\n", ret);
		return ret;
	}

	ret = i915_gem_obj_ggtt_pin(ctx_obj, GEN8_LR_CONTEXT_ALIGN, 0);
	if (ret) {
		DRM_DEBUG_DRIVER("Pin LRC backing obj failed: %d\n", ret);
		drm_gem_object_unreference(&ctx_obj->base);
		return ret;
	}

	ringbuf = kzalloc(sizeof(*ringbuf), GFP_KERNEL);
	if (!ringbuf) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer %s\n",
				ring->name);
		i915_gem_object_ggtt_unpin(ctx_obj);
		drm_gem_object_unreference(&ctx_obj->base);
		ret = -ENOMEM;
		return ret;
	}

	ringbuf->ring = ring;
	ringbuf->FIXME_lrc_ctx = ctx;

	ringbuf->size = 32 * PAGE_SIZE;
	ringbuf->effective_size = ringbuf->size;
	ringbuf->head = 0;
	ringbuf->tail = 0;
	ringbuf->space = ringbuf->size;
	ringbuf->last_retired_head = -1;

	/* TODO: For now we put this in the mappable region so that we can reuse
	 * the existing ringbuffer code which ioremaps it. When we start
	 * creating many contexts, this will no longer work and we must switch
	 * to a kmapish interface.
	 */
	ret = intel_alloc_ringbuffer_obj(dev, ringbuf);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer obj %s: %d\n",
				ring->name, ret);
		goto error;
	}

	ret = populate_lr_context(ctx, ctx_obj, ring, ringbuf);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to populate LRC: %d\n", ret);
		intel_destroy_ringbuffer_obj(ringbuf);
		goto error;
	}

	ctx->engine[ring->id].ringbuf = ringbuf;
	ctx->engine[ring->id].state = ctx_obj;

	return 0;

error:
	kfree(ringbuf);
	i915_gem_object_ggtt_unpin(ctx_obj);
	drm_gem_object_unreference(&ctx_obj->base);
	return ret;
}
