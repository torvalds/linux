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

#include <linux/log2.h>
#include <drm/drmP.h>
#include "i915_drv.h"
#include <drm/i915_drm.h>
#include "i915_trace.h"
#include "intel_drv.h"

int __intel_ring_space(int head, int tail, int size)
{
	int space = head - tail;
	if (space <= 0)
		space += size;
	return space - I915_RING_FREE_SPACE;
}

void intel_ring_update_space(struct intel_ringbuffer *ringbuf)
{
	if (ringbuf->last_retired_head != -1) {
		ringbuf->head = ringbuf->last_retired_head;
		ringbuf->last_retired_head = -1;
	}

	ringbuf->space = __intel_ring_space(ringbuf->head & HEAD_ADDR,
					    ringbuf->tail, ringbuf->size);
}

int intel_ring_space(struct intel_ringbuffer *ringbuf)
{
	intel_ring_update_space(ringbuf);
	return ringbuf->space;
}

bool intel_ring_stopped(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	return dev_priv->gpu_error.stop_rings & intel_ring_flag(ring);
}

static void __intel_ring_advance(struct intel_engine_cs *ring)
{
	struct intel_ringbuffer *ringbuf = ring->buffer;
	ringbuf->tail &= ringbuf->size - 1;
	if (intel_ring_stopped(ring))
		return;
	ring->write_tail(ring, ringbuf->tail);
}

static int
gen2_render_ring_flush(struct drm_i915_gem_request *req,
		       u32	invalidate_domains,
		       u32	flush_domains)
{
	struct intel_engine_cs *ring = req->ring;
	u32 cmd;
	int ret;

	cmd = MI_FLUSH;
	if (((invalidate_domains|flush_domains) & I915_GEM_DOMAIN_RENDER) == 0)
		cmd |= MI_NO_WRITE_FLUSH;

	if (invalidate_domains & I915_GEM_DOMAIN_SAMPLER)
		cmd |= MI_READ_FLUSH;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, cmd);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

static int
gen4_render_ring_flush(struct drm_i915_gem_request *req,
		       u32	invalidate_domains,
		       u32	flush_domains)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_device *dev = ring->dev;
	u32 cmd;
	int ret;

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
	if ((invalidate_domains|flush_domains) & I915_GEM_DOMAIN_RENDER)
		cmd &= ~MI_NO_WRITE_FLUSH;
	if (invalidate_domains & I915_GEM_DOMAIN_INSTRUCTION)
		cmd |= MI_EXE_FLUSH;

	if (invalidate_domains & I915_GEM_DOMAIN_COMMAND &&
	    (IS_G4X(dev) || IS_GEN5(dev)))
		cmd |= MI_INVALIDATE_ISP;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, cmd);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

/**
 * Emits a PIPE_CONTROL with a non-zero post-sync operation, for
 * implementing two workarounds on gen6.  From section 1.4.7.1
 * "PIPE_CONTROL" of the Sandy Bridge PRM volume 2 part 1:
 *
 * [DevSNB-C+{W/A}] Before any depth stall flush (including those
 * produced by non-pipelined state commands), software needs to first
 * send a PIPE_CONTROL with no bits set except Post-Sync Operation !=
 * 0.
 *
 * [Dev-SNB{W/A}]: Before a PIPE_CONTROL with Write Cache Flush Enable
 * =1, a PIPE_CONTROL with any non-zero post-sync-op is required.
 *
 * And the workaround for these two requires this workaround first:
 *
 * [Dev-SNB{W/A}]: Pipe-control with CS-stall bit set must be sent
 * BEFORE the pipe-control with a post-sync op and no write-cache
 * flushes.
 *
 * And this last workaround is tricky because of the requirements on
 * that bit.  From section 1.4.7.2.3 "Stall" of the Sandy Bridge PRM
 * volume 2 part 1:
 *
 *     "1 of the following must also be set:
 *      - Render Target Cache Flush Enable ([12] of DW1)
 *      - Depth Cache Flush Enable ([0] of DW1)
 *      - Stall at Pixel Scoreboard ([1] of DW1)
 *      - Depth Stall ([13] of DW1)
 *      - Post-Sync Operation ([13] of DW1)
 *      - Notify Enable ([8] of DW1)"
 *
 * The cache flushes require the workaround flush that triggered this
 * one, so we can't use it.  Depth stall would trigger the same.
 * Post-sync nonzero is what triggered this second workaround, so we
 * can't use that one either.  Notify enable is IRQs, which aren't
 * really our business.  That leaves only stall at scoreboard.
 */
static int
intel_emit_post_sync_nonzero_flush(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	int ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(5));
	intel_ring_emit(ring, PIPE_CONTROL_CS_STALL |
			PIPE_CONTROL_STALL_AT_SCOREBOARD);
	intel_ring_emit(ring, scratch_addr | PIPE_CONTROL_GLOBAL_GTT); /* address */
	intel_ring_emit(ring, 0); /* low dword */
	intel_ring_emit(ring, 0); /* high dword */
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(5));
	intel_ring_emit(ring, PIPE_CONTROL_QW_WRITE);
	intel_ring_emit(ring, scratch_addr | PIPE_CONTROL_GLOBAL_GTT); /* address */
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

static int
gen6_render_ring_flush(struct drm_i915_gem_request *req,
		       u32 invalidate_domains, u32 flush_domains)
{
	struct intel_engine_cs *ring = req->ring;
	u32 flags = 0;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	int ret;

	/* Force SNB workarounds for PIPE_CONTROL flushes */
	ret = intel_emit_post_sync_nonzero_flush(req);
	if (ret)
		return ret;

	/* Just flush everything.  Experiments have shown that reducing the
	 * number of bits based on the write domains has little performance
	 * impact.
	 */
	if (flush_domains) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		/*
		 * Ensure that any following seqno writes only happen
		 * when the render cache is indeed flushed.
		 */
		flags |= PIPE_CONTROL_CS_STALL;
	}
	if (invalidate_domains) {
		flags |= PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		/*
		 * TLB invalidate requires a post-sync write.
		 */
		flags |= PIPE_CONTROL_QW_WRITE | PIPE_CONTROL_CS_STALL;
	}

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(4));
	intel_ring_emit(ring, flags);
	intel_ring_emit(ring, scratch_addr | PIPE_CONTROL_GLOBAL_GTT);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);

	return 0;
}

static int
gen7_render_ring_cs_stall_wa(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(4));
	intel_ring_emit(ring, PIPE_CONTROL_CS_STALL |
			      PIPE_CONTROL_STALL_AT_SCOREBOARD);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);

	return 0;
}

static int
gen7_render_ring_flush(struct drm_i915_gem_request *req,
		       u32 invalidate_domains, u32 flush_domains)
{
	struct intel_engine_cs *ring = req->ring;
	u32 flags = 0;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	int ret;

	/*
	 * Ensure that any following seqno writes only happen when the render
	 * cache is indeed flushed.
	 *
	 * Workaround: 4th PIPE_CONTROL command (except the ones with only
	 * read-cache invalidate bits set) must have the CS_STALL bit set. We
	 * don't try to be clever and just set it unconditionally.
	 */
	flags |= PIPE_CONTROL_CS_STALL;

	/* Just flush everything.  Experiments have shown that reducing the
	 * number of bits based on the write domains has little performance
	 * impact.
	 */
	if (flush_domains) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DC_FLUSH_ENABLE;
		flags |= PIPE_CONTROL_FLUSH_ENABLE;
	}
	if (invalidate_domains) {
		flags |= PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_MEDIA_STATE_CLEAR;
		/*
		 * TLB invalidate requires a post-sync write.
		 */
		flags |= PIPE_CONTROL_QW_WRITE;
		flags |= PIPE_CONTROL_GLOBAL_GTT_IVB;

		flags |= PIPE_CONTROL_STALL_AT_SCOREBOARD;

		/* Workaround: we must issue a pipe_control with CS-stall bit
		 * set before a pipe_control command that has the state cache
		 * invalidate bit set. */
		gen7_render_ring_cs_stall_wa(req);
	}

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(4));
	intel_ring_emit(ring, flags);
	intel_ring_emit(ring, scratch_addr);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);

	return 0;
}

static int
gen8_emit_pipe_control(struct drm_i915_gem_request *req,
		       u32 flags, u32 scratch_addr)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(6));
	intel_ring_emit(ring, flags);
	intel_ring_emit(ring, scratch_addr);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_emit(ring, 0);
	intel_ring_advance(ring);

	return 0;
}

static int
gen8_render_ring_flush(struct drm_i915_gem_request *req,
		       u32 invalidate_domains, u32 flush_domains)
{
	u32 flags = 0;
	u32 scratch_addr = req->ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	int ret;

	flags |= PIPE_CONTROL_CS_STALL;

	if (flush_domains) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DC_FLUSH_ENABLE;
		flags |= PIPE_CONTROL_FLUSH_ENABLE;
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

		/* WaCsStallBeforeStateCacheInvalidate:bdw,chv */
		ret = gen8_emit_pipe_control(req,
					     PIPE_CONTROL_CS_STALL |
					     PIPE_CONTROL_STALL_AT_SCOREBOARD,
					     0);
		if (ret)
			return ret;
	}

	return gen8_emit_pipe_control(req, flags, scratch_addr);
}

static void ring_write_tail(struct intel_engine_cs *ring,
			    u32 value)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	I915_WRITE_TAIL(ring, value);
}

u64 intel_ring_get_active_head(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u64 acthd;

	if (INTEL_INFO(ring->dev)->gen >= 8)
		acthd = I915_READ64_2x32(RING_ACTHD(ring->mmio_base),
					 RING_ACTHD_UDW(ring->mmio_base));
	else if (INTEL_INFO(ring->dev)->gen >= 4)
		acthd = I915_READ(RING_ACTHD(ring->mmio_base));
	else
		acthd = I915_READ(ACTHD);

	return acthd;
}

static void ring_setup_phys_status_page(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u32 addr;

	addr = dev_priv->status_page_dmah->busaddr;
	if (INTEL_INFO(ring->dev)->gen >= 4)
		addr |= (dev_priv->status_page_dmah->busaddr >> 28) & 0xf0;
	I915_WRITE(HWS_PGA, addr);
}

static void intel_ring_setup_status_page(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	i915_reg_t mmio;

	/* The ring status page addresses are no longer next to the rest of
	 * the ring registers as of gen7.
	 */
	if (IS_GEN7(dev)) {
		switch (ring->id) {
		case RCS:
			mmio = RENDER_HWS_PGA_GEN7;
			break;
		case BCS:
			mmio = BLT_HWS_PGA_GEN7;
			break;
		/*
		 * VCS2 actually doesn't exist on Gen7. Only shut up
		 * gcc switch check warning
		 */
		case VCS2:
		case VCS:
			mmio = BSD_HWS_PGA_GEN7;
			break;
		case VECS:
			mmio = VEBOX_HWS_PGA_GEN7;
			break;
		}
	} else if (IS_GEN6(ring->dev)) {
		mmio = RING_HWS_PGA_GEN6(ring->mmio_base);
	} else {
		/* XXX: gen8 returns to sanity */
		mmio = RING_HWS_PGA(ring->mmio_base);
	}

	I915_WRITE(mmio, (u32)ring->status_page.gfx_addr);
	POSTING_READ(mmio);

	/*
	 * Flush the TLB for this page
	 *
	 * FIXME: These two bits have disappeared on gen8, so a question
	 * arises: do we still need this and if so how should we go about
	 * invalidating the TLB?
	 */
	if (INTEL_INFO(dev)->gen >= 6 && INTEL_INFO(dev)->gen < 8) {
		i915_reg_t reg = RING_INSTPM(ring->mmio_base);

		/* ring should be idle before issuing a sync flush*/
		WARN_ON((I915_READ_MODE(ring) & MODE_IDLE) == 0);

		I915_WRITE(reg,
			   _MASKED_BIT_ENABLE(INSTPM_TLB_INVALIDATE |
					      INSTPM_SYNC_FLUSH));
		if (wait_for((I915_READ(reg) & INSTPM_SYNC_FLUSH) == 0,
			     1000))
			DRM_ERROR("%s: wait for SyncFlush to complete for TLB invalidation timed out\n",
				  ring->name);
	}
}

static bool stop_ring(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = to_i915(ring->dev);

	if (!IS_GEN2(ring->dev)) {
		I915_WRITE_MODE(ring, _MASKED_BIT_ENABLE(STOP_RING));
		if (wait_for((I915_READ_MODE(ring) & MODE_IDLE) != 0, 1000)) {
			DRM_ERROR("%s : timed out trying to stop ring\n", ring->name);
			/* Sometimes we observe that the idle flag is not
			 * set even though the ring is empty. So double
			 * check before giving up.
			 */
			if (I915_READ_HEAD(ring) != I915_READ_TAIL(ring))
				return false;
		}
	}

	I915_WRITE_CTL(ring, 0);
	I915_WRITE_HEAD(ring, 0);
	ring->write_tail(ring, 0);

	if (!IS_GEN2(ring->dev)) {
		(void)I915_READ_CTL(ring);
		I915_WRITE_MODE(ring, _MASKED_BIT_DISABLE(STOP_RING));
	}

	return (I915_READ_HEAD(ring) & HEAD_ADDR) == 0;
}

static int init_ring_common(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ringbuffer *ringbuf = ring->buffer;
	struct drm_i915_gem_object *obj = ringbuf->obj;
	int ret = 0;

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	if (!stop_ring(ring)) {
		/* G45 ring initialization often fails to reset head to zero */
		DRM_DEBUG_KMS("%s head not reset to zero "
			      "ctl %08x head %08x tail %08x start %08x\n",
			      ring->name,
			      I915_READ_CTL(ring),
			      I915_READ_HEAD(ring),
			      I915_READ_TAIL(ring),
			      I915_READ_START(ring));

		if (!stop_ring(ring)) {
			DRM_ERROR("failed to set %s head to zero "
				  "ctl %08x head %08x tail %08x start %08x\n",
				  ring->name,
				  I915_READ_CTL(ring),
				  I915_READ_HEAD(ring),
				  I915_READ_TAIL(ring),
				  I915_READ_START(ring));
			ret = -EIO;
			goto out;
		}
	}

	if (I915_NEED_GFX_HWS(dev))
		intel_ring_setup_status_page(ring);
	else
		ring_setup_phys_status_page(ring);

	/* Enforce ordering by reading HEAD register back */
	I915_READ_HEAD(ring);

	/* Initialize the ring. This must happen _after_ we've cleared the ring
	 * registers with the above sequence (the readback of the HEAD registers
	 * also enforces ordering), otherwise the hw might lose the new ring
	 * register values. */
	I915_WRITE_START(ring, i915_gem_obj_ggtt_offset(obj));

	/* WaClearRingBufHeadRegAtInit:ctg,elk */
	if (I915_READ_HEAD(ring))
		DRM_DEBUG("%s initialization failed [head=%08x], fudging\n",
			  ring->name, I915_READ_HEAD(ring));
	I915_WRITE_HEAD(ring, 0);
	(void)I915_READ_HEAD(ring);

	I915_WRITE_CTL(ring,
			((ringbuf->size - PAGE_SIZE) & RING_NR_PAGES)
			| RING_VALID);

	/* If the head is still not zero, the ring is dead */
	if (wait_for((I915_READ_CTL(ring) & RING_VALID) != 0 &&
		     I915_READ_START(ring) == i915_gem_obj_ggtt_offset(obj) &&
		     (I915_READ_HEAD(ring) & HEAD_ADDR) == 0, 50)) {
		DRM_ERROR("%s initialization failed "
			  "ctl %08x (valid? %d) head %08x tail %08x start %08x [expected %08lx]\n",
			  ring->name,
			  I915_READ_CTL(ring), I915_READ_CTL(ring) & RING_VALID,
			  I915_READ_HEAD(ring), I915_READ_TAIL(ring),
			  I915_READ_START(ring), (unsigned long)i915_gem_obj_ggtt_offset(obj));
		ret = -EIO;
		goto out;
	}

	ringbuf->last_retired_head = -1;
	ringbuf->head = I915_READ_HEAD(ring);
	ringbuf->tail = I915_READ_TAIL(ring) & TAIL_ADDR;
	intel_ring_update_space(ringbuf);

	memset(&ring->hangcheck, 0, sizeof(ring->hangcheck));

out:
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

void
intel_fini_pipe_control(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;

	if (ring->scratch.obj == NULL)
		return;

	if (INTEL_INFO(dev)->gen >= 5) {
		kunmap(sg_page(ring->scratch.obj->pages->sgl));
		i915_gem_object_ggtt_unpin(ring->scratch.obj);
	}

	drm_gem_object_unreference(&ring->scratch.obj->base);
	ring->scratch.obj = NULL;
}

int
intel_init_pipe_control(struct intel_engine_cs *ring)
{
	int ret;

	WARN_ON(ring->scratch.obj);

	ring->scratch.obj = i915_gem_alloc_object(ring->dev, 4096);
	if (ring->scratch.obj == NULL) {
		DRM_ERROR("Failed to allocate seqno page\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = i915_gem_object_set_cache_level(ring->scratch.obj, I915_CACHE_LLC);
	if (ret)
		goto err_unref;

	ret = i915_gem_obj_ggtt_pin(ring->scratch.obj, 4096, 0);
	if (ret)
		goto err_unref;

	ring->scratch.gtt_offset = i915_gem_obj_ggtt_offset(ring->scratch.obj);
	ring->scratch.cpu_page = kmap(sg_page(ring->scratch.obj->pages->sgl));
	if (ring->scratch.cpu_page == NULL) {
		ret = -ENOMEM;
		goto err_unpin;
	}

	DRM_DEBUG_DRIVER("%s pipe control offset: 0x%08x\n",
			 ring->name, ring->scratch.gtt_offset);
	return 0;

err_unpin:
	i915_gem_object_ggtt_unpin(ring->scratch.obj);
err_unref:
	drm_gem_object_unreference(&ring->scratch.obj->base);
err:
	return ret;
}

static int intel_ring_workarounds_emit(struct drm_i915_gem_request *req)
{
	int ret, i;
	struct intel_engine_cs *ring = req->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_workarounds *w = &dev_priv->workarounds;

	if (w->count == 0)
		return 0;

	ring->gpu_caches_dirty = true;
	ret = intel_ring_flush_all_caches(req);
	if (ret)
		return ret;

	ret = intel_ring_begin(req, (w->count * 2 + 2));
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(w->count));
	for (i = 0; i < w->count; i++) {
		intel_ring_emit_reg(ring, w->reg[i].addr);
		intel_ring_emit(ring, w->reg[i].value);
	}
	intel_ring_emit(ring, MI_NOOP);

	intel_ring_advance(ring);

	ring->gpu_caches_dirty = true;
	ret = intel_ring_flush_all_caches(req);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Number of Workarounds emitted: %d\n", w->count);

	return 0;
}

static int intel_rcs_ctx_init(struct drm_i915_gem_request *req)
{
	int ret;

	ret = intel_ring_workarounds_emit(req);
	if (ret != 0)
		return ret;

	ret = i915_gem_render_state_init(req);
	if (ret)
		return ret;

	return 0;
}

static int wa_add(struct drm_i915_private *dev_priv,
		  i915_reg_t addr,
		  const u32 mask, const u32 val)
{
	const u32 idx = dev_priv->workarounds.count;

	if (WARN_ON(idx >= I915_MAX_WA_REGS))
		return -ENOSPC;

	dev_priv->workarounds.reg[idx].addr = addr;
	dev_priv->workarounds.reg[idx].value = val;
	dev_priv->workarounds.reg[idx].mask = mask;

	dev_priv->workarounds.count++;

	return 0;
}

#define WA_REG(addr, mask, val) do { \
		const int r = wa_add(dev_priv, (addr), (mask), (val)); \
		if (r) \
			return r; \
	} while (0)

#define WA_SET_BIT_MASKED(addr, mask) \
	WA_REG(addr, (mask), _MASKED_BIT_ENABLE(mask))

#define WA_CLR_BIT_MASKED(addr, mask) \
	WA_REG(addr, (mask), _MASKED_BIT_DISABLE(mask))

#define WA_SET_FIELD_MASKED(addr, mask, value) \
	WA_REG(addr, mask, _MASKED_FIELD(mask, value))

#define WA_SET_BIT(addr, mask) WA_REG(addr, mask, I915_READ(addr) | (mask))
#define WA_CLR_BIT(addr, mask) WA_REG(addr, mask, I915_READ(addr) & ~(mask))

#define WA_WRITE(addr, val) WA_REG(addr, 0xffffffff, val)

static int wa_ring_whitelist_reg(struct intel_engine_cs *ring, i915_reg_t reg)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct i915_workarounds *wa = &dev_priv->workarounds;
	const uint32_t index = wa->hw_whitelist_count[ring->id];

	if (WARN_ON(index >= RING_MAX_NONPRIV_SLOTS))
		return -EINVAL;

	WA_WRITE(RING_FORCE_TO_NONPRIV(ring->mmio_base, index),
		 i915_mmio_reg_offset(reg));
	wa->hw_whitelist_count[ring->id]++;

	return 0;
}

static int gen8_init_workarounds(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	WA_SET_BIT_MASKED(INSTPM, INSTPM_FORCE_ORDERING);

	/* WaDisableAsyncFlipPerfMode:bdw,chv */
	WA_SET_BIT_MASKED(MI_MODE, ASYNC_FLIP_PERF_DISABLE);

	/* WaDisablePartialInstShootdown:bdw,chv */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* Use Force Non-Coherent whenever executing a 3D context. This is a
	 * workaround for for a possible hang in the unlikely event a TLB
	 * invalidation occurs during a PSD flush.
	 */
	/* WaForceEnableNonCoherent:bdw,chv */
	/* WaHdcDisableFetchWhenMasked:bdw,chv */
	WA_SET_BIT_MASKED(HDC_CHICKEN0,
			  HDC_DONOT_FETCH_MEM_WHEN_MASKED |
			  HDC_FORCE_NON_COHERENT);

	/* From the Haswell PRM, Command Reference: Registers, CACHE_MODE_0:
	 * "The Hierarchical Z RAW Stall Optimization allows non-overlapping
	 *  polygons in the same 8x4 pixel/sample area to be processed without
	 *  stalling waiting for the earlier ones to write to Hierarchical Z
	 *  buffer."
	 *
	 * This optimization is off by default for BDW and CHV; turn it on.
	 */
	WA_CLR_BIT_MASKED(CACHE_MODE_0_GEN7, HIZ_RAW_STALL_OPT_DISABLE);

	/* Wa4x4STCOptimizationDisable:bdw,chv */
	WA_SET_BIT_MASKED(CACHE_MODE_1, GEN8_4x4_STC_OPTIMIZATION_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	WA_SET_FIELD_MASKED(GEN7_GT_MODE,
			    GEN6_WIZ_HASHING_MASK,
			    GEN6_WIZ_HASHING_16x4);

	return 0;
}

static int bdw_init_workarounds(struct intel_engine_cs *ring)
{
	int ret;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	ret = gen8_init_workarounds(ring);
	if (ret)
		return ret;

	/* WaDisableThreadStallDopClockGating:bdw (pre-production) */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* WaDisableDopClockGating:bdw */
	WA_SET_BIT_MASKED(GEN7_ROW_CHICKEN2,
			  DOP_CLOCK_GATING_DISABLE);

	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
			  GEN8_SAMPLER_POWER_BYPASS_DIS);

	WA_SET_BIT_MASKED(HDC_CHICKEN0,
			  /* WaForceContextSaveRestoreNonCoherent:bdw */
			  HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT |
			  /* WaDisableFenceDestinationToSLM:bdw (pre-prod) */
			  (IS_BDW_GT3(dev) ? HDC_FENCE_DEST_SLM_DISABLE : 0));

	return 0;
}

static int chv_init_workarounds(struct intel_engine_cs *ring)
{
	int ret;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	ret = gen8_init_workarounds(ring);
	if (ret)
		return ret;

	/* WaDisableThreadStallDopClockGating:chv */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* Improve HiZ throughput on CHV. */
	WA_SET_BIT_MASKED(HIZ_CHICKEN, CHV_HZ_8X8_MODE_IN_1X);

	return 0;
}

static int gen9_init_workarounds(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t tmp;
	int ret;

	/* WaEnableLbsSlaRetryTimerDecrement:skl */
	I915_WRITE(BDW_SCRATCH1, I915_READ(BDW_SCRATCH1) |
		   GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE);

	/* WaDisableKillLogic:bxt,skl */
	I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) |
		   ECOCHK_DIS_TLB);

	/* WaDisablePartialInstShootdown:skl,bxt */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* Syncing dependencies between camera and graphics:skl,bxt */
	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
			  GEN9_DISABLE_OCL_OOB_SUPPRESS_LOGIC);

	/* WaDisableDgMirrorFixInHalfSliceChicken5:skl,bxt */
	if (IS_SKL_REVID(dev, 0, SKL_REVID_B0) ||
	    IS_BXT_REVID(dev, 0, BXT_REVID_A1))
		WA_CLR_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN5,
				  GEN9_DG_MIRROR_FIX_ENABLE);

	/* WaSetDisablePixMaskCammingAndRhwoInCommonSliceChicken:skl,bxt */
	if (IS_SKL_REVID(dev, 0, SKL_REVID_B0) ||
	    IS_BXT_REVID(dev, 0, BXT_REVID_A1)) {
		WA_SET_BIT_MASKED(GEN7_COMMON_SLICE_CHICKEN1,
				  GEN9_RHWO_OPTIMIZATION_DISABLE);
		/*
		 * WA also requires GEN9_SLICE_COMMON_ECO_CHICKEN0[14:14] to be set
		 * but we do that in per ctx batchbuffer as there is an issue
		 * with this register not getting restored on ctx restore
		 */
	}

	/* WaEnableYV12BugFixInHalfSliceChicken7:skl,bxt */
	if (IS_SKL_REVID(dev, SKL_REVID_C0, REVID_FOREVER) || IS_BROXTON(dev))
		WA_SET_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN7,
				  GEN9_ENABLE_YV12_BUGFIX);

	/* Wa4x4STCOptimizationDisable:skl,bxt */
	/* WaDisablePartialResolveInVc:skl,bxt */
	WA_SET_BIT_MASKED(CACHE_MODE_1, (GEN8_4x4_STC_OPTIMIZATION_DISABLE |
					 GEN9_PARTIAL_RESOLVE_IN_VC_DISABLE));

	/* WaCcsTlbPrefetchDisable:skl,bxt */
	WA_CLR_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN5,
			  GEN9_CCS_TLB_PREFETCH_ENABLE);

	/* WaDisableMaskBasedCammingInRCC:skl,bxt */
	if (IS_SKL_REVID(dev, SKL_REVID_C0, SKL_REVID_C0) ||
	    IS_BXT_REVID(dev, 0, BXT_REVID_A1))
		WA_SET_BIT_MASKED(SLICE_ECO_CHICKEN0,
				  PIXEL_MASK_CAMMING_DISABLE);

	/* WaForceContextSaveRestoreNonCoherent:skl,bxt */
	tmp = HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT;
	if (IS_SKL_REVID(dev, SKL_REVID_F0, SKL_REVID_F0) ||
	    IS_BXT_REVID(dev, BXT_REVID_B0, REVID_FOREVER))
		tmp |= HDC_FORCE_CSR_NON_COHERENT_OVR_DISABLE;
	WA_SET_BIT_MASKED(HDC_CHICKEN0, tmp);

	/* WaDisableSamplerPowerBypassForSOPingPong:skl,bxt */
	if (IS_SKYLAKE(dev) || IS_BXT_REVID(dev, 0, BXT_REVID_B0))
		WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
				  GEN8_SAMPLER_POWER_BYPASS_DIS);

	/* WaDisableSTUnitPowerOptimization:skl,bxt */
	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN2, GEN8_ST_PO_DISABLE);

	/* WaOCLCoherentLineFlush:skl,bxt */
	I915_WRITE(GEN8_L3SQCREG4, (I915_READ(GEN8_L3SQCREG4) |
				    GEN8_LQSC_FLUSH_COHERENT_LINES));

	/* WaEnablePreemptionGranularityControlByUMD:skl,bxt */
	ret= wa_ring_whitelist_reg(ring, GEN8_CS_CHICKEN1);
	if (ret)
		return ret;

	/* WaAllowUMDToModifyHDCChicken1:skl,bxt */
	ret = wa_ring_whitelist_reg(ring, GEN8_HDC_CHICKEN1);
	if (ret)
		return ret;

	return 0;
}

static int skl_tune_iz_hashing(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u8 vals[3] = { 0, 0, 0 };
	unsigned int i;

	for (i = 0; i < 3; i++) {
		u8 ss;

		/*
		 * Only consider slices where one, and only one, subslice has 7
		 * EUs
		 */
		if (!is_power_of_2(dev_priv->info.subslice_7eu[i]))
			continue;

		/*
		 * subslice_7eu[i] != 0 (because of the check above) and
		 * ss_max == 4 (maximum number of subslices possible per slice)
		 *
		 * ->    0 <= ss <= 3;
		 */
		ss = ffs(dev_priv->info.subslice_7eu[i]) - 1;
		vals[i] = 3 - ss;
	}

	if (vals[0] == 0 && vals[1] == 0 && vals[2] == 0)
		return 0;

	/* Tune IZ hashing. See intel_device_info_runtime_init() */
	WA_SET_FIELD_MASKED(GEN7_GT_MODE,
			    GEN9_IZ_HASHING_MASK(2) |
			    GEN9_IZ_HASHING_MASK(1) |
			    GEN9_IZ_HASHING_MASK(0),
			    GEN9_IZ_HASHING(2, vals[2]) |
			    GEN9_IZ_HASHING(1, vals[1]) |
			    GEN9_IZ_HASHING(0, vals[0]));

	return 0;
}

static int skl_init_workarounds(struct intel_engine_cs *ring)
{
	int ret;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	ret = gen9_init_workarounds(ring);
	if (ret)
		return ret;

	/*
	 * Actual WA is to disable percontext preemption granularity control
	 * until D0 which is the default case so this is equivalent to
	 * !WaDisablePerCtxtPreemptionGranularityControl:skl
	 */
	if (IS_SKL_REVID(dev, SKL_REVID_E0, REVID_FOREVER)) {
		I915_WRITE(GEN7_FF_SLICE_CS_CHICKEN1,
			   _MASKED_BIT_ENABLE(GEN9_FFSC_PERCTX_PREEMPT_CTRL));
	}

	if (IS_SKL_REVID(dev, 0, SKL_REVID_D0)) {
		/* WaDisableChickenBitTSGBarrierAckForFFSliceCS:skl */
		I915_WRITE(FF_SLICE_CS_CHICKEN2,
			   _MASKED_BIT_ENABLE(GEN9_TSG_BARRIER_ACK_DISABLE));
	}

	/* GEN8_L3SQCREG4 has a dependency with WA batch so any new changes
	 * involving this register should also be added to WA batch as required.
	 */
	if (IS_SKL_REVID(dev, 0, SKL_REVID_E0))
		/* WaDisableLSQCROPERFforOCL:skl */
		I915_WRITE(GEN8_L3SQCREG4, I915_READ(GEN8_L3SQCREG4) |
			   GEN8_LQSC_RO_PERF_DIS);

	/* WaEnableGapsTsvCreditFix:skl */
	if (IS_SKL_REVID(dev, SKL_REVID_C0, REVID_FOREVER)) {
		I915_WRITE(GEN8_GARBCNTL, (I915_READ(GEN8_GARBCNTL) |
					   GEN9_GAPS_TSV_CREDIT_DISABLE));
	}

	/* WaDisablePowerCompilerClockGating:skl */
	if (IS_SKL_REVID(dev, SKL_REVID_B0, SKL_REVID_B0))
		WA_SET_BIT_MASKED(HIZ_CHICKEN,
				  BDW_HIZ_POWER_COMPILER_CLOCK_GATING_DISABLE);

	if (IS_SKL_REVID(dev, 0, SKL_REVID_F0)) {
		/*
		 *Use Force Non-Coherent whenever executing a 3D context. This
		 * is a workaround for a possible hang in the unlikely event
		 * a TLB invalidation occurs during a PSD flush.
		 */
		/* WaForceEnableNonCoherent:skl */
		WA_SET_BIT_MASKED(HDC_CHICKEN0,
				  HDC_FORCE_NON_COHERENT);

		/* WaDisableHDCInvalidation:skl */
		I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) |
			   BDW_DISABLE_HDC_INVALIDATION);
	}

	/* WaBarrierPerformanceFixDisable:skl */
	if (IS_SKL_REVID(dev, SKL_REVID_C0, SKL_REVID_D0))
		WA_SET_BIT_MASKED(HDC_CHICKEN0,
				  HDC_FENCE_DEST_SLM_DISABLE |
				  HDC_BARRIER_PERFORMANCE_DISABLE);

	/* WaDisableSbeCacheDispatchPortSharing:skl */
	if (IS_SKL_REVID(dev, 0, SKL_REVID_F0))
		WA_SET_BIT_MASKED(
			GEN7_HALF_SLICE_CHICKEN1,
			GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);

	/* WaDisableLSQCROPERFforOCL:skl */
	ret = wa_ring_whitelist_reg(ring, GEN8_L3SQCREG4);
	if (ret)
		return ret;

	return skl_tune_iz_hashing(ring);
}

static int bxt_init_workarounds(struct intel_engine_cs *ring)
{
	int ret;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	ret = gen9_init_workarounds(ring);
	if (ret)
		return ret;

	/* WaStoreMultiplePTEenable:bxt */
	/* This is a requirement according to Hardware specification */
	if (IS_BXT_REVID(dev, 0, BXT_REVID_A1))
		I915_WRITE(TILECTL, I915_READ(TILECTL) | TILECTL_TLBPF);

	/* WaSetClckGatingDisableMedia:bxt */
	if (IS_BXT_REVID(dev, 0, BXT_REVID_A1)) {
		I915_WRITE(GEN7_MISCCPCTL, (I915_READ(GEN7_MISCCPCTL) &
					    ~GEN8_DOP_CLOCK_GATE_MEDIA_ENABLE));
	}

	/* WaDisableThreadStallDopClockGating:bxt */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  STALL_DOP_GATING_DISABLE);

	/* WaDisableSbeCacheDispatchPortSharing:bxt */
	if (IS_BXT_REVID(dev, 0, BXT_REVID_B0)) {
		WA_SET_BIT_MASKED(
			GEN7_HALF_SLICE_CHICKEN1,
			GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);
	}

	/* WaDisableObjectLevelPreemptionForTrifanOrPolygon:bxt */
	/* WaDisableObjectLevelPreemptionForInstancedDraw:bxt */
	/* WaDisableObjectLevelPreemtionForInstanceId:bxt */
	/* WaDisableLSQCROPERFforOCL:bxt */
	if (IS_BXT_REVID(dev, 0, BXT_REVID_A1)) {
		ret = wa_ring_whitelist_reg(ring, GEN9_CS_DEBUG_MODE1);
		if (ret)
			return ret;

		ret = wa_ring_whitelist_reg(ring, GEN8_L3SQCREG4);
		if (ret)
			return ret;
	}

	return 0;
}

int init_workarounds_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(ring->id != RCS);

	dev_priv->workarounds.count = 0;
	dev_priv->workarounds.hw_whitelist_count[RCS] = 0;

	if (IS_BROADWELL(dev))
		return bdw_init_workarounds(ring);

	if (IS_CHERRYVIEW(dev))
		return chv_init_workarounds(ring);

	if (IS_SKYLAKE(dev))
		return skl_init_workarounds(ring);

	if (IS_BROXTON(dev))
		return bxt_init_workarounds(ring);

	return 0;
}

static int init_render_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret = init_ring_common(ring);
	if (ret)
		return ret;

	/* WaTimedSingleVertexDispatch:cl,bw,ctg,elk,ilk,snb */
	if (INTEL_INFO(dev)->gen >= 4 && INTEL_INFO(dev)->gen < 7)
		I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(VS_TIMER_DISPATCH));

	/* We need to disable the AsyncFlip performance optimisations in order
	 * to use MI_WAIT_FOR_EVENT within the CS. It should already be
	 * programmed to '1' on all products.
	 *
	 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv
	 */
	if (INTEL_INFO(dev)->gen >= 6 && INTEL_INFO(dev)->gen < 8)
		I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(ASYNC_FLIP_PERF_DISABLE));

	/* Required for the hardware to program scanline values for waiting */
	/* WaEnableFlushTlbInvalidationMode:snb */
	if (INTEL_INFO(dev)->gen == 6)
		I915_WRITE(GFX_MODE,
			   _MASKED_BIT_ENABLE(GFX_TLB_INVALIDATE_EXPLICIT));

	/* WaBCSVCSTlbInvalidationMode:ivb,vlv,hsw */
	if (IS_GEN7(dev))
		I915_WRITE(GFX_MODE_GEN7,
			   _MASKED_BIT_ENABLE(GFX_TLB_INVALIDATE_EXPLICIT) |
			   _MASKED_BIT_ENABLE(GFX_REPLAY_MODE));

	if (IS_GEN6(dev)) {
		/* From the Sandybridge PRM, volume 1 part 3, page 24:
		 * "If this bit is set, STCunit will have LRA as replacement
		 *  policy. [...] This bit must be reset.  LRA replacement
		 *  policy is not supported."
		 */
		I915_WRITE(CACHE_MODE_0,
			   _MASKED_BIT_DISABLE(CM0_STC_EVICT_DISABLE_LRA_SNB));
	}

	if (INTEL_INFO(dev)->gen >= 6 && INTEL_INFO(dev)->gen < 8)
		I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_FORCE_ORDERING));

	if (HAS_L3_DPF(dev))
		I915_WRITE_IMR(ring, ~GT_PARITY_ERROR(dev));

	return init_workarounds_ring(ring);
}

static void render_ring_cleanup(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->semaphore_obj) {
		i915_gem_object_ggtt_unpin(dev_priv->semaphore_obj);
		drm_gem_object_unreference(&dev_priv->semaphore_obj->base);
		dev_priv->semaphore_obj = NULL;
	}

	intel_fini_pipe_control(ring);
}

static int gen8_rcs_signal(struct drm_i915_gem_request *signaller_req,
			   unsigned int num_dwords)
{
#define MBOX_UPDATE_DWORDS 8
	struct intel_engine_cs *signaller = signaller_req->ring;
	struct drm_device *dev = signaller->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *waiter;
	int i, ret, num_rings;

	num_rings = hweight32(INTEL_INFO(dev)->ring_mask);
	num_dwords += (num_rings-1) * MBOX_UPDATE_DWORDS;
#undef MBOX_UPDATE_DWORDS

	ret = intel_ring_begin(signaller_req, num_dwords);
	if (ret)
		return ret;

	for_each_ring(waiter, dev_priv, i) {
		u32 seqno;
		u64 gtt_offset = signaller->semaphore.signal_ggtt[i];
		if (gtt_offset == MI_SEMAPHORE_SYNC_INVALID)
			continue;

		seqno = i915_gem_request_get_seqno(signaller_req);
		intel_ring_emit(signaller, GFX_OP_PIPE_CONTROL(6));
		intel_ring_emit(signaller, PIPE_CONTROL_GLOBAL_GTT_IVB |
					   PIPE_CONTROL_QW_WRITE |
					   PIPE_CONTROL_FLUSH_ENABLE);
		intel_ring_emit(signaller, lower_32_bits(gtt_offset));
		intel_ring_emit(signaller, upper_32_bits(gtt_offset));
		intel_ring_emit(signaller, seqno);
		intel_ring_emit(signaller, 0);
		intel_ring_emit(signaller, MI_SEMAPHORE_SIGNAL |
					   MI_SEMAPHORE_TARGET(waiter->id));
		intel_ring_emit(signaller, 0);
	}

	return 0;
}

static int gen8_xcs_signal(struct drm_i915_gem_request *signaller_req,
			   unsigned int num_dwords)
{
#define MBOX_UPDATE_DWORDS 6
	struct intel_engine_cs *signaller = signaller_req->ring;
	struct drm_device *dev = signaller->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *waiter;
	int i, ret, num_rings;

	num_rings = hweight32(INTEL_INFO(dev)->ring_mask);
	num_dwords += (num_rings-1) * MBOX_UPDATE_DWORDS;
#undef MBOX_UPDATE_DWORDS

	ret = intel_ring_begin(signaller_req, num_dwords);
	if (ret)
		return ret;

	for_each_ring(waiter, dev_priv, i) {
		u32 seqno;
		u64 gtt_offset = signaller->semaphore.signal_ggtt[i];
		if (gtt_offset == MI_SEMAPHORE_SYNC_INVALID)
			continue;

		seqno = i915_gem_request_get_seqno(signaller_req);
		intel_ring_emit(signaller, (MI_FLUSH_DW + 1) |
					   MI_FLUSH_DW_OP_STOREDW);
		intel_ring_emit(signaller, lower_32_bits(gtt_offset) |
					   MI_FLUSH_DW_USE_GTT);
		intel_ring_emit(signaller, upper_32_bits(gtt_offset));
		intel_ring_emit(signaller, seqno);
		intel_ring_emit(signaller, MI_SEMAPHORE_SIGNAL |
					   MI_SEMAPHORE_TARGET(waiter->id));
		intel_ring_emit(signaller, 0);
	}

	return 0;
}

static int gen6_signal(struct drm_i915_gem_request *signaller_req,
		       unsigned int num_dwords)
{
	struct intel_engine_cs *signaller = signaller_req->ring;
	struct drm_device *dev = signaller->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *useless;
	int i, ret, num_rings;

#define MBOX_UPDATE_DWORDS 3
	num_rings = hweight32(INTEL_INFO(dev)->ring_mask);
	num_dwords += round_up((num_rings-1) * MBOX_UPDATE_DWORDS, 2);
#undef MBOX_UPDATE_DWORDS

	ret = intel_ring_begin(signaller_req, num_dwords);
	if (ret)
		return ret;

	for_each_ring(useless, dev_priv, i) {
		i915_reg_t mbox_reg = signaller->semaphore.mbox.signal[i];

		if (i915_mmio_reg_valid(mbox_reg)) {
			u32 seqno = i915_gem_request_get_seqno(signaller_req);

			intel_ring_emit(signaller, MI_LOAD_REGISTER_IMM(1));
			intel_ring_emit_reg(signaller, mbox_reg);
			intel_ring_emit(signaller, seqno);
		}
	}

	/* If num_dwords was rounded, make sure the tail pointer is correct */
	if (num_rings % 2 == 0)
		intel_ring_emit(signaller, MI_NOOP);

	return 0;
}

/**
 * gen6_add_request - Update the semaphore mailbox registers
 *
 * @request - request to write to the ring
 *
 * Update the mailbox registers in the *other* rings with the current seqno.
 * This acts like a signal in the canonical semaphore.
 */
static int
gen6_add_request(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	if (ring->semaphore.signal)
		ret = ring->semaphore.signal(req, 4);
	else
		ret = intel_ring_begin(req, 4);

	if (ret)
		return ret;

	intel_ring_emit(ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(ring, I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(ring, i915_gem_request_get_seqno(req));
	intel_ring_emit(ring, MI_USER_INTERRUPT);
	__intel_ring_advance(ring);

	return 0;
}

static inline bool i915_gem_has_seqno_wrapped(struct drm_device *dev,
					      u32 seqno)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	return dev_priv->last_seqno < seqno;
}

/**
 * intel_ring_sync - sync the waiter to the signaller on seqno
 *
 * @waiter - ring that is waiting
 * @signaller - ring which has, or will signal
 * @seqno - seqno which the waiter will block on
 */

static int
gen8_ring_sync(struct drm_i915_gem_request *waiter_req,
	       struct intel_engine_cs *signaller,
	       u32 seqno)
{
	struct intel_engine_cs *waiter = waiter_req->ring;
	struct drm_i915_private *dev_priv = waiter->dev->dev_private;
	int ret;

	ret = intel_ring_begin(waiter_req, 4);
	if (ret)
		return ret;

	intel_ring_emit(waiter, MI_SEMAPHORE_WAIT |
				MI_SEMAPHORE_GLOBAL_GTT |
				MI_SEMAPHORE_POLL |
				MI_SEMAPHORE_SAD_GTE_SDD);
	intel_ring_emit(waiter, seqno);
	intel_ring_emit(waiter,
			lower_32_bits(GEN8_WAIT_OFFSET(waiter, signaller->id)));
	intel_ring_emit(waiter,
			upper_32_bits(GEN8_WAIT_OFFSET(waiter, signaller->id)));
	intel_ring_advance(waiter);
	return 0;
}

static int
gen6_ring_sync(struct drm_i915_gem_request *waiter_req,
	       struct intel_engine_cs *signaller,
	       u32 seqno)
{
	struct intel_engine_cs *waiter = waiter_req->ring;
	u32 dw1 = MI_SEMAPHORE_MBOX |
		  MI_SEMAPHORE_COMPARE |
		  MI_SEMAPHORE_REGISTER;
	u32 wait_mbox = signaller->semaphore.mbox.wait[waiter->id];
	int ret;

	/* Throughout all of the GEM code, seqno passed implies our current
	 * seqno is >= the last seqno executed. However for hardware the
	 * comparison is strictly greater than.
	 */
	seqno -= 1;

	WARN_ON(wait_mbox == MI_SEMAPHORE_SYNC_INVALID);

	ret = intel_ring_begin(waiter_req, 4);
	if (ret)
		return ret;

	/* If seqno wrap happened, omit the wait with no-ops */
	if (likely(!i915_gem_has_seqno_wrapped(waiter->dev, seqno))) {
		intel_ring_emit(waiter, dw1 | wait_mbox);
		intel_ring_emit(waiter, seqno);
		intel_ring_emit(waiter, 0);
		intel_ring_emit(waiter, MI_NOOP);
	} else {
		intel_ring_emit(waiter, MI_NOOP);
		intel_ring_emit(waiter, MI_NOOP);
		intel_ring_emit(waiter, MI_NOOP);
		intel_ring_emit(waiter, MI_NOOP);
	}
	intel_ring_advance(waiter);

	return 0;
}

#define PIPE_CONTROL_FLUSH(ring__, addr__)					\
do {									\
	intel_ring_emit(ring__, GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE |		\
		 PIPE_CONTROL_DEPTH_STALL);				\
	intel_ring_emit(ring__, (addr__) | PIPE_CONTROL_GLOBAL_GTT);			\
	intel_ring_emit(ring__, 0);							\
	intel_ring_emit(ring__, 0);							\
} while (0)

static int
pc_render_add_request(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	int ret;

	/* For Ironlake, MI_USER_INTERRUPT was deprecated and apparently
	 * incoherent with writes to memory, i.e. completely fubar,
	 * so we need to use PIPE_NOTIFY instead.
	 *
	 * However, we also need to workaround the qword write
	 * incoherence by flushing the 6 PIPE_NOTIFY buffers out to
	 * memory before requesting an interrupt.
	 */
	ret = intel_ring_begin(req, 32);
	if (ret)
		return ret;

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE |
			PIPE_CONTROL_WRITE_FLUSH |
			PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE);
	intel_ring_emit(ring, ring->scratch.gtt_offset | PIPE_CONTROL_GLOBAL_GTT);
	intel_ring_emit(ring, i915_gem_request_get_seqno(req));
	intel_ring_emit(ring, 0);
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 2 * CACHELINE_BYTES; /* write to separate cachelines */
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 2 * CACHELINE_BYTES;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 2 * CACHELINE_BYTES;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 2 * CACHELINE_BYTES;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);
	scratch_addr += 2 * CACHELINE_BYTES;
	PIPE_CONTROL_FLUSH(ring, scratch_addr);

	intel_ring_emit(ring, GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE |
			PIPE_CONTROL_WRITE_FLUSH |
			PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
			PIPE_CONTROL_NOTIFY);
	intel_ring_emit(ring, ring->scratch.gtt_offset | PIPE_CONTROL_GLOBAL_GTT);
	intel_ring_emit(ring, i915_gem_request_get_seqno(req));
	intel_ring_emit(ring, 0);
	__intel_ring_advance(ring);

	return 0;
}

static u32
gen6_ring_get_seqno(struct intel_engine_cs *ring, bool lazy_coherency)
{
	/* Workaround to force correct ordering between irq and seqno writes on
	 * ivb (and maybe also on snb) by reading from a CS register (like
	 * ACTHD) before reading the status page. */
	if (!lazy_coherency) {
		struct drm_i915_private *dev_priv = ring->dev->dev_private;
		POSTING_READ(RING_ACTHD(ring->mmio_base));
	}

	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static u32
ring_get_seqno(struct intel_engine_cs *ring, bool lazy_coherency)
{
	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static void
ring_set_seqno(struct intel_engine_cs *ring, u32 seqno)
{
	intel_write_status_page(ring, I915_GEM_HWS_INDEX, seqno);
}

static u32
pc_render_get_seqno(struct intel_engine_cs *ring, bool lazy_coherency)
{
	return ring->scratch.cpu_page[0];
}

static void
pc_render_set_seqno(struct intel_engine_cs *ring, u32 seqno)
{
	ring->scratch.cpu_page[0] = seqno;
}

static bool
gen5_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0)
		gen5_enable_gt_irq(dev_priv, ring->irq_enable_mask);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
gen5_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0)
		gen5_disable_gt_irq(dev_priv, ring->irq_enable_mask);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static bool
i9xx_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (!intel_irqs_enabled(dev_priv))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		dev_priv->irq_mask &= ~ring->irq_enable_mask;
		I915_WRITE(IMR, dev_priv->irq_mask);
		POSTING_READ(IMR);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
i9xx_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		dev_priv->irq_mask |= ring->irq_enable_mask;
		I915_WRITE(IMR, dev_priv->irq_mask);
		POSTING_READ(IMR);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static bool
i8xx_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (!intel_irqs_enabled(dev_priv))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		dev_priv->irq_mask &= ~ring->irq_enable_mask;
		I915_WRITE16(IMR, dev_priv->irq_mask);
		POSTING_READ16(IMR);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
i8xx_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		dev_priv->irq_mask |= ring->irq_enable_mask;
		I915_WRITE16(IMR, dev_priv->irq_mask);
		POSTING_READ16(IMR);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static int
bsd_ring_flush(struct drm_i915_gem_request *req,
	       u32     invalidate_domains,
	       u32     flush_domains)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_FLUSH);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);
	return 0;
}

static int
i9xx_add_request(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_STORE_DWORD_INDEX);
	intel_ring_emit(ring, I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	intel_ring_emit(ring, i915_gem_request_get_seqno(req));
	intel_ring_emit(ring, MI_USER_INTERRUPT);
	__intel_ring_advance(ring);

	return 0;
}

static bool
gen6_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		if (HAS_L3_DPF(dev) && ring->id == RCS)
			I915_WRITE_IMR(ring,
				       ~(ring->irq_enable_mask |
					 GT_PARITY_ERROR(dev)));
		else
			I915_WRITE_IMR(ring, ~ring->irq_enable_mask);
		gen5_enable_gt_irq(dev_priv, ring->irq_enable_mask);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
gen6_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		if (HAS_L3_DPF(dev) && ring->id == RCS)
			I915_WRITE_IMR(ring, ~GT_PARITY_ERROR(dev));
		else
			I915_WRITE_IMR(ring, ~0);
		gen5_disable_gt_irq(dev_priv, ring->irq_enable_mask);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static bool
hsw_vebox_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		I915_WRITE_IMR(ring, ~ring->irq_enable_mask);
		gen6_enable_pm_irq(dev_priv, ring->irq_enable_mask);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
hsw_vebox_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		I915_WRITE_IMR(ring, ~0);
		gen6_disable_pm_irq(dev_priv, ring->irq_enable_mask);
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static bool
gen8_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		if (HAS_L3_DPF(dev) && ring->id == RCS) {
			I915_WRITE_IMR(ring,
				       ~(ring->irq_enable_mask |
					 GT_RENDER_L3_PARITY_ERROR_INTERRUPT));
		} else {
			I915_WRITE_IMR(ring, ~ring->irq_enable_mask);
		}
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void
gen8_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		if (HAS_L3_DPF(dev) && ring->id == RCS) {
			I915_WRITE_IMR(ring,
				       ~GT_RENDER_L3_PARITY_ERROR_INTERRUPT);
		} else {
			I915_WRITE_IMR(ring, ~0);
		}
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static int
i965_dispatch_execbuffer(struct drm_i915_gem_request *req,
			 u64 offset, u32 length,
			 unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring,
			MI_BATCH_BUFFER_START |
			MI_BATCH_GTT |
			(dispatch_flags & I915_DISPATCH_SECURE ?
			 0 : MI_BATCH_NON_SECURE_I965));
	intel_ring_emit(ring, offset);
	intel_ring_advance(ring);

	return 0;
}

/* Just userspace ABI convention to limit the wa batch bo to a resonable size */
#define I830_BATCH_LIMIT (256*1024)
#define I830_TLB_ENTRIES (2)
#define I830_WA_SIZE max(I830_TLB_ENTRIES*4096, I830_BATCH_LIMIT)
static int
i830_dispatch_execbuffer(struct drm_i915_gem_request *req,
			 u64 offset, u32 len,
			 unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	u32 cs_offset = ring->scratch.gtt_offset;
	int ret;

	ret = intel_ring_begin(req, 6);
	if (ret)
		return ret;

	/* Evict the invalid PTE TLBs */
	intel_ring_emit(ring, COLOR_BLT_CMD | BLT_WRITE_RGBA);
	intel_ring_emit(ring, BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | 4096);
	intel_ring_emit(ring, I830_TLB_ENTRIES << 16 | 4); /* load each page */
	intel_ring_emit(ring, cs_offset);
	intel_ring_emit(ring, 0xdeadbeef);
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	if ((dispatch_flags & I915_DISPATCH_PINNED) == 0) {
		if (len > I830_BATCH_LIMIT)
			return -ENOSPC;

		ret = intel_ring_begin(req, 6 + 2);
		if (ret)
			return ret;

		/* Blit the batch (which has now all relocs applied) to the
		 * stable batch scratch bo area (so that the CS never
		 * stumbles over its tlb invalidation bug) ...
		 */
		intel_ring_emit(ring, SRC_COPY_BLT_CMD | BLT_WRITE_RGBA);
		intel_ring_emit(ring, BLT_DEPTH_32 | BLT_ROP_SRC_COPY | 4096);
		intel_ring_emit(ring, DIV_ROUND_UP(len, 4096) << 16 | 4096);
		intel_ring_emit(ring, cs_offset);
		intel_ring_emit(ring, 4096);
		intel_ring_emit(ring, offset);

		intel_ring_emit(ring, MI_FLUSH);
		intel_ring_emit(ring, MI_NOOP);
		intel_ring_advance(ring);

		/* ... and execute it. */
		offset = cs_offset;
	}

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_BATCH_BUFFER_START | MI_BATCH_GTT);
	intel_ring_emit(ring, offset | (dispatch_flags & I915_DISPATCH_SECURE ?
					0 : MI_BATCH_NON_SECURE));
	intel_ring_advance(ring);

	return 0;
}

static int
i915_dispatch_execbuffer(struct drm_i915_gem_request *req,
			 u64 offset, u32 len,
			 unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring, MI_BATCH_BUFFER_START | MI_BATCH_GTT);
	intel_ring_emit(ring, offset | (dispatch_flags & I915_DISPATCH_SECURE ?
					0 : MI_BATCH_NON_SECURE));
	intel_ring_advance(ring);

	return 0;
}

static void cleanup_phys_status_page(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = to_i915(ring->dev);

	if (!dev_priv->status_page_dmah)
		return;

	drm_pci_free(ring->dev, dev_priv->status_page_dmah);
	ring->status_page.page_addr = NULL;
}

static void cleanup_status_page(struct intel_engine_cs *ring)
{
	struct drm_i915_gem_object *obj;

	obj = ring->status_page.obj;
	if (obj == NULL)
		return;

	kunmap(sg_page(obj->pages->sgl));
	i915_gem_object_ggtt_unpin(obj);
	drm_gem_object_unreference(&obj->base);
	ring->status_page.obj = NULL;
}

static int init_status_page(struct intel_engine_cs *ring)
{
	struct drm_i915_gem_object *obj = ring->status_page.obj;

	if (obj == NULL) {
		unsigned flags;
		int ret;

		obj = i915_gem_alloc_object(ring->dev, 4096);
		if (obj == NULL) {
			DRM_ERROR("Failed to allocate status page\n");
			return -ENOMEM;
		}

		ret = i915_gem_object_set_cache_level(obj, I915_CACHE_LLC);
		if (ret)
			goto err_unref;

		flags = 0;
		if (!HAS_LLC(ring->dev))
			/* On g33, we cannot place HWS above 256MiB, so
			 * restrict its pinning to the low mappable arena.
			 * Though this restriction is not documented for
			 * gen4, gen5, or byt, they also behave similarly
			 * and hang if the HWS is placed at the top of the
			 * GTT. To generalise, it appears that all !llc
			 * platforms have issues with us placing the HWS
			 * above the mappable region (even though we never
			 * actualy map it).
			 */
			flags |= PIN_MAPPABLE;
		ret = i915_gem_obj_ggtt_pin(obj, 4096, flags);
		if (ret) {
err_unref:
			drm_gem_object_unreference(&obj->base);
			return ret;
		}

		ring->status_page.obj = obj;
	}

	ring->status_page.gfx_addr = i915_gem_obj_ggtt_offset(obj);
	ring->status_page.page_addr = kmap(sg_page(obj->pages->sgl));
	memset(ring->status_page.page_addr, 0, PAGE_SIZE);

	DRM_DEBUG_DRIVER("%s hws offset: 0x%08x\n",
			ring->name, ring->status_page.gfx_addr);

	return 0;
}

static int init_phys_status_page(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

	if (!dev_priv->status_page_dmah) {
		dev_priv->status_page_dmah =
			drm_pci_alloc(ring->dev, PAGE_SIZE, PAGE_SIZE);
		if (!dev_priv->status_page_dmah)
			return -ENOMEM;
	}

	ring->status_page.page_addr = dev_priv->status_page_dmah->vaddr;
	memset(ring->status_page.page_addr, 0, PAGE_SIZE);

	return 0;
}

void intel_unpin_ringbuffer_obj(struct intel_ringbuffer *ringbuf)
{
	if (HAS_LLC(ringbuf->obj->base.dev) && !ringbuf->obj->stolen)
		vunmap(ringbuf->virtual_start);
	else
		iounmap(ringbuf->virtual_start);
	ringbuf->virtual_start = NULL;
	ringbuf->vma = NULL;
	i915_gem_object_ggtt_unpin(ringbuf->obj);
}

static u32 *vmap_obj(struct drm_i915_gem_object *obj)
{
	struct sg_page_iter sg_iter;
	struct page **pages;
	void *addr;
	int i;

	pages = drm_malloc_ab(obj->base.size >> PAGE_SHIFT, sizeof(*pages));
	if (pages == NULL)
		return NULL;

	i = 0;
	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0)
		pages[i++] = sg_page_iter_page(&sg_iter);

	addr = vmap(pages, i, 0, PAGE_KERNEL);
	drm_free_large(pages);

	return addr;
}

int intel_pin_and_map_ringbuffer_obj(struct drm_device *dev,
				     struct intel_ringbuffer *ringbuf)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj = ringbuf->obj;
	int ret;

	if (HAS_LLC(dev_priv) && !obj->stolen) {
		ret = i915_gem_obj_ggtt_pin(obj, PAGE_SIZE, 0);
		if (ret)
			return ret;

		ret = i915_gem_object_set_to_cpu_domain(obj, true);
		if (ret) {
			i915_gem_object_ggtt_unpin(obj);
			return ret;
		}

		ringbuf->virtual_start = vmap_obj(obj);
		if (ringbuf->virtual_start == NULL) {
			i915_gem_object_ggtt_unpin(obj);
			return -ENOMEM;
		}
	} else {
		ret = i915_gem_obj_ggtt_pin(obj, PAGE_SIZE, PIN_MAPPABLE);
		if (ret)
			return ret;

		ret = i915_gem_object_set_to_gtt_domain(obj, true);
		if (ret) {
			i915_gem_object_ggtt_unpin(obj);
			return ret;
		}

		/* Access through the GTT requires the device to be awake. */
		assert_rpm_wakelock_held(dev_priv);

		ringbuf->virtual_start = ioremap_wc(dev_priv->gtt.mappable_base +
						    i915_gem_obj_ggtt_offset(obj), ringbuf->size);
		if (ringbuf->virtual_start == NULL) {
			i915_gem_object_ggtt_unpin(obj);
			return -EINVAL;
		}
	}

	ringbuf->vma = i915_gem_obj_to_ggtt(obj);

	return 0;
}

static void intel_destroy_ringbuffer_obj(struct intel_ringbuffer *ringbuf)
{
	drm_gem_object_unreference(&ringbuf->obj->base);
	ringbuf->obj = NULL;
}

static int intel_alloc_ringbuffer_obj(struct drm_device *dev,
				      struct intel_ringbuffer *ringbuf)
{
	struct drm_i915_gem_object *obj;

	obj = NULL;
	if (!HAS_LLC(dev))
		obj = i915_gem_object_create_stolen(dev, ringbuf->size);
	if (obj == NULL)
		obj = i915_gem_alloc_object(dev, ringbuf->size);
	if (obj == NULL)
		return -ENOMEM;

	/* mark ring buffers as read-only from GPU side by default */
	obj->gt_ro = 1;

	ringbuf->obj = obj;

	return 0;
}

struct intel_ringbuffer *
intel_engine_create_ringbuffer(struct intel_engine_cs *engine, int size)
{
	struct intel_ringbuffer *ring;
	int ret;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (ring == NULL) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer %s\n",
				 engine->name);
		return ERR_PTR(-ENOMEM);
	}

	ring->ring = engine;
	list_add(&ring->link, &engine->buffers);

	ring->size = size;
	/* Workaround an erratum on the i830 which causes a hang if
	 * the TAIL pointer points to within the last 2 cachelines
	 * of the buffer.
	 */
	ring->effective_size = size;
	if (IS_I830(engine->dev) || IS_845G(engine->dev))
		ring->effective_size -= 2 * CACHELINE_BYTES;

	ring->last_retired_head = -1;
	intel_ring_update_space(ring);

	ret = intel_alloc_ringbuffer_obj(engine->dev, ring);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer %s: %d\n",
				 engine->name, ret);
		list_del(&ring->link);
		kfree(ring);
		return ERR_PTR(ret);
	}

	return ring;
}

void
intel_ringbuffer_free(struct intel_ringbuffer *ring)
{
	intel_destroy_ringbuffer_obj(ring);
	list_del(&ring->link);
	kfree(ring);
}

static int intel_init_ring_buffer(struct drm_device *dev,
				  struct intel_engine_cs *ring)
{
	struct intel_ringbuffer *ringbuf;
	int ret;

	WARN_ON(ring->buffer);

	ring->dev = dev;
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	INIT_LIST_HEAD(&ring->execlist_queue);
	INIT_LIST_HEAD(&ring->buffers);
	i915_gem_batch_pool_init(dev, &ring->batch_pool);
	memset(ring->semaphore.sync_seqno, 0, sizeof(ring->semaphore.sync_seqno));

	init_waitqueue_head(&ring->irq_queue);

	ringbuf = intel_engine_create_ringbuffer(ring, 32 * PAGE_SIZE);
	if (IS_ERR(ringbuf)) {
		ret = PTR_ERR(ringbuf);
		goto error;
	}
	ring->buffer = ringbuf;

	if (I915_NEED_GFX_HWS(dev)) {
		ret = init_status_page(ring);
		if (ret)
			goto error;
	} else {
		WARN_ON(ring->id != RCS);
		ret = init_phys_status_page(ring);
		if (ret)
			goto error;
	}

	ret = intel_pin_and_map_ringbuffer_obj(dev, ringbuf);
	if (ret) {
		DRM_ERROR("Failed to pin and map ringbuffer %s: %d\n",
				ring->name, ret);
		intel_destroy_ringbuffer_obj(ringbuf);
		goto error;
	}

	ret = i915_cmd_parser_init_ring(ring);
	if (ret)
		goto error;

	return 0;

error:
	intel_cleanup_ring_buffer(ring);
	return ret;
}

void intel_cleanup_ring_buffer(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv;

	if (!intel_ring_initialized(ring))
		return;

	dev_priv = to_i915(ring->dev);

	if (ring->buffer) {
		intel_stop_ring_buffer(ring);
		WARN_ON(!IS_GEN2(ring->dev) && (I915_READ_MODE(ring) & MODE_IDLE) == 0);

		intel_unpin_ringbuffer_obj(ring->buffer);
		intel_ringbuffer_free(ring->buffer);
		ring->buffer = NULL;
	}

	if (ring->cleanup)
		ring->cleanup(ring);

	if (I915_NEED_GFX_HWS(ring->dev)) {
		cleanup_status_page(ring);
	} else {
		WARN_ON(ring->id != RCS);
		cleanup_phys_status_page(ring);
	}

	i915_cmd_parser_fini_ring(ring);
	i915_gem_batch_pool_fini(&ring->batch_pool);
	ring->dev = NULL;
}

static int ring_wait_for_space(struct intel_engine_cs *ring, int n)
{
	struct intel_ringbuffer *ringbuf = ring->buffer;
	struct drm_i915_gem_request *request;
	unsigned space;
	int ret;

	if (intel_ring_space(ringbuf) >= n)
		return 0;

	/* The whole point of reserving space is to not wait! */
	WARN_ON(ringbuf->reserved_in_use);

	list_for_each_entry(request, &ring->request_list, list) {
		space = __intel_ring_space(request->postfix, ringbuf->tail,
					   ringbuf->size);
		if (space >= n)
			break;
	}

	if (WARN_ON(&request->list == &ring->request_list))
		return -ENOSPC;

	ret = i915_wait_request(request);
	if (ret)
		return ret;

	ringbuf->space = space;
	return 0;
}

static void __wrap_ring_buffer(struct intel_ringbuffer *ringbuf)
{
	uint32_t __iomem *virt;
	int rem = ringbuf->size - ringbuf->tail;

	virt = ringbuf->virtual_start + ringbuf->tail;
	rem /= 4;
	while (rem--)
		iowrite32(MI_NOOP, virt++);

	ringbuf->tail = 0;
	intel_ring_update_space(ringbuf);
}

int intel_ring_idle(struct intel_engine_cs *ring)
{
	struct drm_i915_gem_request *req;

	/* Wait upon the last request to be completed */
	if (list_empty(&ring->request_list))
		return 0;

	req = list_entry(ring->request_list.prev,
			struct drm_i915_gem_request,
			list);

	/* Make sure we do not trigger any retires */
	return __i915_wait_request(req,
				   atomic_read(&to_i915(ring->dev)->gpu_error.reset_counter),
				   to_i915(ring->dev)->mm.interruptible,
				   NULL, NULL);
}

int intel_ring_alloc_request_extras(struct drm_i915_gem_request *request)
{
	request->ringbuf = request->ring->buffer;
	return 0;
}

int intel_ring_reserve_space(struct drm_i915_gem_request *request)
{
	/*
	 * The first call merely notes the reserve request and is common for
	 * all back ends. The subsequent localised _begin() call actually
	 * ensures that the reservation is available. Without the begin, if
	 * the request creator immediately submitted the request without
	 * adding any commands to it then there might not actually be
	 * sufficient room for the submission commands.
	 */
	intel_ring_reserved_space_reserve(request->ringbuf, MIN_SPACE_FOR_ADD_REQUEST);

	return intel_ring_begin(request, 0);
}

void intel_ring_reserved_space_reserve(struct intel_ringbuffer *ringbuf, int size)
{
	WARN_ON(ringbuf->reserved_size);
	WARN_ON(ringbuf->reserved_in_use);

	ringbuf->reserved_size = size;
}

void intel_ring_reserved_space_cancel(struct intel_ringbuffer *ringbuf)
{
	WARN_ON(ringbuf->reserved_in_use);

	ringbuf->reserved_size   = 0;
	ringbuf->reserved_in_use = false;
}

void intel_ring_reserved_space_use(struct intel_ringbuffer *ringbuf)
{
	WARN_ON(ringbuf->reserved_in_use);

	ringbuf->reserved_in_use = true;
	ringbuf->reserved_tail   = ringbuf->tail;
}

void intel_ring_reserved_space_end(struct intel_ringbuffer *ringbuf)
{
	WARN_ON(!ringbuf->reserved_in_use);
	if (ringbuf->tail > ringbuf->reserved_tail) {
		WARN(ringbuf->tail > ringbuf->reserved_tail + ringbuf->reserved_size,
		     "request reserved size too small: %d vs %d!\n",
		     ringbuf->tail - ringbuf->reserved_tail, ringbuf->reserved_size);
	} else {
		/*
		 * The ring was wrapped while the reserved space was in use.
		 * That means that some unknown amount of the ring tail was
		 * no-op filled and skipped. Thus simply adding the ring size
		 * to the tail and doing the above space check will not work.
		 * Rather than attempt to track how much tail was skipped,
		 * it is much simpler to say that also skipping the sanity
		 * check every once in a while is not a big issue.
		 */
	}

	ringbuf->reserved_size   = 0;
	ringbuf->reserved_in_use = false;
}

static int __intel_ring_prepare(struct intel_engine_cs *ring, int bytes)
{
	struct intel_ringbuffer *ringbuf = ring->buffer;
	int remain_usable = ringbuf->effective_size - ringbuf->tail;
	int remain_actual = ringbuf->size - ringbuf->tail;
	int ret, total_bytes, wait_bytes = 0;
	bool need_wrap = false;

	if (ringbuf->reserved_in_use)
		total_bytes = bytes;
	else
		total_bytes = bytes + ringbuf->reserved_size;

	if (unlikely(bytes > remain_usable)) {
		/*
		 * Not enough space for the basic request. So need to flush
		 * out the remainder and then wait for base + reserved.
		 */
		wait_bytes = remain_actual + total_bytes;
		need_wrap = true;
	} else {
		if (unlikely(total_bytes > remain_usable)) {
			/*
			 * The base request will fit but the reserved space
			 * falls off the end. So only need to to wait for the
			 * reserved size after flushing out the remainder.
			 */
			wait_bytes = remain_actual + ringbuf->reserved_size;
			need_wrap = true;
		} else if (total_bytes > ringbuf->space) {
			/* No wrapping required, just waiting. */
			wait_bytes = total_bytes;
		}
	}

	if (wait_bytes) {
		ret = ring_wait_for_space(ring, wait_bytes);
		if (unlikely(ret))
			return ret;

		if (need_wrap)
			__wrap_ring_buffer(ringbuf);
	}

	return 0;
}

int intel_ring_begin(struct drm_i915_gem_request *req,
		     int num_dwords)
{
	struct intel_engine_cs *ring;
	struct drm_i915_private *dev_priv;
	int ret;

	WARN_ON(req == NULL);
	ring = req->ring;
	dev_priv = ring->dev->dev_private;

	ret = i915_gem_check_wedge(&dev_priv->gpu_error,
				   dev_priv->mm.interruptible);
	if (ret)
		return ret;

	ret = __intel_ring_prepare(ring, num_dwords * sizeof(uint32_t));
	if (ret)
		return ret;

	ring->buffer->space -= num_dwords * sizeof(uint32_t);
	return 0;
}

/* Align the ring tail to a cacheline boundary */
int intel_ring_cacheline_align(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int num_dwords = (ring->buffer->tail & (CACHELINE_BYTES - 1)) / sizeof(uint32_t);
	int ret;

	if (num_dwords == 0)
		return 0;

	num_dwords = CACHELINE_BYTES / sizeof(uint32_t) - num_dwords;
	ret = intel_ring_begin(req, num_dwords);
	if (ret)
		return ret;

	while (num_dwords--)
		intel_ring_emit(ring, MI_NOOP);

	intel_ring_advance(ring);

	return 0;
}

void intel_ring_init_seqno(struct intel_engine_cs *ring, u32 seqno)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen == 6 || INTEL_INFO(dev)->gen == 7) {
		I915_WRITE(RING_SYNC_0(ring->mmio_base), 0);
		I915_WRITE(RING_SYNC_1(ring->mmio_base), 0);
		if (HAS_VEBOX(dev))
			I915_WRITE(RING_SYNC_2(ring->mmio_base), 0);
	}

	ring->set_seqno(ring, seqno);
	ring->hangcheck.seqno = seqno;
}

static void gen6_bsd_ring_write_tail(struct intel_engine_cs *ring,
				     u32 value)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

       /* Every tail move must follow the sequence below */

	/* Disable notification that the ring is IDLE. The GT
	 * will then assume that it is busy and bring it out of rc6.
	 */
	I915_WRITE(GEN6_BSD_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN6_BSD_SLEEP_MSG_DISABLE));

	/* Clear the context id. Here be magic! */
	I915_WRITE64(GEN6_BSD_RNCID, 0x0);

	/* Wait for the ring not to be idle, i.e. for it to wake up. */
	if (wait_for((I915_READ(GEN6_BSD_SLEEP_PSMI_CONTROL) &
		      GEN6_BSD_SLEEP_INDICATOR) == 0,
		     50))
		DRM_ERROR("timed out waiting for the BSD ring to wake up\n");

	/* Now that the ring is fully powered up, update the tail */
	I915_WRITE_TAIL(ring, value);
	POSTING_READ(RING_TAIL(ring->mmio_base));

	/* Let the ring send IDLE messages to the GT again,
	 * and so let it sleep to conserve power when idle.
	 */
	I915_WRITE(GEN6_BSD_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_DISABLE(GEN6_BSD_SLEEP_MSG_DISABLE));
}

static int gen6_bsd_ring_flush(struct drm_i915_gem_request *req,
			       u32 invalidate, u32 flush)
{
	struct intel_engine_cs *ring = req->ring;
	uint32_t cmd;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	cmd = MI_FLUSH_DW;
	if (INTEL_INFO(ring->dev)->gen >= 8)
		cmd += 1;

	/* We always require a command barrier so that subsequent
	 * commands, such as breadcrumb interrupts, are strictly ordered
	 * wrt the contents of the write cache being flushed to memory
	 * (and thus being coherent from the CPU).
	 */
	cmd |= MI_FLUSH_DW_STORE_INDEX | MI_FLUSH_DW_OP_STOREDW;

	/*
	 * Bspec vol 1c.5 - video engine command streamer:
	 * "If ENABLED, all TLBs will be invalidated once the flush
	 * operation is complete. This bit is only valid when the
	 * Post-Sync Operation field is a value of 1h or 3h."
	 */
	if (invalidate & I915_GEM_GPU_DOMAINS)
		cmd |= MI_INVALIDATE_TLB | MI_INVALIDATE_BSD;

	intel_ring_emit(ring, cmd);
	intel_ring_emit(ring, I915_GEM_HWS_SCRATCH_ADDR | MI_FLUSH_DW_USE_GTT);
	if (INTEL_INFO(ring->dev)->gen >= 8) {
		intel_ring_emit(ring, 0); /* upper addr */
		intel_ring_emit(ring, 0); /* value */
	} else  {
		intel_ring_emit(ring, 0);
		intel_ring_emit(ring, MI_NOOP);
	}
	intel_ring_advance(ring);
	return 0;
}

static int
gen8_ring_dispatch_execbuffer(struct drm_i915_gem_request *req,
			      u64 offset, u32 len,
			      unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	bool ppgtt = USES_PPGTT(ring->dev) &&
			!(dispatch_flags & I915_DISPATCH_SECURE);
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	/* FIXME(BDW): Address space and security selectors. */
	intel_ring_emit(ring, MI_BATCH_BUFFER_START_GEN8 | (ppgtt<<8) |
			(dispatch_flags & I915_DISPATCH_RS ?
			 MI_BATCH_RESOURCE_STREAMER : 0));
	intel_ring_emit(ring, lower_32_bits(offset));
	intel_ring_emit(ring, upper_32_bits(offset));
	intel_ring_emit(ring, MI_NOOP);
	intel_ring_advance(ring);

	return 0;
}

static int
hsw_ring_dispatch_execbuffer(struct drm_i915_gem_request *req,
			     u64 offset, u32 len,
			     unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring,
			MI_BATCH_BUFFER_START |
			(dispatch_flags & I915_DISPATCH_SECURE ?
			 0 : MI_BATCH_PPGTT_HSW | MI_BATCH_NON_SECURE_HSW) |
			(dispatch_flags & I915_DISPATCH_RS ?
			 MI_BATCH_RESOURCE_STREAMER : 0));
	/* bit0-7 is the length on GEN6+ */
	intel_ring_emit(ring, offset);
	intel_ring_advance(ring);

	return 0;
}

static int
gen6_ring_dispatch_execbuffer(struct drm_i915_gem_request *req,
			      u64 offset, u32 len,
			      unsigned dispatch_flags)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	ret = intel_ring_begin(req, 2);
	if (ret)
		return ret;

	intel_ring_emit(ring,
			MI_BATCH_BUFFER_START |
			(dispatch_flags & I915_DISPATCH_SECURE ?
			 0 : MI_BATCH_NON_SECURE_I965));
	/* bit0-7 is the length on GEN6+ */
	intel_ring_emit(ring, offset);
	intel_ring_advance(ring);

	return 0;
}

/* Blitter support (SandyBridge+) */

static int gen6_ring_flush(struct drm_i915_gem_request *req,
			   u32 invalidate, u32 flush)
{
	struct intel_engine_cs *ring = req->ring;
	struct drm_device *dev = ring->dev;
	uint32_t cmd;
	int ret;

	ret = intel_ring_begin(req, 4);
	if (ret)
		return ret;

	cmd = MI_FLUSH_DW;
	if (INTEL_INFO(dev)->gen >= 8)
		cmd += 1;

	/* We always require a command barrier so that subsequent
	 * commands, such as breadcrumb interrupts, are strictly ordered
	 * wrt the contents of the write cache being flushed to memory
	 * (and thus being coherent from the CPU).
	 */
	cmd |= MI_FLUSH_DW_STORE_INDEX | MI_FLUSH_DW_OP_STOREDW;

	/*
	 * Bspec vol 1c.3 - blitter engine command streamer:
	 * "If ENABLED, all TLBs will be invalidated once the flush
	 * operation is complete. This bit is only valid when the
	 * Post-Sync Operation field is a value of 1h or 3h."
	 */
	if (invalidate & I915_GEM_DOMAIN_RENDER)
		cmd |= MI_INVALIDATE_TLB;
	intel_ring_emit(ring, cmd);
	intel_ring_emit(ring, I915_GEM_HWS_SCRATCH_ADDR | MI_FLUSH_DW_USE_GTT);
	if (INTEL_INFO(dev)->gen >= 8) {
		intel_ring_emit(ring, 0); /* upper addr */
		intel_ring_emit(ring, 0); /* value */
	} else  {
		intel_ring_emit(ring, 0);
		intel_ring_emit(ring, MI_NOOP);
	}
	intel_ring_advance(ring);

	return 0;
}

int intel_init_render_ring_buffer(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[RCS];
	struct drm_i915_gem_object *obj;
	int ret;

	ring->name = "render ring";
	ring->id = RCS;
	ring->exec_id = I915_EXEC_RENDER;
	ring->mmio_base = RENDER_RING_BASE;

	if (INTEL_INFO(dev)->gen >= 8) {
		if (i915_semaphore_is_enabled(dev)) {
			obj = i915_gem_alloc_object(dev, 4096);
			if (obj == NULL) {
				DRM_ERROR("Failed to allocate semaphore bo. Disabling semaphores\n");
				i915.semaphores = 0;
			} else {
				i915_gem_object_set_cache_level(obj, I915_CACHE_LLC);
				ret = i915_gem_obj_ggtt_pin(obj, 0, PIN_NONBLOCK);
				if (ret != 0) {
					drm_gem_object_unreference(&obj->base);
					DRM_ERROR("Failed to pin semaphore bo. Disabling semaphores\n");
					i915.semaphores = 0;
				} else
					dev_priv->semaphore_obj = obj;
			}
		}

		ring->init_context = intel_rcs_ctx_init;
		ring->add_request = gen6_add_request;
		ring->flush = gen8_render_ring_flush;
		ring->irq_get = gen8_ring_get_irq;
		ring->irq_put = gen8_ring_put_irq;
		ring->irq_enable_mask = GT_RENDER_USER_INTERRUPT;
		ring->get_seqno = gen6_ring_get_seqno;
		ring->set_seqno = ring_set_seqno;
		if (i915_semaphore_is_enabled(dev)) {
			WARN_ON(!dev_priv->semaphore_obj);
			ring->semaphore.sync_to = gen8_ring_sync;
			ring->semaphore.signal = gen8_rcs_signal;
			GEN8_RING_SEMAPHORE_INIT;
		}
	} else if (INTEL_INFO(dev)->gen >= 6) {
		ring->init_context = intel_rcs_ctx_init;
		ring->add_request = gen6_add_request;
		ring->flush = gen7_render_ring_flush;
		if (INTEL_INFO(dev)->gen == 6)
			ring->flush = gen6_render_ring_flush;
		ring->irq_get = gen6_ring_get_irq;
		ring->irq_put = gen6_ring_put_irq;
		ring->irq_enable_mask = GT_RENDER_USER_INTERRUPT;
		ring->get_seqno = gen6_ring_get_seqno;
		ring->set_seqno = ring_set_seqno;
		if (i915_semaphore_is_enabled(dev)) {
			ring->semaphore.sync_to = gen6_ring_sync;
			ring->semaphore.signal = gen6_signal;
			/*
			 * The current semaphore is only applied on pre-gen8
			 * platform.  And there is no VCS2 ring on the pre-gen8
			 * platform. So the semaphore between RCS and VCS2 is
			 * initialized as INVALID.  Gen8 will initialize the
			 * sema between VCS2 and RCS later.
			 */
			ring->semaphore.mbox.wait[RCS] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.wait[VCS] = MI_SEMAPHORE_SYNC_RV;
			ring->semaphore.mbox.wait[BCS] = MI_SEMAPHORE_SYNC_RB;
			ring->semaphore.mbox.wait[VECS] = MI_SEMAPHORE_SYNC_RVE;
			ring->semaphore.mbox.wait[VCS2] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.signal[RCS] = GEN6_NOSYNC;
			ring->semaphore.mbox.signal[VCS] = GEN6_VRSYNC;
			ring->semaphore.mbox.signal[BCS] = GEN6_BRSYNC;
			ring->semaphore.mbox.signal[VECS] = GEN6_VERSYNC;
			ring->semaphore.mbox.signal[VCS2] = GEN6_NOSYNC;
		}
	} else if (IS_GEN5(dev)) {
		ring->add_request = pc_render_add_request;
		ring->flush = gen4_render_ring_flush;
		ring->get_seqno = pc_render_get_seqno;
		ring->set_seqno = pc_render_set_seqno;
		ring->irq_get = gen5_ring_get_irq;
		ring->irq_put = gen5_ring_put_irq;
		ring->irq_enable_mask = GT_RENDER_USER_INTERRUPT |
					GT_RENDER_PIPECTL_NOTIFY_INTERRUPT;
	} else {
		ring->add_request = i9xx_add_request;
		if (INTEL_INFO(dev)->gen < 4)
			ring->flush = gen2_render_ring_flush;
		else
			ring->flush = gen4_render_ring_flush;
		ring->get_seqno = ring_get_seqno;
		ring->set_seqno = ring_set_seqno;
		if (IS_GEN2(dev)) {
			ring->irq_get = i8xx_ring_get_irq;
			ring->irq_put = i8xx_ring_put_irq;
		} else {
			ring->irq_get = i9xx_ring_get_irq;
			ring->irq_put = i9xx_ring_put_irq;
		}
		ring->irq_enable_mask = I915_USER_INTERRUPT;
	}
	ring->write_tail = ring_write_tail;

	if (IS_HASWELL(dev))
		ring->dispatch_execbuffer = hsw_ring_dispatch_execbuffer;
	else if (IS_GEN8(dev))
		ring->dispatch_execbuffer = gen8_ring_dispatch_execbuffer;
	else if (INTEL_INFO(dev)->gen >= 6)
		ring->dispatch_execbuffer = gen6_ring_dispatch_execbuffer;
	else if (INTEL_INFO(dev)->gen >= 4)
		ring->dispatch_execbuffer = i965_dispatch_execbuffer;
	else if (IS_I830(dev) || IS_845G(dev))
		ring->dispatch_execbuffer = i830_dispatch_execbuffer;
	else
		ring->dispatch_execbuffer = i915_dispatch_execbuffer;
	ring->init_hw = init_render_ring;
	ring->cleanup = render_ring_cleanup;

	/* Workaround batchbuffer to combat CS tlb bug. */
	if (HAS_BROKEN_CS_TLB(dev)) {
		obj = i915_gem_alloc_object(dev, I830_WA_SIZE);
		if (obj == NULL) {
			DRM_ERROR("Failed to allocate batch bo\n");
			return -ENOMEM;
		}

		ret = i915_gem_obj_ggtt_pin(obj, 0, 0);
		if (ret != 0) {
			drm_gem_object_unreference(&obj->base);
			DRM_ERROR("Failed to ping batch bo\n");
			return ret;
		}

		ring->scratch.obj = obj;
		ring->scratch.gtt_offset = i915_gem_obj_ggtt_offset(obj);
	}

	ret = intel_init_ring_buffer(dev, ring);
	if (ret)
		return ret;

	if (INTEL_INFO(dev)->gen >= 5) {
		ret = intel_init_pipe_control(ring);
		if (ret)
			return ret;
	}

	return 0;
}

int intel_init_bsd_ring_buffer(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS];

	ring->name = "bsd ring";
	ring->id = VCS;
	ring->exec_id = I915_EXEC_BSD;

	ring->write_tail = ring_write_tail;
	if (INTEL_INFO(dev)->gen >= 6) {
		ring->mmio_base = GEN6_BSD_RING_BASE;
		/* gen6 bsd needs a special wa for tail updates */
		if (IS_GEN6(dev))
			ring->write_tail = gen6_bsd_ring_write_tail;
		ring->flush = gen6_bsd_ring_flush;
		ring->add_request = gen6_add_request;
		ring->get_seqno = gen6_ring_get_seqno;
		ring->set_seqno = ring_set_seqno;
		if (INTEL_INFO(dev)->gen >= 8) {
			ring->irq_enable_mask =
				GT_RENDER_USER_INTERRUPT << GEN8_VCS1_IRQ_SHIFT;
			ring->irq_get = gen8_ring_get_irq;
			ring->irq_put = gen8_ring_put_irq;
			ring->dispatch_execbuffer =
				gen8_ring_dispatch_execbuffer;
			if (i915_semaphore_is_enabled(dev)) {
				ring->semaphore.sync_to = gen8_ring_sync;
				ring->semaphore.signal = gen8_xcs_signal;
				GEN8_RING_SEMAPHORE_INIT;
			}
		} else {
			ring->irq_enable_mask = GT_BSD_USER_INTERRUPT;
			ring->irq_get = gen6_ring_get_irq;
			ring->irq_put = gen6_ring_put_irq;
			ring->dispatch_execbuffer =
				gen6_ring_dispatch_execbuffer;
			if (i915_semaphore_is_enabled(dev)) {
				ring->semaphore.sync_to = gen6_ring_sync;
				ring->semaphore.signal = gen6_signal;
				ring->semaphore.mbox.wait[RCS] = MI_SEMAPHORE_SYNC_VR;
				ring->semaphore.mbox.wait[VCS] = MI_SEMAPHORE_SYNC_INVALID;
				ring->semaphore.mbox.wait[BCS] = MI_SEMAPHORE_SYNC_VB;
				ring->semaphore.mbox.wait[VECS] = MI_SEMAPHORE_SYNC_VVE;
				ring->semaphore.mbox.wait[VCS2] = MI_SEMAPHORE_SYNC_INVALID;
				ring->semaphore.mbox.signal[RCS] = GEN6_RVSYNC;
				ring->semaphore.mbox.signal[VCS] = GEN6_NOSYNC;
				ring->semaphore.mbox.signal[BCS] = GEN6_BVSYNC;
				ring->semaphore.mbox.signal[VECS] = GEN6_VEVSYNC;
				ring->semaphore.mbox.signal[VCS2] = GEN6_NOSYNC;
			}
		}
	} else {
		ring->mmio_base = BSD_RING_BASE;
		ring->flush = bsd_ring_flush;
		ring->add_request = i9xx_add_request;
		ring->get_seqno = ring_get_seqno;
		ring->set_seqno = ring_set_seqno;
		if (IS_GEN5(dev)) {
			ring->irq_enable_mask = ILK_BSD_USER_INTERRUPT;
			ring->irq_get = gen5_ring_get_irq;
			ring->irq_put = gen5_ring_put_irq;
		} else {
			ring->irq_enable_mask = I915_BSD_USER_INTERRUPT;
			ring->irq_get = i9xx_ring_get_irq;
			ring->irq_put = i9xx_ring_put_irq;
		}
		ring->dispatch_execbuffer = i965_dispatch_execbuffer;
	}
	ring->init_hw = init_ring_common;

	return intel_init_ring_buffer(dev, ring);
}

/**
 * Initialize the second BSD ring (eg. Broadwell GT3, Skylake GT3)
 */
int intel_init_bsd2_ring_buffer(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS2];

	ring->name = "bsd2 ring";
	ring->id = VCS2;
	ring->exec_id = I915_EXEC_BSD;

	ring->write_tail = ring_write_tail;
	ring->mmio_base = GEN8_BSD2_RING_BASE;
	ring->flush = gen6_bsd_ring_flush;
	ring->add_request = gen6_add_request;
	ring->get_seqno = gen6_ring_get_seqno;
	ring->set_seqno = ring_set_seqno;
	ring->irq_enable_mask =
			GT_RENDER_USER_INTERRUPT << GEN8_VCS2_IRQ_SHIFT;
	ring->irq_get = gen8_ring_get_irq;
	ring->irq_put = gen8_ring_put_irq;
	ring->dispatch_execbuffer =
			gen8_ring_dispatch_execbuffer;
	if (i915_semaphore_is_enabled(dev)) {
		ring->semaphore.sync_to = gen8_ring_sync;
		ring->semaphore.signal = gen8_xcs_signal;
		GEN8_RING_SEMAPHORE_INIT;
	}
	ring->init_hw = init_ring_common;

	return intel_init_ring_buffer(dev, ring);
}

int intel_init_blt_ring_buffer(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[BCS];

	ring->name = "blitter ring";
	ring->id = BCS;
	ring->exec_id = I915_EXEC_BLT;

	ring->mmio_base = BLT_RING_BASE;
	ring->write_tail = ring_write_tail;
	ring->flush = gen6_ring_flush;
	ring->add_request = gen6_add_request;
	ring->get_seqno = gen6_ring_get_seqno;
	ring->set_seqno = ring_set_seqno;
	if (INTEL_INFO(dev)->gen >= 8) {
		ring->irq_enable_mask =
			GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT;
		ring->irq_get = gen8_ring_get_irq;
		ring->irq_put = gen8_ring_put_irq;
		ring->dispatch_execbuffer = gen8_ring_dispatch_execbuffer;
		if (i915_semaphore_is_enabled(dev)) {
			ring->semaphore.sync_to = gen8_ring_sync;
			ring->semaphore.signal = gen8_xcs_signal;
			GEN8_RING_SEMAPHORE_INIT;
		}
	} else {
		ring->irq_enable_mask = GT_BLT_USER_INTERRUPT;
		ring->irq_get = gen6_ring_get_irq;
		ring->irq_put = gen6_ring_put_irq;
		ring->dispatch_execbuffer = gen6_ring_dispatch_execbuffer;
		if (i915_semaphore_is_enabled(dev)) {
			ring->semaphore.signal = gen6_signal;
			ring->semaphore.sync_to = gen6_ring_sync;
			/*
			 * The current semaphore is only applied on pre-gen8
			 * platform.  And there is no VCS2 ring on the pre-gen8
			 * platform. So the semaphore between BCS and VCS2 is
			 * initialized as INVALID.  Gen8 will initialize the
			 * sema between BCS and VCS2 later.
			 */
			ring->semaphore.mbox.wait[RCS] = MI_SEMAPHORE_SYNC_BR;
			ring->semaphore.mbox.wait[VCS] = MI_SEMAPHORE_SYNC_BV;
			ring->semaphore.mbox.wait[BCS] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.wait[VECS] = MI_SEMAPHORE_SYNC_BVE;
			ring->semaphore.mbox.wait[VCS2] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.signal[RCS] = GEN6_RBSYNC;
			ring->semaphore.mbox.signal[VCS] = GEN6_VBSYNC;
			ring->semaphore.mbox.signal[BCS] = GEN6_NOSYNC;
			ring->semaphore.mbox.signal[VECS] = GEN6_VEBSYNC;
			ring->semaphore.mbox.signal[VCS2] = GEN6_NOSYNC;
		}
	}
	ring->init_hw = init_ring_common;

	return intel_init_ring_buffer(dev, ring);
}

int intel_init_vebox_ring_buffer(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VECS];

	ring->name = "video enhancement ring";
	ring->id = VECS;
	ring->exec_id = I915_EXEC_VEBOX;

	ring->mmio_base = VEBOX_RING_BASE;
	ring->write_tail = ring_write_tail;
	ring->flush = gen6_ring_flush;
	ring->add_request = gen6_add_request;
	ring->get_seqno = gen6_ring_get_seqno;
	ring->set_seqno = ring_set_seqno;

	if (INTEL_INFO(dev)->gen >= 8) {
		ring->irq_enable_mask =
			GT_RENDER_USER_INTERRUPT << GEN8_VECS_IRQ_SHIFT;
		ring->irq_get = gen8_ring_get_irq;
		ring->irq_put = gen8_ring_put_irq;
		ring->dispatch_execbuffer = gen8_ring_dispatch_execbuffer;
		if (i915_semaphore_is_enabled(dev)) {
			ring->semaphore.sync_to = gen8_ring_sync;
			ring->semaphore.signal = gen8_xcs_signal;
			GEN8_RING_SEMAPHORE_INIT;
		}
	} else {
		ring->irq_enable_mask = PM_VEBOX_USER_INTERRUPT;
		ring->irq_get = hsw_vebox_get_irq;
		ring->irq_put = hsw_vebox_put_irq;
		ring->dispatch_execbuffer = gen6_ring_dispatch_execbuffer;
		if (i915_semaphore_is_enabled(dev)) {
			ring->semaphore.sync_to = gen6_ring_sync;
			ring->semaphore.signal = gen6_signal;
			ring->semaphore.mbox.wait[RCS] = MI_SEMAPHORE_SYNC_VER;
			ring->semaphore.mbox.wait[VCS] = MI_SEMAPHORE_SYNC_VEV;
			ring->semaphore.mbox.wait[BCS] = MI_SEMAPHORE_SYNC_VEB;
			ring->semaphore.mbox.wait[VECS] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.wait[VCS2] = MI_SEMAPHORE_SYNC_INVALID;
			ring->semaphore.mbox.signal[RCS] = GEN6_RVESYNC;
			ring->semaphore.mbox.signal[VCS] = GEN6_VVESYNC;
			ring->semaphore.mbox.signal[BCS] = GEN6_BVESYNC;
			ring->semaphore.mbox.signal[VECS] = GEN6_NOSYNC;
			ring->semaphore.mbox.signal[VCS2] = GEN6_NOSYNC;
		}
	}
	ring->init_hw = init_ring_common;

	return intel_init_ring_buffer(dev, ring);
}

int
intel_ring_flush_all_caches(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	int ret;

	if (!ring->gpu_caches_dirty)
		return 0;

	ret = ring->flush(req, 0, I915_GEM_GPU_DOMAINS);
	if (ret)
		return ret;

	trace_i915_gem_ring_flush(req, 0, I915_GEM_GPU_DOMAINS);

	ring->gpu_caches_dirty = false;
	return 0;
}

int
intel_ring_invalidate_all_caches(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *ring = req->ring;
	uint32_t flush_domains;
	int ret;

	flush_domains = 0;
	if (ring->gpu_caches_dirty)
		flush_domains = I915_GEM_GPU_DOMAINS;

	ret = ring->flush(req, I915_GEM_GPU_DOMAINS, flush_domains);
	if (ret)
		return ret;

	trace_i915_gem_ring_flush(req, I915_GEM_GPU_DOMAINS, flush_domains);

	ring->gpu_caches_dirty = false;
	return 0;
}

void
intel_stop_ring_buffer(struct intel_engine_cs *ring)
{
	int ret;

	if (!intel_ring_initialized(ring))
		return;

	ret = intel_ring_idle(ring);
	if (ret && !i915_reset_in_progress(&to_i915(ring->dev)->gpu_error))
		DRM_ERROR("failed to quiesce %s whilst cleaning up: %d\n",
			  ring->name, ret);

	stop_ring(ring);
}
