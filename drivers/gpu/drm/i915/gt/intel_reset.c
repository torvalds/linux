/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2018 Intel Corporation
 */

#include <linux/sched/mm.h>
#include <linux/stop_machine.h>

#include "gem/i915_gem_context.h"

#include "i915_drv.h"
#include "i915_gpu_error.h"
#include "i915_irq.h"
#include "intel_engine_pm.h"
#include "intel_gt_pm.h"
#include "intel_reset.h"

#include "intel_guc.h"
#include "intel_overlay.h"

#define RESET_MAX_RETRIES 3

/* XXX How to handle concurrent GGTT updates using tiling registers? */
#define RESET_UNDER_STOP_MACHINE 0

static void rmw_set(struct intel_uncore *uncore, i915_reg_t reg, u32 set)
{
	intel_uncore_rmw(uncore, reg, 0, set);
}

static void rmw_clear(struct intel_uncore *uncore, i915_reg_t reg, u32 clr)
{
	intel_uncore_rmw(uncore, reg, clr, 0);
}

static void rmw_set_fw(struct intel_uncore *uncore, i915_reg_t reg, u32 set)
{
	intel_uncore_rmw_fw(uncore, reg, 0, set);
}

static void rmw_clear_fw(struct intel_uncore *uncore, i915_reg_t reg, u32 clr)
{
	intel_uncore_rmw_fw(uncore, reg, clr, 0);
}

static void engine_skip_context(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct i915_gem_context *hung_ctx = rq->gem_context;

	lockdep_assert_held(&engine->timeline.lock);

	if (!i915_request_is_active(rq))
		return;

	list_for_each_entry_continue(rq, &engine->timeline.requests, link)
		if (rq->gem_context == hung_ctx)
			i915_request_skip(rq, -EIO);
}

static void client_mark_guilty(struct drm_i915_file_private *file_priv,
			       const struct i915_gem_context *ctx)
{
	unsigned int score;
	unsigned long prev_hang;

	if (i915_gem_context_is_banned(ctx))
		score = I915_CLIENT_SCORE_CONTEXT_BAN;
	else
		score = 0;

	prev_hang = xchg(&file_priv->hang_timestamp, jiffies);
	if (time_before(jiffies, prev_hang + I915_CLIENT_FAST_HANG_JIFFIES))
		score += I915_CLIENT_SCORE_HANG_FAST;

	if (score) {
		atomic_add(score, &file_priv->ban_score);

		DRM_DEBUG_DRIVER("client %s: gained %u ban score, now %u\n",
				 ctx->name, score,
				 atomic_read(&file_priv->ban_score));
	}
}

static bool context_mark_guilty(struct i915_gem_context *ctx)
{
	unsigned long prev_hang;
	bool banned;
	int i;

	atomic_inc(&ctx->guilty_count);

	/* Cool contexts are too cool to be banned! (Used for reset testing.) */
	if (!i915_gem_context_is_bannable(ctx))
		return false;

	/* Record the timestamp for the last N hangs */
	prev_hang = ctx->hang_timestamp[0];
	for (i = 0; i < ARRAY_SIZE(ctx->hang_timestamp) - 1; i++)
		ctx->hang_timestamp[i] = ctx->hang_timestamp[i + 1];
	ctx->hang_timestamp[i] = jiffies;

	/* If we have hung N+1 times in rapid succession, we ban the context! */
	banned = !i915_gem_context_is_recoverable(ctx);
	if (time_before(jiffies, prev_hang + CONTEXT_FAST_HANG_JIFFIES))
		banned = true;
	if (banned) {
		DRM_DEBUG_DRIVER("context %s: guilty %d, banned\n",
				 ctx->name, atomic_read(&ctx->guilty_count));
		i915_gem_context_set_banned(ctx);
	}

	if (!IS_ERR_OR_NULL(ctx->file_priv))
		client_mark_guilty(ctx->file_priv, ctx);

	return banned;
}

static void context_mark_innocent(struct i915_gem_context *ctx)
{
	atomic_inc(&ctx->active_count);
}

void i915_reset_request(struct i915_request *rq, bool guilty)
{
	GEM_TRACE("%s rq=%llx:%lld, guilty? %s\n",
		  rq->engine->name,
		  rq->fence.context,
		  rq->fence.seqno,
		  yesno(guilty));

	lockdep_assert_held(&rq->engine->timeline.lock);
	GEM_BUG_ON(i915_request_completed(rq));

	if (guilty) {
		i915_request_skip(rq, -EIO);
		if (context_mark_guilty(rq->gem_context))
			engine_skip_context(rq);
	} else {
		dma_fence_set_error(&rq->fence, -EAGAIN);
		context_mark_innocent(rq->gem_context);
	}
}

static void gen3_stop_engine(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	const u32 base = engine->mmio_base;

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

static void i915_stop_engines(struct drm_i915_private *i915,
			      intel_engine_mask_t engine_mask)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;

	if (INTEL_GEN(i915) < 3)
		return;

	for_each_engine_masked(engine, i915, engine_mask, tmp)
		gen3_stop_engine(engine);
}

static bool i915_in_reset(struct pci_dev *pdev)
{
	u8 gdrst;

	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return gdrst & GRDOM_RESET_STATUS;
}

static int i915_do_reset(struct drm_i915_private *i915,
			 intel_engine_mask_t engine_mask,
			 unsigned int retry)
{
	struct pci_dev *pdev = i915->drm.pdev;
	int err;

	/* Assert reset for at least 20 usec, and wait for acknowledgement. */
	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	udelay(50);
	err = wait_for_atomic(i915_in_reset(pdev), 50);

	/* Clear the reset request. */
	pci_write_config_byte(pdev, I915_GDRST, 0);
	udelay(50);
	if (!err)
		err = wait_for_atomic(!i915_in_reset(pdev), 50);

	return err;
}

static bool g4x_reset_complete(struct pci_dev *pdev)
{
	u8 gdrst;

	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_ENABLE) == 0;
}

static int g33_do_reset(struct drm_i915_private *i915,
			intel_engine_mask_t engine_mask,
			unsigned int retry)
{
	struct pci_dev *pdev = i915->drm.pdev;

	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	return wait_for_atomic(g4x_reset_complete(pdev), 50);
}

static int g4x_do_reset(struct drm_i915_private *i915,
			intel_engine_mask_t engine_mask,
			unsigned int retry)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct intel_uncore *uncore = &i915->uncore;
	int ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	rmw_set_fw(uncore, VDECCLK_GATE_D, VCP_UNIT_CLOCK_GATE_DISABLE);
	intel_uncore_posting_read_fw(uncore, VDECCLK_GATE_D);

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	ret =  wait_for_atomic(g4x_reset_complete(pdev), 50);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for media reset failed\n");
		goto out;
	}

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  wait_for_atomic(g4x_reset_complete(pdev), 50);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for render reset failed\n");
		goto out;
	}

out:
	pci_write_config_byte(pdev, I915_GDRST, 0);

	rmw_clear_fw(uncore, VDECCLK_GATE_D, VCP_UNIT_CLOCK_GATE_DISABLE);
	intel_uncore_posting_read_fw(uncore, VDECCLK_GATE_D);

	return ret;
}

static int ironlake_do_reset(struct drm_i915_private *i915,
			     intel_engine_mask_t engine_mask,
			     unsigned int retry)
{
	struct intel_uncore *uncore = &i915->uncore;
	int ret;

	intel_uncore_write_fw(uncore, ILK_GDSR,
			      ILK_GRDOM_RENDER | ILK_GRDOM_RESET_ENABLE);
	ret = __intel_wait_for_register_fw(uncore, ILK_GDSR,
					   ILK_GRDOM_RESET_ENABLE, 0,
					   5000, 0,
					   NULL);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for render reset failed\n");
		goto out;
	}

	intel_uncore_write_fw(uncore, ILK_GDSR,
			      ILK_GRDOM_MEDIA | ILK_GRDOM_RESET_ENABLE);
	ret = __intel_wait_for_register_fw(uncore, ILK_GDSR,
					   ILK_GRDOM_RESET_ENABLE, 0,
					   5000, 0,
					   NULL);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for media reset failed\n");
		goto out;
	}

out:
	intel_uncore_write_fw(uncore, ILK_GDSR, 0);
	intel_uncore_posting_read_fw(uncore, ILK_GDSR);
	return ret;
}

/* Reset the hardware domains (GENX_GRDOM_*) specified by mask */
static int gen6_hw_domain_reset(struct drm_i915_private *i915,
				u32 hw_domain_mask)
{
	struct intel_uncore *uncore = &i915->uncore;
	int err;

	/*
	 * GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	intel_uncore_write_fw(uncore, GEN6_GDRST, hw_domain_mask);

	/* Wait for the device to ack the reset requests */
	err = __intel_wait_for_register_fw(uncore,
					   GEN6_GDRST, hw_domain_mask, 0,
					   500, 0,
					   NULL);
	if (err)
		DRM_DEBUG_DRIVER("Wait for 0x%08x engines reset failed\n",
				 hw_domain_mask);

	return err;
}

static int gen6_reset_engines(struct drm_i915_private *i915,
			      intel_engine_mask_t engine_mask,
			      unsigned int retry)
{
	struct intel_engine_cs *engine;
	const u32 hw_engine_mask[] = {
		[RCS0]  = GEN6_GRDOM_RENDER,
		[BCS0]  = GEN6_GRDOM_BLT,
		[VCS0]  = GEN6_GRDOM_MEDIA,
		[VCS1]  = GEN8_GRDOM_MEDIA2,
		[VECS0] = GEN6_GRDOM_VECS,
	};
	u32 hw_mask;

	if (engine_mask == ALL_ENGINES) {
		hw_mask = GEN6_GRDOM_FULL;
	} else {
		intel_engine_mask_t tmp;

		hw_mask = 0;
		for_each_engine_masked(engine, i915, engine_mask, tmp) {
			GEM_BUG_ON(engine->id >= ARRAY_SIZE(hw_engine_mask));
			hw_mask |= hw_engine_mask[engine->id];
		}
	}

	return gen6_hw_domain_reset(i915, hw_mask);
}

static u32 gen11_lock_sfc(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	u8 vdbox_sfc_access = RUNTIME_INFO(engine->i915)->vdbox_sfc_access;
	i915_reg_t sfc_forced_lock, sfc_forced_lock_ack;
	u32 sfc_forced_lock_bit, sfc_forced_lock_ack_bit;
	i915_reg_t sfc_usage;
	u32 sfc_usage_bit;
	u32 sfc_reset_bit;

	switch (engine->class) {
	case VIDEO_DECODE_CLASS:
		if ((BIT(engine->instance) & vdbox_sfc_access) == 0)
			return 0;

		sfc_forced_lock = GEN11_VCS_SFC_FORCED_LOCK(engine);
		sfc_forced_lock_bit = GEN11_VCS_SFC_FORCED_LOCK_BIT;

		sfc_forced_lock_ack = GEN11_VCS_SFC_LOCK_STATUS(engine);
		sfc_forced_lock_ack_bit  = GEN11_VCS_SFC_LOCK_ACK_BIT;

		sfc_usage = GEN11_VCS_SFC_LOCK_STATUS(engine);
		sfc_usage_bit = GEN11_VCS_SFC_USAGE_BIT;
		sfc_reset_bit = GEN11_VCS_SFC_RESET_BIT(engine->instance);
		break;

	case VIDEO_ENHANCEMENT_CLASS:
		sfc_forced_lock = GEN11_VECS_SFC_FORCED_LOCK(engine);
		sfc_forced_lock_bit = GEN11_VECS_SFC_FORCED_LOCK_BIT;

		sfc_forced_lock_ack = GEN11_VECS_SFC_LOCK_ACK(engine);
		sfc_forced_lock_ack_bit  = GEN11_VECS_SFC_LOCK_ACK_BIT;

		sfc_usage = GEN11_VECS_SFC_USAGE(engine);
		sfc_usage_bit = GEN11_VECS_SFC_USAGE_BIT;
		sfc_reset_bit = GEN11_VECS_SFC_RESET_BIT(engine->instance);
		break;

	default:
		return 0;
	}

	/*
	 * Tell the engine that a software reset is going to happen. The engine
	 * will then try to force lock the SFC (if currently locked, it will
	 * remain so until we tell the engine it is safe to unlock; if currently
	 * unlocked, it will ignore this and all new lock requests). If SFC
	 * ends up being locked to the engine we want to reset, we have to reset
	 * it as well (we will unlock it once the reset sequence is completed).
	 */
	rmw_set_fw(uncore, sfc_forced_lock, sfc_forced_lock_bit);

	if (__intel_wait_for_register_fw(uncore,
					 sfc_forced_lock_ack,
					 sfc_forced_lock_ack_bit,
					 sfc_forced_lock_ack_bit,
					 1000, 0, NULL)) {
		DRM_DEBUG_DRIVER("Wait for SFC forced lock ack failed\n");
		return 0;
	}

	if (intel_uncore_read_fw(uncore, sfc_usage) & sfc_usage_bit)
		return sfc_reset_bit;

	return 0;
}

static void gen11_unlock_sfc(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	u8 vdbox_sfc_access = RUNTIME_INFO(engine->i915)->vdbox_sfc_access;
	i915_reg_t sfc_forced_lock;
	u32 sfc_forced_lock_bit;

	switch (engine->class) {
	case VIDEO_DECODE_CLASS:
		if ((BIT(engine->instance) & vdbox_sfc_access) == 0)
			return;

		sfc_forced_lock = GEN11_VCS_SFC_FORCED_LOCK(engine);
		sfc_forced_lock_bit = GEN11_VCS_SFC_FORCED_LOCK_BIT;
		break;

	case VIDEO_ENHANCEMENT_CLASS:
		sfc_forced_lock = GEN11_VECS_SFC_FORCED_LOCK(engine);
		sfc_forced_lock_bit = GEN11_VECS_SFC_FORCED_LOCK_BIT;
		break;

	default:
		return;
	}

	rmw_clear_fw(uncore, sfc_forced_lock, sfc_forced_lock_bit);
}

static int gen11_reset_engines(struct drm_i915_private *i915,
			       intel_engine_mask_t engine_mask,
			       unsigned int retry)
{
	const u32 hw_engine_mask[] = {
		[RCS0]  = GEN11_GRDOM_RENDER,
		[BCS0]  = GEN11_GRDOM_BLT,
		[VCS0]  = GEN11_GRDOM_MEDIA,
		[VCS1]  = GEN11_GRDOM_MEDIA2,
		[VCS2]  = GEN11_GRDOM_MEDIA3,
		[VCS3]  = GEN11_GRDOM_MEDIA4,
		[VECS0] = GEN11_GRDOM_VECS,
		[VECS1] = GEN11_GRDOM_VECS2,
	};
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;
	u32 hw_mask;
	int ret;

	if (engine_mask == ALL_ENGINES) {
		hw_mask = GEN11_GRDOM_FULL;
	} else {
		hw_mask = 0;
		for_each_engine_masked(engine, i915, engine_mask, tmp) {
			GEM_BUG_ON(engine->id >= ARRAY_SIZE(hw_engine_mask));
			hw_mask |= hw_engine_mask[engine->id];
			hw_mask |= gen11_lock_sfc(engine);
		}
	}

	ret = gen6_hw_domain_reset(i915, hw_mask);

	if (engine_mask != ALL_ENGINES)
		for_each_engine_masked(engine, i915, engine_mask, tmp)
			gen11_unlock_sfc(engine);

	return ret;
}

static int gen8_engine_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	const i915_reg_t reg = RING_RESET_CTL(engine->mmio_base);
	u32 request, mask, ack;
	int ret;

	ack = intel_uncore_read_fw(uncore, reg);
	if (ack & RESET_CTL_CAT_ERROR) {
		/*
		 * For catastrophic errors, ready-for-reset sequence
		 * needs to be bypassed: HAS#396813
		 */
		request = RESET_CTL_CAT_ERROR;
		mask = RESET_CTL_CAT_ERROR;

		/* Catastrophic errors need to be cleared by HW */
		ack = 0;
	} else if (!(ack & RESET_CTL_READY_TO_RESET)) {
		request = RESET_CTL_REQUEST_RESET;
		mask = RESET_CTL_READY_TO_RESET;
		ack = RESET_CTL_READY_TO_RESET;
	} else {
		return 0;
	}

	intel_uncore_write_fw(uncore, reg, _MASKED_BIT_ENABLE(request));
	ret = __intel_wait_for_register_fw(uncore, reg, mask, ack,
					   700, 0, NULL);
	if (ret)
		DRM_ERROR("%s reset request timed out: {request: %08x, RESET_CTL: %08x}\n",
			  engine->name, request,
			  intel_uncore_read_fw(uncore, reg));

	return ret;
}

static void gen8_engine_reset_cancel(struct intel_engine_cs *engine)
{
	intel_uncore_write_fw(engine->uncore,
			      RING_RESET_CTL(engine->mmio_base),
			      _MASKED_BIT_DISABLE(RESET_CTL_REQUEST_RESET));
}

static int gen8_reset_engines(struct drm_i915_private *i915,
			      intel_engine_mask_t engine_mask,
			      unsigned int retry)
{
	struct intel_engine_cs *engine;
	const bool reset_non_ready = retry >= 1;
	intel_engine_mask_t tmp;
	int ret;

	for_each_engine_masked(engine, i915, engine_mask, tmp) {
		ret = gen8_engine_reset_prepare(engine);
		if (ret && !reset_non_ready)
			goto skip_reset;

		/*
		 * If this is not the first failed attempt to prepare,
		 * we decide to proceed anyway.
		 *
		 * By doing so we risk context corruption and with
		 * some gens (kbl), possible system hang if reset
		 * happens during active bb execution.
		 *
		 * We rather take context corruption instead of
		 * failed reset with a wedged driver/gpu. And
		 * active bb execution case should be covered by
		 * i915_stop_engines we have before the reset.
		 */
	}

	if (INTEL_GEN(i915) >= 11)
		ret = gen11_reset_engines(i915, engine_mask, retry);
	else
		ret = gen6_reset_engines(i915, engine_mask, retry);

skip_reset:
	for_each_engine_masked(engine, i915, engine_mask, tmp)
		gen8_engine_reset_cancel(engine);

	return ret;
}

typedef int (*reset_func)(struct drm_i915_private *,
			  intel_engine_mask_t engine_mask,
			  unsigned int retry);

static reset_func intel_get_gpu_reset(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 8)
		return gen8_reset_engines;
	else if (INTEL_GEN(i915) >= 6)
		return gen6_reset_engines;
	else if (INTEL_GEN(i915) >= 5)
		return ironlake_do_reset;
	else if (IS_G4X(i915))
		return g4x_do_reset;
	else if (IS_G33(i915) || IS_PINEVIEW(i915))
		return g33_do_reset;
	else if (INTEL_GEN(i915) >= 3)
		return i915_do_reset;
	else
		return NULL;
}

int intel_gpu_reset(struct drm_i915_private *i915,
		    intel_engine_mask_t engine_mask)
{
	const int retries = engine_mask == ALL_ENGINES ? RESET_MAX_RETRIES : 1;
	reset_func reset;
	int ret = -ETIMEDOUT;
	int retry;

	reset = intel_get_gpu_reset(i915);
	if (!reset)
		return -ENODEV;

	/*
	 * If the power well sleeps during the reset, the reset
	 * request may be dropped and never completes (causing -EIO).
	 */
	intel_uncore_forcewake_get(&i915->uncore, FORCEWAKE_ALL);
	for (retry = 0; ret == -ETIMEDOUT && retry < retries; retry++) {
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
		if (retry)
			i915_stop_engines(i915, engine_mask);

		GEM_TRACE("engine_mask=%x\n", engine_mask);
		preempt_disable();
		ret = reset(i915, engine_mask, retry);
		preempt_enable();
	}
	intel_uncore_forcewake_put(&i915->uncore, FORCEWAKE_ALL);

	return ret;
}

bool intel_has_gpu_reset(struct drm_i915_private *i915)
{
	if (!i915_modparams.reset)
		return NULL;

	return intel_get_gpu_reset(i915);
}

bool intel_has_reset_engine(struct drm_i915_private *i915)
{
	return INTEL_INFO(i915)->has_reset_engine && i915_modparams.reset >= 2;
}

int intel_reset_guc(struct drm_i915_private *i915)
{
	u32 guc_domain =
		INTEL_GEN(i915) >= 11 ? GEN11_GRDOM_GUC : GEN9_GRDOM_GUC;
	int ret;

	GEM_BUG_ON(!HAS_GUC(i915));

	intel_uncore_forcewake_get(&i915->uncore, FORCEWAKE_ALL);
	ret = gen6_hw_domain_reset(i915, guc_domain);
	intel_uncore_forcewake_put(&i915->uncore, FORCEWAKE_ALL);

	return ret;
}

/*
 * Ensure irq handler finishes, and not run again.
 * Also return the active request so that we only search for it once.
 */
static void reset_prepare_engine(struct intel_engine_cs *engine)
{
	/*
	 * During the reset sequence, we must prevent the engine from
	 * entering RC6. As the context state is undefined until we restart
	 * the engine, if it does enter RC6 during the reset, the state
	 * written to the powercontext is undefined and so we may lose
	 * GPU state upon resume, i.e. fail to restart after a reset.
	 */
	intel_engine_pm_get(engine);
	intel_uncore_forcewake_get(engine->uncore, FORCEWAKE_ALL);
	engine->reset.prepare(engine);
}

static void revoke_mmaps(struct drm_i915_private *i915)
{
	int i;

	for (i = 0; i < i915->ggtt.num_fences; i++) {
		struct drm_vma_offset_node *node;
		struct i915_vma *vma;
		u64 vma_offset;

		vma = READ_ONCE(i915->ggtt.fence_regs[i].vma);
		if (!vma)
			continue;

		if (!i915_vma_has_userfault(vma))
			continue;

		GEM_BUG_ON(vma->fence != &i915->ggtt.fence_regs[i]);
		node = &vma->obj->base.vma_node;
		vma_offset = vma->ggtt_view.partial.offset << PAGE_SHIFT;
		unmap_mapping_range(i915->drm.anon_inode->i_mapping,
				    drm_vma_node_offset_addr(node) + vma_offset,
				    vma->size,
				    1);
	}
}

static void reset_prepare(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	intel_gt_pm_get(i915);
	for_each_engine(engine, i915, id)
		reset_prepare_engine(engine);

	intel_uc_reset_prepare(i915);
}

static void gt_revoke(struct drm_i915_private *i915)
{
	revoke_mmaps(i915);
}

static int gt_reset(struct drm_i915_private *i915,
		    intel_engine_mask_t stalled_mask)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	/*
	 * Everything depends on having the GTT running, so we need to start
	 * there.
	 */
	err = i915_ggtt_enable_hw(i915);
	if (err)
		return err;

	for_each_engine(engine, i915, id)
		intel_engine_reset(engine, stalled_mask & engine->mask);

	i915_gem_restore_fences(i915);

	return err;
}

static void reset_finish_engine(struct intel_engine_cs *engine)
{
	engine->reset.finish(engine);
	intel_engine_pm_put(engine);
	intel_uncore_forcewake_put(engine->uncore, FORCEWAKE_ALL);
}

static void reset_finish(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id) {
		reset_finish_engine(engine);
		intel_engine_signal_breadcrumbs(engine);
	}
	intel_gt_pm_put(i915);
}

static void nop_submit_request(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	GEM_TRACE("%s fence %llx:%lld -> -EIO\n",
		  engine->name, request->fence.context, request->fence.seqno);
	dma_fence_set_error(&request->fence, -EIO);

	spin_lock_irqsave(&engine->timeline.lock, flags);
	__i915_request_submit(request);
	i915_request_mark_complete(request);
	spin_unlock_irqrestore(&engine->timeline.lock, flags);

	intel_engine_queue_breadcrumbs(engine);
}

static void __i915_gem_set_wedged(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	if (test_bit(I915_WEDGED, &error->flags))
		return;

	if (GEM_SHOW_DEBUG() && !intel_engines_are_idle(i915)) {
		struct drm_printer p = drm_debug_printer(__func__);

		for_each_engine(engine, i915, id)
			intel_engine_dump(engine, &p, "%s\n", engine->name);
	}

	GEM_TRACE("start\n");

	/*
	 * First, stop submission to hw, but do not yet complete requests by
	 * rolling the global seqno forward (since this would complete requests
	 * for which we haven't set the fence error to EIO yet).
	 */
	reset_prepare(i915);

	/* Even if the GPU reset fails, it should still stop the engines */
	if (!INTEL_INFO(i915)->gpu_reset_clobbers_display)
		intel_gpu_reset(i915, ALL_ENGINES);

	for_each_engine(engine, i915, id) {
		engine->submit_request = nop_submit_request;
		engine->schedule = NULL;
	}
	i915->caps.scheduler = 0;

	/*
	 * Make sure no request can slip through without getting completed by
	 * either this call here to intel_engine_write_global_seqno, or the one
	 * in nop_submit_request.
	 */
	synchronize_rcu_expedited();
	set_bit(I915_WEDGED, &error->flags);

	/* Mark all executing requests as skipped */
	for_each_engine(engine, i915, id)
		engine->cancel_requests(engine);

	reset_finish(i915);

	GEM_TRACE("end\n");
}

void i915_gem_set_wedged(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	intel_wakeref_t wakeref;

	mutex_lock(&error->wedge_mutex);
	with_intel_runtime_pm(i915, wakeref)
		__i915_gem_set_wedged(i915);
	mutex_unlock(&error->wedge_mutex);
}

static bool __i915_gem_unset_wedged(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	struct i915_timeline *tl;

	if (!test_bit(I915_WEDGED, &error->flags))
		return true;

	if (!i915->gt.scratch) /* Never full initialised, recovery impossible */
		return false;

	GEM_TRACE("start\n");

	/*
	 * Before unwedging, make sure that all pending operations
	 * are flushed and errored out - we may have requests waiting upon
	 * third party fences. We marked all inflight requests as EIO, and
	 * every execbuf since returned EIO, for consistency we want all
	 * the currently pending requests to also be marked as EIO, which
	 * is done inside our nop_submit_request - and so we must wait.
	 *
	 * No more can be submitted until we reset the wedged bit.
	 */
	mutex_lock(&i915->gt.timelines.mutex);
	list_for_each_entry(tl, &i915->gt.timelines.active_list, link) {
		struct i915_request *rq;

		rq = i915_active_request_get_unlocked(&tl->last_request);
		if (!rq)
			continue;

		/*
		 * All internal dependencies (i915_requests) will have
		 * been flushed by the set-wedge, but we may be stuck waiting
		 * for external fences. These should all be capped to 10s
		 * (I915_FENCE_TIMEOUT) so this wait should not be unbounded
		 * in the worst case.
		 */
		dma_fence_default_wait(&rq->fence, false, MAX_SCHEDULE_TIMEOUT);
		i915_request_put(rq);
	}
	mutex_unlock(&i915->gt.timelines.mutex);

	intel_gt_sanitize(i915, false);

	/*
	 * Undo nop_submit_request. We prevent all new i915 requests from
	 * being queued (by disallowing execbuf whilst wedged) so having
	 * waited for all active requests above, we know the system is idle
	 * and do not have to worry about a thread being inside
	 * engine->submit_request() as we swap over. So unlike installing
	 * the nop_submit_request on reset, we can do this from normal
	 * context and do not require stop_machine().
	 */
	intel_engines_reset_default_submission(i915);

	GEM_TRACE("end\n");

	smp_mb__before_atomic(); /* complete takeover before enabling execbuf */
	clear_bit(I915_WEDGED, &i915->gpu_error.flags);

	return true;
}

bool i915_gem_unset_wedged(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	bool result;

	mutex_lock(&error->wedge_mutex);
	result = __i915_gem_unset_wedged(i915);
	mutex_unlock(&error->wedge_mutex);

	return result;
}

static int do_reset(struct drm_i915_private *i915,
		    intel_engine_mask_t stalled_mask)
{
	int err, i;

	gt_revoke(i915);

	err = intel_gpu_reset(i915, ALL_ENGINES);
	for (i = 0; err && i < RESET_MAX_RETRIES; i++) {
		msleep(10 * (i + 1));
		err = intel_gpu_reset(i915, ALL_ENGINES);
	}
	if (err)
		return err;

	return gt_reset(i915, stalled_mask);
}

/**
 * i915_reset - reset chip after a hang
 * @i915: #drm_i915_private to reset
 * @stalled_mask: mask of the stalled engines with the guilty requests
 * @reason: user error message for why we are resetting
 *
 * Reset the chip.  Useful if a hang is detected. Marks the device as wedged
 * on failure.
 *
 * Procedure is fairly simple:
 *   - reset the chip using the reset reg
 *   - re-init context state
 *   - re-init hardware status page
 *   - re-init ring buffer
 *   - re-init interrupt state
 *   - re-init display
 */
void i915_reset(struct drm_i915_private *i915,
		intel_engine_mask_t stalled_mask,
		const char *reason)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	int ret;

	GEM_TRACE("flags=%lx\n", error->flags);

	might_sleep();
	GEM_BUG_ON(!test_bit(I915_RESET_BACKOFF, &error->flags));
	mutex_lock(&error->wedge_mutex);

	/* Clear any previous failed attempts at recovery. Time to try again. */
	if (!__i915_gem_unset_wedged(i915))
		goto unlock;

	if (reason)
		dev_notice(i915->drm.dev, "Resetting chip for %s\n", reason);
	error->reset_count++;

	reset_prepare(i915);

	if (!intel_has_gpu_reset(i915)) {
		if (i915_modparams.reset)
			dev_err(i915->drm.dev, "GPU reset not supported\n");
		else
			DRM_DEBUG_DRIVER("GPU reset disabled\n");
		goto error;
	}

	if (INTEL_INFO(i915)->gpu_reset_clobbers_display)
		intel_runtime_pm_disable_interrupts(i915);

	if (do_reset(i915, stalled_mask)) {
		dev_err(i915->drm.dev, "Failed to reset chip\n");
		goto taint;
	}

	if (INTEL_INFO(i915)->gpu_reset_clobbers_display)
		intel_runtime_pm_enable_interrupts(i915);

	intel_overlay_reset(i915);

	/*
	 * Next we need to restore the context, but we don't use those
	 * yet either...
	 *
	 * Ring buffer needs to be re-initialized in the KMS case, or if X
	 * was running at the time of the reset (i.e. we weren't VT
	 * switched away).
	 */
	ret = i915_gem_init_hw(i915);
	if (ret) {
		DRM_ERROR("Failed to initialise HW following reset (%d)\n",
			  ret);
		goto error;
	}

	i915_queue_hangcheck(i915);

finish:
	reset_finish(i915);
unlock:
	mutex_unlock(&error->wedge_mutex);
	return;

taint:
	/*
	 * History tells us that if we cannot reset the GPU now, we
	 * never will. This then impacts everything that is run
	 * subsequently. On failing the reset, we mark the driver
	 * as wedged, preventing further execution on the GPU.
	 * We also want to go one step further and add a taint to the
	 * kernel so that any subsequent faults can be traced back to
	 * this failure. This is important for CI, where if the
	 * GPU/driver fails we would like to reboot and restart testing
	 * rather than continue on into oblivion. For everyone else,
	 * the system should still plod along, but they have been warned!
	 */
	add_taint_for_CI(TAINT_WARN);
error:
	__i915_gem_set_wedged(i915);
	goto finish;
}

static inline int intel_gt_reset_engine(struct drm_i915_private *i915,
					struct intel_engine_cs *engine)
{
	return intel_gpu_reset(i915, engine->mask);
}

/**
 * i915_reset_engine - reset GPU engine to recover from a hang
 * @engine: engine to reset
 * @msg: reason for GPU reset; or NULL for no dev_notice()
 *
 * Reset a specific GPU engine. Useful if a hang is detected.
 * Returns zero on successful reset or otherwise an error code.
 *
 * Procedure is:
 *  - identifies the request that caused the hang and it is dropped
 *  - reset engine (which will force the engine to idle)
 *  - re-init/configure engine
 */
int i915_reset_engine(struct intel_engine_cs *engine, const char *msg)
{
	struct i915_gpu_error *error = &engine->i915->gpu_error;
	int ret;

	GEM_TRACE("%s flags=%lx\n", engine->name, error->flags);
	GEM_BUG_ON(!test_bit(I915_RESET_ENGINE + engine->id, &error->flags));

	if (!intel_wakeref_active(&engine->wakeref))
		return 0;

	reset_prepare_engine(engine);

	if (msg)
		dev_notice(engine->i915->drm.dev,
			   "Resetting %s for %s\n", engine->name, msg);
	error->reset_engine_count[engine->id]++;

	if (!engine->i915->guc.execbuf_client)
		ret = intel_gt_reset_engine(engine->i915, engine);
	else
		ret = intel_guc_reset_engine(&engine->i915->guc, engine);
	if (ret) {
		/* If we fail here, we expect to fallback to a global reset */
		DRM_DEBUG_DRIVER("%sFailed to reset %s, ret=%d\n",
				 engine->i915->guc.execbuf_client ? "GuC " : "",
				 engine->name, ret);
		goto out;
	}

	/*
	 * The request that caused the hang is stuck on elsp, we know the
	 * active request and can drop it, adjust head to skip the offending
	 * request to resume executing remaining requests in the queue.
	 */
	intel_engine_reset(engine, true);

	/*
	 * The engine and its registers (and workarounds in case of render)
	 * have been reset to their default values. Follow the init_ring
	 * process to program RING_MODE, HWSP and re-enable submission.
	 */
	ret = engine->resume(engine);
	if (ret)
		goto out;

out:
	intel_engine_cancel_stop_cs(engine);
	reset_finish_engine(engine);
	return ret;
}

static void i915_reset_device(struct drm_i915_private *i915,
			      u32 engine_mask,
			      const char *reason)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	struct kobject *kobj = &i915->drm.primary->kdev->kobj;
	char *error_event[] = { I915_ERROR_UEVENT "=1", NULL };
	char *reset_event[] = { I915_RESET_UEVENT "=1", NULL };
	char *reset_done_event[] = { I915_ERROR_UEVENT "=0", NULL };
	struct i915_wedge_me w;

	kobject_uevent_env(kobj, KOBJ_CHANGE, error_event);

	DRM_DEBUG_DRIVER("resetting chip\n");
	kobject_uevent_env(kobj, KOBJ_CHANGE, reset_event);

	/* Use a watchdog to ensure that our reset completes */
	i915_wedge_on_timeout(&w, i915, 5 * HZ) {
		intel_prepare_reset(i915);

		/* Flush everyone using a resource about to be clobbered */
		synchronize_srcu_expedited(&error->reset_backoff_srcu);

		i915_reset(i915, engine_mask, reason);

		intel_finish_reset(i915);
	}

	if (!test_bit(I915_WEDGED, &error->flags))
		kobject_uevent_env(kobj, KOBJ_CHANGE, reset_done_event);
}

static void clear_register(struct intel_uncore *uncore, i915_reg_t reg)
{
	intel_uncore_rmw(uncore, reg, 0, 0);
}

static void gen8_clear_engine_error_register(struct intel_engine_cs *engine)
{
	GEN6_RING_FAULT_REG_RMW(engine, RING_FAULT_VALID, 0);
	GEN6_RING_FAULT_REG_POSTING_READ(engine);
}

static void clear_error_registers(struct drm_i915_private *i915,
				  intel_engine_mask_t engine_mask)
{
	struct intel_uncore *uncore = &i915->uncore;
	u32 eir;

	if (!IS_GEN(i915, 2))
		clear_register(uncore, PGTBL_ER);

	if (INTEL_GEN(i915) < 4)
		clear_register(uncore, IPEIR(RENDER_RING_BASE));
	else
		clear_register(uncore, IPEIR_I965);

	clear_register(uncore, EIR);
	eir = intel_uncore_read(uncore, EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		DRM_DEBUG_DRIVER("EIR stuck: 0x%08x, masking\n", eir);
		rmw_set(uncore, EMR, eir);
		intel_uncore_write(uncore, GEN2_IIR,
				   I915_MASTER_ERROR_INTERRUPT);
	}

	if (INTEL_GEN(i915) >= 8) {
		rmw_clear(uncore, GEN8_RING_FAULT_REG, RING_FAULT_VALID);
		intel_uncore_posting_read(uncore, GEN8_RING_FAULT_REG);
	} else if (INTEL_GEN(i915) >= 6) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine_masked(engine, i915, engine_mask, id)
			gen8_clear_engine_error_register(engine);
	}
}

static void gen6_check_faults(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 fault;

	for_each_engine(engine, dev_priv, id) {
		fault = GEN6_RING_FAULT_REG_READ(engine);
		if (fault & RING_FAULT_VALID) {
			DRM_DEBUG_DRIVER("Unexpected fault\n"
					 "\tAddr: 0x%08lx\n"
					 "\tAddress space: %s\n"
					 "\tSource ID: %d\n"
					 "\tType: %d\n",
					 fault & PAGE_MASK,
					 fault & RING_FAULT_GTTSEL_MASK ? "GGTT" : "PPGTT",
					 RING_FAULT_SRCID(fault),
					 RING_FAULT_FAULT_TYPE(fault));
		}
	}
}

static void gen8_check_faults(struct drm_i915_private *dev_priv)
{
	u32 fault = I915_READ(GEN8_RING_FAULT_REG);

	if (fault & RING_FAULT_VALID) {
		u32 fault_data0, fault_data1;
		u64 fault_addr;

		fault_data0 = I915_READ(GEN8_FAULT_TLB_DATA0);
		fault_data1 = I915_READ(GEN8_FAULT_TLB_DATA1);
		fault_addr = ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
			     ((u64)fault_data0 << 12);

		DRM_DEBUG_DRIVER("Unexpected fault\n"
				 "\tAddr: 0x%08x_%08x\n"
				 "\tAddress space: %s\n"
				 "\tEngine ID: %d\n"
				 "\tSource ID: %d\n"
				 "\tType: %d\n",
				 upper_32_bits(fault_addr),
				 lower_32_bits(fault_addr),
				 fault_data1 & FAULT_GTT_SEL ? "GGTT" : "PPGTT",
				 GEN8_RING_FAULT_ENGINE_ID(fault),
				 RING_FAULT_SRCID(fault),
				 RING_FAULT_FAULT_TYPE(fault));
	}
}

void i915_check_and_clear_faults(struct drm_i915_private *i915)
{
	/* From GEN8 onwards we only have one 'All Engine Fault Register' */
	if (INTEL_GEN(i915) >= 8)
		gen8_check_faults(i915);
	else if (INTEL_GEN(i915) >= 6)
		gen6_check_faults(i915);
	else
		return;

	clear_error_registers(i915, ALL_ENGINES);
}

/**
 * i915_handle_error - handle a gpu error
 * @i915: i915 device private
 * @engine_mask: mask representing engines that are hung
 * @flags: control flags
 * @fmt: Error message format string
 *
 * Do some basic checking of register state at error time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 */
void i915_handle_error(struct drm_i915_private *i915,
		       intel_engine_mask_t engine_mask,
		       unsigned long flags,
		       const char *fmt, ...)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	intel_engine_mask_t tmp;
	char error_msg[80];
	char *msg = NULL;

	if (fmt) {
		va_list args;

		va_start(args, fmt);
		vscnprintf(error_msg, sizeof(error_msg), fmt, args);
		va_end(args);

		msg = error_msg;
	}

	/*
	 * In most cases it's guaranteed that we get here with an RPM
	 * reference held, for example because there is a pending GPU
	 * request that won't finish until the reset is done. This
	 * isn't the case at least when we get here by doing a
	 * simulated reset via debugfs, so get an RPM reference.
	 */
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	engine_mask &= INTEL_INFO(i915)->engine_mask;

	if (flags & I915_ERROR_CAPTURE) {
		i915_capture_error_state(i915, engine_mask, msg);
		clear_error_registers(i915, engine_mask);
	}

	/*
	 * Try engine reset when available. We fall back to full reset if
	 * single reset fails.
	 */
	if (intel_has_reset_engine(i915) && !__i915_wedged(error)) {
		for_each_engine_masked(engine, i915, engine_mask, tmp) {
			BUILD_BUG_ON(I915_RESET_MODESET >= I915_RESET_ENGINE);
			if (test_and_set_bit(I915_RESET_ENGINE + engine->id,
					     &error->flags))
				continue;

			if (i915_reset_engine(engine, msg) == 0)
				engine_mask &= ~engine->mask;

			clear_bit(I915_RESET_ENGINE + engine->id,
				  &error->flags);
			wake_up_bit(&error->flags,
				    I915_RESET_ENGINE + engine->id);
		}
	}

	if (!engine_mask)
		goto out;

	/* Full reset needs the mutex, stop any other user trying to do so. */
	if (test_and_set_bit(I915_RESET_BACKOFF, &error->flags)) {
		wait_event(error->reset_queue,
			   !test_bit(I915_RESET_BACKOFF, &error->flags));
		goto out; /* piggy-back on the other reset */
	}

	/* Make sure i915_reset_trylock() sees the I915_RESET_BACKOFF */
	synchronize_rcu_expedited();

	/* Prevent any other reset-engine attempt. */
	for_each_engine(engine, i915, tmp) {
		while (test_and_set_bit(I915_RESET_ENGINE + engine->id,
					&error->flags))
			wait_on_bit(&error->flags,
				    I915_RESET_ENGINE + engine->id,
				    TASK_UNINTERRUPTIBLE);
	}

	i915_reset_device(i915, engine_mask, msg);

	for_each_engine(engine, i915, tmp) {
		clear_bit(I915_RESET_ENGINE + engine->id,
			  &error->flags);
	}

	clear_bit(I915_RESET_BACKOFF, &error->flags);
	wake_up_all(&error->reset_queue);

out:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

int i915_reset_trylock(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;
	int srcu;

	might_lock(&error->reset_backoff_srcu);
	might_sleep();

	rcu_read_lock();
	while (test_bit(I915_RESET_BACKOFF, &error->flags)) {
		rcu_read_unlock();

		if (wait_event_interruptible(error->reset_queue,
					     !test_bit(I915_RESET_BACKOFF,
						       &error->flags)))
			return -EINTR;

		rcu_read_lock();
	}
	srcu = srcu_read_lock(&error->reset_backoff_srcu);
	rcu_read_unlock();

	return srcu;
}

void i915_reset_unlock(struct drm_i915_private *i915, int tag)
__releases(&i915->gpu_error.reset_backoff_srcu)
{
	struct i915_gpu_error *error = &i915->gpu_error;

	srcu_read_unlock(&error->reset_backoff_srcu, tag);
}

int i915_terminally_wedged(struct drm_i915_private *i915)
{
	struct i915_gpu_error *error = &i915->gpu_error;

	might_sleep();

	if (!__i915_wedged(error))
		return 0;

	/* Reset still in progress? Maybe we will recover? */
	if (!test_bit(I915_RESET_BACKOFF, &error->flags))
		return -EIO;

	/* XXX intel_reset_finish() still takes struct_mutex!!! */
	if (mutex_is_locked(&i915->drm.struct_mutex))
		return -EAGAIN;

	if (wait_event_interruptible(error->reset_queue,
				     !test_bit(I915_RESET_BACKOFF,
					       &error->flags)))
		return -EINTR;

	return __i915_wedged(error) ? -EIO : 0;
}

static void i915_wedge_me(struct work_struct *work)
{
	struct i915_wedge_me *w = container_of(work, typeof(*w), work.work);

	dev_err(w->i915->drm.dev,
		"%s timed out, cancelling all in-flight rendering.\n",
		w->name);
	i915_gem_set_wedged(w->i915);
}

void __i915_init_wedge(struct i915_wedge_me *w,
		       struct drm_i915_private *i915,
		       long timeout,
		       const char *name)
{
	w->i915 = i915;
	w->name = name;

	INIT_DELAYED_WORK_ONSTACK(&w->work, i915_wedge_me);
	schedule_delayed_work(&w->work, timeout);
}

void __i915_fini_wedge(struct i915_wedge_me *w)
{
	cancel_delayed_work_sync(&w->work);
	destroy_delayed_work_on_stack(&w->work);
	w->i915 = NULL;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_reset.c"
#endif
