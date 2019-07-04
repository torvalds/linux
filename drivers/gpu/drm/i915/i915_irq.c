/* i915_irq.c -- IRQ support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/circ_buf.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/sysrq.h>

#include <drm/drm_drv.h>
#include <drm/drm_irq.h>
#include <drm/i915_drm.h>

#include "display/intel_fifo_underrun.h"
#include "display/intel_hotplug.h"
#include "display/intel_lpe_audio.h"
#include "display/intel_psr.h"

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_pm.h"

/**
 * DOC: interrupt handling
 *
 * These functions provide the basic support for enabling and disabling the
 * interrupt handling support. There's a lot more functionality in i915_irq.c
 * and related files, but that will be described in separate chapters.
 */

static const u32 hpd_ilk[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG,
};

static const u32 hpd_ivb[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG_IVB,
};

static const u32 hpd_bdw[HPD_NUM_PINS] = {
	[HPD_PORT_A] = GEN8_PORT_DP_A_HOTPLUG,
};

static const u32 hpd_ibx[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG
};

static const u32 hpd_cpt[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG_CPT,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG_CPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT
};

static const u32 hpd_spt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_PORTA_HOTPLUG_SPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT,
	[HPD_PORT_E] = SDE_PORTE_HOTPLUG_SPT
};

static const u32 hpd_mask_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_EN,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_EN,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_EN,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_EN,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_EN,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_EN
};

static const u32 hpd_status_g4x[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_G4X,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_G4X,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS
};

static const u32 hpd_status_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I915,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I915,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS
};

/* BXT hpd list */
static const u32 hpd_bxt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = BXT_DE_PORT_HP_DDIA,
	[HPD_PORT_B] = BXT_DE_PORT_HP_DDIB,
	[HPD_PORT_C] = BXT_DE_PORT_HP_DDIC
};

static const u32 hpd_gen11[HPD_NUM_PINS] = {
	[HPD_PORT_C] = GEN11_TC1_HOTPLUG | GEN11_TBT1_HOTPLUG,
	[HPD_PORT_D] = GEN11_TC2_HOTPLUG | GEN11_TBT2_HOTPLUG,
	[HPD_PORT_E] = GEN11_TC3_HOTPLUG | GEN11_TBT3_HOTPLUG,
	[HPD_PORT_F] = GEN11_TC4_HOTPLUG | GEN11_TBT4_HOTPLUG
};

static const u32 hpd_icp[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDIA_HOTPLUG_ICP,
	[HPD_PORT_B] = SDE_DDIB_HOTPLUG_ICP,
	[HPD_PORT_C] = SDE_TC1_HOTPLUG_ICP,
	[HPD_PORT_D] = SDE_TC2_HOTPLUG_ICP,
	[HPD_PORT_E] = SDE_TC3_HOTPLUG_ICP,
	[HPD_PORT_F] = SDE_TC4_HOTPLUG_ICP
};

static const u32 hpd_mcc[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDIA_HOTPLUG_ICP,
	[HPD_PORT_B] = SDE_DDIB_HOTPLUG_ICP,
	[HPD_PORT_C] = SDE_TC1_HOTPLUG_ICP
};

static void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
			   i915_reg_t iir, i915_reg_t ier)
{
	intel_uncore_write(uncore, imr, 0xffffffff);
	intel_uncore_posting_read(uncore, imr);

	intel_uncore_write(uncore, ier, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
}

static void gen2_irq_reset(struct intel_uncore *uncore)
{
	intel_uncore_write16(uncore, GEN2_IMR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IMR);

	intel_uncore_write16(uncore, GEN2_IER, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
}

#define GEN8_IRQ_RESET_NDX(uncore, type, which) \
({ \
	unsigned int which_ = which; \
	gen3_irq_reset((uncore), GEN8_##type##_IMR(which_), \
		       GEN8_##type##_IIR(which_), GEN8_##type##_IER(which_)); \
})

#define GEN3_IRQ_RESET(uncore, type) \
	gen3_irq_reset((uncore), type##IMR, type##IIR, type##IER)

#define GEN2_IRQ_RESET(uncore) \
	gen2_irq_reset(uncore)

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
static void gen3_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
{
	u32 val = intel_uncore_read(uncore, reg);

	if (val == 0)
		return;

	WARN(1, "Interrupt register 0x%x is not zero: 0x%08x\n",
	     i915_mmio_reg_offset(reg), val);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
}

static void gen2_assert_iir_is_zero(struct intel_uncore *uncore)
{
	u16 val = intel_uncore_read16(uncore, GEN2_IIR);

	if (val == 0)
		return;

	WARN(1, "Interrupt register 0x%x is not zero: 0x%08x\n",
	     i915_mmio_reg_offset(GEN2_IIR), val);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
}

static void gen3_irq_init(struct intel_uncore *uncore,
			  i915_reg_t imr, u32 imr_val,
			  i915_reg_t ier, u32 ier_val,
			  i915_reg_t iir)
{
	gen3_assert_iir_is_zero(uncore, iir);

	intel_uncore_write(uncore, ier, ier_val);
	intel_uncore_write(uncore, imr, imr_val);
	intel_uncore_posting_read(uncore, imr);
}

static void gen2_irq_init(struct intel_uncore *uncore,
			  u32 imr_val, u32 ier_val)
{
	gen2_assert_iir_is_zero(uncore);

	intel_uncore_write16(uncore, GEN2_IER, ier_val);
	intel_uncore_write16(uncore, GEN2_IMR, imr_val);
	intel_uncore_posting_read16(uncore, GEN2_IMR);
}

#define GEN8_IRQ_INIT_NDX(uncore, type, which, imr_val, ier_val) \
({ \
	unsigned int which_ = which; \
	gen3_irq_init((uncore), \
		      GEN8_##type##_IMR(which_), imr_val, \
		      GEN8_##type##_IER(which_), ier_val, \
		      GEN8_##type##_IIR(which_)); \
})

#define GEN3_IRQ_INIT(uncore, type, imr_val, ier_val) \
	gen3_irq_init((uncore), \
		      type##IMR, imr_val, \
		      type##IER, ier_val, \
		      type##IIR)

#define GEN2_IRQ_INIT(uncore, imr_val, ier_val) \
	gen2_irq_init((uncore), imr_val, ier_val)

static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir);
static void gen9_guc_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir);

/* For display hotplug interrupt */
static inline void
i915_hotplug_interrupt_update_locked(struct drm_i915_private *dev_priv,
				     u32 mask,
				     u32 bits)
{
	u32 val;

	lockdep_assert_held(&dev_priv->irq_lock);
	WARN_ON(bits & ~mask);

	val = I915_READ(PORT_HOTPLUG_EN);
	val &= ~mask;
	val |= bits;
	I915_WRITE(PORT_HOTPLUG_EN, val);
}

/**
 * i915_hotplug_interrupt_update - update hotplug interrupt enable
 * @dev_priv: driver private
 * @mask: bits to update
 * @bits: bits to enable
 * NOTE: the HPD enable bits are modified both inside and outside
 * of an interrupt context. To avoid that read-modify-write cycles
 * interfer, these bits are protected by a spinlock. Since this
 * function is usually not called from a context where the lock is
 * held already, this function acquires the lock itself. A non-locking
 * version is also available.
 */
void i915_hotplug_interrupt_update(struct drm_i915_private *dev_priv,
				   u32 mask,
				   u32 bits)
{
	spin_lock_irq(&dev_priv->irq_lock);
	i915_hotplug_interrupt_update_locked(dev_priv, mask, bits);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static u32
gen11_gt_engine_identity(struct intel_gt *gt,
			 const unsigned int bank, const unsigned int bit);

static bool gen11_reset_one_iir(struct intel_gt *gt,
				const unsigned int bank,
				const unsigned int bit)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 dw;

	lockdep_assert_held(&gt->i915->irq_lock);

	dw = raw_reg_read(regs, GEN11_GT_INTR_DW(bank));
	if (dw & BIT(bit)) {
		/*
		 * According to the BSpec, DW_IIR bits cannot be cleared without
		 * first servicing the Selector & Shared IIR registers.
		 */
		gen11_gt_engine_identity(gt, bank, bit);

		/*
		 * We locked GT INT DW by reading it. If we want to (try
		 * to) recover from this succesfully, we need to clear
		 * our bit, otherwise we are locking the register for
		 * everybody.
		 */
		raw_reg_write(regs, GEN11_GT_INTR_DW(bank), BIT(bit));

		return true;
	}

	return false;
}

/**
 * ilk_update_display_irq - update DEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ilk_update_display_irq(struct drm_i915_private *dev_priv,
			    u32 interrupt_mask,
			    u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	new_val = dev_priv->irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->irq_mask) {
		dev_priv->irq_mask = new_val;
		I915_WRITE(DEIMR, dev_priv->irq_mask);
		POSTING_READ(DEIMR);
	}
}

/**
 * ilk_update_gt_irq - update GTIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void ilk_update_gt_irq(struct drm_i915_private *dev_priv,
			      u32 interrupt_mask,
			      u32 enabled_irq_mask)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	dev_priv->gt_irq_mask &= ~interrupt_mask;
	dev_priv->gt_irq_mask |= (~enabled_irq_mask & interrupt_mask);
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
}

void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	ilk_update_gt_irq(dev_priv, mask, mask);
	intel_uncore_posting_read_fw(&dev_priv->uncore, GTIMR);
}

void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	ilk_update_gt_irq(dev_priv, mask, 0);
}

static i915_reg_t gen6_pm_iir(struct drm_i915_private *dev_priv)
{
	WARN_ON_ONCE(INTEL_GEN(dev_priv) >= 11);

	return INTEL_GEN(dev_priv) >= 8 ? GEN8_GT_IIR(2) : GEN6_PMIIR;
}

static void write_pm_imr(struct drm_i915_private *dev_priv)
{
	i915_reg_t reg;
	u32 mask = dev_priv->pm_imr;

	if (INTEL_GEN(dev_priv) >= 11) {
		reg = GEN11_GPM_WGBOXPERF_INTR_MASK;
		/* pm is in upper half */
		mask = mask << 16;
	} else if (INTEL_GEN(dev_priv) >= 8) {
		reg = GEN8_GT_IMR(2);
	} else {
		reg = GEN6_PMIMR;
	}

	I915_WRITE(reg, mask);
	POSTING_READ(reg);
}

static void write_pm_ier(struct drm_i915_private *dev_priv)
{
	i915_reg_t reg;
	u32 mask = dev_priv->pm_ier;

	if (INTEL_GEN(dev_priv) >= 11) {
		reg = GEN11_GPM_WGBOXPERF_INTR_ENABLE;
		/* pm is in upper half */
		mask = mask << 16;
	} else if (INTEL_GEN(dev_priv) >= 8) {
		reg = GEN8_GT_IER(2);
	} else {
		reg = GEN6_PMIER;
	}

	I915_WRITE(reg, mask);
}

/**
 * snb_update_pm_irq - update GEN6_PMIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void snb_update_pm_irq(struct drm_i915_private *dev_priv,
			      u32 interrupt_mask,
			      u32 enabled_irq_mask)
{
	u32 new_val;

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&dev_priv->irq_lock);

	new_val = dev_priv->pm_imr;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->pm_imr) {
		dev_priv->pm_imr = new_val;
		write_pm_imr(dev_priv);
	}
}

void gen6_unmask_pm_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	snb_update_pm_irq(dev_priv, mask, mask);
}

static void __gen6_mask_pm_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	snb_update_pm_irq(dev_priv, mask, 0);
}

void gen6_mask_pm_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	__gen6_mask_pm_irq(dev_priv, mask);
}

static void gen6_reset_pm_iir(struct drm_i915_private *dev_priv, u32 reset_mask)
{
	i915_reg_t reg = gen6_pm_iir(dev_priv);

	lockdep_assert_held(&dev_priv->irq_lock);

	I915_WRITE(reg, reset_mask);
	I915_WRITE(reg, reset_mask);
	POSTING_READ(reg);
}

static void gen6_enable_pm_irq(struct drm_i915_private *dev_priv, u32 enable_mask)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	dev_priv->pm_ier |= enable_mask;
	write_pm_ier(dev_priv);
	gen6_unmask_pm_irq(dev_priv, enable_mask);
	/* unmask_pm_irq provides an implicit barrier (POSTING_READ) */
}

static void gen6_disable_pm_irq(struct drm_i915_private *dev_priv, u32 disable_mask)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	dev_priv->pm_ier &= ~disable_mask;
	__gen6_mask_pm_irq(dev_priv, disable_mask);
	write_pm_ier(dev_priv);
	/* though a barrier is missing here, but don't really need a one */
}

void gen11_reset_rps_interrupts(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);

	while (gen11_reset_one_iir(&dev_priv->gt, 0, GEN11_GTPM))
		;

	dev_priv->gt_pm.rps.pm_iir = 0;

	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen6_reset_rps_interrupts(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);
	gen6_reset_pm_iir(dev_priv, GEN6_PM_RPS_EVENTS);
	dev_priv->gt_pm.rps.pm_iir = 0;
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen6_enable_rps_interrupts(struct drm_i915_private *dev_priv)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;

	if (READ_ONCE(rps->interrupts_enabled))
		return;

	spin_lock_irq(&dev_priv->irq_lock);
	WARN_ON_ONCE(rps->pm_iir);

	if (INTEL_GEN(dev_priv) >= 11)
		WARN_ON_ONCE(gen11_reset_one_iir(&dev_priv->gt, 0, GEN11_GTPM));
	else
		WARN_ON_ONCE(I915_READ(gen6_pm_iir(dev_priv)) & dev_priv->pm_rps_events);

	rps->interrupts_enabled = true;
	gen6_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);

	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen6_disable_rps_interrupts(struct drm_i915_private *dev_priv)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;

	if (!READ_ONCE(rps->interrupts_enabled))
		return;

	spin_lock_irq(&dev_priv->irq_lock);
	rps->interrupts_enabled = false;

	I915_WRITE(GEN6_PMINTRMSK, gen6_sanitize_rps_pm_mask(dev_priv, ~0u));

	gen6_disable_pm_irq(dev_priv, GEN6_PM_RPS_EVENTS);

	spin_unlock_irq(&dev_priv->irq_lock);
	intel_synchronize_irq(dev_priv);

	/* Now that we will not be generating any more work, flush any
	 * outstanding tasks. As we are called on the RPS idle path,
	 * we will reset the GPU to minimum frequencies, so the current
	 * state of the worker can be discarded.
	 */
	cancel_work_sync(&rps->work);
	if (INTEL_GEN(dev_priv) >= 11)
		gen11_reset_rps_interrupts(dev_priv);
	else
		gen6_reset_rps_interrupts(dev_priv);
}

void gen9_reset_guc_interrupts(struct drm_i915_private *dev_priv)
{
	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	spin_lock_irq(&dev_priv->irq_lock);
	gen6_reset_pm_iir(dev_priv, dev_priv->pm_guc_events);
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen9_enable_guc_interrupts(struct drm_i915_private *dev_priv)
{
	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	spin_lock_irq(&dev_priv->irq_lock);
	if (!dev_priv->guc.interrupts.enabled) {
		WARN_ON_ONCE(I915_READ(gen6_pm_iir(dev_priv)) &
				       dev_priv->pm_guc_events);
		dev_priv->guc.interrupts.enabled = true;
		gen6_enable_pm_irq(dev_priv, dev_priv->pm_guc_events);
	}
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen9_disable_guc_interrupts(struct drm_i915_private *dev_priv)
{
	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->guc.interrupts.enabled = false;

	gen6_disable_pm_irq(dev_priv, dev_priv->pm_guc_events);

	spin_unlock_irq(&dev_priv->irq_lock);
	intel_synchronize_irq(dev_priv);

	gen9_reset_guc_interrupts(dev_priv);
}

void gen11_reset_guc_interrupts(struct drm_i915_private *i915)
{
	spin_lock_irq(&i915->irq_lock);
	gen11_reset_one_iir(&i915->gt, 0, GEN11_GUC);
	spin_unlock_irq(&i915->irq_lock);
}

void gen11_enable_guc_interrupts(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);
	if (!dev_priv->guc.interrupts.enabled) {
		u32 events = REG_FIELD_PREP(ENGINE1_MASK,
					    GEN11_GUC_INTR_GUC2HOST);

		WARN_ON_ONCE(gen11_reset_one_iir(&dev_priv->gt, 0, GEN11_GUC));
		I915_WRITE(GEN11_GUC_SG_INTR_ENABLE, events);
		I915_WRITE(GEN11_GUC_SG_INTR_MASK, ~events);
		dev_priv->guc.interrupts.enabled = true;
	}
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen11_disable_guc_interrupts(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->guc.interrupts.enabled = false;

	I915_WRITE(GEN11_GUC_SG_INTR_MASK, ~0);
	I915_WRITE(GEN11_GUC_SG_INTR_ENABLE, 0);

	spin_unlock_irq(&dev_priv->irq_lock);
	intel_synchronize_irq(dev_priv);

	gen11_reset_guc_interrupts(dev_priv);
}

/**
 * bdw_update_port_irq - update DE port interrupt
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void bdw_update_port_irq(struct drm_i915_private *dev_priv,
				u32 interrupt_mask,
				u32 enabled_irq_mask)
{
	u32 new_val;
	u32 old_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	old_val = I915_READ(GEN8_DE_PORT_IMR);

	new_val = old_val;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != old_val) {
		I915_WRITE(GEN8_DE_PORT_IMR, new_val);
		POSTING_READ(GEN8_DE_PORT_IMR);
	}
}

/**
 * bdw_update_pipe_irq - update DE pipe interrupt
 * @dev_priv: driver private
 * @pipe: pipe whose interrupt to update
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void bdw_update_pipe_irq(struct drm_i915_private *dev_priv,
			 enum pipe pipe,
			 u32 interrupt_mask,
			 u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	new_val = dev_priv->de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->de_irq_mask[pipe]) {
		dev_priv->de_irq_mask[pipe] = new_val;
		I915_WRITE(GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
		POSTING_READ(GEN8_DE_PIPE_IMR(pipe));
	}
}

/**
 * ibx_display_interrupt_update - update SDEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
				  u32 interrupt_mask,
				  u32 enabled_irq_mask)
{
	u32 sdeimr = I915_READ(SDEIMR);
	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&dev_priv->irq_lock);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	I915_WRITE(SDEIMR, sdeimr);
	POSTING_READ(SDEIMR);
}

u32 i915_pipestat_enable_mask(struct drm_i915_private *dev_priv,
			      enum pipe pipe)
{
	u32 status_mask = dev_priv->pipestat_irq_mask[pipe];
	u32 enable_mask = status_mask << 16;

	lockdep_assert_held(&dev_priv->irq_lock);

	if (INTEL_GEN(dev_priv) < 5)
		goto out;

	/*
	 * On pipe A we don't support the PSR interrupt yet,
	 * on pipe B and C the same bit MBZ.
	 */
	if (WARN_ON_ONCE(status_mask & PIPE_A_PSR_STATUS_VLV))
		return 0;
	/*
	 * On pipe B and C we don't support the PSR interrupt yet, on pipe
	 * A the same bit is for perf counters which we don't use either.
	 */
	if (WARN_ON_ONCE(status_mask & PIPE_B_PSR_STATUS_VLV))
		return 0;

	enable_mask &= ~(PIPE_FIFO_UNDERRUN_STATUS |
			 SPRITE0_FLIP_DONE_INT_EN_VLV |
			 SPRITE1_FLIP_DONE_INT_EN_VLV);
	if (status_mask & SPRITE0_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE0_FLIP_DONE_INT_EN_VLV;
	if (status_mask & SPRITE1_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE1_FLIP_DONE_INT_EN_VLV;

out:
	WARN_ONCE(enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
		  status_mask & ~PIPESTAT_INT_STATUS_MASK,
		  "pipe %c: enable_mask=0x%x, status_mask=0x%x\n",
		  pipe_name(pipe), enable_mask, status_mask);

	return enable_mask;
}

void i915_enable_pipestat(struct drm_i915_private *dev_priv,
			  enum pipe pipe, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 enable_mask;

	WARN_ONCE(status_mask & ~PIPESTAT_INT_STATUS_MASK,
		  "pipe %c: status_mask=0x%x\n",
		  pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	WARN_ON(!intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == status_mask)
		return;

	dev_priv->pipestat_irq_mask[pipe] |= status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	I915_WRITE(reg, enable_mask | status_mask);
	POSTING_READ(reg);
}

void i915_disable_pipestat(struct drm_i915_private *dev_priv,
			   enum pipe pipe, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 enable_mask;

	WARN_ONCE(status_mask & ~PIPESTAT_INT_STATUS_MASK,
		  "pipe %c: status_mask=0x%x\n",
		  pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	WARN_ON(!intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == 0)
		return;

	dev_priv->pipestat_irq_mask[pipe] &= ~status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	I915_WRITE(reg, enable_mask | status_mask);
	POSTING_READ(reg);
}

static bool i915_has_asle(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->opregion.asle)
		return false;

	return IS_PINEVIEW(dev_priv) || IS_MOBILE(dev_priv);
}

/**
 * i915_enable_asle_pipestat - enable ASLE pipestat for OpRegion
 * @dev_priv: i915 device private
 */
static void i915_enable_asle_pipestat(struct drm_i915_private *dev_priv)
{
	if (!i915_has_asle(dev_priv))
		return;

	spin_lock_irq(&dev_priv->irq_lock);

	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_LEGACY_BLC_EVENT_STATUS);
	if (INTEL_GEN(dev_priv) >= 4)
		i915_enable_pipestat(dev_priv, PIPE_A,
				     PIPE_LEGACY_BLC_EVENT_STATUS);

	spin_unlock_irq(&dev_priv->irq_lock);
}

/*
 * This timing diagram depicts the video signal in and
 * around the vertical blanking period.
 *
 * Assumptions about the fictitious mode used in this example:
 *  vblank_start >= 3
 *  vsync_start = vblank_start + 1
 *  vsync_end = vblank_start + 2
 *  vtotal = vblank_start + 3
 *
 *           start of vblank:
 *           latch double buffered registers
 *           increment frame counter (ctg+)
 *           generate start of vblank interrupt (gen4+)
 *           |
 *           |          frame start:
 *           |          generate frame start interrupt (aka. vblank interrupt) (gmch)
 *           |          may be shifted forward 1-3 extra lines via PIPECONF
 *           |          |
 *           |          |  start of vsync:
 *           |          |  generate vsync interrupt
 *           |          |  |
 * ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx___    ___xxxx
 *       .   \hs/   .      \hs/          \hs/          \hs/   .      \hs/
 * ----va---> <-----------------vb--------------------> <--------va-------------
 *       |          |       <----vs----->                     |
 * -vbs-----> <---vbs+1---> <---vbs+2---> <-----0-----> <-----1-----> <-----2--- (scanline counter gen2)
 * -vbs-2---> <---vbs-1---> <---vbs-----> <---vbs+1---> <---vbs+2---> <-----0--- (scanline counter gen3+)
 * -vbs-2---> <---vbs-2---> <---vbs-1---> <---vbs-----> <---vbs+1---> <---vbs+2- (scanline counter hsw+ hdmi)
 *       |          |                                         |
 *       last visible pixel                                   first visible pixel
 *                  |                                         increment frame counter (gen3/4)
 *                  pixel counter = vblank_start * htotal     pixel counter = 0 (gen3/4)
 *
 * x  = horizontal active
 * _  = horizontal blanking
 * hs = horizontal sync
 * va = vertical active
 * vb = vertical blanking
 * vs = vertical sync
 * vbs = vblank_start (number)
 *
 * Summary:
 * - most events happen at the start of horizontal sync
 * - frame start happens at the start of horizontal blank, 1-4 lines
 *   (depending on PIPECONF settings) after the start of vblank
 * - gen3/4 pixel and frame counter are synchronized with the start
 *   of horizontal active on the first line of vertical active
 */

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
u32 i915_get_vblank_counter(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct drm_vblank_crtc *vblank = &dev_priv->drm.vblank[drm_crtc_index(crtc)];
	const struct drm_display_mode *mode = &vblank->hwmode;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	i915_reg_t high_frame, low_frame;
	u32 high1, high2, low, pixel, vbl_start, hsync_start, htotal;
	unsigned long irqflags;

	/*
	 * On i965gm TV output the frame counter only works up to
	 * the point when we enable the TV encoder. After that the
	 * frame counter ceases to work and reads zero. We need a
	 * vblank wait before enabling the TV encoder and so we
	 * have to enable vblank interrupts while the frame counter
	 * is still in a working state. However the core vblank code
	 * does not like us returning non-zero frame counter values
	 * when we've told it that we don't have a working frame
	 * counter. Thus we must stop non-zero values leaking out.
	 */
	if (!vblank->max_vblank_count)
		return 0;

	htotal = mode->crtc_htotal;
	hsync_start = mode->crtc_hsync_start;
	vbl_start = mode->crtc_vblank_start;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vbl_start = DIV_ROUND_UP(vbl_start, 2);

	/* Convert to pixel count */
	vbl_start *= htotal;

	/* Start of vblank event occurs at start of hsync */
	vbl_start -= htotal - hsync_start;

	high_frame = PIPEFRAME(pipe);
	low_frame = PIPEFRAMEPIXEL(pipe);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = I915_READ_FW(high_frame) & PIPE_FRAME_HIGH_MASK;
		low   = I915_READ_FW(low_frame);
		high2 = I915_READ_FW(high_frame) & PIPE_FRAME_HIGH_MASK;
	} while (high1 != high2);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	high1 >>= PIPE_FRAME_HIGH_SHIFT;
	pixel = low & PIPE_PIXEL_MASK;
	low >>= PIPE_FRAME_LOW_SHIFT;

	/*
	 * The frame counter increments at beginning of active.
	 * Cook up a vblank counter by also checking the pixel
	 * counter against vblank start.
	 */
	return (((high1 << 8) | low) + (pixel >= vbl_start)) & 0xffffff;
}

u32 g4x_get_vblank_counter(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	return I915_READ(PIPE_FRMCOUNT_G4X(pipe));
}

/*
 * On certain encoders on certain platforms, pipe
 * scanline register will not work to get the scanline,
 * since the timings are driven from the PORT or issues
 * with scanline register updates.
 * This function will use Framestamp and current
 * timestamp registers to calculate the scanline.
 */
static u32 __intel_get_crtc_scanline_from_timestamp(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct drm_vblank_crtc *vblank =
		&crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	const struct drm_display_mode *mode = &vblank->hwmode;
	u32 vblank_start = mode->crtc_vblank_start;
	u32 vtotal = mode->crtc_vtotal;
	u32 htotal = mode->crtc_htotal;
	u32 clock = mode->crtc_clock;
	u32 scanline, scan_prev_time, scan_curr_time, scan_post_time;

	/*
	 * To avoid the race condition where we might cross into the
	 * next vblank just between the PIPE_FRMTMSTMP and TIMESTAMP_CTR
	 * reads. We make sure we read PIPE_FRMTMSTMP and TIMESTAMP_CTR
	 * during the same frame.
	 */
	do {
		/*
		 * This field provides read back of the display
		 * pipe frame time stamp. The time stamp value
		 * is sampled at every start of vertical blank.
		 */
		scan_prev_time = I915_READ_FW(PIPE_FRMTMSTMP(crtc->pipe));

		/*
		 * The TIMESTAMP_CTR register has the current
		 * time stamp value.
		 */
		scan_curr_time = I915_READ_FW(IVB_TIMESTAMP_CTR);

		scan_post_time = I915_READ_FW(PIPE_FRMTMSTMP(crtc->pipe));
	} while (scan_post_time != scan_prev_time);

	scanline = div_u64(mul_u32_u32(scan_curr_time - scan_prev_time,
					clock), 1000 * htotal);
	scanline = min(scanline, vtotal - 1);
	scanline = (scanline + vblank_start) % vtotal;

	return scanline;
}

/* I915_READ_FW, only for fast reads of display block, no need for forcewake etc. */
static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct drm_display_mode *mode;
	struct drm_vblank_crtc *vblank;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	if (!crtc->active)
		return -1;

	vblank = &crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	mode = &vblank->hwmode;

	if (mode->private_flags & I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP)
		return __intel_get_crtc_scanline_from_timestamp(crtc);

	vtotal = mode->crtc_vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	if (IS_GEN(dev_priv, 2))
		position = I915_READ_FW(PIPEDSL(pipe)) & DSL_LINEMASK_GEN2;
	else
		position = I915_READ_FW(PIPEDSL(pipe)) & DSL_LINEMASK_GEN3;

	/*
	 * On HSW, the DSL reg (0x70000) appears to return 0 if we
	 * read it just before the start of vblank.  So try it again
	 * so we don't accidentally end up spanning a vblank frame
	 * increment, causing the pipe_update_end() code to squak at us.
	 *
	 * The nature of this problem means we can't simply check the ISR
	 * bit and return the vblank start value; nor can we use the scanline
	 * debug register in the transcoder as it appears to have the same
	 * problem.  We may need to extend this to include other platforms,
	 * but so far testing only shows the problem on HSW.
	 */
	if (HAS_DDI(dev_priv) && !position) {
		int i, temp;

		for (i = 0; i < 100; i++) {
			udelay(1);
			temp = I915_READ_FW(PIPEDSL(pipe)) & DSL_LINEMASK_GEN3;
			if (temp != position) {
				position = temp;
				break;
			}
		}
	}

	/*
	 * See update_scanline_offset() for the details on the
	 * scanline_offset adjustment.
	 */
	return (position + crtc->scanline_offset) % vtotal;
}

bool i915_get_crtc_scanoutpos(struct drm_device *dev, unsigned int pipe,
			      bool in_vblank_irq, int *vpos, int *hpos,
			      ktime_t *stime, ktime_t *etime,
			      const struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = intel_get_crtc_for_pipe(dev_priv,
								pipe);
	int position;
	int vbl_start, vbl_end, hsync_start, htotal, vtotal;
	unsigned long irqflags;
	bool use_scanline_counter = INTEL_GEN(dev_priv) >= 5 ||
		IS_G4X(dev_priv) || IS_GEN(dev_priv, 2) ||
		mode->private_flags & I915_MODE_FLAG_USE_SCANLINE_COUNTER;

	if (WARN_ON(!mode->crtc_clock)) {
		DRM_DEBUG_DRIVER("trying to get scanoutpos for disabled "
				 "pipe %c\n", pipe_name(pipe));
		return false;
	}

	htotal = mode->crtc_htotal;
	hsync_start = mode->crtc_hsync_start;
	vtotal = mode->crtc_vtotal;
	vbl_start = mode->crtc_vblank_start;
	vbl_end = mode->crtc_vblank_end;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vbl_start = DIV_ROUND_UP(vbl_start, 2);
		vbl_end /= 2;
		vtotal /= 2;
	}

	/*
	 * Lock uncore.lock, as we will do multiple timing critical raw
	 * register reads, potentially with preemption disabled, so the
	 * following code must not block on uncore.lock.
	 */
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	/* preempt_disable_rt() should go right here in PREEMPT_RT patchset. */

	/* Get optional system timestamp before query. */
	if (stime)
		*stime = ktime_get();

	if (use_scanline_counter) {
		/* No obvious pixelcount register. Only query vertical
		 * scanout position from Display scan line register.
		 */
		position = __intel_get_crtc_scanline(intel_crtc);
	} else {
		/* Have access to pixelcount since start of frame.
		 * We can split this into vertical and horizontal
		 * scanout position.
		 */
		position = (I915_READ_FW(PIPEFRAMEPIXEL(pipe)) & PIPE_PIXEL_MASK) >> PIPE_PIXEL_SHIFT;

		/* convert to pixel counts */
		vbl_start *= htotal;
		vbl_end *= htotal;
		vtotal *= htotal;

		/*
		 * In interlaced modes, the pixel counter counts all pixels,
		 * so one field will have htotal more pixels. In order to avoid
		 * the reported position from jumping backwards when the pixel
		 * counter is beyond the length of the shorter field, just
		 * clamp the position the length of the shorter field. This
		 * matches how the scanline counter based position works since
		 * the scanline counter doesn't count the two half lines.
		 */
		if (position >= vtotal)
			position = vtotal - 1;

		/*
		 * Start of vblank interrupt is triggered at start of hsync,
		 * just prior to the first active line of vblank. However we
		 * consider lines to start at the leading edge of horizontal
		 * active. So, should we get here before we've crossed into
		 * the horizontal active of the first line in vblank, we would
		 * not set the DRM_SCANOUTPOS_INVBL flag. In order to fix that,
		 * always add htotal-hsync_start to the current pixel position.
		 */
		position = (position + htotal - hsync_start) % vtotal;
	}

	/* Get optional system timestamp after query. */
	if (etime)
		*etime = ktime_get();

	/* preempt_enable_rt() should go right here in PREEMPT_RT patchset. */

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	/*
	 * While in vblank, position will be negative
	 * counting up towards 0 at vbl_end. And outside
	 * vblank, position will be positive counting
	 * up since vbl_end.
	 */
	if (position >= vbl_start)
		position -= vbl_end;
	else
		position += vtotal - vbl_end;

	if (use_scanline_counter) {
		*vpos = position;
		*hpos = 0;
	} else {
		*vpos = position / htotal;
		*hpos = position - (*vpos * htotal);
	}

	return true;
}

int intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	unsigned long irqflags;
	int position;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	position = __intel_get_crtc_scanline(crtc);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	return position;
}

static void ironlake_rps_change_irq_handler(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 busy_up, busy_down, max_avg, min_avg;
	u8 new_delay;

	spin_lock(&mchdev_lock);

	intel_uncore_write16(uncore,
			     MEMINTRSTS,
			     intel_uncore_read(uncore, MEMINTRSTS));

	new_delay = dev_priv->ips.cur_delay;

	intel_uncore_write16(uncore, MEMINTRSTS, MEMINT_EVAL_CHG);
	busy_up = intel_uncore_read(uncore, RCPREVBSYTUPAVG);
	busy_down = intel_uncore_read(uncore, RCPREVBSYTDNAVG);
	max_avg = intel_uncore_read(uncore, RCBMAXAVG);
	min_avg = intel_uncore_read(uncore, RCBMINAVG);

	/* Handle RCS change request from hw */
	if (busy_up > max_avg) {
		if (dev_priv->ips.cur_delay != dev_priv->ips.max_delay)
			new_delay = dev_priv->ips.cur_delay - 1;
		if (new_delay < dev_priv->ips.max_delay)
			new_delay = dev_priv->ips.max_delay;
	} else if (busy_down < min_avg) {
		if (dev_priv->ips.cur_delay != dev_priv->ips.min_delay)
			new_delay = dev_priv->ips.cur_delay + 1;
		if (new_delay > dev_priv->ips.min_delay)
			new_delay = dev_priv->ips.min_delay;
	}

	if (ironlake_set_drps(dev_priv, new_delay))
		dev_priv->ips.cur_delay = new_delay;

	spin_unlock(&mchdev_lock);

	return;
}

static void vlv_c0_read(struct drm_i915_private *dev_priv,
			struct intel_rps_ei *ei)
{
	ei->ktime = ktime_get_raw();
	ei->render_c0 = I915_READ(VLV_RENDER_C0_COUNT);
	ei->media_c0 = I915_READ(VLV_MEDIA_C0_COUNT);
}

void gen6_rps_reset_ei(struct drm_i915_private *dev_priv)
{
	memset(&dev_priv->gt_pm.rps.ei, 0, sizeof(dev_priv->gt_pm.rps.ei));
}

static u32 vlv_wa_c0_ei(struct drm_i915_private *dev_priv, u32 pm_iir)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	const struct intel_rps_ei *prev = &rps->ei;
	struct intel_rps_ei now;
	u32 events = 0;

	if ((pm_iir & GEN6_PM_RP_UP_EI_EXPIRED) == 0)
		return 0;

	vlv_c0_read(dev_priv, &now);

	if (prev->ktime) {
		u64 time, c0;
		u32 render, media;

		time = ktime_us_delta(now.ktime, prev->ktime);

		time *= dev_priv->czclk_freq;

		/* Workload can be split between render + media,
		 * e.g. SwapBuffers being blitted in X after being rendered in
		 * mesa. To account for this we need to combine both engines
		 * into our activity counter.
		 */
		render = now.render_c0 - prev->render_c0;
		media = now.media_c0 - prev->media_c0;
		c0 = max(render, media);
		c0 *= 1000 * 100 << 8; /* to usecs and scale to threshold% */

		if (c0 > time * rps->power.up_threshold)
			events = GEN6_PM_RP_UP_THRESHOLD;
		else if (c0 < time * rps->power.down_threshold)
			events = GEN6_PM_RP_DOWN_THRESHOLD;
	}

	rps->ei = now;
	return events;
}

static void gen6_pm_rps_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, gt_pm.rps.work);
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	bool client_boost = false;
	int new_delay, adj, min, max;
	u32 pm_iir = 0;

	spin_lock_irq(&dev_priv->irq_lock);
	if (rps->interrupts_enabled) {
		pm_iir = fetch_and_zero(&rps->pm_iir);
		client_boost = atomic_read(&rps->num_waiters);
	}
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	WARN_ON(pm_iir & ~dev_priv->pm_rps_events);
	if ((pm_iir & dev_priv->pm_rps_events) == 0 && !client_boost)
		goto out;

	mutex_lock(&rps->lock);

	pm_iir |= vlv_wa_c0_ei(dev_priv, pm_iir);

	adj = rps->last_adj;
	new_delay = rps->cur_freq;
	min = rps->min_freq_softlimit;
	max = rps->max_freq_softlimit;
	if (client_boost)
		max = rps->max_freq;
	if (client_boost && new_delay < rps->boost_freq) {
		new_delay = rps->boost_freq;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_UP_THRESHOLD) {
		if (adj > 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv) ? 2 : 1;

		if (new_delay >= rps->max_freq_softlimit)
			adj = 0;
	} else if (client_boost) {
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_TIMEOUT) {
		if (rps->cur_freq > rps->efficient_freq)
			new_delay = rps->efficient_freq;
		else if (rps->cur_freq > rps->min_freq_softlimit)
			new_delay = rps->min_freq_softlimit;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_THRESHOLD) {
		if (adj < 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv) ? -2 : -1;

		if (new_delay <= rps->min_freq_softlimit)
			adj = 0;
	} else { /* unknown event */
		adj = 0;
	}

	rps->last_adj = adj;

	/*
	 * Limit deboosting and boosting to keep ourselves at the extremes
	 * when in the respective power modes (i.e. slowly decrease frequencies
	 * while in the HIGH_POWER zone and slowly increase frequencies while
	 * in the LOW_POWER zone). On idle, we will hit the timeout and drop
	 * to the next level quickly, and conversely if busy we expect to
	 * hit a waitboost and rapidly switch into max power.
	 */
	if ((adj < 0 && rps->power.mode == HIGH_POWER) ||
	    (adj > 0 && rps->power.mode == LOW_POWER))
		rps->last_adj = 0;

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	new_delay += adj;
	new_delay = clamp_t(int, new_delay, min, max);

	if (intel_set_rps(dev_priv, new_delay)) {
		DRM_DEBUG_DRIVER("Failed to set new GPU frequency\n");
		rps->last_adj = 0;
	}

	mutex_unlock(&rps->lock);

out:
	/* Make sure not to corrupt PMIMR state used by ringbuffer on GEN6 */
	spin_lock_irq(&dev_priv->irq_lock);
	if (rps->interrupts_enabled)
		gen6_unmask_pm_irq(dev_priv, dev_priv->pm_rps_events);
	spin_unlock_irq(&dev_priv->irq_lock);
}


/**
 * ivybridge_parity_work - Workqueue called when a parity error interrupt
 * occurred.
 * @work: workqueue struct
 *
 * Doesn't actually do anything except notify userspace. As a consequence of
 * this event, userspace should try to remap the bad rows since statistically
 * it is likely the same row is more likely to go bad again.
 */
static void ivybridge_parity_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), l3_parity.error_work);
	u32 error_status, row, bank, subbank;
	char *parity_event[6];
	u32 misccpctl;
	u8 slice = 0;

	/* We must turn off DOP level clock gating to access the L3 registers.
	 * In order to prevent a get/put style interface, acquire struct mutex
	 * any time we access those registers.
	 */
	mutex_lock(&dev_priv->drm.struct_mutex);

	/* If we've screwed up tracking, just let the interrupt fire again */
	if (WARN_ON(!dev_priv->l3_parity.which_slice))
		goto out;

	misccpctl = I915_READ(GEN7_MISCCPCTL);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);
	POSTING_READ(GEN7_MISCCPCTL);

	while ((slice = ffs(dev_priv->l3_parity.which_slice)) != 0) {
		i915_reg_t reg;

		slice--;
		if (WARN_ON_ONCE(slice >= NUM_L3_SLICES(dev_priv)))
			break;

		dev_priv->l3_parity.which_slice &= ~(1<<slice);

		reg = GEN7_L3CDERRST1(slice);

		error_status = I915_READ(reg);
		row = GEN7_PARITY_ERROR_ROW(error_status);
		bank = GEN7_PARITY_ERROR_BANK(error_status);
		subbank = GEN7_PARITY_ERROR_SUBBANK(error_status);

		I915_WRITE(reg, GEN7_PARITY_ERROR_VALID | GEN7_L3CDERRST1_ENABLE);
		POSTING_READ(reg);

		parity_event[0] = I915_L3_PARITY_UEVENT "=1";
		parity_event[1] = kasprintf(GFP_KERNEL, "ROW=%d", row);
		parity_event[2] = kasprintf(GFP_KERNEL, "BANK=%d", bank);
		parity_event[3] = kasprintf(GFP_KERNEL, "SUBBANK=%d", subbank);
		parity_event[4] = kasprintf(GFP_KERNEL, "SLICE=%d", slice);
		parity_event[5] = NULL;

		kobject_uevent_env(&dev_priv->drm.primary->kdev->kobj,
				   KOBJ_CHANGE, parity_event);

		DRM_DEBUG("Parity error: Slice = %d, Row = %d, Bank = %d, Sub bank = %d.\n",
			  slice, row, bank, subbank);

		kfree(parity_event[4]);
		kfree(parity_event[3]);
		kfree(parity_event[2]);
		kfree(parity_event[1]);
	}

	I915_WRITE(GEN7_MISCCPCTL, misccpctl);

out:
	WARN_ON(dev_priv->l3_parity.which_slice);
	spin_lock_irq(&dev_priv->irq_lock);
	gen5_enable_gt_irq(dev_priv, GT_PARITY_ERROR(dev_priv));
	spin_unlock_irq(&dev_priv->irq_lock);

	mutex_unlock(&dev_priv->drm.struct_mutex);
}

static void ivybridge_parity_error_irq_handler(struct drm_i915_private *dev_priv,
					       u32 iir)
{
	if (!HAS_L3_DPF(dev_priv))
		return;

	spin_lock(&dev_priv->irq_lock);
	gen5_disable_gt_irq(dev_priv, GT_PARITY_ERROR(dev_priv));
	spin_unlock(&dev_priv->irq_lock);

	iir &= GT_PARITY_ERROR(dev_priv);
	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1)
		dev_priv->l3_parity.which_slice |= 1 << 1;

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT)
		dev_priv->l3_parity.which_slice |= 1 << 0;

	queue_work(dev_priv->wq, &dev_priv->l3_parity.error_work);
}

static void ilk_gt_irq_handler(struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[RCS0]);
	if (gt_iir & ILK_BSD_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[VCS0]);
}

static void snb_gt_irq_handler(struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[RCS0]);
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[VCS0]);
	if (gt_iir & GT_BLT_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[BCS0]);

	if (gt_iir & (GT_BLT_CS_ERROR_INTERRUPT |
		      GT_BSD_CS_ERROR_INTERRUPT |
		      GT_RENDER_CS_MASTER_ERROR_INTERRUPT))
		DRM_DEBUG("Command parser error, gt_iir 0x%08x\n", gt_iir);

	if (gt_iir & GT_PARITY_ERROR(dev_priv))
		ivybridge_parity_error_irq_handler(dev_priv, gt_iir);
}

static void
gen8_cs_irq_handler(struct intel_engine_cs *engine, u32 iir)
{
	bool tasklet = false;

	if (iir & GT_CONTEXT_SWITCH_INTERRUPT)
		tasklet = true;

	if (iir & GT_RENDER_USER_INTERRUPT) {
		intel_engine_breadcrumbs_irq(engine);
		tasklet |= intel_engine_needs_breadcrumb_tasklet(engine);
	}

	if (tasklet)
		tasklet_hi_schedule(&engine->execlists.tasklet);
}

static void gen8_gt_irq_ack(struct drm_i915_private *i915,
			    u32 master_ctl, u32 gt_iir[4])
{
	void __iomem * const regs = i915->uncore.regs;

#define GEN8_GT_IRQS (GEN8_GT_RCS_IRQ | \
		      GEN8_GT_BCS_IRQ | \
		      GEN8_GT_VCS0_IRQ | \
		      GEN8_GT_VCS1_IRQ | \
		      GEN8_GT_VECS_IRQ | \
		      GEN8_GT_PM_IRQ | \
		      GEN8_GT_GUC_IRQ)

	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		gt_iir[0] = raw_reg_read(regs, GEN8_GT_IIR(0));
		if (likely(gt_iir[0]))
			raw_reg_write(regs, GEN8_GT_IIR(0), gt_iir[0]);
	}

	if (master_ctl & (GEN8_GT_VCS0_IRQ | GEN8_GT_VCS1_IRQ)) {
		gt_iir[1] = raw_reg_read(regs, GEN8_GT_IIR(1));
		if (likely(gt_iir[1]))
			raw_reg_write(regs, GEN8_GT_IIR(1), gt_iir[1]);
	}

	if (master_ctl & (GEN8_GT_PM_IRQ | GEN8_GT_GUC_IRQ)) {
		gt_iir[2] = raw_reg_read(regs, GEN8_GT_IIR(2));
		if (likely(gt_iir[2]))
			raw_reg_write(regs, GEN8_GT_IIR(2), gt_iir[2]);
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		gt_iir[3] = raw_reg_read(regs, GEN8_GT_IIR(3));
		if (likely(gt_iir[3]))
			raw_reg_write(regs, GEN8_GT_IIR(3), gt_iir[3]);
	}
}

static void gen8_gt_irq_handler(struct drm_i915_private *i915,
				u32 master_ctl, u32 gt_iir[4])
{
	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		gen8_cs_irq_handler(i915->engine[RCS0],
				    gt_iir[0] >> GEN8_RCS_IRQ_SHIFT);
		gen8_cs_irq_handler(i915->engine[BCS0],
				    gt_iir[0] >> GEN8_BCS_IRQ_SHIFT);
	}

	if (master_ctl & (GEN8_GT_VCS0_IRQ | GEN8_GT_VCS1_IRQ)) {
		gen8_cs_irq_handler(i915->engine[VCS0],
				    gt_iir[1] >> GEN8_VCS0_IRQ_SHIFT);
		gen8_cs_irq_handler(i915->engine[VCS1],
				    gt_iir[1] >> GEN8_VCS1_IRQ_SHIFT);
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		gen8_cs_irq_handler(i915->engine[VECS0],
				    gt_iir[3] >> GEN8_VECS_IRQ_SHIFT);
	}

	if (master_ctl & (GEN8_GT_PM_IRQ | GEN8_GT_GUC_IRQ)) {
		gen6_rps_irq_handler(i915, gt_iir[2]);
		gen9_guc_irq_handler(i915, gt_iir[2]);
	}
}

static bool gen11_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_C:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(PORT_TC1);
	case HPD_PORT_D:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(PORT_TC2);
	case HPD_PORT_E:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(PORT_TC3);
	case HPD_PORT_F:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(PORT_TC4);
	default:
		return false;
	}
}

static bool bxt_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool icp_ddi_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & ICP_DDIA_HPD_LONG_DETECT;
	case HPD_PORT_B:
		return val & ICP_DDIB_HPD_LONG_DETECT;
	default:
		return false;
	}
}

static bool icp_tc_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_C:
		return val & ICP_TC_HPD_LONG_DETECT(PORT_TC1);
	case HPD_PORT_D:
		return val & ICP_TC_HPD_LONG_DETECT(PORT_TC2);
	case HPD_PORT_E:
		return val & ICP_TC_HPD_LONG_DETECT(PORT_TC3);
	case HPD_PORT_F:
		return val & ICP_TC_HPD_LONG_DETECT(PORT_TC4);
	default:
		return false;
	}
}

static bool spt_port_hotplug2_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_E:
		return val & PORTE_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool spt_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool ilk_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & DIGITAL_PORTA_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool pch_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool i9xx_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_INT_LONG_PULSE;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_INT_LONG_PULSE;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_INT_LONG_PULSE;
	default:
		return false;
	}
}

/*
 * Get a bit mask of pins that have triggered, and which ones may be long.
 * This can be called multiple times with the same masks to accumulate
 * hotplug detection results from several registers.
 *
 * Note that the caller is expected to zero out the masks initially.
 */
static void intel_get_hpd_pins(struct drm_i915_private *dev_priv,
			       u32 *pin_mask, u32 *long_mask,
			       u32 hotplug_trigger, u32 dig_hotplug_reg,
			       const u32 hpd[HPD_NUM_PINS],
			       bool long_pulse_detect(enum hpd_pin pin, u32 val))
{
	enum hpd_pin pin;

	for_each_hpd_pin(pin) {
		if ((hpd[pin] & hotplug_trigger) == 0)
			continue;

		*pin_mask |= BIT(pin);

		if (long_pulse_detect(pin, dig_hotplug_reg))
			*long_mask |= BIT(pin);
	}

	DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x, dig 0x%08x, pins 0x%08x, long 0x%08x\n",
			 hotplug_trigger, dig_hotplug_reg, *pin_mask, *long_mask);

}

static void gmbus_irq_handler(struct drm_i915_private *dev_priv)
{
	wake_up_all(&dev_priv->gmbus_wait_queue);
}

static void dp_aux_irq_handler(struct drm_i915_private *dev_priv)
{
	wake_up_all(&dev_priv->gmbus_wait_queue);
}

#if defined(CONFIG_DEBUG_FS)
static void display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe,
					 u32 crc0, u32 crc1,
					 u32 crc2, u32 crc3,
					 u32 crc4)
{
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	u32 crcs[5] = { crc0, crc1, crc2, crc3, crc4 };

	trace_intel_pipe_crc(crtc, crcs);

	spin_lock(&pipe_crc->lock);
	/*
	 * For some not yet identified reason, the first CRC is
	 * bonkers. So let's just wait for the next vblank and read
	 * out the buggy result.
	 *
	 * On GEN8+ sometimes the second CRC is bonkers as well, so
	 * don't trust that one either.
	 */
	if (pipe_crc->skipped <= 0 ||
	    (INTEL_GEN(dev_priv) >= 8 && pipe_crc->skipped == 1)) {
		pipe_crc->skipped++;
		spin_unlock(&pipe_crc->lock);
		return;
	}
	spin_unlock(&pipe_crc->lock);

	drm_crtc_add_crc_entry(&crtc->base, true,
				drm_crtc_accurate_vblank_count(&crtc->base),
				crcs);
}
#else
static inline void
display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
			     enum pipe pipe,
			     u32 crc0, u32 crc1,
			     u32 crc2, u32 crc3,
			     u32 crc4) {}
#endif


static void hsw_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     I915_READ(PIPE_CRC_RES_1_IVB(pipe)),
				     0, 0, 0, 0);
}

static void ivb_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     I915_READ(PIPE_CRC_RES_1_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_2_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_3_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_4_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_5_IVB(pipe)));
}

static void i9xx_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 res1, res2;

	if (INTEL_GEN(dev_priv) >= 3)
		res1 = I915_READ(PIPE_CRC_RES_RES1_I915(pipe));
	else
		res1 = 0;

	if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		res2 = I915_READ(PIPE_CRC_RES_RES2_G4X(pipe));
	else
		res2 = 0;

	display_pipe_crc_irq_handler(dev_priv, pipe,
				     I915_READ(PIPE_CRC_RES_RED(pipe)),
				     I915_READ(PIPE_CRC_RES_GREEN(pipe)),
				     I915_READ(PIPE_CRC_RES_BLUE(pipe)),
				     res1, res2);
}

/* The RPS events need forcewake, so we add them to a work queue and mask their
 * IMR bits until the work is done. Other interrupts can be processed without
 * the work queue. */
static void gen11_rps_irq_handler(struct drm_i915_private *i915, u32 pm_iir)
{
	struct intel_rps *rps = &i915->gt_pm.rps;
	const u32 events = i915->pm_rps_events & pm_iir;

	lockdep_assert_held(&i915->irq_lock);

	if (unlikely(!events))
		return;

	gen6_mask_pm_irq(i915, events);

	if (!rps->interrupts_enabled)
		return;

	rps->pm_iir |= events;
	schedule_work(&rps->work);
}

static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;

	if (pm_iir & dev_priv->pm_rps_events) {
		spin_lock(&dev_priv->irq_lock);
		gen6_mask_pm_irq(dev_priv, pm_iir & dev_priv->pm_rps_events);
		if (rps->interrupts_enabled) {
			rps->pm_iir |= pm_iir & dev_priv->pm_rps_events;
			schedule_work(&rps->work);
		}
		spin_unlock(&dev_priv->irq_lock);
	}

	if (INTEL_GEN(dev_priv) >= 8)
		return;

	if (pm_iir & PM_VEBOX_USER_INTERRUPT)
		intel_engine_breadcrumbs_irq(dev_priv->engine[VECS0]);

	if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT)
		DRM_DEBUG("Command parser error, pm_iir 0x%08x\n", pm_iir);
}

static void gen9_guc_irq_handler(struct drm_i915_private *dev_priv, u32 gt_iir)
{
	if (gt_iir & GEN9_GUC_TO_HOST_INT_EVENT)
		intel_guc_to_host_event_handler(&dev_priv->guc);
}

static void gen11_guc_irq_handler(struct drm_i915_private *i915, u16 iir)
{
	if (iir & GEN11_GUC_INTR_GUC2HOST)
		intel_guc_to_host_event_handler(&i915->guc);
}

static void i9xx_pipestat_irq_reset(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		I915_WRITE(PIPESTAT(pipe),
			   PIPESTAT_INT_STATUS_MASK |
			   PIPE_FIFO_UNDERRUN_STATUS);

		dev_priv->pipestat_irq_mask[pipe] = 0;
	}
}

static void i9xx_pipestat_irq_ack(struct drm_i915_private *dev_priv,
				  u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	int pipe;

	spin_lock(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled) {
		spin_unlock(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe(dev_priv, pipe) {
		i915_reg_t reg;
		u32 status_mask, enable_mask, iir_bit = 0;

		/*
		 * PIPESTAT bits get signalled even when the interrupt is
		 * disabled with the mask bits, and some of the status bits do
		 * not generate interrupts at all (like the underrun bit). Hence
		 * we need to be careful that we only handle what we want to
		 * handle.
		 */

		/* fifo underruns are filterered in the underrun handler. */
		status_mask = PIPE_FIFO_UNDERRUN_STATUS;

		switch (pipe) {
		case PIPE_A:
			iir_bit = I915_DISPLAY_PIPE_A_EVENT_INTERRUPT;
			break;
		case PIPE_B:
			iir_bit = I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
			break;
		case PIPE_C:
			iir_bit = I915_DISPLAY_PIPE_C_EVENT_INTERRUPT;
			break;
		}
		if (iir & iir_bit)
			status_mask |= dev_priv->pipestat_irq_mask[pipe];

		if (!status_mask)
			continue;

		reg = PIPESTAT(pipe);
		pipe_stats[pipe] = I915_READ(reg) & status_mask;
		enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

		/*
		 * Clear the PIPE*STAT regs before the IIR
		 *
		 * Toggle the enable bits to make sure we get an
		 * edge in the ISR pipe event bit if we don't clear
		 * all the enabled status bits. Otherwise the edge
		 * triggered IIR on i965/g4x wouldn't notice that
		 * an interrupt is still pending.
		 */
		if (pipe_stats[pipe]) {
			I915_WRITE(reg, pipe_stats[pipe]);
			I915_WRITE(reg, enable_mask);
		}
	}
	spin_unlock(&dev_priv->irq_lock);
}

static void i8xx_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u16 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}
}

static void i915_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(dev_priv);
}

static void i965_pipestat_irq_handler(struct drm_i915_private *dev_priv,
				      u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	bool blc_event = false;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
			blc_event = true;

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (blc_event || (iir & I915_ASLE_INTERRUPT))
		intel_opregion_asle_intr(dev_priv);

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		gmbus_irq_handler(dev_priv);
}

static void valleyview_pipestat_irq_handler(struct drm_i915_private *dev_priv,
					    u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS)
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		gmbus_irq_handler(dev_priv);
}

static u32 i9xx_hpd_irq_ack(struct drm_i915_private *dev_priv)
{
	u32 hotplug_status = 0, hotplug_status_mask;
	int i;

	if (IS_G4X(dev_priv) ||
	    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		hotplug_status_mask = HOTPLUG_INT_STATUS_G4X |
			DP_AUX_CHANNEL_MASK_INT_STATUS_G4X;
	else
		hotplug_status_mask = HOTPLUG_INT_STATUS_I915;

	/*
	 * We absolutely have to clear all the pending interrupt
	 * bits in PORT_HOTPLUG_STAT. Otherwise the ISR port
	 * interrupt bit won't have an edge, and the i965/g4x
	 * edge triggered IIR will not notice that an interrupt
	 * is still pending. We can't use PORT_HOTPLUG_EN to
	 * guarantee the edge as the act of toggling the enable
	 * bits can itself generate a new hotplug interrupt :(
	 */
	for (i = 0; i < 10; i++) {
		u32 tmp = I915_READ(PORT_HOTPLUG_STAT) & hotplug_status_mask;

		if (tmp == 0)
			return hotplug_status;

		hotplug_status |= tmp;
		I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
	}

	WARN_ONCE(1,
		  "PORT_HOTPLUG_STAT did not clear (0x%08x)\n",
		  I915_READ(PORT_HOTPLUG_STAT));

	return hotplug_status;
}

static void i9xx_hpd_irq_handler(struct drm_i915_private *dev_priv,
				 u32 hotplug_status)
{
	u32 pin_mask = 0, long_mask = 0;

	if (IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
	    IS_CHERRYVIEW(dev_priv)) {
		u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_G4X;

		if (hotplug_trigger) {
			intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
					   hotplug_trigger, hotplug_trigger,
					   hpd_status_g4x,
					   i9xx_port_hotplug_long_detect);

			intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
		}

		if (hotplug_status & DP_AUX_CHANNEL_MASK_INT_STATUS_G4X)
			dp_aux_irq_handler(dev_priv);
	} else {
		u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

		if (hotplug_trigger) {
			intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
					   hotplug_trigger, hotplug_trigger,
					   hpd_status_i915,
					   i9xx_port_hotplug_long_detect);
			intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
		}
	}
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 iir, gt_iir, pm_iir;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 ier = 0;

		gt_iir = I915_READ(GTIIR);
		pm_iir = I915_READ(GEN6_PMIIR);
		iir = I915_READ(VLV_IIR);

		if (gt_iir == 0 && pm_iir == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      (((GT_IIR & GT_IER) || (GEN6_PMIIR & GEN6_PMIER)) &&
		 *       (VLV_MASTER_IER & MASTER_INTERRUPT_ENABLE)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear MASTER_INTERRUPT_ENABLE and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR, GT_IIR, GEN6_PMIIR
		 * bits this time around.
		 */
		I915_WRITE(VLV_MASTER_IER, 0);
		ier = I915_READ(VLV_IER);
		I915_WRITE(VLV_IER, 0);

		if (gt_iir)
			I915_WRITE(GTIIR, gt_iir);
		if (pm_iir)
			I915_WRITE(GEN6_PMIIR, pm_iir);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT))
			intel_lpe_audio_irq_handler(dev_priv);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			I915_WRITE(VLV_IIR, iir);

		I915_WRITE(VLV_IER, ier);
		I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

		if (gt_iir)
			snb_gt_irq_handler(dev_priv, gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(dev_priv, pm_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static irqreturn_t cherryview_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 master_ctl, iir;
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
		u32 gt_iir[4];
		u32 ier = 0;

		master_ctl = I915_READ(GEN8_MASTER_IRQ) & ~GEN8_MASTER_IRQ_CONTROL;
		iir = I915_READ(VLV_IIR);

		if (master_ctl == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		/*
		 * Theory on interrupt generation, based on empirical evidence:
		 *
		 * x = ((VLV_IIR & VLV_IER) ||
		 *      ((GEN8_MASTER_IRQ & ~GEN8_MASTER_IRQ_CONTROL) &&
		 *       (GEN8_MASTER_IRQ & GEN8_MASTER_IRQ_CONTROL)));
		 *
		 * A CPU interrupt will only be raised when 'x' has a 0->1 edge.
		 * Hence we clear GEN8_MASTER_IRQ_CONTROL and VLV_IER to
		 * guarantee the CPU interrupt will be raised again even if we
		 * don't end up clearing all the VLV_IIR and GEN8_MASTER_IRQ_CONTROL
		 * bits this time around.
		 */
		I915_WRITE(GEN8_MASTER_IRQ, 0);
		ier = I915_READ(VLV_IER);
		I915_WRITE(VLV_IER, 0);

		gen8_gt_irq_ack(dev_priv, master_ctl, gt_iir);

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & (I915_LPE_PIPE_A_INTERRUPT |
			   I915_LPE_PIPE_B_INTERRUPT |
			   I915_LPE_PIPE_C_INTERRUPT))
			intel_lpe_audio_irq_handler(dev_priv);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			I915_WRITE(VLV_IIR, iir);

		I915_WRITE(VLV_IER, ier);
		I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);

		gen8_gt_irq_handler(dev_priv, master_ctl, gt_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void ibx_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger,
				const u32 hpd[HPD_NUM_PINS])
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	/*
	 * Somehow the PCH doesn't seem to really ack the interrupt to the CPU
	 * unless we touch the hotplug register, even if hotplug_trigger is
	 * zero. Not acking leads to "The master control interrupt lied (SDE)!"
	 * errors.
	 */
	dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
	if (!hotplug_trigger) {
		u32 mask = PORTA_HOTPLUG_STATUS_MASK |
			PORTD_HOTPLUG_STATUS_MASK |
			PORTC_HOTPLUG_STATUS_MASK |
			PORTB_HOTPLUG_STATUS_MASK;
		dig_hotplug_reg &= ~mask;
	}

	I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);
	if (!hotplug_trigger)
		return;

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask, hotplug_trigger,
			   dig_hotplug_reg, hpd,
			   pch_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

static void ibx_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger, hpd_ibx);

	if (pch_iir & SDE_AUDIO_POWER_MASK) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK) >>
			       SDE_AUDIO_POWER_SHIFT);
		DRM_DEBUG_DRIVER("PCH audio power change on port %d\n",
				 port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK)
		dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS)
		gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_HDCP_MASK)
		DRM_DEBUG_DRIVER("PCH HDCP audio interrupt\n");

	if (pch_iir & SDE_AUDIO_TRANS_MASK)
		DRM_DEBUG_DRIVER("PCH transcoder audio interrupt\n");

	if (pch_iir & SDE_POISON)
		DRM_ERROR("PCH poison interrupt\n");

	if (pch_iir & SDE_FDI_MASK)
		for_each_pipe(dev_priv, pipe)
			DRM_DEBUG_DRIVER("  pipe %c FDI IIR: 0x%08x\n",
					 pipe_name(pipe),
					 I915_READ(FDI_RX_IIR(pipe)));

	if (pch_iir & (SDE_TRANSB_CRC_DONE | SDE_TRANSA_CRC_DONE))
		DRM_DEBUG_DRIVER("PCH transcoder CRC done interrupt\n");

	if (pch_iir & (SDE_TRANSB_CRC_ERR | SDE_TRANSA_CRC_ERR))
		DRM_DEBUG_DRIVER("PCH transcoder CRC error interrupt\n");

	if (pch_iir & SDE_TRANSA_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_A);

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_B);
}

static void ivb_err_int_handler(struct drm_i915_private *dev_priv)
{
	u32 err_int = I915_READ(GEN7_ERR_INT);
	enum pipe pipe;

	if (err_int & ERR_INT_POISON)
		DRM_ERROR("Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (err_int & ERR_INT_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (err_int & ERR_INT_PIPE_CRC_DONE(pipe)) {
			if (IS_IVYBRIDGE(dev_priv))
				ivb_pipe_crc_irq_handler(dev_priv, pipe);
			else
				hsw_pipe_crc_irq_handler(dev_priv, pipe);
		}
	}

	I915_WRITE(GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct drm_i915_private *dev_priv)
{
	u32 serr_int = I915_READ(SERR_INT);
	enum pipe pipe;

	if (serr_int & SERR_INT_POISON)
		DRM_ERROR("PCH poison interrupt\n");

	for_each_pipe(dev_priv, pipe)
		if (serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pipe))
			intel_pch_fifo_underrun_irq_handler(dev_priv, pipe);

	I915_WRITE(SERR_INT, serr_int);
}

static void cpt_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger, hpd_cpt);

	if (pch_iir & SDE_AUDIO_POWER_MASK_CPT) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK_CPT) >>
			       SDE_AUDIO_POWER_SHIFT_CPT);
		DRM_DEBUG_DRIVER("PCH audio power change on port %c\n",
				 port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK_CPT)
		dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS_CPT)
		gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_CP_REQ_CPT)
		DRM_DEBUG_DRIVER("Audio CP request interrupt\n");

	if (pch_iir & SDE_AUDIO_CP_CHG_CPT)
		DRM_DEBUG_DRIVER("Audio CP change interrupt\n");

	if (pch_iir & SDE_FDI_MASK_CPT)
		for_each_pipe(dev_priv, pipe)
			DRM_DEBUG_DRIVER("  pipe %c FDI IIR: 0x%08x\n",
					 pipe_name(pipe),
					 I915_READ(FDI_RX_IIR(pipe)));

	if (pch_iir & SDE_ERROR_CPT)
		cpt_serr_int_handler(dev_priv);
}

static void icp_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir,
			    const u32 *pins)
{
	u32 ddi_hotplug_trigger = pch_iir & SDE_DDI_MASK_ICP;
	u32 tc_hotplug_trigger = pch_iir & SDE_TC_MASK_ICP;
	u32 pin_mask = 0, long_mask = 0;

	if (ddi_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(SHOTPLUG_CTL_DDI);
		I915_WRITE(SHOTPLUG_CTL_DDI, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   ddi_hotplug_trigger,
				   dig_hotplug_reg, pins,
				   icp_ddi_port_hotplug_long_detect);
	}

	if (tc_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(SHOTPLUG_CTL_TC);
		I915_WRITE(SHOTPLUG_CTL_TC, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   tc_hotplug_trigger,
				   dig_hotplug_reg, pins,
				   icp_tc_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_ICP)
		gmbus_irq_handler(dev_priv);
}

static void spt_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_SPT &
		~SDE_PORTE_HOTPLUG_SPT;
	u32 hotplug2_trigger = pch_iir & SDE_PORTE_HOTPLUG_SPT;
	u32 pin_mask = 0, long_mask = 0;

	if (hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
		I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug_trigger, dig_hotplug_reg, hpd_spt,
				   spt_port_hotplug_long_detect);
	}

	if (hotplug2_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG2);
		I915_WRITE(PCH_PORT_HOTPLUG2, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug2_trigger, dig_hotplug_reg, hpd_spt,
				   spt_port_hotplug2_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_CPT)
		gmbus_irq_handler(dev_priv);
}

static void ilk_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger,
				const u32 hpd[HPD_NUM_PINS])
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = I915_READ(DIGITAL_PORT_HOTPLUG_CNTRL);
	I915_WRITE(DIGITAL_PORT_HOTPLUG_CNTRL, dig_hotplug_reg);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask, hotplug_trigger,
			   dig_hotplug_reg, hpd,
			   ilk_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

static void ilk_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger, hpd_ilk);

	if (de_iir & DE_AUX_CHANNEL_A)
		dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(dev_priv);

	if (de_iir & DE_POISON)
		DRM_ERROR("Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK(pipe))
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);
	}

	/* check event from PCH */
	if (de_iir & DE_PCH_EVENT) {
		u32 pch_iir = I915_READ(SDEIIR);

		if (HAS_PCH_CPT(dev_priv))
			cpt_irq_handler(dev_priv, pch_iir);
		else
			ibx_irq_handler(dev_priv, pch_iir);

		/* should clear PCH hotplug event before clear CPU irq */
		I915_WRITE(SDEIIR, pch_iir);
	}

	if (IS_GEN(dev_priv, 5) && de_iir & DE_PCU_EVENT)
		ironlake_rps_change_irq_handler(dev_priv);
}

static void ivb_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG_IVB;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger, hpd_ivb);

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(dev_priv);

	if (de_iir & DE_EDP_PSR_INT_HSW) {
		u32 psr_iir = I915_READ(EDP_PSR_IIR);

		intel_psr_irq_handler(dev_priv, psr_iir);
		I915_WRITE(EDP_PSR_IIR, psr_iir);
	}

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev_priv);

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & (DE_PIPE_VBLANK_IVB(pipe)))
			drm_handle_vblank(&dev_priv->drm, pipe);
	}

	/* check event from PCH */
	if (!HAS_PCH_NOP(dev_priv) && (de_iir & DE_PCH_EVENT_IVB)) {
		u32 pch_iir = I915_READ(SDEIIR);

		cpt_irq_handler(dev_priv, pch_iir);

		/* clear PCH hotplug event before clear CPU irq */
		I915_WRITE(SDEIIR, pch_iir);
	}
}

/*
 * To handle irqs with the minimum potential races with fresh interrupts, we:
 * 1 - Disable Master Interrupt Control.
 * 2 - Find the source(s) of the interrupt.
 * 3 - Clear the Interrupt Identity bits (IIR).
 * 4 - Process the interrupt(s) that had bits set in the IIRs.
 * 5 - Re-enable Master Interrupt Control.
 */
static irqreturn_t ironlake_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(dev_priv)) {
		sde_ier = I915_READ(SDEIER);
		I915_WRITE(SDEIER, 0);
	}

	/* Find, clear, then process each source of interrupt */

	gt_iir = I915_READ(GTIIR);
	if (gt_iir) {
		I915_WRITE(GTIIR, gt_iir);
		ret = IRQ_HANDLED;
		if (INTEL_GEN(dev_priv) >= 6)
			snb_gt_irq_handler(dev_priv, gt_iir);
		else
			ilk_gt_irq_handler(dev_priv, gt_iir);
	}

	de_iir = I915_READ(DEIIR);
	if (de_iir) {
		I915_WRITE(DEIIR, de_iir);
		ret = IRQ_HANDLED;
		if (INTEL_GEN(dev_priv) >= 7)
			ivb_display_irq_handler(dev_priv, de_iir);
		else
			ilk_display_irq_handler(dev_priv, de_iir);
	}

	if (INTEL_GEN(dev_priv) >= 6) {
		u32 pm_iir = I915_READ(GEN6_PMIIR);
		if (pm_iir) {
			I915_WRITE(GEN6_PMIIR, pm_iir);
			ret = IRQ_HANDLED;
			gen6_rps_irq_handler(dev_priv, pm_iir);
		}
	}

	I915_WRITE(DEIER, de_ier);
	if (!HAS_PCH_NOP(dev_priv))
		I915_WRITE(SDEIER, sde_ier);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void bxt_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger,
				const u32 hpd[HPD_NUM_PINS])
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
	I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask, hotplug_trigger,
			   dig_hotplug_reg, hpd,
			   bxt_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

static void gen11_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 iir)
{
	u32 pin_mask = 0, long_mask = 0;
	u32 trigger_tc = iir & GEN11_DE_TC_HOTPLUG_MASK;
	u32 trigger_tbt = iir & GEN11_DE_TBT_HOTPLUG_MASK;

	if (trigger_tc) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(GEN11_TC_HOTPLUG_CTL);
		I915_WRITE(GEN11_TC_HOTPLUG_CTL, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask, trigger_tc,
				   dig_hotplug_reg, hpd_gen11,
				   gen11_port_hotplug_long_detect);
	}

	if (trigger_tbt) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(GEN11_TBT_HOTPLUG_CTL);
		I915_WRITE(GEN11_TBT_HOTPLUG_CTL, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask, trigger_tbt,
				   dig_hotplug_reg, hpd_gen11,
				   gen11_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
	else
		DRM_ERROR("Unexpected DE HPD interrupt 0x%08x\n", iir);
}

static u32 gen8_de_port_aux_mask(struct drm_i915_private *dev_priv)
{
	u32 mask = GEN8_AUX_CHANNEL_A;

	if (INTEL_GEN(dev_priv) >= 9)
		mask |= GEN9_AUX_CHANNEL_B |
			GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;

	if (IS_CNL_WITH_PORT_F(dev_priv))
		mask |= CNL_AUX_CHANNEL_F;

	if (INTEL_GEN(dev_priv) >= 11)
		mask |= ICL_AUX_CHANNEL_E |
			CNL_AUX_CHANNEL_F;

	return mask;
}

static irqreturn_t
gen8_de_irq_handler(struct drm_i915_private *dev_priv, u32 master_ctl)
{
	irqreturn_t ret = IRQ_NONE;
	u32 iir;
	enum pipe pipe;

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		iir = I915_READ(GEN8_DE_MISC_IIR);
		if (iir) {
			bool found = false;

			I915_WRITE(GEN8_DE_MISC_IIR, iir);
			ret = IRQ_HANDLED;

			if (iir & GEN8_DE_MISC_GSE) {
				intel_opregion_asle_intr(dev_priv);
				found = true;
			}

			if (iir & GEN8_DE_EDP_PSR) {
				u32 psr_iir = I915_READ(EDP_PSR_IIR);

				intel_psr_irq_handler(dev_priv, psr_iir);
				I915_WRITE(EDP_PSR_IIR, psr_iir);
				found = true;
			}

			if (!found)
				DRM_ERROR("Unexpected DE Misc interrupt\n");
		}
		else
			DRM_ERROR("The master control interrupt lied (DE MISC)!\n");
	}

	if (INTEL_GEN(dev_priv) >= 11 && (master_ctl & GEN11_DE_HPD_IRQ)) {
		iir = I915_READ(GEN11_DE_HPD_IIR);
		if (iir) {
			I915_WRITE(GEN11_DE_HPD_IIR, iir);
			ret = IRQ_HANDLED;
			gen11_hpd_irq_handler(dev_priv, iir);
		} else {
			DRM_ERROR("The master control interrupt lied, (DE HPD)!\n");
		}
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = I915_READ(GEN8_DE_PORT_IIR);
		if (iir) {
			u32 tmp_mask;
			bool found = false;

			I915_WRITE(GEN8_DE_PORT_IIR, iir);
			ret = IRQ_HANDLED;

			if (iir & gen8_de_port_aux_mask(dev_priv)) {
				dp_aux_irq_handler(dev_priv);
				found = true;
			}

			if (IS_GEN9_LP(dev_priv)) {
				tmp_mask = iir & BXT_DE_PORT_HOTPLUG_MASK;
				if (tmp_mask) {
					bxt_hpd_irq_handler(dev_priv, tmp_mask,
							    hpd_bxt);
					found = true;
				}
			} else if (IS_BROADWELL(dev_priv)) {
				tmp_mask = iir & GEN8_PORT_DP_A_HOTPLUG;
				if (tmp_mask) {
					ilk_hpd_irq_handler(dev_priv,
							    tmp_mask, hpd_bdw);
					found = true;
				}
			}

			if (IS_GEN9_LP(dev_priv) && (iir & BXT_DE_PORT_GMBUS)) {
				gmbus_irq_handler(dev_priv);
				found = true;
			}

			if (!found)
				DRM_ERROR("Unexpected DE Port interrupt\n");
		}
		else
			DRM_ERROR("The master control interrupt lied (DE PORT)!\n");
	}

	for_each_pipe(dev_priv, pipe) {
		u32 fault_errors;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = I915_READ(GEN8_DE_PIPE_IIR(pipe));
		if (!iir) {
			DRM_ERROR("The master control interrupt lied (DE PIPE)!\n");
			continue;
		}

		ret = IRQ_HANDLED;
		I915_WRITE(GEN8_DE_PIPE_IIR(pipe), iir);

		if (iir & GEN8_PIPE_VBLANK)
			drm_handle_vblank(&dev_priv->drm, pipe);

		if (iir & GEN8_PIPE_CDCLK_CRC_DONE)
			hsw_pipe_crc_irq_handler(dev_priv, pipe);

		if (iir & GEN8_PIPE_FIFO_UNDERRUN)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		fault_errors = iir;
		if (INTEL_GEN(dev_priv) >= 9)
			fault_errors &= GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
		else
			fault_errors &= GEN8_DE_PIPE_IRQ_FAULT_ERRORS;

		if (fault_errors)
			DRM_ERROR("Fault errors on pipe %c: 0x%08x\n",
				  pipe_name(pipe),
				  fault_errors);
	}

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_PCH_NOP(dev_priv) &&
	    master_ctl & GEN8_DE_PCH_IRQ) {
		/*
		 * FIXME(BDW): Assume for now that the new interrupt handling
		 * scheme also closed the SDE interrupt handling race we've seen
		 * on older pch-split platforms. But this needs testing.
		 */
		iir = I915_READ(SDEIIR);
		if (iir) {
			I915_WRITE(SDEIIR, iir);
			ret = IRQ_HANDLED;

			if (INTEL_PCH_TYPE(dev_priv) >= PCH_MCC)
				icp_irq_handler(dev_priv, iir, hpd_mcc);
			else if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
				icp_irq_handler(dev_priv, iir, hpd_icp);
			else if (INTEL_PCH_TYPE(dev_priv) >= PCH_SPT)
				spt_irq_handler(dev_priv, iir);
			else
				cpt_irq_handler(dev_priv, iir);
		} else {
			/*
			 * Like on previous PCH there seems to be something
			 * fishy going on with forwarding PCH interrupts.
			 */
			DRM_DEBUG_DRIVER("The master control interrupt lied (SDE)!\n");
		}
	}

	return ret;
}

static inline u32 gen8_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN8_MASTER_IRQ);
}

static inline void gen8_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
}

static irqreturn_t gen8_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	void __iomem * const regs = dev_priv->uncore.regs;
	u32 master_ctl;
	u32 gt_iir[4];

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	master_ctl = gen8_master_intr_disable(regs);
	if (!master_ctl) {
		gen8_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, clear, then process each source of interrupt */
	gen8_gt_irq_ack(dev_priv, master_ctl, gt_iir);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & ~GEN8_GT_IRQS) {
		disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
		gen8_de_irq_handler(dev_priv, master_ctl);
		enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
	}

	gen8_master_intr_enable(regs);

	gen8_gt_irq_handler(dev_priv, master_ctl, gt_iir);

	return IRQ_HANDLED;
}

static u32
gen11_gt_engine_identity(struct intel_gt *gt,
			 const unsigned int bank, const unsigned int bit)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 timeout_ts;
	u32 ident;

	lockdep_assert_held(&gt->i915->irq_lock);

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
	struct drm_i915_private *i915 = gt->i915;

	if (instance == OTHER_GUC_INSTANCE)
		return gen11_guc_irq_handler(i915, iir);

	if (instance == OTHER_GTPM_INSTANCE)
		return gen11_rps_irq_handler(i915, iir);

	WARN_ONCE(1, "unhandled other interrupt instance=0x%x, iir=0x%x\n",
		  instance, iir);
}

static void
gen11_engine_irq_handler(struct intel_gt *gt, const u8 class,
			 const u8 instance, const u16 iir)
{
	struct intel_engine_cs *engine;

	if (instance <= MAX_ENGINE_INSTANCE)
		engine = gt->i915->engine_class[class][instance];
	else
		engine = NULL;

	if (likely(engine))
		return gen8_cs_irq_handler(engine, iir);

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

	lockdep_assert_held(&gt->i915->irq_lock);

	intr_dw = raw_reg_read(regs, GEN11_GT_INTR_DW(bank));

	for_each_set_bit(bit, &intr_dw, 32) {
		const u32 ident = gen11_gt_engine_identity(gt, bank, bit);

		gen11_gt_identity_handler(gt, ident);
	}

	/* Clear must be after shared has been served for engine */
	raw_reg_write(regs, GEN11_GT_INTR_DW(bank), intr_dw);
}

static void
gen11_gt_irq_handler(struct intel_gt *gt, const u32 master_ctl)
{
	struct drm_i915_private *i915 = gt->i915;
	unsigned int bank;

	spin_lock(&i915->irq_lock);

	for (bank = 0; bank < 2; bank++) {
		if (master_ctl & GEN11_GT_DW_IRQ(bank))
			gen11_gt_bank_handler(gt, bank);
	}

	spin_unlock(&i915->irq_lock);
}

static u32
gen11_gu_misc_irq_ack(struct intel_gt *gt, const u32 master_ctl)
{
	void __iomem * const regs = gt->uncore->regs;
	u32 iir;

	if (!(master_ctl & GEN11_GU_MISC_IRQ))
		return 0;

	iir = raw_reg_read(regs, GEN11_GU_MISC_IIR);
	if (likely(iir))
		raw_reg_write(regs, GEN11_GU_MISC_IIR, iir);

	return iir;
}

static void
gen11_gu_misc_irq_handler(struct intel_gt *gt, const u32 iir)
{
	if (iir & GEN11_GU_MISC_GSE)
		intel_opregion_asle_intr(gt->i915);
}

static inline u32 gen11_master_intr_disable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, 0);

	/*
	 * Now with master disabled, get a sample of level indications
	 * for this interrupt. Indications will be cleared on related acks.
	 * New indications can and will light up during processing,
	 * and will generate new interrupt after enabling master.
	 */
	return raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
}

static inline void gen11_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
}

static irqreturn_t gen11_irq_handler(int irq, void *arg)
{
	struct drm_i915_private * const i915 = arg;
	void __iomem * const regs = i915->uncore.regs;
	struct intel_gt *gt = &i915->gt;
	u32 master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_ctl = gen11_master_intr_disable(regs);
	if (!master_ctl) {
		gen11_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, clear, then process each source of interrupt. */
	gen11_gt_irq_handler(gt, master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & GEN11_DISPLAY_IRQ) {
		const u32 disp_ctl = raw_reg_read(regs, GEN11_DISPLAY_INT_CTL);

		disable_rpm_wakeref_asserts(&i915->runtime_pm);
		/*
		 * GEN11_DISPLAY_INT_CTL has same format as GEN8_MASTER_IRQ
		 * for the display related bits.
		 */
		gen8_de_irq_handler(i915, disp_ctl);
		enable_rpm_wakeref_asserts(&i915->runtime_pm);
	}

	gu_misc_iir = gen11_gu_misc_irq_ack(gt, master_ctl);

	gen11_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(gt, gu_misc_iir);

	return IRQ_HANDLED;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
int i8xx_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

int i945gm_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	if (dev_priv->i945gm_vblank.enabled++ == 0)
		schedule_work(&dev_priv->i945gm_vblank.work);

	return i8xx_enable_vblank(crtc);
}

int i965_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe,
			     PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

int ilk_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = INTEL_GEN(dev_priv) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_enable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	/* Even though there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated.
	 */
	if (HAS_PSR(dev_priv))
		drm_crtc_vblank_restore(crtc);

	return 0;
}

int bdw_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_enable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	/* Even if there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated, so check only for PSR.
	 */
	if (HAS_PSR(dev_priv))
		drm_crtc_vblank_restore(crtc);

	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
void i8xx_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe, PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void i945gm_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	i8xx_disable_vblank(crtc);

	if (--dev_priv->i945gm_vblank.enabled == 0)
		schedule_work(&dev_priv->i945gm_vblank.work);
}

void i965_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void ilk_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;
	u32 bit = INTEL_GEN(dev_priv) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void bdw_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_disable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void i945gm_vblank_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, i945gm_vblank.work);

	/*
	 * Vblank interrupts fail to wake up the device from C3,
	 * hence we want to prevent C3 usage while vblank interrupts
	 * are enabled.
	 */
	pm_qos_update_request(&dev_priv->i945gm_vblank.pm_qos,
			      READ_ONCE(dev_priv->i945gm_vblank.enabled) ?
			      dev_priv->i945gm_vblank.c3_disable_latency :
			      PM_QOS_DEFAULT_VALUE);
}

static int cstate_disable_latency(const char *name)
{
	const struct cpuidle_driver *drv;
	int i;

	drv = cpuidle_get_driver();
	if (!drv)
		return 0;

	for (i = 0; i < drv->state_count; i++) {
		const struct cpuidle_state *state = &drv->states[i];

		if (!strcmp(state->name, name))
			return state->exit_latency ?
				state->exit_latency - 1 : 0;
	}

	return 0;
}

static void i945gm_vblank_work_init(struct drm_i915_private *dev_priv)
{
	INIT_WORK(&dev_priv->i945gm_vblank.work,
		  i945gm_vblank_work_func);

	dev_priv->i945gm_vblank.c3_disable_latency =
		cstate_disable_latency("C3");
	pm_qos_add_request(&dev_priv->i945gm_vblank.pm_qos,
			   PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);
}

static void i945gm_vblank_work_fini(struct drm_i915_private *dev_priv)
{
	cancel_work_sync(&dev_priv->i945gm_vblank.work);
	pm_qos_remove_request(&dev_priv->i945gm_vblank.pm_qos);
}

static void ibx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (HAS_PCH_NOP(dev_priv))
		return;

	GEN3_IRQ_RESET(uncore, SDE);

	if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		I915_WRITE(SERR_INT, 0xffffffff);
}

/*
 * SDEIER is also touched by the interrupt handler to work around missed PCH
 * interrupts. Hence we can't update it after the interrupt handler is enabled -
 * instead we unconditionally enable all PCH interrupt sources here, but then
 * only unmask them as needed with SDEIMR.
 *
 * This function needs to be called before interrupts are enabled.
 */
static void ibx_irq_pre_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_NOP(dev_priv))
		return;

	WARN_ON(I915_READ(SDEIER) != 0);
	I915_WRITE(SDEIER, 0xffffffff);
	POSTING_READ(SDEIER);
}

static void gen5_gt_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	GEN3_IRQ_RESET(uncore, GT);
	if (INTEL_GEN(dev_priv) >= 6)
		GEN3_IRQ_RESET(uncore, GEN6_PM);
}

static void vlv_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (IS_CHERRYVIEW(dev_priv))
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK_CHV);
	else
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK);

	i915_hotplug_interrupt_update_locked(dev_priv, 0xffffffff, 0);
	intel_uncore_write(uncore, PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, VLV_);
	dev_priv->irq_mask = ~0u;
}

static void vlv_display_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 pipestat_mask;
	u32 enable_mask;
	enum pipe pipe;

	pipestat_mask = PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(dev_priv, pipe)
		i915_enable_pipestat(dev_priv, pipe, pipestat_mask);

	enable_mask = I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_LPE_PIPE_A_INTERRUPT |
		I915_LPE_PIPE_B_INTERRUPT;

	if (IS_CHERRYVIEW(dev_priv))
		enable_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT |
			I915_LPE_PIPE_C_INTERRUPT;

	WARN_ON(dev_priv->irq_mask != ~0u);

	dev_priv->irq_mask = ~enable_mask;

	GEN3_IRQ_INIT(uncore, VLV_, dev_priv->irq_mask, enable_mask);
}

/* drm_dma.h hooks
*/
static void ironlake_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	GEN3_IRQ_RESET(uncore, DE);
	if (IS_GEN(dev_priv, 7))
		intel_uncore_write(uncore, GEN7_ERR_INT, 0xffffffff);

	if (IS_HASWELL(dev_priv)) {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	gen5_gt_irq_reset(dev_priv);

	ibx_irq_reset(dev_priv);
}

static void valleyview_irq_reset(struct drm_i915_private *dev_priv)
{
	I915_WRITE(VLV_MASTER_IER, 0);
	POSTING_READ(VLV_MASTER_IER);

	gen5_gt_irq_reset(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void gen8_gt_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	GEN8_IRQ_RESET_NDX(uncore, GT, 0);
	GEN8_IRQ_RESET_NDX(uncore, GT, 1);
	GEN8_IRQ_RESET_NDX(uncore, GT, 2);
	GEN8_IRQ_RESET_NDX(uncore, GT, 3);
}

static void gen8_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	int pipe;

	gen8_master_intr_disable(dev_priv->uncore.regs);

	gen8_gt_irq_reset(dev_priv);

	intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
	intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_reset(dev_priv);
}

static void gen11_gt_irq_reset(struct intel_gt *gt)
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

static void gen11_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	int pipe;

	gen11_master_intr_disable(dev_priv->uncore.regs);

	gen11_gt_irq_reset(&dev_priv->gt);

	intel_uncore_write(uncore, GEN11_DISPLAY_INT_CTL, 0);

	intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
	intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);
	GEN3_IRQ_RESET(uncore, GEN11_DE_HPD_);
	GEN3_IRQ_RESET(uncore, GEN11_GU_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		GEN3_IRQ_RESET(uncore, SDE);
}

void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 extra_ier = GEN8_PIPE_VBLANK | GEN8_PIPE_FIFO_UNDERRUN;
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe,
				  dev_priv->de_irq_mask[pipe],
				  ~dev_priv->de_irq_mask[pipe] | extra_ier);

	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);

	if (!intel_irqs_enabled(dev_priv)) {
		spin_unlock_irq(&dev_priv->irq_lock);
		return;
	}

	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	spin_unlock_irq(&dev_priv->irq_lock);

	/* make sure we're done processing display irqs */
	intel_synchronize_irq(dev_priv);
}

static void cherryview_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static u32 intel_hpd_enabled_irqs(struct drm_i915_private *dev_priv,
				  const u32 hpd[HPD_NUM_PINS])
{
	struct intel_encoder *encoder;
	u32 enabled_irqs = 0;

	for_each_intel_encoder(&dev_priv->drm, encoder)
		if (dev_priv->hotplug.stats[encoder->hpd_pin].state == HPD_ENABLED)
			enabled_irqs |= hpd[encoder->hpd_pin];

	return enabled_irqs;
}

static void ibx_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	/*
	 * Enable digital hotplug on the PCH, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec).
	 * The pulse duration bits are reserved on LPT+.
	 */
	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTB_PULSE_DURATION_MASK |
		     PORTC_PULSE_DURATION_MASK |
		     PORTD_PULSE_DURATION_MASK);
	hotplug |= PORTB_HOTPLUG_ENABLE | PORTB_PULSE_DURATION_2ms;
	hotplug |= PORTC_HOTPLUG_ENABLE | PORTC_PULSE_DURATION_2ms;
	hotplug |= PORTD_HOTPLUG_ENABLE | PORTD_PULSE_DURATION_2ms;
	/*
	 * When CPU and PCH are on the same package, port A
	 * HPD must be enabled in both north and south.
	 */
	if (HAS_PCH_LPT_LP(dev_priv))
		hotplug |= PORTA_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);
}

static void ibx_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	if (HAS_PCH_IBX(dev_priv)) {
		hotplug_irqs = SDE_HOTPLUG_MASK;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_ibx);
	} else {
		hotplug_irqs = SDE_HOTPLUG_MASK_CPT;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_cpt);
	}

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	ibx_hpd_detection_setup(dev_priv);
}

static void icp_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = I915_READ(SHOTPLUG_CTL_DDI);
	hotplug |= ICP_DDIA_HPD_ENABLE |
		   ICP_DDIB_HPD_ENABLE;
	I915_WRITE(SHOTPLUG_CTL_DDI, hotplug);

	hotplug = I915_READ(SHOTPLUG_CTL_TC);
	hotplug |= ICP_TC_HPD_ENABLE(PORT_TC1) |
		   ICP_TC_HPD_ENABLE(PORT_TC2) |
		   ICP_TC_HPD_ENABLE(PORT_TC3) |
		   ICP_TC_HPD_ENABLE(PORT_TC4);
	I915_WRITE(SHOTPLUG_CTL_TC, hotplug);
}

static void icp_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	hotplug_irqs = SDE_DDI_MASK_ICP | SDE_TC_MASK_ICP;
	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_icp);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	icp_hpd_detection_setup(dev_priv);
}

static void gen11_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = I915_READ(GEN11_TC_HOTPLUG_CTL);
	hotplug |= GEN11_HOTPLUG_CTL_ENABLE(PORT_TC1) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC2) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC3) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC4);
	I915_WRITE(GEN11_TC_HOTPLUG_CTL, hotplug);

	hotplug = I915_READ(GEN11_TBT_HOTPLUG_CTL);
	hotplug |= GEN11_HOTPLUG_CTL_ENABLE(PORT_TC1) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC2) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC3) |
		   GEN11_HOTPLUG_CTL_ENABLE(PORT_TC4);
	I915_WRITE(GEN11_TBT_HOTPLUG_CTL, hotplug);
}

static void gen11_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;
	u32 val;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_gen11);
	hotplug_irqs = GEN11_DE_TC_HOTPLUG_MASK | GEN11_DE_TBT_HOTPLUG_MASK;

	val = I915_READ(GEN11_DE_HPD_IMR);
	val &= ~hotplug_irqs;
	I915_WRITE(GEN11_DE_HPD_IMR, val);
	POSTING_READ(GEN11_DE_HPD_IMR);

	gen11_hpd_detection_setup(dev_priv);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_hpd_irq_setup(dev_priv);
}

static void spt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 val, hotplug;

	/* Display WA #1179 WaHardHangonHotPlug: cnp */
	if (HAS_PCH_CNP(dev_priv)) {
		val = I915_READ(SOUTH_CHICKEN1);
		val &= ~CHASSIS_CLK_REQ_DURATION_MASK;
		val |= CHASSIS_CLK_REQ_DURATION(0xf);
		I915_WRITE(SOUTH_CHICKEN1, val);
	}

	/* Enable digital hotplug on the PCH */
	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug |= PORTA_HOTPLUG_ENABLE |
		   PORTB_HOTPLUG_ENABLE |
		   PORTC_HOTPLUG_ENABLE |
		   PORTD_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);

	hotplug = I915_READ(PCH_PORT_HOTPLUG2);
	hotplug |= PORTE_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG2, hotplug);
}

static void spt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	hotplug_irqs = SDE_HOTPLUG_MASK_SPT;
	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_spt);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	spt_hpd_detection_setup(dev_priv);
}

static void ilk_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	/*
	 * Enable digital hotplug on the CPU, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec)
	 * The pulse duration bits are reserved on HSW+.
	 */
	hotplug = I915_READ(DIGITAL_PORT_HOTPLUG_CNTRL);
	hotplug &= ~DIGITAL_PORTA_PULSE_DURATION_MASK;
	hotplug |= DIGITAL_PORTA_HOTPLUG_ENABLE |
		   DIGITAL_PORTA_PULSE_DURATION_2ms;
	I915_WRITE(DIGITAL_PORT_HOTPLUG_CNTRL, hotplug);
}

static void ilk_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	if (INTEL_GEN(dev_priv) >= 8) {
		hotplug_irqs = GEN8_PORT_DP_A_HOTPLUG;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_bdw);

		bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);
	} else if (INTEL_GEN(dev_priv) >= 7) {
		hotplug_irqs = DE_DP_A_HOTPLUG_IVB;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_ivb);

		ilk_update_display_irq(dev_priv, hotplug_irqs, enabled_irqs);
	} else {
		hotplug_irqs = DE_DP_A_HOTPLUG;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_ilk);

		ilk_update_display_irq(dev_priv, hotplug_irqs, enabled_irqs);
	}

	ilk_hpd_detection_setup(dev_priv);

	ibx_hpd_irq_setup(dev_priv);
}

static void __bxt_hpd_detection_setup(struct drm_i915_private *dev_priv,
				      u32 enabled_irqs)
{
	u32 hotplug;

	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug |= PORTA_HOTPLUG_ENABLE |
		   PORTB_HOTPLUG_ENABLE |
		   PORTC_HOTPLUG_ENABLE;

	DRM_DEBUG_KMS("Invert bit setting: hp_ctl:%x hp_port:%x\n",
		      hotplug, enabled_irqs);
	hotplug &= ~BXT_DDI_HPD_INVERT_MASK;

	/*
	 * For BXT invert bit has to be set based on AOB design
	 * for HPD detection logic, update it based on VBT fields.
	 */
	if ((enabled_irqs & BXT_DE_PORT_HP_DDIA) &&
	    intel_bios_is_port_hpd_inverted(dev_priv, PORT_A))
		hotplug |= BXT_DDIA_HPD_INVERT;
	if ((enabled_irqs & BXT_DE_PORT_HP_DDIB) &&
	    intel_bios_is_port_hpd_inverted(dev_priv, PORT_B))
		hotplug |= BXT_DDIB_HPD_INVERT;
	if ((enabled_irqs & BXT_DE_PORT_HP_DDIC) &&
	    intel_bios_is_port_hpd_inverted(dev_priv, PORT_C))
		hotplug |= BXT_DDIC_HPD_INVERT;

	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);
}

static void bxt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	__bxt_hpd_detection_setup(dev_priv, BXT_DE_PORT_HOTPLUG_MASK);
}

static void bxt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_bxt);
	hotplug_irqs = BXT_DE_PORT_HOTPLUG_MASK;

	bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);

	__bxt_hpd_detection_setup(dev_priv, enabled_irqs);
}

static void ibx_irq_postinstall(struct drm_i915_private *dev_priv)
{
	u32 mask;

	if (HAS_PCH_NOP(dev_priv))
		return;

	if (HAS_PCH_IBX(dev_priv))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;
	else
		mask = SDE_GMBUS_CPT;

	gen3_assert_iir_is_zero(&dev_priv->uncore, SDEIIR);
	I915_WRITE(SDEIMR, ~mask);

	if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv) ||
	    HAS_PCH_LPT(dev_priv))
		ibx_hpd_detection_setup(dev_priv);
	else
		spt_hpd_detection_setup(dev_priv);
}

static void gen5_gt_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 pm_irqs, gt_irqs;

	pm_irqs = gt_irqs = 0;

	dev_priv->gt_irq_mask = ~0;
	if (HAS_L3_DPF(dev_priv)) {
		/* L3 parity interrupt is always unmasked. */
		dev_priv->gt_irq_mask = ~GT_PARITY_ERROR(dev_priv);
		gt_irqs |= GT_PARITY_ERROR(dev_priv);
	}

	gt_irqs |= GT_RENDER_USER_INTERRUPT;
	if (IS_GEN(dev_priv, 5)) {
		gt_irqs |= ILK_BSD_USER_INTERRUPT;
	} else {
		gt_irqs |= GT_BLT_USER_INTERRUPT | GT_BSD_USER_INTERRUPT;
	}

	GEN3_IRQ_INIT(uncore, GT, dev_priv->gt_irq_mask, gt_irqs);

	if (INTEL_GEN(dev_priv) >= 6) {
		/*
		 * RPS interrupts will get enabled/disabled on demand when RPS
		 * itself is enabled/disabled.
		 */
		if (HAS_ENGINE(dev_priv, VECS0)) {
			pm_irqs |= PM_VEBOX_USER_INTERRUPT;
			dev_priv->pm_ier |= PM_VEBOX_USER_INTERRUPT;
		}

		dev_priv->pm_imr = 0xffffffff;
		GEN3_IRQ_INIT(uncore, GEN6_PM, dev_priv->pm_imr, pm_irqs);
	}
}

static void ironlake_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 display_mask, extra_mask;

	if (INTEL_GEN(dev_priv) >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB |
			      DE_DP_A_HOTPLUG_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_AUX_CHANNEL_A | DE_PIPEB_CRC_DONE |
				DE_PIPEA_CRC_DONE | DE_POISON);
		extra_mask = (DE_PIPEA_VBLANK | DE_PIPEB_VBLANK | DE_PCU_EVENT |
			      DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN |
			      DE_DP_A_HOTPLUG);
	}

	if (IS_HASWELL(dev_priv)) {
		gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
		intel_psr_irq_control(dev_priv, dev_priv->psr.debug);
		display_mask |= DE_EDP_PSR_INT_HSW;
	}

	dev_priv->irq_mask = ~display_mask;

	ibx_irq_pre_postinstall(dev_priv);

	GEN3_IRQ_INIT(uncore, DE, dev_priv->irq_mask,
		      display_mask | extra_mask);

	gen5_gt_irq_postinstall(dev_priv);

	ilk_hpd_detection_setup(dev_priv);

	ibx_irq_postinstall(dev_priv);

	if (IS_IRONLAKE_M(dev_priv)) {
		/* Enable PCU event interrupts
		 *
		 * spinlocking not required here for correctness since interrupt
		 * setup is guaranteed to run in single-threaded context. But we
		 * need it to make the assert_spin_locked happy. */
		spin_lock_irq(&dev_priv->irq_lock);
		ilk_enable_display_irq(dev_priv, DE_PCU_EVENT);
		spin_unlock_irq(&dev_priv->irq_lock);
	}
}

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	if (dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = true;

	if (intel_irqs_enabled(dev_priv)) {
		vlv_display_irq_reset(dev_priv);
		vlv_display_irq_postinstall(dev_priv);
	}
}

void valleyview_disable_display_irqs(struct drm_i915_private *dev_priv)
{
	lockdep_assert_held(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = false;

	if (intel_irqs_enabled(dev_priv))
		vlv_display_irq_reset(dev_priv);
}


static void valleyview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	gen5_gt_irq_postinstall(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
	POSTING_READ(VLV_MASTER_IER);
}

static void gen8_gt_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	/* These are interrupts we'll toggle with the ring mask register */
	u32 gt_interrupts[] = {
		(GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
		 GT_CONTEXT_SWITCH_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
		 GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT |
		 GT_CONTEXT_SWITCH_INTERRUPT << GEN8_BCS_IRQ_SHIFT),

		(GT_RENDER_USER_INTERRUPT << GEN8_VCS0_IRQ_SHIFT |
		 GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS0_IRQ_SHIFT |
		 GT_RENDER_USER_INTERRUPT << GEN8_VCS1_IRQ_SHIFT |
		 GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS1_IRQ_SHIFT),

		0,

		(GT_RENDER_USER_INTERRUPT << GEN8_VECS_IRQ_SHIFT |
		 GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VECS_IRQ_SHIFT)
	};

	dev_priv->pm_ier = 0x0;
	dev_priv->pm_imr = ~dev_priv->pm_ier;
	GEN8_IRQ_INIT_NDX(uncore, GT, 0, ~gt_interrupts[0], gt_interrupts[0]);
	GEN8_IRQ_INIT_NDX(uncore, GT, 1, ~gt_interrupts[1], gt_interrupts[1]);
	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled. Same wil be the case for GuC interrupts.
	 */
	GEN8_IRQ_INIT_NDX(uncore, GT, 2, dev_priv->pm_imr, dev_priv->pm_ier);
	GEN8_IRQ_INIT_NDX(uncore, GT, 3, ~gt_interrupts[3], gt_interrupts[3]);
}

static void gen8_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 de_pipe_masked = GEN8_PIPE_CDCLK_CRC_DONE;
	u32 de_pipe_enables;
	u32 de_port_masked = GEN8_AUX_CHANNEL_A;
	u32 de_port_enables;
	u32 de_misc_masked = GEN8_DE_EDP_PSR;
	enum pipe pipe;

	if (INTEL_GEN(dev_priv) <= 10)
		de_misc_masked |= GEN8_DE_MISC_GSE;

	if (INTEL_GEN(dev_priv) >= 9) {
		de_pipe_masked |= GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
		de_port_masked |= GEN9_AUX_CHANNEL_B | GEN9_AUX_CHANNEL_C |
				  GEN9_AUX_CHANNEL_D;
		if (IS_GEN9_LP(dev_priv))
			de_port_masked |= BXT_DE_PORT_GMBUS;
	} else {
		de_pipe_masked |= GEN8_DE_PIPE_IRQ_FAULT_ERRORS;
	}

	if (INTEL_GEN(dev_priv) >= 11)
		de_port_masked |= ICL_AUX_CHANNEL_E;

	if (IS_CNL_WITH_PORT_F(dev_priv) || INTEL_GEN(dev_priv) >= 11)
		de_port_masked |= CNL_AUX_CHANNEL_F;

	de_pipe_enables = de_pipe_masked | GEN8_PIPE_VBLANK |
					   GEN8_PIPE_FIFO_UNDERRUN;

	de_port_enables = de_port_masked;
	if (IS_GEN9_LP(dev_priv))
		de_port_enables |= BXT_DE_PORT_HOTPLUG_MASK;
	else if (IS_BROADWELL(dev_priv))
		de_port_enables |= GEN8_PORT_DP_A_HOTPLUG;

	gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
	intel_psr_irq_control(dev_priv, dev_priv->psr.debug);

	for_each_pipe(dev_priv, pipe) {
		dev_priv->de_irq_mask[pipe] = ~de_pipe_masked;

		if (intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe,
					  dev_priv->de_irq_mask[pipe],
					  de_pipe_enables);
	}

	GEN3_IRQ_INIT(uncore, GEN8_DE_PORT_, ~de_port_masked, de_port_enables);
	GEN3_IRQ_INIT(uncore, GEN8_DE_MISC_, ~de_misc_masked, de_misc_masked);

	if (INTEL_GEN(dev_priv) >= 11) {
		u32 de_hpd_masked = 0;
		u32 de_hpd_enables = GEN11_DE_TC_HOTPLUG_MASK |
				     GEN11_DE_TBT_HOTPLUG_MASK;

		GEN3_IRQ_INIT(uncore, GEN11_DE_HPD_, ~de_hpd_masked,
			      de_hpd_enables);
		gen11_hpd_detection_setup(dev_priv);
	} else if (IS_GEN9_LP(dev_priv)) {
		bxt_hpd_detection_setup(dev_priv);
	} else if (IS_BROADWELL(dev_priv)) {
		ilk_hpd_detection_setup(dev_priv);
	}
}

static void gen8_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_pre_postinstall(dev_priv);

	gen8_gt_irq_postinstall(dev_priv);
	gen8_de_irq_postinstall(dev_priv);

	if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_postinstall(dev_priv);

	gen8_master_intr_enable(dev_priv->uncore.regs);
}

static void gen11_gt_irq_postinstall(struct intel_gt *gt)
{
	const u32 irqs = GT_RENDER_USER_INTERRUPT | GT_CONTEXT_SWITCH_INTERRUPT;
	struct drm_i915_private *dev_priv = gt->i915;
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
	dev_priv->pm_ier = 0x0;
	dev_priv->pm_imr = ~dev_priv->pm_ier;
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK,  ~0);

	/* Same thing for GuC interrupts */
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_ENABLE, 0);
	intel_uncore_write(uncore, GEN11_GUC_SG_INTR_MASK,  ~0);
}

static void icp_irq_postinstall(struct drm_i915_private *dev_priv)
{
	u32 mask = SDE_GMBUS_ICP;

	WARN_ON(I915_READ(SDEIER) != 0);
	I915_WRITE(SDEIER, 0xffffffff);
	POSTING_READ(SDEIER);

	gen3_assert_iir_is_zero(&dev_priv->uncore, SDEIIR);
	I915_WRITE(SDEIMR, ~mask);

	icp_hpd_detection_setup(dev_priv);
}

static void gen11_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_irq_postinstall(dev_priv);

	gen11_gt_irq_postinstall(&dev_priv->gt);
	gen8_de_irq_postinstall(dev_priv);

	GEN3_IRQ_INIT(uncore, GEN11_GU_MISC_, ~gu_misc_masked, gu_misc_masked);

	I915_WRITE(GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE);

	gen11_master_intr_enable(uncore->regs);
	POSTING_READ(GEN11_GFX_MSTR_IRQ);
}

static void cherryview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	gen8_gt_irq_postinstall(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	POSTING_READ(GEN8_MASTER_IRQ);
}

static void i8xx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i9xx_pipestat_irq_reset(dev_priv);

	GEN2_IRQ_RESET(uncore);
}

static void i8xx_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u16 enable_mask;

	intel_uncore_write16(uncore,
			     EMR,
			     ~(I915_ERROR_PAGE_TABLE |
			       I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	GEN2_IRQ_INIT(uncore, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void i8xx_error_irq_ack(struct drm_i915_private *i915,
			       u16 *eir, u16 *eir_stuck)
{
	struct intel_uncore *uncore = &i915->uncore;
	u16 emr;

	*eir = intel_uncore_read16(uncore, EIR);

	if (*eir)
		intel_uncore_write16(uncore, EIR, *eir);

	*eir_stuck = intel_uncore_read16(uncore, EIR);
	if (*eir_stuck == 0)
		return;

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits. Otherwise the edge triggered
	 * IIR on i965/g4x wouldn't notice that an interrupt
	 * is still pending. Also some EIR bits can't be
	 * cleared except by handling the underlying error
	 * (or by a GPU reset) so we mask any bit that
	 * remains set.
	 */
	emr = intel_uncore_read16(uncore, EMR);
	intel_uncore_write16(uncore, EMR, 0xffff);
	intel_uncore_write16(uncore, EMR, emr | *eir_stuck);
}

static void i8xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u16 eir, u16 eir_stuck)
{
	DRM_DEBUG("Master Error: EIR 0x%04x\n", eir);

	if (eir_stuck)
		DRM_DEBUG_DRIVER("EIR stuck: 0x%04x, masked\n", eir_stuck);
}

static void i9xx_error_irq_ack(struct drm_i915_private *dev_priv,
			       u32 *eir, u32 *eir_stuck)
{
	u32 emr;

	*eir = I915_READ(EIR);

	I915_WRITE(EIR, *eir);

	*eir_stuck = I915_READ(EIR);
	if (*eir_stuck == 0)
		return;

	/*
	 * Toggle all EMR bits to make sure we get an edge
	 * in the ISR master error bit if we don't clear
	 * all the EIR bits. Otherwise the edge triggered
	 * IIR on i965/g4x wouldn't notice that an interrupt
	 * is still pending. Also some EIR bits can't be
	 * cleared except by handling the underlying error
	 * (or by a GPU reset) so we mask any bit that
	 * remains set.
	 */
	emr = I915_READ(EMR);
	I915_WRITE(EMR, 0xffffffff);
	I915_WRITE(EMR, emr | *eir_stuck);
}

static void i9xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u32 eir, u32 eir_stuck)
{
	DRM_DEBUG("Master Error, EIR 0x%08x\n", eir);

	if (eir_stuck)
		DRM_DEBUG_DRIVER("EIR stuck: 0x%08x, masked\n", eir_stuck);
}

static irqreturn_t i8xx_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u16 eir = 0, eir_stuck = 0;
		u16 iir;

		iir = intel_uncore_read16(&dev_priv->uncore, GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i8xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		intel_uncore_write16(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_breadcrumbs_irq(dev_priv->engine[RCS0]);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i8xx_error_irq_handler(dev_priv, eir, eir_stuck);

		i8xx_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i915_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (I915_HAS_HOTPLUG(dev_priv)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
}

static void i915_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	I915_WRITE(EMR, ~(I915_ERROR_PAGE_TABLE |
			  I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (I915_HAS_HOTPLUG(dev_priv)) {
		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		dev_priv->irq_mask &= ~I915_DISPLAY_PORT_INTERRUPT;
	}

	GEN3_IRQ_INIT(uncore, GEN2_, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	i915_enable_asle_pipestat(dev_priv);
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = I915_READ(GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (I915_HAS_HOTPLUG(dev_priv) &&
		    iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		I915_WRITE(GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_breadcrumbs_irq(dev_priv->engine[RCS0]);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i915_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i965_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
}

static void i965_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;
	u32 error_mask;

	/*
	 * Enable some error detection, note the instruction error mask
	 * bit is reserved, so we leave it masked.
	 */
	if (IS_G4X(dev_priv)) {
		error_mask = ~(GM45_ERROR_PAGE_TABLE |
			       GM45_ERROR_MEM_PRIV |
			       GM45_ERROR_CP_PRIV |
			       I915_ERROR_MEMORY_REFRESH);
	} else {
		error_mask = ~(I915_ERROR_PAGE_TABLE |
			       I915_ERROR_MEMORY_REFRESH);
	}
	I915_WRITE(EMR, error_mask);

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PORT_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_MASTER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_MASTER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (IS_G4X(dev_priv))
		enable_mask |= I915_BSD_USER_INTERRUPT;

	GEN3_IRQ_INIT(uncore, GEN2_, dev_priv->irq_mask, enable_mask);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	i915_enable_asle_pipestat(dev_priv);
}

static void i915_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_en;

	lockdep_assert_held(&dev_priv->irq_lock);

	/* Note HDMI and DP share hotplug bits */
	/* enable bits are the same for all generations */
	hotplug_en = intel_hpd_enabled_irqs(dev_priv, hpd_mask_i915);
	/* Programming the CRT detection parameters tends
	   to generate a spurious hotplug event about three
	   seconds later.  So just do it once.
	*/
	if (IS_G4X(dev_priv))
		hotplug_en |= CRT_HOTPLUG_ACTIVATION_PERIOD_64;
	hotplug_en |= CRT_HOTPLUG_VOLTAGE_COMPARE_50;

	/* Ignore TV since it's buggy */
	i915_hotplug_interrupt_update_locked(dev_priv,
					     HOTPLUG_INT_EN_MASK |
					     CRT_HOTPLUG_VOLTAGE_COMPARE_MASK |
					     CRT_HOTPLUG_ACTIVATION_PERIOD_64,
					     hotplug_en);
}

static irqreturn_t i965_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *dev_priv = arg;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	do {
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 eir = 0, eir_stuck = 0;
		u32 hotplug_status = 0;
		u32 iir;

		iir = I915_READ(GEN2_IIR);
		if (iir == 0)
			break;

		ret = IRQ_HANDLED;

		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			hotplug_status = i9xx_hpd_irq_ack(dev_priv);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		i9xx_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_ack(dev_priv, &eir, &eir_stuck);

		I915_WRITE(GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_breadcrumbs_irq(dev_priv->engine[RCS0]);

		if (iir & I915_BSD_USER_INTERRUPT)
			intel_engine_breadcrumbs_irq(dev_priv->engine[VCS0]);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i965_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

/**
 * intel_irq_init - initializes irq support
 * @dev_priv: i915 device instance
 *
 * This function initializes all the irq support including work items, timers
 * and all the vtables. It does not setup the interrupt itself though.
 */
void intel_irq_init(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	int i;

	if (IS_I945GM(dev_priv))
		i945gm_vblank_work_init(dev_priv);

	intel_hpd_init_work(dev_priv);

	INIT_WORK(&rps->work, gen6_pm_rps_work);

	INIT_WORK(&dev_priv->l3_parity.error_work, ivybridge_parity_work);
	for (i = 0; i < MAX_L3_SLICES; ++i)
		dev_priv->l3_parity.remap_info[i] = NULL;

	if (HAS_GUC_SCHED(dev_priv) && INTEL_GEN(dev_priv) < 11)
		dev_priv->pm_guc_events = GEN9_GUC_TO_HOST_INT_EVENT;

	/* Let's track the enabled rps events */
	if (IS_VALLEYVIEW(dev_priv))
		/* WaGsvRC0ResidencyMethod:vlv */
		dev_priv->pm_rps_events = GEN6_PM_RP_UP_EI_EXPIRED;
	else
		dev_priv->pm_rps_events = (GEN6_PM_RP_UP_THRESHOLD |
					   GEN6_PM_RP_DOWN_THRESHOLD |
					   GEN6_PM_RP_DOWN_TIMEOUT);

	/* We share the register with other engine */
	if (INTEL_GEN(dev_priv) > 9)
		GEM_WARN_ON(dev_priv->pm_rps_events & 0xffff0000);

	rps->pm_intrmsk_mbz = 0;

	/*
	 * SNB,IVB,HSW can while VLV,CHV may hard hang on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 *
	 * TODO: verify if this can be reproduced on VLV,CHV.
	 */
	if (INTEL_GEN(dev_priv) <= 7)
		rps->pm_intrmsk_mbz |= GEN6_PM_RP_UP_EI_EXPIRED;

	if (INTEL_GEN(dev_priv) >= 8)
		rps->pm_intrmsk_mbz |= GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC;

	dev->vblank_disable_immediate = true;

	/* Most platforms treat the display irq block as an always-on
	 * power domain. vlv/chv can disable it at runtime and need
	 * special care to avoid writing any of the display block registers
	 * outside of the power domain. We defer setting up the display irqs
	 * in this case to the runtime pm.
	 */
	dev_priv->display_irqs_enabled = true;
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		dev_priv->display_irqs_enabled = false;

	dev_priv->hotplug.hpd_storm_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	/* If we have MST support, we want to avoid doing short HPD IRQ storm
	 * detection, as short HPD storms will occur as a natural part of
	 * sideband messaging with MST.
	 * On older platforms however, IRQ storms can occur with both long and
	 * short pulses, as seen on some G4x systems.
	 */
	dev_priv->hotplug.hpd_short_storm_enabled = !HAS_DP_MST(dev_priv);

	if (HAS_GMCH(dev_priv)) {
		if (I915_HAS_HOTPLUG(dev_priv))
			dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
	} else {
		if (INTEL_GEN(dev_priv) >= 11)
			dev_priv->display.hpd_irq_setup = gen11_hpd_irq_setup;
		else if (IS_GEN9_LP(dev_priv))
			dev_priv->display.hpd_irq_setup = bxt_hpd_irq_setup;
		else if (INTEL_PCH_TYPE(dev_priv) >= PCH_SPT)
			dev_priv->display.hpd_irq_setup = spt_hpd_irq_setup;
		else
			dev_priv->display.hpd_irq_setup = ilk_hpd_irq_setup;
	}
}

/**
 * intel_irq_fini - deinitializes IRQ support
 * @i915: i915 device instance
 *
 * This function deinitializes all the IRQ support.
 */
void intel_irq_fini(struct drm_i915_private *i915)
{
	int i;

	if (IS_I945GM(i915))
		i945gm_vblank_work_fini(i915);

	for (i = 0; i < MAX_L3_SLICES; ++i)
		kfree(i915->l3_parity.remap_info[i]);
}

static irq_handler_t intel_irq_handler(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			return cherryview_irq_handler;
		else if (IS_VALLEYVIEW(dev_priv))
			return valleyview_irq_handler;
		else if (IS_GEN(dev_priv, 4))
			return i965_irq_handler;
		else if (IS_GEN(dev_priv, 3))
			return i915_irq_handler;
		else
			return i8xx_irq_handler;
	} else {
		if (INTEL_GEN(dev_priv) >= 11)
			return gen11_irq_handler;
		else if (INTEL_GEN(dev_priv) >= 8)
			return gen8_irq_handler;
		else
			return ironlake_irq_handler;
	}
}

static void intel_irq_reset(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_reset(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_reset(dev_priv);
		else if (IS_GEN(dev_priv, 4))
			i965_irq_reset(dev_priv);
		else if (IS_GEN(dev_priv, 3))
			i915_irq_reset(dev_priv);
		else
			i8xx_irq_reset(dev_priv);
	} else {
		if (INTEL_GEN(dev_priv) >= 11)
			gen11_irq_reset(dev_priv);
		else if (INTEL_GEN(dev_priv) >= 8)
			gen8_irq_reset(dev_priv);
		else
			ironlake_irq_reset(dev_priv);
	}
}

static void intel_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_postinstall(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_postinstall(dev_priv);
		else if (IS_GEN(dev_priv, 4))
			i965_irq_postinstall(dev_priv);
		else if (IS_GEN(dev_priv, 3))
			i915_irq_postinstall(dev_priv);
		else
			i8xx_irq_postinstall(dev_priv);
	} else {
		if (INTEL_GEN(dev_priv) >= 11)
			gen11_irq_postinstall(dev_priv);
		else if (INTEL_GEN(dev_priv) >= 8)
			gen8_irq_postinstall(dev_priv);
		else
			ironlake_irq_postinstall(dev_priv);
	}
}

/**
 * intel_irq_install - enables the hardware interrupt
 * @dev_priv: i915 device instance
 *
 * This function enables the hardware interrupt handling, but leaves the hotplug
 * handling still disabled. It is called after intel_irq_init().
 *
 * In the driver load and resume code we need working interrupts in a few places
 * but don't want to deal with the hassle of concurrent probe and hotplug
 * workers. Hence the split into this two-stage approach.
 */
int intel_irq_install(struct drm_i915_private *dev_priv)
{
	int irq = dev_priv->drm.pdev->irq;
	int ret;

	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid
	 * special cases in our ordering checks.
	 */
	dev_priv->runtime_pm.irqs_enabled = true;

	dev_priv->drm.irq_enabled = true;

	intel_irq_reset(dev_priv);

	ret = request_irq(irq, intel_irq_handler(dev_priv),
			  IRQF_SHARED, DRIVER_NAME, dev_priv);
	if (ret < 0) {
		dev_priv->drm.irq_enabled = false;
		return ret;
	}

	intel_irq_postinstall(dev_priv);

	return ret;
}

/**
 * intel_irq_uninstall - finilizes all irq handling
 * @dev_priv: i915 device instance
 *
 * This stops interrupt and hotplug handling and unregisters and frees all
 * resources acquired in the init functions.
 */
void intel_irq_uninstall(struct drm_i915_private *dev_priv)
{
	int irq = dev_priv->drm.pdev->irq;

	/*
	 * FIXME we can get called twice during driver load
	 * error handling due to intel_modeset_cleanup()
	 * calling us out of sequence. Would be nice if
	 * it didn't do that...
	 */
	if (!dev_priv->drm.irq_enabled)
		return;

	dev_priv->drm.irq_enabled = false;

	intel_irq_reset(dev_priv);

	free_irq(irq, dev_priv);

	intel_hpd_cancel_work(dev_priv);
	dev_priv->runtime_pm.irqs_enabled = false;
}

/**
 * intel_runtime_pm_disable_interrupts - runtime interrupt disabling
 * @dev_priv: i915 device instance
 *
 * This function is used to disable interrupts at runtime, both in the runtime
 * pm and the system suspend/resume code.
 */
void intel_runtime_pm_disable_interrupts(struct drm_i915_private *dev_priv)
{
	intel_irq_reset(dev_priv);
	dev_priv->runtime_pm.irqs_enabled = false;
	intel_synchronize_irq(dev_priv);
}

/**
 * intel_runtime_pm_enable_interrupts - runtime interrupt enabling
 * @dev_priv: i915 device instance
 *
 * This function is used to enable interrupts at runtime, both in the runtime
 * pm and the system suspend/resume code.
 */
void intel_runtime_pm_enable_interrupts(struct drm_i915_private *dev_priv)
{
	dev_priv->runtime_pm.irqs_enabled = true;
	intel_irq_reset(dev_priv);
	intel_irq_postinstall(dev_priv);
}
