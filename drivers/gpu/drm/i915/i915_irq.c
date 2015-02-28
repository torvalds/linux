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

static const u32 hpd_status_i915[HPD_NUM_PINS] = { /* i915 and valleyview are the same */
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I915,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I915,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS
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
#define GEN5_ASSERT_IIR_IS_ZERO(reg) do { \
	u32 val = I915_READ(reg); \
	if (val) { \
		WARN(1, "Interrupt register 0x%x is not zero: 0x%08x\n", \
		     (reg), val); \
		I915_WRITE((reg), 0xffffffff); \
		POSTING_READ(reg); \
		I915_WRITE((reg), 0xffffffff); \
		POSTING_READ(reg); \
	} \
} while (0)

#define GEN8_IRQ_INIT_NDX(type, which, imr_val, ier_val) do { \
	GEN5_ASSERT_IIR_IS_ZERO(GEN8_##type##_IIR(which)); \
	I915_WRITE(GEN8_##type##_IER(which), (ier_val)); \
	I915_WRITE(GEN8_##type##_IMR(which), (imr_val)); \
	POSTING_READ(GEN8_##type##_IMR(which)); \
} while (0)

#define GEN5_IRQ_INIT(type, imr_val, ier_val) do { \
	GEN5_ASSERT_IIR_IS_ZERO(type##IIR); \
	I915_WRITE(type##IER, (ier_val)); \
	I915_WRITE(type##IMR, (imr_val)); \
	POSTING_READ(type##IMR); \
} while (0)

static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir);

/* For display hotplug interrupt */
void
ironlake_enable_display_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	if ((dev_priv->irq_mask & mask) != 0) {
		dev_priv->irq_mask &= ~mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask);
		POSTING_READ(DEIMR);
	}
}

void
ironlake_disable_display_irq(struct drm_i915_private *dev_priv, u32 mask)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (WARN_ON(!intel_irqs_enabled(dev_priv)))
		return;

	if ((dev_priv->irq_mask & mask) != mask) {
		dev_priv->irq_mask |= mask;
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
	POSTING_READ(GTIMR);
}

void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, mask);
}

void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, 0);
}

static u32 gen6_pm_iir(struct drm_i915_private *dev_priv)
{
	return INTEL_INFO(dev_priv)->gen >= 8 ? GEN8_GT_IIR(2) : GEN6_PMIIR;
}

static u32 gen6_pm_imr(struct drm_i915_private *dev_priv)
{
	return INTEL_INFO(dev_priv)->gen >= 8 ? GEN8_GT_IMR(2) : GEN6_PMIMR;
}

static u32 gen6_pm_ier(struct drm_i915_private *dev_priv)
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

void gen6_reset_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t reg = gen6_pm_iir(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	I915_WRITE(reg, dev_priv->pm_rps_events);
	I915_WRITE(reg, dev_priv->pm_rps_events);
	POSTING_READ(reg);
	spin_unlock_irq(&dev_priv->irq_lock);
}

void gen6_enable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	spin_lock_irq(&dev_priv->irq_lock);

	WARN_ON(dev_priv->rps.pm_iir);
	WARN_ON(I915_READ(gen6_pm_iir(dev_priv)) & dev_priv->pm_rps_events);
	dev_priv->rps.interrupts_enabled = true;
	I915_WRITE(gen6_pm_ier(dev_priv), I915_READ(gen6_pm_ier(dev_priv)) |
				dev_priv->pm_rps_events);
	gen6_enable_pm_irq(dev_priv, dev_priv->pm_rps_events);

	spin_unlock_irq(&dev_priv->irq_lock);
}

u32 gen6_sanitize_rps_pm_mask(struct drm_i915_private *dev_priv, u32 mask)
{
	/*
	 * SNB,IVB can while VLV,CHV may hard hang on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 *
	 * TODO: verify if this can be reproduced on VLV,CHV.
	 */
	if (INTEL_INFO(dev_priv)->gen <= 7 && !IS_HASWELL(dev_priv))
		mask &= ~GEN6_PM_RP_UP_EI_EXPIRED;

	if (INTEL_INFO(dev_priv)->gen >= 8)
		mask &= ~GEN8_PMINTR_REDIRECT_TO_NON_DISP;

	return mask;
}

void gen6_disable_rps_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	spin_lock_irq(&dev_priv->irq_lock);
	dev_priv->rps.interrupts_enabled = false;
	spin_unlock_irq(&dev_priv->irq_lock);

	cancel_work_sync(&dev_priv->rps.work);

	spin_lock_irq(&dev_priv->irq_lock);

	I915_WRITE(GEN6_PMINTRMSK, gen6_sanitize_rps_pm_mask(dev_priv, ~0));

	__gen6_disable_pm_irq(dev_priv, dev_priv->pm_rps_events);
	I915_WRITE(gen6_pm_ier(dev_priv), I915_READ(gen6_pm_ier(dev_priv)) &
				~dev_priv->pm_rps_events);
	I915_WRITE(gen6_pm_iir(dev_priv), dev_priv->pm_rps_events);
	I915_WRITE(gen6_pm_iir(dev_priv), dev_priv->pm_rps_events);

	dev_priv->rps.pm_iir = 0;

	spin_unlock_irq(&dev_priv->irq_lock);
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
	u32 reg = PIPESTAT(pipe);
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
	u32 reg = PIPESTAT(pipe);
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

	if (IS_VALLEYVIEW(dev_priv->dev))
		enable_mask = vlv_get_pipestat_enable_mask(dev_priv->dev,
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

	if (IS_VALLEYVIEW(dev_priv->dev))
		enable_mask = vlv_get_pipestat_enable_mask(dev_priv->dev,
							   status_mask);
	else
		enable_mask = status_mask << 16;
	__i915_disable_pipestat(dev_priv, pipe, enable_mask, status_mask);
}

/**
 * i915_enable_asle_pipestat - enable ASLE pipestat for OpRegion
 */
static void i915_enable_asle_pipestat(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv->opregion.asle || !IS_MOBILE(dev))
		return;

	spin_lock_irq(&dev_priv->irq_lock);

	i915_enable_pipestat(dev_priv, PIPE_B, PIPE_LEGACY_BLC_EVENT_STATUS);
	if (INTEL_INFO(dev)->gen >= 4)
		i915_enable_pipestat(dev_priv, PIPE_A,
				     PIPE_LEGACY_BLC_EVENT_STATUS);

	spin_unlock_irq(&dev_priv->irq_lock);
}

/**
 * i915_pipe_enabled - check if a pipe is enabled
 * @dev: DRM device
 * @pipe: pipe to check
 *
 * Reading certain registers when the pipe is disabled can hang the chip.
 * Use this routine to make sure the PLL is running and the pipe is active
 * before reading such registers if unsure.
 */
static int
i915_pipe_enabled(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* Locking is horribly broken here, but whatever. */
		struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		return intel_crtc->active;
	} else {
		return I915_READ(PIPECONF(pipe)) & PIPECONF_ENABLE;
	}
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

static u32 i8xx_get_vblank_counter(struct drm_device *dev, int pipe)
{
	/* Gen2 doesn't have a hardware frame counter */
	return 0;
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
static u32 i915_get_vblank_counter(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long high_frame;
	unsigned long low_frame;
	u32 high1, high2, low, pixel, vbl_start, hsync_start, htotal;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
				"pipe %c\n", pipe_name(pipe));
		return 0;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct intel_crtc *intel_crtc =
			to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);
		const struct drm_display_mode *mode =
			&intel_crtc->config->base.adjusted_mode;

		htotal = mode->crtc_htotal;
		hsync_start = mode->crtc_hsync_start;
		vbl_start = mode->crtc_vblank_start;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			vbl_start = DIV_ROUND_UP(vbl_start, 2);
	} else {
		enum transcoder cpu_transcoder = (enum transcoder) pipe;

		htotal = ((I915_READ(HTOTAL(cpu_transcoder)) >> 16) & 0x1fff) + 1;
		hsync_start = (I915_READ(HSYNC(cpu_transcoder))  & 0x1fff) + 1;
		vbl_start = (I915_READ(VBLANK(cpu_transcoder)) & 0x1fff) + 1;
		if ((I915_READ(PIPECONF(cpu_transcoder)) &
		     PIPECONF_INTERLACE_MASK) != PIPECONF_PROGRESSIVE)
			vbl_start = DIV_ROUND_UP(vbl_start, 2);
	}

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

static u32 gm45_get_vblank_counter(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int reg = PIPE_FRMCOUNT_GM45(pipe);

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
				 "pipe %c\n", pipe_name(pipe));
		return 0;
	}

	return I915_READ(reg);
}

/* raw reads, only for fast reads of display block, no need for forcewake etc. */
#define __raw_i915_read32(dev_priv__, reg__) readl((dev_priv__)->regs + (reg__))

static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct drm_display_mode *mode = &crtc->config->base.adjusted_mode;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	vtotal = mode->crtc_vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	if (IS_GEN2(dev))
		position = __raw_i915_read32(dev_priv, PIPEDSL(pipe)) & DSL_LINEMASK_GEN2;
	else
		position = __raw_i915_read32(dev_priv, PIPEDSL(pipe)) & DSL_LINEMASK_GEN3;

	/*
	 * See update_scanline_offset() for the details on the
	 * scanline_offset adjustment.
	 */
	return (position + crtc->scanline_offset) % vtotal;
}

static int i915_get_crtc_scanoutpos(struct drm_device *dev, int pipe,
				    unsigned int flags, int *vpos, int *hpos,
				    ktime_t *stime, ktime_t *etime)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	const struct drm_display_mode *mode = &intel_crtc->config->base.adjusted_mode;
	int position;
	int vbl_start, vbl_end, hsync_start, htotal, vtotal;
	bool in_vbl = true;
	int ret = 0;
	unsigned long irqflags;

	if (!intel_crtc->active) {
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

	if (IS_GEN2(dev) || IS_G4X(dev) || INTEL_INFO(dev)->gen >= 5) {
		/* No obvious pixelcount register. Only query vertical
		 * scanout position from Display scan line register.
		 */
		position = __intel_get_crtc_scanline(intel_crtc);
	} else {
		/* Have access to pixelcount since start of frame.
		 * We can split this into vertical and horizontal
		 * scanout position.
		 */
		position = (__raw_i915_read32(dev_priv, PIPEFRAMEPIXEL(pipe)) & PIPE_PIXEL_MASK) >> PIPE_PIXEL_SHIFT;

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

	if (IS_GEN2(dev) || IS_G4X(dev) || INTEL_INFO(dev)->gen >= 5) {
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
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	unsigned long irqflags;
	int position;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	position = __intel_get_crtc_scanline(crtc);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	return position;
}

static int i915_get_vblank_timestamp(struct drm_device *dev, int pipe,
			      int *max_error,
			      struct timeval *vblank_time,
			      unsigned flags)
{
	struct drm_crtc *crtc;

	if (pipe < 0 || pipe >= INTEL_INFO(dev)->num_pipes) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return -EINVAL;
	}

	/* Get drm_crtc to timestamp: */
	crtc = intel_get_crtc_for_pipe(dev, pipe);
	if (crtc == NULL) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return -EINVAL;
	}

	if (!crtc->enabled) {
		DRM_DEBUG_KMS("crtc %d is disabled\n", pipe);
		return -EBUSY;
	}

	/* Helper routine in DRM core does all the work: */
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error,
						     vblank_time, flags,
						     crtc,
						     &to_intel_crtc(crtc)->config->base.adjusted_mode);
}

static bool intel_hpd_irq_event(struct drm_device *dev,
				struct drm_connector *connector)
{
	enum drm_connector_status old_status;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));
	old_status = connector->status;

	connector->status = connector->funcs->detect(connector, false);
	if (old_status == connector->status)
		return false;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %s to %s\n",
		      connector->base.id,
		      connector->name,
		      drm_get_connector_status_name(old_status),
		      drm_get_connector_status_name(connector->status));

	return true;
}

static void i915_digport_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, dig_port_work);
	u32 long_port_mask, short_port_mask;
	struct intel_digital_port *intel_dig_port;
	int i;
	u32 old_bits = 0;

	spin_lock_irq(&dev_priv->irq_lock);
	long_port_mask = dev_priv->long_hpd_port_mask;
	dev_priv->long_hpd_port_mask = 0;
	short_port_mask = dev_priv->short_hpd_port_mask;
	dev_priv->short_hpd_port_mask = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	for (i = 0; i < I915_MAX_PORTS; i++) {
		bool valid = false;
		bool long_hpd = false;
		intel_dig_port = dev_priv->hpd_irq_port[i];
		if (!intel_dig_port || !intel_dig_port->hpd_pulse)
			continue;

		if (long_port_mask & (1 << i))  {
			valid = true;
			long_hpd = true;
		} else if (short_port_mask & (1 << i))
			valid = true;

		if (valid) {
			enum irqreturn ret;

			ret = intel_dig_port->hpd_pulse(intel_dig_port, long_hpd);
			if (ret == IRQ_NONE) {
				/* fall back to old school hpd */
				old_bits |= (1 << intel_dig_port->base.hpd_pin);
			}
		}
	}

	if (old_bits) {
		spin_lock_irq(&dev_priv->irq_lock);
		dev_priv->hpd_event_bits |= old_bits;
		spin_unlock_irq(&dev_priv->irq_lock);
		schedule_work(&dev_priv->hotplug_work);
	}
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
#define I915_REENABLE_HOTPLUG_DELAY (2*60*1000)

static void i915_hotplug_work_func(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, hotplug_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_connector *intel_connector;
	struct intel_encoder *intel_encoder;
	struct drm_connector *connector;
	bool hpd_disabled = false;
	bool changed = false;
	u32 hpd_event_bits;

	mutex_lock(&mode_config->mutex);
	DRM_DEBUG_KMS("running encoder hotplug functions\n");

	spin_lock_irq(&dev_priv->irq_lock);

	hpd_event_bits = dev_priv->hpd_event_bits;
	dev_priv->hpd_event_bits = 0;
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		intel_connector = to_intel_connector(connector);
		if (!intel_connector->encoder)
			continue;
		intel_encoder = intel_connector->encoder;
		if (intel_encoder->hpd_pin > HPD_NONE &&
		    dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_MARK_DISABLED &&
		    connector->polled == DRM_CONNECTOR_POLL_HPD) {
			DRM_INFO("HPD interrupt storm detected on connector %s: "
				 "switching from hotplug detection to polling\n",
				connector->name);
			dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark = HPD_DISABLED;
			connector->polled = DRM_CONNECTOR_POLL_CONNECT
				| DRM_CONNECTOR_POLL_DISCONNECT;
			hpd_disabled = true;
		}
		if (hpd_event_bits & (1 << intel_encoder->hpd_pin)) {
			DRM_DEBUG_KMS("Connector %s (pin %i) received hotplug event.\n",
				      connector->name, intel_encoder->hpd_pin);
		}
	}
	 /* if there were no outputs to poll, poll was disabled,
	  * therefore make sure it's enabled when disabling HPD on
	  * some connectors */
	if (hpd_disabled) {
		drm_kms_helper_poll_enable(dev);
		mod_delayed_work(system_wq, &dev_priv->hotplug_reenable_work,
				 msecs_to_jiffies(I915_REENABLE_HOTPLUG_DELAY));
	}

	spin_unlock_irq(&dev_priv->irq_lock);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		intel_connector = to_intel_connector(connector);
		if (!intel_connector->encoder)
			continue;
		intel_encoder = intel_connector->encoder;
		if (hpd_event_bits & (1 << intel_encoder->hpd_pin)) {
			if (intel_encoder->hot_plug)
				intel_encoder->hot_plug(intel_encoder);
			if (intel_hpd_irq_event(dev, connector))
				changed = true;
		}
	}
	mutex_unlock(&mode_config->mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);
}

static void ironlake_rps_change_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	if (ironlake_set_drps(dev, new_delay))
		dev_priv->ips.cur_delay = new_delay;

	spin_unlock(&mchdev_lock);

	return;
}

static void notify_ring(struct drm_device *dev,
			struct intel_engine_cs *ring)
{
	if (!intel_ring_initialized(ring))
		return;

	trace_i915_gem_request_notify(ring);

	wake_up_all(&ring->irq_queue);
}

static u32 vlv_c0_residency(struct drm_i915_private *dev_priv,
			    struct intel_rps_ei *rps_ei)
{
	u32 cz_ts, cz_freq_khz;
	u32 render_count, media_count;
	u32 elapsed_render, elapsed_media, elapsed_time;
	u32 residency = 0;

	cz_ts = vlv_punit_read(dev_priv, PUNIT_REG_CZ_TIMESTAMP);
	cz_freq_khz = DIV_ROUND_CLOSEST(dev_priv->mem_freq * 1000, 4);

	render_count = I915_READ(VLV_RENDER_C0_COUNT_REG);
	media_count = I915_READ(VLV_MEDIA_C0_COUNT_REG);

	if (rps_ei->cz_clock == 0) {
		rps_ei->cz_clock = cz_ts;
		rps_ei->render_c0 = render_count;
		rps_ei->media_c0 = media_count;

		return dev_priv->rps.cur_freq;
	}

	elapsed_time = cz_ts - rps_ei->cz_clock;
	rps_ei->cz_clock = cz_ts;

	elapsed_render = render_count - rps_ei->render_c0;
	rps_ei->render_c0 = render_count;

	elapsed_media = media_count - rps_ei->media_c0;
	rps_ei->media_c0 = media_count;

	/* Convert all the counters into common unit of milli sec */
	elapsed_time /= VLV_CZ_CLOCK_TO_MILLI_SEC;
	elapsed_render /=  cz_freq_khz;
	elapsed_media /= cz_freq_khz;

	/*
	 * Calculate overall C0 residency percentage
	 * only if elapsed time is non zero
	 */
	if (elapsed_time) {
		residency =
			((max(elapsed_render, elapsed_media) * 100)
				/ elapsed_time);
	}

	return residency;
}

/**
 * vlv_calc_delay_from_C0_counters - Increase/Decrease freq based on GPU
 * busy-ness calculated from C0 counters of render & media power wells
 * @dev_priv: DRM device private
 *
 */
static int vlv_calc_delay_from_C0_counters(struct drm_i915_private *dev_priv)
{
	u32 residency_C0_up = 0, residency_C0_down = 0;
	int new_delay, adj;

	dev_priv->rps.ei_interrupt_count++;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));


	if (dev_priv->rps.up_ei.cz_clock == 0) {
		vlv_c0_residency(dev_priv, &dev_priv->rps.up_ei);
		vlv_c0_residency(dev_priv, &dev_priv->rps.down_ei);
		return dev_priv->rps.cur_freq;
	}


	/*
	 * To down throttle, C0 residency should be less than down threshold
	 * for continous EI intervals. So calculate down EI counters
	 * once in VLV_INT_COUNT_FOR_DOWN_EI
	 */
	if (dev_priv->rps.ei_interrupt_count == VLV_INT_COUNT_FOR_DOWN_EI) {

		dev_priv->rps.ei_interrupt_count = 0;

		residency_C0_down = vlv_c0_residency(dev_priv,
						     &dev_priv->rps.down_ei);
	} else {
		residency_C0_up = vlv_c0_residency(dev_priv,
						   &dev_priv->rps.up_ei);
	}

	new_delay = dev_priv->rps.cur_freq;

	adj = dev_priv->rps.last_adj;
	/* C0 residency is greater than UP threshold. Increase Frequency */
	if (residency_C0_up >= VLV_RP_UP_EI_THRESHOLD) {
		if (adj > 0)
			adj *= 2;
		else
			adj = 1;

		if (dev_priv->rps.cur_freq < dev_priv->rps.max_freq_softlimit)
			new_delay = dev_priv->rps.cur_freq + adj;

		/*
		 * For better performance, jump directly
		 * to RPe if we're below it.
		 */
		if (new_delay < dev_priv->rps.efficient_freq)
			new_delay = dev_priv->rps.efficient_freq;

	} else if (!dev_priv->rps.ei_interrupt_count &&
			(residency_C0_down < VLV_RP_DOWN_EI_THRESHOLD)) {
		if (adj < 0)
			adj *= 2;
		else
			adj = -1;
		/*
		 * This means, C0 residency is less than down threshold over
		 * a period of VLV_INT_COUNT_FOR_DOWN_EI. So, reduce the freq
		 */
		if (dev_priv->rps.cur_freq > dev_priv->rps.min_freq_softlimit)
			new_delay = dev_priv->rps.cur_freq + adj;
	}

	return new_delay;
}

static void gen6_pm_rps_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private, rps.work);
	u32 pm_iir;
	int new_delay, adj;

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
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	WARN_ON(pm_iir & ~dev_priv->pm_rps_events);

	if ((pm_iir & dev_priv->pm_rps_events) == 0)
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	adj = dev_priv->rps.last_adj;
	if (pm_iir & GEN6_PM_RP_UP_THRESHOLD) {
		if (adj > 0)
			adj *= 2;
		else {
			/* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv->dev) ? 2 : 1;
		}
		new_delay = dev_priv->rps.cur_freq + adj;

		/*
		 * For better performance, jump directly
		 * to RPe if we're below it.
		 */
		if (new_delay < dev_priv->rps.efficient_freq)
			new_delay = dev_priv->rps.efficient_freq;
	} else if (pm_iir & GEN6_PM_RP_DOWN_TIMEOUT) {
		if (dev_priv->rps.cur_freq > dev_priv->rps.efficient_freq)
			new_delay = dev_priv->rps.efficient_freq;
		else
			new_delay = dev_priv->rps.min_freq_softlimit;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_UP_EI_EXPIRED) {
		new_delay = vlv_calc_delay_from_C0_counters(dev_priv);
	} else if (pm_iir & GEN6_PM_RP_DOWN_THRESHOLD) {
		if (adj < 0)
			adj *= 2;
		else {
			/* CHV needs even encode values */
			adj = IS_CHERRYVIEW(dev_priv->dev) ? -2 : -1;
		}
		new_delay = dev_priv->rps.cur_freq + adj;
	} else { /* unknown event */
		new_delay = dev_priv->rps.cur_freq;
	}

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	new_delay = clamp_t(int, new_delay,
			    dev_priv->rps.min_freq_softlimit,
			    dev_priv->rps.max_freq_softlimit);

	dev_priv->rps.last_adj = new_delay - dev_priv->rps.cur_freq;

	if (IS_VALLEYVIEW(dev_priv->dev))
		valleyview_set_rps(dev_priv->dev, new_delay);
	else
		gen6_set_rps(dev_priv->dev, new_delay);

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
	mutex_lock(&dev_priv->dev->struct_mutex);

	/* If we've screwed up tracking, just let the interrupt fire again */
	if (WARN_ON(!dev_priv->l3_parity.which_slice))
		goto out;

	misccpctl = I915_READ(GEN7_MISCCPCTL);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);
	POSTING_READ(GEN7_MISCCPCTL);

	while ((slice = ffs(dev_priv->l3_parity.which_slice)) != 0) {
		u32 reg;

		slice--;
		if (WARN_ON_ONCE(slice >= NUM_L3_SLICES(dev_priv->dev)))
			break;

		dev_priv->l3_parity.which_slice &= ~(1<<slice);

		reg = GEN7_L3CDERRST1 + (slice * 0x200);

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

		kobject_uevent_env(&dev_priv->dev->primary->kdev->kobj,
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
	gen5_enable_gt_irq(dev_priv, GT_PARITY_ERROR(dev_priv->dev));
	spin_unlock_irq(&dev_priv->irq_lock);

	mutex_unlock(&dev_priv->dev->struct_mutex);
}

static void ivybridge_parity_error_irq_handler(struct drm_device *dev, u32 iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!HAS_L3_DPF(dev))
		return;

	spin_lock(&dev_priv->irq_lock);
	gen5_disable_gt_irq(dev_priv, GT_PARITY_ERROR(dev));
	spin_unlock(&dev_priv->irq_lock);

	iir &= GT_PARITY_ERROR(dev);
	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1)
		dev_priv->l3_parity.which_slice |= 1 << 1;

	if (iir & GT_RENDER_L3_PARITY_ERROR_INTERRUPT)
		dev_priv->l3_parity.which_slice |= 1 << 0;

	queue_work(dev_priv->wq, &dev_priv->l3_parity.error_work);
}

static void ilk_gt_irq_handler(struct drm_device *dev,
			       struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{
	if (gt_iir &
	    (GT_RENDER_USER_INTERRUPT | GT_RENDER_PIPECTL_NOTIFY_INTERRUPT))
		notify_ring(dev, &dev_priv->ring[RCS]);
	if (gt_iir & ILK_BSD_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[VCS]);
}

static void snb_gt_irq_handler(struct drm_device *dev,
			       struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{

	if (gt_iir &
	    (GT_RENDER_USER_INTERRUPT | GT_RENDER_PIPECTL_NOTIFY_INTERRUPT))
		notify_ring(dev, &dev_priv->ring[RCS]);
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[VCS]);
	if (gt_iir & GT_BLT_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[BCS]);

	if (gt_iir & (GT_BLT_CS_ERROR_INTERRUPT |
		      GT_BSD_CS_ERROR_INTERRUPT |
		      GT_RENDER_CS_MASTER_ERROR_INTERRUPT))
		DRM_DEBUG("Command parser error, gt_iir 0x%08x\n", gt_iir);

	if (gt_iir & GT_PARITY_ERROR(dev))
		ivybridge_parity_error_irq_handler(dev, gt_iir);
}

static irqreturn_t gen8_gt_irq_handler(struct drm_device *dev,
				       struct drm_i915_private *dev_priv,
				       u32 master_ctl)
{
	struct intel_engine_cs *ring;
	u32 rcs, bcs, vcs;
	uint32_t tmp = 0;
	irqreturn_t ret = IRQ_NONE;

	if (master_ctl & (GEN8_GT_RCS_IRQ | GEN8_GT_BCS_IRQ)) {
		tmp = I915_READ(GEN8_GT_IIR(0));
		if (tmp) {
			I915_WRITE(GEN8_GT_IIR(0), tmp);
			ret = IRQ_HANDLED;

			rcs = tmp >> GEN8_RCS_IRQ_SHIFT;
			ring = &dev_priv->ring[RCS];
			if (rcs & GT_RENDER_USER_INTERRUPT)
				notify_ring(dev, ring);
			if (rcs & GT_CONTEXT_SWITCH_INTERRUPT)
				intel_lrc_irq_handler(ring);

			bcs = tmp >> GEN8_BCS_IRQ_SHIFT;
			ring = &dev_priv->ring[BCS];
			if (bcs & GT_RENDER_USER_INTERRUPT)
				notify_ring(dev, ring);
			if (bcs & GT_CONTEXT_SWITCH_INTERRUPT)
				intel_lrc_irq_handler(ring);
		} else
			DRM_ERROR("The master control interrupt lied (GT0)!\n");
	}

	if (master_ctl & (GEN8_GT_VCS1_IRQ | GEN8_GT_VCS2_IRQ)) {
		tmp = I915_READ(GEN8_GT_IIR(1));
		if (tmp) {
			I915_WRITE(GEN8_GT_IIR(1), tmp);
			ret = IRQ_HANDLED;

			vcs = tmp >> GEN8_VCS1_IRQ_SHIFT;
			ring = &dev_priv->ring[VCS];
			if (vcs & GT_RENDER_USER_INTERRUPT)
				notify_ring(dev, ring);
			if (vcs & GT_CONTEXT_SWITCH_INTERRUPT)
				intel_lrc_irq_handler(ring);

			vcs = tmp >> GEN8_VCS2_IRQ_SHIFT;
			ring = &dev_priv->ring[VCS2];
			if (vcs & GT_RENDER_USER_INTERRUPT)
				notify_ring(dev, ring);
			if (vcs & GT_CONTEXT_SWITCH_INTERRUPT)
				intel_lrc_irq_handler(ring);
		} else
			DRM_ERROR("The master control interrupt lied (GT1)!\n");
	}

	if (master_ctl & GEN8_GT_PM_IRQ) {
		tmp = I915_READ(GEN8_GT_IIR(2));
		if (tmp & dev_priv->pm_rps_events) {
			I915_WRITE(GEN8_GT_IIR(2),
				   tmp & dev_priv->pm_rps_events);
			ret = IRQ_HANDLED;
			gen6_rps_irq_handler(dev_priv, tmp);
		} else
			DRM_ERROR("The master control interrupt lied (PM)!\n");
	}

	if (master_ctl & GEN8_GT_VECS_IRQ) {
		tmp = I915_READ(GEN8_GT_IIR(3));
		if (tmp) {
			I915_WRITE(GEN8_GT_IIR(3), tmp);
			ret = IRQ_HANDLED;

			vcs = tmp >> GEN8_VECS_IRQ_SHIFT;
			ring = &dev_priv->ring[VECS];
			if (vcs & GT_RENDER_USER_INTERRUPT)
				notify_ring(dev, ring);
			if (vcs & GT_CONTEXT_SWITCH_INTERRUPT)
				intel_lrc_irq_handler(ring);
		} else
			DRM_ERROR("The master control interrupt lied (GT3)!\n");
	}

	return ret;
}

#define HPD_STORM_DETECT_PERIOD 1000
#define HPD_STORM_THRESHOLD 5

static int pch_port_to_hotplug_shift(enum port port)
{
	switch (port) {
	case PORT_A:
	case PORT_E:
	default:
		return -1;
	case PORT_B:
		return 0;
	case PORT_C:
		return 8;
	case PORT_D:
		return 16;
	}
}

static int i915_port_to_hotplug_shift(enum port port)
{
	switch (port) {
	case PORT_A:
	case PORT_E:
	default:
		return -1;
	case PORT_B:
		return 17;
	case PORT_C:
		return 19;
	case PORT_D:
		return 21;
	}
}

static inline enum port get_port_from_pin(enum hpd_pin pin)
{
	switch (pin) {
	case HPD_PORT_B:
		return PORT_B;
	case HPD_PORT_C:
		return PORT_C;
	case HPD_PORT_D:
		return PORT_D;
	default:
		return PORT_A; /* no hpd */
	}
}

static inline void intel_hpd_irq_handler(struct drm_device *dev,
					 u32 hotplug_trigger,
					 u32 dig_hotplug_reg,
					 const u32 hpd[HPD_NUM_PINS])
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	enum port port;
	bool storm_detected = false;
	bool queue_dig = false, queue_hp = false;
	u32 dig_shift;
	u32 dig_port_mask = 0;

	if (!hotplug_trigger)
		return;

	DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x, dig 0x%08x\n",
			 hotplug_trigger, dig_hotplug_reg);

	spin_lock(&dev_priv->irq_lock);
	for (i = 1; i < HPD_NUM_PINS; i++) {
		if (!(hpd[i] & hotplug_trigger))
			continue;

		port = get_port_from_pin(i);
		if (port && dev_priv->hpd_irq_port[port]) {
			bool long_hpd;

			if (HAS_PCH_SPLIT(dev)) {
				dig_shift = pch_port_to_hotplug_shift(port);
				long_hpd = (dig_hotplug_reg >> dig_shift) & PORTB_HOTPLUG_LONG_DETECT;
			} else {
				dig_shift = i915_port_to_hotplug_shift(port);
				long_hpd = (hotplug_trigger >> dig_shift) & PORTB_HOTPLUG_LONG_DETECT;
			}

			DRM_DEBUG_DRIVER("digital hpd port %c - %s\n",
					 port_name(port),
					 long_hpd ? "long" : "short");
			/* for long HPD pulses we want to have the digital queue happen,
			   but we still want HPD storm detection to function. */
			if (long_hpd) {
				dev_priv->long_hpd_port_mask |= (1 << port);
				dig_port_mask |= hpd[i];
			} else {
				/* for short HPD just trigger the digital queue */
				dev_priv->short_hpd_port_mask |= (1 << port);
				hotplug_trigger &= ~hpd[i];
			}
			queue_dig = true;
		}
	}

	for (i = 1; i < HPD_NUM_PINS; i++) {
		if (hpd[i] & hotplug_trigger &&
		    dev_priv->hpd_stats[i].hpd_mark == HPD_DISABLED) {
			/*
			 * On GMCH platforms the interrupt mask bits only
			 * prevent irq generation, not the setting of the
			 * hotplug bits itself. So only WARN about unexpected
			 * interrupts on saner platforms.
			 */
			WARN_ONCE(INTEL_INFO(dev)->gen >= 5 && !IS_VALLEYVIEW(dev),
				  "Received HPD interrupt (0x%08x) on pin %d (0x%08x) although disabled\n",
				  hotplug_trigger, i, hpd[i]);

			continue;
		}

		if (!(hpd[i] & hotplug_trigger) ||
		    dev_priv->hpd_stats[i].hpd_mark != HPD_ENABLED)
			continue;

		if (!(dig_port_mask & hpd[i])) {
			dev_priv->hpd_event_bits |= (1 << i);
			queue_hp = true;
		}

		if (!time_in_range(jiffies, dev_priv->hpd_stats[i].hpd_last_jiffies,
				   dev_priv->hpd_stats[i].hpd_last_jiffies
				   + msecs_to_jiffies(HPD_STORM_DETECT_PERIOD))) {
			dev_priv->hpd_stats[i].hpd_last_jiffies = jiffies;
			dev_priv->hpd_stats[i].hpd_cnt = 0;
			DRM_DEBUG_KMS("Received HPD interrupt on PIN %d - cnt: 0\n", i);
		} else if (dev_priv->hpd_stats[i].hpd_cnt > HPD_STORM_THRESHOLD) {
			dev_priv->hpd_stats[i].hpd_mark = HPD_MARK_DISABLED;
			dev_priv->hpd_event_bits &= ~(1 << i);
			DRM_DEBUG_KMS("HPD interrupt storm detected on PIN %d\n", i);
			storm_detected = true;
		} else {
			dev_priv->hpd_stats[i].hpd_cnt++;
			DRM_DEBUG_KMS("Received HPD interrupt on PIN %d - cnt: %d\n", i,
				      dev_priv->hpd_stats[i].hpd_cnt);
		}
	}

	if (storm_detected)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock(&dev_priv->irq_lock);

	/*
	 * Our hotplug handler can grab modeset locks (by calling down into the
	 * fb helpers). Hence it must not be run on our own dev-priv->wq work
	 * queue for otherwise the flush_work in the pageflip code will
	 * deadlock.
	 */
	if (queue_dig)
		queue_work(dev_priv->dp_wq, &dev_priv->dig_port_work);
	if (queue_hp)
		schedule_work(&dev_priv->hotplug_work);
}

static void gmbus_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	wake_up_all(&dev_priv->gmbus_wait_queue);
}

static void dp_aux_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	wake_up_all(&dev_priv->gmbus_wait_queue);
}

#if defined(CONFIG_DEBUG_FS)
static void display_pipe_crc_irq_handler(struct drm_device *dev, enum pipe pipe,
					 uint32_t crc0, uint32_t crc1,
					 uint32_t crc2, uint32_t crc3,
					 uint32_t crc4)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	entry->frame = dev->driver->get_vblank_counter(dev, pipe);
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
display_pipe_crc_irq_handler(struct drm_device *dev, enum pipe pipe,
			     uint32_t crc0, uint32_t crc1,
			     uint32_t crc2, uint32_t crc3,
			     uint32_t crc4) {}
#endif


static void hsw_pipe_crc_irq_handler(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	display_pipe_crc_irq_handler(dev, pipe,
				     I915_READ(PIPE_CRC_RES_1_IVB(pipe)),
				     0, 0, 0, 0);
}

static void ivb_pipe_crc_irq_handler(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	display_pipe_crc_irq_handler(dev, pipe,
				     I915_READ(PIPE_CRC_RES_1_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_2_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_3_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_4_IVB(pipe)),
				     I915_READ(PIPE_CRC_RES_5_IVB(pipe)));
}

static void i9xx_pipe_crc_irq_handler(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t res1, res2;

	if (INTEL_INFO(dev)->gen >= 3)
		res1 = I915_READ(PIPE_CRC_RES_RES1_I915(pipe));
	else
		res1 = 0;

	if (INTEL_INFO(dev)->gen >= 5 || IS_G4X(dev))
		res2 = I915_READ(PIPE_CRC_RES_RES2_G4X(pipe));
	else
		res2 = 0;

	display_pipe_crc_irq_handler(dev, pipe,
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
	/* TODO: RPS on GEN9+ is not supported yet. */
	if (WARN_ONCE(INTEL_INFO(dev_priv)->gen >= 9,
		      "GEN9+: unexpected RPS IRQ\n"))
		return;

	if (pm_iir & dev_priv->pm_rps_events) {
		spin_lock(&dev_priv->irq_lock);
		gen6_disable_pm_irq(dev_priv, pm_iir & dev_priv->pm_rps_events);
		if (dev_priv->rps.interrupts_enabled) {
			dev_priv->rps.pm_iir |= pm_iir & dev_priv->pm_rps_events;
			queue_work(dev_priv->wq, &dev_priv->rps.work);
		}
		spin_unlock(&dev_priv->irq_lock);
	}

	if (INTEL_INFO(dev_priv)->gen >= 8)
		return;

	if (HAS_VEBOX(dev_priv->dev)) {
		if (pm_iir & PM_VEBOX_USER_INTERRUPT)
			notify_ring(dev_priv->dev, &dev_priv->ring[VECS]);

		if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT)
			DRM_DEBUG("Command parser error, pm_iir 0x%08x\n", pm_iir);
	}
}

static bool intel_pipe_handle_vblank(struct drm_device *dev, enum pipe pipe)
{
	if (!drm_handle_vblank(dev, pipe))
		return false;

	return true;
}

static void valleyview_pipestat_irq_handler(struct drm_device *dev, u32 iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pipe_stats[I915_MAX_PIPES] = { };
	int pipe;

	spin_lock(&dev_priv->irq_lock);
	for_each_pipe(dev_priv, pipe) {
		int reg;
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

	for_each_pipe(dev_priv, pipe) {
		if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS &&
		    intel_pipe_handle_vblank(dev, pipe))
			intel_check_page_flip(dev, pipe);

		if (pipe_stats[pipe] & PLANE_FLIP_DONE_INT_STATUS_VLV) {
			intel_prepare_page_flip(dev, pipe);
			intel_finish_page_flip(dev, pipe);
		}

		if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
			i9xx_pipe_crc_irq_handler(dev, pipe);

		if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
	}

	if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
		gmbus_irq_handler(dev);
}

static void i9xx_hpd_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);

	if (hotplug_status) {
		I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
		/*
		 * Make sure hotplug status is cleared before we clear IIR, or else we
		 * may miss hotplug events.
		 */
		POSTING_READ(PORT_HOTPLUG_STAT);

		if (IS_G4X(dev)) {
			u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_G4X;

			intel_hpd_irq_handler(dev, hotplug_trigger, 0, hpd_status_g4x);
		} else {
			u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

			intel_hpd_irq_handler(dev, hotplug_trigger, 0, hpd_status_i915);
		}

		if ((IS_G4X(dev) || IS_VALLEYVIEW(dev)) &&
		    hotplug_status & DP_AUX_CHANNEL_MASK_INT_STATUS_G4X)
			dp_aux_irq_handler(dev);
	}
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 iir, gt_iir, pm_iir;
	irqreturn_t ret = IRQ_NONE;

	while (true) {
		/* Find, clear, then process each source of interrupt */

		gt_iir = I915_READ(GTIIR);
		if (gt_iir)
			I915_WRITE(GTIIR, gt_iir);

		pm_iir = I915_READ(GEN6_PMIIR);
		if (pm_iir)
			I915_WRITE(GEN6_PMIIR, pm_iir);

		iir = I915_READ(VLV_IIR);
		if (iir) {
			/* Consume port before clearing IIR or we'll miss events */
			if (iir & I915_DISPLAY_PORT_INTERRUPT)
				i9xx_hpd_irq_handler(dev);
			I915_WRITE(VLV_IIR, iir);
		}

		if (gt_iir == 0 && pm_iir == 0 && iir == 0)
			goto out;

		ret = IRQ_HANDLED;

		if (gt_iir)
			snb_gt_irq_handler(dev, dev_priv, gt_iir);
		if (pm_iir)
			gen6_rps_irq_handler(dev_priv, pm_iir);
		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		valleyview_pipestat_irq_handler(dev, iir);
	}

out:
	return ret;
}

static irqreturn_t cherryview_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 master_ctl, iir;
	irqreturn_t ret = IRQ_NONE;

	for (;;) {
		master_ctl = I915_READ(GEN8_MASTER_IRQ) & ~GEN8_MASTER_IRQ_CONTROL;
		iir = I915_READ(VLV_IIR);

		if (master_ctl == 0 && iir == 0)
			break;

		ret = IRQ_HANDLED;

		I915_WRITE(GEN8_MASTER_IRQ, 0);

		/* Find, clear, then process each source of interrupt */

		if (iir) {
			/* Consume port before clearing IIR or we'll miss events */
			if (iir & I915_DISPLAY_PORT_INTERRUPT)
				i9xx_hpd_irq_handler(dev);
			I915_WRITE(VLV_IIR, iir);
		}

		gen8_gt_irq_handler(dev, dev_priv, master_ctl);

		/* Call regardless, as some status bits might not be
		 * signalled in iir */
		valleyview_pipestat_irq_handler(dev, iir);

		I915_WRITE(GEN8_MASTER_IRQ, DE_MASTER_IRQ_CONTROL);
		POSTING_READ(GEN8_MASTER_IRQ);
	}

	return ret;
}

static void ibx_irq_handler(struct drm_device *dev, u32 pch_iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;
	u32 dig_hotplug_reg;

	dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
	I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);

	intel_hpd_irq_handler(dev, hotplug_trigger, dig_hotplug_reg, hpd_ibx);

	if (pch_iir & SDE_AUDIO_POWER_MASK) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK) >>
			       SDE_AUDIO_POWER_SHIFT);
		DRM_DEBUG_DRIVER("PCH audio power change on port %d\n",
				 port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK)
		dp_aux_irq_handler(dev);

	if (pch_iir & SDE_GMBUS)
		gmbus_irq_handler(dev);

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

static void ivb_err_int_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 err_int = I915_READ(GEN7_ERR_INT);
	enum pipe pipe;

	if (err_int & ERR_INT_POISON)
		DRM_ERROR("Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (err_int & ERR_INT_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (err_int & ERR_INT_PIPE_CRC_DONE(pipe)) {
			if (IS_IVYBRIDGE(dev))
				ivb_pipe_crc_irq_handler(dev, pipe);
			else
				hsw_pipe_crc_irq_handler(dev, pipe);
		}
	}

	I915_WRITE(GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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

static void cpt_irq_handler(struct drm_device *dev, u32 pch_iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;
	u32 dig_hotplug_reg;

	dig_hotplug_reg = I915_READ(PCH_PORT_HOTPLUG);
	I915_WRITE(PCH_PORT_HOTPLUG, dig_hotplug_reg);

	intel_hpd_irq_handler(dev, hotplug_trigger, dig_hotplug_reg, hpd_cpt);

	if (pch_iir & SDE_AUDIO_POWER_MASK_CPT) {
		int port = ffs((pch_iir & SDE_AUDIO_POWER_MASK_CPT) >>
			       SDE_AUDIO_POWER_SHIFT_CPT);
		DRM_DEBUG_DRIVER("PCH audio power change on port %c\n",
				 port_name(port));
	}

	if (pch_iir & SDE_AUX_MASK_CPT)
		dp_aux_irq_handler(dev);

	if (pch_iir & SDE_GMBUS_CPT)
		gmbus_irq_handler(dev);

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
		cpt_serr_int_handler(dev);
}

static void ilk_display_irq_handler(struct drm_device *dev, u32 de_iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;

	if (de_iir & DE_AUX_CHANNEL_A)
		dp_aux_irq_handler(dev);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(dev);

	if (de_iir & DE_POISON)
		DRM_ERROR("Poison interrupt\n");

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & DE_PIPE_VBLANK(pipe) &&
		    intel_pipe_handle_vblank(dev, pipe))
			intel_check_page_flip(dev, pipe);

		if (de_iir & DE_PIPE_FIFO_UNDERRUN(pipe))
			intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);

		if (de_iir & DE_PIPE_CRC_DONE(pipe))
			i9xx_pipe_crc_irq_handler(dev, pipe);

		/* plane/pipes map 1:1 on ilk+ */
		if (de_iir & DE_PLANE_FLIP_DONE(pipe)) {
			intel_prepare_page_flip(dev, pipe);
			intel_finish_page_flip_plane(dev, pipe);
		}
	}

	/* check event from PCH */
	if (de_iir & DE_PCH_EVENT) {
		u32 pch_iir = I915_READ(SDEIIR);

		if (HAS_PCH_CPT(dev))
			cpt_irq_handler(dev, pch_iir);
		else
			ibx_irq_handler(dev, pch_iir);

		/* should clear PCH hotplug event before clear CPU irq */
		I915_WRITE(SDEIIR, pch_iir);
	}

	if (IS_GEN5(dev) && de_iir & DE_PCU_EVENT)
		ironlake_rps_change_irq_handler(dev);
}

static void ivb_display_irq_handler(struct drm_device *dev, u32 de_iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(dev);

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		dp_aux_irq_handler(dev);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev);

	for_each_pipe(dev_priv, pipe) {
		if (de_iir & (DE_PIPE_VBLANK_IVB(pipe)) &&
		    intel_pipe_handle_vblank(dev, pipe))
			intel_check_page_flip(dev, pipe);

		/* plane/pipes map 1:1 on ilk+ */
		if (de_iir & DE_PLANE_FLIP_DONE_IVB(pipe)) {
			intel_prepare_page_flip(dev, pipe);
			intel_finish_page_flip_plane(dev, pipe);
		}
	}

	/* check event from PCH */
	if (!HAS_PCH_NOP(dev) && (de_iir & DE_PCH_EVENT_IVB)) {
		u32 pch_iir = I915_READ(SDEIIR);

		cpt_irq_handler(dev, pch_iir);

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	/* We get interrupts on unclaimed registers, so check for this before we
	 * do any I915_{READ,WRITE}. */
	intel_uncore_check_errors(dev);

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);
	POSTING_READ(DEIER);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	if (!HAS_PCH_NOP(dev)) {
		sde_ier = I915_READ(SDEIER);
		I915_WRITE(SDEIER, 0);
		POSTING_READ(SDEIER);
	}

	/* Find, clear, then process each source of interrupt */

	gt_iir = I915_READ(GTIIR);
	if (gt_iir) {
		I915_WRITE(GTIIR, gt_iir);
		ret = IRQ_HANDLED;
		if (INTEL_INFO(dev)->gen >= 6)
			snb_gt_irq_handler(dev, dev_priv, gt_iir);
		else
			ilk_gt_irq_handler(dev, dev_priv, gt_iir);
	}

	de_iir = I915_READ(DEIIR);
	if (de_iir) {
		I915_WRITE(DEIIR, de_iir);
		ret = IRQ_HANDLED;
		if (INTEL_INFO(dev)->gen >= 7)
			ivb_display_irq_handler(dev, de_iir);
		else
			ilk_display_irq_handler(dev, de_iir);
	}

	if (INTEL_INFO(dev)->gen >= 6) {
		u32 pm_iir = I915_READ(GEN6_PMIIR);
		if (pm_iir) {
			I915_WRITE(GEN6_PMIIR, pm_iir);
			ret = IRQ_HANDLED;
			gen6_rps_irq_handler(dev_priv, pm_iir);
		}
	}

	I915_WRITE(DEIER, de_ier);
	POSTING_READ(DEIER);
	if (!HAS_PCH_NOP(dev)) {
		I915_WRITE(SDEIER, sde_ier);
		POSTING_READ(SDEIER);
	}

	return ret;
}

static irqreturn_t gen8_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 master_ctl;
	irqreturn_t ret = IRQ_NONE;
	uint32_t tmp = 0;
	enum pipe pipe;
	u32 aux_mask = GEN8_AUX_CHANNEL_A;

	if (IS_GEN9(dev))
		aux_mask |=  GEN9_AUX_CHANNEL_B | GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;

	master_ctl = I915_READ(GEN8_MASTER_IRQ);
	master_ctl &= ~GEN8_MASTER_IRQ_CONTROL;
	if (!master_ctl)
		return IRQ_NONE;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	/* Find, clear, then process each source of interrupt */

	ret = gen8_gt_irq_handler(dev, dev_priv, master_ctl);

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		tmp = I915_READ(GEN8_DE_MISC_IIR);
		if (tmp) {
			I915_WRITE(GEN8_DE_MISC_IIR, tmp);
			ret = IRQ_HANDLED;
			if (tmp & GEN8_DE_MISC_GSE)
				intel_opregion_asle_intr(dev);
			else
				DRM_ERROR("Unexpected DE Misc interrupt\n");
		}
		else
			DRM_ERROR("The master control interrupt lied (DE MISC)!\n");
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		tmp = I915_READ(GEN8_DE_PORT_IIR);
		if (tmp) {
			I915_WRITE(GEN8_DE_PORT_IIR, tmp);
			ret = IRQ_HANDLED;

			if (tmp & aux_mask)
				dp_aux_irq_handler(dev);
			else
				DRM_ERROR("Unexpected DE Port interrupt\n");
		}
		else
			DRM_ERROR("The master control interrupt lied (DE PORT)!\n");
	}

	for_each_pipe(dev_priv, pipe) {
		uint32_t pipe_iir, flip_done = 0, fault_errors = 0;

		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		pipe_iir = I915_READ(GEN8_DE_PIPE_IIR(pipe));
		if (pipe_iir) {
			ret = IRQ_HANDLED;
			I915_WRITE(GEN8_DE_PIPE_IIR(pipe), pipe_iir);

			if (pipe_iir & GEN8_PIPE_VBLANK &&
			    intel_pipe_handle_vblank(dev, pipe))
				intel_check_page_flip(dev, pipe);

			if (IS_GEN9(dev))
				flip_done = pipe_iir & GEN9_PIPE_PLANE1_FLIP_DONE;
			else
				flip_done = pipe_iir & GEN8_PIPE_PRIMARY_FLIP_DONE;

			if (flip_done) {
				intel_prepare_page_flip(dev, pipe);
				intel_finish_page_flip_plane(dev, pipe);
			}

			if (pipe_iir & GEN8_PIPE_CDCLK_CRC_DONE)
				hsw_pipe_crc_irq_handler(dev, pipe);

			if (pipe_iir & GEN8_PIPE_FIFO_UNDERRUN)
				intel_cpu_fifo_underrun_irq_handler(dev_priv,
								    pipe);


			if (IS_GEN9(dev))
				fault_errors = pipe_iir & GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
			else
				fault_errors = pipe_iir & GEN8_DE_PIPE_IRQ_FAULT_ERRORS;

			if (fault_errors)
				DRM_ERROR("Fault errors on pipe %c\n: 0x%08x",
					  pipe_name(pipe),
					  pipe_iir & GEN8_DE_PIPE_IRQ_FAULT_ERRORS);
		} else
			DRM_ERROR("The master control interrupt lied (DE PIPE)!\n");
	}

	if (!HAS_PCH_NOP(dev) && master_ctl & GEN8_DE_PCH_IRQ) {
		/*
		 * FIXME(BDW): Assume for now that the new interrupt handling
		 * scheme also closed the SDE interrupt handling race we've seen
		 * on older pch-split platforms. But this needs testing.
		 */
		u32 pch_iir = I915_READ(SDEIIR);
		if (pch_iir) {
			I915_WRITE(SDEIIR, pch_iir);
			ret = IRQ_HANDLED;
			cpt_irq_handler(dev, pch_iir);
		} else
			DRM_ERROR("The master control interrupt lied (SDE)!\n");

	}

	I915_WRITE(GEN8_MASTER_IRQ, GEN8_MASTER_IRQ_CONTROL);
	POSTING_READ(GEN8_MASTER_IRQ);

	return ret;
}

static void i915_error_wake_up(struct drm_i915_private *dev_priv,
			       bool reset_completed)
{
	struct intel_engine_cs *ring;
	int i;

	/*
	 * Notify all waiters for GPU completion events that reset state has
	 * been changed, and that they need to restart their wait after
	 * checking for potential errors (and bail out to drop locks if there is
	 * a gpu reset pending so that i915_error_work_func can acquire them).
	 */

	/* Wake up __wait_seqno, potentially holding dev->struct_mutex. */
	for_each_ring(ring, dev_priv, i)
		wake_up_all(&ring->irq_queue);

	/* Wake up intel_crtc_wait_for_pending_flips, holding crtc->mutex. */
	wake_up_all(&dev_priv->pending_flip_queue);

	/*
	 * Signal tasks blocked in i915_gem_wait_for_error that the pending
	 * reset state is cleared.
	 */
	if (reset_completed)
		wake_up_all(&dev_priv->gpu_error.reset_queue);
}

/**
 * i915_reset_and_wakeup - do process context error handling work
 *
 * Fire an error uevent so userspace can see that a hang or error
 * was detected.
 */
static void i915_reset_and_wakeup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_gpu_error *error = &dev_priv->gpu_error;
	char *error_event[] = { I915_ERROR_UEVENT "=1", NULL };
	char *reset_event[] = { I915_RESET_UEVENT "=1", NULL };
	char *reset_done_event[] = { I915_ERROR_UEVENT "=0", NULL };
	int ret;

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, error_event);

	/*
	 * Note that there's only one work item which does gpu resets, so we
	 * need not worry about concurrent gpu resets potentially incrementing
	 * error->reset_counter twice. We only need to take care of another
	 * racing irq/hangcheck declaring the gpu dead for a second time. A
	 * quick check for that is good enough: schedule_work ensures the
	 * correct ordering between hang detection and this work item, and since
	 * the reset in-progress bit is only ever set by code outside of this
	 * work we don't need to worry about any other races.
	 */
	if (i915_reset_in_progress(error) && !i915_terminally_wedged(error)) {
		DRM_DEBUG_DRIVER("resetting chip\n");
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE,
				   reset_event);

		/*
		 * In most cases it's guaranteed that we get here with an RPM
		 * reference held, for example because there is a pending GPU
		 * request that won't finish until the reset is done. This
		 * isn't the case at least when we get here by doing a
		 * simulated reset via debugs, so get an RPM reference.
		 */
		intel_runtime_pm_get(dev_priv);

		intel_prepare_reset(dev);

		/*
		 * All state reset _must_ be completed before we update the
		 * reset counter, for otherwise waiters might miss the reset
		 * pending state and not properly drop locks, resulting in
		 * deadlocks with the reset work.
		 */
		ret = i915_reset(dev);

		intel_finish_reset(dev);

		intel_runtime_pm_put(dev_priv);

		if (ret == 0) {
			/*
			 * After all the gem state is reset, increment the reset
			 * counter and wake up everyone waiting for the reset to
			 * complete.
			 *
			 * Since unlock operations are a one-sided barrier only,
			 * we need to insert a barrier here to order any seqno
			 * updates before
			 * the counter increment.
			 */
			smp_mb__before_atomic();
			atomic_inc(&dev_priv->gpu_error.reset_counter);

			kobject_uevent_env(&dev->primary->kdev->kobj,
					   KOBJ_CHANGE, reset_done_event);
		} else {
			atomic_set_mask(I915_WEDGED, &error->reset_counter);
		}

		/*
		 * Note: The wake_up also serves as a memory barrier so that
		 * waiters see the update value of the reset counter atomic_t.
		 */
		i915_error_wake_up(dev_priv, true);
	}
}

static void i915_report_and_clear_eir(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t instdone[I915_NUM_INSTDONE_REG];
	u32 eir = I915_READ(EIR);
	int pipe, i;

	if (!eir)
		return;

	pr_err("render error detected, EIR: 0x%08x\n", eir);

	i915_get_extra_instdone(dev, instdone);

	if (IS_G4X(dev)) {
		if (eir & (GM45_ERROR_MEM_PRIV | GM45_ERROR_CP_PRIV)) {
			u32 ipeir = I915_READ(IPEIR_I965);

			pr_err("  IPEIR: 0x%08x\n", I915_READ(IPEIR_I965));
			pr_err("  IPEHR: 0x%08x\n", I915_READ(IPEHR_I965));
			for (i = 0; i < ARRAY_SIZE(instdone); i++)
				pr_err("  INSTDONE_%d: 0x%08x\n", i, instdone[i]);
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

	if (!IS_GEN2(dev)) {
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
		for (i = 0; i < ARRAY_SIZE(instdone); i++)
			pr_err("  INSTDONE_%d: 0x%08x\n", i, instdone[i]);
		if (INTEL_INFO(dev)->gen < 4) {
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
 * @dev: drm device
 *
 * Do some basic checking of regsiter state at error time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 */
void i915_handle_error(struct drm_device *dev, bool wedged,
		       const char *fmt, ...)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	va_list args;
	char error_msg[80];

	va_start(args, fmt);
	vscnprintf(error_msg, sizeof(error_msg), fmt, args);
	va_end(args);

	i915_capture_error_state(dev, wedged, error_msg);
	i915_report_and_clear_eir(dev);

	if (wedged) {
		atomic_set_mask(I915_RESET_IN_PROGRESS_FLAG,
				&dev_priv->gpu_error.reset_counter);

		/*
		 * Wakeup waiting processes so that the reset function
		 * i915_reset_and_wakeup doesn't deadlock trying to grab
		 * various locks. By bumping the reset counter first, the woken
		 * processes will see a reset in progress and back off,
		 * releasing their locks and then wait for the reset completion.
		 * We must do this for _all_ gpu waiters that might hold locks
		 * that the reset work needs to acquire.
		 *
		 * Note: The wake_up serves as the required memory barrier to
		 * ensure that the waiters see the updated value of the reset
		 * counter atomic_t.
		 */
		i915_error_wake_up(dev_priv, false);
	}

	i915_reset_and_wakeup(dev);
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static int i915_enable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

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

static int ironlake_enable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK(pipe);

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_enable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int valleyview_enable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, pipe,
			     PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int gen8_enable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	dev_priv->de_irq_mask[pipe] &= ~GEN8_PIPE_VBLANK;
	I915_WRITE(GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
	POSTING_READ(GEN8_DE_PIPE_IMR(pipe));
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static void i915_disable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_VBLANK_INTERRUPT_STATUS |
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ironlake_disable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void valleyview_disable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_START_VBLANK_INTERRUPT_STATUS);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void gen8_disable_vblank(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	dev_priv->de_irq_mask[pipe] |= GEN8_PIPE_VBLANK;
	I915_WRITE(GEN8_DE_PIPE_IMR(pipe), dev_priv->de_irq_mask[pipe]);
	POSTING_READ(GEN8_DE_PIPE_IMR(pipe));
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static struct drm_i915_gem_request *
ring_last_request(struct intel_engine_cs *ring)
{
	return list_entry(ring->request_list.prev,
			  struct drm_i915_gem_request, list);
}

static bool
ring_idle(struct intel_engine_cs *ring)
{
	return (list_empty(&ring->request_list) ||
		i915_gem_request_completed(ring_last_request(ring), false));
}

static bool
ipehr_is_semaphore_wait(struct drm_device *dev, u32 ipehr)
{
	if (INTEL_INFO(dev)->gen >= 8) {
		return (ipehr >> 23) == 0x1c;
	} else {
		ipehr &= ~MI_SEMAPHORE_SYNC_MASK;
		return ipehr == (MI_SEMAPHORE_MBOX | MI_SEMAPHORE_COMPARE |
				 MI_SEMAPHORE_REGISTER);
	}
}

static struct intel_engine_cs *
semaphore_wait_to_signaller_ring(struct intel_engine_cs *ring, u32 ipehr, u64 offset)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct intel_engine_cs *signaller;
	int i;

	if (INTEL_INFO(dev_priv->dev)->gen >= 8) {
		for_each_ring(signaller, dev_priv, i) {
			if (ring == signaller)
				continue;

			if (offset == signaller->semaphore.signal_ggtt[ring->id])
				return signaller;
		}
	} else {
		u32 sync_bits = ipehr & MI_SEMAPHORE_SYNC_MASK;

		for_each_ring(signaller, dev_priv, i) {
			if(ring == signaller)
				continue;

			if (sync_bits == signaller->semaphore.mbox.wait[ring->id])
				return signaller;
		}
	}

	DRM_ERROR("No signaller ring found for ring %i, ipehr 0x%08x, offset 0x%016llx\n",
		  ring->id, ipehr, offset);

	return NULL;
}

static struct intel_engine_cs *
semaphore_waits_for(struct intel_engine_cs *ring, u32 *seqno)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u32 cmd, ipehr, head;
	u64 offset = 0;
	int i, backwards;

	ipehr = I915_READ(RING_IPEHR(ring->mmio_base));
	if (!ipehr_is_semaphore_wait(ring->dev, ipehr))
		return NULL;

	/*
	 * HEAD is likely pointing to the dword after the actual command,
	 * so scan backwards until we find the MBOX. But limit it to just 3
	 * or 4 dwords depending on the semaphore wait command size.
	 * Note that we don't care about ACTHD here since that might
	 * point at at batch, and semaphores are always emitted into the
	 * ringbuffer itself.
	 */
	head = I915_READ_HEAD(ring) & HEAD_ADDR;
	backwards = (INTEL_INFO(ring->dev)->gen >= 8) ? 5 : 4;

	for (i = backwards; i; --i) {
		/*
		 * Be paranoid and presume the hw has gone off into the wild -
		 * our ring is smaller than what the hardware (and hence
		 * HEAD_ADDR) allows. Also handles wrap-around.
		 */
		head &= ring->buffer->size - 1;

		/* This here seems to blow up */
		cmd = ioread32(ring->buffer->virtual_start + head);
		if (cmd == ipehr)
			break;

		head -= 4;
	}

	if (!i)
		return NULL;

	*seqno = ioread32(ring->buffer->virtual_start + head + 4) + 1;
	if (INTEL_INFO(ring->dev)->gen >= 8) {
		offset = ioread32(ring->buffer->virtual_start + head + 12);
		offset <<= 32;
		offset = ioread32(ring->buffer->virtual_start + head + 8);
	}
	return semaphore_wait_to_signaller_ring(ring, ipehr, offset);
}

static int semaphore_passed(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct intel_engine_cs *signaller;
	u32 seqno;

	ring->hangcheck.deadlock++;

	signaller = semaphore_waits_for(ring, &seqno);
	if (signaller == NULL)
		return -1;

	/* Prevent pathological recursion due to driver bugs */
	if (signaller->hangcheck.deadlock >= I915_NUM_RINGS)
		return -1;

	if (i915_seqno_passed(signaller->get_seqno(signaller, false), seqno))
		return 1;

	/* cursory check for an unkickable deadlock */
	if (I915_READ_CTL(signaller) & RING_WAIT_SEMAPHORE &&
	    semaphore_passed(signaller) < 0)
		return -1;

	return 0;
}

static void semaphore_clear_deadlocks(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		ring->hangcheck.deadlock = 0;
}

static enum intel_ring_hangcheck_action
ring_stuck(struct intel_engine_cs *ring, u64 acthd)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp;

	if (acthd != ring->hangcheck.acthd) {
		if (acthd > ring->hangcheck.max_acthd) {
			ring->hangcheck.max_acthd = acthd;
			return HANGCHECK_ACTIVE;
		}

		return HANGCHECK_ACTIVE_LOOP;
	}

	if (IS_GEN2(dev))
		return HANGCHECK_HUNG;

	/* Is the chip hanging on a WAIT_FOR_EVENT?
	 * If so we can simply poke the RB_WAIT bit
	 * and break the hang. This should work on
	 * all but the second generation chipsets.
	 */
	tmp = I915_READ_CTL(ring);
	if (tmp & RING_WAIT) {
		i915_handle_error(dev, false,
				  "Kicking stuck wait on %s",
				  ring->name);
		I915_WRITE_CTL(ring, tmp);
		return HANGCHECK_KICK;
	}

	if (INTEL_INFO(dev)->gen >= 6 && tmp & RING_WAIT_SEMAPHORE) {
		switch (semaphore_passed(ring)) {
		default:
			return HANGCHECK_HUNG;
		case 1:
			i915_handle_error(dev, false,
					  "Kicking stuck semaphore on %s",
					  ring->name);
			I915_WRITE_CTL(ring, tmp);
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
	struct drm_device *dev = dev_priv->dev;
	struct intel_engine_cs *ring;
	int i;
	int busy_count = 0, rings_hung = 0;
	bool stuck[I915_NUM_RINGS] = { 0 };
#define BUSY 1
#define KICK 5
#define HUNG 20

	if (!i915.enable_hangcheck)
		return;

	for_each_ring(ring, dev_priv, i) {
		u64 acthd;
		u32 seqno;
		bool busy = true;

		semaphore_clear_deadlocks(dev_priv);

		seqno = ring->get_seqno(ring, false);
		acthd = intel_ring_get_active_head(ring);

		if (ring->hangcheck.seqno == seqno) {
			if (ring_idle(ring)) {
				ring->hangcheck.action = HANGCHECK_IDLE;

				if (waitqueue_active(&ring->irq_queue)) {
					/* Issue a wake-up to catch stuck h/w. */
					if (!test_and_set_bit(ring->id, &dev_priv->gpu_error.missed_irq_rings)) {
						if (!(dev_priv->gpu_error.test_irq_rings & intel_ring_flag(ring)))
							DRM_ERROR("Hangcheck timer elapsed... %s idle\n",
								  ring->name);
						else
							DRM_INFO("Fake missed irq on %s\n",
								 ring->name);
						wake_up_all(&ring->irq_queue);
					}
					/* Safeguard against driver failure */
					ring->hangcheck.score += BUSY;
				} else
					busy = false;
			} else {
				/* We always increment the hangcheck score
				 * if the ring is busy and still processing
				 * the same request, so that no single request
				 * can run indefinitely (such as a chain of
				 * batches). The only time we do not increment
				 * the hangcheck score on this ring, if this
				 * ring is in a legitimate wait for another
				 * ring. In that case the waiting ring is a
				 * victim and we want to be sure we catch the
				 * right culprit. Then every time we do kick
				 * the ring, add a small increment to the
				 * score so that we can catch a batch that is
				 * being repeatedly kicked and so responsible
				 * for stalling the machine.
				 */
				ring->hangcheck.action = ring_stuck(ring,
								    acthd);

				switch (ring->hangcheck.action) {
				case HANGCHECK_IDLE:
				case HANGCHECK_WAIT:
				case HANGCHECK_ACTIVE:
					break;
				case HANGCHECK_ACTIVE_LOOP:
					ring->hangcheck.score += BUSY;
					break;
				case HANGCHECK_KICK:
					ring->hangcheck.score += KICK;
					break;
				case HANGCHECK_HUNG:
					ring->hangcheck.score += HUNG;
					stuck[i] = true;
					break;
				}
			}
		} else {
			ring->hangcheck.action = HANGCHECK_ACTIVE;

			/* Gradually reduce the count so that we catch DoS
			 * attempts across multiple batches.
			 */
			if (ring->hangcheck.score > 0)
				ring->hangcheck.score--;

			ring->hangcheck.acthd = ring->hangcheck.max_acthd = 0;
		}

		ring->hangcheck.seqno = seqno;
		ring->hangcheck.acthd = acthd;
		busy_count += busy;
	}

	for_each_ring(ring, dev_priv, i) {
		if (ring->hangcheck.score >= HANGCHECK_SCORE_RING_HUNG) {
			DRM_INFO("%s on %s\n",
				 stuck[i] ? "stuck" : "no progress",
				 ring->name);
			rings_hung++;
		}
	}

	if (rings_hung)
		return i915_handle_error(dev, true, "Ring hung");

	if (busy_count)
		/* Reset timer case chip hangs without another request
		 * being added */
		i915_queue_hangcheck(dev);
}

void i915_queue_hangcheck(struct drm_device *dev)
{
	struct i915_gpu_error *e = &to_i915(dev)->gpu_error;

	if (!i915.enable_hangcheck)
		return;

	/* Don't continually defer the hangcheck so that it is always run at
	 * least once after work has been scheduled on any ring. Otherwise,
	 * we will ignore a hung ring if a second ring is kept busy.
	 */

	queue_delayed_work(e->hangcheck_wq, &e->hangcheck_work,
			   round_jiffies_up_relative(DRM_I915_HANGCHECK_JIFFIES));
}

static void ibx_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

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
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_PCH_NOP(dev))
		return;

	WARN_ON(I915_READ(SDEIER) != 0);
	I915_WRITE(SDEIER, 0xffffffff);
	POSTING_READ(SDEIER);
}

static void gen5_gt_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	GEN5_IRQ_RESET(GT);
	if (INTEL_INFO(dev)->gen >= 6)
		GEN5_IRQ_RESET(GEN6_PM);
}

/* drm_dma.h hooks
*/
static void ironlake_irq_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(HWSTAM, 0xffffffff);

	GEN5_IRQ_RESET(DE);
	if (IS_GEN7(dev))
		I915_WRITE(GEN7_ERR_INT, 0xffffffff);

	gen5_gt_irq_reset(dev);

	ibx_irq_reset(dev);
}

static void vlv_display_irq_reset(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0xffff);

	GEN5_IRQ_RESET(VLV_);
}

static void valleyview_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* VLV magic */
	I915_WRITE(VLV_IMR, 0);
	I915_WRITE(RING_IMR(RENDER_RING_BASE), 0);
	I915_WRITE(RING_IMR(GEN6_BSD_RING_BASE), 0);
	I915_WRITE(RING_IMR(BLT_RING_BASE), 0);

	gen5_gt_irq_reset(dev);

	I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK);

	vlv_display_irq_reset(dev_priv);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	ibx_irq_reset(dev);
}

void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv)
{
	uint32_t extra_ier = GEN8_PIPE_VBLANK | GEN8_PIPE_FIFO_UNDERRUN;

	spin_lock_irq(&dev_priv->irq_lock);
	GEN8_IRQ_INIT_NDX(DE_PIPE, PIPE_B, dev_priv->de_irq_mask[PIPE_B],
			  ~dev_priv->de_irq_mask[PIPE_B] | extra_ier);
	GEN8_IRQ_INIT_NDX(DE_PIPE, PIPE_C, dev_priv->de_irq_mask[PIPE_C],
			  ~dev_priv->de_irq_mask[PIPE_C] | extra_ier);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static void cherryview_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	GEN5_IRQ_RESET(GEN8_PCU_);

	I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK_CHV);

	vlv_display_irq_reset(dev_priv);
}

static void ibx_hpd_irq_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder;
	u32 hotplug_irqs, hotplug, enabled_irqs = 0;

	if (HAS_PCH_IBX(dev)) {
		hotplug_irqs = SDE_HOTPLUG_MASK;
		for_each_intel_encoder(dev, intel_encoder)
			if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
				enabled_irqs |= hpd_ibx[intel_encoder->hpd_pin];
	} else {
		hotplug_irqs = SDE_HOTPLUG_MASK_CPT;
		for_each_intel_encoder(dev, intel_encoder)
			if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
				enabled_irqs |= hpd_cpt[intel_encoder->hpd_pin];
	}

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	/*
	 * Enable digital hotplug on the PCH, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec)
	 *
	 * This register is the same on all known PCH chips.
	 */
	hotplug = I915_READ(PCH_PORT_HOTPLUG);
	hotplug &= ~(PORTD_PULSE_DURATION_MASK|PORTC_PULSE_DURATION_MASK|PORTB_PULSE_DURATION_MASK);
	hotplug |= PORTD_HOTPLUG_ENABLE | PORTD_PULSE_DURATION_2ms;
	hotplug |= PORTC_HOTPLUG_ENABLE | PORTC_PULSE_DURATION_2ms;
	hotplug |= PORTB_HOTPLUG_ENABLE | PORTB_PULSE_DURATION_2ms;
	I915_WRITE(PCH_PORT_HOTPLUG, hotplug);
}

static void ibx_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 mask;

	if (HAS_PCH_NOP(dev))
		return;

	if (HAS_PCH_IBX(dev))
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_POISON;
	else
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT;

	GEN5_ASSERT_IIR_IS_ZERO(SDEIIR);
	I915_WRITE(SDEIMR, ~mask);
}

static void gen5_gt_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
		gt_irqs |= GT_RENDER_PIPECTL_NOTIFY_INTERRUPT |
			   ILK_BSD_USER_INTERRUPT;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 display_mask, extra_mask;

	if (INTEL_INFO(dev)->gen >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_PLANEC_FLIP_DONE_IVB |
				DE_PLANEB_FLIP_DONE_IVB |
				DE_PLANEA_FLIP_DONE_IVB | DE_AUX_CHANNEL_A_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB | DE_ERR_INT_IVB);
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE |
				DE_AUX_CHANNEL_A |
				DE_PIPEB_CRC_DONE | DE_PIPEA_CRC_DONE |
				DE_POISON);
		extra_mask = DE_PIPEA_VBLANK | DE_PIPEB_VBLANK | DE_PCU_EVENT |
				DE_PIPEB_FIFO_UNDERRUN | DE_PIPEA_FIFO_UNDERRUN;
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
		ironlake_enable_display_irq(dev_priv, DE_PCU_EVENT);
		spin_unlock_irq(&dev_priv->irq_lock);
	}

	return 0;
}

static void valleyview_display_irqs_install(struct drm_i915_private *dev_priv)
{
	u32 pipestat_mask;
	u32 iir_mask;
	enum pipe pipe;

	pipestat_mask = PIPESTAT_INT_STATUS_MASK |
			PIPE_FIFO_UNDERRUN_STATUS;

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), pipestat_mask);
	POSTING_READ(PIPESTAT(PIPE_A));

	pipestat_mask = PLANE_FLIP_DONE_INT_STATUS_VLV |
			PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_enable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(dev_priv, pipe)
		      i915_enable_pipestat(dev_priv, pipe, pipestat_mask);

	iir_mask = I915_DISPLAY_PORT_INTERRUPT |
		   I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		   I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
	if (IS_CHERRYVIEW(dev_priv))
		iir_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT;
	dev_priv->irq_mask &= ~iir_mask;

	I915_WRITE(VLV_IIR, iir_mask);
	I915_WRITE(VLV_IIR, iir_mask);
	I915_WRITE(VLV_IER, ~dev_priv->irq_mask);
	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	POSTING_READ(VLV_IMR);
}

static void valleyview_display_irqs_uninstall(struct drm_i915_private *dev_priv)
{
	u32 pipestat_mask;
	u32 iir_mask;
	enum pipe pipe;

	iir_mask = I915_DISPLAY_PORT_INTERRUPT |
		   I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		   I915_DISPLAY_PIPE_B_EVENT_INTERRUPT;
	if (IS_CHERRYVIEW(dev_priv))
		iir_mask |= I915_DISPLAY_PIPE_C_EVENT_INTERRUPT;

	dev_priv->irq_mask |= iir_mask;
	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	I915_WRITE(VLV_IER, ~dev_priv->irq_mask);
	I915_WRITE(VLV_IIR, iir_mask);
	I915_WRITE(VLV_IIR, iir_mask);
	POSTING_READ(VLV_IIR);

	pipestat_mask = PLANE_FLIP_DONE_INT_STATUS_VLV |
			PIPE_CRC_DONE_INTERRUPT_STATUS;

	i915_disable_pipestat(dev_priv, PIPE_A, PIPE_GMBUS_INTERRUPT_STATUS);
	for_each_pipe(dev_priv, pipe)
		i915_disable_pipestat(dev_priv, pipe, pipestat_mask);

	pipestat_mask = PIPESTAT_INT_STATUS_MASK |
			PIPE_FIFO_UNDERRUN_STATUS;

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), pipestat_mask);
	POSTING_READ(PIPESTAT(PIPE_A));
}

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = true;

	if (intel_irqs_enabled(dev_priv))
		valleyview_display_irqs_install(dev_priv);
}

void valleyview_disable_display_irqs(struct drm_i915_private *dev_priv)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (!dev_priv->display_irqs_enabled)
		return;

	dev_priv->display_irqs_enabled = false;

	if (intel_irqs_enabled(dev_priv))
		valleyview_display_irqs_uninstall(dev_priv);
}

static void vlv_display_irq_postinstall(struct drm_i915_private *dev_priv)
{
	dev_priv->irq_mask = ~0;

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	POSTING_READ(PORT_HOTPLUG_EN);

	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IER, ~dev_priv->irq_mask);
	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	POSTING_READ(VLV_IMR);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		valleyview_display_irqs_install(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static int valleyview_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	vlv_display_irq_postinstall(dev_priv);

	gen5_gt_irq_postinstall(dev);

	/* ack & enable invalid PTE error interrupts */
#if 0 /* FIXME: add support to irq handler for checking these bits */
	I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK);
	I915_WRITE(DPINVGTT, DPINVGTT_EN_MASK);
#endif

	I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

	return 0;
}

static void gen8_gt_irq_postinstall(struct drm_i915_private *dev_priv)
{
	/* These are interrupts we'll toggle with the ring mask register */
	uint32_t gt_interrupts[] = {
		GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
			GT_CONTEXT_SWITCH_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
			GT_RENDER_L3_PARITY_ERROR_INTERRUPT |
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
	int pipe;
	u32 aux_en = GEN8_AUX_CHANNEL_A;

	if (IS_GEN9(dev_priv)) {
		de_pipe_masked |= GEN9_PIPE_PLANE1_FLIP_DONE |
				  GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
		aux_en |= GEN9_AUX_CHANNEL_B | GEN9_AUX_CHANNEL_C |
			GEN9_AUX_CHANNEL_D;
	} else
		de_pipe_masked |= GEN8_PIPE_PRIMARY_FLIP_DONE |
				  GEN8_DE_PIPE_IRQ_FAULT_ERRORS;

	de_pipe_enables = de_pipe_masked | GEN8_PIPE_VBLANK |
					   GEN8_PIPE_FIFO_UNDERRUN;

	dev_priv->de_irq_mask[PIPE_A] = ~de_pipe_masked;
	dev_priv->de_irq_mask[PIPE_B] = ~de_pipe_masked;
	dev_priv->de_irq_mask[PIPE_C] = ~de_pipe_masked;

	for_each_pipe(dev_priv, pipe)
		if (intel_display_power_is_enabled(dev_priv,
				POWER_DOMAIN_PIPE(pipe)))
			GEN8_IRQ_INIT_NDX(DE_PIPE, pipe,
					  dev_priv->de_irq_mask[pipe],
					  de_pipe_enables);

	GEN5_IRQ_INIT(GEN8_DE_PORT_, ~aux_en, aux_en);
}

static int gen8_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	ibx_irq_pre_postinstall(dev);

	gen8_gt_irq_postinstall(dev_priv);
	gen8_de_irq_postinstall(dev_priv);

	ibx_irq_postinstall(dev);

	I915_WRITE(GEN8_MASTER_IRQ, DE_MASTER_IRQ_CONTROL);
	POSTING_READ(GEN8_MASTER_IRQ);

	return 0;
}

static int cherryview_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	vlv_display_irq_postinstall(dev_priv);

	gen8_gt_irq_postinstall(dev_priv);

	I915_WRITE(GEN8_MASTER_IRQ, MASTER_INTERRUPT_ENABLE);
	POSTING_READ(GEN8_MASTER_IRQ);

	return 0;
}

static void gen8_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	gen8_irq_reset(dev);
}

static void vlv_display_irq_uninstall(struct drm_i915_private *dev_priv)
{
	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display_irqs_enabled)
		valleyview_display_irqs_uninstall(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	vlv_display_irq_reset(dev_priv);

	dev_priv->irq_mask = ~0;
}

static void valleyview_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	I915_WRITE(VLV_MASTER_IER, 0);

	gen5_gt_irq_reset(dev);

	I915_WRITE(HWSTAM, 0xffffffff);

	vlv_display_irq_uninstall(dev_priv);
}

static void cherryview_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	I915_WRITE(GEN8_MASTER_IRQ, 0);
	POSTING_READ(GEN8_MASTER_IRQ);

	gen8_gt_irq_reset(dev_priv);

	GEN5_IRQ_RESET(GEN8_PCU_);

	vlv_display_irq_uninstall(dev_priv);
}

static void ironlake_irq_uninstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	ironlake_irq_reset(dev);
}

static void i8xx_irq_preinstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	for_each_pipe(dev_priv, pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE16(IMR, 0xffff);
	I915_WRITE16(IER, 0x0);
	POSTING_READ16(IER);
}

static int i8xx_irq_postinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE16(EMR,
		     ~(I915_ERROR_PAGE_TABLE | I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		  I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT |
		  I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);
	I915_WRITE16(IMR, dev_priv->irq_mask);

	I915_WRITE16(IER,
		     I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		     I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		     I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT |
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
static bool i8xx_handle_vblank(struct drm_device *dev,
			       int plane, int pipe, u32 iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 flip_pending = DISPLAY_PLANE_FLIP_PENDING(plane);

	if (!intel_pipe_handle_vblank(dev, pipe))
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

	intel_prepare_page_flip(dev, plane);
	intel_finish_page_flip(dev, pipe);
	return true;

check_page_flip:
	intel_check_page_flip(dev, pipe);
	return false;
}

static irqreturn_t i8xx_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u16 iir, new_iir;
	u32 pipe_stats[2];
	int pipe;
	u16 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;

	iir = I915_READ16(IIR);
	if (iir == 0)
		return IRQ_NONE;

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
			int reg = PIPESTAT(pipe);
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
			notify_ring(dev, &dev_priv->ring[RCS]);

		for_each_pipe(dev_priv, pipe) {
			int plane = pipe;
			if (HAS_FBC(dev))
				plane = !plane;

			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS &&
			    i8xx_handle_vblank(dev, plane, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(plane);

			if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
				i9xx_pipe_crc_irq_handler(dev, pipe);

			if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
				intel_cpu_fifo_underrun_irq_handler(dev_priv,
								    pipe);
		}

		iir = new_iir;
	}

	return IRQ_HANDLED;
}

static void i8xx_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 enable_mask;

	I915_WRITE(EMR, ~(I915_ERROR_PAGE_TABLE | I915_ERROR_MEMORY_REFRESH));

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask =
		~(I915_ASLE_INTERRUPT |
		  I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		  I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		  I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		  I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT |
		  I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);

	enable_mask =
		I915_ASLE_INTERRUPT |
		I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT |
		I915_USER_INTERRUPT;

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		POSTING_READ(PORT_HOTPLUG_EN);

		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		dev_priv->irq_mask &= ~I915_DISPLAY_PORT_INTERRUPT;
	}

	I915_WRITE(IMR, dev_priv->irq_mask);
	I915_WRITE(IER, enable_mask);
	POSTING_READ(IER);

	i915_enable_asle_pipestat(dev);

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
static bool i915_handle_vblank(struct drm_device *dev,
			       int plane, int pipe, u32 iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 flip_pending = DISPLAY_PLANE_FLIP_PENDING(plane);

	if (!intel_pipe_handle_vblank(dev, pipe))
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

	intel_prepare_page_flip(dev, plane);
	intel_finish_page_flip(dev, pipe);
	return true;

check_page_flip:
	intel_check_page_flip(dev, pipe);
	return false;
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 iir, new_iir, pipe_stats[I915_MAX_PIPES];
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;
	int pipe, ret = IRQ_NONE;

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
			int reg = PIPESTAT(pipe);
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
		if (I915_HAS_HOTPLUG(dev) &&
		    iir & I915_DISPLAY_PORT_INTERRUPT)
			i9xx_hpd_irq_handler(dev);

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[RCS]);

		for_each_pipe(dev_priv, pipe) {
			int plane = pipe;
			if (HAS_FBC(dev))
				plane = !plane;

			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev, plane, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(plane);

			if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
				blc_event = true;

			if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
				i9xx_pipe_crc_irq_handler(dev, pipe);

			if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
				intel_cpu_fifo_underrun_irq_handler(dev_priv,
								    pipe);
		}

		if (blc_event || (iir & I915_ASLE_INTERRUPT))
			intel_opregion_asle_intr(dev);

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

	return ret;
}

static void i915_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	I915_WRITE(PORT_HOTPLUG_EN, 0);
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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

	if (IS_G4X(dev))
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
	if (IS_G4X(dev)) {
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

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	POSTING_READ(PORT_HOTPLUG_EN);

	i915_enable_asle_pipestat(dev);

	return 0;
}

static void i915_hpd_irq_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder;
	u32 hotplug_en;

	assert_spin_locked(&dev_priv->irq_lock);

	hotplug_en = I915_READ(PORT_HOTPLUG_EN);
	hotplug_en &= ~HOTPLUG_INT_EN_MASK;
	/* Note HDMI and DP share hotplug bits */
	/* enable bits are the same for all generations */
	for_each_intel_encoder(dev, intel_encoder)
		if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
			hotplug_en |= hpd_mask_i915[intel_encoder->hpd_pin];
	/* Programming the CRT detection parameters tends
	   to generate a spurious hotplug event about three
	   seconds later.  So just do it once.
	*/
	if (IS_G4X(dev))
		hotplug_en |= CRT_HOTPLUG_ACTIVATION_PERIOD_64;
	hotplug_en &= ~CRT_HOTPLUG_VOLTAGE_COMPARE_MASK;
	hotplug_en |= CRT_HOTPLUG_VOLTAGE_COMPARE_50;

	/* Ignore TV since it's buggy */
	I915_WRITE(PORT_HOTPLUG_EN, hotplug_en);
}

static irqreturn_t i965_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 iir, new_iir;
	u32 pipe_stats[I915_MAX_PIPES];
	int ret = IRQ_NONE, pipe;
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;

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
			int reg = PIPESTAT(pipe);
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
		if (iir & I915_DISPLAY_PORT_INTERRUPT)
			i9xx_hpd_irq_handler(dev);

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[RCS]);
		if (iir & I915_BSD_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[VCS]);

		for_each_pipe(dev_priv, pipe) {
			if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev, pipe, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(pipe);

			if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
				blc_event = true;

			if (pipe_stats[pipe] & PIPE_CRC_DONE_INTERRUPT_STATUS)
				i9xx_pipe_crc_irq_handler(dev, pipe);

			if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
				intel_cpu_fifo_underrun_irq_handler(dev_priv, pipe);
		}

		if (blc_event || (iir & I915_ASLE_INTERRUPT))
			intel_opregion_asle_intr(dev);

		if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
			gmbus_irq_handler(dev);

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

	return ret;
}

static void i965_irq_uninstall(struct drm_device * dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe;

	if (!dev_priv)
		return;

	I915_WRITE(PORT_HOTPLUG_EN, 0);
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

static void intel_hpd_irq_reenable_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv),
			     hotplug_reenable_work.work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int i;

	intel_runtime_pm_get(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	for (i = (HPD_NONE + 1); i < HPD_NUM_PINS; i++) {
		struct drm_connector *connector;

		if (dev_priv->hpd_stats[i].hpd_mark != HPD_DISABLED)
			continue;

		dev_priv->hpd_stats[i].hpd_mark = HPD_ENABLED;

		list_for_each_entry(connector, &mode_config->connector_list, head) {
			struct intel_connector *intel_connector = to_intel_connector(connector);

			if (intel_connector->encoder->hpd_pin == i) {
				if (connector->polled != intel_connector->polled)
					DRM_DEBUG_DRIVER("Reenabling HPD on connector %s\n",
							 connector->name);
				connector->polled = intel_connector->polled;
				if (!connector->polled)
					connector->polled = DRM_CONNECTOR_POLL_HPD;
			}
		}
	}
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_runtime_pm_put(dev_priv);
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
	struct drm_device *dev = dev_priv->dev;

	INIT_WORK(&dev_priv->hotplug_work, i915_hotplug_work_func);
	INIT_WORK(&dev_priv->dig_port_work, i915_digport_work_func);
	INIT_WORK(&dev_priv->rps.work, gen6_pm_rps_work);
	INIT_WORK(&dev_priv->l3_parity.error_work, ivybridge_parity_work);

	/* Let's track the enabled rps events */
	if (IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		/* WaGsvRC0ResidencyMethod:vlv */
		dev_priv->pm_rps_events = GEN6_PM_RP_UP_EI_EXPIRED;
	else
		dev_priv->pm_rps_events = GEN6_PM_RPS_EVENTS;

	INIT_DELAYED_WORK(&dev_priv->gpu_error.hangcheck_work,
			  i915_hangcheck_elapsed);
	INIT_DELAYED_WORK(&dev_priv->hotplug_reenable_work,
			  intel_hpd_irq_reenable_work);

	pm_qos_add_request(&dev_priv->pm_qos, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

	if (IS_GEN2(dev_priv)) {
		dev->max_vblank_count = 0;
		dev->driver->get_vblank_counter = i8xx_get_vblank_counter;
	} else if (IS_G4X(dev_priv) || INTEL_INFO(dev_priv)->gen >= 5) {
		dev->max_vblank_count = 0xffffffff; /* full 32 bit counter */
		dev->driver->get_vblank_counter = gm45_get_vblank_counter;
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

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		dev->driver->get_vblank_timestamp = i915_get_vblank_timestamp;
		dev->driver->get_scanout_position = i915_get_crtc_scanoutpos;
	}

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
		dev_priv->display.hpd_irq_setup = ibx_hpd_irq_setup;
	} else if (HAS_PCH_SPLIT(dev)) {
		dev->driver->irq_handler = ironlake_irq_handler;
		dev->driver->irq_preinstall = ironlake_irq_reset;
		dev->driver->irq_postinstall = ironlake_irq_postinstall;
		dev->driver->irq_uninstall = ironlake_irq_uninstall;
		dev->driver->enable_vblank = ironlake_enable_vblank;
		dev->driver->disable_vblank = ironlake_disable_vblank;
		dev_priv->display.hpd_irq_setup = ibx_hpd_irq_setup;
	} else {
		if (INTEL_INFO(dev_priv)->gen == 2) {
			dev->driver->irq_preinstall = i8xx_irq_preinstall;
			dev->driver->irq_postinstall = i8xx_irq_postinstall;
			dev->driver->irq_handler = i8xx_irq_handler;
			dev->driver->irq_uninstall = i8xx_irq_uninstall;
		} else if (INTEL_INFO(dev_priv)->gen == 3) {
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
 * intel_hpd_init - initializes and enables hpd support
 * @dev_priv: i915 device instance
 *
 * This function enables the hotplug support. It requires that interrupts have
 * already been enabled with intel_irq_init_hw(). From this point on hotplug and
 * poll request can run concurrently to other code, so locking rules must be
 * obeyed.
 *
 * This is a separate step from interrupt enabling to simplify the locking rules
 * in the driver load and resume code.
 */
void intel_hpd_init(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	int i;

	for (i = 1; i < HPD_NUM_PINS; i++) {
		dev_priv->hpd_stats[i].hpd_cnt = 0;
		dev_priv->hpd_stats[i].hpd_mark = HPD_ENABLED;
	}
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct intel_connector *intel_connector = to_intel_connector(connector);
		connector->polled = intel_connector->polled;
		if (connector->encoder && !connector->polled && I915_HAS_HOTPLUG(dev) && intel_connector->encoder->hpd_pin > HPD_NONE)
			connector->polled = DRM_CONNECTOR_POLL_HPD;
		if (intel_connector->mst_port)
			connector->polled = DRM_CONNECTOR_POLL_HPD;
	}

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked checks happy. */
	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock_irq(&dev_priv->irq_lock);
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

	return drm_irq_install(dev_priv->dev, dev_priv->dev->pdev->irq);
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
	drm_irq_uninstall(dev_priv->dev);
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
	dev_priv->dev->driver->irq_uninstall(dev_priv->dev);
	dev_priv->pm.irqs_enabled = false;
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
	dev_priv->dev->driver->irq_preinstall(dev_priv->dev);
	dev_priv->dev->driver->irq_postinstall(dev_priv->dev);
}
