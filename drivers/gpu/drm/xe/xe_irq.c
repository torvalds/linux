// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_irq.h"

#include <linux/sched/clock.h>

#include <drm/drm_managed.h>

#include "display/xe_display.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_device.h"
#include "xe_drv.h"
#include "xe_gsc_proxy.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_hw_engine.h"
#include "xe_memirq.h"
#include "xe_mmio.h"
#include "xe_sriov.h"

/*
 * Interrupt registers for a unit are always consecutive and ordered
 * ISR, IMR, IIR, IER.
 */
#define IMR(offset)				XE_REG(offset + 0x4)
#define IIR(offset)				XE_REG(offset + 0x8)
#define IER(offset)				XE_REG(offset + 0xc)

static void assert_iir_is_zero(struct xe_gt *mmio, struct xe_reg reg)
{
	u32 val = xe_mmio_read32(mmio, reg);

	if (val == 0)
		return;

	drm_WARN(&gt_to_xe(mmio)->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 reg.addr, val);
	xe_mmio_write32(mmio, reg, 0xffffffff);
	xe_mmio_read32(mmio, reg);
	xe_mmio_write32(mmio, reg, 0xffffffff);
	xe_mmio_read32(mmio, reg);
}

/*
 * Unmask and enable the specified interrupts.  Does not check current state,
 * so any bits not specified here will become masked and disabled.
 */
static void unmask_and_enable(struct xe_tile *tile, u32 irqregs, u32 bits)
{
	struct xe_gt *mmio = tile->primary_gt;

	/*
	 * If we're just enabling an interrupt now, it shouldn't already
	 * be raised in the IIR.
	 */
	assert_iir_is_zero(mmio, IIR(irqregs));

	xe_mmio_write32(mmio, IER(irqregs), bits);
	xe_mmio_write32(mmio, IMR(irqregs), ~bits);

	/* Posting read */
	xe_mmio_read32(mmio, IMR(irqregs));
}

/* Mask and disable all interrupts. */
static void mask_and_disable(struct xe_tile *tile, u32 irqregs)
{
	struct xe_gt *mmio = tile->primary_gt;

	xe_mmio_write32(mmio, IMR(irqregs), ~0);
	/* Posting read */
	xe_mmio_read32(mmio, IMR(irqregs));

	xe_mmio_write32(mmio, IER(irqregs), 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	xe_mmio_write32(mmio, IIR(irqregs), ~0);
	xe_mmio_read32(mmio, IIR(irqregs));
	xe_mmio_write32(mmio, IIR(irqregs), ~0);
	xe_mmio_read32(mmio, IIR(irqregs));
}

static u32 xelp_intr_disable(struct xe_device *xe)
{
	struct xe_gt *mmio = xe_root_mmio_gt(xe);

	xe_mmio_write32(mmio, GFX_MSTR_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return xe_mmio_read32(mmio, GFX_MSTR_IRQ);
}

static u32
gu_misc_irq_ack(struct xe_device *xe, const u32 master_ctl)
{
	struct xe_gt *mmio = xe_root_mmio_gt(xe);
	u32 iir;

	if (!(master_ctl & GU_MISC_IRQ))
		return 0;

	iir = xe_mmio_read32(mmio, IIR(GU_MISC_IRQ_OFFSET));
	if (likely(iir))
		xe_mmio_write32(mmio, IIR(GU_MISC_IRQ_OFFSET), iir);

	return iir;
}

static inline void xelp_intr_enable(struct xe_device *xe, bool stall)
{
	struct xe_gt *mmio = xe_root_mmio_gt(xe);

	xe_mmio_write32(mmio, GFX_MSTR_IRQ, MASTER_IRQ);
	if (stall)
		xe_mmio_read32(mmio, GFX_MSTR_IRQ);
}

/* Enable/unmask the HWE interrupts for a specific GT's engines. */
void xe_irq_enable_hwe(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 ccs_mask, bcs_mask;
	u32 irqs, dmask, smask;
	u32 gsc_mask = 0;
	u32 heci_mask = 0;

	if (xe_device_uc_enabled(xe)) {
		irqs = GT_RENDER_USER_INTERRUPT |
			GT_RENDER_PIPECTL_NOTIFY_INTERRUPT;
	} else {
		irqs = GT_RENDER_USER_INTERRUPT |
		       GT_CS_MASTER_ERROR_INTERRUPT |
		       GT_CONTEXT_SWITCH_INTERRUPT |
		       GT_WAIT_SEMAPHORE_INTERRUPT;
	}

	ccs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COMPUTE);
	bcs_mask = xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_COPY);

	dmask = irqs << 16 | irqs;
	smask = irqs << 16;

	if (!xe_gt_is_media_type(gt)) {
		/* Enable interrupts for each engine class */
		xe_mmio_write32(gt, RENDER_COPY_INTR_ENABLE, dmask);
		if (ccs_mask)
			xe_mmio_write32(gt, CCS_RSVD_INTR_ENABLE, smask);

		/* Unmask interrupts for each engine instance */
		xe_mmio_write32(gt, RCS0_RSVD_INTR_MASK, ~smask);
		xe_mmio_write32(gt, BCS_RSVD_INTR_MASK, ~smask);
		if (bcs_mask & (BIT(1)|BIT(2)))
			xe_mmio_write32(gt, XEHPC_BCS1_BCS2_INTR_MASK, ~dmask);
		if (bcs_mask & (BIT(3)|BIT(4)))
			xe_mmio_write32(gt, XEHPC_BCS3_BCS4_INTR_MASK, ~dmask);
		if (bcs_mask & (BIT(5)|BIT(6)))
			xe_mmio_write32(gt, XEHPC_BCS5_BCS6_INTR_MASK, ~dmask);
		if (bcs_mask & (BIT(7)|BIT(8)))
			xe_mmio_write32(gt, XEHPC_BCS7_BCS8_INTR_MASK, ~dmask);
		if (ccs_mask & (BIT(0)|BIT(1)))
			xe_mmio_write32(gt, CCS0_CCS1_INTR_MASK, ~dmask);
		if (ccs_mask & (BIT(2)|BIT(3)))
			xe_mmio_write32(gt,  CCS2_CCS3_INTR_MASK, ~dmask);
	}

	if (xe_gt_is_media_type(gt) || MEDIA_VER(xe) < 13) {
		/* Enable interrupts for each engine class */
		xe_mmio_write32(gt, VCS_VECS_INTR_ENABLE, dmask);

		/* Unmask interrupts for each engine instance */
		xe_mmio_write32(gt, VCS0_VCS1_INTR_MASK, ~dmask);
		xe_mmio_write32(gt, VCS2_VCS3_INTR_MASK, ~dmask);
		xe_mmio_write32(gt, VECS0_VECS1_INTR_MASK, ~dmask);

		/*
		 * the heci2 interrupt is enabled via the same register as the
		 * GSCCS interrupts, but it has its own mask register.
		 */
		if (xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_OTHER)) {
			gsc_mask = irqs | GSC_ER_COMPLETE;
			heci_mask = GSC_IRQ_INTF(1);
		} else if (HAS_HECI_GSCFI(xe)) {
			gsc_mask = GSC_IRQ_INTF(1);
		}

		if (gsc_mask) {
			xe_mmio_write32(gt, GUNIT_GSC_INTR_ENABLE, gsc_mask | heci_mask);
			xe_mmio_write32(gt, GUNIT_GSC_INTR_MASK, ~gsc_mask);
		}
		if (heci_mask)
			xe_mmio_write32(gt, HECI2_RSVD_INTR_MASK, ~(heci_mask << 16));
	}
}

static u32
gt_engine_identity(struct xe_device *xe,
		   struct xe_gt *mmio,
		   const unsigned int bank,
		   const unsigned int bit)
{
	u32 timeout_ts;
	u32 ident;

	lockdep_assert_held(&xe->irq.lock);

	xe_mmio_write32(mmio, IIR_REG_SELECTOR(bank), BIT(bit));

	/*
	 * NB: Specs do not specify how long to spin wait,
	 * so we do ~100us as an educated guess.
	 */
	timeout_ts = (local_clock() >> 10) + 100;
	do {
		ident = xe_mmio_read32(mmio, INTR_IDENTITY_REG(bank));
	} while (!(ident & INTR_DATA_VALID) &&
		 !time_after32(local_clock() >> 10, timeout_ts));

	if (unlikely(!(ident & INTR_DATA_VALID))) {
		drm_err(&xe->drm, "INTR_IDENTITY_REG%u:%u 0x%08x not valid!\n",
			bank, bit, ident);
		return 0;
	}

	xe_mmio_write32(mmio, INTR_IDENTITY_REG(bank), ident);

	return ident;
}

#define   OTHER_MEDIA_GUC_INSTANCE           16

static void
gt_other_irq_handler(struct xe_gt *gt, const u8 instance, const u16 iir)
{
	if (instance == OTHER_GUC_INSTANCE && !xe_gt_is_media_type(gt))
		return xe_guc_irq_handler(&gt->uc.guc, iir);
	if (instance == OTHER_MEDIA_GUC_INSTANCE && xe_gt_is_media_type(gt))
		return xe_guc_irq_handler(&gt->uc.guc, iir);
	if (instance == OTHER_GSC_HECI2_INSTANCE && xe_gt_is_media_type(gt))
		return xe_gsc_proxy_irq_handler(&gt->uc.gsc, iir);

	if (instance != OTHER_GUC_INSTANCE &&
	    instance != OTHER_MEDIA_GUC_INSTANCE) {
		WARN_ONCE(1, "unhandled other interrupt instance=0x%x, iir=0x%x\n",
			  instance, iir);
	}
}

static struct xe_gt *pick_engine_gt(struct xe_tile *tile,
				    enum xe_engine_class class,
				    unsigned int instance)
{
	struct xe_device *xe = tile_to_xe(tile);

	if (MEDIA_VER(xe) < 13)
		return tile->primary_gt;

	switch (class) {
	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return tile->media_gt;
	case XE_ENGINE_CLASS_OTHER:
		switch (instance) {
		case OTHER_MEDIA_GUC_INSTANCE:
		case OTHER_GSC_INSTANCE:
		case OTHER_GSC_HECI2_INSTANCE:
			return tile->media_gt;
		default:
			break;
		};
		fallthrough;
	default:
		return tile->primary_gt;
	}
}

static void gt_irq_handler(struct xe_tile *tile,
			   u32 master_ctl, unsigned long *intr_dw,
			   u32 *identity)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_gt *mmio = tile->primary_gt;
	unsigned int bank, bit;
	u16 instance, intr_vec;
	enum xe_engine_class class;
	struct xe_hw_engine *hwe;

	spin_lock(&xe->irq.lock);

	for (bank = 0; bank < 2; bank++) {
		if (!(master_ctl & GT_DW_IRQ(bank)))
			continue;

		intr_dw[bank] = xe_mmio_read32(mmio, GT_INTR_DW(bank));
		for_each_set_bit(bit, intr_dw + bank, 32)
			identity[bit] = gt_engine_identity(xe, mmio, bank, bit);
		xe_mmio_write32(mmio, GT_INTR_DW(bank), intr_dw[bank]);

		for_each_set_bit(bit, intr_dw + bank, 32) {
			struct xe_gt *engine_gt;

			class = INTR_ENGINE_CLASS(identity[bit]);
			instance = INTR_ENGINE_INSTANCE(identity[bit]);
			intr_vec = INTR_ENGINE_INTR(identity[bit]);

			engine_gt = pick_engine_gt(tile, class, instance);

			hwe = xe_gt_hw_engine(engine_gt, class, instance, false);
			if (hwe) {
				xe_hw_engine_handle_irq(hwe, intr_vec);
				continue;
			}

			if (class == XE_ENGINE_CLASS_OTHER) {
				/* HECI GSCFI interrupts come from outside of GT */
				if (HAS_HECI_GSCFI(xe) && instance == OTHER_GSC_INSTANCE)
					xe_heci_gsc_irq_handler(xe, intr_vec);
				else
					gt_other_irq_handler(engine_gt, instance, intr_vec);
			}
		}
	}

	spin_unlock(&xe->irq.lock);
}

/*
 * Top-level interrupt handler for Xe_LP platforms (which did not have
 * a "master tile" interrupt register.
 */
static irqreturn_t xelp_irq_handler(int irq, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	u32 master_ctl, gu_misc_iir;
	unsigned long intr_dw[2];
	u32 identity[32];

	spin_lock(&xe->irq.lock);
	if (!xe->irq.enabled) {
		spin_unlock(&xe->irq.lock);
		return IRQ_NONE;
	}
	spin_unlock(&xe->irq.lock);

	master_ctl = xelp_intr_disable(xe);
	if (!master_ctl) {
		xelp_intr_enable(xe, false);
		return IRQ_NONE;
	}

	gt_irq_handler(tile, master_ctl, intr_dw, identity);

	xe_display_irq_handler(xe, master_ctl);

	gu_misc_iir = gu_misc_irq_ack(xe, master_ctl);

	xelp_intr_enable(xe, false);

	xe_display_irq_enable(xe, gu_misc_iir);

	return IRQ_HANDLED;
}

static u32 dg1_intr_disable(struct xe_device *xe)
{
	struct xe_gt *mmio = xe_root_mmio_gt(xe);
	u32 val;

	/* First disable interrupts */
	xe_mmio_write32(mmio, DG1_MSTR_TILE_INTR, 0);

	/* Get the indication levels and ack the master unit */
	val = xe_mmio_read32(mmio, DG1_MSTR_TILE_INTR);
	if (unlikely(!val))
		return 0;

	xe_mmio_write32(mmio, DG1_MSTR_TILE_INTR, val);

	return val;
}

static void dg1_intr_enable(struct xe_device *xe, bool stall)
{
	struct xe_gt *mmio = xe_root_mmio_gt(xe);

	xe_mmio_write32(mmio, DG1_MSTR_TILE_INTR, DG1_MSTR_IRQ);
	if (stall)
		xe_mmio_read32(mmio, DG1_MSTR_TILE_INTR);
}

/*
 * Top-level interrupt handler for Xe_LP+ and beyond.  These platforms have
 * a "master tile" interrupt register which must be consulted before the
 * "graphics master" interrupt register.
 */
static irqreturn_t dg1_irq_handler(int irq, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_tile *tile;
	u32 master_tile_ctl, master_ctl = 0, gu_misc_iir = 0;
	unsigned long intr_dw[2];
	u32 identity[32];
	u8 id;

	/* TODO: This really shouldn't be copied+pasted */

	spin_lock(&xe->irq.lock);
	if (!xe->irq.enabled) {
		spin_unlock(&xe->irq.lock);
		return IRQ_NONE;
	}
	spin_unlock(&xe->irq.lock);

	master_tile_ctl = dg1_intr_disable(xe);
	if (!master_tile_ctl) {
		dg1_intr_enable(xe, false);
		return IRQ_NONE;
	}

	for_each_tile(tile, xe, id) {
		struct xe_gt *mmio = tile->primary_gt;

		if ((master_tile_ctl & DG1_MSTR_TILE(tile->id)) == 0)
			continue;

		master_ctl = xe_mmio_read32(mmio, GFX_MSTR_IRQ);

		/*
		 * We might be in irq handler just when PCIe DPC is initiated
		 * and all MMIO reads will be returned with all 1's. Ignore this
		 * irq as device is inaccessible.
		 */
		if (master_ctl == REG_GENMASK(31, 0)) {
			drm_dbg(&tile_to_xe(tile)->drm,
				"Ignore this IRQ as device might be in DPC containment.\n");
			return IRQ_HANDLED;
		}

		xe_mmio_write32(mmio, GFX_MSTR_IRQ, master_ctl);

		gt_irq_handler(tile, master_ctl, intr_dw, identity);

		/*
		 * Display interrupts (including display backlight operations
		 * that get reported as Gunit GSE) would only be hooked up to
		 * the primary tile.
		 */
		if (id == 0) {
			xe_display_irq_handler(xe, master_ctl);
			gu_misc_iir = gu_misc_irq_ack(xe, master_ctl);
		}
	}

	dg1_intr_enable(xe, false);
	xe_display_irq_enable(xe, gu_misc_iir);

	return IRQ_HANDLED;
}

static void gt_irq_reset(struct xe_tile *tile)
{
	struct xe_gt *mmio = tile->primary_gt;

	u32 ccs_mask = xe_hw_engine_mask_per_class(tile->primary_gt,
						   XE_ENGINE_CLASS_COMPUTE);
	u32 bcs_mask = xe_hw_engine_mask_per_class(tile->primary_gt,
						   XE_ENGINE_CLASS_COPY);

	/* Disable RCS, BCS, VCS and VECS class engines. */
	xe_mmio_write32(mmio, RENDER_COPY_INTR_ENABLE, 0);
	xe_mmio_write32(mmio, VCS_VECS_INTR_ENABLE, 0);
	if (ccs_mask)
		xe_mmio_write32(mmio, CCS_RSVD_INTR_ENABLE, 0);

	/* Restore masks irqs on RCS, BCS, VCS and VECS engines. */
	xe_mmio_write32(mmio, RCS0_RSVD_INTR_MASK,	~0);
	xe_mmio_write32(mmio, BCS_RSVD_INTR_MASK,	~0);
	if (bcs_mask & (BIT(1)|BIT(2)))
		xe_mmio_write32(mmio, XEHPC_BCS1_BCS2_INTR_MASK, ~0);
	if (bcs_mask & (BIT(3)|BIT(4)))
		xe_mmio_write32(mmio, XEHPC_BCS3_BCS4_INTR_MASK, ~0);
	if (bcs_mask & (BIT(5)|BIT(6)))
		xe_mmio_write32(mmio, XEHPC_BCS5_BCS6_INTR_MASK, ~0);
	if (bcs_mask & (BIT(7)|BIT(8)))
		xe_mmio_write32(mmio, XEHPC_BCS7_BCS8_INTR_MASK, ~0);
	xe_mmio_write32(mmio, VCS0_VCS1_INTR_MASK,	~0);
	xe_mmio_write32(mmio, VCS2_VCS3_INTR_MASK,	~0);
	xe_mmio_write32(mmio, VECS0_VECS1_INTR_MASK,	~0);
	if (ccs_mask & (BIT(0)|BIT(1)))
		xe_mmio_write32(mmio, CCS0_CCS1_INTR_MASK, ~0);
	if (ccs_mask & (BIT(2)|BIT(3)))
		xe_mmio_write32(mmio,  CCS2_CCS3_INTR_MASK, ~0);

	if ((tile->media_gt &&
	     xe_hw_engine_mask_per_class(tile->media_gt, XE_ENGINE_CLASS_OTHER)) ||
	    HAS_HECI_GSCFI(tile_to_xe(tile))) {
		xe_mmio_write32(mmio, GUNIT_GSC_INTR_ENABLE, 0);
		xe_mmio_write32(mmio, GUNIT_GSC_INTR_MASK, ~0);
		xe_mmio_write32(mmio, HECI2_RSVD_INTR_MASK, ~0);
	}

	xe_mmio_write32(mmio, GPM_WGBOXPERF_INTR_ENABLE, 0);
	xe_mmio_write32(mmio, GPM_WGBOXPERF_INTR_MASK,  ~0);
	xe_mmio_write32(mmio, GUC_SG_INTR_ENABLE,	 0);
	xe_mmio_write32(mmio, GUC_SG_INTR_MASK,		~0);
}

static void xelp_irq_reset(struct xe_tile *tile)
{
	xelp_intr_disable(tile_to_xe(tile));

	gt_irq_reset(tile);

	if (IS_SRIOV_VF(tile_to_xe(tile)))
		return;

	mask_and_disable(tile, PCU_IRQ_OFFSET);
}

static void dg1_irq_reset(struct xe_tile *tile)
{
	if (tile->id == 0)
		dg1_intr_disable(tile_to_xe(tile));

	gt_irq_reset(tile);

	if (IS_SRIOV_VF(tile_to_xe(tile)))
		return;

	mask_and_disable(tile, PCU_IRQ_OFFSET);
}

static void dg1_irq_reset_mstr(struct xe_tile *tile)
{
	struct xe_gt *mmio = tile->primary_gt;

	xe_mmio_write32(mmio, GFX_MSTR_IRQ, ~0);
}

static void vf_irq_reset(struct xe_device *xe)
{
	struct xe_tile *tile;
	unsigned int id;

	xe_assert(xe, IS_SRIOV_VF(xe));

	if (GRAPHICS_VERx100(xe) < 1210)
		xelp_intr_disable(xe);
	else
		xe_assert(xe, xe_device_has_memirq(xe));

	for_each_tile(tile, xe, id) {
		if (xe_device_has_memirq(xe))
			xe_memirq_reset(&tile->sriov.vf.memirq);
		else
			gt_irq_reset(tile);
	}
}

static void xe_irq_reset(struct xe_device *xe)
{
	struct xe_tile *tile;
	u8 id;

	if (IS_SRIOV_VF(xe))
		return vf_irq_reset(xe);

	for_each_tile(tile, xe, id) {
		if (GRAPHICS_VERx100(xe) >= 1210)
			dg1_irq_reset(tile);
		else
			xelp_irq_reset(tile);
	}

	tile = xe_device_get_root_tile(xe);
	mask_and_disable(tile, GU_MISC_IRQ_OFFSET);
	xe_display_irq_reset(xe);

	/*
	 * The tile's top-level status register should be the last one
	 * to be reset to avoid possible bit re-latching from lower
	 * level interrupts.
	 */
	if (GRAPHICS_VERx100(xe) >= 1210) {
		for_each_tile(tile, xe, id)
			dg1_irq_reset_mstr(tile);
	}
}

static void vf_irq_postinstall(struct xe_device *xe)
{
	struct xe_tile *tile;
	unsigned int id;

	for_each_tile(tile, xe, id)
		if (xe_device_has_memirq(xe))
			xe_memirq_postinstall(&tile->sriov.vf.memirq);

	if (GRAPHICS_VERx100(xe) < 1210)
		xelp_intr_enable(xe, true);
	else
		xe_assert(xe, xe_device_has_memirq(xe));
}

static void xe_irq_postinstall(struct xe_device *xe)
{
	if (IS_SRIOV_VF(xe))
		return vf_irq_postinstall(xe);

	xe_display_irq_postinstall(xe, xe_root_mmio_gt(xe));

	/*
	 * ASLE backlight operations are reported via GUnit GSE interrupts
	 * on the root tile.
	 */
	unmask_and_enable(xe_device_get_root_tile(xe),
			  GU_MISC_IRQ_OFFSET, GU_MISC_GSE);

	/* Enable top-level interrupts */
	if (GRAPHICS_VERx100(xe) >= 1210)
		dg1_intr_enable(xe, true);
	else
		xelp_intr_enable(xe, true);
}

static irqreturn_t vf_mem_irq_handler(int irq, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_tile *tile;
	unsigned int id;

	spin_lock(&xe->irq.lock);
	if (!xe->irq.enabled) {
		spin_unlock(&xe->irq.lock);
		return IRQ_NONE;
	}
	spin_unlock(&xe->irq.lock);

	for_each_tile(tile, xe, id)
		xe_memirq_handler(&tile->sriov.vf.memirq);

	return IRQ_HANDLED;
}

static irq_handler_t xe_irq_handler(struct xe_device *xe)
{
	if (IS_SRIOV_VF(xe) && xe_device_has_memirq(xe))
		return vf_mem_irq_handler;

	if (GRAPHICS_VERx100(xe) >= 1210)
		return dg1_irq_handler;
	else
		return xelp_irq_handler;
}

static void irq_uninstall(void *arg)
{
	struct xe_device *xe = arg;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int irq;

	if (!xe->irq.enabled)
		return;

	xe->irq.enabled = false;
	xe_irq_reset(xe);

	irq = pci_irq_vector(pdev, 0);
	free_irq(irq, xe);
}

int xe_irq_install(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	unsigned int irq_flags = PCI_IRQ_MSIX;
	irq_handler_t irq_handler;
	int err, irq, nvec;

	irq_handler = xe_irq_handler(xe);
	if (!irq_handler) {
		drm_err(&xe->drm, "No supported interrupt handler");
		return -EINVAL;
	}

	xe_irq_reset(xe);

	nvec = pci_msix_vec_count(pdev);
	if (nvec <= 0) {
		if (nvec == -EINVAL) {
			/* MSIX capability is not supported in the device, using MSI */
			irq_flags = PCI_IRQ_MSI;
			nvec = 1;
		} else {
			drm_err(&xe->drm, "MSIX: Failed getting count\n");
			return nvec;
		}
	}

	err = pci_alloc_irq_vectors(pdev, nvec, nvec, irq_flags);
	if (err < 0) {
		drm_err(&xe->drm, "MSI/MSIX: Failed to enable support %d\n", err);
		return err;
	}

	irq = pci_irq_vector(pdev, 0);
	err = request_irq(irq, irq_handler, IRQF_SHARED, DRIVER_NAME, xe);
	if (err < 0) {
		drm_err(&xe->drm, "Failed to request MSI/MSIX IRQ %d\n", err);
		return err;
	}

	xe->irq.enabled = true;

	xe_irq_postinstall(xe);

	err = devm_add_action_or_reset(xe->drm.dev, irq_uninstall, xe);
	if (err)
		goto free_irq_handler;

	return 0;

free_irq_handler:
	free_irq(irq, xe);

	return err;
}

void xe_irq_shutdown(struct xe_device *xe)
{
	irq_uninstall(xe);
}

void xe_irq_suspend(struct xe_device *xe)
{
	int irq = to_pci_dev(xe->drm.dev)->irq;

	spin_lock_irq(&xe->irq.lock);
	xe->irq.enabled = false; /* no new irqs */
	spin_unlock_irq(&xe->irq.lock);

	synchronize_irq(irq); /* flush irqs */
	xe_irq_reset(xe); /* turn irqs off */
}

void xe_irq_resume(struct xe_device *xe)
{
	struct xe_gt *gt;
	int id;

	/*
	 * lock not needed:
	 * 1. no irq will arrive before the postinstall
	 * 2. display is not yet resumed
	 */
	xe->irq.enabled = true;
	xe_irq_reset(xe);
	xe_irq_postinstall(xe); /* turn irqs on */

	for_each_gt(gt, xe, id)
		xe_irq_enable_hwe(gt);
}
