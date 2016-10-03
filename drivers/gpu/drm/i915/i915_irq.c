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

#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

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

/* IIR can theoretically queue up two events. Be paranoid. */
#define GEN8_IRQ_RESET_NDX(type, which) do { \
	I915_WRITE(GEN8_##type##_IMR(which), 0xffffffff); \
	POSTING_READ(GEN8_##type##_IMR(which)); \
	I915_WRITE(GEN8_##type##_IER(which), 0); \
	I915_WRITE(GEN8_##type##_IIR(which), 0xffffffff); \
	POSTING_READ(GEN8_##type##_IIR(which)); \
	I915_WRITE(GEN8_##type##_IIR(which), 0xffffffff); \
	POSTING_READ(GEN8_##type##_IIR(which)); \
} while (0)

#define GEN5_IRQ_RESET(type) do { \
	I915_WRITE(type##IMR, 0xffffffff); \
	POSTING_READ(type##IMR); \
	I915_WRITE(type##IER, 0); \
	I915_WRITE(type##IIR, 0xffffffff); \
	POSTING_READ(type##IIR); \
	I915_WRITE(type##IIR, 0xffffffff); \
	POSTING_READ(type##IIR); \
} while (0)

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
static void gen5_assert_iir_is_zero(struct drm_i915_private *dev_priv,
				    i915_reg_t reg)
{
	u32 val = I915_READ(reg);

	if (val == 0)
		return;

	WARN(1, "Interrupt register 0x%x is not zero: 0x%08x\n",
	     i915_mmio_reg_offset(reg), val);
	I915_WRITE(reg, 0xffffffff);
	POSTING_READ(reg);
	I915_WRITE(reg, 0xffffffff);
	POSTING_READ(reg);
}

#define GEN8_IRQ_INIT_NDX(type, which, imr_val, ier_val) do { \
	gen5_assert_iir_is_zero(dev_priv, GEN8_##type##_IIR(which)); \
	I915_WRITE(GEN8_##type##_IER(which), (ier_val)); \
	I915_WRITE(GEN8_##type##_IMR(which), (imr_val)); \
	POSTING_READ(GEN8_##type##_IMR(which)); \
} while (0)

#define GEN5_IRQ_INIT(type, imr_val, ier_val) do { \
	gen5_assert_iir_is_zero(dev_priv, type##IIR); \
	I915_WRITE(type##IER, (ier_val)); \
	I915_WRITE(type##IMR, (imr_val)); \
	POSTING_READ(type##IMR); \
} while (0)

static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir);

/* For display hotplug interrupt */
static inline void
i915_hotplug_interrupt_update_locked(struct drm_i915_private *dev_priv,
				     uint32_t mask,
				     uint32_t bits)
{
	uint32_t val;

	assert_spin_locked(&dev_priv->irq_lock);
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
				   uint32_t mask,
				   uint32_t bits)
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
void ilk_update_display_irq(struct drm_i915_private *dev_priv,
			    uint32_t interrupt_mask,
			    uint32_t enabled_irq_mask)
{
	uint32_t new_val;

	assert_spin_locked(&dev_priv->irq_lock);

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
			      uint32_t interrupt_mask,
			      uint32_t enabled_irq_mask)
{
	assert_spin_locked(&dev_priv->irq_lock);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	dev_priv->gt_irq_mask &= ~interrupt_mask;
	dev_priv->gt_irq_mask |= (~enabled_irq_mask & interrupt_mask);
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
}

void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, mask);
	POSTING_READ_FW(GTIMR);
}

void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, 0);
}

static i915_reg_t gen6_pm_iir(struct drm_i915_private *dev_priv)
{
	return INTEL_INFO(dev_priv)->gen >= 8 ? GEN8_GT_IIR(2) : GEN6_PMIIR;
}

static i915_reg_t gen6_pm_imr(struct drm_i915_private *dev_priv)
{
	return INTEL_INFO(dev_priv)->gen >= 8 ? GEN8_GT_IMR(2) : GEN6_PMIMR;
}

static i915_reg_t gen6_pm_ier(struct drm_i915_private *dev_priv)
{
	return INTEL_INFO(dev_priv)->gen >= 8 ? GEN8_GT_IER(2) : GEN6_PMIER;
}

/**
 * snb_update_pm_irq - update GEN6_PMIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void snb_update_pm_irq(struct drm_i915_private *dev_priv,
			      uint32_t interrupt_mask,
			      uint32_t enabled_irq_mask)
{
	uint32_t new_val;

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	assert_spin_locked(&dev_priv->irq_lock);

	new_val = dev_priv->pm_irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->pm_irq_mask) {
		dev_priv->pm_irq_mask = new_val;
		I915_WRITE(gen6_pm_imr(dev_priv), dev_priv->pm_irq_mask);
		POSTING_READ(gen6_pm_imr(dev_priv));
	}
}

void gen6_enable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	snb_update_pm_irq(dev_priv, mask, mask);
}

static void __gen6_disable_pm_irq(struct drm_i915_private *dev_priv,
				  uint32_t mask)
{
	snb_update_pm_irq(dev_priv, mask, 0);
}

void gen6_disable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	__gen6_disable_pm_irq(dev_priv, mask);
}

void gen6_reset_rps_interrupts(struct drm_i915_private *dev_priv)
{
	i915_reg_t reg = gen6_pm_iir(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	I915_WRITE(reg, dev_priv->pm_rps_events);
	I915_WRITE(reg, dev_priv->pm_rps_events);
	POSTING_READ(reg);
	dev_priv->rps.pm_iir = 0;
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen6_enable_rps_interrupts(struct drm_i915_private *dev_priv)
{
	if (READ_ONCE(dev_priv->rps.interrupts_enabled))
		return;

	spin_lock_irq(&dev_priv->irq_lock);
	WARN_ON_ONCE(dev_priv->rps.pm_iir);
	WARN_ON_ONCE(I915_READ(gen6_pm_iir(dev_priv)) & dev_priv->pm_rps_events);
	dev_priv->rps.interrupts_enabled = true;
	I915_WRITE(gen6_pm_ier(dev_priv), I915_READ(gen6_pm_ier(dev_priv)) |
				dev_priv->pm_rps_events);
	gen6_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);

	spin_unlock_irq(&dev_priv->irq_lock);
}

u32 gen6_sanitize_rps_pm_mask(struct drm_i915_private *dev_priv, u32 mask)
{
	return (mask & ~dev_priv->rps.pm_intr_keep);
}

void gen6_disable_rps_interrupts(struct drm_i915_private *dev_priv)
{
	if (!READ_ONCE(dev_priv->rps.interrupts_enabled))
		return;

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->rps.interrupts_enabled = false;

	I915_WRITE(GEN6_PMINTRMSK, gen6_sanitize_rps_pm_mask(dev_priv, ~0u));

	__gen6_disable_pm_irq(dev_priv, dev_priv->pm_rps_events);
	I915_WRITE(gen6_pm_ier(dev_priv), I915_READ(gen6_pm_ier(dev_priv)) &
				~dev_priv->pm_rps_events);

	spin_unlock_irq(&dev_priv->irq_lock);
	synchronize_irq(dev_priv->drm.irq);

	/* Now that we will not be generating any more work, flush any
	 * outsanding tasks. As we are called on the RPS idle path,
	 * we will reset the GPU to minimum frequencies, so the current
	 * state of the worker can be discarded.
	 */
	cancel_work_sync(&dev_priv->rps.work);
	gen6_reset_rps_interrupts(dev_priv);
}

/**
 * bdw_update_port_irq - update DE port interrupt
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void bdw_update_port_irq(struct drm_i915_private *dev_priv,
				uint32_t interrupt_mask,
				uint32_t enabled_irq_mask)
{
	uint32_t new_val;
	uint32_t old_val;

	assert_spin_locked(&dev_priv->irq_lock);

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
			 uint32_t interrupt_mask,
			 uint32_t enabled_irq_mask)
{
	uint32_t new_val;

	assert_spin_locked(&dev_priv->irq_lock);

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
				  uint32_t interrupt_mask,
				  uint32_t enabled_irq_mask)
{
	uint32_t sdeimr = I915_READ(SDEIMR);
	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	assert_spin_locked(&dev_priv->irq_lock);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	I915_WRITE(SDEIMR, sdeimr);
	POSTING_READ(SDEIMR);
}

static void
__i915_enable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		       u32 enable_mask, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 pipestat = I915_READ(reg) & PIPESTAT_INT_ENABLE_MASK;

	assert_spin_locked(&dev_priv->irq_lock);
	WARN_ON(!intel_irqs_enabled(dev_priv));

	if (WARN_ONCE(enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
		      status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: enable_mask=0x%x, status_mask=0x%x\n",
		      pipe_name(pipe), enable_mask, status_mask))
		return;

	if ((pipestat & enable_mask) == enable_mask)
		return;

	dev_priv->pipestat_irq_mask[pipe] |= status_mask;

	/* Enable the interrupt, clear any pending status */
	pipestat |= enable_mask | status_mask;
	I915_WRITE(reg, pipestat);
	POSTING_READ(reg);
}

static void
__i915_disable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		        u32 enable_mask, u32 status_mask)
{
	i915_reg_t reg = PIPESTAT(pipe);
	u32 pipestat = I915_READ(reg) & PIPESTAT_INT_ENABLE_MASK;

	assert_spin_locked(&dev_priv->irq_lock);
	WARN_ON(!intel_irqs_enabled(dev_priv));

	if (WARN_ONCE(enable_mask & ~PIPESTAT_INT_ENABLE_MASK ||
		      status_mask & ~PIPESTAT_INT_STATUS_MASK,
		      "pipe %c: enable_mask=0x%x, status_mask=0x%x\n",
		      pipe_name(pipe), enable_mask, status_mask))
		return;

	if ((pipestat & enable_mask) == 0)
		return;

	dev_priv->pipestat_irq_mask[pipe] &= ~status_mask;

	pipestat &= ~enable_mask;
	I915_WRITE(reg, pipestat);
	POSTING_READ(reg);
}

static u32 vlv_get_pipestat_enable_mask(struct drm_device *dev, u32 status_mask)
{
	u32 enable_mask = status_mask << 16;

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

	return enable_mask;
}

void
i915_enable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		     u32 status_mask)
{
	u32 enable_mask;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		enable_mask = vlv_get_pipestat_enable_mask(&dev_priv->drm,
							   status_mask);
	else
		enable_mask = status_mask << 16;
	__i915_enable_pipestat(dev_priv, pipe, enable_mask, status_mask);
}

void
i915_disable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		      u32 status_mask)
{
	u32 enable_mask;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		enable_mask = vlv_get_pipestat_enable_mask(&dev_priv->drm,
							   status_mask);
	else
		enable_mask = status_mask << 16;
	__i915_disable_pipestat(dev_priv, pipe, enable_mask, status_mask);
}

/**
 * i915_enable_asle_pipestat - enable ASLE pipestat for OpRegion
 * @dev_priv: i915 device private
 */
static void i915_enable_asle_pipestat(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->opregion.asle || !IS_MOBILE(dev_priv))
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
static u32 i915_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	i915_reg_t high_frame, low_frame;
	u32 high1, high2, low, pixel, vbl_start, hsync_start, htotal;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);
	const struct drm_display_mode *mode = &intel_crtc->base.hwmode;

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

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK;
		low   = I915_READ(low_frame);
		high2 = I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK;
	} while (high1 != high2);

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

static u32 g4x_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	return I915_READ(PIPE_FRMCOUNT_G4X(pipe));
}

/* I915_READ_FW, only for fast reads of display block, no need for forcewake etc. */
static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct drm_display_mode *mode = &crtc->base.hwmode;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	vtotal = mode->crtc_vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	if (IS_GEN2(dev_priv))
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
			temp = __raw_i915_read32(dev_priv, PIPEDSL(pipe)) &
				DSL_LINEMASK_GEN3;
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

static int i915_get_crtc_scanoutpos(struct drm_device *dev, unsigned int pipe,
				    unsigned int flags, int *vpos, int *hpos,
				    ktime_t *stime, ktime_t *etime,
				    const struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int position;
	int vbl_start, vbl_end, hsync_start, htotal, vtotal;
	bool in_vbl = true;
	int ret = 0;
	unsigned long irqflags;

	if (WARN_ON(!mode->crtc_clock)) {
		DRM_DEBUG_DRIVER("trying to get scanoutpos for disabled "
				 "pipe %c\n", pipe_name(pipe));
		return 0;
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

	ret |= DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE;

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

	if (IS_GEN2(dev_priv) || IS_G4X(dev_priv) || INTEL_GEN(dev_priv) >= 5) {
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

	in_vbl = position >= vbl_start && position < vbl_end;

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

	if (IS_GEN2(dev_priv) || IS_G4X(dev_priv) || INTEL_GEN(dev_priv) >= 5) {
		*vpos = position;
		*hpos = 0;
	} else {
		*vpos = position / htotal;
		*hpos = position - (*vpos * htotal);
	}

	/* In vblank? */
	if (in_vbl)
		ret |= DRM_SCANOUTPOS_IN_VBLANK;

	return ret;
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

static int i915_get_vblank_timestamp(struct drm_device *dev, unsigned int pipe,
			      int *max_error,
			      struct timeval *vblank_time,
			      unsigned flags)
{
	struct drm_crtc *crtc;

	if (pipe >= INTEL_INFO(dev)->num_pipes) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return -EINVAL;
	}

	/* Get drm_crtc to timestamp: */
	crtc = intel_get_crtc_for_pipe(dev, pipe);
	if (crtc == NULL) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return -EINVAL;
	}

	if (!crtc->hwmode.crtc_clock) {
		DRM_DEBUG_KMS("crtc %u is disabled\n", pipe);
		return -EBUSY;
	}

	/* Helper routine in DRM core does all the work: */
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error,
						     vblank_time, flags,
						     &crtc->hwmode);
}

static void ironlake_rps_change_irq_handler(struct drm_i915_private *dev_priv)
{
	u32 busy_up, busy_down, max_avg, min_avg;
	u8 new_delay;

	spin_lock(&mchdev_lock);

	I915_WRITE16(MEMINTRSTS, I915_READ(MEMINTRSTS));

	new_delay = dev_priv->ips.cur_delay;

	I915_WRITE16(MEMINTRSTS, MEMINT_EVAL_CHG);
	busy_up = I915_READ(RCPREVBSYTUPAVG);
	busy_down = I915_READ(RCPREVBSYTDNAVG);
	max_avg = I915_READ(RCBMAXAVG);
	min_avg = I915_READ(RCBMINAVG);

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

static void notify_ring(struct intel_engine_cs *engine)
{
	smp_store_mb(engine->breadcrumbs.irq_posted, true);
	if (intel_engine_wakeup(engine))
		trace_i915_gem_request_notify(engine);
}

static void vlv_c0_read(struct drm_i915_private *dev_priv,
			struct intel_rps_ei *ei)
{
	ei->cz_clock = vlv_punit_read(dev_priv, PUNIT_REG_CZ_TIMESTAMP);
	ei->render_c0 = I915_READ(VLV_RENDER_C0_COUNT);
	ei->media_c0 = I915_READ(VLV_MEDIA_C0_COUNT);
}

static bool vlv_c0_above(struct drm_i915_private *dev_priv,
			 const struct intel_rps_ei *old,
			 const struct intel_rps_ei *now,
			 int threshold)
{
	u64 time, c0;
	unsigned int mul = 100;

	if (old->cz_clock == 0)
		return false;

	if (I915_READ(VLV_COUNTER_CONTROL) & VLV_COUNT_RANGE_HIGH)
		mul <<= 8;

	time = now->cz_clock - old->cz_clock;
	time *= threshold * dev_priv->czclk_freq;

	/* Workload can be split between render + media, e.g. SwapBuffers
	 * being blitted in X after being rendered in mesa. To account for
	 * this we need to combine both engines into our activity counter.
	 */
	c0 = now->render_c0 - old->render_c0;
	c0 += now->media_c0 - old->media_c0;
	c0 *= mul * VLV_CZ_CLOCK_TO_MILLI_SEC;

	return c0 >= time;
}

void gen6_rps_reset_ei(struct drm_i915_private *dev_priv)
{
	vlv_c0_read(dev_priv, &dev_priv->rps.down_ei);
	dev_priv->rps.up_ei = dev_priv->rps.down_ei;
}

static u32 vlv_wa_c0_ei(struct drm_i915_private *dev_priv, u32 pm_iir)
{
	struct intel_rps_ei now;
	u32 events = 0;

	if ((pm_iir & (GEN6_PM_RP_DOWN_EI_EXPIRED | GEN6_PM_RP_UP_EI_EXPIRED)) == 0)
		return 0;

	vlv_c0_read(dev_priv, &now);
	if (now.cz_clock == 0)
		return 0;

	if (pm_iir & GEN6_PM_RP_DOWN_EI_EXPIRED) {
		if (!vlv_c0_above(dev_priv,
				  &dev_priv->rps.down_ei, &now,
				  dev_priv->rps.down_threshold))
			events |= GEN6_PM_RP_DOWN_THRESHOLD;
		dev_priv->rps.down_ei = now;
	}

	if (pm_iir & GEN6_PM_RP_UP_EI_EXPIRED) {
		if (vlv_c0_above(dev_priv,
				 &dev_priv->rps.up_ei, &now,
				 dev_priv->rps.up_threshold))
			events |= GEN6_PM_RP_UP_THRESHOLD;
		dev_priv->rps.up_ei = now;
	}

	return events;
}

static bool any_waiters(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;

	for_each_engine(engine, dev_priv)
		if (intel_engine_has_waiter(engine))
			return true;

	return false;
}

static void gen6_pm_rps_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, rps.work);
	bool client_boost;
	int new_delay, adj, min, max;
	u32 pm_iir;

	spin_lock_irq(&dev_priv->irq_lock);
	/* Speed up work cancelation during disabling rps interrupts. */
	if (!dev_priv->rps.interrupts_enabled) {
		spin_unlock_irq(&dev_priv->irq_lock);
		return;
	}

	pm_iir = dev_priv->rps.pm_iir;
	dev_priv->rps.pm_iir = 0;
	/* Make sure not to corrupt PMIMR state used by ringbuffer on GEN6 */
	gen6_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);
	client_boost = dev_priv->rps.client_boost;
	dev_priv->rps.client_boost = false;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	WARN_ON(pm_iir & ~dev_priv->pm_rps_events);

	if ((pm_iir & dev_priv->pm_rps_events) == 0 && !client_boost)
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	pm_iir |= vlv_wa_c0_ei(dev_priv, pm_iir);

	adj = dev_priv->rps.last_adj;
	new_delay = dev_priv->rps.cur_freq;
	min = dev_priv->rps.min_freq_softlimit;
	max = dev_priv->rps.max_freq_softlimit;
	if (client_boost || any_waiters(dev_priv))
		max = dev_priv->rps.max_freq;
	if (client_boost && new_delay < dev_priv->rps.boost_freq) {
		new_delay = dev_priv->rps.boost_freq;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_UP_THRESHOLD) {
		if (adj > 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv) ? 2 : 1;
		/*
		 * For better performance, jump directly
		 * to RPe if we're below it.
		 */
		if (new_delay < dev_priv->rps.efficient_freq - adj) {
			new_delay = dev_priv->rps.efficient_freq;
			adj = 0;
		}
	} else if (client_boost || any_waiters(dev_priv)) {
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_TIMEOUT) {
		if (dev_priv->rps.cur_freq > dev_priv->rps.efficient_freq)
			new_delay = dev_priv->rps.efficient_freq;
		else
			new_delay = dev_priv->rps.min_freq_softlimit;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_THRESHOLD) {
		if (adj < 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv) ? -2 : -1;
	} else { /* unknown event */
		adj = 0;
	}

	dev_priv->rps.last_adj = adj;

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	new_delay += adj;
	new_delay = clamp_t(int, new_delay, min, max);

	intel_set_rps(dev_priv, new_delay);

	mutex_unlock(&dev_priv->rps.hw_lock);
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
		container_of(work, struct drm_i915_private, l3_parity.error_work);
	u32 error_status, row, bank, subbank;
	char *parity_event[6];
	uint32_t misccpctl;
	uint8_t slice = 0;

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
		notify_ring(&dev_priv->engine[RCS]);
	if (gt_iir & ILK_BSD_USER_INTERRUPT)
		notify_ring(&dev_priv->engine[VCS]);
}

static void snb_gt_irq_handler(struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{
	if (gt_iir & GT_RENDER_USER_INTERRUPT)
		notify_ring(&dev_priv->engine[RCS]);
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		notify_ring(&dev_priv->engine[VCS]);
	if (gt_iir & GT_BLT_USER_INTERRUPT)
		notify_ring(&dev_priv->engine[BCS]);

	if (gt_iir & (GT_BLT_CS_ERROR_INTERRUPT |
		      GT_BSD_CS_ERROR_INTERRUPT |
		      GT_RENDER_CS_MASTER_ERROR_INTERRUPT))
		DRM_DEBUG("Command parser error, gt_iir 0x%08x\n", gt_iir);

	if (gt_iir & GT_PARITY_ERROR(dev_priv))
		ivybridge_parity_error_irq_handler(dev_priv, gt_iir);
}

static __always_inline void
gen8_cs_irq_handler(struct intel_engine_cs *engine, u32 iir, int test_shift)
{
	if (iir & (GT_RENDER_USER_INTERRUPT << test_shift))
		notify_ring(engine);
	if (iir & (GT_CONTEXT_SWITCH_INTERRUPT << test_shift))
		tasklet_schedule(&engine->irq_tasklet);
}

static irqreturn_t gen8_gt_irq_ack(struct drm_i915_private *dev_priv,
				   u32 master_ctl,
				   u32 gt_iir[4])
{
	irqreturn_t ret = IRQ_NONE;

	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		gt_iir[0] = I915_READ_FW(GEN8_GT_IIR(0));
		if (gt_iir[0]) {
			I915_WRITE_FW(GEN8_GT_IIR(0), gt_iir[0]);
			ret = IRQ_HANDLED;
		} else
			DRM_ERROR("The master control interrupt lied (GT0)!\n");
	}

	if (master_ctl & (GEN8_GT_VCS1_IRQ | GEN8_GT_VCS2_IRQ)) {
		gt_iir[1] = I915_READ_FW(GEN8_GT_IIR(1));
		if (gt_iir[1]) {
			I915_WRITE_FW(GEN8_GT_IIR(1), gt_iir[1]);
			ret = IRQ_HANDLED;
		} else
			DRM_ERROR("The master control interrupt lied (GT1)!\n");
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		gt_iir[3] = I915_READ_FW(GEN8_GT_IIR(3));
		if (gt_iir[3]) {
			I915_WRITE_FW(GEN8_GT_IIR(3), gt_iir[3]);
			ret = IRQ_HANDLED;
		} else
			DRM_ERROR("The master control interrupt lied (GT3)!\n");
	}

	if (master_ctl & GEN8_GT_PM_IRQ) {
		gt_iir[2] = I915_READ_FW(GEN8_GT_IIR(2));
		if (gt_iir[2] & dev_priv->pm_rps_events) {
			I915_WRITE_FW(GEN8_GT_IIR(2),
				      gt_iir[2] & dev_priv->pm_rps_events);
			ret = IRQ_HANDLED;
		} else
			DRM_ERROR("The master control interrupt lied (PM)!\n");
	}

	return ret;
}

static void gen8_gt_irq_handler(struct drm_i915_private *dev_priv,
				u32 gt_iir[4])
{
	if (gt_iir[0]) {
		gen8_cs_irq_handler(&dev_priv->engine[RCS],
				    gt_iir[0], GEN8_RCS_IRQ_SHIFT);
		gen8_cs_irq_handler(&dev_priv->engine[BCS],
				    gt_iir[0], GEN8_BCS_IRQ_SHIFT);
	}

	if (gt_iir[1]) {
		gen8_cs_irq_handler(&dev_priv->engine[VCS],
				    gt_iir[1], GEN8_VCS1_IRQ_SHIFT);
		gen8_cs_irq_handler(&dev_priv->engine[VCS2],
				    gt_iir[1], GEN8_VCS2_IRQ_SHIFT);
	}

	if (gt_iir[3])
		gen8_cs_irq_handler(&dev_priv->engine[VECS],
				    gt_iir[3], GEN8_VECS_IRQ_SHIFT);

	if (gt_iir[2] & dev_priv->pm_rps_events)
		gen6_rps_irq_handler(dev_priv, gt_iir[2]);
}

static bool bxt_port_hotplug_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool spt_port_hotplug2_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_E:
		return val & PORTE_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool spt_port_hotplug_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool ilk_port_hotplug_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_A:
		return val & DIGITAL_PORTA_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool pch_port_hotplug_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool i9xx_port_hotplug_long_detect(enum port port, u32 val)
{
	switch (port) {
	case PORT_B:
		return val & PORTB_HOTPLUG_INT_LONG_PULSE;
	case PORT_C:
		return val & PORTC_HOTPLUG_INT_LONG_PULSE;
	case PORT_D:
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
static void intel_get_hpd_pins(u32 *pin_mask, u32 *long_mask,
			     u32 hotplug_trigger, u32 dig_hotplug_reg,
			     const u32 hpd[HPD_NUM_PINS],
			     bool long_pulse_detect(enum port port, u32 val))
{
	enum port port;
	int i;

	for_each_hpd_pin(i) {
		if ((hpd[i] & hotplug_trigger) == 0)
			continue;

		*pin_mask |= BIT(i);

		if (!intel_hpd_pin_to_port(i, &port))
			continue;

		if (long_pulse_detect(port, dig_hotplug_reg))
			*long_mask |= BIT(i);
	}

	DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x, dig 0x%08x, pins 0x%08x\n",
			 hotplug_trigger, dig_hotplug_reg, *pin_mask);

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
					 uint32_t crc0, uint32_t crc1,
					 uint32_t crc2, uint32_t crc3,
					 uint32_t crc4)
{
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];
	struct intel_pipe_crc_entry *entry;
	int head, tail;

	spin_lock(&pipe_crc->lock);

	if (!pipe_crc->entries) {
		spin_unlock(&pipe_crc->lock);
		DRM_DEBUG_KMS("spurious interrupt\n");
		return;
	}

	head = pipe_crc->head;
	tail = pipe_crc->tail;

	if (CIRC_SPACE(head, tail, INTEL_PIPE_CRC_ENTRIES_NR) < 1) {
		spin_unlock(&pipe_crc->lock);
		DRM_ERROR("CRC buffer overflowing\n");
		return;
	}

	entry = &pipe_crc->entries[head];

	entry->frame = dev_priv->drm.driver->get_vblank_counter(&dev_priv->drm,
								 pipe);
	entry->crc[0] = crc0;
	entry->crc[1] = crc1;
	entry->crc[2] = crc2;
	entry->crc[3] = crc3;
	entry->crc[4] = crc4;

	head = (head + 1) & (INTEL_PIPE_CRC_ENTRIES_NR - 1);
	pipe_crc->head = head;

	spin_unlock(&pipe_crc->lock);

	wake_up_interruptible(&pipe_crc->wq);
}
#else
static inline void
display_pipe_crc_irq_handler(struct drm_i915_private *dev_priv,
			     enum pipe pipe,
			     uint32_t crc0, uint32_t crc1,
			     uint32_t crc2, uint32_t crc3,
			     uint32_t crc4) {}
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
	uint32_t res1, res2;

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
static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir)
{
	if (pm_iir & dev_priv->pm_rps_events) {
		spin_lock(&dev_priv->irq_lock);
		gen6_disable_pm_irq(dev_priv, pm_iir & dev_priv->pm_rps_events);
		if (dev_priv->rps.interrupts_enabled) {
			dev_priv->rps.pm_iir |= pm_iir & dev_priv->pm_rps_events;
			schedule_work(&dev_priv->rps.work);
		}
		spin_unlock(&dev_priv->irq_lock);
	}

	if (INTEL_INFO(dev_priv)->gen >= 8)
		return;

	if (HAS_VEBOX(dev_priv)) {
		if (pm_iir & PM_VEBOX_USER_INTERRUPT)
			notify_ring(&dev_priv->engine[VECS]);

		if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT)
			DRM_DEBUG("Command parser error, pm_iir 0x%08x\n", pm_iir);
	}
}

static bool intel_pipe_handle_vblank(struct drm_i915_private *dev_priv,
				     enum pipe pipe)
{
	bool ret;

	ret = drm_handle_vblank(&dev_priv->drm, pipe);
	if (ret)
		intel_finish_page_flip_mmio(dev_priv, pipe);

	return ret;
}

static void valleyview_pipestat_irq_ack(struct drm_i915_private *dev_priv,
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
		u32 mask, iir_bit = 0;

		/*
		 * PIPESTAT bits get signalled even when the interrupt is
		 * disabled with the mask bits, and some of the status bits do
		 * not generate interrupts at all (like the underrun bit). Hence
		 * we need to be careful that we only handle what we want to
		 * handle.
		 */

		/* fifo underruns are filterered in the underrun handler. */
		mask = PIPE_FIFO_UNDERRUN_STATUS;

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
			mask |= dev_priv->pipestat_irq_mask[pipe];

		if (!mask)
			continue;

		reg = PIPESTAT(pipe);
		mask |= PIPESTAT_INT_ENABLE_MASK;
		pipe_stats[pipe] = I915_READ(reg) & mask;

		/*
		 * Clear the PIPE*STAT regs before the IIR
		 */
		if (pipe_stats[pipe] & (PIPE_FIFO_UNDERRUN_STATUS |
					PIPESTAT_INT_STATUS_MASK))
			I915_WRITE(reg, pipe_stats[pipe]);
	}
	spin_unlock(&dev_priv->irq_lock);
}

static void valleyview_pipestat_irq_handler(struct drm_i915_private *dev_priv,
					    u32 pipe_stats[I915_MAX_PIPES])
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS &&
		    intel_pipe_handle_vblank(dev_priv, pipe))
			intel_check_page_flip(dev_priv, pipe);

		if (pipe_stats[pipe] & PLANE_FLIP_DONE_INT_STATUS_VLV)
			intel_finish_page_flip_cs(dev_priv, pipe);

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
	u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);

	if (hotplug_status)
		I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);

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
			intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
					   hotplug_trigger, hpd_status_g4x,
					   i9xx_port_hotplug_long_detect);

			intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
		}

		if (hotplug_status & DP_AUX_CHANNEL_MASK_INT_STATUS_G4X)
			dp_aux_irq_handler(dev_priv);
	} else {
		u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

		if (hotplug_trigger) {
			intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
					   hotplug_trigger, hpd_status_i915,
					   i9xx_port_hotplug_long_detect);
			intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
		}
	}
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

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
		valleyview_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			I915_WRITE(VLV_IIR, iir);

		I915_WRITE(VLV_IER, ier);
		I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
		POSTING_READ(VLV_MASTER_IER);

		if (gt_iir)
			snb_gt_irq_handler(dev_priv, gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(dev_priv, pm_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static irqreturn_t cherryview_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	do {
		u32 master_ctl, iir;
		u32 gt_iir[4] = {};
		u32 pipe_stats[I915_MAX_PIPES] = {};
		u32 hotplug_status = 0;
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
		valleyview_pipestat_irq_ack(dev_priv, iir, pipe_stats);

		/*
		 * VLV_IIR is single buffered, and reflects the level
		 * from PIPESTAT/PORT_HOTPLUG_STAT, hence clear it last.
		 */
		if (iir)
			I915_WRITE(VLV_IIR, iir);

		I915_WRITE(VLV_IER, ier);
		I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
		POSTING_READ(GEN8_MASTER_IRQ);

		gen8_gt_irq_handler(dev_priv, gt_iir);

		if (hotplug_status)
			i9xx_hpd_irq_handler(dev_priv, hotplug_status);

		valleyview_pipestat_irq_handler(dev_priv, pipe_stats);
	} while (0);

	enable_rpm_wakeref_asserts(dev_priv);

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

	intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
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
		intel_pch_fifo_underrun_irq_handler(dev_priv, TRANSCODER_A);

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		intel_pch_fifo_underrun_irq_handler(dev_priv, TRANSCODER_B);
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

	if (serr_int & SERR_INT_POISON)
		DRM_ERROR("PCH poison interrupt\n");

	if (serr_int & SERR_INT_TRANS_A_FIFO_UNDERRUN)
		intel_pch_fifo_underrun_irq_handler(dev_priv, TRANSCODER_A);

	if (serr_int & SERR_INT_TRANS_B_FIFO_UNDERRUN)
		intel_pch_fifo_underrun_irq_handler(dev_priv, TRANSCODER_B);

	if (serr_int & SERR_INT_TRANS_C_FIFO_UNDERRUN)
		intel_pch_fifo_underrun_irq_handler(dev_priv, TRANSCODER_C);

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

		intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
				   dig_hotplug_reg, hpd_spt,
				   spt_port_hotplug_long_detect);
	}

	if (hotplug2_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG2);
		I915_WRITE(PCH_PORT_HOTPLUG2, dig_hotplug_reg);

		intel_get_hpd_pins(&pin_mask, &long_mask, hotplug2_trigger,
				   dig_hotplug_reg, hpd_spt,
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

	intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
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
		if (de_iir & DE_PIPE_VBLANK(pipe) &&
		    intel_pipe_handle_vblank(dev_priv, pipe))
			intel_check_page_flip(dev_priv, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(dev_priv, pipe);

		/* plane/pipes map 1:1 on ilk+ */
		if (de_iir & DE_PLANE_FLIP_DONE(pipe))
			intel_finish_page_flip_cs(dev_priv, pipe);
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

	if (IS_GEN5(dev_priv) && de_iir & DE_PCU_EVENT)
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

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		dp_aux_irq_handler(dev_priv);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev_priv);

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & (DE_PIPE_VBLANK_IVB(pipe)) &&
		    intel_pipe_handle_vblank(dev_priv, pipe))
			intel_check_page_flip(dev_priv, pipe);

		/* plane/pipes map 1:1 on ilk+ */
		if (de_iir & DE_PLANE_FLIP_DONE_IVB(pipe))
			intel_finish_page_flip_cs(dev_priv, pipe);
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
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);
	POSTING_READ(DEIER);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(dev_priv)) {
		sde_ier = I915_READ(SDEIER);
		I915_WRITE(SDEIER, 0);
		POSTING_READ(SDEIER);
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
	POSTING_READ(DEIER);
	if (!HAS_PCH_NOP(dev_priv)) {
		I915_WRITE(SDEIER, sde_ier);
		POSTING_READ(SDEIER);
	}

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static void bxt_hpd_irq_handler(struct drm_i915_private *dev_priv,
				u32 hotplug_trigger,
				const u32 hpd[HPD_NUM_PINS])
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
	I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);

	intel_get_hpd_pins(&pin_mask, &long_mask, hotplug_trigger,
			   dig_hotplug_reg, hpd,
			   bxt_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
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
			I915_WRITE(GEN8_DE_MISC_IIR, iir);
			ret = IRQ_HANDLED;
			if (iir & GEN8_DE_MISC_GSE)
				intel_opregion_asle_intr(dev_priv);
			else
				DRM_ERROR("Unexpected DE Misc interrupt\n");
		}
		else
			DRM_ERROR("The master control interrupt lied (DE MISC)!\n");
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = I915_READ(GEN8_DE_PORT_IIR);
		if (iir) {
			u32 tmp_mask;
			bool found = false;

			I915_WRITE(GEN8_DE_PORT_IIR, iir);
			ret = IRQ_HANDLED;

			tmp_mask = GEN8_AUX_CHANNEL_A;
			if (INTEL_INFO(dev_priv)->gen >= 9)
				tmp_mask |= GEN9_AUX_CHANNEL_B |
					    GEN9_AUX_CHANNEL_C |
					    GEN9_AUX_CHANNEL_D;

			if (iir & tmp_mask) {
				dp_aux_irq_handler(dev_priv);
				found = true;
			}

			if (IS_BROXTON(dev_priv)) {
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

			if (IS_BROXTON(dev_priv) && (iir & BXT_DE_PORT_GMBUS)) {
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
		u32 flip_done, fault_errors;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = I915_READ(GEN8_DE_PIPE_IIR(pipe));
		if (!iir) {
			DRM_ERROR("The master control interrupt lied (DE PIPE)!\n");
			continue;
		}

		ret = IRQ_HANDLED;
		I915_WRITE(GEN8_DE_PIPE_IIR(pipe), iir);

		if (iir & GEN8_PIPE_VBLANK &&
		    intel_pipe_handle_vblank(dev_priv, pipe))
			intel_check_page_flip(dev_priv, pipe);

		flip_done = iir;
		if (INTEL_INFO(dev_priv)->gen >= 9)
			flip_done &= GEN9_PIPE_PLANE1_FLIP_DONE;
		else
			flip_done &= GEN8_PIPE_PRIMARY_FLIP_DONE;

		if (flip_done)
			intel_finish_page_flip_cs(dev_priv, pipe);

		if (iir & GEN8_PIPE_CDCLK_CRC_DONE)
			hsw_pipe_crc_irq_handler(dev_priv, pipe);

		if (iir & GEN8_PIPE_FIFO_UNDERRUN)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		fault_errors = iir;
		if (INTEL_INFO(dev_priv)->gen >= 9)
			fault_errors &= GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
		else
			fault_errors &= GEN8_DE_PIPE_IRQ_FAULT_ERRORS;

		if (fault_errors)
			DRM_ERROR("Fault errors on pipe %c\n: 0x%08x",
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

			if (HAS_PCH_SPT(dev_priv) || HAS_PCH_KBP(dev_priv))
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

static irqreturn_t gen8_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 master_ctl;
	u32 gt_iir[4] = {};
	irqreturn_t ret;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	master_ctl = I915_READ_FW(GEN8_MASTER_IRQ);
	master_ctl &= ~GEN8_MASTER_IRQ_CONTROL;
	if (!master_ctl)
		return IRQ_NONE;

	I915_WRITE_FW(GEN8_MASTER_IRQ, 0);

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	/* Find, clear, then process each source of interrupt */
	ret = gen8_gt_irq_ack(dev_priv, master_ctl, gt_iir);
	gen8_gt_irq_handler(dev_priv, gt_iir);
	ret |= gen8_de_irq_handler(dev_priv, master_ctl);

	I915_WRITE_FW(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	POSTING_READ_FW(GEN8_MASTER_IRQ);

	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static void i915_error_wake_up(struct drm_i915_private *dev_priv)
{
	/*
	 * Notify all waiters for GPU completion events that reset state has
	 * been changed, and that they need to restart their wait after
	 * checking for potential errors (and bail out to drop locks if there is
	 * a gpu reset pending so that i915_error_work_func can acquire them).
	 */

	/* Wake up __wait_seqno, potentially holding dev->struct_mutex. */
	wake_up_all(&dev_priv->gpu_error.wait_queue);

	/* Wake up intel_crtc_wait_for_pending_flips, holding crtc->mutex. */
	wake_up_all(&dev_priv->pending_flip_queue);
}

/**
 * i915_reset_and_wakeup - do process context error handling work
 * @dev_priv: i915 device private
 *
 * Fire an error uevent so userspace can see that a hang or error
 * was detected.
 */
static void i915_reset_and_wakeup(struct drm_i915_private *dev_priv)
{
	struct kobject *kobj = &dev_priv->drm.primary->kdev->kobj;
	char *error_event[] = { I915_ERROR_UEVENT "=1", NULL };
	char *reset_event[] = { I915_RESET_UEVENT "=1", NULL };
	char *reset_done_event[] = { I915_ERROR_UEVENT "=0", NULL };

	kobject_uevent_env(kobj, KOBJ_CHANGE, error_event);

	DRM_DEBUG_DRIVER("resetting chip\n");
	kobject_uevent_env(kobj, KOBJ_CHANGE, reset_event);

	/*
	 * In most cases it's guaranteed that we get here with an RPM
	 * reference held, for example because there is a pending GPU
	 * request that won't finish until the reset is done. This
	 * isn't the case at least when we get here by doing a
	 * simulated reset via debugs, so get an RPM reference.
	 */
	intel_runtime_pm_get(dev_priv);
	intel_prepare_reset(dev_priv);

	do {
		/*
		 * All state reset _must_ be completed before we update the
		 * reset counter, for otherwise waiters might miss the reset
		 * pending state and not properly drop locks, resulting in
		 * deadlocks with the reset work.
		 */
		if (mutex_trylock(&dev_priv->drm.struct_mutex)) {
			i915_reset(dev_priv);
			mutex_unlock(&dev_priv->drm.struct_mutex);
		}

		/* We need to wait for anyone holding the lock to wakeup */
	} while (wait_on_bit_timeout(&dev_priv->gpu_error.flags,
				     I915_RESET_IN_PROGRESS,
				     TASK_UNINTERRUPTIBLE,
				     HZ));

	intel_finish_reset(dev_priv);
	intel_runtime_pm_put(dev_priv);

	if (!test_bit(I915_WEDGED, &dev_priv->gpu_error.flags))
		kobject_uevent_env(kobj,
				   KOBJ_CHANGE, reset_done_event);

	/*
	 * Note: The wake_up also serves as a memory barrier so that
	 * waiters see the updated value of the dev_priv->gpu_error.
	 */
	wake_up_all(&dev_priv->gpu_error.reset_queue);
}

static inline void
i915_err_print_instdone(struct drm_i915_private *dev_priv,
			struct intel_instdone *instdone)
{
	int slice;
	int subslice;

	pr_err("  INSTDONE: 0x%08x\n", instdone->instdone);

	if (INTEL_GEN(dev_priv) <= 3)
		return;

	pr_err("  SC_INSTDONE: 0x%08x\n", instdone->slice_common);

	if (INTEL_GEN(dev_priv) <= 6)
		return;

	for_each_instdone_slice_subslice(dev_priv, slice, subslice)
		pr_err("  SAMPLER_INSTDONE[%d][%d]: 0x%08x\n",
		       slice, subslice, instdone->sampler[slice][subslice]);

	for_each_instdone_slice_subslice(dev_priv, slice, subslice)
		pr_err("  ROW_INSTDONE[%d][%d]: 0x%08x\n",
		       slice, subslice, instdone->row[slice][subslice]);
}

static void i915_report_and_clear_eir(struct drm_i915_private *dev_priv)
{
	struct intel_instdone instdone;
	u32 eir = I915_READ(EIR);
	int pipe;

	if (!eir)
		return;

	pr_err("render error detected, EIR: 0x%08x\n", eir);

	i915_get_engine_instdone(dev_priv, RCS, &instdone);

	if (IS_G4X(dev_priv)) {
		if (eir & (GM45_ERROR_MEM_PRIV | GM45_ERROR_CP_PRIV)) {
			u32 ipeir = I915_READ(IPEIR_I965);

			pr_err("  IPEIR: 0x%08x\n", I915_READ(IPEIR_I965));
			pr_err("  IPEHR: 0x%08x\n", I915_READ(IPEHR_I965));
			i915_err_print_instdone(dev_priv, &instdone);
			pr_err("  INSTPS: 0x%08x\n", I915_READ(INSTPS));
			pr_err("  ACTHD: 0x%08x\n", I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			POSTING_READ(IPEIR_I965);
		}
		if (eir & GM45_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			pr_err("page table error\n");
			pr_err("  PGTBL_ER: 0x%08x\n", pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			POSTING_READ(PGTBL_ER);
		}
	}

	if (!IS_GEN2(dev_priv)) {
		if (eir & I915_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			pr_err("page table error\n");
			pr_err("  PGTBL_ER: 0x%08x\n", pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			POSTING_READ(PGTBL_ER);
		}
	}

	if (eir & I915_ERROR_MEMORY_REFRESH) {
		pr_err("memory refresh error:\n");
		for_each_pipe(dev_priv, pipe)
			pr_err("pipe %c stat: 0x%08x\n",
			       pipe_name(pipe), I915_READ(PIPESTAT(pipe)));
		/* pipestat has already been acked */
	}
	if (eir & I915_ERROR_INSTRUCTION) {
		pr_err("instruction error\n");
		pr_err("  INSTPM: 0x%08x\n", I915_READ(INSTPM));
		i915_err_print_instdone(dev_priv, &instdone);
		if (INTEL_GEN(dev_priv) < 4) {
			u32 ipeir = I915_READ(IPEIR);

			pr_err("  IPEIR: 0x%08x\n", I915_READ(IPEIR));
			pr_err("  IPEHR: 0x%08x\n", I915_READ(IPEHR));
			pr_err("  ACTHD: 0x%08x\n", I915_READ(ACTHD));
			I915_WRITE(IPEIR, ipeir);
			POSTING_READ(IPEIR);
		} else {
			u32 ipeir = I915_READ(IPEIR_I965);

			pr_err("  IPEIR: 0x%08x\n", I915_READ(IPEIR_I965));
			pr_err("  IPEHR: 0x%08x\n", I915_READ(IPEHR_I965));
			pr_err("  INSTPS: 0x%08x\n", I915_READ(INSTPS));
			pr_err("  ACTHD: 0x%08x\n", I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			POSTING_READ(IPEIR_I965);
		}
	}

	I915_WRITE(EIR, eir);
	POSTING_READ(EIR);
	eir = I915_READ(EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		DRM_ERROR("EIR stuck: 0x%08x, masking\n", eir);
		I915_WRITE(EMR, I915_READ(EMR) | eir);
		I915_WRITE(IIR, I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);
	}
}

/**
 * i915_handle_error - handle a gpu error
 * @dev_priv: i915 device private
 * @engine_mask: mask representing engines that are hung
 * Do some basic checking of register state at error time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 * @fmt: Error message format string
 */
void i915_handle_error(struct drm_i915_private *dev_priv,
		       u32 engine_mask,
		       const char *fmt, ...)
{
	va_list args;
	char error_msg[80];

	va_start(args, fmt);
	vscnprintf(error_msg, sizeof(error_msg), fmt, args);
	va_end(args);

	i915_capture_error_state(dev_priv, engine_mask, error_msg);
	i915_report_and_clear_eir(dev_priv);

	if (!engine_mask)
		return;

	if (test_and_set_bit(I915_RESET_IN_PROGRESS,
			     &dev_priv->gpu_error.flags))
		return;

	/*
	 * Wakeup waiting processes so that the reset function
	 * i915_reset_and_wakeup doesn't deadlock trying to grab
	 * various locks. By bumping the reset counter first, the woken
	 * processes will see a reset in progress and back off,
	 * releasing their locks and then wait for the reset completion.
	 * We must do this for _all_ gpu waiters that might hold locks
	 * that the reset work needs to acquire.
	 *
	 * Note: The wake_up also provides a memory barrier to ensure that the
	 * waiters see the updated value of the reset flags.
	 */
	i915_error_wake_up(dev_priv);

	i915_reset_and_wakeup(dev_priv);
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static int i915_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	if (INTEL_INFO(dev)->gen >= 4)
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_START_VBLANK_INTERRUPT_STATUS);
	else
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int ironlake_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_enable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int valleyview_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe,
			     PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int gen8_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_enable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static void i915_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_VBLANK_INTERRUPT_STATUS |
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ironlake_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ilk_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void valleyview_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void gen8_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	bdw_disable_pipe_irq(dev_priv, pipe, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static bool
ipehr_is_semaphore_wait(struct intel_engine_cs *engine, u32 ipehr)
{
	if (INTEL_GEN(engine->i915) >= 8) {
		return (ipehr >> 23) == 0x1c;
	} else {
		ipehr &= ~MI_SEMAPHORE_SYNC_MASK;
		return ipehr == (MI_SEMAPHORE_MBOX | MI_SEMAPHORE_COMPARE |
				 MI_SEMAPHORE_REGISTER);
	}
}

static struct intel_engine_cs *
semaphore_wait_to_signaller_ring(struct intel_engine_cs *engine, u32 ipehr,
				 u64 offset)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_engine_cs *signaller;

	if (INTEL_GEN(dev_priv) >= 8) {
		for_each_engine(signaller, dev_priv) {
			if (engine == signaller)
				continue;

			if (offset == signaller->semaphore.signal_ggtt[engine->hw_id])
				return signaller;
		}
	} else {
		u32 sync_bits = ipehr & MI_SEMAPHORE_SYNC_MASK;

		for_each_engine(signaller, dev_priv) {
			if(engine == signaller)
				continue;

			if (sync_bits == signaller->semaphore.mbox.wait[engine->hw_id])
				return signaller;
		}
	}

	DRM_DEBUG_DRIVER("No signaller ring found for %s, ipehr 0x%08x, offset 0x%016llx\n",
			 engine->name, ipehr, offset);

	return ERR_PTR(-ENODEV);
}

static struct intel_engine_cs *
semaphore_waits_for(struct intel_engine_cs *engine, u32 *seqno)
{
	struct drm_i915_private *dev_priv = engine->i915;
	void __iomem *vaddr;
	u32 cmd, ipehr, head;
	u64 offset = 0;
	int i, backwards;

	/*
	 * This function does not support execlist mode - any attempt to
	 * proceed further into this function will result in a kernel panic
	 * when dereferencing ring->buffer, which is not set up in execlist
	 * mode.
	 *
	 * The correct way of doing it would be to derive the currently
	 * executing ring buffer from the current context, which is derived
	 * from the currently running request. Unfortunately, to get the
	 * current request we would have to grab the struct_mutex before doing
	 * anything else, which would be ill-advised since some other thread
	 * might have grabbed it already and managed to hang itself, causing
	 * the hang checker to deadlock.
	 *
	 * Therefore, this function does not support execlist mode in its
	 * current form. Just return NULL and move on.
	 */
	if (engine->buffer == NULL)
		return NULL;

	ipehr = I915_READ(RING_IPEHR(engine->mmio_base));
	if (!ipehr_is_semaphore_wait(engine, ipehr))
		return NULL;

	/*
	 * HEAD is likely pointing to the dword after the actual command,
	 * so scan backwards until we find the MBOX. But limit it to just 3
	 * or 4 dwords depending on the semaphore wait command size.
	 * Note that we don't care about ACTHD here since that might
	 * point at at batch, and semaphores are always emitted into the
	 * ringbuffer itself.
	 */
	head = I915_READ_HEAD(engine) & HEAD_ADDR;
	backwards = (INTEL_GEN(dev_priv) >= 8) ? 5 : 4;
	vaddr = (void __iomem *)engine->buffer->vaddr;

	for (i = backwards; i; --i) {
		/*
		 * Be paranoid and presume the hw has gone off into the wild -
		 * our ring is smaller than what the hardware (and hence
		 * HEAD_ADDR) allows. Also handles wrap-around.
		 */
		head &= engine->buffer->size - 1;

		/* This here seems to blow up */
		cmd = ioread32(vaddr + head);
		if (cmd == ipehr)
			break;

		head -= 4;
	}

	if (!i)
		return NULL;

	*seqno = ioread32(vaddr + head + 4) + 1;
	if (INTEL_GEN(dev_priv) >= 8) {
		offset = ioread32(vaddr + head + 12);
		offset <<= 32;
		offset |= ioread32(vaddr + head + 8);
	}
	return semaphore_wait_to_signaller_ring(engine, ipehr, offset);
}

static int semaphore_passed(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_engine_cs *signaller;
	u32 seqno;

	engine->hangcheck.deadlock++;

	signaller = semaphore_waits_for(engine, &seqno);
	if (signaller == NULL)
		return -1;

	if (IS_ERR(signaller))
		return 0;

	/* Prevent pathological recursion due to driver bugs */
	if (signaller->hangcheck.deadlock >= I915_NUM_ENGINES)
		return -1;

	if (i915_seqno_passed(intel_engine_get_seqno(signaller), seqno))
		return 1;

	/* cursory check for an unkickable deadlock */
	if (I915_READ_CTL(signaller) & RING_WAIT_SEMAPHORE &&
	    semaphore_passed(signaller) < 0)
		return -1;

	return 0;
}

static void semaphore_clear_deadlocks(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;

	for_each_engine(engine, dev_priv)
		engine->hangcheck.deadlock = 0;
}

static bool instdone_unchanged(u32 current_instdone, u32 *old_instdone)
{
	u32 tmp = current_instdone | *old_instdone;
	bool unchanged;

	unchanged = tmp == *old_instdone;
	*old_instdone |= tmp;

	return unchanged;
}

static bool subunits_stuck(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_instdone instdone;
	struct intel_instdone *accu_instdone = &engine->hangcheck.instdone;
	bool stuck;
	int slice;
	int subslice;

	if (engine->id != RCS)
		return true;

	i915_get_engine_instdone(dev_priv, RCS, &instdone);

	/* There might be unstable subunit states even when
	 * actual head is not moving. Filter out the unstable ones by
	 * accumulating the undone -> done transitions and only
	 * consider those as progress.
	 */
	stuck = instdone_unchanged(instdone.instdone,
				   &accu_instdone->instdone);
	stuck &= instdone_unchanged(instdone.slice_common,
				    &accu_instdone->slice_common);

	for_each_instdone_slice_subslice(dev_priv, slice, subslice) {
		stuck &= instdone_unchanged(instdone.sampler[slice][subslice],
					    &accu_instdone->sampler[slice][subslice]);
		stuck &= instdone_unchanged(instdone.row[slice][subslice],
					    &accu_instdone->row[slice][subslice]);
	}

	return stuck;
}

static enum intel_engine_hangcheck_action
head_stuck(struct intel_engine_cs *engine, u64 acthd)
{
	if (acthd != engine->hangcheck.acthd) {

		/* Clear subunit states on head movement */
		memset(&engine->hangcheck.instdone, 0,
		       sizeof(engine->hangcheck.instdone));

		return HANGCHECK_ACTIVE;
	}

	if (!subunits_stuck(engine))
		return HANGCHECK_ACTIVE;

	return HANGCHECK_HUNG;
}

static enum intel_engine_hangcheck_action
engine_stuck(struct intel_engine_cs *engine, u64 acthd)
{
	struct drm_i915_private *dev_priv = engine->i915;
	enum intel_engine_hangcheck_action ha;
	u32 tmp;

	ha = head_stuck(engine, acthd);
	if (ha != HANGCHECK_HUNG)
		return ha;

	if (IS_GEN2(dev_priv))
		return HANGCHECK_HUNG;

	/* Is the chip hanging on a WAIT_FOR_EVENT?
	 * If so we can simply poke the RB_WAIT bit
	 * and break the hang. This should work on
	 * all but the second generation chipsets.
	 */
	tmp = I915_READ_CTL(engine);
	if (tmp & RING_WAIT) {
		i915_handle_error(dev_priv, 0,
				  "Kicking stuck wait on %s",
				  engine->name);
		I915_WRITE_CTL(engine, tmp);
		return HANGCHECK_KICK;
	}

	if (INTEL_GEN(dev_priv) >= 6 && tmp & RING_WAIT_SEMAPHORE) {
		switch (semaphore_passed(engine)) {
		default:
			return HANGCHECK_HUNG;
		case 1:
			i915_handle_error(dev_priv, 0,
					  "Kicking stuck semaphore on %s",
					  engine->name);
			I915_WRITE_CTL(engine, tmp);
			return HANGCHECK_KICK;
		case 0:
			return HANGCHECK_WAIT;
		}
	}

	return HANGCHECK_HUNG;
}

/*
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. We keep track per ring seqno progress and
 * if there are no progress, hangcheck score for that ring is increased.
 * Further, acthd is inspected to see if the ring is stuck. On stuck case
 * we kick the ring. If we see no progress on three subsequent calls
 * we assume chip is wedged and try to fix it by resetting the chip.
 */
static void i915_hangcheck_elapsed(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv),
			     gpu_error.hangcheck_work.work);
	struct intel_engine_cs *engine;
	unsigned int hung = 0, stuck = 0;
	int busy_count = 0;
#define BUSY 1
#define KICK 5
#define HUNG 20
#define ACTIVE_DECAY 15

	if (!i915.enable_hangcheck)
		return;

	if (!READ_ONCE(dev_priv->gt.awake))
		return;

	/* As enabling the GPU requires fairly extensive mmio access,
	 * periodically arm the mmio checker to see if we are triggering
	 * any invalid access.
	 */
	intel_uncore_arm_unclaimed_mmio_detection(dev_priv);

	for_each_engine(engine, dev_priv) {
		bool busy = intel_engine_has_waiter(engine);
		u64 acthd;
		u32 seqno;
		u32 submit;

		semaphore_clear_deadlocks(dev_priv);

		/* We don't strictly need an irq-barrier here, as we are not
		 * serving an interrupt request, be paranoid in case the
		 * barrier has side-effects (such as preventing a broken
		 * cacheline snoop) and so be sure that we can see the seqno
		 * advance. If the seqno should stick, due to a stale
		 * cacheline, we would erroneously declare the GPU hung.
		 */
		if (engine->irq_seqno_barrier)
			engine->irq_seqno_barrier(engine);

		acthd = intel_engine_get_active_head(engine);
		seqno = intel_engine_get_seqno(engine);
		submit = READ_ONCE(engine->last_submitted_seqno);

		if (engine->hangcheck.seqno == seqno) {
			if (i915_seqno_passed(seqno, submit)) {
				engine->hangcheck.action = HANGCHECK_IDLE;
			} else {
				/* We always increment the hangcheck score
				 * if the engine is busy and still processing
				 * the same request, so that no single request
				 * can run indefinitely (such as a chain of
				 * batches). The only time we do not increment
				 * the hangcheck score on this ring, if this
				 * engine is in a legitimate wait for another
				 * engine. In that case the waiting engine is a
				 * victim and we want to be sure we catch the
				 * right culprit. Then every time we do kick
				 * the ring, add a small increment to the
				 * score so that we can catch a batch that is
				 * being repeatedly kicked and so responsible
				 * for stalling the machine.
				 */
				engine->hangcheck.action =
					engine_stuck(engine, acthd);

				switch (engine->hangcheck.action) {
				case HANGCHECK_IDLE:
				case HANGCHECK_WAIT:
					break;
				case HANGCHECK_ACTIVE:
					engine->hangcheck.score += BUSY;
					break;
				case HANGCHECK_KICK:
					engine->hangcheck.score += KICK;
					break;
				case HANGCHECK_HUNG:
					engine->hangcheck.score += HUNG;
					break;
				}
			}

			if (engine->hangcheck.score >= HANGCHECK_SCORE_RING_HUNG) {
				hung |= intel_engine_flag(engine);
				if (engine->hangcheck.action != HANGCHECK_HUNG)
					stuck |= intel_engine_flag(engine);
			}
		} else {
			engine->hangcheck.action = HANGCHECK_ACTIVE;

			/* Gradually reduce the count so that we catch DoS
			 * attempts across multiple batches.
			 */
			if (engine->hangcheck.score > 0)
				engine->hangcheck.score -= ACTIVE_DECAY;
			if (engine->hangcheck.score < 0)
				engine->hangcheck.score = 0;

			/* Clear head and subunit states on seqno movement */
			acthd = 0;

			memset(&engine->hangcheck.instdone, 0,
			       sizeof(engine->hangcheck.instdone));
		}

		engine->hangcheck.seqno = seqno;
		engine->hangcheck.acthd = acthd;
		busy_count += busy;
	}

	if (hung) {
		char msg[80];
		unsigned int tmp;
		int len;

		/* If some rings hung but others were still busy, only
		 * blame the hanging rings in the synopsis.
		 */
		if (stuck != hung)
			hung &= ~stuck;
		len = scnprintf(msg, sizeof(msg),
				"%s on ", stuck == hung ? "No progress" : "Hang");
		for_each_engine_masked(engine, dev_priv, hung, tmp)
			len += scnprintf(msg + len, sizeof(msg) - len,
					 "%s, ", engine->name);
		msg[len-2] = '\0';

		return i915_handle_error(dev_priv, hung, msg);
	}

	/* Reset timer in case GPU hangs without another request being added */
	if (busy_count)
		i915_queue_hangcheck(dev_priv);
}

static void ibx_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (HAS_PCH_NOP(dev))
		return;

	GEN5_IRQ_RESET(SDE);

	if (HAS_PCH_CPT(dev) || HAS_PCH_LPT(dev))
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
static void ibx_irq_pre_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (HAS_PCH_NOP(dev))
		return;

	WARN_ON(I915_READ(SDEIER) != 0);
	I915_WRITE(SDEIER, 0xffffffff);
	POSTING_READ(SDEIER);
}

static void gen5_gt_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	GEN5_IRQ_RESET(GT);
	if (INTEL_INFO(dev)->gen >= 6)
		GEN5_IRQ_RESET(GEN6_PM);
}

static void vlv_display_irq_reset(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	if (IS_CHERRYVIEW(dev_priv))
		I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK_CHV);
	else
		I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK);

	i915_hotplug_interrupt_update_locked(dev_priv, 0xffffffff, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	for_each_pipe(dev_priv, pipe) {
		I915_WRITE(PIPESTAT(pipe),
			   PIPE_FIFO_UNDERRUN_STATUS |
			   PIPESTAT_INT_STATUS_MASK);
		dev_priv->pipestat_irq_mask[pipe] = 0;
	}

	GEN5_IRQ_RESET(VLV_);
	dev_priv->irq_mask = ~0;
}

static void vlv_display_irq_postinstall(struct drm_i915_private *dev_priv)
{
	u32 pipestat_mask;
	u32 enable_mask;
	enum pipe pipe;

	pipestat_mask = PLANE_FLIP_DONE_INT_STATUS_VLV |
			PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(dev_priv, pipe)
		i915_enable_pipestat(dev_priv, pipe, pipestat_mask);

	enable_mask = I915_DISPLAY_PORT_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
	if (IS_CHERRYVIEW(dev_priv))
		enable_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT;

	WARN_ON(dev_priv->irq_mask != ~0);

	dev_priv->irq_mask = ~enable_mask;

	GEN5_IRQ_INIT(VLV_, dev_priv->irq_mask, enable_mask);
}

/* drm_dma.h hooks
*/
static void ironlake_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(HWSTAM, 0xffffffff);

	GEN5_IRQ_RESET(DE);
	if (IS_GEN7(dev))
		I915_WRITE(GEN7_ERR_INT, 0xffffffff);

	gen5_gt_irq_reset(dev);

	ibx_irq_reset(dev);
}

static void valleyview_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(VLV_MASTER_IER, 0);
	POSTING_READ(VLV_MASTER_IER);

	gen5_gt_irq_reset(dev);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void gen8_gt_irq_reset(struct drm_i915_private *dev_priv)
{
	GEN8_IRQ_RESET_NDX(GT, 0);
	GEN8_IRQ_RESET_NDX(GT, 1);
	GEN8_IRQ_RESET_NDX(GT, 2);
	GEN8_IRQ_RESET_NDX(GT, 3);
}

static void gen8_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
						   POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_RESET_NDX(DE_PIPE, pipe);

	GEN5_IRQ_RESET(GEN8_DE_PORT_);
	GEN5_IRQ_RESET(GEN8_DE_MISC_);
	GEN5_IRQ_RESET(GEN8_PCU_);

	if (HAS_PCH_SPLIT(dev))
		ibx_irq_reset(dev);
}

void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     unsigned int pipe_mask)
{
	uint32_t extra_ier = GEN8_PIPE_VBLANK | GEN8_PIPE_FIFO_UNDERRUN;
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);
	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_INIT_NDX(DE_PIPE, pipe,
				  dev_priv->de_irq_mask[pipe],
				  ~dev_priv->de_irq_mask[pipe] | extra_ier);
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     unsigned int pipe_mask)
{
	enum pipe pipe;

	spin_lock_irq(&dev_priv->irq_lock);
	for_each_pipe_masked(dev_priv, pipe, pipe_mask)
		GEN8_IRQ_RESET_NDX(DE_PIPE, pipe);
	spin_unlock_irq(&dev_priv->irq_lock);

	/* make sure we're done processing display irqs */
	synchronize_irq(dev_priv->drm.irq);
}

static void cherryview_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	GEN5_IRQ_RESET(GEN8_PCU_);

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

static void ibx_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, hotplug, enabled_irqs;

	if (HAS_PCH_IBX(dev_priv)) {
		hotplug_irqs = SDE_HOTPLUG_MASK;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_ibx);
	} else {
		hotplug_irqs = SDE_HOTPLUG_MASK_CPT;
		enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_cpt);
	}

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	/*
	 * Enable digital hotplug on the PCH, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec).
	 * The pulse duration bits are reserved on LPT+.
	 */
	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTD_PULSE_DURATION_MASK|PORTC_PULSE_DURATION_MASK|PORTB_PULSE_DURATION_MASK);
	hotplug |= PORTD_HOTPLUG_ENABLE | PORTD_PULSE_DURATION_2ms;
	hotplug |= PORTC_HOTPLUG_ENABLE | PORTC_PULSE_DURATION_2ms;
	hotplug |= PORTB_HOTPLUG_ENABLE | PORTB_PULSE_DURATION_2ms;
	/*
	 * When CPU and PCH are on the same package, port A
	 * HPD must be enabled in both north and south.
	 */
	if (HAS_PCH_LPT_LP(dev_priv))
		hotplug |= PORTA_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);
}

static void spt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, hotplug, enabled_irqs;

	hotplug_irqs = SDE_HOTPLUG_MASK_SPT;
	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_spt);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	/* Enable digital hotplug on the PCH */
	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug |= PORTD_HOTPLUG_ENABLE | PORTC_HOTPLUG_ENABLE |
		PORTB_HOTPLUG_ENABLE | PORTA_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);

	hotplug = I915_READ(PCH_PORT_HOTPLUG2);
	hotplug |= PORTE_HOTPLUG_ENABLE;
	I915_WRITE(PCH_PORT_HOTPLUG2, hotplug);
}

static void ilk_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, hotplug, enabled_irqs;

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

	/*
	 * Enable digital hotplug on the CPU, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec)
	 * The pulse duration bits are reserved on HSW+.
	 */
	hotplug = I915_READ(DIGITAL_PORT_HOTPLUG_CNTRL);
	hotplug &= ~DIGITAL_PORTA_PULSE_DURATION_MASK;
	hotplug |= DIGITAL_PORTA_HOTPLUG_ENABLE | DIGITAL_PORTA_PULSE_DURATION_2ms;
	I915_WRITE(DIGITAL_PORT_HOTPLUG_CNTRL, hotplug);

	ibx_hpd_irq_setup(dev_priv);
}

static void bxt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, hotplug, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, hpd_bxt);
	hotplug_irqs = BXT_DE_PORT_HOTPLUG_MASK;

	bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);

	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug |= PORTC_HOTPLUG_ENABLE | PORTB_HOTPLUG_ENABLE |
		PORTA_HOTPLUG_ENABLE;

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

static void ibx_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 mask;

	if (HAS_PCH_NOP(dev))
		return;

	if (HAS_PCH_IBX(dev))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;

	gen5_assert_iir_is_zero(dev_priv, SDEIIR);
	I915_WRITE(SDEIMR, ~mask);
}

static void gen5_gt_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 pm_irqs, gt_irqs;

	pm_irqs = gt_irqs = 0;

	dev_priv->gt_irq_mask = ~0;
	if (HAS_L3_DPF(dev)) {
		/* L3 parity interrupt is always unmasked. */
		dev_priv->gt_irq_mask = ~GT_PARITY_ERROR(dev);
		gt_irqs |= GT_PARITY_ERROR(dev);
	}

	gt_irqs |= GT_RENDER_USER_INTERRUPT;
	if (IS_GEN5(dev)) {
		gt_irqs |= ILK_BSD_USER_INTERRUPT;
	} else {
		gt_irqs |= GT_BLT_USER_INTERRUPT | GT_BSD_USER_INTERRUPT;
	}

	GEN5_IRQ_INIT(GT, dev_priv->gt_irq_mask, gt_irqs);

	if (INTEL_INFO(dev)->gen >= 6) {
		/*
		 * RPS interrupts will get enabled/disabled on demand when RPS
		 * itself is enabled/disabled.
		 */
		if (HAS_VEBOX(dev))
			pm_irqs |= PM_VEBOX_USER_INTERRUPT;

		dev_priv->pm_irq_mask = 0xffffffff;
		GEN5_IRQ_INIT(GEN6_PM, dev_priv->pm_irq_mask, pm_irqs);
	}
}

static int ironlake_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 display_mask, extra_mask;

	if (INTEL_INFO(dev)->gen >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_PLANEC_FLIP_DONE_IVB |
				DE_PLANEB_FLIP_DONE_IVB |
				DE_PLANEA_FLIP_DONE_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB |
			      DE_DP_A_HOTPLUG_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE |
				DE_AUX_CHANNEL_A |
				DE_PIPEB_CRC_DONE | DE_PIPEA_CRC_DONE |
				DE_POISON);
		extra_mask = (DE_PIPEA_VBLANK | DE_PIPEB_VBLANK | DE_PCU_EVENT |
			      DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN |
			      DE_DP_A_HOTPLUG);
	}

	dev_priv->irq_mask = ~display_mask;

	I915_WRITE(HWSTAM, 0xeffe);

	ibx_irq_pre_postinstall(dev);

	GEN5_IRQ_INIT(DE, dev_priv->irq_mask, display_mask | extra_mask);

	gen5_gt_irq_postinstall(dev);

	ibx_irq_postinstall(dev);

	if (IS_IRONLAKE_M(dev)) {
		/* Enable PCU event interrupts
		 *
		 * spinlocking not required here for correctness since interrupt
		 * setup is guaranteed to run in single-threaded context. But we
		 * need it to make the assert_spin_locked happy. */
		spin_lock_irq(&dev_priv->irq_lock);
		ilk_enable_display_irq(dev_priv, DE_PCU_EVENT);
		spin_unlock_irq(&dev_priv->irq_lock);
	}

	return 0;
}

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv)
{
	assert_spin_locked(&dev_priv->irq_lock);

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
	assert_spin_locked(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = false;

	if (intel_irqs_enabled(dev_priv))
		vlv_display_irq_reset(dev_priv);
}


static int valleyview_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	gen5_gt_irq_postinstall(dev);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);
	POSTING_READ(VLV_MASTER_IER);

	return 0;
}

static void gen8_gt_irq_postinstall(struct drm_i915_private *dev_priv)
{
	/* These are interrupts we'll toggle with the ring mask register */
	uint32_t gt_interrupts[] = {
		GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
			GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_BCS_IRQ_SHIFT,
		GT_RENDER_USER_INTERRUPT << GEN8_VCS1_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS1_IRQ_SHIFT |
			GT_RENDER_USER_INTERRUPT << GEN8_VCS2_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS2_IRQ_SHIFT,
		0,
		GT_RENDER_USER_INTERRUPT << GEN8_VECS_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VECS_IRQ_SHIFT
		};

	if (HAS_L3_DPF(dev_priv))
		gt_interrupts[0] |= GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	dev_priv->pm_irq_mask = 0xffffffff;
	GEN8_IRQ_INIT_NDX(GT, 0, ~gt_interrupts[0], gt_interrupts[0]);
	GEN8_IRQ_INIT_NDX(GT, 1, ~gt_interrupts[1], gt_interrupts[1]);
	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself
	 * is enabled/disabled.
	 */
	GEN8_IRQ_INIT_NDX(GT, 2, dev_priv->pm_irq_mask, 0);
	GEN8_IRQ_INIT_NDX(GT, 3, ~gt_interrupts[3], gt_interrupts[3]);
}

static void gen8_de_irq_postinstall(struct drm_i915_private *dev_priv)
{
	uint32_t de_pipe_masked = GEN8_PIPE_CDCLK_CRC_DONE;
	uint32_t de_pipe_enables;
	u32 de_port_masked = GEN8_AUX_CHANNEL_A;
	u32 de_port_enables;
	u32 de_misc_masked = GEN8_DE_MISC_GSE;
	enum pipe pipe;

	if (INTEL_INFO(dev_priv)->gen >= 9) {
		de_pipe_masked |= GEN9_PIPE_PLANE1_FLIP_DONE |
				  GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
		de_port_masked |= GEN9_AUX_CHANNEL_B | GEN9_AUX_CHANNEL_C |
				  GEN9_AUX_CHANNEL_D;
		if (IS_BROXTON(dev_priv))
			de_port_masked |= BXT_DE_PORT_GMBUS;
	} else {
		de_pipe_masked |= GEN8_PIPE_PRIMARY_FLIP_DONE |
				  GEN8_DE_PIPE_IRQ_FAULT_ERRORS;
	}

	de_pipe_enables = de_pipe_masked | GEN8_PIPE_VBLANK |
					   GEN8_PIPE_FIFO_UNDERRUN;

	de_port_enables = de_port_masked;
	if (IS_BROXTON(dev_priv))
		de_port_enables |= BXT_DE_PORT_HOTPLUG_MASK;
	else if (IS_BROADWELL(dev_priv))
		de_port_enables |= GEN8_PORT_DP_A_HOTPLUG;

	dev_priv->de_irq_mask[PIPE_A] = ~de_pipe_masked;
	dev_priv->de_irq_mask[PIPE_B] = ~de_pipe_masked;
	dev_priv->de_irq_mask[PIPE_C] = ~de_pipe_masked;

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_INIT_NDX(DE_PIPE, pipe,
					  dev_priv->de_irq_mask[pipe],
					  de_pipe_enables);

	GEN5_IRQ_INIT(GEN8_DE_PORT_, ~de_port_masked, de_port_enables);
	GEN5_IRQ_INIT(GEN8_DE_MISC_, ~de_misc_masked, de_misc_masked);
}

static int gen8_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (HAS_PCH_SPLIT(dev))
		ibx_irq_pre_postinstall(dev);

	gen8_gt_irq_postinstall(dev_priv);
	gen8_de_irq_postinstall(dev_priv);

	if (HAS_PCH_SPLIT(dev))
		ibx_irq_postinstall(dev);

	I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	POSTING_READ(GEN8_MASTER_IRQ);

	return 0;
}

static int cherryview_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	gen8_gt_irq_postinstall(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_postinstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	POSTING_READ(GEN8_MASTER_IRQ);

	return 0;
}

static void gen8_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!dev_priv)
		return;

	gen8_irq_reset(dev);
}

static void valleyview_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!dev_priv)
		return;

	I915_WRITE(VLV_MASTER_IER, 0);
	POSTING_READ(VLV_MASTER_IER);

	gen5_gt_irq_reset(dev);

	I915_WRITE(HWSTAM, 0xffffffff);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void cherryview_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!dev_priv)
		return;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	GEN5_IRQ_RESET(GEN8_PCU_);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		vlv_display_irq_reset(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void ironlake_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!dev_priv)
		return;

	ironlake_irq_reset(dev);
}

static void i8xx_irq_preinstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE16(IMR, 0xffff);
	I915_WRITE16(IER, 0x0);
	POSTING_READ16(IER);
}

static int i8xx_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE16(EMR,
		     ~(I915_ERROR_PAGE_TABLE | I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		  I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT);
	I915_WRITE16(IMR, dev_priv->irq_mask);

	I915_WRITE16(IER,
		     I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		     I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		     I915_USER_INTERRUPT);
	POSTING_READ16(IER);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	return 0;
}

/*
 * Returns true when a page flip has completed.
 */
static bool i8xx_handle_vblank(struct drm_i915_private *dev_priv,
			       int plane, int pipe, u32 iir)
{
	u16 flip_pending = DISPLAY_PLANE_FLIP_PENDING(plane);

	if (!intel_pipe_handle_vblank(dev_priv, pipe))
		return false;

	if ((iir & flip_pending) == 0)
		goto check_page_flip;

	/* We detect FlipDone by looking for the change in PendingFlip from '1'
	 * to '0' on the following vblank, i.e. IIR has the Pendingflip
	 * asserted following the MI_DISPLAY_FLIP, but ISR is deasserted, hence
	 * the flip is completed (no longer pending). Since this doesn't raise
	 * an interrupt per se, we watch for the change at vblank.
	 */
	if (I915_READ16(ISR) & flip_pending)
		goto check_page_flip;

	intel_finish_page_flip_cs(dev_priv, pipe);
	return true;

check_page_flip:
	intel_check_page_flip(dev_priv, pipe);
	return false;
}

static irqreturn_t i8xx_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u16 iir, new_iir;
	u32 pipe_stats[2];
	int pipe;
	u16 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;
	irqreturn_t ret;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	ret = IRQ_NONE;
	iir = I915_READ16(IIR);
	if (iir == 0)
		goto out;

	while (iir & ~flip_mask) {
		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock(&dev_priv->irq_lock);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			DRM_DEBUG("Command parser error, iir 0x%08x\n", iir);

		for_each_pipe(dev_priv, pipe) {
			i915_reg_t reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/*
			 * Clear the PIPE*STAT regs before the IIR
			 */
			if (pipe_stats[pipe] & 0x8000ffff)
				I915_WRITE(reg, pipe_stats[pipe]);
		}
		spin_unlock(&dev_priv->irq_lock);

		I915_WRITE16(IIR, iir & ~flip_mask);
		new_iir = I915_READ16(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(&dev_priv->engine[RCS]);

		for_each_pipe(dev_priv, pipe) {
			int plane = pipe;
			if (HAS_FBC(dev_priv))
				plane = !plane;

			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS &&
			    i8xx_handle_vblank(dev_priv, plane, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(plane);

			if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
				i9xx_pipe_crc_irq_handler(dev_priv, pipe);

			if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
				intel_cpu_fifo_underrun_irq_handler(dev_priv,
								    pipe);
		}

		iir = new_iir;
	}
	ret = IRQ_HANDLED;

out:
	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static void i8xx_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	for_each_pipe(dev_priv, pipe) {
		/* Clear enable bits; then clear status bits */
		I915_WRITE(PIPESTAT(pipe), 0);
		I915_WRITE(PIPESTAT(pipe), I915_READ(PIPESTAT(pipe)));
	}
	I915_WRITE16(IMR, 0xffff);
	I915_WRITE16(IER, 0x0);
	I915_WRITE16(IIR, I915_READ16(IIR));
}

static void i915_irq_preinstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	if (I915_HAS_HOTPLUG(dev)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE16(HWSTAM, 0xeffe);
	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	POSTING_READ(IER);
}

static int i915_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 enable_mask;

	I915_WRITE(EMR, ~(I915_ERROR_PAGE_TABLE | I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		  I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_USER_INTERRUPT;

	if (I915_HAS_HOTPLUG(dev)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		POSTING_READ(PORT_HOTPLUG_EN);

		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		dev_priv->irq_mask &= ~I915_DISPLAY_PORT_INTERRUPT;
	}

	I915_WRITE(IMR, dev_priv->irq_mask);
	I915_WRITE(IER, enable_mask);
	POSTING_READ(IER);

	i915_enable_asle_pipestat(dev_priv);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

	return 0;
}

/*
 * Returns true when a page flip has completed.
 */
static bool i915_handle_vblank(struct drm_i915_private *dev_priv,
			       int plane, int pipe, u32 iir)
{
	u32 flip_pending = DISPLAY_PLANE_FLIP_PENDING(plane);

	if (!intel_pipe_handle_vblank(dev_priv, pipe))
		return false;

	if ((iir & flip_pending) == 0)
		goto check_page_flip;

	/* We detect FlipDone by looking for the change in PendingFlip from '1'
	 * to '0' on the following vblank, i.e. IIR has the Pendingflip
	 * asserted following the MI_DISPLAY_FLIP, but ISR is deasserted, hence
	 * the flip is completed (no longer pending). Since this doesn't raise
	 * an interrupt per se, we watch for the change at vblank.
	 */
	if (I915_READ(ISR) & flip_pending)
		goto check_page_flip;

	intel_finish_page_flip_cs(dev_priv, pipe);
	return true;

check_page_flip:
	intel_check_page_flip(dev_priv, pipe);
	return false;
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 iir, new_iir, pipe_stats[I915_MAX_PIPES];
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;
	int pipe, ret = IRQ_NONE;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	iir = I915_READ(IIR);
	do {
		bool irq_received = (iir & ~flip_mask) != 0;
		bool blc_event = false;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock(&dev_priv->irq_lock);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			DRM_DEBUG("Command parser error, iir 0x%08x\n", iir);

		for_each_pipe(dev_priv, pipe) {
			i915_reg_t reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/* Clear the PIPE*STAT regs before the IIR */
			if (pipe_stats[pipe] & 0x8000ffff) {
				I915_WRITE(reg, pipe_stats[pipe]);
				irq_received = true;
			}
		}
		spin_unlock(&dev_priv->irq_lock);

		if (!irq_received)
			break;

		/* Consume port.  Then clear IIR or we'll miss events */
		if (I915_HAS_HOTPLUG(dev_priv) &&
		    iir & I915_DISPLAY_PORT_INTERRUPT) {
			u32 hotplug_status = i9xx_hpd_irq_ack(dev_priv);
			if (hotplug_status)
				i9xx_hpd_irq_handler(dev_priv, hotplug_status);
		}

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(&dev_priv->engine[RCS]);

		for_each_pipe(dev_priv, pipe) {
			int plane = pipe;
			if (HAS_FBC(dev_priv))
				plane = !plane;

			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev_priv, plane, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(plane);

			if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
				blc_event = true;

			if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
				i9xx_pipe_crc_irq_handler(dev_priv, pipe);

			if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
				intel_cpu_fifo_underrun_irq_handler(dev_priv,
								    pipe);
		}

		if (blc_event || (iir & I915_ASLE_INTERRUPT))
			intel_opregion_asle_intr(dev_priv);

		/* With MSI, interrupts are only generated when iir
		 * transitions from zero to nonzero.  If another bit got
		 * set while we were handling the existing iir bits, then
		 * we would never get another interrupt.
		 *
		 * This is fine on non-MSI as well, as if we hit this path
		 * we avoid exiting the interrupt handler only to generate
		 * another one.
		 *
		 * Note that for MSI this could cause a stray interrupt report
		 * if an interrupt landed in the time between writing IIR and
		 * the posting read.  This should be rare enough to never
		 * trigger the 99% of 100,000 interrupts test for disabling
		 * stray interrupts.
		 */
		ret = IRQ_HANDLED;
		iir = new_iir;
	} while (iir & ~flip_mask);

	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static void i915_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	if (I915_HAS_HOTPLUG(dev)) {
		i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE16(HWSTAM, 0xffff);
	for_each_pipe(dev_priv, pipe) {
		/* Clear enable bits; then clear status bits */
		I915_WRITE(PIPESTAT(pipe), 0);
		I915_WRITE(PIPESTAT(pipe), I915_READ(PIPESTAT(pipe)));
	}
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	I915_WRITE(IIR, I915_READ(IIR));
}

static void i965_irq_preinstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	I915_WRITE(HWSTAM, 0xeffe);
	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	POSTING_READ(IER);
}

static int i965_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 enable_mask;
	u32 error_mask;

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask = ~(I915_ASLE_INTERRUPT |
			       I915_DISPLAY_PORT_INTERRUPT |
			       I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
			       I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
			       I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
			       I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT |
			       I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);

	enable_mask = ~dev_priv->irq_mask;
	enable_mask &= ~(I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
			 I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT);
	enable_mask |= I915_USER_INTERRUPT;

	if (IS_G4X(dev_priv))
		enable_mask |= I915_BSD_USER_INTERRUPT;

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_CRC_DONE_INTERRUPT_STATUS);
	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_CRC_DONE_INTERRUPT_STATUS);
	spin_unlock_irq(&dev_priv->irq_lock);

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

	I915_WRITE(IMR, dev_priv->irq_mask);
	I915_WRITE(IER, enable_mask);
	POSTING_READ(IER);

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	POSTING_READ(PORT_HOTPLUG_EN);

	i915_enable_asle_pipestat(dev_priv);

	return 0;
}

static void i915_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_en;

	assert_spin_locked(&dev_priv->irq_lock);

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
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 iir, new_iir;
	u32 pipe_stats[I915_MAX_PIPES];
	int ret = IRQ_NONE, pipe;
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;

	if (!intel_irqs_enabled(dev_priv))
		return IRQ_NONE;

	/* IRQs are synced during runtime_suspend, we don't require a wakeref */
	disable_rpm_wakeref_asserts(dev_priv);

	iir = I915_READ(IIR);

	for (;;) {
		bool irq_received = (iir & ~flip_mask) != 0;
		bool blc_event = false;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock(&dev_priv->irq_lock);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			DRM_DEBUG("Command parser error, iir 0x%08x\n", iir);

		for_each_pipe(dev_priv, pipe) {
			i915_reg_t reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/*
			 * Clear the PIPE*STAT regs before the IIR
			 */
			if (pipe_stats[pipe] & 0x8000ffff) {
				I915_WRITE(reg, pipe_stats[pipe]);
				irq_received = true;
			}
		}
		spin_unlock(&dev_priv->irq_lock);

		if (!irq_received)
			break;

		ret = IRQ_HANDLED;

		/* Consume port.  Then clear IIR or we'll miss events */
		if (iir & I915_DISPLAY_PORT_INTERRUPT) {
			u32 hotplug_status = i9xx_hpd_irq_ack(dev_priv);
			if (hotplug_status)
				i9xx_hpd_irq_handler(dev_priv, hotplug_status);
		}

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(&dev_priv->engine[RCS]);
		if (iir & I915_BSD_USER_INTERRUPT)
			notify_ring(&dev_priv->engine[VCS]);

		for_each_pipe(dev_priv, pipe) {
			if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev_priv, pipe, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(pipe);

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

		/* With MSI, interrupts are only generated when iir
		 * transitions from zero to nonzero.  If another bit got
		 * set while we were handling the existing iir bits, then
		 * we would never get another interrupt.
		 *
		 * This is fine on non-MSI as well, as if we hit this path
		 * we avoid exiting the interrupt handler only to generate
		 * another one.
		 *
		 * Note that for MSI this could cause a stray interrupt report
		 * if an interrupt landed in the time between writing IIR and
		 * the posting read.  This should be rare enough to never
		 * trigger the 99% of 100,000 interrupts test for disabling
		 * stray interrupts.
		 */
		iir = new_iir;
	}

	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static void i965_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int pipe;

	if (!dev_priv)
		return;

	i915_hotplug_interrupt_update(dev_priv, 0xffffffff, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	I915_WRITE(HWSTAM, 0xffffffff);
	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe),
			   I915_READ(PIPESTAT(pipe)) & 0x8000ffff);
	I915_WRITE(IIR, I915_READ(IIR));
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

	intel_hpd_init_work(dev_priv);

	INIT_WORK(&dev_priv->rps.work, gen6_pm_rps_work);
	INIT_WORK(&dev_priv->l3_parity.error_work, ivybridge_parity_work);

	/* Let's track the enabled rps events */
	if (IS_VALLEYVIEW(dev_priv))
		/* WaGsvRC0ResidencyMethod:vlv */
		dev_priv->pm_rps_events = GEN6_PM_RP_DOWN_EI_EXPIRED | GEN6_PM_RP_UP_EI_EXPIRED;
	else
		dev_priv->pm_rps_events = GEN6_PM_RPS_EVENTS;

	dev_priv->rps.pm_intr_keep = 0;

	/*
	 * SNB,IVB can while VLV,CHV may hard hang on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 *
	 * TODO: verify if this can be reproduced on VLV,CHV.
	 */
	if (INTEL_INFO(dev_priv)->gen <= 7 && !IS_HASWELL(dev_priv))
		dev_priv->rps.pm_intr_keep |= GEN6_PM_RP_UP_EI_EXPIRED;

	if (INTEL_INFO(dev_priv)->gen >= 8)
		dev_priv->rps.pm_intr_keep |= GEN8_PMINTR_REDIRECT_TO_GUC;

	INIT_DELAYED_WORK(&dev_priv->gpu_error.hangcheck_work,
			  i915_hangcheck_elapsed);

	if (IS_GEN2(dev_priv)) {
		/* Gen2 doesn't have a hardware frame counter */
		dev->max_vblank_count = 0;
		dev->driver->get_vblank_counter = drm_vblank_no_hw_counter;
	} else if (IS_G4X(dev_priv) || INTEL_INFO(dev_priv)->gen >= 5) {
		dev->max_vblank_count = 0xffffffff; /* full 32 bit counter */
		dev->driver->get_vblank_counter = g4x_get_vblank_counter;
	} else {
		dev->driver->get_vblank_counter = i915_get_vblank_counter;
		dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */
	}

	/*
	 * Opt out of the vblank disable timer on everything except gen2.
	 * Gen2 doesn't have a hardware frame counter and so depends on
	 * vblank interrupts to produce sane vblank seuquence numbers.
	 */
	if (!IS_GEN2(dev_priv))
		dev->vblank_disable_immediate = true;

	dev->driver->get_vblank_timestamp = i915_get_vblank_timestamp;
	dev->driver->get_scanout_position = i915_get_crtc_scanoutpos;

	if (IS_CHERRYVIEW(dev_priv)) {
		dev->driver->irq_handler = cherryview_irq_handler;
		dev->driver->irq_preinstall = cherryview_irq_preinstall;
		dev->driver->irq_postinstall = cherryview_irq_postinstall;
		dev->driver->irq_uninstall = cherryview_irq_uninstall;
		dev->driver->enable_vblank = valleyview_enable_vblank;
		dev->driver->disable_vblank = valleyview_disable_vblank;
		dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		dev->driver->irq_handler = valleyview_irq_handler;
		dev->driver->irq_preinstall = valleyview_irq_preinstall;
		dev->driver->irq_postinstall = valleyview_irq_postinstall;
		dev->driver->irq_uninstall = valleyview_irq_uninstall;
		dev->driver->enable_vblank = valleyview_enable_vblank;
		dev->driver->disable_vblank = valleyview_disable_vblank;
		dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
	} else if (INTEL_INFO(dev_priv)->gen >= 8) {
		dev->driver->irq_handler = gen8_irq_handler;
		dev->driver->irq_preinstall = gen8_irq_reset;
		dev->driver->irq_postinstall = gen8_irq_postinstall;
		dev->driver->irq_uninstall = gen8_irq_uninstall;
		dev->driver->enable_vblank = gen8_enable_vblank;
		dev->driver->disable_vblank = gen8_disable_vblank;
		if (IS_BROXTON(dev))
			dev_priv->display.hpd_irq_setup = bxt_hpd_irq_setup;
		else if (HAS_PCH_SPT(dev) || HAS_PCH_KBP(dev))
			dev_priv->display.hpd_irq_setup = spt_hpd_irq_setup;
		else
			dev_priv->display.hpd_irq_setup = ilk_hpd_irq_setup;
	} else if (HAS_PCH_SPLIT(dev)) {
		dev->driver->irq_handler = ironlake_irq_handler;
		dev->driver->irq_preinstall = ironlake_irq_reset;
		dev->driver->irq_postinstall = ironlake_irq_postinstall;
		dev->driver->irq_uninstall = ironlake_irq_uninstall;
		dev->driver->enable_vblank = ironlake_enable_vblank;
		dev->driver->disable_vblank = ironlake_disable_vblank;
		dev_priv->display.hpd_irq_setup = ilk_hpd_irq_setup;
	} else {
		if (IS_GEN2(dev_priv)) {
			dev->driver->irq_preinstall = i8xx_irq_preinstall;
			dev->driver->irq_postinstall = i8xx_irq_postinstall;
			dev->driver->irq_handler = i8xx_irq_handler;
			dev->driver->irq_uninstall = i8xx_irq_uninstall;
		} else if (IS_GEN3(dev_priv)) {
			dev->driver->irq_preinstall = i915_irq_preinstall;
			dev->driver->irq_postinstall = i915_irq_postinstall;
			dev->driver->irq_uninstall = i915_irq_uninstall;
			dev->driver->irq_handler = i915_irq_handler;
		} else {
			dev->driver->irq_preinstall = i965_irq_preinstall;
			dev->driver->irq_postinstall = i965_irq_postinstall;
			dev->driver->irq_uninstall = i965_irq_uninstall;
			dev->driver->irq_handler = i965_irq_handler;
		}
		if (I915_HAS_HOTPLUG(dev_priv))
			dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
		dev->driver->enable_vblank = i915_enable_vblank;
		dev->driver->disable_vblank = i915_disable_vblank;
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
	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid
	 * special cases in our ordering checks.
	 */
	dev_priv->pm.irqs_enabled = true;

	return drm_irq_install(&dev_priv->drm, dev_priv->drm.pdev->irq);
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
	drm_irq_uninstall(&dev_priv->drm);
	intel_hpd_cancel_work(dev_priv);
	dev_priv->pm.irqs_enabled = false;
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
	dev_priv->drm.driver->irq_uninstall(&dev_priv->drm);
	dev_priv->pm.irqs_enabled = false;
	synchronize_irq(dev_priv->drm.irq);
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
	dev_priv->pm.irqs_enabled = true;
	dev_priv->drm.driver->irq_preinstall(&dev_priv->drm);
	dev_priv->drm.driver->irq_postinstall(&dev_priv->drm);
}
