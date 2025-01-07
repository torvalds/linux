// SPDX-License-Identifier: MIT
/*
 * Copyright © 2008-2018 Intel Corporation
 */

#include <linux/sched/mm.h>
#include <linux/stop_machine.h>
#include <linux/string_helpers.h>

#include "display/intel_display_reset.h"
#include "display/intel_overlay.h"

#include "gem/i915_gem_context.h"

#include "gt/intel_gt_regs.h"

#include "gt/uc/intel_gsc_fw.h"

#include "i915_drv.h"
#include "i915_file_private.h"
#include "i915_gpu_error.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_breadcrumbs.h"
#include "intel_engine_pm.h"
#include "intel_engine_regs.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_gt_print.h"
#include "intel_gt_requests.h"
#include "intel_mchbar_regs.h"
#include "intel_pci_config.h"
#include "intel_reset.h"

#include "uc/intel_guc.h"

#define RESET_MAX_RETRIES 3

static void client_mark_guilty(struct i915_gem_context *ctx, bool banned)
{
	struct drm_i915_file_private *file_priv = ctx->file_priv;
	unsigned long prev_hang;
	unsigned int score;

	if (IS_ERR_OR_NULL(file_priv))
		return;

	score = 0;
	if (banned)
		score = I915_CLIENT_SCORE_CONTEXT_BAN;

	prev_hang = xchg(&file_priv->hang_timestamp, jiffies);
	if (time_before(jiffies, prev_hang + I915_CLIENT_FAST_HANG_JIFFIES))
		score += I915_CLIENT_SCORE_HANG_FAST;

	if (score) {
		atomic_add(score, &file_priv->ban_score);

		drm_dbg(&ctx->i915->drm,
			"client %s: gained %u ban score, now %u\n",
			ctx->name, score,
			atomic_read(&file_priv->ban_score));
	}
}

static bool mark_guilty(struct i915_request *rq)
{
	struct i915_gem_context *ctx;
	unsigned long prev_hang;
	bool banned;
	int i;

	if (intel_context_is_closed(rq->context))
		return true;

	rcu_read_lock();
	ctx = rcu_dereference(rq->context->gem_context);
	if (ctx && !kref_get_unless_zero(&ctx->ref))
		ctx = NULL;
	rcu_read_unlock();
	if (!ctx)
		return intel_context_is_banned(rq->context);

	atomic_inc(&ctx->guilty_count);

	/* Cool contexts are too cool to be banned! (Used for reset testing.) */
	if (!i915_gem_context_is_bannable(ctx)) {
		banned = false;
		goto out;
	}

	drm_notice(&ctx->i915->drm,
		   "%s context reset due to GPU hang\n",
		   ctx->name);

	/* Record the timestamp for the last N hangs */
	prev_hang = ctx->hang_timestamp[0];
	for (i = 0; i < ARRAY_SIZE(ctx->hang_timestamp) - 1; i++)
		ctx->hang_timestamp[i] = ctx->hang_timestamp[i + 1];
	ctx->hang_timestamp[i] = jiffies;

	/* If we have hung N+1 times in rapid succession, we ban the context! */
	banned = !i915_gem_context_is_recoverable(ctx);
	if (time_before(jiffies, prev_hang + CONTEXT_FAST_HANG_JIFFIES))
		banned = true;
	if (banned)
		drm_dbg(&ctx->i915->drm, "context %s: guilty %d, banned\n",
			ctx->name, atomic_read(&ctx->guilty_count));

	client_mark_guilty(ctx, banned);

out:
	i915_gem_context_put(ctx);
	return banned;
}

static void mark_innocent(struct i915_request *rq)
{
	struct i915_gem_context *ctx;

	rcu_read_lock();
	ctx = rcu_dereference(rq->context->gem_context);
	if (ctx)
		atomic_inc(&ctx->active_count);
	rcu_read_unlock();
}

void __i915_request_reset(struct i915_request *rq, bool guilty)
{
	bool banned = false;

	RQ_TRACE(rq, "guilty? %s\n", str_yes_no(guilty));
	GEM_BUG_ON(__i915_request_is_complete(rq));

	rcu_read_lock(); /* protect the GEM context */
	if (guilty) {
		i915_request_set_error_once(rq, -EIO);
		__i915_request_skip(rq);
		banned = mark_guilty(rq);
	} else {
		i915_request_set_error_once(rq, -EAGAIN);
		mark_innocent(rq);
	}
	rcu_read_unlock();

	if (banned)
		intel_context_ban(rq->context, rq);
}

static bool i915_in_reset(struct pci_dev *pdev)
{
	u8 gdrst;

	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return gdrst & GRDOM_RESET_STATUS;
}

static int i915_do_reset(struct intel_gt *gt,
			 intel_engine_mask_t engine_mask,
			 unsigned int retry)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);
	int err;

	/* Assert reset for at least 50 usec, and wait for acknowledgement. */
	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	udelay(50);
	err = _wait_for_atomic(i915_in_reset(pdev), 50000, 0);

	/* Clear the reset request. */
	pci_write_config_byte(pdev, I915_GDRST, 0);
	udelay(50);
	if (!err)
		err = _wait_for_atomic(!i915_in_reset(pdev), 50000, 0);

	return err;
}

static bool g4x_reset_complete(struct pci_dev *pdev)
{
	u8 gdrst;

	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_ENABLE) == 0;
}

static int g33_do_reset(struct intel_gt *gt,
			intel_engine_mask_t engine_mask,
			unsigned int retry)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);

	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	return _wait_for_atomic(g4x_reset_complete(pdev), 50000, 0);
}

static int g4x_do_reset(struct intel_gt *gt,
			intel_engine_mask_t engine_mask,
			unsigned int retry)
{
	struct pci_dev *pdev = to_pci_dev(gt->i915->drm.dev);
	struct intel_uncore *uncore = gt->uncore;
	int ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	intel_uncore_rmw_fw(uncore, VDECCLK_GATE_D, 0, VCP_UNIT_CLOCK_GATE_DISABLE);
	intel_uncore_posting_read_fw(uncore, VDECCLK_GATE_D);

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	ret =  _wait_for_atomic(g4x_reset_complete(pdev), 50000, 0);
	if (ret) {
		GT_TRACE(gt, "Wait for media reset failed\n");
		goto out;
	}

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  _wait_for_atomic(g4x_reset_complete(pdev), 50000, 0);
	if (ret) {
		GT_TRACE(gt, "Wait for render reset failed\n");
		goto out;
	}

out:
	pci_write_config_byte(pdev, I915_GDRST, 0);

	intel_uncore_rmw_fw(uncore, VDECCLK_GATE_D, VCP_UNIT_CLOCK_GATE_DISABLE, 0);
	intel_uncore_posting_read_fw(uncore, VDECCLK_GATE_D);

	return ret;
}

static int ilk_do_reset(struct intel_gt *gt, intel_engine_mask_t engine_mask,
			unsigned int retry)
{
	struct intel_uncore *uncore = gt->uncore;
	int ret;

	intel_uncore_write_fw(uncore, ILK_GDSR,
			      ILK_GRDOM_RENDER | ILK_GRDOM_RESET_ENABLE);
	ret = __intel_wait_for_register_fw(uncore, ILK_GDSR,
					   ILK_GRDOM_RESET_ENABLE, 0,
					   5000, 0,
					   NULL);
	if (ret) {
		GT_TRACE(gt, "Wait for render reset failed\n");
		goto out;
	}

	intel_uncore_write_fw(uncore, ILK_GDSR,
			      ILK_GRDOM_MEDIA | ILK_GRDOM_RESET_ENABLE);
	ret = __intel_wait_for_register_fw(uncore, ILK_GDSR,
					   ILK_GRDOM_RESET_ENABLE, 0,
					   5000, 0,
					   NULL);
	if (ret) {
		GT_TRACE(gt, "Wait for media reset failed\n");
		goto out;
	}

out:
	intel_uncore_write_fw(uncore, ILK_GDSR, 0);
	intel_uncore_posting_read_fw(uncore, ILK_GDSR);
	return ret;
}

/* Reset the hardware domains (GENX_GRDOM_*) specified by mask */
static int gen6_hw_domain_reset(struct intel_gt *gt, u32 hw_domain_mask)
{
	struct intel_uncore *uncore = gt->uncore;
	int loops;
	int err;

	/*
	 * On some platforms, e.g. Jasperlake, we see that the engine register
	 * state is not cleared until shortly after GDRST reports completion,
	 * causing a failure as we try to immediately resume while the internal
	 * state is still in flux. If we immediately repeat the reset, the
	 * second reset appears to serialise with the first, and since it is a
	 * no-op, the registers should retain their reset value. However, there
	 * is still a concern that upon leaving the second reset, the internal
	 * engine state is still in flux and not ready for resuming.
	 *
	 * Starting on MTL, there are some prep steps that we need to do when
	 * resetting some engines that need to be applied every time we write to
	 * GEN6_GDRST. As those are time consuming (tens of ms), we don't want
	 * to perform that twice, so, since the Jasperlake issue hasn't been
	 * observed on MTL, we avoid repeating the reset on newer platforms.
	 */
	loops = GRAPHICS_VER_FULL(gt->i915) < IP_VER(12, 70) ? 2 : 1;

	/*
	 * GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	do {
		intel_uncore_write_fw(uncore, GEN6_GDRST, hw_domain_mask);

		/* Wait for the device to ack the reset requests. */
		err = __intel_wait_for_register_fw(uncore, GEN6_GDRST,
						   hw_domain_mask, 0,
						   2000, 0,
						   NULL);
	} while (err == 0 && --loops);
	if (err)
		GT_TRACE(gt,
			 "Wait for 0x%08x engines reset failed\n",
			 hw_domain_mask);

	/*
	 * As we have observed that the engine state is still volatile
	 * after GDRST is acked, impose a small delay to let everything settle.
	 */
	udelay(50);

	return err;
}

static int __gen6_reset_engines(struct intel_gt *gt,
				intel_engine_mask_t engine_mask,
				unsigned int retry)
{
	struct intel_engine_cs *engine;
	u32 hw_mask;

	if (engine_mask == ALL_ENGINES) {
		hw_mask = GEN6_GRDOM_FULL;
	} else {
		intel_engine_mask_t tmp;

		hw_mask = 0;
		for_each_engine_masked(engine, gt, engine_mask, tmp) {
			hw_mask |= engine->reset_domain;
		}
	}

	return gen6_hw_domain_reset(gt, hw_mask);
}

static int gen6_reset_engines(struct intel_gt *gt,
			      intel_engine_mask_t engine_mask,
			      unsigned int retry)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&gt->uncore->lock, flags);
	ret = __gen6_reset_engines(gt, engine_mask, retry);
	spin_unlock_irqrestore(&gt->uncore->lock, flags);

	return ret;
}

static struct intel_engine_cs *find_sfc_paired_vecs_engine(struct intel_engine_cs *engine)
{
	int vecs_id;

	GEM_BUG_ON(engine->class != VIDEO_DECODE_CLASS);

	vecs_id = _VECS((engine->instance) / 2);

	return engine->gt->engine[vecs_id];
}

struct sfc_lock_data {
	i915_reg_t lock_reg;
	i915_reg_t ack_reg;
	i915_reg_t usage_reg;
	u32 lock_bit;
	u32 ack_bit;
	u32 usage_bit;
	u32 reset_bit;
};

static void get_sfc_forced_lock_data(struct intel_engine_cs *engine,
				     struct sfc_lock_data *sfc_lock)
{
	switch (engine->class) {
	default:
		MISSING_CASE(engine->class);
		fallthrough;
	case VIDEO_DECODE_CLASS:
		sfc_lock->lock_reg = GEN11_VCS_SFC_FORCED_LOCK(engine->mmio_base);
		sfc_lock->lock_bit = GEN11_VCS_SFC_FORCED_LOCK_BIT;

		sfc_lock->ack_reg = GEN11_VCS_SFC_LOCK_STATUS(engine->mmio_base);
		sfc_lock->ack_bit  = GEN11_VCS_SFC_LOCK_ACK_BIT;

		sfc_lock->usage_reg = GEN11_VCS_SFC_LOCK_STATUS(engine->mmio_base);
		sfc_lock->usage_bit = GEN11_VCS_SFC_USAGE_BIT;
		sfc_lock->reset_bit = GEN11_VCS_SFC_RESET_BIT(engine->instance);

		break;
	case VIDEO_ENHANCEMENT_CLASS:
		sfc_lock->lock_reg = GEN11_VECS_SFC_FORCED_LOCK(engine->mmio_base);
		sfc_lock->lock_bit = GEN11_VECS_SFC_FORCED_LOCK_BIT;

		sfc_lock->ack_reg = GEN11_VECS_SFC_LOCK_ACK(engine->mmio_base);
		sfc_lock->ack_bit  = GEN11_VECS_SFC_LOCK_ACK_BIT;

		sfc_lock->usage_reg = GEN11_VECS_SFC_USAGE(engine->mmio_base);
		sfc_lock->usage_bit = GEN11_VECS_SFC_USAGE_BIT;
		sfc_lock->reset_bit = GEN11_VECS_SFC_RESET_BIT(engine->instance);

		break;
	}
}

static int gen11_lock_sfc(struct intel_engine_cs *engine,
			  u32 *reset_mask,
			  u32 *unlock_mask)
{
	struct intel_uncore *uncore = engine->uncore;
	u8 vdbox_sfc_access = engine->gt->info.vdbox_sfc_access;
	struct sfc_lock_data sfc_lock;
	bool lock_obtained, lock_to_other = false;
	int ret;

	switch (engine->class) {
	case VIDEO_DECODE_CLASS:
		if ((BIT(engine->instance) & vdbox_sfc_access) == 0)
			return 0;

		fallthrough;
	case VIDEO_ENHANCEMENT_CLASS:
		get_sfc_forced_lock_data(engine, &sfc_lock);

		break;
	default:
		return 0;
	}

	if (!(intel_uncore_read_fw(uncore, sfc_lock.usage_reg) & sfc_lock.usage_bit)) {
		struct intel_engine_cs *paired_vecs;

		if (engine->class != VIDEO_DECODE_CLASS ||
		    GRAPHICS_VER(engine->i915) != 12)
			return 0;

		/*
		 * Wa_14010733141
		 *
		 * If the VCS-MFX isn't using the SFC, we also need to check
		 * whether VCS-HCP is using it.  If so, we need to issue a *VE*
		 * forced lock on the VE engine that shares the same SFC.
		 */
		if (!(intel_uncore_read_fw(uncore,
					   GEN12_HCP_SFC_LOCK_STATUS(engine->mmio_base)) &
		      GEN12_HCP_SFC_USAGE_BIT))
			return 0;

		paired_vecs = find_sfc_paired_vecs_engine(engine);
		get_sfc_forced_lock_data(paired_vecs, &sfc_lock);
		lock_to_other = true;
		*unlock_mask |= paired_vecs->mask;
	} else {
		*unlock_mask |= engine->mask;
	}

	/*
	 * If the engine is using an SFC, tell the engine that a software reset
	 * is going to happen. The engine will then try to force lock the SFC.
	 * If SFC ends up being locked to the engine we want to reset, we have
	 * to reset it as well (we will unlock it once the reset sequence is
	 * completed).
	 */
	intel_uncore_rmw_fw(uncore, sfc_lock.lock_reg, 0, sfc_lock.lock_bit);

	ret = __intel_wait_for_register_fw(uncore,
					   sfc_lock.ack_reg,
					   sfc_lock.ack_bit,
					   sfc_lock.ack_bit,
					   1000, 0, NULL);

	/*
	 * Was the SFC released while we were trying to lock it?
	 *
	 * We should reset both the engine and the SFC if:
	 *  - We were locking the SFC to this engine and the lock succeeded
	 *       OR
	 *  - We were locking the SFC to a different engine (Wa_14010733141)
	 *    but the SFC was released before the lock was obtained.
	 *
	 * Otherwise we need only reset the engine by itself and we can
	 * leave the SFC alone.
	 */
	lock_obtained = (intel_uncore_read_fw(uncore, sfc_lock.usage_reg) &
			sfc_lock.usage_bit) != 0;
	if (lock_obtained == lock_to_other)
		return 0;

	if (ret) {
		ENGINE_TRACE(engine, "Wait for SFC forced lock ack failed\n");
		return ret;
	}

	*reset_mask |= sfc_lock.reset_bit;
	return 0;
}

static void gen11_unlock_sfc(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	u8 vdbox_sfc_access = engine->gt->info.vdbox_sfc_access;
	struct sfc_lock_data sfc_lock = {};

	if (engine->class != VIDEO_DECODE_CLASS &&
	    engine->class != VIDEO_ENHANCEMENT_CLASS)
		return;

	if (engine->class == VIDEO_DECODE_CLASS &&
	    (BIT(engine->instance) & vdbox_sfc_access) == 0)
		return;

	get_sfc_forced_lock_data(engine, &sfc_lock);

	intel_uncore_rmw_fw(uncore, sfc_lock.lock_reg, sfc_lock.lock_bit, 0);
}

static int __gen11_reset_engines(struct intel_gt *gt,
				 intel_engine_mask_t engine_mask,
				 unsigned int retry)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;
	u32 reset_mask, unlock_mask = 0;
	int ret;

	if (engine_mask == ALL_ENGINES) {
		reset_mask = GEN11_GRDOM_FULL;
	} else {
		reset_mask = 0;
		for_each_engine_masked(engine, gt, engine_mask, tmp) {
			reset_mask |= engine->reset_domain;
			ret = gen11_lock_sfc(engine, &reset_mask, &unlock_mask);
			if (ret)
				goto sfc_unlock;
		}
	}

	ret = gen6_hw_domain_reset(gt, reset_mask);

sfc_unlock:
	/*
	 * We unlock the SFC based on the lock status and not the result of
	 * gen11_lock_sfc to make sure that we clean properly if something
	 * wrong happened during the lock (e.g. lock acquired after timeout
	 * expiration).
	 *
	 * Due to Wa_14010733141, we may have locked an SFC to an engine that
	 * wasn't being reset.  So instead of calling gen11_unlock_sfc()
	 * on engine_mask, we instead call it on the mask of engines that our
	 * gen11_lock_sfc() calls told us actually had locks attempted.
	 */
	for_each_engine_masked(engine, gt, unlock_mask, tmp)
		gen11_unlock_sfc(engine);

	return ret;
}

static int gen8_engine_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	const i915_reg_t reg = RING_RESET_CTL(engine->mmio_base);
	u32 request, mask, ack;
	int ret;

	if (I915_SELFTEST_ONLY(should_fail(&engine->reset_timeout, 1)))
		return -ETIMEDOUT;

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
		gt_err(engine->gt,
		       "%s reset request timed out: {request: %08x, RESET_CTL: %08x}\n",
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

static int gen8_reset_engines(struct intel_gt *gt,
			      intel_engine_mask_t engine_mask,
			      unsigned int retry)
{
	struct intel_engine_cs *engine;
	const bool reset_non_ready = retry >= 1;
	intel_engine_mask_t tmp;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&gt->uncore->lock, flags);

	for_each_engine_masked(engine, gt, engine_mask, tmp) {
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
		 * stop_engines() we have before the reset.
		 */
	}

	/*
	 * Wa_22011100796:dg2, whenever Full soft reset is required,
	 * reset all individual engines firstly, and then do a full soft reset.
	 *
	 * This is best effort, so ignore any error from the initial reset.
	 */
	if (IS_DG2(gt->i915) && engine_mask == ALL_ENGINES)
		__gen11_reset_engines(gt, gt->info.engine_mask, 0);

	if (GRAPHICS_VER(gt->i915) >= 11)
		ret = __gen11_reset_engines(gt, engine_mask, retry);
	else
		ret = __gen6_reset_engines(gt, engine_mask, retry);

skip_reset:
	for_each_engine_masked(engine, gt, engine_mask, tmp)
		gen8_engine_reset_cancel(engine);

	spin_unlock_irqrestore(&gt->uncore->lock, flags);

	return ret;
}

static int mock_reset(struct intel_gt *gt,
		      intel_engine_mask_t mask,
		      unsigned int retry)
{
	return 0;
}

typedef int (*reset_func)(struct intel_gt *,
			  intel_engine_mask_t engine_mask,
			  unsigned int retry);

static reset_func intel_get_gpu_reset(const struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	if (is_mock_gt(gt))
		return mock_reset;
	else if (GRAPHICS_VER(i915) >= 8)
		return gen8_reset_engines;
	else if (GRAPHICS_VER(i915) >= 6)
		return gen6_reset_engines;
	else if (GRAPHICS_VER(i915) >= 5)
		return ilk_do_reset;
	else if (IS_G4X(i915))
		return g4x_do_reset;
	else if (IS_G33(i915) || IS_PINEVIEW(i915))
		return g33_do_reset;
	else if (GRAPHICS_VER(i915) >= 3)
		return i915_do_reset;
	else
		return NULL;
}

static int __reset_guc(struct intel_gt *gt)
{
	u32 guc_domain =
		GRAPHICS_VER(gt->i915) >= 11 ? GEN11_GRDOM_GUC : GEN9_GRDOM_GUC;

	return gen6_hw_domain_reset(gt, guc_domain);
}

static bool needs_wa_14015076503(struct intel_gt *gt, intel_engine_mask_t engine_mask)
{
	if (MEDIA_VER_FULL(gt->i915) != IP_VER(13, 0) || !HAS_ENGINE(gt, GSC0))
		return false;

	if (!__HAS_ENGINE(engine_mask, GSC0))
		return false;

	return intel_gsc_uc_fw_init_done(&gt->uc.gsc);
}

static intel_engine_mask_t
wa_14015076503_start(struct intel_gt *gt, intel_engine_mask_t engine_mask, bool first)
{
	if (!needs_wa_14015076503(gt, engine_mask))
		return engine_mask;

	/*
	 * wa_14015076503: if the GSC FW is loaded, we need to alert it that
	 * we're going to do a GSC engine reset and then wait for 200ms for the
	 * FW to get ready for it. However, if this is the first ALL_ENGINES
	 * reset attempt and the GSC is not busy, we can try to instead reset
	 * the GuC and all the other engines individually to avoid the 200ms
	 * wait.
	 * Skipping the GSC engine is safe because, differently from other
	 * engines, the GSCCS only role is to forward the commands to the GSC
	 * FW, so it doesn't have any HW outside of the CS itself and therefore
	 * it has no state that we don't explicitly re-init on resume or on
	 * context switch LRC or power context). The HW for the GSC uC is
	 * managed by the GSC FW so we don't need to care about that.
	 */
	if (engine_mask == ALL_ENGINES && first && intel_engine_is_idle(gt->engine[GSC0])) {
		__reset_guc(gt);
		engine_mask = gt->info.engine_mask & ~BIT(GSC0);
	} else {
		intel_uncore_rmw(gt->uncore,
				 HECI_H_GS1(MTL_GSC_HECI2_BASE),
				 0, HECI_H_GS1_ER_PREP);

		/* make sure the reset bit is clear when writing the CSR reg */
		intel_uncore_rmw(gt->uncore,
				 HECI_H_CSR(MTL_GSC_HECI2_BASE),
				 HECI_H_CSR_RST, HECI_H_CSR_IG);
		msleep(200);
	}

	return engine_mask;
}

static void
wa_14015076503_end(struct intel_gt *gt, intel_engine_mask_t engine_mask)
{
	if (!needs_wa_14015076503(gt, engine_mask))
		return;

	intel_uncore_rmw(gt->uncore,
			 HECI_H_GS1(MTL_GSC_HECI2_BASE),
			 HECI_H_GS1_ER_PREP, 0);
}

static int __intel_gt_reset(struct intel_gt *gt, intel_engine_mask_t engine_mask)
{
	const int retries = engine_mask == ALL_ENGINES ? RESET_MAX_RETRIES : 1;
	reset_func reset;
	int ret = -ETIMEDOUT;
	int retry;

	reset = intel_get_gpu_reset(gt);
	if (!reset)
		return -ENODEV;

	/*
	 * If the power well sleeps during the reset, the reset
	 * request may be dropped and never completes (causing -EIO).
	 */
	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);
	for (retry = 0; ret == -ETIMEDOUT && retry < retries; retry++) {
		intel_engine_mask_t reset_mask;

		reset_mask = wa_14015076503_start(gt, engine_mask, !retry);

		GT_TRACE(gt, "engine_mask=%x\n", reset_mask);
		ret = reset(gt, reset_mask, retry);

		wa_14015076503_end(gt, reset_mask);
	}
	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);

	return ret;
}

bool intel_has_gpu_reset(const struct intel_gt *gt)
{
	if (!gt->i915->params.reset)
		return NULL;

	return intel_get_gpu_reset(gt);
}

bool intel_has_reset_engine(const struct intel_gt *gt)
{
	if (gt->i915->params.reset < 2)
		return false;

	return INTEL_INFO(gt->i915)->has_reset_engine;
}

int intel_reset_guc(struct intel_gt *gt)
{
	int ret;

	GEM_BUG_ON(!HAS_GT_UC(gt->i915));

	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);
	ret = __reset_guc(gt);
	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);

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
	intel_uncore_forcewake_get(engine->uncore, FORCEWAKE_ALL);
	if (engine->reset.prepare)
		engine->reset.prepare(engine);
}

static void revoke_mmaps(struct intel_gt *gt)
{
	int i;

	for (i = 0; i < gt->ggtt->num_fences; i++) {
		struct drm_vma_offset_node *node;
		struct i915_vma *vma;
		u64 vma_offset;

		vma = READ_ONCE(gt->ggtt->fence_regs[i].vma);
		if (!vma)
			continue;

		if (!i915_vma_has_userfault(vma))
			continue;

		GEM_BUG_ON(vma->fence != &gt->ggtt->fence_regs[i]);

		if (!vma->mmo)
			continue;

		node = &vma->mmo->vma_node;
		vma_offset = vma->gtt_view.partial.offset << PAGE_SHIFT;

		unmap_mapping_range(gt->i915->drm.anon_inode->i_mapping,
				    drm_vma_node_offset_addr(node) + vma_offset,
				    vma->size,
				    1);
	}
}

static intel_engine_mask_t reset_prepare(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t awake = 0;
	enum intel_engine_id id;

	/**
	 * For GuC mode with submission enabled, ensure submission
	 * is disabled before stopping ring.
	 *
	 * For GuC mode with submission disabled, ensure that GuC is not
	 * sanitized, do that after engine reset. reset_prepare()
	 * is followed by engine reset which in this mode requires GuC to
	 * process any CSB FIFO entries generated by the resets.
	 */
	if (intel_uc_uses_guc_submission(&gt->uc))
		intel_uc_reset_prepare(&gt->uc);

	for_each_engine(engine, gt, id) {
		if (intel_engine_pm_get_if_awake(engine))
			awake |= engine->mask;
		reset_prepare_engine(engine);
	}

	return awake;
}

static void gt_revoke(struct intel_gt *gt)
{
	revoke_mmaps(gt);
}

static int gt_reset(struct intel_gt *gt, intel_engine_mask_t stalled_mask)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	/*
	 * Everything depends on having the GTT running, so we need to start
	 * there.
	 */
	err = i915_ggtt_enable_hw(gt->i915);
	if (err)
		return err;

	local_bh_disable();
	for_each_engine(engine, gt, id)
		__intel_engine_reset(engine, stalled_mask & engine->mask);
	local_bh_enable();

	intel_uc_reset(&gt->uc, ALL_ENGINES);

	intel_ggtt_restore_fences(gt->ggtt);

	return err;
}

static void reset_finish_engine(struct intel_engine_cs *engine)
{
	if (engine->reset.finish)
		engine->reset.finish(engine);
	intel_uncore_forcewake_put(engine->uncore, FORCEWAKE_ALL);

	intel_engine_signal_breadcrumbs(engine);
}

static void reset_finish(struct intel_gt *gt, intel_engine_mask_t awake)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id) {
		reset_finish_engine(engine);
		if (awake & engine->mask)
			intel_engine_pm_put(engine);
	}

	intel_uc_reset_finish(&gt->uc);
}

static void nop_submit_request(struct i915_request *request)
{
	RQ_TRACE(request, "-EIO\n");

	request = i915_request_mark_eio(request);
	if (request) {
		i915_request_submit(request);
		intel_engine_signal_breadcrumbs(request->engine);

		i915_request_put(request);
	}
}

static void __intel_gt_set_wedged(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t awake;
	enum intel_engine_id id;

	if (test_bit(I915_WEDGED, &gt->reset.flags))
		return;

	GT_TRACE(gt, "start\n");

	/*
	 * First, stop submission to hw, but do not yet complete requests by
	 * rolling the global seqno forward (since this would complete requests
	 * for which we haven't set the fence error to EIO yet).
	 */
	awake = reset_prepare(gt);

	/* Even if the GPU reset fails, it should still stop the engines */
	if (!INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		intel_gt_reset_all_engines(gt);

	for_each_engine(engine, gt, id)
		engine->submit_request = nop_submit_request;

	/*
	 * Make sure no request can slip through without getting completed by
	 * either this call here to intel_engine_write_global_seqno, or the one
	 * in nop_submit_request.
	 */
	synchronize_rcu_expedited();
	set_bit(I915_WEDGED, &gt->reset.flags);

	/* Mark all executing requests as skipped */
	local_bh_disable();
	for_each_engine(engine, gt, id)
		if (engine->reset.cancel)
			engine->reset.cancel(engine);
	intel_uc_cancel_requests(&gt->uc);
	local_bh_enable();

	reset_finish(gt, awake);

	GT_TRACE(gt, "end\n");
}

static void set_wedged_work(struct work_struct *w)
{
	struct intel_gt *gt = container_of(w, struct intel_gt, wedge);
	intel_wakeref_t wf;

	with_intel_runtime_pm(gt->uncore->rpm, wf)
		__intel_gt_set_wedged(gt);
}

void intel_gt_set_wedged(struct intel_gt *gt)
{
	intel_wakeref_t wakeref;

	if (test_bit(I915_WEDGED, &gt->reset.flags))
		return;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	mutex_lock(&gt->reset.mutex);

	if (GEM_SHOW_DEBUG()) {
		struct drm_printer p = drm_dbg_printer(&gt->i915->drm,
						       DRM_UT_DRIVER, NULL);
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		drm_printf(&p, "called from %pS\n", (void *)_RET_IP_);
		for_each_engine(engine, gt, id) {
			if (intel_engine_is_idle(engine))
				continue;

			intel_engine_dump(engine, &p, "%s\n", engine->name);
		}
	}

	__intel_gt_set_wedged(gt);

	mutex_unlock(&gt->reset.mutex);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}

static bool __intel_gt_unset_wedged(struct intel_gt *gt)
{
	struct intel_gt_timelines *timelines = &gt->timelines;
	struct intel_timeline *tl;
	bool ok;

	if (!test_bit(I915_WEDGED, &gt->reset.flags))
		return true;

	/* Never fully initialised, recovery impossible */
	if (intel_gt_has_unrecoverable_error(gt))
		return false;

	GT_TRACE(gt, "start\n");

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
	spin_lock(&timelines->lock);
	list_for_each_entry(tl, &timelines->active_list, link) {
		struct dma_fence *fence;

		fence = i915_active_fence_get(&tl->last_request);
		if (!fence)
			continue;

		spin_unlock(&timelines->lock);

		/*
		 * All internal dependencies (i915_requests) will have
		 * been flushed by the set-wedge, but we may be stuck waiting
		 * for external fences. These should all be capped to 10s
		 * (I915_FENCE_TIMEOUT) so this wait should not be unbounded
		 * in the worst case.
		 */
		dma_fence_default_wait(fence, false, MAX_SCHEDULE_TIMEOUT);
		dma_fence_put(fence);

		/* Restart iteration after droping lock */
		spin_lock(&timelines->lock);
		tl = list_entry(&timelines->active_list, typeof(*tl), link);
	}
	spin_unlock(&timelines->lock);

	/* We must reset pending GPU events before restoring our submission */
	ok = !HAS_EXECLISTS(gt->i915); /* XXX better agnosticism desired */
	if (!INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		ok = intel_gt_reset_all_engines(gt) == 0;
	if (!ok) {
		/*
		 * Warn CI about the unrecoverable wedged condition.
		 * Time for a reboot.
		 */
		add_taint_for_CI(gt->i915, TAINT_WARN);
		return false;
	}

	/*
	 * Undo nop_submit_request. We prevent all new i915 requests from
	 * being queued (by disallowing execbuf whilst wedged) so having
	 * waited for all active requests above, we know the system is idle
	 * and do not have to worry about a thread being inside
	 * engine->submit_request() as we swap over. So unlike installing
	 * the nop_submit_request on reset, we can do this from normal
	 * context and do not require stop_machine().
	 */
	intel_engines_reset_default_submission(gt);

	GT_TRACE(gt, "end\n");

	smp_mb__before_atomic(); /* complete takeover before enabling execbuf */
	clear_bit(I915_WEDGED, &gt->reset.flags);

	return true;
}

bool intel_gt_unset_wedged(struct intel_gt *gt)
{
	bool result;

	mutex_lock(&gt->reset.mutex);
	result = __intel_gt_unset_wedged(gt);
	mutex_unlock(&gt->reset.mutex);

	return result;
}

static int do_reset(struct intel_gt *gt, intel_engine_mask_t stalled_mask)
{
	int err, i;

	err = intel_gt_reset_all_engines(gt);
	for (i = 0; err && i < RESET_MAX_RETRIES; i++) {
		msleep(10 * (i + 1));
		err = intel_gt_reset_all_engines(gt);
	}
	if (err)
		return err;

	return gt_reset(gt, stalled_mask);
}

static int resume(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int ret;

	for_each_engine(engine, gt, id) {
		ret = intel_engine_resume(engine);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * intel_gt_reset - reset chip after a hang
 * @gt: #intel_gt to reset
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
void intel_gt_reset(struct intel_gt *gt,
		    intel_engine_mask_t stalled_mask,
		    const char *reason)
{
	struct intel_display *display = &gt->i915->display;
	intel_engine_mask_t awake;
	int ret;

	GT_TRACE(gt, "flags=%lx\n", gt->reset.flags);

	might_sleep();
	GEM_BUG_ON(!test_bit(I915_RESET_BACKOFF, &gt->reset.flags));

	/*
	 * FIXME: Revoking cpu mmap ptes cannot be done from a dma_fence
	 * critical section like gpu reset.
	 */
	gt_revoke(gt);

	mutex_lock(&gt->reset.mutex);

	/* Clear any previous failed attempts at recovery. Time to try again. */
	if (!__intel_gt_unset_wedged(gt))
		goto unlock;

	if (reason)
		gt_notice(gt, "Resetting chip for %s\n", reason);
	atomic_inc(&gt->i915->gpu_error.reset_count);

	awake = reset_prepare(gt);

	if (!intel_has_gpu_reset(gt)) {
		if (gt->i915->params.reset)
			gt_err(gt, "GPU reset not supported\n");
		else
			gt_dbg(gt, "GPU reset disabled\n");
		goto error;
	}

	if (INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		intel_irq_suspend(gt->i915);

	if (do_reset(gt, stalled_mask)) {
		gt_err(gt, "Failed to reset chip\n");
		goto taint;
	}

	if (INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		intel_irq_resume(gt->i915);

	intel_overlay_reset(display);

	/* sanitize uC after engine reset */
	if (!intel_uc_uses_guc_submission(&gt->uc))
		intel_uc_reset_prepare(&gt->uc);
	/*
	 * Next we need to restore the context, but we don't use those
	 * yet either...
	 *
	 * Ring buffer needs to be re-initialized in the KMS case, or if X
	 * was running at the time of the reset (i.e. we weren't VT
	 * switched away).
	 */
	ret = intel_gt_init_hw(gt);
	if (ret) {
		gt_err(gt, "Failed to initialise HW following reset (%d)\n", ret);
		goto taint;
	}

	ret = resume(gt);
	if (ret)
		goto taint;

finish:
	reset_finish(gt, awake);
unlock:
	mutex_unlock(&gt->reset.mutex);
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
	add_taint_for_CI(gt->i915, TAINT_WARN);
error:
	__intel_gt_set_wedged(gt);
	goto finish;
}

/**
 * intel_gt_reset_all_engines() - Reset all engines in the given gt.
 * @gt: the GT to reset all engines for.
 *
 * This function resets all engines within the given gt.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int intel_gt_reset_all_engines(struct intel_gt *gt)
{
	return __intel_gt_reset(gt, ALL_ENGINES);
}

/**
 * intel_gt_reset_engine() - Reset a specific engine within a gt.
 * @engine: engine to be reset.
 *
 * This function resets the specified engine within a gt.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int intel_gt_reset_engine(struct intel_engine_cs *engine)
{
	return __intel_gt_reset(engine->gt, engine->mask);
}

int __intel_engine_reset_bh(struct intel_engine_cs *engine, const char *msg)
{
	struct intel_gt *gt = engine->gt;
	int ret;

	ENGINE_TRACE(engine, "flags=%lx\n", gt->reset.flags);
	GEM_BUG_ON(!test_bit(I915_RESET_ENGINE + engine->id, &gt->reset.flags));

	if (intel_engine_uses_guc(engine))
		return -ENODEV;

	if (!intel_engine_pm_get_if_awake(engine))
		return 0;

	reset_prepare_engine(engine);

	if (msg)
		drm_notice(&engine->i915->drm,
			   "Resetting %s for %s\n", engine->name, msg);
	i915_increase_reset_engine_count(&engine->i915->gpu_error, engine);

	ret = intel_gt_reset_engine(engine);
	if (ret) {
		/* If we fail here, we expect to fallback to a global reset */
		ENGINE_TRACE(engine, "Failed to reset %s, err: %d\n", engine->name, ret);
		goto out;
	}

	/*
	 * The request that caused the hang is stuck on elsp, we know the
	 * active request and can drop it, adjust head to skip the offending
	 * request to resume executing remaining requests in the queue.
	 */
	__intel_engine_reset(engine, true);

	/*
	 * The engine and its registers (and workarounds in case of render)
	 * have been reset to their default values. Follow the init_ring
	 * process to program RING_MODE, HWSP and re-enable submission.
	 */
	ret = intel_engine_resume(engine);

out:
	intel_engine_cancel_stop_cs(engine);
	reset_finish_engine(engine);
	intel_engine_pm_put_async(engine);
	return ret;
}

/**
 * intel_engine_reset - reset GPU engine to recover from a hang
 * @engine: engine to reset
 * @msg: reason for GPU reset; or NULL for no drm_notice()
 *
 * Reset a specific GPU engine. Useful if a hang is detected.
 * Returns zero on successful reset or otherwise an error code.
 *
 * Procedure is:
 *  - identifies the request that caused the hang and it is dropped
 *  - reset engine (which will force the engine to idle)
 *  - re-init/configure engine
 */
int intel_engine_reset(struct intel_engine_cs *engine, const char *msg)
{
	int err;

	local_bh_disable();
	err = __intel_engine_reset_bh(engine, msg);
	local_bh_enable();

	return err;
}

static void intel_gt_reset_global(struct intel_gt *gt,
				  u32 engine_mask,
				  const char *reason)
{
	struct kobject *kobj = &gt->i915->drm.primary->kdev->kobj;
	char *error_event[] = { I915_ERROR_UEVENT "=1", NULL };
	char *reset_event[] = { I915_RESET_UEVENT "=1", NULL };
	char *reset_done_event[] = { I915_ERROR_UEVENT "=0", NULL };
	struct intel_wedge_me w;

	kobject_uevent_env(kobj, KOBJ_CHANGE, error_event);

	GT_TRACE(gt, "resetting chip, engines=%x\n", engine_mask);
	kobject_uevent_env(kobj, KOBJ_CHANGE, reset_event);

	/* Use a watchdog to ensure that our reset completes */
	intel_wedge_on_timeout(&w, gt, 60 * HZ) {
		intel_display_reset_prepare(gt->i915);

		intel_gt_reset(gt, engine_mask, reason);

		intel_display_reset_finish(gt->i915);
	}

	if (!test_bit(I915_WEDGED, &gt->reset.flags))
		kobject_uevent_env(kobj, KOBJ_CHANGE, reset_done_event);
}

/**
 * intel_gt_handle_error - handle a gpu error
 * @gt: the intel_gt
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
void intel_gt_handle_error(struct intel_gt *gt,
			   intel_engine_mask_t engine_mask,
			   unsigned long flags,
			   const char *fmt, ...)
{
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
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	engine_mask &= gt->info.engine_mask;

	if (flags & I915_ERROR_CAPTURE) {
		i915_capture_error_state(gt, engine_mask, CORE_DUMP_FLAG_NONE);
		intel_gt_clear_error_registers(gt, engine_mask);
	}

	/*
	 * Try engine reset when available. We fall back to full reset if
	 * single reset fails.
	 */
	if (!intel_uc_uses_guc_submission(&gt->uc) &&
	    intel_has_reset_engine(gt) && !intel_gt_is_wedged(gt)) {
		local_bh_disable();
		for_each_engine_masked(engine, gt, engine_mask, tmp) {
			BUILD_BUG_ON(I915_RESET_MODESET >= I915_RESET_ENGINE);
			if (test_and_set_bit(I915_RESET_ENGINE + engine->id,
					     &gt->reset.flags))
				continue;

			if (__intel_engine_reset_bh(engine, msg) == 0)
				engine_mask &= ~engine->mask;

			clear_and_wake_up_bit(I915_RESET_ENGINE + engine->id,
					      &gt->reset.flags);
		}
		local_bh_enable();
	}

	if (!engine_mask)
		goto out;

	/* Full reset needs the mutex, stop any other user trying to do so. */
	if (test_and_set_bit(I915_RESET_BACKOFF, &gt->reset.flags)) {
		wait_event(gt->reset.queue,
			   !test_bit(I915_RESET_BACKOFF, &gt->reset.flags));
		goto out; /* piggy-back on the other reset */
	}

	/* Make sure i915_reset_trylock() sees the I915_RESET_BACKOFF */
	synchronize_rcu_expedited();

	/*
	 * Prevent any other reset-engine attempt. We don't do this for GuC
	 * submission the GuC owns the per-engine reset, not the i915.
	 */
	if (!intel_uc_uses_guc_submission(&gt->uc)) {
		for_each_engine(engine, gt, tmp) {
			while (test_and_set_bit(I915_RESET_ENGINE + engine->id,
						&gt->reset.flags))
				wait_on_bit(&gt->reset.flags,
					    I915_RESET_ENGINE + engine->id,
					    TASK_UNINTERRUPTIBLE);
		}
	}

	/* Flush everyone using a resource about to be clobbered */
	synchronize_srcu_expedited(&gt->reset.backoff_srcu);

	intel_gt_reset_global(gt, engine_mask, msg);

	if (!intel_uc_uses_guc_submission(&gt->uc)) {
		for_each_engine(engine, gt, tmp)
			clear_bit_unlock(I915_RESET_ENGINE + engine->id,
					 &gt->reset.flags);
	}
	clear_bit_unlock(I915_RESET_BACKOFF, &gt->reset.flags);
	smp_mb__after_atomic();
	wake_up_all(&gt->reset.queue);

out:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}

static int _intel_gt_reset_lock(struct intel_gt *gt, int *srcu, bool retry)
{
	might_lock(&gt->reset.backoff_srcu);
	if (retry)
		might_sleep();

	rcu_read_lock();
	while (test_bit(I915_RESET_BACKOFF, &gt->reset.flags)) {
		rcu_read_unlock();

		if (!retry)
			return -EBUSY;

		if (wait_event_interruptible(gt->reset.queue,
					     !test_bit(I915_RESET_BACKOFF,
						       &gt->reset.flags)))
			return -EINTR;

		rcu_read_lock();
	}
	*srcu = srcu_read_lock(&gt->reset.backoff_srcu);
	rcu_read_unlock();

	return 0;
}

int intel_gt_reset_trylock(struct intel_gt *gt, int *srcu)
{
	return _intel_gt_reset_lock(gt, srcu, false);
}

int intel_gt_reset_lock_interruptible(struct intel_gt *gt, int *srcu)
{
	return _intel_gt_reset_lock(gt, srcu, true);
}

void intel_gt_reset_unlock(struct intel_gt *gt, int tag)
__releases(&gt->reset.backoff_srcu)
{
	srcu_read_unlock(&gt->reset.backoff_srcu, tag);
}

int intel_gt_terminally_wedged(struct intel_gt *gt)
{
	might_sleep();

	if (!intel_gt_is_wedged(gt))
		return 0;

	if (intel_gt_has_unrecoverable_error(gt))
		return -EIO;

	/* Reset still in progress? Maybe we will recover? */
	if (wait_event_interruptible(gt->reset.queue,
				     !test_bit(I915_RESET_BACKOFF,
					       &gt->reset.flags)))
		return -EINTR;

	return intel_gt_is_wedged(gt) ? -EIO : 0;
}

void intel_gt_set_wedged_on_init(struct intel_gt *gt)
{
	BUILD_BUG_ON(I915_RESET_ENGINE + I915_NUM_ENGINES >
		     I915_WEDGED_ON_INIT);
	intel_gt_set_wedged(gt);
	i915_disable_error_state(gt->i915, -ENODEV);
	set_bit(I915_WEDGED_ON_INIT, &gt->reset.flags);

	/* Wedged on init is non-recoverable */
	add_taint_for_CI(gt->i915, TAINT_WARN);
}

void intel_gt_set_wedged_on_fini(struct intel_gt *gt)
{
	intel_gt_set_wedged(gt);
	i915_disable_error_state(gt->i915, -ENODEV);
	set_bit(I915_WEDGED_ON_FINI, &gt->reset.flags);
	intel_gt_retire_requests(gt); /* cleanup any wedged requests */
}

void intel_gt_init_reset(struct intel_gt *gt)
{
	init_waitqueue_head(&gt->reset.queue);
	mutex_init(&gt->reset.mutex);
	init_srcu_struct(&gt->reset.backoff_srcu);
	INIT_WORK(&gt->wedge, set_wedged_work);

	/*
	 * While undesirable to wait inside the shrinker, complain anyway.
	 *
	 * If we have to wait during shrinking, we guarantee forward progress
	 * by forcing the reset. Therefore during the reset we must not
	 * re-enter the shrinker. By declaring that we take the reset mutex
	 * within the shrinker, we forbid ourselves from performing any
	 * fs-reclaim or taking related locks during reset.
	 */
	i915_gem_shrinker_taints_mutex(gt->i915, &gt->reset.mutex);

	/* no GPU until we are ready! */
	__set_bit(I915_WEDGED, &gt->reset.flags);
}

void intel_gt_fini_reset(struct intel_gt *gt)
{
	cleanup_srcu_struct(&gt->reset.backoff_srcu);
}

static void intel_wedge_me(struct work_struct *work)
{
	struct intel_wedge_me *w = container_of(work, typeof(*w), work.work);

	gt_err(w->gt, "%s timed out, cancelling all in-flight rendering.\n", w->name);
	set_wedged_work(&w->gt->wedge);
}

void __intel_init_wedge(struct intel_wedge_me *w,
			struct intel_gt *gt,
			long timeout,
			const char *name)
{
	w->gt = gt;
	w->name = name;

	INIT_DELAYED_WORK_ONSTACK(&w->work, intel_wedge_me);
	queue_delayed_work(gt->i915->unordered_wq, &w->work, timeout);
}

void __intel_fini_wedge(struct intel_wedge_me *w)
{
	cancel_delayed_work_sync(&w->work);
	destroy_delayed_work_on_stack(&w->work);
	w->gt = NULL;
}

/*
 * Wa_22011802037 requires that we (or the GuC) ensure that no command
 * streamers are executing MI_FORCE_WAKE while an engine reset is initiated.
 */
bool intel_engine_reset_needs_wa_22011802037(struct intel_gt *gt)
{
	if (GRAPHICS_VER(gt->i915) < 11)
		return false;

	if (IS_GFX_GT_IP_STEP(gt, IP_VER(12, 70), STEP_A0, STEP_B0))
		return true;

	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70))
		return false;

	return true;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_reset.c"
#include "selftest_hangcheck.c"
#endif
