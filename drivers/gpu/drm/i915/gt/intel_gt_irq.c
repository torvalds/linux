// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/sched/clock.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_breadcrumbs.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_gt_print.h"
#include "intel_gt_regs.h"
#include "intel_uncore.h"
#include "intel_rps.h"
#include "pxp/intel_pxp_irq.h"
#include "uc/intel_gsc_proxy.h"

static void guc_irq_handler(struct intel_guc *guc, u16 iir)
{
	if (unlikely(!guc->interrupts.enabled))
		return;

	if (iir & GUC_INTR_GUC2HOST)
		intel_guc_to_host_event_handler(guc);
}

static u32
gen11_gt_engine_identity(struct intel_gt *gt,
			 const unsigned int bank, const unsigned int bit)
{
	void __iomem * const regs = intel_uncore_regs(gt->uncore);
	u32 timeout_ts;
	u32 ident;

	lockdep_assert_held(gt->irq_lock);

	raw_reg_write(regs, GEN11_IIR_REG_SELECTOR(bank), BIT(bit));

	/*
	 * NB: Specs do not specify how long to spin wait,
	 * so we do ~100us as an educated guess.
	 */
	timeout_ts = (local_clock() >> 10) + 100;
	do {
		ident = raw_reg_read(regs, GEN11_INTR_IDENTITY_REG(bank));
	} while (!(ident & GEN11_INTR_DATA_VALID) &&
		 !time_after32(local_clock() >> 10, timeout_ts));

	if (unlikely(!(ident & GEN11_INTR_DATA_VALID))) {
		gt_err(gt, "INTR_IDENTITY_REG%u:%u 0x%08x not valid!\n",
		       bank, bit, ident);
		return 0;
	}

	raw_reg_write(regs, GEN11_INTR_IDENTITY_REG(bank),
		      GEN11_INTR_DATA_VALID);

	return ident;
}

static void
gen11_other_irq_handler(struct intel_gt *gt, const u8 instance,
			const u16 iir)
{
	struct intel_gt *media_gt = gt->i915->media_gt;

	if (instance == OTHER_GUC_INSTANCE)
		return guc_irq_handler(gt_to_guc(gt), iir);
	if (instance == OTHER_MEDIA_GUC_INSTANCE && media_gt)
		return guc_irq_handler(gt_to_guc(media_gt), iir);

	if (instance == OTHER_GTPM_INSTANCE)
		return gen11_rps_irq_handler(&gt->rps, iir);
	if (instance == OTHER_MEDIA_GTPM_INSTANCE && media_gt)
		return gen11_rps_irq_handler(&media_gt->rps, iir);

	if (instance == OTHER_KCR_INSTANCE)
		return intel_pxp_irq_handler(gt->i915->pxp, iir);

	if (instance == OTHER_GSC_INSTANCE)
		return intel_gsc_irq_handler(gt, iir);

	if (instance == OTHER_GSC_HECI_2_INSTANCE)
		return intel_gsc_proxy_irq_handler(&gt->uc.gsc, iir);

	WARN_ONCE(1, "unhandled other interrupt instance=0x%x, iir=0x%x\n",
		  instance, iir);
}

static struct intel_gt *pick_gt(struct intel_gt *gt, u8 class, u8 instance)
{
	struct intel_gt *media_gt = gt->i915->media_gt;

	/* we expect the non-media gt to be passed in */
	GEM_BUG_ON(gt == media_gt);

	if (!media_gt)
		return gt;

	switch (class) {
	case VIDEO_DECODE_CLASS:
	case VIDEO_ENHANCEMENT_CLASS:
		return media_gt;
	case OTHER_CLASS:
		if (instance == OTHER_GSC_HECI_2_INSTANCE)
			return media_gt;
		if ((instance == OTHER_GSC_INSTANCE || instance == OTHER_KCR_INSTANCE) &&
		    HAS_ENGINE(media_gt, GSC0))
			return media_gt;
		fallthrough;
	default:
		return gt;
	}
}

static void
gen11_gt_identity_handler(struct intel_gt *gt, const u32 identity)
{
	const u8 class = GEN11_INTR_ENGINE_CLASS(identity);
	const u8 instance = GEN11_INTR_ENGINE_INSTANCE(identity);
	const u16 intr = GEN11_INTR_ENGINE_INTR(identity);

	if (unlikely(!intr))
		return;

	/*
	 * Platforms with standalone media have the media and GSC engines in
	 * another GT.
	 */
	gt = pick_gt(gt, class, instance);

	if (class <= MAX_ENGINE_CLASS && instance <= MAX_ENGINE_INSTANCE) {
		struct intel_engine_cs *engine = gt->engine_class[class][instance];
		if (engine)
			return intel_engine_cs_irq(engine, intr);
	}

	if (class == OTHER_CLASS)
		return gen11_other_irq_handler(gt, instance, intr);

	WARN_ONCE(1, "unknown interrupt class=0x%x, instance=0x%x, intr=0x%x\n",
		  class, instance, intr);
}

static void
gen11_gt_bank_handler(struct intel_gt *gt, const unsigned int bank)
{
	void __iomem * const regs = intel_uncore_regs(gt->uncore);
	unsigned long intr_dw;
	unsigned int bit;

	lockdep_assert_held(gt->irq_lock);

	intr_dw = raw_reg_read(regs, GEN11_GT_INTR_DW(bank));

	for_each_set_bit(bit, &intr_dw, 32) {
		const u32 ident = gen11_gt_engine_identity(gt, bank, bit);

		gen11_gt_identity_handler(gt, ident);
	}

	/* Clear must be after shared has been served for engine */
	raw_reg_write(regs, GEN11_GT_INTR_DW(bank), intr_dw);
}

void gen11_gt_irq_handler(struct intel_gt *gt, const u32 master_ctl)
{
	unsigned int bank;

	spin_lock(gt->irq_lock);

	for (bank = 0; bank < 2; bank++) {
		if (master_ctl & GEN11_GT_DW_IRQ(bank))
			gen11_gt_bank_handler(gt, bank);
	}

	spin_unlock(gt->irq_lock);
}

bool gen11_gt_reset_one_iir(struct intel_gt *gt,
			    const unsigned int bank, const unsigned int bit)
{
	void __iomem * const regs = intel_uncore_regs(gt->uncore);
	u32 dw;

	lockdep_assert_held(gt->irq_lock);

	dw = raw_reg_read(regs, GEN11_GT_INTR_DW(bank));
	if (dw & BIT(bit)) {
		/*
		 * According to the BSpec, DW_IIR bits cannot be cleared without
		 * first servicing the Selector & Shared IIR registers.
		 */
		gen11_gt_engine_identity(gt, bank, bit);

		/*
		 * We locked GT INT DW by reading it. If we want to (try
		 * to) recover from this successfully, we need to clear
		 * our bit, otherwise we are locking the register for
		 * everybody.
		 */
		raw_reg_write(regs, GEN11_GT_INTR_DW(bank), BIT(bit));

		return true;
	}

	return false;
}

void gen11_gt_irq_reset(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;

	/* Disable RCS, BCS, VCS and VECS class engines. */
	intel_uncore_write(uncore, GEN11_RENDER_COPY_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_VCS_VECS_INTR_ENABLE,	  0);
	if (CCS_MASK(gt))
		intel_uncore_write(uncore, GEN12_CCS_RSVD_INTR_ENABLE, 0);
	if (HAS_HECI_GSC(gt->i915) || HAS_ENGINE(gt, GSC0))
		intel_uncore_write(uncore, GEN11_GUNIT_CSME_INTR_ENABLE, 0);

	/* Restore masks irqs on RCS, BCS, VCS and VECS engines. */
	intel_uncore_write(uncore, GEN11_RCS0_RSVD_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_BCS_RSVD_INTR_MASK,	~0);
	if (HAS_ENGINE(gt, BCS1) || HAS_ENGINE(gt, BCS2))
		intel_uncore_write(uncore, XEHPC_BCS1_BCS2_INTR_MASK, ~0);
	if (HAS_ENGINE(gt, BCS3) || HAS_ENGINE(gt, BCS4))
		intel_uncore_write(uncore, XEHPC_BCS3_BCS4_INTR_MASK, ~0);
	if (HAS_ENGINE(gt, BCS5) || HAS_ENGINE(gt, BCS6))
		intel_uncore_write(uncore, XEHPC_BCS5_BCS6_INTR_MASK, ~0);
	if (HAS_ENGINE(gt, BCS7) || HAS_ENGINE(gt, BCS8))
		intel_uncore_write(uncore, XEHPC_BCS7_BCS8_INTR_MASK, ~0);
	intel_uncore_write(uncore, GEN11_VCS0_VCS1_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_VCS2_VCS3_INTR_MASK,	~0);
	if (HAS_ENGINE(gt, VCS4) || HAS_ENGINE(gt, VCS5))
		intel_uncore_write(uncore, GEN12_VCS4_VCS5_INTR_MASK,   ~0);
	if (HAS_ENGINE(gt, VCS6) || HAS_ENGINE(gt, VCS7))
		intel_uncore_write(uncore, GEN12_VCS6_VCS7_INTR_MASK,   ~0);
	intel_uncore_write(uncore, GEN11_VECS0_VECS1_INTR_MASK,	~0);
	if (HAS_ENGINE(gt, VECS2) || HAS_ENGINE(gt, VECS3))
		intel_uncore_write(uncore, GEN12_VECS2_VECS3_INTR_MASK, ~0);
	if (HAS_ENGINE(gt, CCS0) || HAS_ENGINE(gt, CCS1))
		intel_uncore_write(uncore, GEN12_CCS0_CCS1_INTR_MASK, ~0);
	if (HAS_ENGINE(gt, CCS2) || HAS_ENGINE(gt, CCS3))
		intel_uncore_write(uncore, GEN12_CCS2_CCS3_INTR_MASK, ~0);
	if (HAS_HECI_GSC(gt->i915) || HAS_ENGINE(gt, GSC0))
		intel_uncore_write(uncore, GEN11_GUNIT_CSME_INTR_MASK, ~0);

	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK,  ~0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_MASK,  ~0);

	intel_uncore_write(uncore, GEN11_CRYPTO_RSVD_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_CRYPTO_RSVD_INTR_MASK,  ~0);
}

void gen11_gt_irq_postinstall(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 irqs = GT_RENDER_USER_INTERRUPT;
	u32 guc_mask = intel_uc_wants_guc(&gt->uc) ? GUC_INTR_GUC2HOST : 0;
	u32 gsc_mask = 0;
	u32 heci_mask = 0;
	u32 dmask;
	u32 smask;

	if (!intel_uc_wants_guc_submission(&gt->uc))
		irqs |= GT_CS_MASTER_ERROR_INTERRUPT |
			GT_CONTEXT_SWITCH_INTERRUPT |
			GT_WAIT_SEMAPHORE_INTERRUPT;

	dmask = irqs << 16 | irqs;
	smask = irqs << 16;

	if (HAS_ENGINE(gt, GSC0)) {
		/*
		 * the heci2 interrupt is enabled via the same register as the
		 * GSC interrupt, but it has its own mask register.
		 */
		gsc_mask = irqs;
		heci_mask = GSC_IRQ_INTF(1); /* HECI2 IRQ for SW Proxy*/
	} else if (HAS_HECI_GSC(gt->i915)) {
		gsc_mask = GSC_IRQ_INTF(0) | GSC_IRQ_INTF(1);
	}

	BUILD_BUG_ON(irqs & 0xffff0000);

	/* Enable RCS, BCS, VCS and VECS class interrupts. */
	intel_uncore_write(uncore, GEN11_RENDER_COPY_INTR_ENABLE, dmask);
	intel_uncore_write(uncore, GEN11_VCS_VECS_INTR_ENABLE, dmask);
	if (CCS_MASK(gt))
		intel_uncore_write(uncore, GEN12_CCS_RSVD_INTR_ENABLE, smask);
	if (gsc_mask)
		intel_uncore_write(uncore, GEN11_GUNIT_CSME_INTR_ENABLE, gsc_mask | heci_mask);

	/* Unmask irqs on RCS, BCS, VCS and VECS engines. */
	intel_uncore_write(uncore, GEN11_RCS0_RSVD_INTR_MASK, ~smask);
	intel_uncore_write(uncore, GEN11_BCS_RSVD_INTR_MASK, ~smask);
	if (HAS_ENGINE(gt, BCS1) || HAS_ENGINE(gt, BCS2))
		intel_uncore_write(uncore, XEHPC_BCS1_BCS2_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, BCS3) || HAS_ENGINE(gt, BCS4))
		intel_uncore_write(uncore, XEHPC_BCS3_BCS4_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, BCS5) || HAS_ENGINE(gt, BCS6))
		intel_uncore_write(uncore, XEHPC_BCS5_BCS6_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, BCS7) || HAS_ENGINE(gt, BCS8))
		intel_uncore_write(uncore, XEHPC_BCS7_BCS8_INTR_MASK, ~dmask);
	intel_uncore_write(uncore, GEN11_VCS0_VCS1_INTR_MASK, ~dmask);
	intel_uncore_write(uncore, GEN11_VCS2_VCS3_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, VCS4) || HAS_ENGINE(gt, VCS5))
		intel_uncore_write(uncore, GEN12_VCS4_VCS5_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, VCS6) || HAS_ENGINE(gt, VCS7))
		intel_uncore_write(uncore, GEN12_VCS6_VCS7_INTR_MASK, ~dmask);
	intel_uncore_write(uncore, GEN11_VECS0_VECS1_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, VECS2) || HAS_ENGINE(gt, VECS3))
		intel_uncore_write(uncore, GEN12_VECS2_VECS3_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, CCS0) || HAS_ENGINE(gt, CCS1))
		intel_uncore_write(uncore, GEN12_CCS0_CCS1_INTR_MASK, ~dmask);
	if (HAS_ENGINE(gt, CCS2) || HAS_ENGINE(gt, CCS3))
		intel_uncore_write(uncore, GEN12_CCS2_CCS3_INTR_MASK, ~dmask);
	if (gsc_mask)
		intel_uncore_write(uncore, GEN11_GUNIT_CSME_INTR_MASK, ~gsc_mask);
	if (heci_mask)
		intel_uncore_write(uncore, GEN12_HECI2_RSVD_INTR_MASK,
				   ~REG_FIELD_PREP(ENGINE1_MASK, heci_mask));

	if (guc_mask) {
		/* the enable bit is common for both GTs but the masks are separate */
		u32 mask = gt->type == GT_MEDIA ?
			REG_FIELD_PREP(ENGINE0_MASK, guc_mask) :
			REG_FIELD_PREP(ENGINE1_MASK, guc_mask);

		intel_uncore_write(uncore, GEN11_GUC_SG_INTR_ENABLE,
				   REG_FIELD_PREP(ENGINE1_MASK, guc_mask));

		/* we might not be the first GT to write this reg */
		intel_uncore_rmw(uncore, MTL_GUC_MGUC_INTR_MASK, mask, 0);
	}

	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled.
	 */
	gt->pm_ier = 0x0;
	gt->pm_imr = ~gt->pm_ier;
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK,  ~0);
}

void gen5_gt_irq_handler(struct intel_gt *gt, u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine_class[RENDER_CLASS][0],
				    gt_iir);

	if (gt_iir & ILK_BSD_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine_class[VIDEO_DECODE_CLASS][0],
				    gt_iir);
}

static void gen7_parity_error_irq_handler(struct intel_gt *gt, u32 iir)
{
	if (!HAS_L3_DPF(gt->i915))
		return;

	spin_lock(gt->irq_lock);
	gen5_gt_disable_irq(gt, GT_PARITY_ERROR(gt->i915));
	spin_unlock(gt->irq_lock);

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1)
		gt->i915->l3_parity.which_slice |= 1 << 1;

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT)
		gt->i915->l3_parity.which_slice |= 1 << 0;

	queue_work(gt->i915->unordered_wq, &gt->i915->l3_parity.error_work);
}

void gen6_gt_irq_handler(struct intel_gt *gt, u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine_class[RENDER_CLASS][0],
				    gt_iir);

	if (gt_iir & GT_BSD_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine_class[VIDEO_DECODE_CLASS][0],
				    gt_iir >> 12);

	if (gt_iir & GT_BLT_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine_class[COPY_ENGINE_CLASS][0],
				    gt_iir >> 22);

	if (gt_iir & (GT_BLT_CS_ERROR_INTERRUPT |
		      GT_BSD_CS_ERROR_INTERRUPT |
		      GT_CS_MASTER_ERROR_INTERRUPT))
		gt_dbg(gt, "Command parser error, gt_iir 0x%08x\n", gt_iir);

	if (gt_iir & GT_PARITY_ERROR(gt->i915))
		gen7_parity_error_irq_handler(gt, gt_iir);
}

void gen8_gt_irq_handler(struct intel_gt *gt, u32 master_ctl)
{
	void __iomem * const regs = intel_uncore_regs(gt->uncore);
	u32 iir;

	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(0));
		if (likely(iir)) {
			intel_engine_cs_irq(gt->engine_class[RENDER_CLASS][0],
					    iir >> GEN8_RCS_IRQ_SHIFT);
			intel_engine_cs_irq(gt->engine_class[COPY_ENGINE_CLASS][0],
					    iir >> GEN8_BCS_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(0), iir);
		}
	}

	if (master_ctl & (GEN8_GT_VCS0_IRQ | GEN8_GT_VCS1_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(1));
		if (likely(iir)) {
			intel_engine_cs_irq(gt->engine_class[VIDEO_DECODE_CLASS][0],
					    iir >> GEN8_VCS0_IRQ_SHIFT);
			intel_engine_cs_irq(gt->engine_class[VIDEO_DECODE_CLASS][1],
					    iir >> GEN8_VCS1_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(1), iir);
		}
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(3));
		if (likely(iir)) {
			intel_engine_cs_irq(gt->engine_class[VIDEO_ENHANCEMENT_CLASS][0],
					    iir >> GEN8_VECS_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(3), iir);
		}
	}

	if (master_ctl & (GEN8_GT_PM_IRQ | GEN8_GT_GUC_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(2));
		if (likely(iir)) {
			gen6_rps_irq_handler(&gt->rps, iir);
			guc_irq_handler(gt_to_guc(gt), iir >> 16);
			raw_reg_write(regs, GEN8_GT_IIR(2), iir);
		}
	}
}

void gen8_gt_irq_reset(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;

	GEN8_IRQ_RESET_NDX(uncore, GT, 0);
	GEN8_IRQ_RESET_NDX(uncore, GT, 1);
	GEN8_IRQ_RESET_NDX(uncore, GT, 2);
	GEN8_IRQ_RESET_NDX(uncore, GT, 3);
}

void gen8_gt_irq_postinstall(struct intel_gt *gt)
{
	/* These are interrupts we'll toggle with the ring mask register */
	const u32 irqs =
		GT_CS_MASTER_ERROR_INTERRUPT |
		GT_RENDER_USER_INTERRUPT |
		GT_CONTEXT_SWITCH_INTERRUPT |
		GT_WAIT_SEMAPHORE_INTERRUPT;
	const u32 gt_interrupts[] = {
		irqs << GEN8_RCS_IRQ_SHIFT | irqs << GEN8_BCS_IRQ_SHIFT,
		irqs << GEN8_VCS0_IRQ_SHIFT | irqs << GEN8_VCS1_IRQ_SHIFT,
		0,
		irqs << GEN8_VECS_IRQ_SHIFT,
	};
	struct intel_uncore *uncore = gt->uncore;

	gt->pm_ier = 0x0;
	gt->pm_imr = ~gt->pm_ier;
	GEN8_IRQ_INIT_NDX(uncore, GT, 0, ~gt_interrupts[0], gt_interrupts[0]);
	GEN8_IRQ_INIT_NDX(uncore, GT, 1, ~gt_interrupts[1], gt_interrupts[1]);
	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled. Same wil be the case for GuC interrupts.
	 */
	GEN8_IRQ_INIT_NDX(uncore, GT, 2, gt->pm_imr, gt->pm_ier);
	GEN8_IRQ_INIT_NDX(uncore, GT, 3, ~gt_interrupts[3], gt_interrupts[3]);
}

static void gen5_gt_update_irq(struct intel_gt *gt,
			       u32 interrupt_mask,
			       u32 enabled_irq_mask)
{
	lockdep_assert_held(gt->irq_lock);

	GEM_BUG_ON(enabled_irq_mask & ~interrupt_mask);

	gt->gt_imr &= ~interrupt_mask;
	gt->gt_imr |= (~enabled_irq_mask & interrupt_mask);
	intel_uncore_write(gt->uncore, GTIMR, gt->gt_imr);
}

void gen5_gt_enable_irq(struct intel_gt *gt, u32 mask)
{
	gen5_gt_update_irq(gt, mask, mask);
	intel_uncore_posting_read_fw(gt->uncore, GTIMR);
}

void gen5_gt_disable_irq(struct intel_gt *gt, u32 mask)
{
	gen5_gt_update_irq(gt, mask, 0);
}

void gen5_gt_irq_reset(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;

	GEN3_IRQ_RESET(uncore, GT);
	if (GRAPHICS_VER(gt->i915) >= 6)
		GEN3_IRQ_RESET(uncore, GEN6_PM);
}

void gen5_gt_irq_postinstall(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 pm_irqs = 0;
	u32 gt_irqs = 0;

	gt->gt_imr = ~0;
	if (HAS_L3_DPF(gt->i915)) {
		/* L3 parity interrupt is always unmasked. */
		gt->gt_imr = ~GT_PARITY_ERROR(gt->i915);
		gt_irqs |= GT_PARITY_ERROR(gt->i915);
	}

	gt_irqs |= GT_RENDER_USER_INTERRUPT;
	if (GRAPHICS_VER(gt->i915) == 5)
		gt_irqs |= ILK_BSD_USER_INTERRUPT;
	else
		gt_irqs |= GT_BLT_USER_INTERRUPT | GT_BSD_USER_INTERRUPT;

	GEN3_IRQ_INIT(uncore, GT, gt->gt_imr, gt_irqs);

	if (GRAPHICS_VER(gt->i915) >= 6) {
		/*
		 * RPS interrupts will get enabled/disabled on demand when RPS
		 * itself is enabled/disabled.
		 */
		if (HAS_ENGINE(gt, VECS0)) {
			pm_irqs |= PM_VEBOX_USER_INTERRUPT;
			gt->pm_ier |= PM_VEBOX_USER_INTERRUPT;
		}

		gt->pm_imr = 0xffffffff;
		GEN3_IRQ_INIT(uncore, GEN6_PM, gt->pm_imr, pm_irqs);
	}
}
