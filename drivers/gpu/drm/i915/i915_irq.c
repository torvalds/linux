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
#include <linux/slab.h>
#include <linux/sysrq.h>

#include <drm/drm_drv.h>

#include "display/icl_dsi_regs.h"
#include "display/intel_de.h"
#include "display/intel_display_trace.h"
#include "display/intel_display_types.h"
#include "display/intel_fifo_underrun.h"
#include "display/intel_hotplug.h"
#include "display/intel_lpe_audio.h"
#include "display/intel_psr.h"

#include "gt/intel_breadcrumbs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm_irq.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_rps.h"

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_pm.h"

/**
 * DOC: interrupt handling
 *
 * These functions provide the basic support for enabling and disabling the
 * interrupt handling support. There's a lot more functionality in i915_irq.c
 * and related files, but that will be described in separate chapters.
 */

/*
 * Interrupt statistic for PMU. Increments the counter only if the
 * interrupt originated from the GPU so interrupts from a device which
 * shares the interrupt line are not accounted.
 */
static inline void pmu_irq_stats(struct drm_i915_private *i915,
				 irqreturn_t res)
{
	if (unlikely(res != IRQ_HANDLED))
		return;

	/*
	 * A clever compiler translates that into INC. A not so clever one
	 * should at least prevent store tearing.
	 */
	WRITE_ONCE(i915->pmu.irq_count, i915->pmu.irq_count + 1);
}

typedef bool (*long_pulse_detect_func)(enum hpd_pin pin, u32 val);
typedef u32 (*hotplug_enables_func)(struct drm_i915_private *i915,
				    enum hpd_pin pin);

static const u32 hpd_ilk[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG,
};

static const u32 hpd_ivb[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG_IVB,
};

static const u32 hpd_bdw[HPD_NUM_PINS] = {
	[HPD_PORT_A] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_A),
};

static const u32 hpd_ibx[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG,
};

static const u32 hpd_cpt[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG_CPT,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG_CPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT,
};

static const u32 hpd_spt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_PORTA_HOTPLUG_SPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT,
	[HPD_PORT_E] = SDE_PORTE_HOTPLUG_SPT,
};

static const u32 hpd_mask_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_EN,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_EN,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_EN,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_EN,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_EN,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_EN,
};

static const u32 hpd_status_g4x[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_G4X,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_G4X,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS,
};

static const u32 hpd_status_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I915,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I915,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS,
};

static const u32 hpd_bxt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_A),
	[HPD_PORT_B] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_B),
	[HPD_PORT_C] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_C),
};

static const u32 hpd_gen11[HPD_NUM_PINS] = {
	[HPD_PORT_TC1] = GEN11_TC_HOTPLUG(HPD_PORT_TC1) | GEN11_TBT_HOTPLUG(HPD_PORT_TC1),
	[HPD_PORT_TC2] = GEN11_TC_HOTPLUG(HPD_PORT_TC2) | GEN11_TBT_HOTPLUG(HPD_PORT_TC2),
	[HPD_PORT_TC3] = GEN11_TC_HOTPLUG(HPD_PORT_TC3) | GEN11_TBT_HOTPLUG(HPD_PORT_TC3),
	[HPD_PORT_TC4] = GEN11_TC_HOTPLUG(HPD_PORT_TC4) | GEN11_TBT_HOTPLUG(HPD_PORT_TC4),
	[HPD_PORT_TC5] = GEN11_TC_HOTPLUG(HPD_PORT_TC5) | GEN11_TBT_HOTPLUG(HPD_PORT_TC5),
	[HPD_PORT_TC6] = GEN11_TC_HOTPLUG(HPD_PORT_TC6) | GEN11_TBT_HOTPLUG(HPD_PORT_TC6),
};

static const u32 hpd_icp[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_A),
	[HPD_PORT_B] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_B),
	[HPD_PORT_C] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_C),
	[HPD_PORT_TC1] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC1),
	[HPD_PORT_TC2] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC2),
	[HPD_PORT_TC3] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC3),
	[HPD_PORT_TC4] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC4),
	[HPD_PORT_TC5] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC5),
	[HPD_PORT_TC6] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC6),
};

static const u32 hpd_sde_dg1[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_A),
	[HPD_PORT_B] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_B),
	[HPD_PORT_C] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_C),
	[HPD_PORT_D] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_D),
	[HPD_PORT_TC1] = SDE_TC_HOTPLUG_DG2(HPD_PORT_TC1),
};

static void intel_hpd_init_pins(struct drm_i915_private *dev_priv)
{
	struct intel_hotplug *hpd = &dev_priv->display.hotplug;

	if (HAS_GMCH(dev_priv)) {
		if (IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
		    IS_CHERRYVIEW(dev_priv))
			hpd->hpd = hpd_status_g4x;
		else
			hpd->hpd = hpd_status_i915;
		return;
	}

	if (DISPLAY_VER(dev_priv) >= 11)
		hpd->hpd = hpd_gen11;
	else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		hpd->hpd = hpd_bxt;
	else if (DISPLAY_VER(dev_priv) >= 8)
		hpd->hpd = hpd_bdw;
	else if (DISPLAY_VER(dev_priv) >= 7)
		hpd->hpd = hpd_ivb;
	else
		hpd->hpd = hpd_ilk;

	if ((INTEL_PCH_TYPE(dev_priv) < PCH_DG1) &&
	    (!HAS_PCH_SPLIT(dev_priv) || HAS_PCH_NOP(dev_priv)))
		return;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_DG1)
		hpd->pch_hpd = hpd_sde_dg1;
	else if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		hpd->pch_hpd = hpd_icp;
	else if (HAS_PCH_CNP(dev_priv) || HAS_PCH_SPT(dev_priv))
		hpd->pch_hpd = hpd_spt;
	else if (HAS_PCH_LPT(dev_priv) || HAS_PCH_CPT(dev_priv))
		hpd->pch_hpd = hpd_cpt;
	else if (HAS_PCH_IBX(dev_priv))
		hpd->pch_hpd = hpd_ibx;
	else
		MISSING_CASE(INTEL_PCH_TYPE(dev_priv));
}

static void
intel_handle_vblank(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);

	drm_crtc_handle_vblank(&crtc->base);
}

void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
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

void gen2_irq_reset(struct intel_uncore *uncore)
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

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
static void gen3_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
{
	u32 val = intel_uncore_read(uncore, reg);

	if (val == 0)
		return;

	drm_WARN(&uncore->i915->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
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

	drm_WARN(&uncore->i915->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 i915_mmio_reg_offset(GEN2_IIR), val);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
	intel_uncore_write16(uncore, GEN2_IIR, 0xffff);
	intel_uncore_posting_read16(uncore, GEN2_IIR);
}

void gen3_irq_init(struct intel_uncore *uncore,
		   i915_reg_t imr, u32 imr_val,
		   i915_reg_t ier, u32 ier_val,
		   i915_reg_t iir)
{
	gen3_assert_iir_is_zero(uncore, iir);

	intel_uncore_write(uncore, ier, ier_val);
	intel_uncore_write(uncore, imr, imr_val);
	intel_uncore_posting_read(uncore, imr);
}

void gen2_irq_init(struct intel_uncore *uncore,
		   u32 imr_val, u32 ier_val)
{
	gen2_assert_iir_is_zero(uncore);

	intel_uncore_write16(uncore, GEN2_IER, ier_val);
	intel_uncore_write16(uncore, GEN2_IMR, imr_val);
	intel_uncore_posting_read16(uncore, GEN2_IMR);
}

/* For display hotplug interrupt */
static inline void
i915_hotplug_interrupt_update_locked(struct drm_i915_private *dev_priv,
				     u32 mask,
				     u32 bits)
{
	u32 val;

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, bits & ~mask);

	val = intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_EN);
	val &= ~mask;
	val |= bits;
	intel_uncore_write(&dev_priv->uncore, PORT_HOTPLUG_EN, val);
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

/**
 * ilk_update_display_irq - update DEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void ilk_update_display_irq(struct drm_i915_private *dev_priv,
				   u32 interrupt_mask, u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	new_val = dev_priv->irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->irq_mask &&
	    !drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv))) {
		dev_priv->irq_mask = new_val;
		intel_uncore_write(&dev_priv->uncore, DEIMR, dev_priv->irq_mask);
		intel_uncore_posting_read(&dev_priv->uncore, DEIMR);
	}
}

void ilk_enable_display_irq(struct drm_i915_private *i915, u32 bits)
{
	ilk_update_display_irq(i915, bits, bits);
}

void ilk_disable_display_irq(struct drm_i915_private *i915, u32 bits)
{
	ilk_update_display_irq(i915, bits, 0);
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

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	old_val = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PORT_IMR);

	new_val = old_val;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != old_val) {
		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PORT_IMR, new_val);
		intel_uncore_posting_read(&dev_priv->uncore, GEN8_DE_PORT_IMR);
	}
}

/**
 * bdw_update_pipe_irq - update DE pipe interrupt
 * @dev_priv: driver private
 * @pipe: pipe whose interrupt to update
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void bdw_update_pipe_irq(struct drm_i915_private *dev_priv,
				enum pipe pipe, u32 interrupt_mask,
				u32 enabled_irq_mask)
{
	u32 new_val;

	lockdep_assert_held(&dev_priv->irq_lock);

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	new_val = dev_priv->de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->de_irq_mask[pipe]) {
		dev_priv->de_irq_mask[pipe] = new_val;
		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
		intel_uncore_posting_read(&dev_priv->uncore, GEN8_DE_PIPE_IMR(pipe));
	}
}

void bdw_enable_pipe_irq(struct drm_i915_private *i915,
			 enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(i915, pipe, bits, bits);
}

void bdw_disable_pipe_irq(struct drm_i915_private *i915,
			  enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(i915, pipe, bits, 0);
}

/**
 * ibx_display_interrupt_update - update SDEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
					 u32 interrupt_mask,
					 u32 enabled_irq_mask)
{
	u32 sdeimr = intel_uncore_read(&dev_priv->uncore, SDEIMR);
	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	drm_WARN_ON(&dev_priv->drm, enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&dev_priv->irq_lock);

	if (drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv)))
		return;

	intel_uncore_write(&dev_priv->uncore, SDEIMR, sdeimr);
	intel_uncore_posting_read(&dev_priv->uncore, SDEIMR);
}

void ibx_enable_display_interrupt(struct drm_i915_private *i915, u32 bits)
{
	ibx_display_interrupt_update(i915, bits, bits);
}

void ibx_disable_display_interrupt(struct drm_i915_private *i915, u32 bits)
{
	ibx_display_interrupt_update(i915, bits, 0);
}

u32 i915_pipestat_enable_mask(struct drm_i915_private *dev_priv,
			      enum pipe pipe)
{
	u32 status_mask = dev_priv->pipestat_irq_mask[pipe];
	u32 enable_mask = status_mask << 16;

	lockdep_assert_held(&dev_priv->irq_lock);

	if (DISPLAY_VER(dev_priv) < 5)
		goto out;

	/*
	 * On pipe A we don't support the PSR interrupt yet,
	 * on pipe B and C the same bit MBZ.
	 */
	if (drm_WARN_ON_ONCE(&dev_priv->drm,
			     status_mask & PIPE_A_PSR_STATUS_VLV))
		return 0;
	/*
	 * On pipe B and C we don't support the PSR interrupt yet, on pipe
	 * A the same bit is for perf counters which we don't use either.
	 */
	if (drm_WARN_ON_ONCE(&dev_priv->drm,
			     status_mask & PIPE_B_PSR_STATUS_VLV))
		return 0;

	enable_mask &= ~(PIPE_FIFO_UNDERRUN_STATUS |
			 SPRITE0_FLIP_DONE_INT_EN_VLV |
			 SPRITE1_FLIP_DONE_INT_EN_VLV);
	if (status_mask & SPRITE0_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE0_FLIP_DONE_INT_EN_VLV;
	if (status_mask & SPRITE1_FLIP_DONE_INT_STATUS_VLV)
		enable_mask |= SPRITE1_FLIP_DONE_INT_EN_VLV;

out:
	drm_WARN_ONCE(&dev_priv->drm,
		      enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
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

	drm_WARN_ONCE(&dev_priv->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == status_mask)
		return;

	dev_priv->pipestat_irq_mask[pipe] |= status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, reg, enable_mask | status_mask);
	intel_uncore_posting_read(&dev_priv->uncore, reg);
}

void i915_disable_pipestat(struct drm_i915_private *dev_priv,
			   enum pipe pipe, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 enable_mask;

	drm_WARN_ONCE(&dev_priv->drm, status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: status_mask=0x%x\n",
		      pipe_name(pipe), status_mask);

	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, !intel_irqs_enabled(dev_priv));

	if ((dev_priv->pipestat_irq_mask[pipe] & status_mask) == 0)
		return;

	dev_priv->pipestat_irq_mask[pipe] &= ~status_mask;
	enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, reg, enable_mask | status_mask);
	intel_uncore_posting_read(&dev_priv->uncore, reg);
}

static bool i915_has_asle(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->display.opregion.asle)
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
	if (DISPLAY_VER(dev_priv) >= 4)
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
		high1 = intel_de_read_fw(dev_priv, high_frame) & PIPE_FRAME_HIGH_MASK;
		low   = intel_de_read_fw(dev_priv, low_frame);
		high2 = intel_de_read_fw(dev_priv, high_frame) & PIPE_FRAME_HIGH_MASK;
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
	struct drm_vblank_crtc *vblank = &dev_priv->drm.vblank[drm_crtc_index(crtc)];
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	if (!vblank->max_vblank_count)
		return 0;

	return intel_uncore_read(&dev_priv->uncore, PIPE_FRMCOUNT_G4X(pipe));
}

static u32 intel_crtc_scanlines_since_frame_timestamp(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct drm_vblank_crtc *vblank =
		&crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	const struct drm_display_mode *mode = &vblank->hwmode;
	u32 htotal = mode->crtc_htotal;
	u32 clock = mode->crtc_clock;
	u32 scan_prev_time, scan_curr_time, scan_post_time;

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
		scan_prev_time = intel_de_read_fw(dev_priv,
						  PIPE_FRMTMSTMP(crtc->pipe));

		/*
		 * The TIMESTAMP_CTR register has the current
		 * time stamp value.
		 */
		scan_curr_time = intel_de_read_fw(dev_priv, IVB_TIMESTAMP_CTR);

		scan_post_time = intel_de_read_fw(dev_priv,
						  PIPE_FRMTMSTMP(crtc->pipe));
	} while (scan_post_time != scan_prev_time);

	return div_u64(mul_u32_u32(scan_curr_time - scan_prev_time,
				   clock), 1000 * htotal);
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
	struct drm_vblank_crtc *vblank =
		&crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	const struct drm_display_mode *mode = &vblank->hwmode;
	u32 vblank_start = mode->crtc_vblank_start;
	u32 vtotal = mode->crtc_vtotal;
	u32 scanline;

	scanline = intel_crtc_scanlines_since_frame_timestamp(crtc);
	scanline = min(scanline, vtotal - 1);
	scanline = (scanline + vblank_start) % vtotal;

	return scanline;
}

/*
 * intel_de_read_fw(), only for fast reads of display block, no need for
 * forcewake etc.
 */
static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct drm_display_mode *mode;
	struct drm_vblank_crtc *vblank;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	if (!crtc->active)
		return 0;

	vblank = &crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	mode = &vblank->hwmode;

	if (crtc->mode_flags & I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP)
		return __intel_get_crtc_scanline_from_timestamp(crtc);

	vtotal = mode->crtc_vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	position = intel_de_read_fw(dev_priv, PIPEDSL(pipe)) & PIPEDSL_LINE_MASK;

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
			temp = intel_de_read_fw(dev_priv, PIPEDSL(pipe)) & PIPEDSL_LINE_MASK;
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

static bool i915_get_crtc_scanoutpos(struct drm_crtc *_crtc,
				     bool in_vblank_irq,
				     int *vpos, int *hpos,
				     ktime_t *stime, ktime_t *etime,
				     const struct drm_display_mode *mode)
{
	struct drm_device *dev = _crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	enum pipe pipe = crtc->pipe;
	int position;
	int vbl_start, vbl_end, hsync_start, htotal, vtotal;
	unsigned long irqflags;
	bool use_scanline_counter = DISPLAY_VER(dev_priv) >= 5 ||
		IS_G4X(dev_priv) || DISPLAY_VER(dev_priv) == 2 ||
		crtc->mode_flags & I915_MODE_FLAG_USE_SCANLINE_COUNTER;

	if (drm_WARN_ON(&dev_priv->drm, !mode->crtc_clock)) {
		drm_dbg(&dev_priv->drm,
			"trying to get scanoutpos for disabled "
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

	if (crtc->mode_flags & I915_MODE_FLAG_VRR) {
		int scanlines = intel_crtc_scanlines_since_frame_timestamp(crtc);

		position = __intel_get_crtc_scanline(crtc);

		/*
		 * Already exiting vblank? If so, shift our position
		 * so it looks like we're already apporaching the full
		 * vblank end. This should make the generated timestamp
		 * more or less match when the active portion will start.
		 */
		if (position >= vbl_start && scanlines < position)
			position = min(crtc->vmax_vblank_start + scanlines, vtotal - 1);
	} else if (use_scanline_counter) {
		/* No obvious pixelcount register. Only query vertical
		 * scanout position from Display scan line register.
		 */
		position = __intel_get_crtc_scanline(crtc);
	} else {
		/* Have access to pixelcount since start of frame.
		 * We can split this into vertical and horizontal
		 * scanout position.
		 */
		position = (intel_de_read_fw(dev_priv, PIPEFRAMEPIXEL(pipe)) & PIPE_PIXEL_MASK) >> PIPE_PIXEL_SHIFT;

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

bool intel_crtc_get_vblank_timestamp(struct drm_crtc *crtc, int *max_error,
				     ktime_t *vblank_time, bool in_vblank_irq)
{
	return drm_crtc_vblank_helper_get_vblank_timestamp_internal(
		crtc, max_error, vblank_time, in_vblank_irq,
		i915_get_crtc_scanoutpos);
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

/**
 * ivb_parity_work - Workqueue called when a parity error interrupt
 * occurred.
 * @work: workqueue struct
 *
 * Doesn't actually do anything except notify userspace. As a consequence of
 * this event, userspace should try to remap the bad rows since statistically
 * it is likely the same row is more likely to go bad again.
 */
static void ivb_parity_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), l3_parity.error_work);
	struct intel_gt *gt = to_gt(dev_priv);
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
	if (drm_WARN_ON(&dev_priv->drm, !dev_priv->l3_parity.which_slice))
		goto out;

	misccpctl = intel_uncore_read(&dev_priv->uncore, GEN7_MISCCPCTL);
	intel_uncore_write(&dev_priv->uncore, GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);
	intel_uncore_posting_read(&dev_priv->uncore, GEN7_MISCCPCTL);

	while ((slice = ffs(dev_priv->l3_parity.which_slice)) != 0) {
		i915_reg_t reg;

		slice--;
		if (drm_WARN_ON_ONCE(&dev_priv->drm,
				     slice >= NUM_L3_SLICES(dev_priv)))
			break;

		dev_priv->l3_parity.which_slice &= ~(1<<slice);

		reg = GEN7_L3CDERRST1(slice);

		error_status = intel_uncore_read(&dev_priv->uncore, reg);
		row = GEN7_PARITY_ERROR_ROW(error_status);
		bank = GEN7_PARITY_ERROR_BANK(error_status);
		subbank = GEN7_PARITY_ERROR_SUBBANK(error_status);

		intel_uncore_write(&dev_priv->uncore, reg, GEN7_PARITY_ERROR_VALID | GEN7_L3CDERRST1_ENABLE);
		intel_uncore_posting_read(&dev_priv->uncore, reg);

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

	intel_uncore_write(&dev_priv->uncore, GEN7_MISCCPCTL, misccpctl);

out:
	drm_WARN_ON(&dev_priv->drm, dev_priv->l3_parity.which_slice);
	spin_lock_irq(gt->irq_lock);
	gen5_gt_enable_irq(gt, GT_PARITY_ERROR(dev_priv));
	spin_unlock_irq(gt->irq_lock);

	mutex_unlock(&dev_priv->drm.struct_mutex);
}

static bool gen11_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(pin);
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
	case HPD_PORT_B:
	case HPD_PORT_C:
	case HPD_PORT_D:
		return val & SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(pin);
	default:
		return false;
	}
}

static bool icp_tc_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return val & ICP_TC_HPD_LONG_DETECT(pin);
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

	BUILD_BUG_ON(BITS_PER_TYPE(*pin_mask) < HPD_NUM_PINS);

	for_each_hpd_pin(pin) {
		if ((hpd[pin] & hotplug_trigger) == 0)
			continue;

		*pin_mask |= BIT(pin);

		if (long_pulse_detect(pin, dig_hotplug_reg))
			*long_mask |= BIT(pin);
	}

	drm_dbg(&dev_priv->drm,
		"hotplug event received, stat 0x%08x, dig 0x%08x, pins 0x%08x, long 0x%08x\n",
		hotplug_trigger, dig_hotplug_reg, *pin_mask, *long_mask);

}

static u32 intel_hpd_enabled_irqs(struct drm_i915_private *dev_priv,
				  const u32 hpd[HPD_NUM_PINS])
{
	struct intel_encoder *encoder;
	u32 enabled_irqs = 0;

	for_each_intel_encoder(&dev_priv->drm, encoder)
		if (dev_priv->display.hotplug.stats[encoder->hpd_pin].state == HPD_ENABLED)
			enabled_irqs |= hpd[encoder->hpd_pin];

	return enabled_irqs;
}

static u32 intel_hpd_hotplug_irqs(struct drm_i915_private *dev_priv,
				  const u32 hpd[HPD_NUM_PINS])
{
	struct intel_encoder *encoder;
	u32 hotplug_irqs = 0;

	for_each_intel_encoder(&dev_priv->drm, encoder)
		hotplug_irqs |= hpd[encoder->hpd_pin];

	return hotplug_irqs;
}

static u32 intel_hpd_hotplug_enables(struct drm_i915_private *i915,
				     hotplug_enables_func hotplug_enables)
{
	struct intel_encoder *encoder;
	u32 hotplug = 0;

	for_each_intel_encoder(&i915->drm, encoder)
		hotplug |= hotplug_enables(i915, encoder->hpd_pin);

	return hotplug;
}

static void gmbus_irq_handler(struct drm_i915_private *dev_priv)
{
	wake_up_all(&dev_priv->display.gmbus.wait_queue);
}

static void dp_aux_irq_handler(struct drm_i915_private *dev_priv)
{
	wake_up_all(&dev_priv->display.gmbus.wait_queue);
}

#if defined(CONFIG_DEBUG_FS)
static void display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe,
					 u32 crc0, u32 crc1,
					 u32 crc2, u32 crc3,
					 u32 crc4)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;
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
	    (DISPLAY_VER(dev_priv) >= 8 && pipe_crc->skipped == 1)) {
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

static void flip_done_handler(struct drm_i915_private *i915,
			      enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(i915, pipe);
	struct drm_crtc_state *crtc_state = crtc->base.state;
	struct drm_pending_vblank_event *e = crtc_state->event;
	struct drm_device *dev = &i915->drm;
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);

	crtc_state->event = NULL;

	drm_crtc_send_vblank_event(&crtc->base, e);

	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}

static void hsw_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_1_IVB(pipe)),
				     0, 0, 0, 0);
}

static void ivb_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_1_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_2_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_3_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_4_IVB(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_5_IVB(pipe)));
}

static void i9xx_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	u32 res1, res2;

	if (DISPLAY_VER(dev_priv) >= 3)
		res1 = intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RES1_I915(pipe));
	else
		res1 = 0;

	if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		res2 = intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RES2_G4X(pipe));
	else
		res2 = 0;

	display_pipe_crc_irq_handler(dev_priv, pipe,
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_RED(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_GREEN(pipe)),
				     intel_uncore_read(&dev_priv->uncore, PIPE_CRC_RES_BLUE(pipe)),
				     res1, res2);
}

static void i9xx_pipestat_irq_reset(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		intel_uncore_write(&dev_priv->uncore, PIPESTAT(pipe),
			   PIPESTAT_INT_STATUS_MASK |
			   PIPE_FIFO_UNDERRUN_STATUS);

		dev_priv->pipestat_irq_mask[pipe] = 0;
	}
}

static void i9xx_pipestat_irq_ack(struct drm_i915_private *dev_priv,
				  u32 iir, u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

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
		default:
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
		pipe_stats[pipe] = intel_uncore_read(&dev_priv->uncore, reg) & status_mask;
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
			intel_uncore_write(&dev_priv->uncore, reg, pipe_stats[pipe]);
			intel_uncore_write(&dev_priv->uncore, reg, enable_mask);
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
			intel_handle_vblank(dev_priv, pipe);

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
			intel_handle_vblank(dev_priv, pipe);

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
			intel_handle_vblank(dev_priv, pipe);

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
			intel_handle_vblank(dev_priv, pipe);

		if (pipe_stats[pipe] & PLANE_FLIP_DONE_INT_STATUS_VLV)
			flip_done_handler(dev_priv, pipe);

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
		u32 tmp = intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT) & hotplug_status_mask;

		if (tmp == 0)
			return hotplug_status;

		hotplug_status |= tmp;
		intel_uncore_write(&dev_priv->uncore, PORT_HOTPLUG_STAT, hotplug_status);
	}

	drm_WARN_ONCE(&dev_priv->drm, 1,
		      "PORT_HOTPLUG_STAT did not clear (0x%08x)\n",
		      intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT));

	return hotplug_status;
}

static void i9xx_hpd_irq_handler(struct drm_i915_private *dev_priv,
				 u32 hotplug_status)
{
	u32 pin_mask = 0, long_mask = 0;
	u32 hotplug_trigger;

	if (IS_G4X(dev_priv) ||
	    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_G4X;
	else
		hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

	if (hotplug_trigger) {
		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug_trigger, hotplug_trigger,
				   dev_priv->display.hotplug.hpd,
				   i9xx_port_hotplug_long_detect);

		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
	}

	if ((IS_G4X(dev_priv) ||
	     IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    hotplug_status & DP_AUX_CHANNEL_MASK_INT_STATUS_G4X)
		dp_aux_irq_handler(dev_priv);
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

		gt_iir = intel_uncore_read(&dev_priv->uncore, GTIIR);
		pm_iir = intel_uncore_read(&dev_priv->uncore, GEN6_PMIIR);
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

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
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
		ier = intel_uncore_read(&dev_priv->uncore, VLV_IER);
		intel_uncore_write(&dev_priv->uncore, VLV_IER, 0);

		if (gt_iir)
			intel_uncore_write(&dev_priv->uncore, GTIIR, gt_iir);
		if (pm_iir)
			intel_uncore_write(&dev_priv->uncore, GEN6_PMIIR, pm_iir);

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
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

		if (gt_iir)
			gen6_gt_irq_handler(to_gt(dev_priv), gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(&to_gt(dev_priv)->rps, pm_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

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
		u32 ier = 0;

		master_ctl = intel_uncore_read(&dev_priv->uncore, GEN8_MASTER_IRQ) & ~GEN8_MASTER_IRQ_CONTROL;
		iir = intel_uncore_read(&dev_priv->uncore, VLV_IIR);

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
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, 0);
		ier = intel_uncore_read(&dev_priv->uncore, VLV_IER);
		intel_uncore_write(&dev_priv->uncore, VLV_IER, 0);

		gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

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
			intel_uncore_write(&dev_priv->uncore, VLV_IIR, iir);

		intel_uncore_write(&dev_priv->uncore, VLV_IER, ier);
		intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void ibx_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	/*
	 * Somehow the PCH doesn't seem to really ack the interrupt to the CPU
	 * unless we touch the hotplug register, even if hotplug_trigger is
	 * zero. Not acking leads to "The master control interrupt lied (SDE)!"
	 * errors.
	 */
	dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	if (!hotplug_trigger) {
		u32 mask = PORTA_HOTPLUG_STATUS_MASK |
			PORTD_HOTPLUG_STATUS_MASK |
			PORTC_HOTPLUG_STATUS_MASK |
			PORTB_HOTPLUG_STATUS_MASK;
		dig_hotplug_reg &= ~mask;
	}

	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, dig_hotplug_reg);
	if (!hotplug_trigger)
		return;

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.pch_hpd,
			   pch_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

static void ibx_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK) >>
			       SDE_AUDIO_POWER_SHIFT);
		drm_dbg(&dev_priv->drm, "PCH audio power change on port %d\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK)
		dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS)
		gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_HDCP_MASK)
		drm_dbg(&dev_priv->drm, "PCH HDCP audio interrupt\n");

	if (pch_iir & SDE_AUDIO_TRANS_MASK)
		drm_dbg(&dev_priv->drm, "PCH transcoder audio interrupt\n");

	if (pch_iir & SDE_POISON)
		drm_err(&dev_priv->drm, "PCH poison interrupt\n");

	if (pch_iir & SDE_FDI_MASK) {
		for_each_pipe(dev_priv, pipe)
			drm_dbg(&dev_priv->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_uncore_read(&dev_priv->uncore, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & (SDE_TRANSB_CRC_DONE | SDE_TRANSA_CRC_DONE))
		drm_dbg(&dev_priv->drm, "PCH transcoder CRC done interrupt\n");

	if (pch_iir & (SDE_TRANSB_CRC_ERR | SDE_TRANSA_CRC_ERR))
		drm_dbg(&dev_priv->drm,
			"PCH transcoder CRC error interrupt\n");

	if (pch_iir & SDE_TRANSA_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_A);

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, PIPE_B);
}

static void ivb_err_int_handler(struct drm_i915_private *dev_priv)
{
	u32 err_int = intel_uncore_read(&dev_priv->uncore, GEN7_ERR_INT);
	enum pipe pipe;

	if (err_int & ERR_INT_POISON)
		drm_err(&dev_priv->drm, "Poison interrupt\n");

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

	intel_uncore_write(&dev_priv->uncore, GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct drm_i915_private *dev_priv)
{
	u32 serr_int = intel_uncore_read(&dev_priv->uncore, SERR_INT);
	enum pipe pipe;

	if (serr_int & SERR_INT_POISON)
		drm_err(&dev_priv->drm, "PCH poison interrupt\n");

	for_each_pipe(dev_priv, pipe)
		if (serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pipe))
			intel_pch_fifo_underrun_irq_handler(dev_priv, pipe);

	intel_uncore_write(&dev_priv->uncore, SERR_INT, serr_int);
}

static void cpt_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;

	ibx_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (pch_iir & SDE_AUDIO_POWER_MASK_CPT) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK_CPT) >>
			       SDE_AUDIO_POWER_SHIFT_CPT);
		drm_dbg(&dev_priv->drm, "PCH audio power change on port %c\n",
			port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK_CPT)
		dp_aux_irq_handler(dev_priv);

	if (pch_iir & SDE_GMBUS_CPT)
		gmbus_irq_handler(dev_priv);

	if (pch_iir & SDE_AUDIO_CP_REQ_CPT)
		drm_dbg(&dev_priv->drm, "Audio CP request interrupt\n");

	if (pch_iir & SDE_AUDIO_CP_CHG_CPT)
		drm_dbg(&dev_priv->drm, "Audio CP change interrupt\n");

	if (pch_iir & SDE_FDI_MASK_CPT) {
		for_each_pipe(dev_priv, pipe)
			drm_dbg(&dev_priv->drm, "  pipe %c FDI IIR: 0x%08x\n",
				pipe_name(pipe),
				intel_uncore_read(&dev_priv->uncore, FDI_RX_IIR(pipe)));
	}

	if (pch_iir & SDE_ERROR_CPT)
		cpt_serr_int_handler(dev_priv);
}

static void icp_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	u32 ddi_hotplug_trigger = pch_iir & SDE_DDI_HOTPLUG_MASK_ICP;
	u32 tc_hotplug_trigger = pch_iir & SDE_TC_HOTPLUG_MASK_ICP;
	u32 pin_mask = 0, long_mask = 0;

	if (ddi_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, SHOTPLUG_CTL_DDI);
		intel_uncore_write(&dev_priv->uncore, SHOTPLUG_CTL_DDI, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   ddi_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   icp_ddi_port_hotplug_long_detect);
	}

	if (tc_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, SHOTPLUG_CTL_TC);
		intel_uncore_write(&dev_priv->uncore, SHOTPLUG_CTL_TC, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   tc_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
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

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
		intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   spt_port_hotplug_long_detect);
	}

	if (hotplug2_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG2);
		intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG2, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug2_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   spt_port_hotplug2_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_CPT)
		gmbus_irq_handler(dev_priv);
}

static void ilk_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL);
	intel_uncore_write(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL, dig_hotplug_reg);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.hpd,
			   ilk_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

static void ilk_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (de_iir & DE_AUX_CHANNEL_A)
		dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(dev_priv);

	if (de_iir & DE_POISON)
		drm_err(&dev_priv->drm, "Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK(pipe))
			intel_handle_vblank(dev_priv, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE(pipe))
			flip_done_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);
	}

	/* check event from PCH */
	if (de_iir & DE_PCH_EVENT) {
		u32 pch_iir = intel_uncore_read(&dev_priv->uncore, SDEIIR);

		if (HAS_PCH_CPT(dev_priv))
			cpt_irq_handler(dev_priv, pch_iir);
		else
			ibx_irq_handler(dev_priv, pch_iir);

		/* should clear PCH hotplug event before clear CPU irq */
		intel_uncore_write(&dev_priv->uncore, SDEIIR, pch_iir);
	}

	if (DISPLAY_VER(dev_priv) == 5 && de_iir & DE_PCU_EVENT)
		gen5_rps_irq_handler(&to_gt(dev_priv)->rps);
}

static void ivb_display_irq_handler(struct drm_i915_private *dev_priv,
				    u32 de_iir)
{
	enum pipe pipe;
	u32 hotplug_trigger = de_iir & DE_DP_A_HOTPLUG_IVB;

	if (hotplug_trigger)
		ilk_hpd_irq_handler(dev_priv, hotplug_trigger);

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(dev_priv);

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev_priv);

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK_IVB(pipe))
			intel_handle_vblank(dev_priv, pipe);

		if (de_iir & DE_PLANE_FLIP_DONE_IVB(pipe))
			flip_done_handler(dev_priv, pipe);
	}

	/* check event from PCH */
	if (!HAS_PCH_NOP(dev_priv) && (de_iir & DE_PCH_EVENT_IVB)) {
		u32 pch_iir = intel_uncore_read(&dev_priv->uncore, SDEIIR);

		cpt_irq_handler(dev_priv, pch_iir);

		/* clear PCH hotplug event before clear CPU irq */
		intel_uncore_write(&dev_priv->uncore, SDEIIR, pch_iir);
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
static irqreturn_t ilk_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	void __iomem * const regs = i915->uncore.regs;
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	if (unlikely(!intel_irqs_enabled(i915)))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	/* disable master interrupt before clearing iir  */
	de_ier = raw_reg_read(regs, DEIER);
	raw_reg_write(regs, DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(i915)) {
		sde_ier = raw_reg_read(regs, SDEIER);
		raw_reg_write(regs, SDEIER, 0);
	}

	/* Find, clear, then process each source of interrupt */

	gt_iir = raw_reg_read(regs, GTIIR);
	if (gt_iir) {
		raw_reg_write(regs, GTIIR, gt_iir);
		if (GRAPHICS_VER(i915) >= 6)
			gen6_gt_irq_handler(to_gt(i915), gt_iir);
		else
			gen5_gt_irq_handler(to_gt(i915), gt_iir);
		ret = IRQ_HANDLED;
	}

	de_iir = raw_reg_read(regs, DEIIR);
	if (de_iir) {
		raw_reg_write(regs, DEIIR, de_iir);
		if (DISPLAY_VER(i915) >= 7)
			ivb_display_irq_handler(i915, de_iir);
		else
			ilk_display_irq_handler(i915, de_iir);
		ret = IRQ_HANDLED;
	}

	if (GRAPHICS_VER(i915) >= 6) {
		u32 pm_iir = raw_reg_read(regs, GEN6_PMIIR);
		if (pm_iir) {
			raw_reg_write(regs, GEN6_PMIIR, pm_iir);
			gen6_rps_irq_handler(&to_gt(i915)->rps, pm_iir);
			ret = IRQ_HANDLED;
		}
	}

	raw_reg_write(regs, DEIER, de_ier);
	if (sde_ier)
		raw_reg_write(regs, SDEIER, sde_ier);

	pmu_irq_stats(i915, ret);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	return ret;
}

static void bxt_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, dig_hotplug_reg);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.hpd,
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

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL);
		intel_uncore_write(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   trigger_tc, dig_hotplug_reg,
				   dev_priv->display.hotplug.hpd,
				   gen11_port_hotplug_long_detect);
	}

	if (trigger_tbt) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL);
		intel_uncore_write(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL, dig_hotplug_reg);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   trigger_tbt, dig_hotplug_reg,
				   dev_priv->display.hotplug.hpd,
				   gen11_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
	else
		drm_err(&dev_priv->drm,
			"Unexpected DE HPD interrupt 0x%08x\n", iir);
}

static u32 gen8_de_port_aux_mask(struct drm_i915_private *dev_priv)
{
	u32 mask;

	if (DISPLAY_VER(dev_priv) >= 13)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC |
			XELPD_DE_PORT_AUX_DDID |
			XELPD_DE_PORT_AUX_DDIE |
			TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 |
			TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4;
	else if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DE_PORT_AUX_DDIA |
			TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC |
			TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 |
			TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4 |
			TGL_DE_PORT_AUX_USBC5 |
			TGL_DE_PORT_AUX_USBC6;


	mask = GEN8_AUX_CHANNEL_A;
	if (DISPLAY_VER(dev_priv) >= 9)
		mask |= GEN9_AUX_CHANNEL_B |
			GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;

	if (DISPLAY_VER(dev_priv) == 11) {
		mask |= ICL_AUX_CHANNEL_F;
		mask |= ICL_AUX_CHANNEL_E;
	}

	return mask;
}

static u32 gen8_de_pipe_fault_mask(struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 13 || HAS_D12_PLANE_MINIMIZATION(dev_priv))
		return RKL_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (DISPLAY_VER(dev_priv) >= 11)
		return GEN11_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (DISPLAY_VER(dev_priv) >= 9)
		return GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
	else
		return GEN8_DE_PIPE_IRQ_FAULT_ERRORS;
}

static void
gen8_de_misc_irq_handler(struct drm_i915_private *dev_priv, u32 iir)
{
	bool found = false;

	if (iir & GEN8_DE_MISC_GSE) {
		intel_opregion_asle_intr(dev_priv);
		found = true;
	}

	if (iir & GEN8_DE_EDP_PSR) {
		struct intel_encoder *encoder;
		u32 psr_iir;
		i915_reg_t iir_reg;

		for_each_intel_encoder_with_psr(&dev_priv->drm, encoder) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

			if (DISPLAY_VER(dev_priv) >= 12)
				iir_reg = TRANS_PSR_IIR(intel_dp->psr.transcoder);
			else
				iir_reg = EDP_PSR_IIR;

			psr_iir = intel_uncore_read(&dev_priv->uncore, iir_reg);
			intel_uncore_write(&dev_priv->uncore, iir_reg, psr_iir);

			if (psr_iir)
				found = true;

			intel_psr_irq_handler(intel_dp, psr_iir);

			/* prior GEN12 only have one EDP PSR */
			if (DISPLAY_VER(dev_priv) < 12)
				break;
		}
	}

	if (!found)
		drm_err(&dev_priv->drm, "Unexpected DE Misc interrupt\n");
}

static void gen11_dsi_te_interrupt_handler(struct drm_i915_private *dev_priv,
					   u32 te_trigger)
{
	enum pipe pipe = INVALID_PIPE;
	enum transcoder dsi_trans;
	enum port port;
	u32 val, tmp;

	/*
	 * Incase of dual link, TE comes from DSI_1
	 * this is to check if dual link is enabled
	 */
	val = intel_uncore_read(&dev_priv->uncore, TRANS_DDI_FUNC_CTL2(TRANSCODER_DSI_0));
	val &= PORT_SYNC_MODE_ENABLE;

	/*
	 * if dual link is enabled, then read DSI_0
	 * transcoder registers
	 */
	port = ((te_trigger & DSI1_TE && val) || (te_trigger & DSI0_TE)) ?
						  PORT_A : PORT_B;
	dsi_trans = (port == PORT_A) ? TRANSCODER_DSI_0 : TRANSCODER_DSI_1;

	/* Check if DSI configured in command mode */
	val = intel_uncore_read(&dev_priv->uncore, DSI_TRANS_FUNC_CONF(dsi_trans));
	val = val & OP_MODE_MASK;

	if (val != CMD_MODE_NO_GATE && val != CMD_MODE_TE_GATE) {
		drm_err(&dev_priv->drm, "DSI trancoder not configured in command mode\n");
		return;
	}

	/* Get PIPE for handling VBLANK event */
	val = intel_uncore_read(&dev_priv->uncore, TRANS_DDI_FUNC_CTL(dsi_trans));
	switch (val & TRANS_DDI_EDP_INPUT_MASK) {
	case TRANS_DDI_EDP_INPUT_A_ON:
		pipe = PIPE_A;
		break;
	case TRANS_DDI_EDP_INPUT_B_ONOFF:
		pipe = PIPE_B;
		break;
	case TRANS_DDI_EDP_INPUT_C_ONOFF:
		pipe = PIPE_C;
		break;
	default:
		drm_err(&dev_priv->drm, "Invalid PIPE\n");
		return;
	}

	intel_handle_vblank(dev_priv, pipe);

	/* clear TE in dsi IIR */
	port = (te_trigger & DSI1_TE) ? PORT_B : PORT_A;
	tmp = intel_uncore_read(&dev_priv->uncore, DSI_INTR_IDENT_REG(port));
	intel_uncore_write(&dev_priv->uncore, DSI_INTR_IDENT_REG(port), tmp);
}

static u32 gen8_de_pipe_flip_done_mask(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 9)
		return GEN9_PIPE_PLANE1_FLIP_DONE;
	else
		return GEN8_PIPE_PRIMARY_FLIP_DONE;
}

u32 gen8_de_pipe_underrun_mask(struct drm_i915_private *dev_priv)
{
	u32 mask = GEN8_PIPE_FIFO_UNDERRUN;

	if (DISPLAY_VER(dev_priv) >= 13)
		mask |= XELPD_PIPE_SOFT_UNDERRUN |
			XELPD_PIPE_HARD_UNDERRUN;

	return mask;
}

static irqreturn_t
gen8_de_irq_handler(struct drm_i915_private *dev_priv, u32 master_ctl)
{
	irqreturn_t ret = IRQ_NONE;
	u32 iir;
	enum pipe pipe;

	drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_DISPLAY(dev_priv));

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_MISC_IIR);
		if (iir) {
			intel_uncore_write(&dev_priv->uncore, GEN8_DE_MISC_IIR, iir);
			ret = IRQ_HANDLED;
			gen8_de_misc_irq_handler(dev_priv, iir);
		} else {
			drm_err(&dev_priv->drm,
				"The master control interrupt lied (DE MISC)!\n");
		}
	}

	if (DISPLAY_VER(dev_priv) >= 11 && (master_ctl & GEN11_DE_HPD_IRQ)) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN11_DE_HPD_IIR);
		if (iir) {
			intel_uncore_write(&dev_priv->uncore, GEN11_DE_HPD_IIR, iir);
			ret = IRQ_HANDLED;
			gen11_hpd_irq_handler(dev_priv, iir);
		} else {
			drm_err(&dev_priv->drm,
				"The master control interrupt lied, (DE HPD)!\n");
		}
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PORT_IIR);
		if (iir) {
			bool found = false;

			intel_uncore_write(&dev_priv->uncore, GEN8_DE_PORT_IIR, iir);
			ret = IRQ_HANDLED;

			if (iir & gen8_de_port_aux_mask(dev_priv)) {
				dp_aux_irq_handler(dev_priv);
				found = true;
			}

			if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
				u32 hotplug_trigger = iir & BXT_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					bxt_hpd_irq_handler(dev_priv, hotplug_trigger);
					found = true;
				}
			} else if (IS_BROADWELL(dev_priv)) {
				u32 hotplug_trigger = iir & BDW_DE_PORT_HOTPLUG_MASK;

				if (hotplug_trigger) {
					ilk_hpd_irq_handler(dev_priv, hotplug_trigger);
					found = true;
				}
			}

			if ((IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) &&
			    (iir & BXT_DE_PORT_GMBUS)) {
				gmbus_irq_handler(dev_priv);
				found = true;
			}

			if (DISPLAY_VER(dev_priv) >= 11) {
				u32 te_trigger = iir & (DSI0_TE | DSI1_TE);

				if (te_trigger) {
					gen11_dsi_te_interrupt_handler(dev_priv, te_trigger);
					found = true;
				}
			}

			if (!found)
				drm_err(&dev_priv->drm,
					"Unexpected DE Port interrupt\n");
		}
		else
			drm_err(&dev_priv->drm,
				"The master control interrupt lied (DE PORT)!\n");
	}

	for_each_pipe(dev_priv, pipe) {
		u32 fault_errors;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = intel_uncore_read(&dev_priv->uncore, GEN8_DE_PIPE_IIR(pipe));
		if (!iir) {
			drm_err(&dev_priv->drm,
				"The master control interrupt lied (DE PIPE)!\n");
			continue;
		}

		ret = IRQ_HANDLED;
		intel_uncore_write(&dev_priv->uncore, GEN8_DE_PIPE_IIR(pipe), iir);

		if (iir & GEN8_PIPE_VBLANK)
			intel_handle_vblank(dev_priv, pipe);

		if (iir & gen8_de_pipe_flip_done_mask(dev_priv))
			flip_done_handler(dev_priv, pipe);

		if (iir & GEN8_PIPE_CDCLK_CRC_DONE)
			hsw_pipe_crc_irq_handler(dev_priv, pipe);

		if (iir & gen8_de_pipe_underrun_mask(dev_priv))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		fault_errors = iir & gen8_de_pipe_fault_mask(dev_priv);
		if (fault_errors)
			drm_err(&dev_priv->drm,
				"Fault errors on pipe %c: 0x%08x\n",
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
		iir = intel_uncore_read(&dev_priv->uncore, SDEIIR);
		if (iir) {
			intel_uncore_write(&dev_priv->uncore, SDEIIR, iir);
			ret = IRQ_HANDLED;

			if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
				icp_irq_handler(dev_priv, iir);
			else if (INTEL_PCH_TYPE(dev_priv) >= PCH_SPT)
				spt_irq_handler(dev_priv, iir);
			else
				cpt_irq_handler(dev_priv, iir);
		} else {
			/*
			 * Like on previous PCH there seems to be something
			 * fishy going on with forwarding PCH interrupts.
			 */
			drm_dbg(&dev_priv->drm,
				"The master control interrupt lied (SDE)!\n");
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

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	master_ctl = gen8_master_intr_disable(regs);
	if (!master_ctl) {
		gen8_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen8_gt_irq_handler(to_gt(dev_priv), master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & ~GEN8_GT_IRQS) {
		disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
		gen8_de_irq_handler(dev_priv, master_ctl);
		enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
	}

	gen8_master_intr_enable(regs);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static u32
gen11_gu_misc_irq_ack(struct drm_i915_private *i915, const u32 master_ctl)
{
	void __iomem * const regs = i915->uncore.regs;
	u32 iir;

	if (!(master_ctl & GEN11_GU_MISC_IRQ))
		return 0;

	iir = raw_reg_read(regs, GEN11_GU_MISC_IIR);
	if (likely(iir))
		raw_reg_write(regs, GEN11_GU_MISC_IIR, iir);

	return iir;
}

static void
gen11_gu_misc_irq_handler(struct drm_i915_private *i915, const u32 iir)
{
	if (iir & GEN11_GU_MISC_GSE)
		intel_opregion_asle_intr(i915);
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

static void
gen11_display_irq_handler(struct drm_i915_private *i915)
{
	void __iomem * const regs = i915->uncore.regs;
	const u32 disp_ctl = raw_reg_read(regs, GEN11_DISPLAY_INT_CTL);

	disable_rpm_wakeref_asserts(&i915->runtime_pm);
	/*
	 * GEN11_DISPLAY_INT_CTL has same format as GEN8_MASTER_IRQ
	 * for the display related bits.
	 */
	raw_reg_write(regs, GEN11_DISPLAY_INT_CTL, 0x0);
	gen8_de_irq_handler(i915, disp_ctl);
	raw_reg_write(regs, GEN11_DISPLAY_INT_CTL,
		      GEN11_DISPLAY_IRQ_ENABLE);

	enable_rpm_wakeref_asserts(&i915->runtime_pm);
}

static irqreturn_t gen11_irq_handler(int irq, void *arg)
{
	struct drm_i915_private *i915 = arg;
	void __iomem * const regs = i915->uncore.regs;
	struct intel_gt *gt = to_gt(i915);
	u32 master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_ctl = gen11_master_intr_disable(regs);
	if (!master_ctl) {
		gen11_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* Find, queue (onto bottom-halves), then clear each source */
	gen11_gt_irq_handler(gt, master_ctl);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(i915);

	gu_misc_iir = gen11_gu_misc_irq_ack(i915, master_ctl);

	gen11_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(i915, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

	return IRQ_HANDLED;
}

static inline u32 dg1_master_intr_disable(void __iomem * const regs)
{
	u32 val;

	/* First disable interrupts */
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, 0);

	/* Get the indication levels and ack the master unit */
	val = raw_reg_read(regs, DG1_MSTR_TILE_INTR);
	if (unlikely(!val))
		return 0;

	raw_reg_write(regs, DG1_MSTR_TILE_INTR, val);

	return val;
}

static inline void dg1_master_intr_enable(void __iomem * const regs)
{
	raw_reg_write(regs, DG1_MSTR_TILE_INTR, DG1_MSTR_IRQ);
}

static irqreturn_t dg1_irq_handler(int irq, void *arg)
{
	struct drm_i915_private * const i915 = arg;
	struct intel_gt *gt = to_gt(i915);
	void __iomem * const regs = gt->uncore->regs;
	u32 master_tile_ctl, master_ctl;
	u32 gu_misc_iir;

	if (!intel_irqs_enabled(i915))
		return IRQ_NONE;

	master_tile_ctl = dg1_master_intr_disable(regs);
	if (!master_tile_ctl) {
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	/* FIXME: we only support tile 0 for now. */
	if (master_tile_ctl & DG1_MSTR_TILE(0)) {
		master_ctl = raw_reg_read(regs, GEN11_GFX_MSTR_IRQ);
		raw_reg_write(regs, GEN11_GFX_MSTR_IRQ, master_ctl);
	} else {
		DRM_ERROR("Tile not supported: 0x%08x\n", master_tile_ctl);
		dg1_master_intr_enable(regs);
		return IRQ_NONE;
	}

	gen11_gt_irq_handler(gt, master_ctl);

	if (master_ctl & GEN11_DISPLAY_IRQ)
		gen11_display_irq_handler(i915);

	gu_misc_iir = gen11_gu_misc_irq_ack(i915, master_ctl);

	dg1_master_intr_enable(regs);

	gen11_gu_misc_irq_handler(i915, gu_misc_iir);

	pmu_irq_stats(i915, IRQ_HANDLED);

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

int i915gm_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	/*
	 * Vblank interrupts fail to wake the device up from C2+.
	 * Disabling render clock gating during C-states avoids
	 * the problem. There is a small power cost so we do this
	 * only when vblank interrupts are actually enabled.
	 */
	if (dev_priv->vblank_enabled++ == 0)
		intel_uncore_write(&dev_priv->uncore, SCPD0, _MASKED_BIT_ENABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));

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
	u32 bit = DISPLAY_VER(dev_priv) >= 7 ?
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

static bool gen11_dsi_configure_te(struct intel_crtc *intel_crtc,
				   bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	enum port port;
	u32 tmp;

	if (!(intel_crtc->mode_flags &
	    (I915_MODE_FLAG_DSI_USE_TE1 | I915_MODE_FLAG_DSI_USE_TE0)))
		return false;

	/* for dual link cases we consider TE from slave */
	if (intel_crtc->mode_flags & I915_MODE_FLAG_DSI_USE_TE1)
		port = PORT_B;
	else
		port = PORT_A;

	tmp =  intel_uncore_read(&dev_priv->uncore, DSI_INTR_MASK_REG(port));
	if (enable)
		tmp &= ~DSI_TE_EVENT;
	else
		tmp |= DSI_TE_EVENT;

	intel_uncore_write(&dev_priv->uncore, DSI_INTR_MASK_REG(port), tmp);

	tmp = intel_uncore_read(&dev_priv->uncore, DSI_INTR_IDENT_REG(port));
	intel_uncore_write(&dev_priv->uncore, DSI_INTR_IDENT_REG(port), tmp);

	return true;
}

int bdw_enable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, true))
		return 0;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_enable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	/* Even if there is no DMC, frame counter can get stuck when
	 * PSR is active as no frames are generated, so check only for PSR.
	 */
	if (HAS_PSR(dev_priv))
		drm_crtc_vblank_restore(&crtc->base);

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

void i915gm_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);

	i8xx_disable_vblank(crtc);

	if (--dev_priv->vblank_enabled == 0)
		intel_uncore_write(&dev_priv->uncore, SCPD0, _MASKED_BIT_DISABLE(CSTATE_RENDER_CLOCK_GATE_DISABLE));
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
	u32 bit = DISPLAY_VER(dev_priv) >= 7 ?
		DE_PIPE_VBLANK_IVB(pipe) : DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void bdw_disable_vblank(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	unsigned long irqflags;

	if (gen11_dsi_configure_te(crtc, false))
		return;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_disable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ibx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (HAS_PCH_NOP(dev_priv))
		return;

	GEN3_IRQ_RESET(uncore, SDE);

	if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		intel_uncore_write(&dev_priv->uncore, SERR_INT, 0xffffffff);
}

static void vlv_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (IS_CHERRYVIEW(dev_priv))
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK_CHV);
	else
		intel_uncore_write(uncore, DPINVGTT, DPINVGTT_STATUS_MASK_VLV);

	i915_hotplug_interrupt_update_locked(dev_priv, 0xffffffff, 0);
	intel_uncore_write(uncore, PORT_HOTPLUG_STAT, intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT));

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

	drm_WARN_ON(&dev_priv->drm, dev_priv->irq_mask != ~0u);

	dev_priv->irq_mask = ~enable_mask;

	GEN3_IRQ_INIT(uncore, VLV_, dev_priv->irq_mask, enable_mask);
}

/* drm_dma.h hooks
*/
static void ilk_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	GEN3_IRQ_RESET(uncore, DE);
	dev_priv->irq_mask = ~0u;

	if (GRAPHICS_VER(dev_priv) == 7)
		intel_uncore_write(uncore, GEN7_ERR_INT, 0xffffffff);

	if (IS_HASWELL(dev_priv)) {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	gen5_gt_irq_reset(to_gt(dev_priv));

	ibx_irq_reset(dev_priv);
}

static void valleyview_irq_reset(struct drm_i915_private *dev_priv)
{
	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, 0);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);

	gen5_gt_irq_reset(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void gen8_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;

	if (!HAS_DISPLAY(dev_priv))
		return;

	intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
	intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);
}

static void gen8_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	gen8_master_intr_disable(dev_priv->uncore.regs);

	gen8_gt_irq_reset(to_gt(dev_priv));
	gen8_display_irq_reset(dev_priv);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_reset(dev_priv);

}

static void gen11_display_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	enum pipe pipe;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);

	if (!HAS_DISPLAY(dev_priv))
		return;

	intel_uncore_write(uncore, GEN11_DISPLAY_INT_CTL, 0);

	if (DISPLAY_VER(dev_priv) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(dev_priv, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(dev_priv, domain))
				continue;

			intel_uncore_write(uncore, TRANS_PSR_IMR(trans), 0xffffffff);
			intel_uncore_write(uncore, TRANS_PSR_IIR(trans), 0xffffffff);
		}
	} else {
		intel_uncore_write(uncore, EDP_PSR_IMR, 0xffffffff);
		intel_uncore_write(uncore, EDP_PSR_IIR, 0xffffffff);
	}

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe);

	GEN3_IRQ_RESET(uncore, GEN8_DE_PORT_);
	GEN3_IRQ_RESET(uncore, GEN8_DE_MISC_);
	GEN3_IRQ_RESET(uncore, GEN11_DE_HPD_);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		GEN3_IRQ_RESET(uncore, SDE);
}

static void gen11_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;

	gen11_master_intr_disable(dev_priv->uncore.regs);

	gen11_gt_irq_reset(gt);
	gen11_display_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN11_GU_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);
}

static void dg1_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;

	dg1_master_intr_disable(dev_priv->uncore.regs);

	gen11_gt_irq_reset(gt);
	gen11_display_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN11_GU_MISC_);
	GEN3_IRQ_RESET(uncore, GEN8_PCU_);
}

void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 extra_ier = GEN8_PIPE_VBLANK |
		gen8_de_pipe_underrun_mask(dev_priv) |
		gen8_de_pipe_flip_done_mask(dev_priv);
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

	intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, 0);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(to_gt(dev_priv));

	GEN3_IRQ_RESET(uncore, GEN8_PCU_);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static u32 ibx_hotplug_enables(struct drm_i915_private *i915,
			       enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_A:
		/*
		 * When CPU and PCH are on the same package, port A
		 * HPD must be enabled in both north and south.
		 */
		return HAS_PCH_LPT_LP(i915) ?
			PORTA_HOTPLUG_ENABLE : 0;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE |
			PORTB_PULSE_DURATION_2ms;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE |
			PORTC_PULSE_DURATION_2ms;
	case HPD_PORT_D:
		return PORTD_HOTPLUG_ENABLE |
			PORTD_PULSE_DURATION_2ms;
	default:
		return 0;
	}
}

static void ibx_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	/*
	 * Enable digital hotplug on the PCH, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec).
	 * The pulse duration bits are reserved on LPT+.
	 */
	hotplug = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTA_HOTPLUG_ENABLE |
		     PORTB_HOTPLUG_ENABLE |
		     PORTC_HOTPLUG_ENABLE |
		     PORTD_HOTPLUG_ENABLE |
		     PORTB_PULSE_DURATION_MASK |
		     PORTC_PULSE_DURATION_MASK |
		     PORTD_PULSE_DURATION_MASK);
	hotplug |= intel_hpd_hotplug_enables(dev_priv, ibx_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, hotplug);
}

static void ibx_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	ibx_hpd_detection_setup(dev_priv);
}

static u32 icp_ddi_hotplug_enables(struct drm_i915_private *i915,
				   enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_A:
	case HPD_PORT_B:
	case HPD_PORT_C:
	case HPD_PORT_D:
		return SHOTPLUG_CTL_DDI_HPD_ENABLE(pin);
	default:
		return 0;
	}
}

static u32 icp_tc_hotplug_enables(struct drm_i915_private *i915,
				  enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return ICP_TC_HPD_ENABLE(pin);
	default:
		return 0;
	}
}

static void icp_ddi_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = intel_uncore_read(&dev_priv->uncore, SHOTPLUG_CTL_DDI);
	hotplug &= ~(SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_A) |
		     SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_B) |
		     SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_C) |
		     SHOTPLUG_CTL_DDI_HPD_ENABLE(HPD_PORT_D));
	hotplug |= intel_hpd_hotplug_enables(dev_priv, icp_ddi_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, SHOTPLUG_CTL_DDI, hotplug);
}

static void icp_tc_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = intel_uncore_read(&dev_priv->uncore, SHOTPLUG_CTL_TC);
	hotplug &= ~(ICP_TC_HPD_ENABLE(HPD_PORT_TC1) |
		     ICP_TC_HPD_ENABLE(HPD_PORT_TC2) |
		     ICP_TC_HPD_ENABLE(HPD_PORT_TC3) |
		     ICP_TC_HPD_ENABLE(HPD_PORT_TC4) |
		     ICP_TC_HPD_ENABLE(HPD_PORT_TC5) |
		     ICP_TC_HPD_ENABLE(HPD_PORT_TC6));
	hotplug |= intel_hpd_hotplug_enables(dev_priv, icp_tc_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, SHOTPLUG_CTL_TC, hotplug);
}

static void icp_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	if (INTEL_PCH_TYPE(dev_priv) <= PCH_TGP)
		intel_uncore_write(&dev_priv->uncore, SHPD_FILTER_CNT, SHPD_FILTER_CNT_500_ADJ);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	icp_ddi_hpd_detection_setup(dev_priv);
	icp_tc_hpd_detection_setup(dev_priv);
}

static u32 gen11_hotplug_enables(struct drm_i915_private *i915,
				 enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return GEN11_HOTPLUG_CTL_ENABLE(pin);
	default:
		return 0;
	}
}

static void dg1_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = intel_uncore_read(&dev_priv->uncore, SOUTH_CHICKEN1);
	val |= (INVERT_DDIA_HPD |
		INVERT_DDIB_HPD |
		INVERT_DDIC_HPD |
		INVERT_DDID_HPD);
	intel_uncore_write(&dev_priv->uncore, SOUTH_CHICKEN1, val);

	icp_hpd_irq_setup(dev_priv);
}

static void gen11_tc_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = intel_uncore_read(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL);
	hotplug &= ~(GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC1) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC2) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC3) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC4) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC5) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC6));
	hotplug |= intel_hpd_hotplug_enables(dev_priv, gen11_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL, hotplug);
}

static void gen11_tbt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = intel_uncore_read(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL);
	hotplug &= ~(GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC1) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC2) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC3) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC4) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC5) |
		     GEN11_HOTPLUG_CTL_ENABLE(HPD_PORT_TC6));
	hotplug |= intel_hpd_hotplug_enables(dev_priv, gen11_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL, hotplug);
}

static void gen11_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;
	u32 val;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	val = intel_uncore_read(&dev_priv->uncore, GEN11_DE_HPD_IMR);
	val &= ~hotplug_irqs;
	val |= ~enabled_irqs & hotplug_irqs;
	intel_uncore_write(&dev_priv->uncore, GEN11_DE_HPD_IMR, val);
	intel_uncore_posting_read(&dev_priv->uncore, GEN11_DE_HPD_IMR);

	gen11_tc_hpd_detection_setup(dev_priv);
	gen11_tbt_hpd_detection_setup(dev_priv);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_hpd_irq_setup(dev_priv);
}

static u32 spt_hotplug_enables(struct drm_i915_private *i915,
			       enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_A:
		return PORTA_HOTPLUG_ENABLE;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE;
	case HPD_PORT_D:
		return PORTD_HOTPLUG_ENABLE;
	default:
		return 0;
	}
}

static u32 spt_hotplug2_enables(struct drm_i915_private *i915,
				enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_E:
		return PORTE_HOTPLUG_ENABLE;
	default:
		return 0;
	}
}

static void spt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 val, hotplug;

	/* Display WA #1179 WaHardHangonHotPlug: cnp */
	if (HAS_PCH_CNP(dev_priv)) {
		val = intel_uncore_read(&dev_priv->uncore, SOUTH_CHICKEN1);
		val &= ~CHASSIS_CLK_REQ_DURATION_MASK;
		val |= CHASSIS_CLK_REQ_DURATION(0xf);
		intel_uncore_write(&dev_priv->uncore, SOUTH_CHICKEN1, val);
	}

	/* Enable digital hotplug on the PCH */
	hotplug = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTA_HOTPLUG_ENABLE |
		     PORTB_HOTPLUG_ENABLE |
		     PORTC_HOTPLUG_ENABLE |
		     PORTD_HOTPLUG_ENABLE);
	hotplug |= intel_hpd_hotplug_enables(dev_priv, spt_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, hotplug);

	hotplug = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG2);
	hotplug &= ~PORTE_HOTPLUG_ENABLE;
	hotplug |= intel_hpd_hotplug_enables(dev_priv, spt_hotplug2_enables);
	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG2, hotplug);
}

static void spt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_CNP)
		intel_uncore_write(&dev_priv->uncore, SHPD_FILTER_CNT, SHPD_FILTER_CNT_500_ADJ);

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	spt_hpd_detection_setup(dev_priv);
}

static u32 ilk_hotplug_enables(struct drm_i915_private *i915,
			       enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_A:
		return DIGITAL_PORTA_HOTPLUG_ENABLE |
			DIGITAL_PORTA_PULSE_DURATION_2ms;
	default:
		return 0;
	}
}

static void ilk_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	/*
	 * Enable digital hotplug on the CPU, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec)
	 * The pulse duration bits are reserved on HSW+.
	 */
	hotplug = intel_uncore_read(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL);
	hotplug &= ~(DIGITAL_PORTA_HOTPLUG_ENABLE |
		     DIGITAL_PORTA_PULSE_DURATION_MASK);
	hotplug |= intel_hpd_hotplug_enables(dev_priv, ilk_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL, hotplug);
}

static void ilk_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	if (DISPLAY_VER(dev_priv) >= 8)
		bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);
	else
		ilk_update_display_irq(dev_priv, hotplug_irqs, enabled_irqs);

	ilk_hpd_detection_setup(dev_priv);

	ibx_hpd_irq_setup(dev_priv);
}

static u32 bxt_hotplug_enables(struct drm_i915_private *i915,
			       enum hpd_pin pin)
{
	u32 hotplug;

	switch (pin) {
	case HPD_PORT_A:
		hotplug = PORTA_HOTPLUG_ENABLE;
		if (intel_bios_is_port_hpd_inverted(i915, PORT_A))
			hotplug |= BXT_DDIA_HPD_INVERT;
		return hotplug;
	case HPD_PORT_B:
		hotplug = PORTB_HOTPLUG_ENABLE;
		if (intel_bios_is_port_hpd_inverted(i915, PORT_B))
			hotplug |= BXT_DDIB_HPD_INVERT;
		return hotplug;
	case HPD_PORT_C:
		hotplug = PORTC_HOTPLUG_ENABLE;
		if (intel_bios_is_port_hpd_inverted(i915, PORT_C))
			hotplug |= BXT_DDIC_HPD_INVERT;
		return hotplug;
	default:
		return 0;
	}
}

static void bxt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug;

	hotplug = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTA_HOTPLUG_ENABLE |
		     PORTB_HOTPLUG_ENABLE |
		     PORTC_HOTPLUG_ENABLE |
		     BXT_DDIA_HPD_INVERT |
		     BXT_DDIB_HPD_INVERT |
		     BXT_DDIC_HPD_INVERT);
	hotplug |= intel_hpd_hotplug_enables(dev_priv, bxt_hotplug_enables);
	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, hotplug);
}

static void bxt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);

	bxt_hpd_detection_setup(dev_priv);
}

/*
 * SDEIER is also touched by the interrupt handler to work around missed PCH
 * interrupts. Hence we can't update it after the interrupt handler is enabled -
 * instead we unconditionally enable all PCH interrupt sources here, but then
 * only unmask them as needed with SDEIMR.
 *
 * Note that we currently do this after installing the interrupt handler,
 * but before we enable the master interrupt. That should be sufficient
 * to avoid races with the irq handler, assuming we have MSI. Shared legacy
 * interrupts could still race.
 */
static void ibx_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 mask;

	if (HAS_PCH_NOP(dev_priv))
		return;

	if (HAS_PCH_IBX(dev_priv))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else if (HAS_PCH_CPT(dev_priv) || HAS_PCH_LPT(dev_priv))
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;
	else
		mask = SDE_GMBUS_CPT;

	GEN3_IRQ_INIT(uncore, SDE, ~mask, 0xffffffff);
}

static void ilk_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 display_mask, extra_mask;

	if (GRAPHICS_VER(dev_priv) >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_C) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_B) |
			      DE_PLANE_FLIP_DONE_IVB(PLANE_A) |
			      DE_DP_A_HOTPLUG_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_AUX_CHANNEL_A | DE_PIPEB_CRC_DONE |
				DE_PIPEA_CRC_DONE | DE_POISON);
		extra_mask = (DE_PIPEA_VBLANK | DE_PIPEB_VBLANK |
			      DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN |
			      DE_PLANE_FLIP_DONE(PLANE_A) |
			      DE_PLANE_FLIP_DONE(PLANE_B) |
			      DE_DP_A_HOTPLUG);
	}

	if (IS_HASWELL(dev_priv)) {
		gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
		display_mask |= DE_EDP_PSR_INT_HSW;
	}

	if (IS_IRONLAKE_M(dev_priv))
		extra_mask |= DE_PCU_EVENT;

	dev_priv->irq_mask = ~display_mask;

	ibx_irq_postinstall(dev_priv);

	gen5_gt_irq_postinstall(to_gt(dev_priv));

	GEN3_IRQ_INIT(uncore, DE, dev_priv->irq_mask,
		      display_mask | extra_mask);
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
	gen5_gt_irq_postinstall(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_uncore_write(&dev_priv->uncore, VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
	intel_uncore_posting_read(&dev_priv->uncore, VLV_MASTER_IER);
}

static void gen8_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	u32 de_pipe_masked = gen8_de_pipe_fault_mask(dev_priv) |
		GEN8_PIPE_CDCLK_CRC_DONE;
	u32 de_pipe_enables;
	u32 de_port_masked = gen8_de_port_aux_mask(dev_priv);
	u32 de_port_enables;
	u32 de_misc_masked = GEN8_DE_EDP_PSR;
	u32 trans_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);
	enum pipe pipe;

	if (!HAS_DISPLAY(dev_priv))
		return;

	if (DISPLAY_VER(dev_priv) <= 10)
		de_misc_masked |= GEN8_DE_MISC_GSE;

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		de_port_masked |= BXT_DE_PORT_GMBUS;

	if (DISPLAY_VER(dev_priv) >= 11) {
		enum port port;

		if (intel_bios_is_dsi_present(dev_priv, &port))
			de_port_masked |= DSI0_TE | DSI1_TE;
	}

	de_pipe_enables = de_pipe_masked |
		GEN8_PIPE_VBLANK |
		gen8_de_pipe_underrun_mask(dev_priv) |
		gen8_de_pipe_flip_done_mask(dev_priv);

	de_port_enables = de_port_masked;
	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		de_port_enables |= BXT_DE_PORT_HOTPLUG_MASK;
	else if (IS_BROADWELL(dev_priv))
		de_port_enables |= BDW_DE_PORT_HOTPLUG_MASK;

	if (DISPLAY_VER(dev_priv) >= 12) {
		enum transcoder trans;

		for_each_cpu_transcoder_masked(dev_priv, trans, trans_mask) {
			enum intel_display_power_domain domain;

			domain = POWER_DOMAIN_TRANSCODER(trans);
			if (!intel_display_power_is_enabled(dev_priv, domain))
				continue;

			gen3_assert_iir_is_zero(uncore, TRANS_PSR_IIR(trans));
		}
	} else {
		gen3_assert_iir_is_zero(uncore, EDP_PSR_IIR);
	}

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

	if (DISPLAY_VER(dev_priv) >= 11) {
		u32 de_hpd_masked = 0;
		u32 de_hpd_enables = GEN11_DE_TC_HOTPLUG_MASK |
				     GEN11_DE_TBT_HOTPLUG_MASK;

		GEN3_IRQ_INIT(uncore, GEN11_DE_HPD_, ~de_hpd_masked,
			      de_hpd_enables);
	}
}

static void icp_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 mask = SDE_GMBUS_ICP;

	GEN3_IRQ_INIT(uncore, SDE, ~mask, 0xffffffff);
}

static void gen8_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_irq_postinstall(dev_priv);
	else if (HAS_PCH_SPLIT(dev_priv))
		ibx_irq_postinstall(dev_priv);

	gen8_gt_irq_postinstall(to_gt(dev_priv));
	gen8_de_irq_postinstall(dev_priv);

	gen8_master_intr_enable(dev_priv->uncore.regs);
}

static void gen11_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	gen8_de_irq_postinstall(dev_priv);

	intel_uncore_write(&dev_priv->uncore, GEN11_DISPLAY_INT_CTL,
			   GEN11_DISPLAY_IRQ_ENABLE);
}

static void gen11_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_irq_postinstall(dev_priv);

	gen11_gt_irq_postinstall(gt);
	gen11_de_irq_postinstall(dev_priv);

	GEN3_IRQ_INIT(uncore, GEN11_GU_MISC_, ~gu_misc_masked, gu_misc_masked);

	gen11_master_intr_enable(uncore->regs);
	intel_uncore_posting_read(&dev_priv->uncore, GEN11_GFX_MSTR_IRQ);
}

static void dg1_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt = to_gt(dev_priv);
	struct intel_uncore *uncore = gt->uncore;
	u32 gu_misc_masked = GEN11_GU_MISC_GSE;

	gen11_gt_irq_postinstall(gt);

	GEN3_IRQ_INIT(uncore, GEN11_GU_MISC_, ~gu_misc_masked, gu_misc_masked);

	if (HAS_DISPLAY(dev_priv)) {
		icp_irq_postinstall(dev_priv);
		gen8_de_irq_postinstall(dev_priv);
		intel_uncore_write(&dev_priv->uncore, GEN11_DISPLAY_INT_CTL,
				   GEN11_DISPLAY_IRQ_ENABLE);
	}

	dg1_master_intr_enable(uncore->regs);
	intel_uncore_posting_read(uncore, DG1_MSTR_TILE_INTR);
}

static void cherryview_irq_postinstall(struct drm_i915_private *dev_priv)
{
	gen8_gt_irq_postinstall(to_gt(dev_priv));

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_uncore_write(&dev_priv->uncore, GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	intel_uncore_posting_read(&dev_priv->uncore, GEN8_MASTER_IRQ);
}

static void i8xx_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i9xx_pipestat_irq_reset(dev_priv);

	GEN2_IRQ_RESET(uncore);
	dev_priv->irq_mask = ~0u;
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
		drm_dbg(&dev_priv->drm, "EIR stuck: 0x%04x, masked\n",
			eir_stuck);
}

static void i9xx_error_irq_ack(struct drm_i915_private *dev_priv,
			       u32 *eir, u32 *eir_stuck)
{
	u32 emr;

	*eir = intel_uncore_read(&dev_priv->uncore, EIR);

	intel_uncore_write(&dev_priv->uncore, EIR, *eir);

	*eir_stuck = intel_uncore_read(&dev_priv->uncore, EIR);
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
	emr = intel_uncore_read(&dev_priv->uncore, EMR);
	intel_uncore_write(&dev_priv->uncore, EMR, 0xffffffff);
	intel_uncore_write(&dev_priv->uncore, EMR, emr | *eir_stuck);
}

static void i9xx_error_irq_handler(struct drm_i915_private *dev_priv,
				   u32 eir, u32 eir_stuck)
{
	DRM_DEBUG("Master Error, EIR 0x%08x\n", eir);

	if (eir_stuck)
		drm_dbg(&dev_priv->drm, "EIR stuck: 0x%08x, masked\n",
			eir_stuck);
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
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0], iir);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i8xx_error_irq_handler(dev_priv, eir, eir_stuck);

		i8xx_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i915_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	if (I915_HAS_HOTPLUG(dev_priv)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		intel_uncore_write(&dev_priv->uncore, PORT_HOTPLUG_STAT, intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT));
	}

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
	dev_priv->irq_mask = ~0u;
}

static void i915_irq_postinstall(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;
	u32 enable_mask;

	intel_uncore_write(&dev_priv->uncore, EMR, ~(I915_ERROR_PAGE_TABLE |
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

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
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

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0], iir);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i915_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, ret);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static void i965_irq_reset(struct drm_i915_private *dev_priv)
{
	struct intel_uncore *uncore = &dev_priv->uncore;

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	intel_uncore_write(&dev_priv->uncore, PORT_HOTPLUG_STAT, intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT));

	i9xx_pipestat_irq_reset(dev_priv);

	GEN3_IRQ_RESET(uncore, GEN2_);
	dev_priv->irq_mask = ~0u;
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
	intel_uncore_write(&dev_priv->uncore, EMR, error_mask);

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

		iir = intel_uncore_read(&dev_priv->uncore, GEN2_IIR);
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

		intel_uncore_write(&dev_priv->uncore, GEN2_IIR, iir);

		if (iir & I915_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[RCS0],
					    iir);

		if (iir & I915_BSD_USER_INTERRUPT)
			intel_engine_cs_irq(to_gt(dev_priv)->engine[VCS0],
					    iir >> 25);

		if (iir & I915_MASTER_ERROR_INTERRUPT)
			i9xx_error_irq_handler(dev_priv, eir, eir_stuck);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		i965_pipestat_irq_handler(dev_priv, iir, pipe_stats);
	} while (0);

	pmu_irq_stats(dev_priv, IRQ_HANDLED);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

struct intel_hotplug_funcs {
	void (*hpd_irq_setup)(struct drm_i915_private *i915);
};

#define HPD_FUNCS(platform)					 \
static const struct intel_hotplug_funcs platform##_hpd_funcs = { \
	.hpd_irq_setup = platform##_hpd_irq_setup,		 \
}

HPD_FUNCS(i915);
HPD_FUNCS(dg1);
HPD_FUNCS(gen11);
HPD_FUNCS(bxt);
HPD_FUNCS(icp);
HPD_FUNCS(spt);
HPD_FUNCS(ilk);
#undef HPD_FUNCS

void intel_hpd_irq_setup(struct drm_i915_private *i915)
{
	if (i915->display_irqs_enabled && i915->display.funcs.hotplug)
		i915->display.funcs.hotplug->hpd_irq_setup(i915);
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
	int i;

	INIT_WORK(&dev_priv->l3_parity.error_work, ivb_parity_work);
	for (i = 0; i < MAX_L3_SLICES; ++i)
		dev_priv->l3_parity.remap_info[i] = NULL;

	/* pre-gen11 the guc irqs bits are in the upper 16 bits of the pm reg */
	if (HAS_GT_UC(dev_priv) && GRAPHICS_VER(dev_priv) < 11)
		to_gt(dev_priv)->pm_guc_events = GUC_INTR_GUC2HOST << 16;

	if (!HAS_DISPLAY(dev_priv))
		return;

	intel_hpd_init_pins(dev_priv);

	intel_hpd_init_work(dev_priv);

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

	dev_priv->display.hotplug.hpd_storm_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	/* If we have MST support, we want to avoid doing short HPD IRQ storm
	 * detection, as short HPD storms will occur as a natural part of
	 * sideband messaging with MST.
	 * On older platforms however, IRQ storms can occur with both long and
	 * short pulses, as seen on some G4x systems.
	 */
	dev_priv->display.hotplug.hpd_short_storm_enabled = !HAS_DP_MST(dev_priv);

	if (HAS_GMCH(dev_priv)) {
		if (I915_HAS_HOTPLUG(dev_priv))
			dev_priv->display.funcs.hotplug = &i915_hpd_funcs;
	} else {
		if (HAS_PCH_DG2(dev_priv))
			dev_priv->display.funcs.hotplug = &icp_hpd_funcs;
		else if (HAS_PCH_DG1(dev_priv))
			dev_priv->display.funcs.hotplug = &dg1_hpd_funcs;
		else if (DISPLAY_VER(dev_priv) >= 11)
			dev_priv->display.funcs.hotplug = &gen11_hpd_funcs;
		else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
			dev_priv->display.funcs.hotplug = &bxt_hpd_funcs;
		else if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
			dev_priv->display.funcs.hotplug = &icp_hpd_funcs;
		else if (INTEL_PCH_TYPE(dev_priv) >= PCH_SPT)
			dev_priv->display.funcs.hotplug = &spt_hpd_funcs;
		else
			dev_priv->display.funcs.hotplug = &ilk_hpd_funcs;
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
		else if (GRAPHICS_VER(dev_priv) == 4)
			return i965_irq_handler;
		else if (GRAPHICS_VER(dev_priv) == 3)
			return i915_irq_handler;
		else
			return i8xx_irq_handler;
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			return dg1_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 11)
			return gen11_irq_handler;
		else if (GRAPHICS_VER(dev_priv) >= 8)
			return gen8_irq_handler;
		else
			return ilk_irq_handler;
	}
}

static void intel_irq_reset(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_reset(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 3)
			i915_irq_reset(dev_priv);
		else
			i8xx_irq_reset(dev_priv);
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_reset(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_reset(dev_priv);
		else
			ilk_irq_reset(dev_priv);
	}
}

static void intel_irq_postinstall(struct drm_i915_private *dev_priv)
{
	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv))
			cherryview_irq_postinstall(dev_priv);
		else if (IS_VALLEYVIEW(dev_priv))
			valleyview_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 4)
			i965_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) == 3)
			i915_irq_postinstall(dev_priv);
		else
			i8xx_irq_postinstall(dev_priv);
	} else {
		if (GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 10))
			dg1_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 11)
			gen11_irq_postinstall(dev_priv);
		else if (GRAPHICS_VER(dev_priv) >= 8)
			gen8_irq_postinstall(dev_priv);
		else
			ilk_irq_postinstall(dev_priv);
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
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;
	int ret;

	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid
	 * special cases in our ordering checks.
	 */
	dev_priv->runtime_pm.irqs_enabled = true;

	dev_priv->irq_enabled = true;

	intel_irq_reset(dev_priv);

	ret = request_irq(irq, intel_irq_handler(dev_priv),
			  IRQF_SHARED, DRIVER_NAME, dev_priv);
	if (ret < 0) {
		dev_priv->irq_enabled = false;
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
	int irq = to_pci_dev(dev_priv->drm.dev)->irq;

	/*
	 * FIXME we can get called twice during driver probe
	 * error handling as well as during driver remove due to
	 * intel_modeset_driver_remove() calling us out of sequence.
	 * Would be nice if it didn't do that...
	 */
	if (!dev_priv->irq_enabled)
		return;

	dev_priv->irq_enabled = false;

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

bool intel_irqs_enabled(struct drm_i915_private *dev_priv)
{
	return dev_priv->runtime_pm.irqs_enabled;
}

void intel_synchronize_irq(struct drm_i915_private *i915)
{
	synchronize_irq(to_pci_dev(i915->drm.dev)->irq);
}

void intel_synchronize_hardirq(struct drm_i915_private *i915)
{
	synchronize_hardirq(to_pci_dev(i915->drm.dev)->irq);
}
