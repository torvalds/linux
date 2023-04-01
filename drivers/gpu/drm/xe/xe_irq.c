// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_irq.h"

#include <linux/sched/clock.h>

#include <drm/drm_managed.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_device.h"
#include "xe_drv.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_hw_engine.h"
#include "xe_mmio.h"

static void assert_iir_is_zero(struct xe_gt *gt, i915_reg_t reg)
{
	u32 val = xe_mmio_read32(gt, reg.reg);

	if (val == 0)
		return;

	drm_WARN(&gt_to_xe(gt)->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 reg.reg, val);
	xe_mmio_write32(gt, reg.reg, 0xffffffff);
	xe_mmio_read32(gt, reg.reg);
	xe_mmio_write32(gt, reg.reg, 0xffffffff);
	xe_mmio_read32(gt, reg.reg);
}

static void irq_init(struct xe_gt *gt,
		     i915_reg_t imr, u32 imr_val,
		     i915_reg_t ier, u32 ier_val,
		     i915_reg_t iir)
{
	assert_iir_is_zero(gt, iir);

	xe_mmio_write32(gt, ier.reg, ier_val);
	xe_mmio_write32(gt, imr.reg, imr_val);
	xe_mmio_read32(gt, imr.reg);
}
#define IRQ_INIT(gt, type, imr_val, ier_val) \
	irq_init((gt), \
		 type##IMR, imr_val, \
		 type##IER, ier_val, \
		 type##IIR)

static void irq_reset(struct xe_gt *gt, i915_reg_t imr, i915_reg_t iir,
			   i915_reg_t ier)
{
	xe_mmio_write32(gt, imr.reg, 0xffffffff);
	xe_mmio_read32(gt, imr.reg);

	xe_mmio_write32(gt, ier.reg, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	xe_mmio_write32(gt, iir.reg, 0xffffffff);
	xe_mmio_read32(gt, iir.reg);
	xe_mmio_write32(gt, iir.reg, 0xffffffff);
	xe_mmio_read32(gt, iir.reg);
}
#define IRQ_RESET(gt, type) \
	irq_reset((gt), type##IMR, type##IIR, type##IER)

static u32 gen11_intr_disable(struct xe_gt *gt)
{
	xe_mmio_write32(gt, GEN11_GFX_MSTR_IRQ.reg, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return xe_mmio_read32(gt, GEN11_GFX_MSTR_IRQ.reg);
}

static u32
gen11_gu_misc_irq_ack(struct xe_gt *gt, const u32 master_ctl)
{
	u32 iir;

	if (!(master_ctl & GEN11_GU_MISC_IRQ))
		return 0;

	iir = xe_mmio_read32(gt, GEN11_GU_MISC_IIR.reg);
	if (likely(iir))
		xe_mmio_write32(gt, GEN11_GU_MISC_IIR.reg, iir);

	return iir;
}

static inline void gen11_intr_enable(struct xe_gt *gt, bool stall)
{
	xe_mmio_write32(gt, GEN11_GFX_MSTR_IRQ.reg, GEN11_MASTER_IRQ);
	if (stall)
		xe_mmio_read32(gt, GEN11_GFX_MSTR_IRQ.reg);
}

static void gen11_gt_irq_postinstall(struct xe_device *xe, struct xe_gt *gt)
{
	u32 irqs, dmask, smask;
	u32 ccs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COMPUTE);
	u32 bcs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COPY);

	if (xe_device_guc_submission_enabled(xe)) {
		irqs = GT_RENDER_USER_INTERRUPT |
			GT_RENDER_PIPECTL_NOTIFY_INTERRUPT;
	} else {
		irqs = GT_RENDER_USER_INTERRUPT |
		       GT_CS_MASTER_ERROR_INTERRUPT |
		       GT_CONTEXT_SWITCH_INTERRUPT |
		       GT_WAIT_SEMAPHORE_INTERRUPT;
	}

	dmask = irqs << 16 | irqs;
	smask = irqs << 16;

	/* Enable RCS, BCS, VCS and VECS class interrupts. */
	xe_mmio_write32(gt, GEN11_RENDER_COPY_INTR_ENABLE.reg, dmask);
	xe_mmio_write32(gt, GEN11_VCS_VECS_INTR_ENABLE.reg, dmask);
	if (ccs_mask)
		xe_mmio_write32(gt, GEN12_CCS_RSVD_INTR_ENABLE.reg, smask);

	/* Unmask irqs on RCS, BCS, VCS and VECS engines. */
	xe_mmio_write32(gt, GEN11_RCS0_RSVD_INTR_MASK.reg, ~smask);
	xe_mmio_write32(gt, GEN11_BCS_RSVD_INTR_MASK.reg, ~smask);
	if (bcs_mask & (BIT(1)|BIT(2)))
		xe_mmio_write32(gt, XEHPC_BCS1_BCS2_INTR_MASK.reg, ~dmask);
	if (bcs_mask & (BIT(3)|BIT(4)))
		xe_mmio_write32(gt, XEHPC_BCS3_BCS4_INTR_MASK.reg, ~dmask);
	if (bcs_mask & (BIT(5)|BIT(6)))
		xe_mmio_write32(gt, XEHPC_BCS5_BCS6_INTR_MASK.reg, ~dmask);
	if (bcs_mask & (BIT(7)|BIT(8)))
		xe_mmio_write32(gt, XEHPC_BCS7_BCS8_INTR_MASK.reg, ~dmask);
	xe_mmio_write32(gt, GEN11_VCS0_VCS1_INTR_MASK.reg, ~dmask);
	xe_mmio_write32(gt, GEN11_VCS2_VCS3_INTR_MASK.reg, ~dmask);
	//if (HAS_ENGINE(gt, VCS4) || HAS_ENGINE(gt, VCS5))
	//	intel_uncore_write(uncore, GEN12_VCS4_VCS5_INTR_MASK, ~dmask);
	//if (HAS_ENGINE(gt, VCS6) || HAS_ENGINE(gt, VCS7))
	//	intel_uncore_write(uncore, GEN12_VCS6_VCS7_INTR_MASK, ~dmask);
	xe_mmio_write32(gt, GEN11_VECS0_VECS1_INTR_MASK.reg, ~dmask);
	//if (HAS_ENGINE(gt, VECS2) || HAS_ENGINE(gt, VECS3))
	//	intel_uncore_write(uncore, GEN12_VECS2_VECS3_INTR_MASK, ~dmask);
	if (ccs_mask & (BIT(0)|BIT(1)))
		xe_mmio_write32(gt, GEN12_CCS0_CCS1_INTR_MASK.reg, ~dmask);
	if (ccs_mask & (BIT(2)|BIT(3)))
		xe_mmio_write32(gt,  GEN12_CCS2_CCS3_INTR_MASK.reg, ~dmask);

	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled.
	 */
	/* TODO: gt->pm_ier, gt->pm_imr */
	xe_mmio_write32(gt, GEN11_GPM_WGBOXPERF_INTR_ENABLE.reg, 0);
	xe_mmio_write32(gt, GEN11_GPM_WGBOXPERF_INTR_MASK.reg,  ~0);

	/* Same thing for GuC interrupts */
	xe_mmio_write32(gt, GEN11_GUC_SG_INTR_ENABLE.reg, 0);
	xe_mmio_write32(gt, GEN11_GUC_SG_INTR_MASK.reg,  ~0);
}

static void gen11_irq_postinstall(struct xe_device *xe, struct xe_gt *gt)
{
	/* TODO: PCH */

	gen11_gt_irq_postinstall(xe, gt);

	IRQ_INIT(gt, GEN11_GU_MISC_, ~GEN11_GU_MISC_GSE, GEN11_GU_MISC_GSE);

	gen11_intr_enable(gt, true);
}

static u32
gen11_gt_engine_identity(struct xe_device *xe,
			 struct xe_gt *gt,
			 const unsigned int bank,
			 const unsigned int bit)
{
	u32 timeout_ts;
	u32 ident;

	lockdep_assert_held(&xe->irq.lock);

	xe_mmio_write32(gt, GEN11_IIR_REG_SELECTOR(bank).reg, BIT(bit));

	/*
	 * NB: Specs do not specify how long to spin wait,
	 * so we do ~100us as an educated guess.
	 */
	timeout_ts = (local_clock() >> 10) + 100;
	do {
		ident = xe_mmio_read32(gt, GEN11_INTR_IDENTITY_REG(bank).reg);
	} while (!(ident & GEN11_INTR_DATA_VALID) &&
		 !time_after32(local_clock() >> 10, timeout_ts));

	if (unlikely(!(ident & GEN11_INTR_DATA_VALID))) {
		drm_err(&xe->drm, "INTR_IDENTITY_REG%u:%u 0x%08x not valid!\n",
			bank, bit, ident);
		return 0;
	}

	xe_mmio_write32(gt, GEN11_INTR_IDENTITY_REG(bank).reg,
			GEN11_INTR_DATA_VALID);

	return ident;
}

#define   OTHER_MEDIA_GUC_INSTANCE           16

static void
gen11_gt_other_irq_handler(struct xe_gt *gt, const u8 instance, const u16 iir)
{
	if (instance == OTHER_GUC_INSTANCE && !xe_gt_is_media_type(gt))
		return xe_guc_irq_handler(&gt->uc.guc, iir);
	if (instance == OTHER_MEDIA_GUC_INSTANCE && xe_gt_is_media_type(gt))
		return xe_guc_irq_handler(&gt->uc.guc, iir);

	if (instance != OTHER_GUC_INSTANCE &&
	    instance != OTHER_MEDIA_GUC_INSTANCE) {
		WARN_ONCE(1, "unhandled other interrupt instance=0x%x, iir=0x%x\n",
			  instance, iir);
	}
}

static void gen11_gt_irq_handler(struct xe_device *xe, struct xe_gt *gt,
				 u32 master_ctl, long unsigned int *intr_dw,
				 u32 *identity)
{
	unsigned int bank, bit;
	u16 instance, intr_vec;
	enum xe_engine_class class;
	struct xe_hw_engine *hwe;

	spin_lock(&xe->irq.lock);

	for (bank = 0; bank < 2; bank++) {
		if (!(master_ctl & GEN11_GT_DW_IRQ(bank)))
			continue;

		if (!xe_gt_is_media_type(gt)) {
			intr_dw[bank] =
				xe_mmio_read32(gt, GEN11_GT_INTR_DW(bank).reg);
			for_each_set_bit(bit, intr_dw + bank, 32)
				identity[bit] = gen11_gt_engine_identity(xe, gt,
									 bank,
									 bit);
			xe_mmio_write32(gt, GEN11_GT_INTR_DW(bank).reg,
					intr_dw[bank]);
		}

		for_each_set_bit(bit, intr_dw + bank, 32) {
			class = GEN11_INTR_ENGINE_CLASS(identity[bit]);
			instance = GEN11_INTR_ENGINE_INSTANCE(identity[bit]);
			intr_vec = GEN11_INTR_ENGINE_INTR(identity[bit]);

			if (class == XE_ENGINE_CLASS_OTHER) {
				gen11_gt_other_irq_handler(gt, instance,
							   intr_vec);
				continue;
			}

			hwe = xe_gt_hw_engine(gt, class, instance, false);
			if (!hwe)
				continue;

			xe_hw_engine_handle_irq(hwe, intr_vec);
		}
	}

	spin_unlock(&xe->irq.lock);
}

static irqreturn_t gen11_irq_handler(int irq, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_gt *gt = xe_device_get_gt(xe, 0);	/* Only 1 GT here */
	u32 master_ctl, gu_misc_iir;
	long unsigned int intr_dw[2];
	u32 identity[32];

	master_ctl = gen11_intr_disable(gt);
	if (!master_ctl) {
		gen11_intr_enable(gt, false);
		return IRQ_NONE;
	}

	gen11_gt_irq_handler(xe, gt, master_ctl, intr_dw, identity);

	gu_misc_iir = gen11_gu_misc_irq_ack(gt, master_ctl);

	gen11_intr_enable(gt, false);

	return IRQ_HANDLED;
}

static u32 dg1_intr_disable(struct xe_device *xe)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);
	u32 val;

	/* First disable interrupts */
	xe_mmio_write32(gt, DG1_MSTR_TILE_INTR.reg, 0);

	/* Get the indication levels and ack the master unit */
	val = xe_mmio_read32(gt, DG1_MSTR_TILE_INTR.reg);
	if (unlikely(!val))
		return 0;

	xe_mmio_write32(gt, DG1_MSTR_TILE_INTR.reg, val);

	return val;
}

static void dg1_intr_enable(struct xe_device *xe, bool stall)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);

	xe_mmio_write32(gt, DG1_MSTR_TILE_INTR.reg, DG1_MSTR_IRQ);
	if (stall)
		xe_mmio_read32(gt, DG1_MSTR_TILE_INTR.reg);
}

static void dg1_irq_postinstall(struct xe_device *xe, struct xe_gt *gt)
{
	gen11_gt_irq_postinstall(xe, gt);

	IRQ_INIT(gt, GEN11_GU_MISC_, ~GEN11_GU_MISC_GSE, GEN11_GU_MISC_GSE);

	if (gt->info.id == XE_GT0)
		dg1_intr_enable(xe, true);
}

static irqreturn_t dg1_irq_handler(int irq, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_gt *gt;
	u32 master_tile_ctl, master_ctl = 0, gu_misc_iir;
	long unsigned int intr_dw[2];
	u32 identity[32];
	u8 id;

	/* TODO: This really shouldn't be copied+pasted */

	master_tile_ctl = dg1_intr_disable(xe);
	if (!master_tile_ctl) {
		dg1_intr_enable(xe, false);
		return IRQ_NONE;
	}

	for_each_gt(gt, xe, id) {
		if ((master_tile_ctl & DG1_MSTR_TILE(gt->info.vram_id)) == 0)
			continue;

		if (!xe_gt_is_media_type(gt))
			master_ctl = xe_mmio_read32(gt, GEN11_GFX_MSTR_IRQ.reg);

		/*
		 * We might be in irq handler just when PCIe DPC is initiated
		 * and all MMIO reads will be returned with all 1's. Ignore this
		 * irq as device is inaccessible.
		 */
		if (master_ctl == REG_GENMASK(31, 0)) {
			dev_dbg(gt_to_xe(gt)->drm.dev,
				"Ignore this IRQ as device might be in DPC containment.\n");
			return IRQ_HANDLED;
		}

		if (!xe_gt_is_media_type(gt))
			xe_mmio_write32(gt, GEN11_GFX_MSTR_IRQ.reg, master_ctl);
		gen11_gt_irq_handler(xe, gt, master_ctl, intr_dw, identity);
	}

	gu_misc_iir = gen11_gu_misc_irq_ack(gt, master_ctl);

	dg1_intr_enable(xe, false);

	return IRQ_HANDLED;
}

static void gen11_gt_irq_reset(struct xe_gt *gt)
{
	u32 ccs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COMPUTE);
	u32 bcs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COPY);

	/* Disable RCS, BCS, VCS and VECS class engines. */
	xe_mmio_write32(gt, GEN11_RENDER_COPY_INTR_ENABLE.reg,	 0);
	xe_mmio_write32(gt, GEN11_VCS_VECS_INTR_ENABLE.reg,	 0);
	if (ccs_mask)
		xe_mmio_write32(gt, GEN12_CCS_RSVD_INTR_ENABLE.reg, 0);

	/* Restore masks irqs on RCS, BCS, VCS and VECS engines. */
	xe_mmio_write32(gt, GEN11_RCS0_RSVD_INTR_MASK.reg,	~0);
	xe_mmio_write32(gt, GEN11_BCS_RSVD_INTR_MASK.reg,	~0);
	if (bcs_mask & (BIT(1)|BIT(2)))
		xe_mmio_write32(gt, XEHPC_BCS1_BCS2_INTR_MASK.reg, ~0);
	if (bcs_mask & (BIT(3)|BIT(4)))
		xe_mmio_write32(gt, XEHPC_BCS3_BCS4_INTR_MASK.reg, ~0);
	if (bcs_mask & (BIT(5)|BIT(6)))
		xe_mmio_write32(gt, XEHPC_BCS5_BCS6_INTR_MASK.reg, ~0);
	if (bcs_mask & (BIT(7)|BIT(8)))
		xe_mmio_write32(gt, XEHPC_BCS7_BCS8_INTR_MASK.reg, ~0);
	xe_mmio_write32(gt, GEN11_VCS0_VCS1_INTR_MASK.reg,	~0);
	xe_mmio_write32(gt, GEN11_VCS2_VCS3_INTR_MASK.reg,	~0);
//	if (HAS_ENGINE(gt, VCS4) || HAS_ENGINE(gt, VCS5))
//		xe_mmio_write32(xe, GEN12_VCS4_VCS5_INTR_MASK.reg,   ~0);
//	if (HAS_ENGINE(gt, VCS6) || HAS_ENGINE(gt, VCS7))
//		xe_mmio_write32(xe, GEN12_VCS6_VCS7_INTR_MASK.reg,   ~0);
	xe_mmio_write32(gt, GEN11_VECS0_VECS1_INTR_MASK.reg,	~0);
//	if (HAS_ENGINE(gt, VECS2) || HAS_ENGINE(gt, VECS3))
//		xe_mmio_write32(xe, GEN12_VECS2_VECS3_INTR_MASK.reg, ~0);
	if (ccs_mask & (BIT(0)|BIT(1)))
		xe_mmio_write32(gt, GEN12_CCS0_CCS1_INTR_MASK.reg, ~0);
	if (ccs_mask & (BIT(2)|BIT(3)))
		xe_mmio_write32(gt,  GEN12_CCS2_CCS3_INTR_MASK.reg, ~0);

	xe_mmio_write32(gt, GEN11_GPM_WGBOXPERF_INTR_ENABLE.reg, 0);
	xe_mmio_write32(gt, GEN11_GPM_WGBOXPERF_INTR_MASK.reg,  ~0);
	xe_mmio_write32(gt, GEN11_GUC_SG_INTR_ENABLE.reg,	 0);
	xe_mmio_write32(gt, GEN11_GUC_SG_INTR_MASK.reg,		~0);
}

static void gen11_irq_reset(struct xe_gt *gt)
{
	gen11_intr_disable(gt);

	gen11_gt_irq_reset(gt);

	IRQ_RESET(gt, GEN11_GU_MISC_);
	IRQ_RESET(gt, GEN8_PCU_);
}

static void dg1_irq_reset(struct xe_gt *gt)
{
	if (gt->info.id == 0)
		dg1_intr_disable(gt_to_xe(gt));

	gen11_gt_irq_reset(gt);

	IRQ_RESET(gt, GEN11_GU_MISC_);
	IRQ_RESET(gt, GEN8_PCU_);
}

static void xe_irq_reset(struct xe_device *xe)
{
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id) {
		if (GRAPHICS_VERx100(xe) >= 1210) {
			dg1_irq_reset(gt);
		} else if (GRAPHICS_VER(xe) >= 11) {
			gen11_irq_reset(gt);
		} else {
			drm_err(&xe->drm, "No interrupt reset hook");
		}
	}
}

void xe_gt_irq_postinstall(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (GRAPHICS_VERx100(xe) >= 1210)
		dg1_irq_postinstall(xe, gt);
	else if (GRAPHICS_VER(xe) >= 11)
		gen11_irq_postinstall(xe, gt);
	else
		drm_err(&xe->drm, "No interrupt postinstall hook");
}

static void xe_irq_postinstall(struct xe_device *xe)
{
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id)
		xe_gt_irq_postinstall(gt);
}

static irq_handler_t xe_irq_handler(struct xe_device *xe)
{
	if (GRAPHICS_VERx100(xe) >= 1210) {
		return dg1_irq_handler;
	} else if (GRAPHICS_VER(xe) >= 11) {
		return gen11_irq_handler;
	} else {
		return NULL;
	}
}

static void irq_uninstall(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int irq = pdev->irq;

	if (!xe->irq.enabled)
		return;

	xe->irq.enabled = false;
	xe_irq_reset(xe);
	free_irq(irq, xe);
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
}

int xe_irq_install(struct xe_device *xe)
{
	int irq = to_pci_dev(xe->drm.dev)->irq;
	irq_handler_t irq_handler;
	int err;

	irq_handler = xe_irq_handler(xe);
	if (!irq_handler) {
		drm_err(&xe->drm, "No supported interrupt handler");
		return -EINVAL;
	}

	xe->irq.enabled = true;

	xe_irq_reset(xe);

	err = request_irq(irq, irq_handler,
			  IRQF_SHARED, DRIVER_NAME, xe);
	if (err < 0) {
		xe->irq.enabled = false;
		return err;
	}

	err = drmm_add_action_or_reset(&xe->drm, irq_uninstall, xe);
	if (err)
		return err;

	return err;
}

void xe_irq_shutdown(struct xe_device *xe)
{
	irq_uninstall(&xe->drm, xe);
}

void xe_irq_suspend(struct xe_device *xe)
{
	spin_lock_irq(&xe->irq.lock);
	xe->irq.enabled = false;
	xe_irq_reset(xe);
	spin_unlock_irq(&xe->irq.lock);
}

void xe_irq_resume(struct xe_device *xe)
{
	spin_lock_irq(&xe->irq.lock);
	xe->irq.enabled = true;
	xe_irq_reset(xe);
	xe_irq_postinstall(xe);
	spin_unlock_irq(&xe->irq.lock);
}
