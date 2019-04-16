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

#include "i915_drv.h"
#include "i915_gem_render_state.h"
#include "i915_reset.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_workarounds.h"

/* Rough estimate of the typical request size, performing a flush,
 * set-context and then emitting the batch.
 */
#define LEGACY_REQUEST_SIZE 200

unsigned int intel_ring_update_space(struct intel_ring *ring)
{
	unsigned int space;

	space = __intel_ring_space(ring->head, ring->emit, ring->size);

	ring->space = space;
	return space;
}

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
		*cs++ = i915_scratch_offset(rq->i915);
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
		*cs++ = i915_scratch_offset(rq->i915) | PIPE_CONTROL_GLOBAL_GTT;
		*cs++ = 0;
		*cs++ = 0;

		for (i = 0; i < 12; i++)
			*cs++ = MI_FLUSH;

		*cs++ = GFX_OP_PIPE_CONTROL(4) | PIPE_CONTROL_QW_WRITE;
		*cs++ = i915_scratch_offset(rq->i915) | PIPE_CONTROL_GLOBAL_GTT;
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
	u32 scratch_addr = i915_scratch_offset(rq->i915) + 2 * CACHELINE_BYTES;
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
	u32 scratch_addr = i915_scratch_offset(rq->i915) + 2 * CACHELINE_BYTES;
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
	*cs++ = i915_scratch_offset(rq->i915) | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = 0;

	/* Finally we can flush and with it emit the breadcrumb */
	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = (PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
		 PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		 PIPE_CONTROL_DC_FLUSH_ENABLE |
		 PIPE_CONTROL_QW_WRITE |
		 PIPE_CONTROL_CS_STALL);
	*cs++ = rq->timeline->hwsp_offset | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = rq->fence.seqno;

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_QW_WRITE | PIPE_CONTROL_STORE_DATA_INDEX;
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR | PIPE_CONTROL_GLOBAL_GTT;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

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
	u32 scratch_addr = i915_scratch_offset(rq->i915) + 2 * CACHELINE_BYTES;
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
	*cs++ = rq->timeline->hwsp_offset;
	*cs++ = rq->fence.seqno;

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = (PIPE_CONTROL_QW_WRITE |
		 PIPE_CONTROL_STORE_DATA_INDEX |
		 PIPE_CONTROL_GLOBAL_GTT_IVB);
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

static u32 *gen6_xcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	GEM_BUG_ON(rq->timeline->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(rq->timeline->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

#define GEN7_XCS_WA 32
static u32 *gen7_xcs_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	int i;

	GEM_BUG_ON(rq->timeline->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(rq->timeline->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

	for (i = 0; i < GEN7_XCS_WA; i++) {
		*cs++ = MI_STORE_DWORD_INDEX;
		*cs++ = I915_GEM_HWS_SEQNO_ADDR;
		*cs++ = rq->fence.seqno;
	}

	*cs++ = MI_FLUSH_DW;
	*cs++ = 0;
	*cs++ = 0;

	*cs++ = MI_USER_INTERRUPT;

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

static int init_ring_common(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_ring *ring = engine->buffer;
	int ret = 0;

	intel_uncore_forcewake_get(engine->uncore, FORCEWAKE_ALL);

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
	ENGINE_READ(engine, RING_HEAD);

	/* Initialize the ring. This must happen _after_ we've cleared the ring
	 * registers with the above sequence (the readback of the HEAD registers
	 * also enforces ordering), otherwise the hw might lose the new ring
	 * register values. */
	ENGINE_WRITE(engine, RING_START, i915_ggtt_offset(ring->vma));

	/* WaClearRingBufHeadRegAtInit:ctg,elk */
	if (ENGINE_READ(engine, RING_HEAD))
		DRM_DEBUG_DRIVER("%s initialization failed [head=%08x], fudging\n",
				 engine->name, ENGINE_READ(engine, RING_HEAD));

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
	intel_engine_stop_cs(engine);
}

static void reset_ring(struct intel_engine_cs *engine, bool stalled)
{
	struct i915_timeline *tl = &engine->timeline;
	struct i915_request *pos, *rq;
	unsigned long flags;
	u32 head;

	rq = NULL;
	spin_lock_irqsave(&tl->lock, flags);
	list_for_each_entry(pos, &tl->requests, link) {
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
		i915_reset_request(rq, stalled);

		GEM_BUG_ON(rq->ring != engine->buffer);
		head = rq->head;
	} else {
		head = engine->buffer->tail;
	}
	engine->buffer->head = intel_ring_wrap(engine->buffer, head);

	spin_unlock_irqrestore(&tl->lock, flags);
}

static void reset_finish(struct intel_engine_cs *engine)
{
}

static int intel_rcs_ctx_init(struct i915_request *rq)
{
	int ret;

	ret = intel_engine_emit_ctx_wa(rq);
	if (ret != 0)
		return ret;

	ret = i915_gem_render_state_emit(rq);
	if (ret)
		return ret;

	return 0;
}

static int init_render_ring(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	int ret = init_ring_common(engine);
	if (ret)
		return ret;

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

	if (INTEL_GEN(dev_priv) >= 6)
		ENGINE_WRITE(engine, RING_IMR, ~engine->irq_keep_mask);

	return 0;
}

static void cancel_requests(struct intel_engine_cs *engine)
{
	struct i915_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->timeline.lock, flags);

	/* Mark all submitted requests as skipped. */
	list_for_each_entry(request, &engine->timeline.requests, link) {
		if (!i915_request_signaled(request))
			dma_fence_set_error(&request->fence, -EIO);

		i915_request_mark_complete(request);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

static void i9xx_submit_request(struct i915_request *request)
{
	i915_request_submit(request);

	ENGINE_WRITE(request->engine, RING_TAIL,
		     intel_ring_set_tail(request->ring, request->tail));
}

static u32 *i9xx_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	GEM_BUG_ON(rq->timeline->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(rq->timeline->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH;

	*cs++ = MI_STORE_DWORD_INDEX;
	*cs++ = I915_GEM_HWS_SEQNO_ADDR;
	*cs++ = rq->fence.seqno;

	*cs++ = MI_STORE_DWORD_INDEX;
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

	*cs++ = MI_USER_INTERRUPT;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}

#define GEN5_WA_STORES 8 /* must be at least 1! */
static u32 *gen5_emit_breadcrumb(struct i915_request *rq, u32 *cs)
{
	int i;

	GEM_BUG_ON(rq->timeline->hwsp_ggtt != rq->engine->status_page.vma);
	GEM_BUG_ON(offset_in_page(rq->timeline->hwsp_offset) != I915_GEM_HWS_SEQNO_ADDR);

	*cs++ = MI_FLUSH;

	*cs++ = MI_STORE_DWORD_INDEX;
	*cs++ = I915_GEM_HWS_HANGCHECK_ADDR;
	*cs++ = intel_engine_next_hangcheck_seqno(rq->engine);

	BUILD_BUG_ON(GEN5_WA_STORES < 1);
	for (i = 0; i < GEN5_WA_STORES; i++) {
		*cs++ = MI_STORE_DWORD_INDEX;
		*cs++ = I915_GEM_HWS_SEQNO_ADDR;
		*cs++ = rq->fence.seqno;
	}

	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_NOOP;

	rq->tail = intel_ring_offset(rq, cs);
	assert_ring_tail_valid(rq->ring, rq->tail);

	return cs;
}
#undef GEN5_WA_STORES

static void
gen5_irq_enable(struct intel_engine_cs *engine)
{
	gen5_enable_gt_irq(engine->i915, engine->irq_enable_mask);
}

static void
gen5_irq_disable(struct intel_engine_cs *engine)
{
	gen5_disable_gt_irq(engine->i915, engine->irq_enable_mask);
}

static void
i9xx_irq_enable(struct intel_engine_cs *engine)
{
	engine->i915->irq_mask &= ~engine->irq_enable_mask;
	intel_uncore_write(engine->uncore, IMR, engine->i915->irq_mask);
	intel_uncore_posting_read_fw(engine->uncore, IMR);
}

static void
i9xx_irq_disable(struct intel_engine_cs *engine)
{
	engine->i915->irq_mask |= engine->irq_enable_mask;
	intel_uncore_write(engine->uncore, IMR, engine->i915->irq_mask);
}

static void
i8xx_irq_enable(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	dev_priv->irq_mask &= ~engine->irq_enable_mask;
	I915_WRITE16(IMR, dev_priv->irq_mask);
	POSTING_READ16(RING_IMR(engine->mmio_base));
}

static void
i8xx_irq_disable(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	dev_priv->irq_mask |= engine->irq_enable_mask;
	I915_WRITE16(IMR, dev_priv->irq_mask);
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

	gen5_enable_gt_irq(engine->i915, engine->irq_enable_mask);
}

static void
gen6_irq_disable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~engine->irq_keep_mask);
	gen5_disable_gt_irq(engine->i915, engine->irq_enable_mask);
}

static void
hsw_vebox_irq_enable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~engine->irq_enable_mask);

	/* Flush/delay to ensure the RING_IMR is active before the GT IMR */
	ENGINE_POSTING_READ(engine, RING_IMR);

	gen6_unmask_pm_irq(engine->i915, engine->irq_enable_mask);
}

static void
hsw_vebox_irq_disable(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~0);
	gen6_mask_pm_irq(engine->i915, engine->irq_enable_mask);
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
	u32 *cs, cs_offset = i915_scratch_offset(rq->i915);

	GEM_BUG_ON(rq->i915->gt.scratch->size < I830_WA_SIZE);

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
		*cs++ = SRC_COPY_BLT_CMD | BLT_WRITE_RGBA;
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

int intel_ring_pin(struct intel_ring *ring)
{
	struct i915_vma *vma = ring->vma;
	enum i915_map_type map = i915_coherent_map_type(vma->vm->i915);
	unsigned int flags;
	void *addr;
	int ret;

	GEM_BUG_ON(ring->vaddr);

	ret = i915_timeline_pin(ring->timeline);
	if (ret)
		return ret;

	flags = PIN_GLOBAL;

	/* Ring wraparound at offset 0 sometimes hangs. No idea why. */
	flags |= PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);

	if (vma->obj->stolen)
		flags |= PIN_MAPPABLE;
	else
		flags |= PIN_HIGH;

	ret = i915_vma_pin(vma, 0, 0, flags);
	if (unlikely(ret))
		goto unpin_timeline;

	if (i915_vma_is_map_and_fenceable(vma))
		addr = (void __force *)i915_vma_pin_iomap(vma);
	else
		addr = i915_gem_object_pin_map(vma->obj, map);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto unpin_ring;
	}

	vma->obj->pin_global++;

	ring->vaddr = addr;
	return 0;

unpin_ring:
	i915_vma_unpin(vma);
unpin_timeline:
	i915_timeline_unpin(ring->timeline);
	return ret;
}

void intel_ring_reset(struct intel_ring *ring, u32 tail)
{
	GEM_BUG_ON(!intel_ring_offset_valid(ring, tail));

	ring->tail = tail;
	ring->head = tail;
	ring->emit = tail;
	intel_ring_update_space(ring);
}

void intel_ring_unpin(struct intel_ring *ring)
{
	GEM_BUG_ON(!ring->vma);
	GEM_BUG_ON(!ring->vaddr);

	/* Discard any unused bytes beyond that submitted to hw. */
	intel_ring_reset(ring, ring->tail);

	if (i915_vma_is_map_and_fenceable(ring->vma))
		i915_vma_unpin_iomap(ring->vma);
	else
		i915_gem_object_unpin_map(ring->vma->obj);
	ring->vaddr = NULL;

	ring->vma->obj->pin_global--;
	i915_vma_unpin(ring->vma);

	i915_timeline_unpin(ring->timeline);
}

static struct i915_vma *
intel_ring_create_vma(struct drm_i915_private *dev_priv, int size)
{
	struct i915_address_space *vm = &dev_priv->ggtt.vm;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_stolen(dev_priv, size);
	if (!obj)
		obj = i915_gem_object_create_internal(dev_priv, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	/*
	 * Mark ring buffers as read-only from GPU side (so no stray overwrites)
	 * if supported by the platform's GGTT.
	 */
	if (vm->has_read_only)
		i915_gem_object_set_readonly(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

struct intel_ring *
intel_engine_create_ring(struct intel_engine_cs *engine,
			 struct i915_timeline *timeline,
			 int size)
{
	struct intel_ring *ring;
	struct i915_vma *vma;

	GEM_BUG_ON(!is_power_of_2(size));
	GEM_BUG_ON(RING_CTL_SIZE(size) & ~RING_NR_PAGES);
	GEM_BUG_ON(timeline == &engine->timeline);
	lockdep_assert_held(&engine->i915->drm.struct_mutex);

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return ERR_PTR(-ENOMEM);

	kref_init(&ring->ref);
	INIT_LIST_HEAD(&ring->request_list);
	ring->timeline = i915_timeline_get(timeline);

	ring->size = size;
	/* Workaround an erratum on the i830 which causes a hang if
	 * the TAIL pointer points to within the last 2 cachelines
	 * of the buffer.
	 */
	ring->effective_size = size;
	if (IS_I830(engine->i915) || IS_I845G(engine->i915))
		ring->effective_size -= 2 * CACHELINE_BYTES;

	intel_ring_update_space(ring);

	vma = intel_ring_create_vma(engine->i915, size);
	if (IS_ERR(vma)) {
		kfree(ring);
		return ERR_CAST(vma);
	}
	ring->vma = vma;

	return ring;
}

void intel_ring_free(struct kref *ref)
{
	struct intel_ring *ring = container_of(ref, typeof(*ring), ref);
	struct drm_i915_gem_object *obj = ring->vma->obj;

	i915_vma_close(ring->vma);
	__i915_gem_object_release_unless_active(obj);

	i915_timeline_put(ring->timeline);
	kfree(ring);
}

static void __ring_context_fini(struct intel_context *ce)
{
	GEM_BUG_ON(i915_gem_object_is_active(ce->state->obj));
	i915_gem_object_put(ce->state->obj);
}

static void ring_context_destroy(struct kref *ref)
{
	struct intel_context *ce = container_of(ref, typeof(*ce), ref);

	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (ce->state)
		__ring_context_fini(ce);

	intel_context_free(ce);
}

static int __context_pin_ppgtt(struct i915_gem_context *ctx)
{
	struct i915_hw_ppgtt *ppgtt;
	int err = 0;

	ppgtt = ctx->ppgtt ?: ctx->i915->mm.aliasing_ppgtt;
	if (ppgtt)
		err = gen6_ppgtt_pin(ppgtt);

	return err;
}

static void __context_unpin_ppgtt(struct i915_gem_context *ctx)
{
	struct i915_hw_ppgtt *ppgtt;

	ppgtt = ctx->ppgtt ?: ctx->i915->mm.aliasing_ppgtt;
	if (ppgtt)
		gen6_ppgtt_unpin(ppgtt);
}

static int __context_pin(struct intel_context *ce)
{
	struct i915_vma *vma;
	int err;

	vma = ce->state;
	if (!vma)
		return 0;

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		return err;

	/*
	 * And mark is as a globally pinned object to let the shrinker know
	 * it cannot reclaim the object until we release it.
	 */
	vma->obj->pin_global++;
	vma->obj->mm.dirty = true;

	return 0;
}

static void __context_unpin(struct intel_context *ce)
{
	struct i915_vma *vma;

	vma = ce->state;
	if (!vma)
		return;

	vma->obj->pin_global--;
	i915_vma_unpin(vma);
}

static void ring_context_unpin(struct intel_context *ce)
{
	__context_unpin_ppgtt(ce->gem_context);
	__context_unpin(ce);
}

static struct i915_vma *
alloc_context_vma(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create(i915, engine->context_size);
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

	vma = i915_vma_instance(obj, &i915->ggtt.vm, NULL);
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

static int ring_context_pin(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	int err;

	/* One ringbuffer to rule them all */
	GEM_BUG_ON(!engine->buffer);
	ce->ring = engine->buffer;

	if (!ce->state && engine->context_size) {
		struct i915_vma *vma;

		vma = alloc_context_vma(engine);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		ce->state = vma;
	}

	err = __context_pin(ce);
	if (err)
		return err;

	err = __context_pin_ppgtt(ce->gem_context);
	if (err)
		goto err_unpin;

	return 0;

err_unpin:
	__context_unpin(ce);
	return err;
}

static const struct intel_context_ops ring_context_ops = {
	.pin = ring_context_pin,
	.unpin = ring_context_unpin,
	.destroy = ring_context_destroy,
};

static int intel_init_ring_buffer(struct intel_engine_cs *engine)
{
	struct i915_timeline *timeline;
	struct intel_ring *ring;
	int err;

	err = intel_engine_setup_common(engine);
	if (err)
		return err;

	timeline = i915_timeline_create(engine->i915, engine->status_page.vma);
	if (IS_ERR(timeline)) {
		err = PTR_ERR(timeline);
		goto err;
	}
	GEM_BUG_ON(timeline->has_initial_breadcrumb);

	ring = intel_engine_create_ring(engine, timeline, 32 * PAGE_SIZE);
	i915_timeline_put(timeline);
	if (IS_ERR(ring)) {
		err = PTR_ERR(ring);
		goto err;
	}

	err = intel_ring_pin(ring);
	if (err)
		goto err_ring;

	GEM_BUG_ON(engine->buffer);
	engine->buffer = ring;

	err = intel_engine_init_common(engine);
	if (err)
		goto err_unpin;

	GEM_BUG_ON(ring->timeline->hwsp_ggtt != engine->status_page.vma);

	return 0;

err_unpin:
	intel_ring_unpin(ring);
err_ring:
	intel_ring_put(ring);
err:
	intel_engine_cleanup_common(engine);
	return err;
}

void intel_engine_cleanup(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	WARN_ON(INTEL_GEN(dev_priv) > 2 &&
		(ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE) == 0);

	intel_ring_unpin(engine->buffer);
	intel_ring_put(engine->buffer);

	if (engine->cleanup)
		engine->cleanup(engine);

	intel_engine_cleanup_common(engine);

	dev_priv->engine[engine->id] = NULL;
	kfree(engine);
}

void intel_legacy_submission_resume(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Restart from the beginning of the rings for convenience */
	for_each_engine(engine, dev_priv, id)
		intel_ring_reset(engine->buffer, 0);
}

static int load_pd_dir(struct i915_request *rq,
		       const struct i915_hw_ppgtt *ppgtt)
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
	*cs++ = ppgtt->pd.base.ggtt_offset << 10;

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
	*cs++ = i915_scratch_offset(rq->i915);
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
		IS_HSW_GT1(i915) ? RUNTIME_INFO(i915)->num_engines - 1 : 0;
	bool force_restore = false;
	int len;
	u32 *cs;

	flags |= MI_MM_SPACE_GTT;
	if (IS_HASWELL(i915))
		/* These flags are for resource streamer on HSW+ */
		flags |= HSW_MI_RS_SAVE_STATE_EN | HSW_MI_RS_RESTORE_STATE_EN;
	else
		flags |= MI_SAVE_EXT_STATE_EN | MI_RESTORE_EXT_STATE_EN;

	len = 4;
	if (IS_GEN(i915, 7))
		len += 2 + (num_engines ? 4 * num_engines + 6 : 0);
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
			for_each_engine(signaller, i915, id) {
				if (signaller == engine)
					continue;

				*cs++ = i915_mmio_reg_offset(
					   RING_PSMI_CTL(signaller->mmio_base));
				*cs++ = _MASKED_BIT_ENABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}
		}
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
			for_each_engine(signaller, i915, id) {
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
			*cs++ = i915_scratch_offset(rq->i915);
			*cs++ = MI_NOOP;
		}
		*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	}

	intel_ring_advance(rq, cs);

	return 0;
}

static int remap_l3(struct i915_request *rq, int slice)
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

static int switch_context(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct i915_gem_context *ctx = rq->gem_context;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt ?: rq->i915->mm.aliasing_ppgtt;
	unsigned int unwind_mm = 0;
	u32 hw_flags = 0;
	int ret, i;

	lockdep_assert_held(&rq->i915->drm.struct_mutex);
	GEM_BUG_ON(HAS_EXECLISTS(rq->i915));

	if (ppgtt) {
		int loops;

		/*
		 * Baytail takes a little more convincing that it really needs
		 * to reload the PD between contexts. It is not just a little
		 * longer, as adding more stalls after the load_pd_dir (i.e.
		 * adding a long loop around flush_pd_dir) is not as effective
		 * as reloading the PD umpteen times. 32 is derived from
		 * experimentation (gem_exec_parallel/fds) and has no good
		 * explanation.
		 */
		loops = 1;
		if (engine->id == BCS0 && IS_VALLEYVIEW(engine->i915))
			loops = 32;

		do {
			ret = load_pd_dir(rq, ppgtt);
			if (ret)
				goto err;
		} while (--loops);

		if (ppgtt->pd_dirty_engines & engine->mask) {
			unwind_mm = engine->mask;
			ppgtt->pd_dirty_engines &= ~unwind_mm;
			hw_flags = MI_FORCE_RESTORE;
		}
	}

	if (rq->hw_context->state) {
		GEM_BUG_ON(engine->id != RCS0);

		/*
		 * The kernel context(s) is treated as pure scratch and is not
		 * expected to retain any state (as we sacrifice it during
		 * suspend and on resume it may be corrupted). This is ok,
		 * as nothing actually executes using the kernel context; it
		 * is purely used for flushing user contexts.
		 */
		if (i915_gem_context_is_kernel(ctx))
			hw_flags = MI_RESTORE_INHIBIT;

		ret = mi_set_context(rq, hw_flags);
		if (ret)
			goto err_mm;
	}

	if (ppgtt) {
		ret = engine->emit_flush(rq, EMIT_INVALIDATE);
		if (ret)
			goto err_mm;

		ret = flush_pd_dir(rq);
		if (ret)
			goto err_mm;

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
			goto err_mm;

		ret = engine->emit_flush(rq, EMIT_FLUSH);
		if (ret)
			goto err_mm;
	}

	if (ctx->remap_slice) {
		for (i = 0; i < MAX_L3_SLICES; i++) {
			if (!(ctx->remap_slice & BIT(i)))
				continue;

			ret = remap_l3(rq, i);
			if (ret)
				goto err_mm;
		}

		ctx->remap_slice = 0;
	}

	return 0;

err_mm:
	if (unwind_mm)
		ppgtt->pd_dirty_engines |= unwind_mm;
err:
	return ret;
}

static int ring_request_alloc(struct i915_request *request)
{
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(request->hw_context));
	GEM_BUG_ON(request->timeline->has_initial_breadcrumb);

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	request->reserved_space += LEGACY_REQUEST_SIZE;

	ret = switch_context(request);
	if (ret)
		return ret;

	/* Unconditionally invalidate GPU caches and TLBs. */
	ret = request->engine->emit_flush(request, EMIT_INVALIDATE);
	if (ret)
		return ret;

	request->reserved_space -= LEGACY_REQUEST_SIZE;
	return 0;
}

static noinline int wait_for_space(struct intel_ring *ring, unsigned int bytes)
{
	struct i915_request *target;
	long timeout;

	lockdep_assert_held(&ring->vma->vm->i915->drm.struct_mutex);

	if (intel_ring_update_space(ring) >= bytes)
		return 0;

	GEM_BUG_ON(list_empty(&ring->request_list));
	list_for_each_entry(target, &ring->request_list, ring_link) {
		/* Would completion of this request free enough space? */
		if (bytes <= __intel_ring_space(target->postfix,
						ring->emit, ring->size))
			break;
	}

	if (WARN_ON(&target->ring_link == &ring->request_list))
		return -ENOSPC;

	timeout = i915_request_wait(target,
				    I915_WAIT_INTERRUPTIBLE | I915_WAIT_LOCKED,
				    MAX_SCHEDULE_TIMEOUT);
	if (timeout < 0)
		return timeout;

	i915_request_retire_upto(target);

	intel_ring_update_space(ring);
	GEM_BUG_ON(ring->space < bytes);
	return 0;
}

u32 *intel_ring_begin(struct i915_request *rq, unsigned int num_dwords)
{
	struct intel_ring *ring = rq->ring;
	const unsigned int remain_usable = ring->effective_size - ring->emit;
	const unsigned int bytes = num_dwords * sizeof(u32);
	unsigned int need_wrap = 0;
	unsigned int total_bytes;
	u32 *cs;

	/* Packets must be qword aligned. */
	GEM_BUG_ON(num_dwords & 1);

	total_bytes = bytes + rq->reserved_space;
	GEM_BUG_ON(total_bytes > ring->effective_size);

	if (unlikely(total_bytes > remain_usable)) {
		const int remain_actual = ring->size - ring->emit;

		if (bytes > remain_usable) {
			/*
			 * Not enough space for the basic request. So need to
			 * flush out the remainder and then wait for
			 * base + reserved.
			 */
			total_bytes += remain_actual;
			need_wrap = remain_actual | 1;
		} else  {
			/*
			 * The base request will fit but the reserved space
			 * falls off the end. So we don't need an immediate
			 * wrap and only need to effectively wait for the
			 * reserved size from the start of ringbuffer.
			 */
			total_bytes = rq->reserved_space + remain_actual;
		}
	}

	if (unlikely(total_bytes > ring->space)) {
		int ret;

		/*
		 * Space is reserved in the ringbuffer for finalising the
		 * request, as that cannot be allowed to fail. During request
		 * finalisation, reserved_space is set to 0 to stop the
		 * overallocation and the assumption is that then we never need
		 * to wait (which has the risk of failing with EINTR).
		 *
		 * See also i915_request_alloc() and i915_request_add().
		 */
		GEM_BUG_ON(!rq->reserved_space);

		ret = wait_for_space(ring, total_bytes);
		if (unlikely(ret))
			return ERR_PTR(ret);
	}

	if (unlikely(need_wrap)) {
		need_wrap &= ~1;
		GEM_BUG_ON(need_wrap > ring->space);
		GEM_BUG_ON(ring->emit + need_wrap > ring->size);
		GEM_BUG_ON(!IS_ALIGNED(need_wrap, sizeof(u64)));

		/* Fill the tail with MI_NOOP */
		memset64(ring->vaddr + ring->emit, 0, need_wrap / sizeof(u64));
		ring->space -= need_wrap;
		ring->emit = 0;
	}

	GEM_BUG_ON(ring->emit > ring->size - bytes);
	GEM_BUG_ON(ring->space < bytes);
	cs = ring->vaddr + ring->emit;
	GEM_DEBUG_EXEC(memset32(cs, POISON_INUSE, bytes / sizeof(*cs)));
	ring->emit += bytes;
	ring->space -= bytes;

	return cs;
}

/* Align the ring tail to a cacheline boundary */
int intel_ring_cacheline_align(struct i915_request *rq)
{
	int num_dwords;
	void *cs;

	num_dwords = (rq->ring->emit & (CACHELINE_BYTES - 1)) / sizeof(u32);
	if (num_dwords == 0)
		return 0;

	num_dwords = CACHELINE_DWORDS - num_dwords;
	GEM_BUG_ON(num_dwords & 1);

	cs = intel_ring_begin(rq, num_dwords);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	memset64(cs, (u64)MI_NOOP << 32 | MI_NOOP, num_dwords / 2);
	intel_ring_advance(rq, cs);

	GEM_BUG_ON(rq->ring->emit & (CACHELINE_BYTES - 1));
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

static void intel_ring_init_irq(struct drm_i915_private *dev_priv,
				struct intel_engine_cs *engine)
{
	if (INTEL_GEN(dev_priv) >= 6) {
		engine->irq_enable = gen6_irq_enable;
		engine->irq_disable = gen6_irq_disable;
	} else if (INTEL_GEN(dev_priv) >= 5) {
		engine->irq_enable = gen5_irq_enable;
		engine->irq_disable = gen5_irq_disable;
	} else if (INTEL_GEN(dev_priv) >= 3) {
		engine->irq_enable = i9xx_irq_enable;
		engine->irq_disable = i9xx_irq_disable;
	} else {
		engine->irq_enable = i8xx_irq_enable;
		engine->irq_disable = i8xx_irq_disable;
	}
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

static void intel_ring_default_vfuncs(struct drm_i915_private *dev_priv,
				      struct intel_engine_cs *engine)
{
	/* gen8+ are only supported with execlists */
	GEM_BUG_ON(INTEL_GEN(dev_priv) >= 8);

	intel_ring_init_irq(dev_priv, engine);

	engine->init_hw = init_ring_common;
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
	if (IS_GEN(dev_priv, 5))
		engine->emit_fini_breadcrumb = gen5_emit_breadcrumb;

	engine->set_default_submission = i9xx_set_default_submission;

	if (INTEL_GEN(dev_priv) >= 6)
		engine->emit_bb_start = gen6_emit_bb_start;
	else if (INTEL_GEN(dev_priv) >= 4)
		engine->emit_bb_start = i965_emit_bb_start;
	else if (IS_I830(dev_priv) || IS_I845G(dev_priv))
		engine->emit_bb_start = i830_emit_bb_start;
	else
		engine->emit_bb_start = i915_emit_bb_start;
}

int intel_init_render_ring_buffer(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	int ret;

	intel_ring_default_vfuncs(dev_priv, engine);

	if (HAS_L3_DPF(dev_priv))
		engine->irq_keep_mask = GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	engine->irq_enable_mask = GT_RENDER_USER_INTERRUPT;

	if (INTEL_GEN(dev_priv) >= 7) {
		engine->init_context = intel_rcs_ctx_init;
		engine->emit_flush = gen7_render_ring_flush;
		engine->emit_fini_breadcrumb = gen7_rcs_emit_breadcrumb;
	} else if (IS_GEN(dev_priv, 6)) {
		engine->init_context = intel_rcs_ctx_init;
		engine->emit_flush = gen6_render_ring_flush;
		engine->emit_fini_breadcrumb = gen6_rcs_emit_breadcrumb;
	} else if (IS_GEN(dev_priv, 5)) {
		engine->emit_flush = gen4_render_ring_flush;
	} else {
		if (INTEL_GEN(dev_priv) < 4)
			engine->emit_flush = gen2_render_ring_flush;
		else
			engine->emit_flush = gen4_render_ring_flush;
		engine->irq_enable_mask = I915_USER_INTERRUPT;
	}

	if (IS_HASWELL(dev_priv))
		engine->emit_bb_start = hsw_emit_bb_start;

	engine->init_hw = init_render_ring;

	ret = intel_init_ring_buffer(engine);
	if (ret)
		return ret;

	return 0;
}

int intel_init_bsd_ring_buffer(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	intel_ring_default_vfuncs(dev_priv, engine);

	if (INTEL_GEN(dev_priv) >= 6) {
		/* gen6 bsd needs a special wa for tail updates */
		if (IS_GEN(dev_priv, 6))
			engine->set_default_submission = gen6_bsd_set_default_submission;
		engine->emit_flush = gen6_bsd_ring_flush;
		engine->irq_enable_mask = GT_BSD_USER_INTERRUPT;

		if (IS_GEN(dev_priv, 6))
			engine->emit_fini_breadcrumb = gen6_xcs_emit_breadcrumb;
		else
			engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;
	} else {
		engine->emit_flush = bsd_ring_flush;
		if (IS_GEN(dev_priv, 5))
			engine->irq_enable_mask = ILK_BSD_USER_INTERRUPT;
		else
			engine->irq_enable_mask = I915_BSD_USER_INTERRUPT;
	}

	return intel_init_ring_buffer(engine);
}

int intel_init_blt_ring_buffer(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	GEM_BUG_ON(INTEL_GEN(dev_priv) < 6);

	intel_ring_default_vfuncs(dev_priv, engine);

	engine->emit_flush = gen6_ring_flush;
	engine->irq_enable_mask = GT_BLT_USER_INTERRUPT;

	if (IS_GEN(dev_priv, 6))
		engine->emit_fini_breadcrumb = gen6_xcs_emit_breadcrumb;
	else
		engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;

	return intel_init_ring_buffer(engine);
}

int intel_init_vebox_ring_buffer(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	GEM_BUG_ON(INTEL_GEN(dev_priv) < 7);

	intel_ring_default_vfuncs(dev_priv, engine);

	engine->emit_flush = gen6_ring_flush;
	engine->irq_enable_mask = PM_VEBOX_USER_INTERRUPT;
	engine->irq_enable = hsw_vebox_irq_enable;
	engine->irq_disable = hsw_vebox_irq_disable;

	engine->emit_fini_breadcrumb = gen7_xcs_emit_breadcrumb;

	return intel_init_ring_buffer(engine);
}
