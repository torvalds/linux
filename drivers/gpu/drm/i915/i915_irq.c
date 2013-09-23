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
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

static const u32 hpd_ibx[] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG
};

static const u32 hpd_cpt[] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG_CPT,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG_CPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT
};

static const u32 hpd_mask_i915[] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_EN,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_EN,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_EN,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_EN,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_EN,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_EN
};

static const u32 hpd_status_gen4[] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_G4X,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_G4X,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS
};

static const u32 hpd_status_i915[] = { /* i915 and valleyview are the same */
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I915,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I915,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS
};

/* For display hotplug interrupt */
static void
ironlake_enable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (dev_priv->pc8.irqs_disabled) {
		WARN(1, "IRQs disabled\n");
		dev_priv->pc8.regsave.deimr &= ~mask;
		return;
	}

	if ((dev_priv->irq_mask & mask) != 0) {
		dev_priv->irq_mask &= ~mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask);
		POSTING_READ(DEIMR);
	}
}

static void
ironlake_disable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	assert_spin_locked(&dev_priv->irq_lock);

	if (dev_priv->pc8.irqs_disabled) {
		WARN(1, "IRQs disabled\n");
		dev_priv->pc8.regsave.deimr |= mask;
		return;
	}

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

	if (dev_priv->pc8.irqs_disabled) {
		WARN(1, "IRQs disabled\n");
		dev_priv->pc8.regsave.gtimr &= ~interrupt_mask;
		dev_priv->pc8.regsave.gtimr |= (~enabled_irq_mask &
						interrupt_mask);
		return;
	}

	dev_priv->gt_irq_mask &= ~interrupt_mask;
	dev_priv->gt_irq_mask |= (~enabled_irq_mask & interrupt_mask);
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
	POSTING_READ(GTIMR);
}

void ilk_enable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, mask);
}

void ilk_disable_gt_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	ilk_update_gt_irq(dev_priv, mask, 0);
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

	assert_spin_locked(&dev_priv->irq_lock);

	if (dev_priv->pc8.irqs_disabled) {
		WARN(1, "IRQs disabled\n");
		dev_priv->pc8.regsave.gen6_pmimr &= ~interrupt_mask;
		dev_priv->pc8.regsave.gen6_pmimr |= (~enabled_irq_mask &
						     interrupt_mask);
		return;
	}

	new_val = dev_priv->pm_irq_mask;
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);

	if (new_val != dev_priv->pm_irq_mask) {
		dev_priv->pm_irq_mask = new_val;
		I915_WRITE(GEN6_PMIMR, dev_priv->pm_irq_mask);
		POSTING_READ(GEN6_PMIMR);
	}
}

void snb_enable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	snb_update_pm_irq(dev_priv, mask, mask);
}

void snb_disable_pm_irq(struct drm_i915_private *dev_priv, uint32_t mask)
{
	snb_update_pm_irq(dev_priv, mask, 0);
}

static bool ivb_can_enable_err_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc;
	enum pipe pipe;

	assert_spin_locked(&dev_priv->irq_lock);

	for_each_pipe(pipe) {
		crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);

		if (crtc->cpu_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static bool cpt_can_enable_serr_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe;
	struct intel_crtc *crtc;

	assert_spin_locked(&dev_priv->irq_lock);

	for_each_pipe(pipe) {
		crtc = to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);

		if (crtc->pch_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static void ironlake_set_fifo_underrun_reporting(struct drm_device *dev,
						 enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t bit = (pipe == PIPE_A) ? DE_PIPEA_FIFO_UNDERRUN :
					  DE_PIPEB_FIFO_UNDERRUN;

	if (enable)
		ironlake_enable_display_irq(dev_priv, bit);
	else
		ironlake_disable_display_irq(dev_priv, bit);
}

static void ivybridge_set_fifo_underrun_reporting(struct drm_device *dev,
						  enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (enable) {
		I915_WRITE(GEN7_ERR_INT, ERR_INT_FIFO_UNDERRUN(pipe));

		if (!ivb_can_enable_err_int(dev))
			return;

		ironlake_enable_display_irq(dev_priv, DE_ERR_INT_IVB);
	} else {
		bool was_enabled = !(I915_READ(DEIMR) & DE_ERR_INT_IVB);

		/* Change the state _after_ we've read out the current one. */
		ironlake_disable_display_irq(dev_priv, DE_ERR_INT_IVB);

		if (!was_enabled &&
		    (I915_READ(GEN7_ERR_INT) & ERR_INT_FIFO_UNDERRUN(pipe))) {
			DRM_DEBUG_KMS("uncleared fifo underrun on pipe %c\n",
				      pipe_name(pipe));
		}
	}
}

/**
 * ibx_display_interrupt_update - update SDEIMR
 * @dev_priv: driver private
 * @interrupt_mask: mask of interrupt bits to update
 * @enabled_irq_mask: mask of interrupt bits to enable
 */
static void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
					 uint32_t interrupt_mask,
					 uint32_t enabled_irq_mask)
{
	uint32_t sdeimr = I915_READ(SDEIMR);
	sdeimr &= ~interrupt_mask;
	sdeimr |= (~enabled_irq_mask & interrupt_mask);

	assert_spin_locked(&dev_priv->irq_lock);

	if (dev_priv->pc8.irqs_disabled &&
	    (interrupt_mask & SDE_HOTPLUG_MASK_CPT)) {
		WARN(1, "IRQs disabled\n");
		dev_priv->pc8.regsave.sdeimr &= ~interrupt_mask;
		dev_priv->pc8.regsave.sdeimr |= (~enabled_irq_mask &
						 interrupt_mask);
		return;
	}

	I915_WRITE(SDEIMR, sdeimr);
	POSTING_READ(SDEIMR);
}
#define ibx_enable_display_interrupt(dev_priv, bits) \
	ibx_display_interrupt_update((dev_priv), (bits), (bits))
#define ibx_disable_display_interrupt(dev_priv, bits) \
	ibx_display_interrupt_update((dev_priv), (bits), 0)

static void ibx_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum transcoder pch_transcoder,
					    bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t bit = (pch_transcoder == TRANSCODER_A) ?
		       SDE_TRANSA_FIFO_UNDER : SDE_TRANSB_FIFO_UNDER;

	if (enable)
		ibx_enable_display_interrupt(dev_priv, bit);
	else
		ibx_disable_display_interrupt(dev_priv, bit);
}

static void cpt_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum transcoder pch_transcoder,
					    bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (enable) {
		I915_WRITE(SERR_INT,
			   SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));

		if (!cpt_can_enable_serr_int(dev))
			return;

		ibx_enable_display_interrupt(dev_priv, SDE_ERROR_CPT);
	} else {
		uint32_t tmp = I915_READ(SERR_INT);
		bool was_enabled = !(I915_READ(SDEIMR) & SDE_ERROR_CPT);

		/* Change the state _after_ we've read out the current one. */
		ibx_disable_display_interrupt(dev_priv, SDE_ERROR_CPT);

		if (!was_enabled &&
		    (tmp & SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder))) {
			DRM_DEBUG_KMS("uncleared pch fifo underrun on pch transcoder %c\n",
				      transcoder_name(pch_transcoder));
		}
	}
}

/**
 * intel_set_cpu_fifo_underrun_reporting - enable/disable FIFO underrun messages
 * @dev: drm device
 * @pipe: pipe
 * @enable: true if we want to report FIFO underrun errors, false otherwise
 *
 * This function makes us disable or enable CPU fifo underruns for a specific
 * pipe. Notice that on some Gens (e.g. IVB, HSW), disabling FIFO underrun
 * reporting for one pipe may also disable all the other CPU error interruts for
 * the other pipes, due to the fact that there's just one interrupt mask/enable
 * bit for all the pipes.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_cpu_fifo_underrun_reporting(struct drm_device *dev,
					   enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);

	ret = !intel_crtc->cpu_fifo_underrun_disabled;

	if (enable == ret)
		goto done;

	intel_crtc->cpu_fifo_underrun_disabled = !enable;

	if (IS_GEN5(dev) || IS_GEN6(dev))
		ironlake_set_fifo_underrun_reporting(dev, pipe, enable);
	else if (IS_GEN7(dev))
		ivybridge_set_fifo_underrun_reporting(dev, pipe, enable);

done:
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
	return ret;
}

/**
 * intel_set_pch_fifo_underrun_reporting - enable/disable FIFO underrun messages
 * @dev: drm device
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 * @enable: true if we want to report FIFO underrun errors, false otherwise
 *
 * This function makes us disable or enable PCH fifo underruns for a specific
 * PCH transcoder. Notice that on some PCHs (e.g. CPT/PPT), disabling FIFO
 * underrun reporting for one transcoder may also disable all the other PCH
 * error interruts for the other transcoders, due to the fact that there's just
 * one interrupt mask/enable bit for all the transcoders.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_pch_fifo_underrun_reporting(struct drm_device *dev,
					   enum transcoder pch_transcoder,
					   bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pch_transcoder];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	unsigned long flags;
	bool ret;

	/*
	 * NOTE: Pre-LPT has a fixed cpu pipe -> pch transcoder mapping, but LPT
	 * has only one pch transcoder A that all pipes can use. To avoid racy
	 * pch transcoder -> pipe lookups from interrupt code simply store the
	 * underrun statistics in crtc A. Since we never expose this anywhere
	 * nor use it outside of the fifo underrun code here using the "wrong"
	 * crtc on LPT won't cause issues.
	 */

	spin_lock_irqsave(&dev_priv->irq_lock, flags);

	ret = !intel_crtc->pch_fifo_underrun_disabled;

	if (enable == ret)
		goto done;

	intel_crtc->pch_fifo_underrun_disabled = !enable;

	if (HAS_PCH_IBX(dev))
		ibx_set_fifo_underrun_reporting(dev, pch_transcoder, enable);
	else
		cpt_set_fifo_underrun_reporting(dev, pch_transcoder, enable);

done:
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
	return ret;
}


void
i915_enable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	u32 reg = PIPESTAT(pipe);
	u32 pipestat = I915_READ(reg) & 0x7fff0000;

	assert_spin_locked(&dev_priv->irq_lock);

	if ((pipestat & mask) == mask)
		return;

	/* Enable the interrupt, clear any pending status */
	pipestat |= mask | (mask >> 16);
	I915_WRITE(reg, pipestat);
	POSTING_READ(reg);
}

void
i915_disable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	u32 reg = PIPESTAT(pipe);
	u32 pipestat = I915_READ(reg) & 0x7fff0000;

	assert_spin_locked(&dev_priv->irq_lock);

	if ((pipestat & mask) == 0)
		return;

	pipestat &= ~mask;
	I915_WRITE(reg, pipestat);
	POSTING_READ(reg);
}

/**
 * i915_enable_asle_pipestat - enable ASLE pipestat for OpRegion
 */
static void i915_enable_asle_pipestat(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned long irqflags;

	if (!dev_priv->opregion.asle || !IS_MOBILE(dev))
		return;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	i915_enable_pipestat(dev_priv, 1, PIPE_LEGACY_BLC_EVENT_ENABLE);
	if (INTEL_INFO(dev)->gen >= 4)
		i915_enable_pipestat(dev_priv, 0, PIPE_LEGACY_BLC_EVENT_ENABLE);

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
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
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* Locking is horribly broken here, but whatever. */
		struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
		struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

		return intel_crtc->active;
	} else {
		return I915_READ(PIPECONF(pipe)) & PIPECONF_ENABLE;
	}
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
static u32 i915_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long high_frame;
	unsigned long low_frame;
	u32 high1, high2, low;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
				"pipe %c\n", pipe_name(pipe));
		return 0;
	}

	high_frame = PIPEFRAME(pipe);
	low_frame = PIPEFRAMEPIXEL(pipe);

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK;
		low   = I915_READ(low_frame)  & PIPE_FRAME_LOW_MASK;
		high2 = I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK;
	} while (high1 != high2);

	high1 >>= PIPE_FRAME_HIGH_SHIFT;
	low >>= PIPE_FRAME_LOW_SHIFT;
	return (high1 << 8) | low;
}

static u32 gm45_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int reg = PIPE_FRMCOUNT_GM45(pipe);

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
				 "pipe %c\n", pipe_name(pipe));
		return 0;
	}

	return I915_READ(reg);
}

static int i915_get_crtc_scanoutpos(struct drm_device *dev, int pipe,
			     int *vpos, int *hpos)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 vbl = 0, position = 0;
	int vbl_start, vbl_end, htotal, vtotal;
	bool in_vbl = true;
	int ret = 0;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get scanoutpos for disabled "
				 "pipe %c\n", pipe_name(pipe));
		return 0;
	}

	/* Get vtotal. */
	vtotal = 1 + ((I915_READ(VTOTAL(cpu_transcoder)) >> 16) & 0x1fff);

	if (INTEL_INFO(dev)->gen >= 4) {
		/* No obvious pixelcount register. Only query vertical
		 * scanout position from Display scan line register.
		 */
		position = I915_READ(PIPEDSL(pipe));

		/* Decode into vertical scanout position. Don't have
		 * horizontal scanout position.
		 */
		*vpos = position & 0x1fff;
		*hpos = 0;
	} else {
		/* Have access to pixelcount since start of frame.
		 * We can split this into vertical and horizontal
		 * scanout position.
		 */
		position = (I915_READ(PIPEFRAMEPIXEL(pipe)) & PIPE_PIXEL_MASK) >> PIPE_PIXEL_SHIFT;

		htotal = 1 + ((I915_READ(HTOTAL(cpu_transcoder)) >> 16) & 0x1fff);
		*vpos = position / htotal;
		*hpos = position - (*vpos * htotal);
	}

	/* Query vblank area. */
	vbl = I915_READ(VBLANK(cpu_transcoder));

	/* Test position against vblank region. */
	vbl_start = vbl & 0x1fff;
	vbl_end = (vbl >> 16) & 0x1fff;

	if ((*vpos < vbl_start) || (*vpos > vbl_end))
		in_vbl = false;

	/* Inside "upper part" of vblank area? Apply corrective offset: */
	if (in_vbl && (*vpos >= vbl_start))
		*vpos = *vpos - vtotal;

	/* Readouts valid? */
	if (vbl > 0)
		ret |= DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE;

	/* In vblank? */
	if (in_vbl)
		ret |= DRM_SCANOUTPOS_INVBL;

	return ret;
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
						     crtc);
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
		      drm_get_connector_name(connector),
		      drm_get_connector_status_name(old_status),
		      drm_get_connector_status_name(connector->status));

	return true;
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
#define I915_REENABLE_HOTPLUG_DELAY (2*60*1000)

static void i915_hotplug_work_func(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    hotplug_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_connector *intel_connector;
	struct intel_encoder *intel_encoder;
	struct drm_connector *connector;
	unsigned long irqflags;
	bool hpd_disabled = false;
	bool changed = false;
	u32 hpd_event_bits;

	/* HPD irq before everything is fully set up. */
	if (!dev_priv->enable_hotplug_processing)
		return;

	mutex_lock(&mode_config->mutex);
	DRM_DEBUG_KMS("running encoder hotplug functions\n");

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	hpd_event_bits = dev_priv->hpd_event_bits;
	dev_priv->hpd_event_bits = 0;
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		intel_connector = to_intel_connector(connector);
		intel_encoder = intel_connector->encoder;
		if (intel_encoder->hpd_pin > HPD_NONE &&
		    dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_MARK_DISABLED &&
		    connector->polled == DRM_CONNECTOR_POLL_HPD) {
			DRM_INFO("HPD interrupt storm detected on connector %s: "
				 "switching from hotplug detection to polling\n",
				drm_get_connector_name(connector));
			dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark = HPD_DISABLED;
			connector->polled = DRM_CONNECTOR_POLL_CONNECT
				| DRM_CONNECTOR_POLL_DISCONNECT;
			hpd_disabled = true;
		}
		if (hpd_event_bits & (1 << intel_encoder->hpd_pin)) {
			DRM_DEBUG_KMS("Connector %s (pin %i) received hotplug event.\n",
				      drm_get_connector_name(connector), intel_encoder->hpd_pin);
		}
	}
	 /* if there were no outputs to poll, poll was disabled,
	  * therefore make sure it's enabled when disabling HPD on
	  * some connectors */
	if (hpd_disabled) {
		drm_kms_helper_poll_enable(dev);
		mod_timer(&dev_priv->hotplug_reenable_timer,
			  jiffies + msecs_to_jiffies(I915_REENABLE_HOTPLUG_DELAY));
	}

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		intel_connector = to_intel_connector(connector);
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
	drm_i915_private_t *dev_priv = dev->dev_private;
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
			struct intel_ring_buffer *ring)
{
	if (ring->obj == NULL)
		return;

	trace_i915_gem_request_complete(ring);

	wake_up_all(&ring->irq_queue);
	i915_queue_hangcheck(dev);
}

static void gen6_pm_rps_work(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    rps.work);
	u32 pm_iir;
	u8 new_delay;

	spin_lock_irq(&dev_priv->irq_lock);
	pm_iir = dev_priv->rps.pm_iir;
	dev_priv->rps.pm_iir = 0;
	/* Make sure not to corrupt PMIMR state used by ringbuffer code */
	snb_enable_pm_irq(dev_priv, GEN6_PM_RPS_EVENTS);
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	WARN_ON(pm_iir & ~GEN6_PM_RPS_EVENTS);

	if ((pm_iir & GEN6_PM_RPS_EVENTS) == 0)
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	if (pm_iir & GEN6_PM_RP_UP_THRESHOLD) {
		new_delay = dev_priv->rps.cur_delay + 1;

		/*
		 * For better performance, jump directly
		 * to RPe if we're below it.
		 */
		if (IS_VALLEYVIEW(dev_priv->dev) &&
		    dev_priv->rps.cur_delay < dev_priv->rps.rpe_delay)
			new_delay = dev_priv->rps.rpe_delay;
	} else
		new_delay = dev_priv->rps.cur_delay - 1;

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	if (new_delay >= dev_priv->rps.min_delay &&
	    new_delay <= dev_priv->rps.max_delay) {
		if (IS_VALLEYVIEW(dev_priv->dev))
			valleyview_set_rps(dev_priv->dev, new_delay);
		else
			gen6_set_rps(dev_priv->dev, new_delay);
	}

	if (IS_VALLEYVIEW(dev_priv->dev)) {
		/*
		 * On VLV, when we enter RC6 we may not be at the minimum
		 * voltage level, so arm a timer to check.  It should only
		 * fire when there's activity or once after we've entered
		 * RC6, and then won't be re-armed until the next RPS interrupt.
		 */
		mod_delayed_work(dev_priv->wq, &dev_priv->rps.vlv_work,
				 msecs_to_jiffies(100));
	}

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
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    l3_parity.error_work);
	u32 error_status, row, bank, subbank;
	char *parity_event[6];
	uint32_t misccpctl;
	unsigned long flags;
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

		kobject_uevent_env(&dev_priv->dev->primary->kdev.kobj,
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
	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	ilk_enable_gt_irq(dev_priv, GT_PARITY_ERROR(dev_priv->dev));
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	mutex_unlock(&dev_priv->dev->struct_mutex);
}

static void ivybridge_parity_error_irq_handler(struct drm_device *dev, u32 iir)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!HAS_L3_DPF(dev))
		return;

	spin_lock(&dev_priv->irq_lock);
	ilk_disable_gt_irq(dev_priv, GT_PARITY_ERROR(dev));
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
		      GT_RENDER_CS_MASTER_ERROR_INTERRUPT)) {
		DRM_ERROR("GT error interrupt 0x%08x\n", gt_iir);
		i915_handle_error(dev, false);
	}

	if (gt_iir & GT_PARITY_ERROR(dev))
		ivybridge_parity_error_irq_handler(dev, gt_iir);
}

#define HPD_STORM_DETECT_PERIOD 1000
#define HPD_STORM_THRESHOLD 5

static inline void intel_hpd_irq_handler(struct drm_device *dev,
					 u32 hotplug_trigger,
					 const u32 *hpd)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	bool storm_detected = false;

	if (!hotplug_trigger)
		return;

	spin_lock(&dev_priv->irq_lock);
	for (i = 1; i < HPD_NUM_PINS; i++) {

		WARN(((hpd[i] & hotplug_trigger) &&
		      dev_priv->hpd_stats[i].hpd_mark != HPD_ENABLED),
		     "Received HPD interrupt although disabled\n");

		if (!(hpd[i] & hotplug_trigger) ||
		    dev_priv->hpd_stats[i].hpd_mark != HPD_ENABLED)
			continue;

		dev_priv->hpd_event_bits |= (1 << i);
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
	schedule_work(&dev_priv->hotplug_work);
}

static void gmbus_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = (drm_i915_private_t *) dev->dev_private;

	wake_up_all(&dev_priv->gmbus_wait_queue);
}

static void dp_aux_irq_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = (drm_i915_private_t *) dev->dev_private;

	wake_up_all(&dev_priv->gmbus_wait_queue);
}

/* The RPS events need forcewake, so we add them to a work queue and mask their
 * IMR bits until the work is done. Other interrupts can be processed without
 * the work queue. */
static void gen6_rps_irq_handler(struct drm_i915_private *dev_priv, u32 pm_iir)
{
	if (pm_iir & GEN6_PM_RPS_EVENTS) {
		spin_lock(&dev_priv->irq_lock);
		dev_priv->rps.pm_iir |= pm_iir & GEN6_PM_RPS_EVENTS;
		snb_disable_pm_irq(dev_priv, pm_iir & GEN6_PM_RPS_EVENTS);
		spin_unlock(&dev_priv->irq_lock);

		queue_work(dev_priv->wq, &dev_priv->rps.work);
	}

	if (HAS_VEBOX(dev_priv->dev)) {
		if (pm_iir & PM_VEBOX_USER_INTERRUPT)
			notify_ring(dev_priv->dev, &dev_priv->ring[VECS]);

		if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT) {
			DRM_ERROR("VEBOX CS error interrupt 0x%08x\n", pm_iir);
			i915_handle_error(dev_priv->dev, false);
		}
	}
}

static irqreturn_t valleyview_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 iir, gt_iir, pm_iir;
	irqreturn_t ret = IRQ_NONE;
	unsigned long irqflags;
	int pipe;
	u32 pipe_stats[I915_MAX_PIPES];

	atomic_inc(&dev_priv->irq_received);

	while (true) {
		iir = I915_READ(VLV_IIR);
		gt_iir = I915_READ(GTIIR);
		pm_iir = I915_READ(GEN6_PMIIR);

		if (gt_iir == 0 && pm_iir == 0 && iir == 0)
			goto out;

		ret = IRQ_HANDLED;

		snb_gt_irq_handler(dev, dev_priv, gt_iir);

		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		for_each_pipe(pipe) {
			int reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/*
			 * Clear the PIPE*STAT regs before the IIR
			 */
			if (pipe_stats[pipe] & 0x8000ffff) {
				if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
					DRM_DEBUG_DRIVER("pipe %c underrun\n",
							 pipe_name(pipe));
				I915_WRITE(reg, pipe_stats[pipe]);
			}
		}
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

		for_each_pipe(pipe) {
			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS)
				drm_handle_vblank(dev, pipe);

			if (pipe_stats[pipe] & PLANE_FLIPDONE_INT_STATUS_VLV) {
				intel_prepare_page_flip(dev, pipe);
				intel_finish_page_flip(dev, pipe);
			}
		}

		/* Consume port.  Then clear IIR or we'll miss events */
		if (iir & I915_DISPLAY_PORT_INTERRUPT) {
			u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);
			u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
					 hotplug_status);

			intel_hpd_irq_handler(dev, hotplug_trigger, hpd_status_i915);

			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			I915_READ(PORT_HOTPLUG_STAT);
		}

		if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
			gmbus_irq_handler(dev);

		if (pm_iir)
			gen6_rps_irq_handler(dev_priv, pm_iir);

		I915_WRITE(GTIIR, gt_iir);
		I915_WRITE(GEN6_PMIIR, pm_iir);
		I915_WRITE(VLV_IIR, iir);
	}

out:
	return ret;
}

static void ibx_irq_handler(struct drm_device *dev, u32 pch_iir)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK;

	intel_hpd_irq_handler(dev, hotplug_trigger, hpd_ibx);

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
		for_each_pipe(pipe)
			DRM_DEBUG_DRIVER("  pipe %c FDI IIR: 0x%08x\n",
					 pipe_name(pipe),
					 I915_READ(FDI_RX_IIR(pipe)));

	if (pch_iir & (SDE_TRANSB_CRC_DONE | SDE_TRANSA_CRC_DONE))
		DRM_DEBUG_DRIVER("PCH transcoder CRC done interrupt\n");

	if (pch_iir & (SDE_TRANSB_CRC_ERR | SDE_TRANSA_CRC_ERR))
		DRM_DEBUG_DRIVER("PCH transcoder CRC error interrupt\n");

	if (pch_iir & SDE_TRANSA_FIFO_UNDER)
		if (intel_set_pch_fifo_underrun_reporting(dev, TRANSCODER_A,
							  false))
			DRM_DEBUG_DRIVER("PCH transcoder A FIFO underrun\n");

	if (pch_iir & SDE_TRANSB_FIFO_UNDER)
		if (intel_set_pch_fifo_underrun_reporting(dev, TRANSCODER_B,
							  false))
			DRM_DEBUG_DRIVER("PCH transcoder B FIFO underrun\n");
}

static void ivb_err_int_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 err_int = I915_READ(GEN7_ERR_INT);

	if (err_int & ERR_INT_POISON)
		DRM_ERROR("Poison interrupt\n");

	if (err_int & ERR_INT_FIFO_UNDERRUN_A)
		if (intel_set_cpu_fifo_underrun_reporting(dev, PIPE_A, false))
			DRM_DEBUG_DRIVER("Pipe A FIFO underrun\n");

	if (err_int & ERR_INT_FIFO_UNDERRUN_B)
		if (intel_set_cpu_fifo_underrun_reporting(dev, PIPE_B, false))
			DRM_DEBUG_DRIVER("Pipe B FIFO underrun\n");

	if (err_int & ERR_INT_FIFO_UNDERRUN_C)
		if (intel_set_cpu_fifo_underrun_reporting(dev, PIPE_C, false))
			DRM_DEBUG_DRIVER("Pipe C FIFO underrun\n");

	I915_WRITE(GEN7_ERR_INT, err_int);
}

static void cpt_serr_int_handler(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 serr_int = I915_READ(SERR_INT);

	if (serr_int & SERR_INT_POISON)
		DRM_ERROR("PCH poison interrupt\n");

	if (serr_int & SERR_INT_TRANS_A_FIFO_UNDERRUN)
		if (intel_set_pch_fifo_underrun_reporting(dev, TRANSCODER_A,
							  false))
			DRM_DEBUG_DRIVER("PCH transcoder A FIFO underrun\n");

	if (serr_int & SERR_INT_TRANS_B_FIFO_UNDERRUN)
		if (intel_set_pch_fifo_underrun_reporting(dev, TRANSCODER_B,
							  false))
			DRM_DEBUG_DRIVER("PCH transcoder B FIFO underrun\n");

	if (serr_int & SERR_INT_TRANS_C_FIFO_UNDERRUN)
		if (intel_set_pch_fifo_underrun_reporting(dev, TRANSCODER_C,
							  false))
			DRM_DEBUG_DRIVER("PCH transcoder C FIFO underrun\n");

	I915_WRITE(SERR_INT, serr_int);
}

static void cpt_irq_handler(struct drm_device *dev, u32 pch_iir)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_CPT;

	intel_hpd_irq_handler(dev, hotplug_trigger, hpd_cpt);

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
		for_each_pipe(pipe)
			DRM_DEBUG_DRIVER("  pipe %c FDI IIR: 0x%08x\n",
					 pipe_name(pipe),
					 I915_READ(FDI_RX_IIR(pipe)));

	if (pch_iir & SDE_ERROR_CPT)
		cpt_serr_int_handler(dev);
}

static void ilk_display_irq_handler(struct drm_device *dev, u32 de_iir)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (de_iir & DE_AUX_CHANNEL_A)
		dp_aux_irq_handler(dev);

	if (de_iir & DE_GSE)
		intel_opregion_asle_intr(dev);

	if (de_iir & DE_PIPEA_VBLANK)
		drm_handle_vblank(dev, 0);

	if (de_iir & DE_PIPEB_VBLANK)
		drm_handle_vblank(dev, 1);

	if (de_iir & DE_POISON)
		DRM_ERROR("Poison interrupt\n");

	if (de_iir & DE_PIPEA_FIFO_UNDERRUN)
		if (intel_set_cpu_fifo_underrun_reporting(dev, PIPE_A, false))
			DRM_DEBUG_DRIVER("Pipe A FIFO underrun\n");

	if (de_iir & DE_PIPEB_FIFO_UNDERRUN)
		if (intel_set_cpu_fifo_underrun_reporting(dev, PIPE_B, false))
			DRM_DEBUG_DRIVER("Pipe B FIFO underrun\n");

	if (de_iir & DE_PLANEA_FLIP_DONE) {
		intel_prepare_page_flip(dev, 0);
		intel_finish_page_flip_plane(dev, 0);
	}

	if (de_iir & DE_PLANEB_FLIP_DONE) {
		intel_prepare_page_flip(dev, 1);
		intel_finish_page_flip_plane(dev, 1);
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
	int i;

	if (de_iir & DE_ERR_INT_IVB)
		ivb_err_int_handler(dev);

	if (de_iir & DE_AUX_CHANNEL_A_IVB)
		dp_aux_irq_handler(dev);

	if (de_iir & DE_GSE_IVB)
		intel_opregion_asle_intr(dev);

	for (i = 0; i < 3; i++) {
		if (de_iir & (DE_PIPEA_VBLANK_IVB << (5 * i)))
			drm_handle_vblank(dev, i);
		if (de_iir & (DE_PLANEA_FLIP_DONE_IVB << (5 * i))) {
			intel_prepare_page_flip(dev, i);
			intel_finish_page_flip_plane(dev, i);
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

static irqreturn_t ironlake_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 de_iir, gt_iir, de_ier, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;

	atomic_inc(&dev_priv->irq_received);

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

	gt_iir = I915_READ(GTIIR);
	if (gt_iir) {
		if (INTEL_INFO(dev)->gen >= 6)
			snb_gt_irq_handler(dev, dev_priv, gt_iir);
		else
			ilk_gt_irq_handler(dev, dev_priv, gt_iir);
		I915_WRITE(GTIIR, gt_iir);
		ret = IRQ_HANDLED;
	}

	de_iir = I915_READ(DEIIR);
	if (de_iir) {
		if (INTEL_INFO(dev)->gen >= 7)
			ivb_display_irq_handler(dev, de_iir);
		else
			ilk_display_irq_handler(dev, de_iir);
		I915_WRITE(DEIIR, de_iir);
		ret = IRQ_HANDLED;
	}

	if (INTEL_INFO(dev)->gen >= 6) {
		u32 pm_iir = I915_READ(GEN6_PMIIR);
		if (pm_iir) {
			gen6_rps_irq_handler(dev_priv, pm_iir);
			I915_WRITE(GEN6_PMIIR, pm_iir);
			ret = IRQ_HANDLED;
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

static void i915_error_wake_up(struct drm_i915_private *dev_priv,
			       bool reset_completed)
{
	struct intel_ring_buffer *ring;
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
 * i915_error_work_func - do process context error handling work
 * @work: work struct
 *
 * Fire an error uevent so userspace can see that a hang or error
 * was detected.
 */
static void i915_error_work_func(struct work_struct *work)
{
	struct i915_gpu_error *error = container_of(work, struct i915_gpu_error,
						    work);
	drm_i915_private_t *dev_priv = container_of(error, drm_i915_private_t,
						    gpu_error);
	struct drm_device *dev = dev_priv->dev;
	char *error_event[] = { I915_ERROR_UEVENT "=1", NULL };
	char *reset_event[] = { I915_RESET_UEVENT "=1", NULL };
	char *reset_done_event[] = { I915_ERROR_UEVENT "=0", NULL };
	int ret;

	kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, error_event);

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
		kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE,
				   reset_event);

		/*
		 * All state reset _must_ be completed before we update the
		 * reset counter, for otherwise waiters might miss the reset
		 * pending state and not properly drop locks, resulting in
		 * deadlocks with the reset work.
		 */
		ret = i915_reset(dev);

		intel_display_handle_reset(dev);

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
			smp_mb__before_atomic_inc();
			atomic_inc(&dev_priv->gpu_error.reset_counter);

			kobject_uevent_env(&dev->primary->kdev.kobj,
					   KOBJ_CHANGE, reset_done_event);
		} else {
			atomic_set(&error->reset_counter, I915_WEDGED);
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
		for_each_pipe(pipe)
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
 * i915_handle_error - handle an error interrupt
 * @dev: drm device
 *
 * Do some basic checking of regsiter state at error interrupt time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 */
void i915_handle_error(struct drm_device *dev, bool wedged)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	i915_capture_error_state(dev);
	i915_report_and_clear_eir(dev);

	if (wedged) {
		atomic_set_mask(I915_RESET_IN_PROGRESS_FLAG,
				&dev_priv->gpu_error.reset_counter);

		/*
		 * Wakeup waiting processes so that the reset work function
		 * i915_error_work_func doesn't deadlock trying to grab various
		 * locks. By bumping the reset counter first, the woken
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

	/*
	 * Our reset work can grab modeset locks (since it needs to reset the
	 * state of outstanding pagelips). Hence it must not be run on our own
	 * dev-priv->wq work queue for otherwise the flush_work in the pageflip
	 * code will deadlock.
	 */
	schedule_work(&dev_priv->gpu_error.work);
}

static void __always_unused i915_pageflip_stall_check(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj;
	struct intel_unpin_work *work;
	unsigned long flags;
	bool stall_detected;

	/* Ignore early vblank irqs */
	if (intel_crtc == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	work = intel_crtc->unpin_work;

	if (work == NULL ||
	    atomic_read(&work->pending) >= INTEL_FLIP_COMPLETE ||
	    !work->enable_stall_check) {
		/* Either the pending flip IRQ arrived, or we're too early. Don't check */
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}

	/* Potential stall - if we see that the flip has happened, assume a missed interrupt */
	obj = work->pending_flip_obj;
	if (INTEL_INFO(dev)->gen >= 4) {
		int dspsurf = DSPSURF(intel_crtc->plane);
		stall_detected = I915_HI_DISPBASE(I915_READ(dspsurf)) ==
					i915_gem_obj_ggtt_offset(obj);
	} else {
		int dspaddr = DSPADDR(intel_crtc->plane);
		stall_detected = I915_READ(dspaddr) == (i915_gem_obj_ggtt_offset(obj) +
							crtc->y * crtc->fb->pitches[0] +
							crtc->x * crtc->fb->bits_per_pixel/8);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (stall_detected) {
		DRM_DEBUG_DRIVER("Pageflip stall detected\n");
		intel_prepare_page_flip(dev, intel_crtc->plane);
	}
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static int i915_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	if (INTEL_INFO(dev)->gen >= 4)
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_START_VBLANK_INTERRUPT_ENABLE);
	else
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_VBLANK_INTERRUPT_ENABLE);

	/* maintain vblank delivery even in deep C-states */
	if (dev_priv->info->gen == 3)
		I915_WRITE(INSTPM, _MASKED_BIT_DISABLE(INSTPM_AGPBUSY_DIS));
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int ironlake_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK_ILK(pipe);

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_enable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int valleyview_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	u32 imr;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	imr = I915_READ(VLV_IMR);
	if (pipe == 0)
		imr &= ~I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT;
	else
		imr &= ~I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT;
	I915_WRITE(VLV_IMR, imr);
	i915_enable_pipestat(dev_priv, pipe,
			     PIPE_START_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
static void i915_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	if (dev_priv->info->gen == 3)
		I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_AGPBUSY_DIS));

	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_VBLANK_INTERRUPT_ENABLE |
			      PIPE_START_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ironlake_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	uint32_t bit = (INTEL_INFO(dev)->gen >= 7) ? DE_PIPE_VBLANK_IVB(pipe) :
						     DE_PIPE_VBLANK_ILK(pipe);

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_disable_display_irq(dev_priv, bit);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void valleyview_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;
	u32 imr;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_disable_pipestat(dev_priv, pipe,
			      PIPE_START_VBLANK_INTERRUPT_ENABLE);
	imr = I915_READ(VLV_IMR);
	if (pipe == 0)
		imr |= I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT;
	else
		imr |= I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT;
	I915_WRITE(VLV_IMR, imr);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static u32
ring_last_seqno(struct intel_ring_buffer *ring)
{
	return list_entry(ring->request_list.prev,
			  struct drm_i915_gem_request, list)->seqno;
}

static bool
ring_idle(struct intel_ring_buffer *ring, u32 seqno)
{
	return (list_empty(&ring->request_list) ||
		i915_seqno_passed(seqno, ring_last_seqno(ring)));
}

static struct intel_ring_buffer *
semaphore_waits_for(struct intel_ring_buffer *ring, u32 *seqno)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u32 cmd, ipehr, acthd, acthd_min;

	ipehr = I915_READ(RING_IPEHR(ring->mmio_base));
	if ((ipehr & ~(0x3 << 16)) !=
	    (MI_SEMAPHORE_MBOX | MI_SEMAPHORE_COMPARE | MI_SEMAPHORE_REGISTER))
		return NULL;

	/* ACTHD is likely pointing to the dword after the actual command,
	 * so scan backwards until we find the MBOX.
	 */
	acthd = intel_ring_get_active_head(ring) & HEAD_ADDR;
	acthd_min = max((int)acthd - 3 * 4, 0);
	do {
		cmd = ioread32(ring->virtual_start + acthd);
		if (cmd == ipehr)
			break;

		acthd -= 4;
		if (acthd < acthd_min)
			return NULL;
	} while (1);

	*seqno = ioread32(ring->virtual_start+acthd+4)+1;
	return &dev_priv->ring[(ring->id + (((ipehr >> 17) & 1) + 1)) % 3];
}

static int semaphore_passed(struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct intel_ring_buffer *signaller;
	u32 seqno, ctl;

	ring->hangcheck.deadlock = true;

	signaller = semaphore_waits_for(ring, &seqno);
	if (signaller == NULL || signaller->hangcheck.deadlock)
		return -1;

	/* cursory check for an unkickable deadlock */
	ctl = I915_READ_CTL(signaller);
	if (ctl & RING_WAIT_SEMAPHORE && semaphore_passed(signaller) < 0)
		return -1;

	return i915_seqno_passed(signaller->get_seqno(signaller, false), seqno);
}

static void semaphore_clear_deadlocks(struct drm_i915_private *dev_priv)
{
	struct intel_ring_buffer *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		ring->hangcheck.deadlock = false;
}

static enum intel_ring_hangcheck_action
ring_stuck(struct intel_ring_buffer *ring, u32 acthd)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp;

	if (ring->hangcheck.acthd != acthd)
		return HANGCHECK_ACTIVE;

	if (IS_GEN2(dev))
		return HANGCHECK_HUNG;

	/* Is the chip hanging on a WAIT_FOR_EVENT?
	 * If so we can simply poke the RB_WAIT bit
	 * and break the hang. This should work on
	 * all but the second generation chipsets.
	 */
	tmp = I915_READ_CTL(ring);
	if (tmp & RING_WAIT) {
		DRM_ERROR("Kicking stuck wait on %s\n",
			  ring->name);
		I915_WRITE_CTL(ring, tmp);
		return HANGCHECK_KICK;
	}

	if (INTEL_INFO(dev)->gen >= 6 && tmp & RING_WAIT_SEMAPHORE) {
		switch (semaphore_passed(ring)) {
		default:
			return HANGCHECK_HUNG;
		case 1:
			DRM_ERROR("Kicking stuck semaphore on %s\n",
				  ring->name);
			I915_WRITE_CTL(ring, tmp);
			return HANGCHECK_KICK;
		case 0:
			return HANGCHECK_WAIT;
		}
	}

	return HANGCHECK_HUNG;
}

/**
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. We keep track per ring seqno progress and
 * if there are no progress, hangcheck score for that ring is increased.
 * Further, acthd is inspected to see if the ring is stuck. On stuck case
 * we kick the ring. If we see no progress on three subsequent calls
 * we assume chip is wedged and try to fix it by resetting the chip.
 */
static void i915_hangcheck_elapsed(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int i;
	int busy_count = 0, rings_hung = 0;
	bool stuck[I915_NUM_RINGS] = { 0 };
#define BUSY 1
#define KICK 5
#define HUNG 20
#define FIRE 30

	if (!i915_enable_hangcheck)
		return;

	for_each_ring(ring, dev_priv, i) {
		u32 seqno, acthd;
		bool busy = true;

		semaphore_clear_deadlocks(dev_priv);

		seqno = ring->get_seqno(ring, false);
		acthd = intel_ring_get_active_head(ring);

		if (ring->hangcheck.seqno == seqno) {
			if (ring_idle(ring, seqno)) {
				ring->hangcheck.action = HANGCHECK_IDLE;

				if (waitqueue_active(&ring->irq_queue)) {
					/* Issue a wake-up to catch stuck h/w. */
					DRM_ERROR("Hangcheck timer elapsed... %s idle\n",
						  ring->name);
					wake_up_all(&ring->irq_queue);
					ring->hangcheck.score += HUNG;
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
					break;
				case HANGCHECK_ACTIVE:
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
		}

		ring->hangcheck.seqno = seqno;
		ring->hangcheck.acthd = acthd;
		busy_count += busy;
	}

	for_each_ring(ring, dev_priv, i) {
		if (ring->hangcheck.score > FIRE) {
			DRM_INFO("%s on %s\n",
				 stuck[i] ? "stuck" : "no progress",
				 ring->name);
			rings_hung++;
		}
	}

	if (rings_hung)
		return i915_handle_error(dev, true);

	if (busy_count)
		/* Reset timer case chip hangs without another request
		 * being added */
		i915_queue_hangcheck(dev);
}

void i915_queue_hangcheck(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (!i915_enable_hangcheck)
		return;

	mod_timer(&dev_priv->gpu_error.hangcheck_timer,
		  round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES));
}

static void ibx_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_PCH_NOP(dev))
		return;

	/* south display irq */
	I915_WRITE(SDEIMR, 0xffffffff);
	/*
	 * SDEIER is also touched by the interrupt handler to work around missed
	 * PCH interrupts. Hence we can't update it after the interrupt handler
	 * is enabled - instead we unconditionally enable all PCH interrupt
	 * sources here, but then only unmask them as needed with SDEIMR.
	 */
	I915_WRITE(SDEIER, 0xffffffff);
	POSTING_READ(SDEIER);
}

static void gen5_gt_irq_preinstall(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* and GT */
	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	POSTING_READ(GTIER);

	if (INTEL_INFO(dev)->gen >= 6) {
		/* and PM */
		I915_WRITE(GEN6_PMIMR, 0xffffffff);
		I915_WRITE(GEN6_PMIER, 0x0);
		POSTING_READ(GEN6_PMIER);
	}
}

/* drm_dma.h hooks
*/
static void ironlake_irq_preinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	atomic_set(&dev_priv->irq_received, 0);

	I915_WRITE(HWSTAM, 0xeffe);

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	POSTING_READ(DEIER);

	gen5_gt_irq_preinstall(dev);

	ibx_irq_preinstall(dev);
}

static void valleyview_irq_preinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	atomic_set(&dev_priv->irq_received, 0);

	/* VLV magic */
	I915_WRITE(VLV_IMR, 0);
	I915_WRITE(RING_IMR(RENDER_RING_BASE), 0);
	I915_WRITE(RING_IMR(GEN6_BSD_RING_BASE), 0);
	I915_WRITE(RING_IMR(BLT_RING_BASE), 0);

	/* and GT */
	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIIR, I915_READ(GTIIR));

	gen5_gt_irq_preinstall(dev);

	I915_WRITE(DPINVGTT, 0xff);

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0xffff);
	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IMR, 0xffffffff);
	I915_WRITE(VLV_IER, 0x0);
	POSTING_READ(VLV_IER);
}

static void ibx_hpd_irq_setup(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *intel_encoder;
	u32 hotplug_irqs, hotplug, enabled_irqs = 0;

	if (HAS_PCH_IBX(dev)) {
		hotplug_irqs = SDE_HOTPLUG_MASK;
		list_for_each_entry(intel_encoder, &mode_config->encoder_list, base.head)
			if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
				enabled_irqs |= hpd_ibx[intel_encoder->hpd_pin];
	} else {
		hotplug_irqs = SDE_HOTPLUG_MASK_CPT;
		list_for_each_entry(intel_encoder, &mode_config->encoder_list, base.head)
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
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 mask;

	if (HAS_PCH_NOP(dev))
		return;

	if (HAS_PCH_IBX(dev)) {
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_TRANSB_FIFO_UNDER |
		       SDE_TRANSA_FIFO_UNDER | SDE_POISON;
	} else {
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT | SDE_ERROR_CPT;

		I915_WRITE(SERR_INT, I915_READ(SERR_INT));
	}

	I915_WRITE(SDEIIR, I915_READ(SDEIIR));
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

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
	I915_WRITE(GTIER, gt_irqs);
	POSTING_READ(GTIER);

	if (INTEL_INFO(dev)->gen >= 6) {
		pm_irqs |= GEN6_PM_RPS_EVENTS;

		if (HAS_VEBOX(dev))
			pm_irqs |= PM_VEBOX_USER_INTERRUPT;

		dev_priv->pm_irq_mask = 0xffffffff;
		I915_WRITE(GEN6_PMIIR, I915_READ(GEN6_PMIIR));
		I915_WRITE(GEN6_PMIMR, dev_priv->pm_irq_mask);
		I915_WRITE(GEN6_PMIER, pm_irqs);
		POSTING_READ(GEN6_PMIER);
	}
}

static int ironlake_irq_postinstall(struct drm_device *dev)
{
	unsigned long irqflags;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 display_mask, extra_mask;

	if (INTEL_INFO(dev)->gen >= 7) {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE_IVB |
				DE_PCH_EVENT_IVB | DE_PLANEC_FLIP_DONE_IVB |
				DE_PLANEB_FLIP_DONE_IVB |
				DE_PLANEA_FLIP_DONE_IVB | DE_AUX_CHANNEL_A_IVB |
				DE_ERR_INT_IVB);
		extra_mask = (DE_PIPEC_VBLANK_IVB | DE_PIPEB_VBLANK_IVB |
			      DE_PIPEA_VBLANK_IVB);

		I915_WRITE(GEN7_ERR_INT, I915_READ(GEN7_ERR_INT));
	} else {
		display_mask = (DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
				DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE |
				DE_AUX_CHANNEL_A | DE_PIPEB_FIFO_UNDERRUN |
				DE_PIPEA_FIFO_UNDERRUN | DE_POISON);
		extra_mask = DE_PIPEA_VBLANK | DE_PIPEB_VBLANK | DE_PCU_EVENT;
	}

	dev_priv->irq_mask = ~display_mask;

	/* should always can generate irq */
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	I915_WRITE(DEIMR, dev_priv->irq_mask);
	I915_WRITE(DEIER, display_mask | extra_mask);
	POSTING_READ(DEIER);

	gen5_gt_irq_postinstall(dev);

	ibx_irq_postinstall(dev);

	if (IS_IRONLAKE_M(dev)) {
		/* Enable PCU event interrupts
		 *
		 * spinlocking not required here for correctness since interrupt
		 * setup is guaranteed to run in single-threaded context. But we
		 * need it to make the assert_spin_locked happy. */
		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		ironlake_enable_display_irq(dev_priv, DE_PCU_EVENT);
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
	}

	return 0;
}

static int valleyview_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 enable_mask;
	u32 pipestat_enable = PLANE_FLIP_DONE_INT_EN_VLV;
	unsigned long irqflags;

	enable_mask = I915_DISPLAY_PORT_INTERRUPT;
	enable_mask |= I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT |
		I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |
		I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT;

	/*
	 *Leave vblank interrupts masked initially.  enable/disable will
	 * toggle them based on usage.
	 */
	dev_priv->irq_mask = (~enable_mask) |
		I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT |
		I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT;

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	POSTING_READ(PORT_HOTPLUG_EN);

	I915_WRITE(VLV_IMR, dev_priv->irq_mask);
	I915_WRITE(VLV_IER, enable_mask);
	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(PIPESTAT(0), 0xffff);
	I915_WRITE(PIPESTAT(1), 0xffff);
	POSTING_READ(VLV_IER);

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked check happy. */
	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, 0, pipestat_enable);
	i915_enable_pipestat(dev_priv, 0, PIPE_GMBUS_EVENT_ENABLE);
	i915_enable_pipestat(dev_priv, 1, pipestat_enable);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IIR, 0xffffffff);

	gen5_gt_irq_postinstall(dev);

	/* ack & enable invalid PTE error interrupts */
#if 0 /* FIXME: add support to irq handler for checking these bits */
	I915_WRITE(DPINVGTT, DPINVGTT_STATUS_MASK);
	I915_WRITE(DPINVGTT, DPINVGTT_EN_MASK);
#endif

	I915_WRITE(VLV_MASTER_IER, MASTER_INTERRUPT_ENABLE);

	return 0;
}

static void valleyview_irq_uninstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	if (!dev_priv)
		return;

	del_timer_sync(&dev_priv->hotplug_reenable_timer);

	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0xffff);

	I915_WRITE(HWSTAM, 0xffffffff);
	I915_WRITE(PORT_HOTPLUG_EN, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0xffff);
	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IMR, 0xffffffff);
	I915_WRITE(VLV_IER, 0x0);
	POSTING_READ(VLV_IER);
}

static void ironlake_irq_uninstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!dev_priv)
		return;

	del_timer_sync(&dev_priv->hotplug_reenable_timer);

	I915_WRITE(HWSTAM, 0xffffffff);

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	if (IS_GEN7(dev))
		I915_WRITE(GEN7_ERR_INT, I915_READ(GEN7_ERR_INT));

	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	I915_WRITE(GTIIR, I915_READ(GTIIR));

	if (HAS_PCH_NOP(dev))
		return;

	I915_WRITE(SDEIMR, 0xffffffff);
	I915_WRITE(SDEIER, 0x0);
	I915_WRITE(SDEIIR, I915_READ(SDEIIR));
	if (HAS_PCH_CPT(dev) || HAS_PCH_LPT(dev))
		I915_WRITE(SERR_INT, I915_READ(SERR_INT));
}

static void i8xx_irq_preinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	atomic_set(&dev_priv->irq_received, 0);

	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE16(IMR, 0xffff);
	I915_WRITE16(IER, 0x0);
	POSTING_READ16(IER);
}

static int i8xx_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

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

	return 0;
}

/*
 * Returns true when a page flip has completed.
 */
static bool i8xx_handle_vblank(struct drm_device *dev,
			       int pipe, u16 iir)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u16 flip_pending = DISPLAY_PLANE_FLIP_PENDING(pipe);

	if (!drm_handle_vblank(dev, pipe))
		return false;

	if ((iir & flip_pending) == 0)
		return false;

	intel_prepare_page_flip(dev, pipe);

	/* We detect FlipDone by looking for the change in PendingFlip from '1'
	 * to '0' on the following vblank, i.e. IIR has the Pendingflip
	 * asserted following the MI_DISPLAY_FLIP, but ISR is deasserted, hence
	 * the flip is completed (no longer pending). Since this doesn't raise
	 * an interrupt per se, we watch for the change at vblank.
	 */
	if (I915_READ16(ISR) & flip_pending)
		return false;

	intel_finish_page_flip(dev, pipe);

	return true;
}

static irqreturn_t i8xx_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u16 iir, new_iir;
	u32 pipe_stats[2];
	unsigned long irqflags;
	int pipe;
	u16 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;

	atomic_inc(&dev_priv->irq_received);

	iir = I915_READ16(IIR);
	if (iir == 0)
		return IRQ_NONE;

	while (iir & ~flip_mask) {
		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			i915_handle_error(dev, false);

		for_each_pipe(pipe) {
			int reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/*
			 * Clear the PIPE*STAT regs before the IIR
			 */
			if (pipe_stats[pipe] & 0x8000ffff) {
				if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
					DRM_DEBUG_DRIVER("pipe %c underrun\n",
							 pipe_name(pipe));
				I915_WRITE(reg, pipe_stats[pipe]);
			}
		}
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

		I915_WRITE16(IIR, iir & ~flip_mask);
		new_iir = I915_READ16(IIR); /* Flush posted writes */

		i915_update_dri1_breadcrumb(dev);

		if (iir & I915_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[RCS]);

		if (pipe_stats[0] & PIPE_VBLANK_INTERRUPT_STATUS &&
		    i8xx_handle_vblank(dev, 0, iir))
			flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(0);

		if (pipe_stats[1] & PIPE_VBLANK_INTERRUPT_STATUS &&
		    i8xx_handle_vblank(dev, 1, iir))
			flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(1);

		iir = new_iir;
	}

	return IRQ_HANDLED;
}

static void i8xx_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	for_each_pipe(pipe) {
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
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	atomic_set(&dev_priv->irq_received, 0);

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE16(HWSTAM, 0xeffe);
	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	POSTING_READ(IER);
}

static int i915_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
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

	return 0;
}

/*
 * Returns true when a page flip has completed.
 */
static bool i915_handle_vblank(struct drm_device *dev,
			       int plane, int pipe, u32 iir)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 flip_pending = DISPLAY_PLANE_FLIP_PENDING(plane);

	if (!drm_handle_vblank(dev, pipe))
		return false;

	if ((iir & flip_pending) == 0)
		return false;

	intel_prepare_page_flip(dev, plane);

	/* We detect FlipDone by looking for the change in PendingFlip from '1'
	 * to '0' on the following vblank, i.e. IIR has the Pendingflip
	 * asserted following the MI_DISPLAY_FLIP, but ISR is deasserted, hence
	 * the flip is completed (no longer pending). Since this doesn't raise
	 * an interrupt per se, we watch for the change at vblank.
	 */
	if (I915_READ(ISR) & flip_pending)
		return false;

	intel_finish_page_flip(dev, pipe);

	return true;
}

static irqreturn_t i915_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 iir, new_iir, pipe_stats[I915_MAX_PIPES];
	unsigned long irqflags;
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;
	int pipe, ret = IRQ_NONE;

	atomic_inc(&dev_priv->irq_received);

	iir = I915_READ(IIR);
	do {
		bool irq_received = (iir & ~flip_mask) != 0;
		bool blc_event = false;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			i915_handle_error(dev, false);

		for_each_pipe(pipe) {
			int reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/* Clear the PIPE*STAT regs before the IIR */
			if (pipe_stats[pipe] & 0x8000ffff) {
				if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
					DRM_DEBUG_DRIVER("pipe %c underrun\n",
							 pipe_name(pipe));
				I915_WRITE(reg, pipe_stats[pipe]);
				irq_received = true;
			}
		}
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

		if (!irq_received)
			break;

		/* Consume port.  Then clear IIR or we'll miss events */
		if ((I915_HAS_HOTPLUG(dev)) &&
		    (iir & I915_DISPLAY_PORT_INTERRUPT)) {
			u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);
			u32 hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
				  hotplug_status);

			intel_hpd_irq_handler(dev, hotplug_trigger, hpd_status_i915);

			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			POSTING_READ(PORT_HOTPLUG_STAT);
		}

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[RCS]);

		for_each_pipe(pipe) {
			int plane = pipe;
			if (IS_MOBILE(dev))
				plane = !plane;

			if (pipe_stats[pipe] & PIPE_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev, plane, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(plane);

			if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
				blc_event = true;
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

	i915_update_dri1_breadcrumb(dev);

	return ret;
}

static void i915_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	del_timer_sync(&dev_priv->hotplug_reenable_timer);

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE16(HWSTAM, 0xffff);
	for_each_pipe(pipe) {
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
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	atomic_set(&dev_priv->irq_received, 0);

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	I915_WRITE(HWSTAM, 0xeffe);
	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	POSTING_READ(IER);
}

static int i965_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 enable_mask;
	u32 error_mask;
	unsigned long irqflags;

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
	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	i915_enable_pipestat(dev_priv, 0, PIPE_GMBUS_EVENT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

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
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *intel_encoder;
	u32 hotplug_en;

	assert_spin_locked(&dev_priv->irq_lock);

	if (I915_HAS_HOTPLUG(dev)) {
		hotplug_en = I915_READ(PORT_HOTPLUG_EN);
		hotplug_en &= ~HOTPLUG_INT_EN_MASK;
		/* Note HDMI and DP share hotplug bits */
		/* enable bits are the same for all generations */
		list_for_each_entry(intel_encoder, &mode_config->encoder_list, base.head)
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
}

static irqreturn_t i965_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 iir, new_iir;
	u32 pipe_stats[I915_MAX_PIPES];
	unsigned long irqflags;
	int irq_received;
	int ret = IRQ_NONE, pipe;
	u32 flip_mask =
		I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |
		I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT;

	atomic_inc(&dev_priv->irq_received);

	iir = I915_READ(IIR);

	for (;;) {
		bool blc_event = false;

		irq_received = (iir & ~flip_mask) != 0;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			i915_handle_error(dev, false);

		for_each_pipe(pipe) {
			int reg = PIPESTAT(pipe);
			pipe_stats[pipe] = I915_READ(reg);

			/*
			 * Clear the PIPE*STAT regs before the IIR
			 */
			if (pipe_stats[pipe] & 0x8000ffff) {
				if (pipe_stats[pipe] & PIPE_FIFO_UNDERRUN_STATUS)
					DRM_DEBUG_DRIVER("pipe %c underrun\n",
							 pipe_name(pipe));
				I915_WRITE(reg, pipe_stats[pipe]);
				irq_received = 1;
			}
		}
		spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

		if (!irq_received)
			break;

		ret = IRQ_HANDLED;

		/* Consume port.  Then clear IIR or we'll miss events */
		if (iir & I915_DISPLAY_PORT_INTERRUPT) {
			u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);
			u32 hotplug_trigger = hotplug_status & (IS_G4X(dev) ?
								  HOTPLUG_INT_STATUS_G4X :
								  HOTPLUG_INT_STATUS_I915);

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
				  hotplug_status);

			intel_hpd_irq_handler(dev, hotplug_trigger,
					      IS_G4X(dev) ? hpd_status_gen4 : hpd_status_i915);

			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			I915_READ(PORT_HOTPLUG_STAT);
		}

		I915_WRITE(IIR, iir & ~flip_mask);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (iir & I915_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[RCS]);
		if (iir & I915_BSD_USER_INTERRUPT)
			notify_ring(dev, &dev_priv->ring[VCS]);

		for_each_pipe(pipe) {
			if (pipe_stats[pipe] & PIPE_START_VBLANK_INTERRUPT_STATUS &&
			    i915_handle_vblank(dev, pipe, pipe, iir))
				flip_mask &= ~DISPLAY_PLANE_FLIP_PENDING(pipe);

			if (pipe_stats[pipe] & PIPE_LEGACY_BLC_EVENT_STATUS)
				blc_event = true;
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

	i915_update_dri1_breadcrumb(dev);

	return ret;
}

static void i965_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int pipe;

	if (!dev_priv)
		return;

	del_timer_sync(&dev_priv->hotplug_reenable_timer);

	I915_WRITE(PORT_HOTPLUG_EN, 0);
	I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));

	I915_WRITE(HWSTAM, 0xffffffff);
	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe), 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	for_each_pipe(pipe)
		I915_WRITE(PIPESTAT(pipe),
			   I915_READ(PIPESTAT(pipe)) & 0x8000ffff);
	I915_WRITE(IIR, I915_READ(IIR));
}

static void i915_reenable_hotplug_timer_func(unsigned long data)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *)data;
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	unsigned long irqflags;
	int i;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
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
							 drm_get_connector_name(connector));
				connector->polled = intel_connector->polled;
				if (!connector->polled)
					connector->polled = DRM_CONNECTOR_POLL_HPD;
			}
		}
	}
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

void intel_irq_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	INIT_WORK(&dev_priv->hotplug_work, i915_hotplug_work_func);
	INIT_WORK(&dev_priv->gpu_error.work, i915_error_work_func);
	INIT_WORK(&dev_priv->rps.work, gen6_pm_rps_work);
	INIT_WORK(&dev_priv->l3_parity.error_work, ivybridge_parity_work);

	setup_timer(&dev_priv->gpu_error.hangcheck_timer,
		    i915_hangcheck_elapsed,
		    (unsigned long) dev);
	setup_timer(&dev_priv->hotplug_reenable_timer, i915_reenable_hotplug_timer_func,
		    (unsigned long) dev_priv);

	pm_qos_add_request(&dev_priv->pm_qos, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

	dev->driver->get_vblank_counter = i915_get_vblank_counter;
	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */
	if (IS_G4X(dev) || INTEL_INFO(dev)->gen >= 5) {
		dev->max_vblank_count = 0xffffffff; /* full 32 bit counter */
		dev->driver->get_vblank_counter = gm45_get_vblank_counter;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		dev->driver->get_vblank_timestamp = i915_get_vblank_timestamp;
	else
		dev->driver->get_vblank_timestamp = NULL;
	dev->driver->get_scanout_position = i915_get_crtc_scanoutpos;

	if (IS_VALLEYVIEW(dev)) {
		dev->driver->irq_handler = valleyview_irq_handler;
		dev->driver->irq_preinstall = valleyview_irq_preinstall;
		dev->driver->irq_postinstall = valleyview_irq_postinstall;
		dev->driver->irq_uninstall = valleyview_irq_uninstall;
		dev->driver->enable_vblank = valleyview_enable_vblank;
		dev->driver->disable_vblank = valleyview_disable_vblank;
		dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
	} else if (HAS_PCH_SPLIT(dev)) {
		dev->driver->irq_handler = ironlake_irq_handler;
		dev->driver->irq_preinstall = ironlake_irq_preinstall;
		dev->driver->irq_postinstall = ironlake_irq_postinstall;
		dev->driver->irq_uninstall = ironlake_irq_uninstall;
		dev->driver->enable_vblank = ironlake_enable_vblank;
		dev->driver->disable_vblank = ironlake_disable_vblank;
		dev_priv->display.hpd_irq_setup = ibx_hpd_irq_setup;
	} else {
		if (INTEL_INFO(dev)->gen == 2) {
			dev->driver->irq_preinstall = i8xx_irq_preinstall;
			dev->driver->irq_postinstall = i8xx_irq_postinstall;
			dev->driver->irq_handler = i8xx_irq_handler;
			dev->driver->irq_uninstall = i8xx_irq_uninstall;
		} else if (INTEL_INFO(dev)->gen == 3) {
			dev->driver->irq_preinstall = i915_irq_preinstall;
			dev->driver->irq_postinstall = i915_irq_postinstall;
			dev->driver->irq_uninstall = i915_irq_uninstall;
			dev->driver->irq_handler = i915_irq_handler;
			dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
		} else {
			dev->driver->irq_preinstall = i965_irq_preinstall;
			dev->driver->irq_postinstall = i965_irq_postinstall;
			dev->driver->irq_uninstall = i965_irq_uninstall;
			dev->driver->irq_handler = i965_irq_handler;
			dev_priv->display.hpd_irq_setup = i915_hpd_irq_setup;
		}
		dev->driver->enable_vblank = i915_enable_vblank;
		dev->driver->disable_vblank = i915_disable_vblank;
	}
}

void intel_hpd_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	unsigned long irqflags;
	int i;

	for (i = 1; i < HPD_NUM_PINS; i++) {
		dev_priv->hpd_stats[i].hpd_cnt = 0;
		dev_priv->hpd_stats[i].hpd_mark = HPD_ENABLED;
	}
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct intel_connector *intel_connector = to_intel_connector(connector);
		connector->polled = intel_connector->polled;
		if (!connector->polled && I915_HAS_HOTPLUG(dev) && intel_connector->encoder->hpd_pin > HPD_NONE)
			connector->polled = DRM_CONNECTOR_POLL_HPD;
	}

	/* Interrupt setup is already guaranteed to be single-threaded, this is
	 * just to make the assert_spin_locked checks happy. */
	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

/* Disable interrupts so we can allow Package C8+. */
void hsw_pc8_disable_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	dev_priv->pc8.regsave.deimr = I915_READ(DEIMR);
	dev_priv->pc8.regsave.sdeimr = I915_READ(SDEIMR);
	dev_priv->pc8.regsave.gtimr = I915_READ(GTIMR);
	dev_priv->pc8.regsave.gtier = I915_READ(GTIER);
	dev_priv->pc8.regsave.gen6_pmimr = I915_READ(GEN6_PMIMR);

	ironlake_disable_display_irq(dev_priv, ~DE_PCH_EVENT_IVB);
	ibx_disable_display_interrupt(dev_priv, ~SDE_HOTPLUG_MASK_CPT);
	ilk_disable_gt_irq(dev_priv, 0xffffffff);
	snb_disable_pm_irq(dev_priv, 0xffffffff);

	dev_priv->pc8.irqs_disabled = true;

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

/* Restore interrupts so we can recover from Package C8+. */
void hsw_pc8_restore_interrupts(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long irqflags;
	uint32_t val, expected;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	val = I915_READ(DEIMR);
	expected = ~DE_PCH_EVENT_IVB;
	WARN(val != expected, "DEIMR is 0x%08x, not 0x%08x\n", val, expected);

	val = I915_READ(SDEIMR) & ~SDE_HOTPLUG_MASK_CPT;
	expected = ~SDE_HOTPLUG_MASK_CPT;
	WARN(val != expected, "SDEIMR non-HPD bits are 0x%08x, not 0x%08x\n",
	     val, expected);

	val = I915_READ(GTIMR);
	expected = 0xffffffff;
	WARN(val != expected, "GTIMR is 0x%08x, not 0x%08x\n", val, expected);

	val = I915_READ(GEN6_PMIMR);
	expected = 0xffffffff;
	WARN(val != expected, "GEN6_PMIMR is 0x%08x, not 0x%08x\n", val,
	     expected);

	dev_priv->pc8.irqs_disabled = false;

	ironlake_enable_display_irq(dev_priv, ~dev_priv->pc8.regsave.deimr);
	ibx_enable_display_interrupt(dev_priv,
				     ~dev_priv->pc8.regsave.sdeimr &
				     ~SDE_HOTPLUG_MASK_CPT);
	ilk_enable_gt_irq(dev_priv, ~dev_priv->pc8.regsave.gtimr);
	snb_enable_pm_irq(dev_priv, ~dev_priv->pc8.regsave.gen6_pmimr);
	I915_WRITE(GTIER, dev_priv->pc8.regsave.gtier);

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}
