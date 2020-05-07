/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/sched/clock.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_uncore.h"
#include "intel_rps.h"

static void guc_irq_handler(struct intel_guc *guc, u16 iir)
{
	if (iir & GUC_INTR_GUC2HOST)
		intel_guc_to_host_event_handler(guc);
}

static void
cs_irq_handler(struct intel_engine_cs *engine, u32 iir)
{
	bool tasklet = false;

	if (unlikely(iir & GT_CS_MASTER_ERROR_INTERRUPT)) {
		u32 eir;

		eir = ENGINE_READ(engine, RING_EIR);
		ENGINE_TRACE(engine, "CS error: %x\n", eir);

		/* Disable the error interrupt until after the reset */
		if (likely(eir)) {
			ENGINE_WRITE(engine, RING_EMR, ~0u);
			ENGINE_WRITE(engine, RING_EIR, eir);
			WRITE_ONCE(engine->execlists.error_interrupt, eir);
			tasklet = true;
		}
	}

	if (iir & GT_CONTEXT_SWITCH_INTERRUPT)
		tasklet = true;

	if (iir & GT_RENDER_USER_INTERRUPT) {
		intel_engine_signal_breadcrumbs(engine);
		tasklet |= intel_engine_needs_breadcrumb_tasklet(engine);
	}

	if (tasklet)
		tasklet_hi_schedule(&engine->execlists.tasklet);
}

static u32
gen11_gt_engine_identity(struct intel_gt *gt,
			 const unsigned int bank, const unsigned int bit)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 timeout_ts;
	u32 ident;

	lockdep_assert_held(&gt->irq_lock);

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
		DRM_ERROR("INTR_IDENTITY_REG%u:%u 0x%08x not valid!\n",
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
	if (instance == OTHER_GUC_INSTANCE)
		return guc_irq_handler(&gt->uc.guc, iir);

	if (instance == OTHER_GTPM_INSTANCE)
		return gen11_rps_irq_handler(&gt->rps, iir);

	WARN_ONCE(1, "unhandled other interrupt instance=0x%x, iir=0x%x\n",
		  instance, iir);
}

static void
gen11_engine_irq_handler(struct intel_gt *gt, const u8 class,
			 const u8 instance, const u16 iir)
{
	struct intel_engine_cs *engine;

	if (instance <= MAX_ENGINE_INSTANCE)
		engine = gt->engine_class[class][instance];
	else
		engine = NULL;

	if (likely(engine))
		return cs_irq_handler(engine, iir);

	WARN_ONCE(1, "unhandled engine interrupt class=0x%x, instance=0x%x\n",
		  class, instance);
}

static void
gen11_gt_identity_handler(struct intel_gt *gt, const u32 identity)
{
	const u8 class = GEN11_INTR_ENGINE_CLASS(identity);
	const u8 instance = GEN11_INTR_ENGINE_INSTANCE(identity);
	const u16 intr = GEN11_INTR_ENGINE_INTR(identity);

	if (unlikely(!intr))
		return;

	if (class <= COPY_ENGINE_CLASS)
		return gen11_engine_irq_handler(gt, class, instance, intr);

	if (class == OTHER_CLASS)
		return gen11_other_irq_handler(gt, instance, intr);

	WARN_ONCE(1, "unknown interrupt class=0x%x, instance=0x%x, intr=0x%x\n",
		  class, instance, intr);
}

static void
gen11_gt_bank_handler(struct intel_gt *gt, const unsigned int bank)
{
	void __iomem * const regs = gt->uncore->regs;
	unsigned long intr_dw;
	unsigned int bit;

	lockdep_assert_held(&gt->irq_lock);

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

	spin_lock(&gt->irq_lock);

	for (bank = 0; bank < 2; bank++) {
		if (master_ctl & GEN11_GT_DW_IRQ(bank))
			gen11_gt_bank_handler(gt, bank);
	}

	spin_unlock(&gt->irq_lock);
}

bool gen11_gt_reset_one_iir(struct intel_gt *gt,
			    const unsigned int bank, const unsigned int bit)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 dw;

	lockdep_assert_held(&gt->irq_lock);

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

	/* Restore masks irqs on RCS, BCS, VCS and VECS engines. */
	intel_uncore_write(uncore, GEN11_RCS0_RSVD_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_BCS_RSVD_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_VCS0_VCS1_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_VCS2_VCS3_INTR_MASK,	~0);
	intel_uncore_write(uncore, GEN11_VECS0_VECS1_INTR_MASK,	~0);

	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK,  ~0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_MASK,  ~0);
}

void gen11_gt_irq_postinstall(struct intel_gt *gt)
{
	const u32 irqs =
		GT_CS_MASTER_ERROR_INTERRUPT |
		GT_RENDER_USER_INTERRUPT |
		GT_CONTEXT_SWITCH_INTERRUPT;
	struct intel_uncore *uncore = gt->uncore;
	const u32 dmask = irqs << 16 | irqs;
	const u32 smask = irqs << 16;

	BUILD_BUG_ON(irqs & 0xffff0000);

	/* Enable RCS, BCS, VCS and VECS class interrupts. */
	intel_uncore_write(uncore, GEN11_RENDER_COPY_INTR_ENABLE, dmask);
	intel_uncore_write(uncore, GEN11_VCS_VECS_INTR_ENABLE, dmask);

	/* Unmask irqs on RCS, BCS, VCS and VECS engines. */
	intel_uncore_write(uncore, GEN11_RCS0_RSVD_INTR_MASK, ~smask);
	intel_uncore_write(uncore, GEN11_BCS_RSVD_INTR_MASK, ~smask);
	intel_uncore_write(uncore, GEN11_VCS0_VCS1_INTR_MASK, ~dmask);
	intel_uncore_write(uncore, GEN11_VCS2_VCS3_INTR_MASK, ~dmask);
	intel_uncore_write(uncore, GEN11_VECS0_VECS1_INTR_MASK, ~dmask);

	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled.
	 */
	gt->pm_ier = 0x0;
	gt->pm_imr = ~gt->pm_ier;
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK,  ~0);

	/* Same thing for GuC interrupts */
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_MASK,  ~0);
}

void gen5_gt_irq_handler(struct intel_gt *gt, u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine_class[RENDER_CLASS][0]);
	if (gt_iir & ILK_BSD_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine_class[VIDEO_DECODE_CLASS][0]);
}

static void gen7_parity_error_irq_handler(struct intel_gt *gt, u32 iir)
{
	if (!HAS_L3_DPF(gt->i915))
		return;

	spin_lock(&gt->irq_lock);
	gen5_gt_disable_irq(gt, GT_PARITY_ERROR(gt->i915));
	spin_unlock(&gt->irq_lock);

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1)
		gt->i915->l3_parity.which_slice |= 1 << 1;

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT)
		gt->i915->l3_parity.which_slice |= 1 << 0;

	schedule_work(&gt->i915->l3_parity.error_work);
}

void gen6_gt_irq_handler(struct intel_gt *gt, u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine_class[RENDER_CLASS][0]);
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine_class[VIDEO_DECODE_CLASS][0]);
	if (gt_iir & GT_BLT_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine_class[COPY_ENGINE_CLASS][0]);

	if (gt_iir & (GT_BLT_CS_ERROR_INTERRUPT |
		      GT_BSD_CS_ERROR_INTERRUPT |
		      GT_CS_MASTER_ERROR_INTERRUPT))
		DRM_DEBUG("Command parser error, gt_iir 0x%08x\n", gt_iir);

	if (gt_iir & GT_PARITY_ERROR(gt->i915))
		gen7_parity_error_irq_handler(gt, gt_iir);
}

void gen8_gt_irq_handler(struct intel_gt *gt, u32 master_ctl)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 iir;

	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(0));
		if (likely(iir)) {
			cs_irq_handler(gt->engine_class[RENDER_CLASS][0],
				       iir >> GEN8_RCS_IRQ_SHIFT);
			cs_irq_handler(gt->engine_class[COPY_ENGINE_CLASS][0],
				       iir >> GEN8_BCS_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(0), iir);
		}
	}

	if (master_ctl & (GEN8_GT_VCS0_IRQ | GEN8_GT_VCS1_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(1));
		if (likely(iir)) {
			cs_irq_handler(gt->engine_class[VIDEO_DECODE_CLASS][0],
				       iir >> GEN8_VCS0_IRQ_SHIFT);
			cs_irq_handler(gt->engine_class[VIDEO_DECODE_CLASS][1],
				       iir >> GEN8_VCS1_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(1), iir);
		}
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(3));
		if (likely(iir)) {
			cs_irq_handler(gt->engine_class[VIDEO_ENHANCEMENT_CLASS][0],
				       iir >> GEN8_VECS_IRQ_SHIFT);
			raw_reg_write(regs, GEN8_GT_IIR(3), iir);
		}
	}

	if (master_ctl & (GEN8_GT_PM_IRQ | GEN8_GT_GUC_IRQ)) {
		iir = raw_reg_read(regs, GEN8_GT_IIR(2));
		if (likely(iir)) {
			gen6_rps_irq_handler(&gt->rps, iir);
			guc_irq_handler(&gt->uc.guc, iir >> 16);
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
		GT_CONTEXT_SWITCH_INTERRUPT;
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
	lockdep_assert_held(&gt->irq_lock);

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
	if (INTEL_GEN(gt->i915) >= 6)
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
	if (IS_GEN(gt->i915, 5))
		gt_irqs |= ILK_BSD_USER_INTERRUPT;
	else
		gt_irqs |= GT_BLT_USER_INTERRUPT | GT_BSD_USER_INTERRUPT;

	GEN3_IRQ_INIT(uncore, GT, gt->gt_imr, gt_irqs);

	if (INTEL_GEN(gt->i915) >= 6) {
		/*
		 * RPS interrupts will get enabled/disabled on demand when RPS
		 * itself is enabled/disabled.
		 */
		if (HAS_ENGINE(gt->i915, VECS0)) {
			pm_irqs |= PM_VEBOX_USER_INTERRUPT;
			gt->pm_ier |= PM_VEBOX_USER_INTERRUPT;
		}

		gt->pm_imr = 0xffffffff;
		GEN3_IRQ_INIT(uncore, GEN6_PM, gt->pm_imr, pm_irqs);
	}
}
