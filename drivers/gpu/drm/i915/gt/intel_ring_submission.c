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

#include <drm/i915_drm.h>

#include "gem/i915_gem_context.h"

#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_context.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_gt_pm_irq.h"
#include "intel_reset.h"
#include "intel_ring.h"
#include "intel_workarounds.h"

/* Rough estimate of the typical request size, performing a flush,
 * set-context and then emitting the batch.
 */
#define LEGACY_REQUEST_SIZE 200

static int
gen2_render_ring_flush(struct i915_request *rq, u32 mode)
{
	unsigned int num_store_dw;
	u32 cmd, *cs;

	cmd = MI_FLUSH;
	num_store_dw = 0;
	if (mode & EMIT_INVALIDATE)
		cmd |= MI_READ_FLUSH;
	if (mode & EMIT_FLUSH)
		num_store_dw = 4;

	cs = intel_ring_begin(rq, 2 + 3 * num_store_dw);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = cmd;
	while (num_store_dw--) {
		*cs++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
		*cs++ = intel_gt_scratch_offset(rq->engine->gt,
						INTEL_GT_SCRATCH_FIELD_DEFAULT);
		*cs++ = 0;
	}
	*cs++ = MI_FLUSH | MI_NO_WRITE_FLUSH;

	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen4_render_ring_flush(struct i915_request *rq, u32 mode)
{
	u32 cmd, *cs;
	int i;

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

	cmd = MI_FLUSH;
	if (mode & EMIT_INVALIDATE) {
		cmd |= MI_EXE_FLUSH;
		if (IS_G4X(rq->i915) || IS_GEN(rq->i915, 5))
			cmd |= MI_INVALIDATE_ISP;
	}

	i = 2;
	if (mode & EMIT_INVALIDATE)
		i += 20;

	cs = intel_ring_begin(rq, i);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = cmd;

	/*
	 * A random delay to let the CS invalidate take effect? Without this
	 * delay, the GPU relocation path fails as the CS does not see
	 * the updated contents. Just as important, if we apply the flushes
	 * to the EMIT_FLUSH branch (i.e. immediately after the relocation
	 * write and before the invalidate on the next batch), the relocations
	 * still fail. This implies that is a delay following invalidation
	 * that is required to reset the caches as opposed to a delay to
	 * ensure the memory is written.
	 */
	if (mode & EMIT_INVALIDATE) {
		*cs++ = GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE;
		*cs++ = intel_gt_scratch_offset(rq->engine->gt,
						INTEL_GT_SCRATCH_FIELD_DEFAULT) |
			PIPE_CONTROL_GLOBAL_GTT;
		*cs++ = 0;
		*cs++ = 0;

		for (i = 0; i < 12; i++)
			*cs++ = MI_FLUSH;

		*cs++ = GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE;
		*cs++ = intel_gt_scratch_offset(rq->engine->gt,
						INTEL_GT_SCRATCH_FIELD_DEFAULT) |
			PIPE_CONTROL_GLOBAL_GTT;
		*cs++ = 0;
		*cs++ = 0;
	}

	*cs++ = cmd;

	intel_ring_advance(rq, cs);

	return 0;
}

/*
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
gen6_emit_post_sync_nonzero_flush(struct i915_request *rq)
{
	u32 scratch_addr =
		intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH);
	u32 *cs;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GFX_OP_PIPE_CONTROL(5);
	*cs++ = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD;
	*cs++ = scratch_addr | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = 0; /* low dword */
	*cs++ = 0; /* high dword */
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GFX_OP_PIPE_CONTROL(5);
	*cs++ = PIPE_CONTROL_QW_WRITE;
	*cs++ = scratch_addr | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen6_render_ring_flush(struct i915_request *rq, u32 mode)
{
	u32 scratch_addr =
		intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH);
	u32 *cs, flags = 0;
	int ret;

	/* Force SNB workarounds for PIPE_CONTROL flushes */
	ret = gen6_emit_post_sync_nonzero_flush(rq);
	if (ret)
		return ret;

	/* Just flush everything.  Experiments have shown that reducing the
	 * number of bits based on the write domains has little performance
	 * impact.
	 */
	if (mode & EMIT_FLUSH) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		/*
		 * Ensure that any following seqno writes only happen
		 * when the render cache is indeed flushed.
		 */
		flags |= PIPE_CONTROL_CS_STALL;
	}
	if (mode & EMIT_INVALIDATE) {
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

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = flags;
	*cs++ = scratch_addr | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = 0;
	intel_ring_advance(rq, cs);

	return 0;
}

static u32 *gen6_rcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	/* First we do the gen6_emit_post_sync_nonzero_flush w/a */
	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD;
	*cs++ = 0;
	*cs++ = 0;

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_QW_WRITE;
	*cs++ = intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_DEFAULT) |
		PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = 0;

	/* Finally we can flush and with it emit the breadcrumb */
	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = (PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
		 PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		 PIPE_CONTROL_DC_FLUSH_ENABLE |
		 PIPE_CONTROL_QW_WRITE |
		 PIPE_CONTROL_CS_STALL);
	*cs++ = i915_request_active_timeline(rq)->hwsp_offset |
		PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

static int
gen7_render_ring_cs_stall_wa(struct i915_request *rq)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD;
	*cs++ = 0;
	*cs++ = 0;
	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen7_render_ring_flush(struct i915_request *rq, u32 mode)
{
	u32 scratch_addr =
		intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH);
	u32 *cs, flags = 0;

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
	if (mode & EMIT_FLUSH) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DC_FLUSH_ENABLE;
		flags |= PIPE_CONTROL_FLUSH_ENABLE;
	}
	if (mode & EMIT_INVALIDATE) {
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
		gen7_render_ring_cs_stall_wa(rq);
	}

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = flags;
	*cs++ = scratch_addr;
	*cs++ = 0;
	intel_ring_advance(rq, cs);

	return 0;
}

static u32 *gen7_rcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = (PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
		 PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		 PIPE_CONTROL_DC_FLUSH_ENABLE |
		 PIPE_CONTROL_FLUSH_ENABLE |
		 PIPE_CONTROL_QW_WRITE |
		 PIPE_CONTROL_GLOBAL_GTT_IVB |
		 PIPE_CONTROL_CS_STALL);
	*cs++ = i915_request_active_timeline(rq)->hwsp_offset;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

static u32 *gen6_xcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	GEM_BUG_ON(i915_request_active_timeline(rq)->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(i915_request_active_timeline(rq)->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_USER_INTERRUPT;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

#define GEN7_XCS_WA 32
static u32 *gen7_xcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	int i;

	GEM_BUG_ON(i915_request_active_timeline(rq)->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(i915_request_active_timeline(rq)->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = rq->fence.seqno;

	for (i = 0; i < GEN7_XCS_WA; i++) {
		*cs++ = MI_STORE_DWORD_INDEX;
		*cs++ = I915_GEM_HWS_SEQNO_ADDR;
		*cs++ = rq->fence.seqno;
	}

	*cs++ = MI_FLUSH_DW;
	*cs++ = 0;
	*cs++ = 0;

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}
#undef GEN7_XCS_WA

static void set_hwstam(struct intel_engine_cs *engine, u32 mask)
{
	/*
	 * Keep the render interrupt unmasked as this papers over
	 * lost interrupts following a reset.
	 */
	if (engine->class == RENDER_CLASS) {
		if (INTEL_GEN(engine->i915) >= 6)
			mask &= ~BIT(0);
		else
			mask &= ~I915_USER_INTERRUPT;
	}

	intel_engine_set_hwsp_writemask(engine, mask);
}

static void set_hws_pga(struct intel_engine_cs *engine, phys_addr_t phys)
{
	struct drm_i915_private *dev_priv = engine->i915;
	u32 addr;

	addr = lower_32_bits(phys);
	if (INTEL_GEN(dev_priv) >= 4)
		addr |= (phys >> 28) & 0xf0;

	I915_WRITE(HWS_PGA, addr);
}

static struct page *status_page(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj = engine->status_page.vma->obj;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	return sg_page(obj->mm.pages->sgl);
}

static void ring_setup_phys_status_page(struct intel_engine_cs *engine)
{
	set_hws_pga(engine, PFN_PHYS(page_to_pfn(status_page(engine))));
	set_hwstam(engine, ~0u);
}

static void set_hwsp(struct intel_engine_cs *engine, u32 offset)
{
	struct drm_i915_private *dev_priv = engine->i915;
	i915_reg_t hwsp;

	/*
	 * The ring status page addresses are no longer next to the rest of
	 * the ring registers as of gen7.
	 */
	if (IS_GEN(dev_priv, 7)) {
		switch (engine->id) {
		/*
		 * No more rings exist on Gen7. Default case is only to shut up
		 * gcc switch check warning.
		 */
		default:
			GEM_BUG_ON(engine->id);
			/* fallthrough */
		case RCS0:
			hwsp = RENDER_HWS_PGA_GEN7;
			break;
		case BCS0:
			hwsp = BLT_HWS_PGA_GEN7;
			break;
		case VCS0:
			hwsp = BSD_HWS_PGA_GEN7;
			break;
		case VECS0:
			hwsp = VEBOX_HWS_PGA_GEN7;
			break;
		}
	} else if (IS_GEN(dev_priv, 6)) {
		hwsp = RING_HWS_PGA_GEN6(engine->mmio_base);
	} else {
		hwsp = RING_HWS_PGA(engine->mmio_base);
	}

	I915_WRITE(hwsp, offset);
	POSTING_READ(hwsp);
}

static void flush_cs_tlb(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	if (!IS_GEN_RANGE(dev_priv, 6, 7))
		return;

	/* ring should be idle before issuing a sync flush*/
	WARN_ON((ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE) == 0);

	ENGINE_WRITE(engine, RING_INSTPM,
		     _MASKED_BIT_ENABLE(INSTPM_TLB_INVALIDATE |
					INSTPM_SYNC_FLUSH));
	if (intel_wait_for_register(engine->uncore,
				    RING_INSTPM(engine->mmio_base),
				    INSTPM_SYNC_FLUSH, 0,
				    1000))
		DRM_ERROR("%s: wait for SyncFlush to complete for TLB invalidation timed out\n",
			  engine->name);
}

static void ring_setup_status_page(struct intel_engine_cs *engine)
{
	set_hwsp(engine, i915_ggtt_offset(engine->status_page.vma));
	set_hwstam(engine, ~0u);

	flush_cs_tlb(engine);
}

static bool stop_ring(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	if (INTEL_GEN(dev_priv) > 2) {
		ENGINE_WRITE(engine,
			     RING_MI_MODE, _MASKED_BIT_ENABLE(STOP_RING));
		if (intel_wait_for_register(engine->uncore,
					    RING_MI_MODE(engine->mmio_base),
					    MODE_IDLE,
					    MODE_IDLE,
					    1000)) {
			DRM_ERROR("%s : timed out trying to stop ring\n",
				  engine->name);

			/*
			 * Sometimes we observe that the idle flag is not
			 * set even though the ring is empty. So double
			 * check before giving up.
			 */
			if (ENGINE_READ(engine, RING_HEAD) !=
			    ENGINE_READ(engine, RING_TAIL))
				return false;
		}
	}

	ENGINE_WRITE(engine, RING_HEAD, ENGINE_READ(engine, RING_TAIL));

	ENGINE_WRITE(engine, RING_HEAD, 0);
	ENGINE_WRITE(engine, RING_TAIL, 0);

	/* The ring must be empty before it is disabled */
	ENGINE_WRITE(engine, RING_CTL, 0);

	return (ENGINE_READ(engine, RING_HEAD) & HEAD_ADDR) == 0;
}

static int xcs_resume(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_ring *ring = engine->legacy.ring;
	int ret = 0;

	GEM_TRACE("%s: ring:{HEAD:%04x, TAIL:%04x}\n",
		  engine->name, ring->head, ring->tail);

	intel_uncore_forcewake_get(engine->uncore, FORCEWAKE_ALL);

	/* WaClearRingBufHeadRegAtInit:ctg,elk */
	if (!stop_ring(engine)) {
		/* G45 ring initialization often fails to reset head to zero */
		DRM_DEBUG_DRIVER("%s head not reset to zero "
				"ctl %08x head %08x tail %08x start %08x\n",
				engine->name,
				ENGINE_READ(engine, RING_CTL),
				ENGINE_READ(engine, RING_HEAD),
				ENGINE_READ(engine, RING_TAIL),
				ENGINE_READ(engine, RING_START));

		if (!stop_ring(engine)) {
			DRM_ERROR("failed to set %s head to zero "
				  "ctl %08x head %08x tail %08x start %08x\n",
				  engine->name,
				  ENGINE_READ(engine, RING_CTL),
				  ENGINE_READ(engine, RING_HEAD),
				  ENGINE_READ(engine, RING_TAIL),
				  ENGINE_READ(engine, RING_START));
			ret = -EIO;
			goto out;
		}
	}

	if (HWS_NEEDS_PHYSICAL(dev_priv))
		ring_setup_phys_status_page(engine);
	else
		ring_setup_status_page(engine);

	intel_engine_reset_breadcrumbs(engine);

	/* Enforce ordering by reading HEAD register back */
	ENGINE_POSTING_READ(engine, RING_HEAD);

	/*
	 * Initialize the ring. This must happen _after_ we've cleared the ring
	 * registers with the above sequence (the readback of the HEAD registers
	 * also enforces ordering), otherwise the hw might lose the new ring
	 * register values.
	 */
	ENGINE_WRITE(engine, RING_START, i915_ggtt_offset(ring->vma));

	/* Check that the ring offsets point within the ring! */
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->head));
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->tail));
	intel_ring_update_space(ring);

	/* First wake the ring up to an empty/idle ring */
	ENGINE_WRITE(engine, RING_HEAD, ring->head);
	ENGINE_WRITE(engine, RING_TAIL, ring->head);
	ENGINE_POSTING_READ(engine, RING_TAIL);

	ENGINE_WRITE(engine, RING_CTL, RING_CTL_SIZE(ring->size) | RING_VALID);

	/* If the head is still not zero, the ring is dead */
	if (intel_wait_for_register(engine->uncore,
				    RING_CTL(engine->mmio_base),
				    RING_VALID, RING_VALID,
				    50)) {
		DRM_ERROR("%s initialization failed "
			  "ctl %08x (valid? %d) head %08x [%08x] tail %08x [%08x] start %08x [expected %08x]\n",
			  engine->name,
			  ENGINE_READ(engine, RING_CTL),
			  ENGINE_READ(engine, RING_CTL) & RING_VALID,
			  ENGINE_READ(engine, RING_HEAD), ring->head,
			  ENGINE_READ(engine, RING_TAIL), ring->tail,
			  ENGINE_READ(engine, RING_START),
			  i915_ggtt_offset(ring->vma));
		ret = -EIO;
		goto out;
	}

	if (INTEL_GEN(dev_priv) > 2)
		ENGINE_WRITE(engine,
			     RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));

	/* Now awake, let it get started */
	if (ring->tail != ring->head) {
		ENGINE_WRITE(engine, RING_TAIL, ring->tail);
		ENGINE_POSTING_READ(engine, RING_TAIL);
	}

	/* Papering over lost _interrupts_ immediately following the restart */
	intel_engine_queue_breadcrumbs(engine);
out:
	intel_uncore_forcewake_put(engine->uncore, FORCEWAKE_ALL);

	return ret;
}

static void reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	const u32 base = engine->mmio_base;

	/*
	 * We stop engines, otherwise we might get failed reset and a
	 * dead gpu (on elk). Also as modern gpu as kbl can suffer
	 * from system hang if batchbuffer is progressing when
	 * the reset is issued, regardless of READY_TO_RESET ack.
	 * Thus assume it is best to stop engines on all gens
	 * where we have a gpu reset.
	 *
	 * WaKBLVECSSemaphoreWaitPoll:kbl (on ALL_ENGINES)
	 *
	 * WaMediaResetMainRingCleanup:ctg,elk (presumably)
	 *
	 * FIXME: Wa for more modern gens needs to be validated
	 */
	GEM_TRACE("%s\n", engine->name);

	if (intel_engine_stop_cs(engine))
		GEM_TRACE("%s: timed out on STOP_RING\n", engine->name);

	intel_uncore_write_fw(uncore,
			      RING_HEAD(base),
			      intel_uncore_read_fw(uncore, RING_TAIL(base)));
	intel_uncore_posting_read_fw(uncore, RING_HEAD(base)); /* paranoia */

	intel_uncore_write_fw(uncore, RING_HEAD(base), 0);
	intel_uncore_write_fw(uncore, RING_TAIL(base), 0);
	intel_uncore_posting_read_fw(uncore, RING_TAIL(base));

	/* The ring must be empty before it is disabled */
	intel_uncore_write_fw(uncore, RING_CTL(base), 0);

	/* Check acts as a post */
	if (intel_uncore_read_fw(uncore, RING_HEAD(base)))
		GEM_TRACE("%s: ring head [%x] not parked\n",
			  engine->name,
			  intel_uncore_read_fw(uncore, RING_HEAD(base)));
}

static void reset_ring(struct intel_engine_cs *engine, bool stalled)
{
	struct i915_request *pos, *rq;
	unsigned long flags;
	u32 head;

	rq = NULL;
	spin_lock_irqsave(&engine->active.lock, flags);
	list_for_each_entry(pos, &engine->active.requests, sched.link) {
		if (!i915_request_completed(pos)) {
			rq = pos;
			break;
		}
	}

	/*
	 * The guilty request will get skipped on a hung engine.
	 *
	 * Users of client default contexts do not rely on logical
	 * state preserved between batches so it is safe to execute
	 * queued requests following the hang. Non default contexts
	 * rely on preserved state, so skipping a batch loses the
	 * evolution of the state and it needs to be considered corrupted.
	 * Executing more queued batches on top of corrupted state is
	 * risky. But we take the risk by trying to advance through
	 * the queued requests in order to make the client behaviour
	 * more predictable around resets, by not throwing away random
	 * amount of batches it has prepared for execution. Sophisticated
	 * clients can use gem_reset_stats_ioctl and dma fence status
	 * (exported via sync_file info ioctl on explicit fences) to observe
	 * when it loses the context state and should rebuild accordingly.
	 *
	 * The context ban, and ultimately the client ban, mechanism are safety
	 * valves if client submission ends up resulting in nothing more than
	 * subsequent hangs.
	 */

	if (rq) {
		/*
		 * Try to restore the logical GPU state to match the
		 * continuation of the request queue. If we skip the
		 * context/PD restore, then the next request may try to execute
		 * assuming that its context is valid and loaded on the GPU and
		 * so may try to access invalid memory, prompting repeated GPU
		 * hangs.
		 *
		 * If the request was guilty, we still restore the logical
		 * state in case the next request requires it (e.g. the
		 * aliasing ppgtt), but skip over the hung batch.
		 *
		 * If the request was innocent, we try to replay the request
		 * with the restored context.
		 */
		__i915_request_reset(rq, stalled);

		GEM_BUG_ON(rq->ring != engine->legacy.ring);
		head = rq->head;
	} else {
		head = engine->legacy.ring->tail;
	}
	engine->legacy.ring->head = intel_ring_wrap(engine->legacy.ring, head);

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void reset_finish(struct intel_engine_cs *engine)
{
}

static int rcs_resume(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	/*
	 * Disable CONSTANT_BUFFER before it is loaded from the context
	 * image. For as it is loaded, it is executed and the stored
	 * address may no longer be valid, leading to a GPU hang.
	 *
	 * This imposes the requirement that userspace reload their
	 * CONSTANT_BUFFER on every batch, fortunately a requirement
	 * they are already accustomed to from before contexts were
	 * enabled.
	 */
	if (IS_GEN(dev_priv, 4))
		I915_WRITE(ECOSKPD,
			   _MASKED_BIT_ENABLE(ECO_CONSTANT_BUFFER_SR_DISABLE));

	/* WaTimedSingleVertexDispatch:cl,bw,ctg,elk,ilk,snb */
	if (IS_GEN_RANGE(dev_priv, 4, 6))
		I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(VS_TIMER_DISPATCH));

	/* We need to disable the AsyncFlip performance optimisations in order
	 * to use MI_WAIT_FOR_EVENT within the CS. It should already be
	 * programmed to '1' on all products.
	 *
	 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv
	 */
	if (IS_GEN_RANGE(dev_priv, 6, 7))
		I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(ASYNC_FLIP_PERF_DISABLE));

	/* Required for the hardware to program scanline values for waiting */
	/* WaEnableFlushTlbInvalidationMode:snb */
	if (IS_GEN(dev_priv, 6))
		I915_WRITE(GFX_MODE,
			   _MASKED_BIT_ENABLE(GFX_TLB_INVALIDATE_EXPLICIT));

	/* WaBCSVCSTlbInvalidationMode:ivb,vlv,hsw */
	if (IS_GEN(dev_priv, 7))
		I915_WRITE(GFX_MODE_GEN7,
			   _MASKED_BIT_ENABLE(GFX_TLB_INVALIDATE_EXPLICIT) |
			   _MASKED_BIT_ENABLE(GFX_REPLAY_MODE));

	if (IS_GEN(dev_priv, 6)) {
		/* From the Sandybridge PRM, volume 1 part 3, page 24:
		 * "If this bit is set, STCunit will have LRA as replacement
		 *  policy. [...] This bit must be reset.  LRA replacement
		 *  policy is not supported."
		 */
		I915_WRITE(CACHE_MODE_0,
			   _MASKED_BIT_DISABLE(CM0_STC_EVICT_DISABLE_LRA_SNB));
	}

	if (IS_GEN_RANGE(dev_priv, 6, 7))
		I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_FORCE_ORDERING));

	return xcs_resume(engine);
}

static void cancel_requests(struct intel_engine_cs *engine)
{
	struct i915_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->active.lock, flags);

	/* Mark all submitted requests as skipped. */
	list_for_each_entry(request, &engine->active.requests, sched.link) {
		if (!i915_request_signaled(request))
			dma_fence_set_error(&request->fence, -EIO);

		i915_request_mark_complete(request);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void i9xx_submit_request(struct i915_request *request)
{
	i915_request_submit(request);
	wmb(); /* paranoid flush writes out of the WCB before mmio */

	ENGINE_WRITE(request->engine, RING_TAIL,
		     intel_ring_set_tail(request->ring, request->tail));
}

static u32 *i9xx_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	GEM_BUG_ON(i915_request_active_timeline(rq)->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(i915_request_active_timeline(rq)->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH;

	*cs++ = MI_STORE_DWORD_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

#define GEN5_WA_STORES 8 /* must be at least 1! */
static u32 *gen5_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	int i;

	GEM_BUG_ON(i915_request_active_timeline(rq)->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(i915_request_active_timeline(rq)->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH;

	BUILD_BUG_ON(GEN5_WA_STORES < 1);
	for (i = 0; i < GEN5_WA_STORES; i++) {
		*cs++ = MI_STORE_DWORD_INDEX;
		*cs++ = I915_GEM_HWS_SEQNO_ADDR;
		*cs++ = rq->fence.seqno;
	}

	*cs++ = MI_USER_INTERRUPT;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}
#undef GEN5_WA_STORES

static void
gen5_irq_enable(struct intel_engine_cs *engine)
{
	gen5_gt_enable_irq(engine->gt, engine->irq_enable_mask);
}

static void
gen5_irq_disable(struct intel_engine_cs *engine)
{
	gen5_gt_disable_irq(engine->gt, engine->irq_enable_mask);
}

static void
i9xx_irq_enable(struct intel_engine_cs *engine)
{
	engine->i915->irq_mask &= ~engine->irq_enable_mask;
	intel_uncore_write(engine->uncore, GEN2_IMR, engine->i915->irq_mask);
	intel_uncore_posting_read_fw(engine->uncore, GEN2_IMR);
}

static void
i9xx_irq_disable(struct intel_engine_cs *engine)
{
	engine->i915->irq_mask |= engine->irq_enable_mask;
	intel_uncore_write(engine->uncore, GEN2_IMR, engine->i915->irq_mask);
}

static void
i8xx_irq_enable(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	i915->irq_mask &= ~engine->irq_enable_mask;
	intel_uncore_write16(&i915->uncore, GEN2_IMR, i915->irq_mask);
	ENGINE_POSTING_READ16(engine, RING_IMR);
}

static void
i8xx_irq_disable(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	i915->irq_mask |= engine->irq_enable_mask;
	intel_uncore_write16(&i915->uncore, GEN2_IMR, i915->irq_mask);
}

static int
bsd_ring_flush(struct i915_request *rq, u32 mode)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_FLUSH;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);
	return 0;
}

static void
gen6_irq_enable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR,
		     ~(engine->irq_enable_mask | engine->irq_keep_mask));

	/* Flush/delay to ensure the RING_IMR is active before the GT IMR */
	ENGINE_POSTING_READ(engine, RING_IMR);

	gen5_gt_enable_irq(engine->gt, engine->irq_enable_mask);
}

static void
gen6_irq_disable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~engine->irq_keep_mask);
	gen5_gt_disable_irq(engine->gt, engine->irq_enable_mask);
}

static void
hsw_vebox_irq_enable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~engine->irq_enable_mask);

	/* Flush/delay to ensure the RING_IMR is active before the GT IMR */
	ENGINE_POSTING_READ(engine, RING_IMR);

	gen6_gt_pm_unmask_irq(engine->gt, engine->irq_enable_mask);
}

static void
hsw_vebox_irq_disable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~0);
	gen6_gt_pm_mask_irq(engine->gt, engine->irq_enable_mask);
}

static int
i965_emit_bb_start(struct i915_request *rq,
		   u64 offset, u32 length,
		   unsigned int dispatch_flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT | (dispatch_flags &
		I915_DISPATCH_SECURE ? 0 : MI_BATCH_NON_SECURE_I965);
	*cs++ = offset;
	intel_ring_advance(rq, cs);

	return 0;
}

/* Just userspace ABI convention to limit the wa batch bo to a resonable size */
#define I830_BATCH_LIMIT SZ_256K
#define I830_TLB_ENTRIES (2)
#define I830_WA_SIZE max(I830_TLB_ENTRIES*4096, I830_BATCH_LIMIT)
static int
i830_emit_bb_start(struct i915_request *rq,
		   u64 offset, u32 len,
		   unsigned int dispatch_flags)
{
	u32 *cs, cs_offset =
		intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_DEFAULT);

	GEM_BUG_ON(rq->engine->gt->scratch->size < I830_WA_SIZE);

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Evict the invalid PTE TLBs */
	*cs++ = COLOR_BLT_CMD | BLT_WRITE_RGBA;
	*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | 4096;
	*cs++ = I830_TLB_ENTRIES << 16 | 4; /* load each page */
	*cs++ = cs_offset;
	*cs++ = 0xdeadbeef;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	if ((dispatch_flags & I915_DISPATCH_PINNED) == 0) {
		if (len > I830_BATCH_LIMIT)
			return -ENOSPC;

		cs = intel_ring_begin(rq, 6 + 2);
		if (IS_ERR(cs))
			return PTR_ERR(cs);

		/* Blit the batch (which has now all relocs applied) to the
		 * stable batch scratch bo area (so that the CS never
		 * stumbles over its tlb invalidation bug) ...
		 */
		*cs++ = SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | 4096;
		*cs++ = DIV_ROUND_UP(len, 4096) << 16 | 4096;
		*cs++ = cs_offset;
		*cs++ = 4096;
		*cs++ = offset;

		*cs++ = MI_FLUSH;
		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);

		/* ... and execute it. */
		offset = cs_offset;
	}

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
	*cs++ = offset | (dispatch_flags & I915_DISPATCH_SECURE ? 0 :
		MI_BATCH_NON_SECURE);
	intel_ring_advance(rq, cs);

	return 0;
}

static int
i915_emit_bb_start(struct i915_request *rq,
		   u64 offset, u32 len,
		   unsigned int dispatch_flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
	*cs++ = offset | (dispatch_flags & I915_DISPATCH_SECURE ? 0 :
		MI_BATCH_NON_SECURE);
	intel_ring_advance(rq, cs);

	return 0;
}

static void __ring_context_fini(struct intel_context *ce)
{
	i915_vma_put(ce->state);
}

static void ring_context_destroy(struct kref *ref)
{
	struct intel_context *ce = container_of(ref, typeof(*ce), ref);

	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (ce->state)
		__ring_context_fini(ce);

	intel_context_fini(ce);
	intel_context_free(ce);
}

static struct i915_address_space *vm_alias(struct intel_context *ce)
{
	struct i915_address_space *vm;

	vm = ce->vm;
	if (i915_is_ggtt(vm))
		vm = &i915_vm_to_ggtt(vm)->alias->vm;

	return vm;
}

static int __context_pin_ppgtt(struct intel_context *ce)
{
	struct i915_address_space *vm;
	int err = 0;

	vm = vm_alias(ce);
	if (vm)
		err = gen6_ppgtt_pin(i915_vm_to_ppgtt((vm)));

	return err;
}

static void __context_unpin_ppgtt(struct intel_context *ce)
{
	struct i915_address_space *vm;

	vm = vm_alias(ce);
	if (vm)
		gen6_ppgtt_unpin(i915_vm_to_ppgtt(vm));
}

static void ring_context_unpin(struct intel_context *ce)
{
	__context_unpin_ppgtt(ce);
}

static struct i915_vma *
alloc_context_vma(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_shmem(i915, engine->context_size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	/*
	 * Try to make the context utilize L3 as well as LLC.
	 *
	 * On VLV we don't have L3 controls in the PTEs so we
	 * shouldn't touch the cache level, especially as that
	 * would make the object snooped which might have a
	 * negative performance impact.
	 *
	 * Snooping is required on non-llc platforms in execlist
	 * mode, but since all GGTT accesses use PAT entry 0 we
	 * get snooping anyway regardless of cache_level.
	 *
	 * This is only applicable for Ivy Bridge devices since
	 * later platforms don't have L3 control bits in the PTE.
	 */
	if (IS_IVYBRIDGE(i915))
		i915_gem_object_set_cache_coherency(obj, I915_CACHE_L3_LLC);

	if (engine->default_state) {
		void *defaults, *vaddr;

		vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_obj;
		}

		defaults = i915_gem_object_pin_map(engine->default_state,
						   I915_MAP_WB);
		if (IS_ERR(defaults)) {
			err = PTR_ERR(defaults);
			goto err_map;
		}

		memcpy(vaddr, defaults, engine->context_size);
		i915_gem_object_unpin_map(engine->default_state);

		i915_gem_object_flush_map(obj);
		i915_gem_object_unpin_map(obj);
	}

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	return vma;

err_map:
	i915_gem_object_unpin_map(obj);
err_obj:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static int ring_context_alloc(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;

	/* One ringbuffer to rule them all */
	GEM_BUG_ON(!engine->legacy.ring);
	ce->ring = engine->legacy.ring;
	ce->timeline = intel_timeline_get(engine->legacy.timeline);

	GEM_BUG_ON(ce->state);
	if (engine->context_size) {
		struct i915_vma *vma;

		vma = alloc_context_vma(engine);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		ce->state = vma;
	}

	return 0;
}

static int ring_context_pin(struct intel_context *ce)
{
	int err;

	err = intel_context_active_acquire(ce);
	if (err)
		return err;

	err = __context_pin_ppgtt(ce);
	if (err)
		goto err_active;

	return 0;

err_active:
	intel_context_active_release(ce);
	return err;
}

static void ring_context_reset(struct intel_context *ce)
{
	intel_ring_reset(ce->ring, 0);
}

static const struct intel_context_ops ring_context_ops = {
	.alloc = ring_context_alloc,

	.pin = ring_context_pin,
	.unpin = ring_context_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.reset = ring_context_reset,
	.destroy = ring_context_destroy,
};

static int load_pd_dir(struct i915_request *rq, const struct i915_ppgtt *ppgtt)
{
	const struct intel_engine_cs * const engine = rq->engine;
	u32 *cs;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_DCLV(engine->mmio_base));
	*cs++ = PP_DIR_DCLV_2G;

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine->mmio_base));
	*cs++ = px_base(ppgtt->pd)->ggtt_offset << 10;

	intel_ring_advance(rq, cs);

	return 0;
}

static int flush_pd_dir(struct i915_request *rq)
{
	const struct intel_engine_cs * const engine = rq->engine;
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Stall until the page table load is complete */
	*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine->mmio_base));
	*cs++ = intel_gt_scratch_offset(rq->engine->gt,
					INTEL_GT_SCRATCH_FIELD_DEFAULT);
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);
	return 0;
}

static inline int mi_set_context(struct i915_request *rq, u32 flags)
{
	struct drm_i915_private *i915 = rq->i915;
	struct intel_engine_cs *engine = rq->engine;
	enum intel_engine_id id;
	const int num_engines =
		IS_HASWELL(i915) ? RUNTIME_INFO(i915)->num_engines - 1 : 0;
	bool force_restore = false;
	int len;
	u32 *cs;

	len = 4;
	if (IS_GEN(i915, 7))
		len += 2 + (num_engines ? 4 * num_engines + 6 : 0);
	else if (IS_GEN(i915, 5))
		len += 2;
	if (flags & MI_FORCE_RESTORE) {
		GEM_BUG_ON(flags & MI_RESTORE_INHIBIT);
		flags &= ~MI_FORCE_RESTORE;
		force_restore = true;
		len += 2;
	}

	cs = intel_ring_begin(rq, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* WaProgramMiArbOnOffAroundMiSetContext:ivb,vlv,hsw,bdw,chv */
	if (IS_GEN(i915, 7)) {
		*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
		if (num_engines) {
			struct intel_engine_cs *signaller;

			*cs++ = MI_LOAD_REGISTER_IMM(num_engines);
			for_each_engine(signaller, engine->gt, id) {
				if (signaller == engine)
					continue;

				*cs++ = i915_mmio_reg_offset(
					   RING_PSMI_CTL(signaller->mmio_base));
				*cs++ = _MASKED_BIT_ENABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}
		}
	} else if (IS_GEN(i915, 5)) {
		/*
		 * This w/a is only listed for pre-production ilk a/b steppings,
		 * but is also mentioned for programming the powerctx. To be
		 * safe, just apply the workaround; we do not use SyncFlush so
		 * this should never take effect and so be a no-op!
		 */
		*cs++ = MI_SUSPEND_FLUSH | MI_SUSPEND_FLUSH_EN;
	}

	if (force_restore) {
		/*
		 * The HW doesn't handle being told to restore the current
		 * context very well. Quite often it likes goes to go off and
		 * sulk, especially when it is meant to be reloading PP_DIR.
		 * A very simple fix to force the reload is to simply switch
		 * away from the current context and back again.
		 *
		 * Note that the kernel_context will contain random state
		 * following the INHIBIT_RESTORE. We accept this since we
		 * never use the kernel_context state; it is merely a
		 * placeholder we use to flush other contexts.
		 */
		*cs++ = MI_SET_CONTEXT;
		*cs++ = i915_ggtt_offset(engine->kernel_context->state) |
			MI_MM_SPACE_GTT |
			MI_RESTORE_INHIBIT;
	}

	*cs++ = MI_NOOP;
	*cs++ = MI_SET_CONTEXT;
	*cs++ = i915_ggtt_offset(rq->hw_context->state) | flags;
	/*
	 * w/a: MI_SET_CONTEXT must always be followed by MI_NOOP
	 * WaMiSetContext_Hang:snb,ivb,vlv
	 */
	*cs++ = MI_NOOP;

	if (IS_GEN(i915, 7)) {
		if (num_engines) {
			struct intel_engine_cs *signaller;
			i915_reg_t last_reg = {}; /* keep gcc quiet */

			*cs++ = MI_LOAD_REGISTER_IMM(num_engines);
			for_each_engine(signaller, engine->gt, id) {
				if (signaller == engine)
					continue;

				last_reg = RING_PSMI_CTL(signaller->mmio_base);
				*cs++ = i915_mmio_reg_offset(last_reg);
				*cs++ = _MASKED_BIT_DISABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}

			/* Insert a delay before the next switch! */
			*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
			*cs++ = i915_mmio_reg_offset(last_reg);
			*cs++ = intel_gt_scratch_offset(engine->gt,
							INTEL_GT_SCRATCH_FIELD_DEFAULT);
			*cs++ = MI_NOOP;
		}
		*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	} else if (IS_GEN(i915, 5)) {
		*cs++ = MI_SUSPEND_FLUSH;
	}

	intel_ring_advance(rq, cs);

	return 0;
}

static int remap_l3_slice(struct i915_request *rq, int slice)
{
	u32 *cs, *remap_info = rq->i915->l3_parity.remap_info[slice];
	int i;

	if (!remap_info)
		return 0;

	cs = intel_ring_begin(rq, GEN7_L3LOG_SIZE/4 * 2 + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * Note: We do not worry about the concurrent register cacheline hang
	 * here because no other code should access these registers other than
	 * at initialization time.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(GEN7_L3LOG_SIZE/4);
	for (i = 0; i < GEN7_L3LOG_SIZE/4; i++) {
		*cs++ = i915_mmio_reg_offset(GEN7_L3LOG(slice, i));
		*cs++ = remap_info[i];
	}
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
}

static int remap_l3(struct i915_request *rq)
{
	struct i915_gem_context *ctx = rq->gem_context;
	int i, err;

	if (!ctx->remap_slice)
		return 0;

	for (i = 0; i < MAX_L3_SLICES; i++) {
		if (!(ctx->remap_slice & BIT(i)))
			continue;

		err = remap_l3_slice(rq, i);
		if (err)
			return err;
	}

	ctx->remap_slice = 0;
	return 0;
}

static int switch_context(struct i915_request *rq)
{
	struct intel_context *ce = rq->hw_context;
	struct i915_address_space *vm = vm_alias(ce);
	int ret;

	GEM_BUG_ON(HAS_EXECLISTS(rq->i915));

	if (vm) {
		ret = load_pd_dir(rq, i915_vm_to_ppgtt(vm));
		if (ret)
			return ret;
	}

	if (ce->state) {
		u32 flags;

		GEM_BUG_ON(rq->engine->id != RCS0);

		/* For resource streamer on HSW+ and power context elsewhere */
		BUILD_BUG_ON(HSW_MI_RS_SAVE_STATE_EN != MI_SAVE_EXT_STATE_EN);
		BUILD_BUG_ON(HSW_MI_RS_RESTORE_STATE_EN != MI_RESTORE_EXT_STATE_EN);

		flags = MI_SAVE_EXT_STATE_EN | MI_MM_SPACE_GTT;
		if (!i915_gem_context_is_kernel(rq->gem_context))
			flags |= MI_RESTORE_EXT_STATE_EN;
		else
			flags |= MI_RESTORE_INHIBIT;

		ret = mi_set_context(rq, flags);
		if (ret)
			return ret;
	}

	if (vm) {
		struct intel_engine_cs *engine = rq->engine;

		ret = engine->emit_flush(rq, EMIT_INVALIDATE);
		if (ret)
			return ret;

		ret = flush_pd_dir(rq);
		if (ret)
			return ret;

		/*
		 * Not only do we need a full barrier (post-sync write) after
		 * invalidating the TLBs, but we need to wait a little bit
		 * longer. Whether this is merely delaying us, or the
		 * subsequent flush is a key part of serialising with the
		 * post-sync op, this extra pass appears vital before a
		 * mm switch!
		 */
		ret = engine->emit_flush(rq, EMIT_INVALIDATE);
		if (ret)
			return ret;

		ret = engine->emit_flush(rq, EMIT_FLUSH);
		if (ret)
			return ret;
	}

	ret = remap_l3(rq);
	if (ret)
		return ret;

	return 0;
}

static int ring_request_alloc(struct i915_request *request)
{
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(request->hw_context));
	GEM_BUG_ON(i915_request_timeline(request)->has_initial_breadcrumb);

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	request->reserved_space += LEGACY_REQUEST_SIZE;

	/* Unconditionally invalidate GPU caches and TLBs. */
	ret = request->engine->emit_flush(request, EMIT_INVALIDATE);
	if (ret)
		return ret;

	ret = switch_context(request);
	if (ret)
		return ret;

	request->reserved_space -= LEGACY_REQUEST_SIZE;
	return 0;
}

static void gen6_bsd_submit_request(struct i915_request *request)
{
	struct intel_uncore *uncore = request->engine->uncore;

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

       /* Every tail move must follow the sequence below */

	/* Disable notification that the ring is IDLE. The GT
	 * will then assume that it is busy and bring it out of rc6.
	 */
	intel_uncore_write_fw(uncore, GEN6_BSD_SLEEP_PSMI_CONTROL,
			      _MASKED_BIT_ENABLE(GEN6_BSD_SLEEP_MSG_DISABLE));

	/* Clear the context id. Here be magic! */
	intel_uncore_write64_fw(uncore, GEN6_BSD_RNCID, 0x0);

	/* Wait for the ring not to be idle, i.e. for it to wake up. */
	if (__intel_wait_for_register_fw(uncore,
					 GEN6_BSD_SLEEP_PSMI_CONTROL,
					 GEN6_BSD_SLEEP_INDICATOR,
					 0,
					 1000, 0, NULL))
		DRM_ERROR("timed out waiting for the BSD ring to wake up\n");

	/* Now that the ring is fully powered up, update the tail */
	i9xx_submit_request(request);

	/* Let the ring send IDLE messages to the GT again,
	 * and so let it sleep to conserve power when idle.
	 */
	intel_uncore_write_fw(uncore, GEN6_BSD_SLEEP_PSMI_CONTROL,
			      _MASKED_BIT_DISABLE(GEN6_BSD_SLEEP_MSG_DISABLE));

	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
}

static int mi_flush_dw(struct i915_request *rq, u32 flags)
{
	u32 cmd, *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	cmd = MI_FLUSH_DW;

	/*
	 * We always require a command barrier so that subsequent
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
	cmd |= flags;

	*cs++ = cmd;
	*cs++ = I915_GEM_HWS_SCRATCH_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = 0;
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	return 0;
}

static int gen6_flush_dw(struct i915_request *rq, u32 mode, u32 invflags)
{
	return mi_flush_dw(rq, mode & EMIT_INVALIDATE ? invflags : 0);
}

static int gen6_bsd_ring_flush(struct i915_request *rq, u32 mode)
{
	return gen6_flush_dw(rq, mode, MI_INVALIDATE_TLB | MI_INVALIDATE_BSD);
}

static int
hsw_emit_bb_start(struct i915_request *rq,
		  u64 offset, u32 len,
		  unsigned int dispatch_flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_BATCH_BUFFER_START | (dispatch_flags & I915_DISPATCH_SECURE ?
		0 : MI_BATCH_PPGTT_HSW | MI_BATCH_NON_SECURE_HSW);
	/* bit0-7 is the length on GEN6+ */
	*cs++ = offset;
	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen6_emit_bb_start(struct i915_request *rq,
		   u64 offset, u32 len,
		   unsigned int dispatch_flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_BATCH_BUFFER_START | (dispatch_flags & I915_DISPATCH_SECURE ?
		0 : MI_BATCH_NON_SECURE_I965);
	/* bit0-7 is the length on GEN6+ */
	*cs++ = offset;
	intel_ring_advance(rq, cs);

	return 0;
}

/* Blitter support (SandyBridge+) */

static int gen6_ring_flush(struct i915_request *rq, u32 mode)
{
	return gen6_flush_dw(rq, mode, MI_INVALIDATE_TLB);
}

static void i9xx_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = i9xx_submit_request;
	engine->cancel_requests = cancel_requests;

	engine->park = NULL;
	engine->unpark = NULL;
}

static void gen6_bsd_set_default_submission(struct intel_engine_cs *engine)
{
	i9xx_set_default_submission(engine);
	engine->submit_request = gen6_bsd_submit_request;
}

static void ring_destroy(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	WARN_ON(INTEL_GEN(dev_priv) > 2 &&
		(ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE) == 0);

	intel_engine_cleanup_common(engine);

	intel_ring_unpin(engine->legacy.ring);
	intel_ring_put(engine->legacy.ring);

	intel_timeline_unpin(engine->legacy.timeline);
	intel_timeline_put(engine->legacy.timeline);

	kfree(engine);
}

static void setup_irq(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (INTEL_GEN(i915) >= 6) {
		engine->irq_enable = gen6_irq_enable;
		engine->irq_disable = gen6_irq_disable;
	} else if (INTEL_GEN(i915) >= 5) {
		engine->irq_enable = gen5_irq_enable;
		engine->irq_disable = gen5_irq_disable;
	} else if (INTEL_GEN(i915) >= 3) {
		engine->irq_enable = i9xx_irq_enable;
		engine->irq_disable = i9xx_irq_disable;
	} else {
		engine->irq_enable = i8xx_irq_enable;
		engine->irq_disable = i8xx_irq_disable;
	}
}

static void setup_common(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	/* gen8+ are only supported with execlists */
	GEM_BUG_ON(INTEL_GEN(i915) >= 8);

	setup_irq(engine);

	engine->destroy = ring_destroy;

	engine->resume = xcs_resume;
	engine->reset.prepare = reset_prepare;
	engine->reset.reset = reset_ring;
	engine->reset.finish = reset_finish;

	engine->cops = &ring_context_ops;
	engine->request_alloc = ring_request_alloc;

	/*
	 * Using a global execution timeline; the previous final breadcrumb is
	 * equivalent to our next initial bread so we can elide
	 * engine->emit_init_breadcrumb().
	 */
	engine->emit_fini_breadcrumb = i9xx_emit_breadcrumb;
	if (IS_GEN(i915, 5))
		engine->emit_fini_breadcrumb = gen5_emit_breadcrumb;

	engine->set_default_submission = i9xx_set_default_submission;

	if (INTEL_GEN(i915) >= 6)
		engine->emit_bb_start = gen6_emit_bb_start;
	else if (INTEL_GEN(i915) >= 4)
		engine->emit_bb_start = i965_emit_bb_start;
	else if (IS_I830(i915) || IS_I845G(i915))
		engine->emit_bb_start = i830_emit_bb_start;
	else
		engine->emit_bb_start = i915_emit_bb_start;
}

static void setup_rcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (HAS_L3_DPF(i915))
		engine->irq_keep_mask = GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	engine->irq_enable_mask = GT_RENDER_USER_INTERRUPT;

	if (INTEL_GEN(i915) >= 7) {
		engine->emit_flush = gen7_render_ring_flush;
		engine->emit_fini_breadcrumb = gen7_rcs_emit_breadcrumb;
	} else if (IS_GEN(i915, 6)) {
		engine->emit_flush = gen6_render_ring_flush;
		engine->emit_fini_breadcrumb = gen6_rcs_emit_breadcrumb;
	} else if (IS_GEN(i915, 5)) {
		engine->emit_flush = gen4_render_ring_flush;
	} else {
		if (INTEL_GEN(i915) < 4)
			engine->emit_flush = gen2_render_ring_flush;
		else
			engine->emit_flush = gen4_render_ring_flush;
		engine->irq_enable_mask = I915_USER_INTERRUPT;
	}

	if (IS_HASWELL(i915))
		engine->emit_bb_start = hsw_emit_bb_start;

	engine->resume = rcs_resume;
}

static void setup_vcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (INTEL_GEN(i915) >= 6) {
		/* gen6 bsd needs a special wa for tail updates */
		if (IS_GEN(i915, 6))
			engine->set_default_submission = gen6_bsd_set_default_submission;
		engine->emit_flush = gen6_bsd_ring_flush;
		engine->irq_enable_mask = GT_BSD_USER_INTERRUPT;

		if (IS_GEN(i915, 6))
			engine->emit_fini_breadcrumb = gen6_xcs_emit_breadcrumb;
		else
			engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;
	} else {
		engine->emit_flush = bsd_ring_flush;
		if (IS_GEN(i915, 5))
			engine->irq_enable_mask = ILK_BSD_USER_INTERRUPT;
		else
			engine->irq_enable_mask = I915_BSD_USER_INTERRUPT;
	}
}

static void setup_bcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	engine->emit_flush = gen6_ring_flush;
	engine->irq_enable_mask = GT_BLT_USER_INTERRUPT;

	if (IS_GEN(i915, 6))
		engine->emit_fini_breadcrumb = gen6_xcs_emit_breadcrumb;
	else
		engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;
}

static void setup_vecs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	GEM_BUG_ON(INTEL_GEN(i915) < 7);

	engine->emit_flush = gen6_ring_flush;
	engine->irq_enable_mask = PM_VEBOX_USER_INTERRUPT;
	engine->irq_enable = hsw_vebox_irq_enable;
	engine->irq_disable = hsw_vebox_irq_disable;

	engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;
}

int intel_ring_submission_setup(struct intel_engine_cs *engine)
{
	setup_common(engine);

	switch (engine->class) {
	case RENDER_CLASS:
		setup_rcs(engine);
		break;
	case VIDEO_DECODE_CLASS:
		setup_vcs(engine);
		break;
	case COPY_ENGINE_CLASS:
		setup_bcs(engine);
		break;
	case VIDEO_ENHANCEMENT_CLASS:
		setup_vecs(engine);
		break;
	default:
		MISSING_CASE(engine->class);
		return -ENODEV;
	}

	return 0;
}

int intel_ring_submission_init(struct intel_engine_cs *engine)
{
	struct intel_timeline *timeline;
	struct intel_ring *ring;
	int err;

	timeline = intel_timeline_create(engine->gt, engine->status_page.vma);
	if (IS_ERR(timeline)) {
		err = PTR_ERR(timeline);
		goto err;
	}
	GEM_BUG_ON(timeline->has_initial_breadcrumb);

	err = intel_timeline_pin(timeline);
	if (err)
		goto err_timeline;

	ring = intel_engine_create_ring(engine, SZ_16K);
	if (IS_ERR(ring)) {
		err = PTR_ERR(ring);
		goto err_timeline_unpin;
	}

	err = intel_ring_pin(ring);
	if (err)
		goto err_ring;

	GEM_BUG_ON(engine->legacy.ring);
	engine->legacy.ring = ring;
	engine->legacy.timeline = timeline;

	err = intel_engine_init_common(engine);
	if (err)
		goto err_ring_unpin;

	GEM_BUG_ON(timeline->hwsp_ggtt != engine->status_page.vma);

	return 0;

err_ring_unpin:
	intel_ring_unpin(ring);
err_ring:
	intel_ring_put(ring);
err_timeline_unpin:
	intel_timeline_unpin(timeline);
err_timeline:
	intel_timeline_put(timeline);
err:
	intel_engine_cleanup_common(engine);
	return err;
}
