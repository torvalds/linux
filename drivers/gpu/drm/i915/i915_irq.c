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

static const u32 hpd_status_i965[] = {
	 [HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	 [HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I965,
	 [HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I965,
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

static void ibx_hpd_irq_setup(struct drm_device *dev);
static void i915_hpd_irq_setup(struct drm_device *dev);

/* For display hotplug interrupt */
static void
ironlake_enable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask & mask) != 0) {
		dev_priv->irq_mask &= ~mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask);
		POSTING_READ(DEIMR);
	}
}

static void
ironlake_disable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask & mask) != mask) {
		dev_priv->irq_mask |= mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask);
		POSTING_READ(DEIMR);
	}
}

static bool ivb_can_enable_err_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc;
	enum pipe pipe;

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
						  bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (enable) {
		if (!ivb_can_enable_err_int(dev))
			return;

		I915_WRITE(GEN7_ERR_INT, ERR_INT_FIFO_UNDERRUN_A |
					 ERR_INT_FIFO_UNDERRUN_B |
					 ERR_INT_FIFO_UNDERRUN_C);

		ironlake_enable_display_irq(dev_priv, DE_ERR_INT_IVB);
	} else {
		ironlake_disable_display_irq(dev_priv, DE_ERR_INT_IVB);
	}
}

static void ibx_set_fifo_underrun_reporting(struct intel_crtc *crtc,
					    bool enable)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t bit = (crtc->pipe == PIPE_A) ? SDE_TRANSA_FIFO_UNDER :
						SDE_TRANSB_FIFO_UNDER;

	if (enable)
		I915_WRITE(SDEIMR, I915_READ(SDEIMR) & ~bit);
	else
		I915_WRITE(SDEIMR, I915_READ(SDEIMR) | bit);

	POSTING_READ(SDEIMR);
}

static void cpt_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum transcoder pch_transcoder,
					    bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (enable) {
		if (!cpt_can_enable_serr_int(dev))
			return;

		I915_WRITE(SERR_INT, SERR_INT_TRANS_A_FIFO_UNDERRUN |
				     SERR_INT_TRANS_B_FIFO_UNDERRUN |
				     SERR_INT_TRANS_C_FIFO_UNDERRUN);

		I915_WRITE(SDEIMR, I915_READ(SDEIMR) & ~SDE_ERROR_CPT);
	} else {
		I915_WRITE(SDEIMR, I915_READ(SDEIMR) | SDE_ERROR_CPT);
	}

	POSTING_READ(SDEIMR);
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
		ivybridge_set_fifo_underrun_reporting(dev, enable);

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
	enum pipe p;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;
	unsigned long flags;
	bool ret;

	if (HAS_PCH_LPT(dev)) {
		crtc = NULL;
		for_each_pipe(p) {
			struct drm_crtc *c = dev_priv->pipe_to_crtc_mapping[p];
			if (intel_pipe_has_type(c, INTEL_OUTPUT_ANALOG)) {
				crtc = c;
				break;
			}
		}
		if (!crtc) {
			DRM_ERROR("PCH FIFO underrun, but no CRTC using the PCH found\n");
			return false;
		}
	} else {
		crtc = dev_priv->pipe_to_crtc_mapping[pch_transcoder];
	}
	intel_crtc = to_intel_crtc(crtc);

	spin_lock_irqsave(&dev_priv->irq_lock, flags);

	ret = !intel_crtc->pch_fifo_underrun_disabled;

	if (enable == ret)
		goto done;

	intel_crtc->pch_fifo_underrun_disabled = !enable;

	if (HAS_PCH_IBX(dev))
		ibx_set_fifo_underrun_reporting(intel_crtc, enable);
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
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);

	if (!intel_display_power_enabled(dev,
		POWER_DOMAIN_TRANSCODER(cpu_transcoder)))
		return false;

	return I915_READ(PIPECONF(cpu_transcoder)) & PIPECONF_ENABLE;
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

static int intel_hpd_irq_event(struct drm_device *dev, struct drm_connector *connector)
{
	enum drm_connector_status old_status;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));
	old_status = connector->status;

	connector->status = connector->funcs->detect(connector, false);
	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
		      connector->base.id,
		      drm_get_connector_name(connector),
		      old_status, connector->status);
	return (old_status != connector->status);
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

static void ironlake_handle_rps_change(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 busy_up, busy_down, max_avg, min_avg;
	u8 new_delay;
	unsigned long flags;

	spin_lock_irqsave(&mchdev_lock, flags);

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

	spin_unlock_irqrestore(&mchdev_lock, flags);

	return;
}

static void notify_ring(struct drm_device *dev,
			struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (ring->obj == NULL)
		return;

	trace_i915_gem_request_complete(ring, ring->get_seqno(ring, false));

	wake_up_all(&ring->irq_queue);
	if (i915_enable_hangcheck) {
		dev_priv->gpu_error.hangcheck_count = 0;
		mod_timer(&dev_priv->gpu_error.hangcheck_timer,
			  round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES));
	}
}

static void gen6_pm_rps_work(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    rps.work);
	u32 pm_iir, pm_imr;
	u8 new_delay;

	spin_lock_irq(&dev_priv->rps.lock);
	pm_iir = dev_priv->rps.pm_iir;
	dev_priv->rps.pm_iir = 0;
	pm_imr = I915_READ(GEN6_PMIMR);
	I915_WRITE(GEN6_PMIMR, 0);
	spin_unlock_irq(&dev_priv->rps.lock);

	if ((pm_iir & GEN6_PM_DEFERRED_EVENTS) == 0)
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	if (pm_iir & GEN6_PM_RP_UP_THRESHOLD)
		new_delay = dev_priv->rps.cur_delay + 1;
	else
		new_delay = dev_priv->rps.cur_delay - 1;

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	if (!(new_delay > dev_priv->rps.max_delay ||
	      new_delay < dev_priv->rps.min_delay)) {
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
	char *parity_event[5];
	uint32_t misccpctl;
	unsigned long flags;

	/* We must turn off DOP level clock gating to access the L3 registers.
	 * In order to prevent a get/put style interface, acquire struct mutex
	 * any time we access those registers.
	 */
	mutex_lock(&dev_priv->dev->struct_mutex);

	misccpctl = I915_READ(GEN7_MISCCPCTL);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);
	POSTING_READ(GEN7_MISCCPCTL);

	error_status = I915_READ(GEN7_L3CDERRST1);
	row = GEN7_PARITY_ERROR_ROW(error_status);
	bank = GEN7_PARITY_ERROR_BANK(error_status);
	subbank = GEN7_PARITY_ERROR_SUBBANK(error_status);

	I915_WRITE(GEN7_L3CDERRST1, GEN7_PARITY_ERROR_VALID |
				    GEN7_L3CDERRST1_ENABLE);
	POSTING_READ(GEN7_L3CDERRST1);

	I915_WRITE(GEN7_MISCCPCTL, misccpctl);

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	dev_priv->gt_irq_mask &= ~GT_GEN7_L3_PARITY_ERROR_INTERRUPT;
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	mutex_unlock(&dev_priv->dev->struct_mutex);

	parity_event[0] = "L3_PARITY_ERROR=1";
	parity_event[1] = kasprintf(GFP_KERNEL, "ROW=%d", row);
	parity_event[2] = kasprintf(GFP_KERNEL, "BANK=%d", bank);
	parity_event[3] = kasprintf(GFP_KERNEL, "SUBBANK=%d", subbank);
	parity_event[4] = NULL;

	kobject_uevent_env(&dev_priv->dev->primary->kdev.kobj,
			   KOBJ_CHANGE, parity_event);

	DRM_DEBUG("Parity error: Row = %d, Bank = %d, Sub bank = %d.\n",
		  row, bank, subbank);

	kfree(parity_event[3]);
	kfree(parity_event[2]);
	kfree(parity_event[1]);
}

static void ivybridge_handle_parity_error(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long flags;

	if (!HAS_L3_GPU_CACHE(dev))
		return;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	dev_priv->gt_irq_mask |= GT_GEN7_L3_PARITY_ERROR_INTERRUPT;
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	queue_work(dev_priv->wq, &dev_priv->l3_parity.error_work);
}

static void snb_gt_irq_handler(struct drm_device *dev,
			       struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{

	if (gt_iir & (GEN6_RENDER_USER_INTERRUPT |
		      GEN6_RENDER_PIPE_CONTROL_NOTIFY_INTERRUPT))
		notify_ring(dev, &dev_priv->ring[RCS]);
	if (gt_iir & GEN6_BSD_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[VCS]);
	if (gt_iir & GEN6_BLITTER_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[BCS]);

	if (gt_iir & (GT_GEN6_BLT_CS_ERROR_INTERRUPT |
		      GT_GEN6_BSD_CS_ERROR_INTERRUPT |
		      GT_RENDER_CS_ERROR_INTERRUPT)) {
		DRM_ERROR("GT error interrupt 0x%08x\n", gt_iir);
		i915_handle_error(dev, false);
	}

	if (gt_iir & GT_GEN7_L3_PARITY_ERROR_INTERRUPT)
		ivybridge_handle_parity_error(dev);
}

static void gen6_queue_rps_work(struct drm_i915_private *dev_priv,
				u32 pm_iir)
{
	unsigned long flags;

	/*
	 * IIR bits should never already be set because IMR should
	 * prevent an interrupt from being shown in IIR. The warning
	 * displays a case where we've unsafely cleared
	 * dev_priv->rps.pm_iir. Although missing an interrupt of the same
	 * type is not a problem, it displays a problem in the logic.
	 *
	 * The mask bit in IMR is cleared by dev_priv->rps.work.
	 */

	spin_lock_irqsave(&dev_priv->rps.lock, flags);
	dev_priv->rps.pm_iir |= pm_iir;
	I915_WRITE(GEN6_PMIMR, dev_priv->rps.pm_iir);
	POSTING_READ(GEN6_PMIMR);
	spin_unlock_irqrestore(&dev_priv->rps.lock, flags);

	queue_work(dev_priv->wq, &dev_priv->rps.work);
}

#define HPD_STORM_DETECT_PERIOD 1000
#define HPD_STORM_THRESHOLD 5

static inline bool hotplug_irq_storm_detect(struct drm_device *dev,
					    u32 hotplug_trigger,
					    const u32 *hpd)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned long irqflags;
	int i;
	bool ret = false;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);

	for (i = 1; i < HPD_NUM_PINS; i++) {

		if (!(hpd[i] & hotplug_trigger) ||
		    dev_priv->hpd_stats[i].hpd_mark != HPD_ENABLED)
			continue;

		dev_priv->hpd_event_bits |= (1 << i);
		if (!time_in_range(jiffies, dev_priv->hpd_stats[i].hpd_last_jiffies,
				   dev_priv->hpd_stats[i].hpd_last_jiffies
				   + msecs_to_jiffies(HPD_STORM_DETECT_PERIOD))) {
			dev_priv->hpd_stats[i].hpd_last_jiffies = jiffies;
			dev_priv->hpd_stats[i].hpd_cnt = 0;
		} else if (dev_priv->hpd_stats[i].hpd_cnt > HPD_STORM_THRESHOLD) {
			dev_priv->hpd_stats[i].hpd_mark = HPD_MARK_DISABLED;
			dev_priv->hpd_event_bits &= ~(1 << i);
			DRM_DEBUG_KMS("HPD interrupt storm detected on PIN %d\n", i);
			ret = true;
		} else {
			dev_priv->hpd_stats[i].hpd_cnt++;
		}
	}

	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return ret;
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
			if (hotplug_trigger) {
				if (hotplug_irq_storm_detect(dev, hotplug_trigger, hpd_status_i915))
					i915_hpd_irq_setup(dev);
				queue_work(dev_priv->wq,
					   &dev_priv->hotplug_work);
			}
			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			I915_READ(PORT_HOTPLUG_STAT);
		}

		if (pipe_stats[0] & PIPE_GMBUS_INTERRUPT_STATUS)
			gmbus_irq_handler(dev);

		if (pm_iir & GEN6_PM_DEFERRED_EVENTS)
			gen6_queue_rps_work(dev_priv, pm_iir);

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

	if (hotplug_trigger) {
		if (hotplug_irq_storm_detect(dev, hotplug_trigger, hpd_ibx))
			ibx_hpd_irq_setup(dev);
		queue_work(dev_priv->wq, &dev_priv->hotplug_work);
	}
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

	if (hotplug_trigger) {
		if (hotplug_irq_storm_detect(dev, hotplug_trigger, hpd_cpt))
			ibx_hpd_irq_setup(dev);
		queue_work(dev_priv->wq, &dev_priv->hotplug_work);
	}
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

static irqreturn_t ivybridge_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 de_iir, gt_iir, de_ier, pm_iir, sde_ier = 0;
	irqreturn_t ret = IRQ_NONE;
	int i;

	atomic_inc(&dev_priv->irq_received);

	/* We get interrupts on unclaimed registers, so check for this before we
	 * do any I915_{READ,WRITE}. */
	if (IS_HASWELL(dev) &&
	    (I915_READ_NOTRACE(FPGA_DBG) & FPGA_DBG_RM_NOCLAIM)) {
		DRM_ERROR("Unclaimed register before interrupt\n");
		I915_WRITE_NOTRACE(FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);

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

	/* On Haswell, also mask ERR_INT because we don't want to risk
	 * generating "unclaimed register" interrupts from inside the interrupt
	 * handler. */
	if (IS_HASWELL(dev))
		ironlake_disable_display_irq(dev_priv, DE_ERR_INT_IVB);

	gt_iir = I915_READ(GTIIR);
	if (gt_iir) {
		snb_gt_irq_handler(dev, dev_priv, gt_iir);
		I915_WRITE(GTIIR, gt_iir);
		ret = IRQ_HANDLED;
	}

	de_iir = I915_READ(DEIIR);
	if (de_iir) {
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

		I915_WRITE(DEIIR, de_iir);
		ret = IRQ_HANDLED;
	}

	pm_iir = I915_READ(GEN6_PMIIR);
	if (pm_iir) {
		if (pm_iir & GEN6_PM_DEFERRED_EVENTS)
			gen6_queue_rps_work(dev_priv, pm_iir);
		I915_WRITE(GEN6_PMIIR, pm_iir);
		ret = IRQ_HANDLED;
	}

	if (IS_HASWELL(dev) && ivb_can_enable_err_int(dev))
		ironlake_enable_display_irq(dev_priv, DE_ERR_INT_IVB);

	I915_WRITE(DEIER, de_ier);
	POSTING_READ(DEIER);
	if (!HAS_PCH_NOP(dev)) {
		I915_WRITE(SDEIER, sde_ier);
		POSTING_READ(SDEIER);
	}

	return ret;
}

static void ilk_gt_irq_handler(struct drm_device *dev,
			       struct drm_i915_private *dev_priv,
			       u32 gt_iir)
{
	if (gt_iir & (GT_USER_INTERRUPT | GT_PIPE_NOTIFY))
		notify_ring(dev, &dev_priv->ring[RCS]);
	if (gt_iir & GT_BSD_USER_INTERRUPT)
		notify_ring(dev, &dev_priv->ring[VCS]);
}

static irqreturn_t ironlake_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = IRQ_NONE;
	u32 de_iir, gt_iir, de_ier, pm_iir, sde_ier;

	atomic_inc(&dev_priv->irq_received);

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);
	POSTING_READ(DEIER);

	/* Disable south interrupts. We'll only write to SDEIIR once, so further
	 * interrupts will will be stored on its back queue, and then we'll be
	 * able to process them after we restore SDEIER (as soon as we restore
	 * it, we'll get an interrupt if SDEIIR still has something to process
	 * due to its back queue). */
	sde_ier = I915_READ(SDEIER);
	I915_WRITE(SDEIER, 0);
	POSTING_READ(SDEIER);

	de_iir = I915_READ(DEIIR);
	gt_iir = I915_READ(GTIIR);
	pm_iir = I915_READ(GEN6_PMIIR);

	if (de_iir == 0 && gt_iir == 0 && (!IS_GEN6(dev) || pm_iir == 0))
		goto done;

	ret = IRQ_HANDLED;

	if (IS_GEN5(dev))
		ilk_gt_irq_handler(dev, dev_priv, gt_iir);
	else
		snb_gt_irq_handler(dev, dev_priv, gt_iir);

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

	if (IS_GEN5(dev) &&  de_iir & DE_PCU_EVENT)
		ironlake_handle_rps_change(dev);

	if (IS_GEN6(dev) && pm_iir & GEN6_PM_DEFERRED_EVENTS)
		gen6_queue_rps_work(dev_priv, pm_iir);

	I915_WRITE(GTIIR, gt_iir);
	I915_WRITE(DEIIR, de_iir);
	I915_WRITE(GEN6_PMIIR, pm_iir);

done:
	I915_WRITE(DEIER, de_ier);
	POSTING_READ(DEIER);
	I915_WRITE(SDEIER, sde_ier);
	POSTING_READ(SDEIER);

	return ret;
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
	struct intel_ring_buffer *ring;
	char *error_event[] = { "ERROR=1", NULL };
	char *reset_event[] = { "RESET=1", NULL };
	char *reset_done_event[] = { "ERROR=0", NULL };
	int i, ret;

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

		ret = i915_reset(dev);

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

		for_each_ring(ring, dev_priv, i)
			wake_up_all(&ring->irq_queue);

		intel_display_handle_reset(dev);

		wake_up_all(&dev_priv->gpu_error.reset_queue);
	}
}

/* NB: please notice the memset */
static void i915_get_extra_instdone(struct drm_device *dev,
				    uint32_t *instdone)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	memset(instdone, 0, sizeof(*instdone) * I915_NUM_INSTDONE_REG);

	switch(INTEL_INFO(dev)->gen) {
	case 2:
	case 3:
		instdone[0] = I915_READ(INSTDONE);
		break;
	case 4:
	case 5:
	case 6:
		instdone[0] = I915_READ(INSTDONE_I965);
		instdone[1] = I915_READ(INSTDONE1);
		break;
	default:
		WARN_ONCE(1, "Unsupported platform\n");
	case 7:
		instdone[0] = I915_READ(GEN7_INSTDONE_1);
		instdone[1] = I915_READ(GEN7_SC_INSTDONE);
		instdone[2] = I915_READ(GEN7_SAMPLER_INSTDONE);
		instdone[3] = I915_READ(GEN7_ROW_INSTDONE);
		break;
	}
}

#ifdef CONFIG_DEBUG_FS
static struct drm_i915_error_object *
i915_error_object_create_sized(struct drm_i915_private *dev_priv,
			       struct drm_i915_gem_object *src,
			       const int num_pages)
{
	struct drm_i915_error_object *dst;
	int i;
	u32 reloc_offset;

	if (src == NULL || src->pages == NULL)
		return NULL;

	dst = kmalloc(sizeof(*dst) + num_pages * sizeof(u32 *), GFP_ATOMIC);
	if (dst == NULL)
		return NULL;

	reloc_offset = src->gtt_offset;
	for (i = 0; i < num_pages; i++) {
		unsigned long flags;
		void *d;

		d = kmalloc(PAGE_SIZE, GFP_ATOMIC);
		if (d == NULL)
			goto unwind;

		local_irq_save(flags);
		if (reloc_offset < dev_priv->gtt.mappable_end &&
		    src->has_global_gtt_mapping) {
			void __iomem *s;

			/* Simply ignore tiling or any overlapping fence.
			 * It's part of the error state, and this hopefully
			 * captures what the GPU read.
			 */

			s = io_mapping_map_atomic_wc(dev_priv->gtt.mappable,
						     reloc_offset);
			memcpy_fromio(d, s, PAGE_SIZE);
			io_mapping_unmap_atomic(s);
		} else if (src->stolen) {
			unsigned long offset;

			offset = dev_priv->mm.stolen_base;
			offset += src->stolen->start;
			offset += i << PAGE_SHIFT;

			memcpy_fromio(d, (void __iomem *) offset, PAGE_SIZE);
		} else {
			struct page *page;
			void *s;

			page = i915_gem_object_get_page(src, i);

			drm_clflush_pages(&page, 1);

			s = kmap_atomic(page);
			memcpy(d, s, PAGE_SIZE);
			kunmap_atomic(s);

			drm_clflush_pages(&page, 1);
		}
		local_irq_restore(flags);

		dst->pages[i] = d;

		reloc_offset += PAGE_SIZE;
	}
	dst->page_count = num_pages;
	dst->gtt_offset = src->gtt_offset;

	return dst;

unwind:
	while (i--)
		kfree(dst->pages[i]);
	kfree(dst);
	return NULL;
}
#define i915_error_object_create(dev_priv, src) \
	i915_error_object_create_sized((dev_priv), (src), \
				       (src)->base.size>>PAGE_SHIFT)

static void
i915_error_object_free(struct drm_i915_error_object *obj)
{
	int page;

	if (obj == NULL)
		return;

	for (page = 0; page < obj->page_count; page++)
		kfree(obj->pages[page]);

	kfree(obj);
}

void
i915_error_state_free(struct kref *error_ref)
{
	struct drm_i915_error_state *error = container_of(error_ref,
							  typeof(*error), ref);
	int i;

	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		i915_error_object_free(error->ring[i].batchbuffer);
		i915_error_object_free(error->ring[i].ringbuffer);
		kfree(error->ring[i].requests);
	}

	kfree(error->active_bo);
	kfree(error->overlay);
	kfree(error);
}
static void capture_bo(struct drm_i915_error_buffer *err,
		       struct drm_i915_gem_object *obj)
{
	err->size = obj->base.size;
	err->name = obj->base.name;
	err->rseqno = obj->last_read_seqno;
	err->wseqno = obj->last_write_seqno;
	err->gtt_offset = obj->gtt_offset;
	err->read_domains = obj->base.read_domains;
	err->write_domain = obj->base.write_domain;
	err->fence_reg = obj->fence_reg;
	err->pinned = 0;
	if (obj->pin_count > 0)
		err->pinned = 1;
	if (obj->user_pin_count > 0)
		err->pinned = -1;
	err->tiling = obj->tiling_mode;
	err->dirty = obj->dirty;
	err->purgeable = obj->madv != I915_MADV_WILLNEED;
	err->ring = obj->ring ? obj->ring->id : -1;
	err->cache_level = obj->cache_level;
}

static u32 capture_active_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head)
{
	struct drm_i915_gem_object *obj;
	int i = 0;

	list_for_each_entry(obj, head, mm_list) {
		capture_bo(err++, obj);
		if (++i == count)
			break;
	}

	return i;
}

static u32 capture_pinned_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head)
{
	struct drm_i915_gem_object *obj;
	int i = 0;

	list_for_each_entry(obj, head, gtt_list) {
		if (obj->pin_count == 0)
			continue;

		capture_bo(err++, obj);
		if (++i == count)
			break;
	}

	return i;
}

static void i915_gem_record_fences(struct drm_device *dev,
				   struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 7:
	case 6:
		for (i = 0; i < dev_priv->num_fence_regs; i++)
			error->fence[i] = I915_READ64(FENCE_REG_SANDYBRIDGE_0 + (i * 8));
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			error->fence[i] = I915_READ64(FENCE_REG_965_0 + (i * 8));
		break;
	case 3:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				error->fence[i+8] = I915_READ(FENCE_REG_945_8 + (i * 4));
	case 2:
		for (i = 0; i < 8; i++)
			error->fence[i] = I915_READ(FENCE_REG_830_0 + (i * 4));
		break;

	default:
		BUG();
	}
}

static struct drm_i915_error_object *
i915_error_first_batchbuffer(struct drm_i915_private *dev_priv,
			     struct intel_ring_buffer *ring)
{
	struct drm_i915_gem_object *obj;
	u32 seqno;

	if (!ring->get_seqno)
		return NULL;

	if (HAS_BROKEN_CS_TLB(dev_priv->dev)) {
		u32 acthd = I915_READ(ACTHD);

		if (WARN_ON(ring->id != RCS))
			return NULL;

		obj = ring->private;
		if (acthd >= obj->gtt_offset &&
		    acthd < obj->gtt_offset + obj->base.size)
			return i915_error_object_create(dev_priv, obj);
	}

	seqno = ring->get_seqno(ring, false);
	list_for_each_entry(obj, &dev_priv->mm.active_list, mm_list) {
		if (obj->ring != ring)
			continue;

		if (i915_seqno_passed(seqno, obj->last_read_seqno))
			continue;

		if ((obj->base.read_domains & I915_GEM_DOMAIN_COMMAND) == 0)
			continue;

		/* We need to copy these to an anonymous buffer as the simplest
		 * method to avoid being overwritten by userspace.
		 */
		return i915_error_object_create(dev_priv, obj);
	}

	return NULL;
}

static void i915_record_ring_state(struct drm_device *dev,
				   struct drm_i915_error_state *error,
				   struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 6) {
		error->rc_psmi[ring->id] = I915_READ(ring->mmio_base + 0x50);
		error->fault_reg[ring->id] = I915_READ(RING_FAULT_REG(ring));
		error->semaphore_mboxes[ring->id][0]
			= I915_READ(RING_SYNC_0(ring->mmio_base));
		error->semaphore_mboxes[ring->id][1]
			= I915_READ(RING_SYNC_1(ring->mmio_base));
		error->semaphore_seqno[ring->id][0] = ring->sync_seqno[0];
		error->semaphore_seqno[ring->id][1] = ring->sync_seqno[1];
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		error->faddr[ring->id] = I915_READ(RING_DMA_FADD(ring->mmio_base));
		error->ipeir[ring->id] = I915_READ(RING_IPEIR(ring->mmio_base));
		error->ipehr[ring->id] = I915_READ(RING_IPEHR(ring->mmio_base));
		error->instdone[ring->id] = I915_READ(RING_INSTDONE(ring->mmio_base));
		error->instps[ring->id] = I915_READ(RING_INSTPS(ring->mmio_base));
		if (ring->id == RCS)
			error->bbaddr = I915_READ64(BB_ADDR);
	} else {
		error->faddr[ring->id] = I915_READ(DMA_FADD_I8XX);
		error->ipeir[ring->id] = I915_READ(IPEIR);
		error->ipehr[ring->id] = I915_READ(IPEHR);
		error->instdone[ring->id] = I915_READ(INSTDONE);
	}

	error->waiting[ring->id] = waitqueue_active(&ring->irq_queue);
	error->instpm[ring->id] = I915_READ(RING_INSTPM(ring->mmio_base));
	error->seqno[ring->id] = ring->get_seqno(ring, false);
	error->acthd[ring->id] = intel_ring_get_active_head(ring);
	error->head[ring->id] = I915_READ_HEAD(ring);
	error->tail[ring->id] = I915_READ_TAIL(ring);
	error->ctl[ring->id] = I915_READ_CTL(ring);

	error->cpu_ring_head[ring->id] = ring->head;
	error->cpu_ring_tail[ring->id] = ring->tail;
}


static void i915_gem_record_active_context(struct intel_ring_buffer *ring,
					   struct drm_i915_error_state *error,
					   struct drm_i915_error_ring *ering)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* Currently render ring is the only HW context user */
	if (ring->id != RCS || !error->ccid)
		return;

	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list) {
		if ((error->ccid & PAGE_MASK) == obj->gtt_offset) {
			ering->ctx = i915_error_object_create_sized(dev_priv,
								    obj, 1);
		}
	}
}

static void i915_gem_record_rings(struct drm_device *dev,
				  struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	struct drm_i915_gem_request *request;
	int i, count;

	for_each_ring(ring, dev_priv, i) {
		i915_record_ring_state(dev, error, ring);

		error->ring[i].batchbuffer =
			i915_error_first_batchbuffer(dev_priv, ring);

		error->ring[i].ringbuffer =
			i915_error_object_create(dev_priv, ring->obj);


		i915_gem_record_active_context(ring, error, &error->ring[i]);

		count = 0;
		list_for_each_entry(request, &ring->request_list, list)
			count++;

		error->ring[i].num_requests = count;
		error->ring[i].requests =
			kmalloc(count*sizeof(struct drm_i915_error_request),
				GFP_ATOMIC);
		if (error->ring[i].requests == NULL) {
			error->ring[i].num_requests = 0;
			continue;
		}

		count = 0;
		list_for_each_entry(request, &ring->request_list, list) {
			struct drm_i915_error_request *erq;

			erq = &error->ring[i].requests[count++];
			erq->seqno = request->seqno;
			erq->jiffies = request->emitted_jiffies;
			erq->tail = request->tail;
		}
	}
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @dev: drm device
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
static void i915_capture_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct drm_i915_error_state *error;
	unsigned long flags;
	int i, pipe;

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	error = dev_priv->gpu_error.first_error;
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);
	if (error)
		return;

	/* Account for pipe specific data like PIPE*STAT */
	error = kzalloc(sizeof(*error), GFP_ATOMIC);
	if (!error) {
		DRM_DEBUG_DRIVER("out of memory, not capturing error state\n");
		return;
	}

	DRM_INFO("capturing error event; look for more information in "
		 "/sys/kernel/debug/dri/%d/i915_error_state\n",
		 dev->primary->index);

	kref_init(&error->ref);
	error->eir = I915_READ(EIR);
	error->pgtbl_er = I915_READ(PGTBL_ER);
	if (HAS_HW_CONTEXTS(dev))
		error->ccid = I915_READ(CCID);

	if (HAS_PCH_SPLIT(dev))
		error->ier = I915_READ(DEIER) | I915_READ(GTIER);
	else if (IS_VALLEYVIEW(dev))
		error->ier = I915_READ(GTIER) | I915_READ(VLV_IER);
	else if (IS_GEN2(dev))
		error->ier = I915_READ16(IER);
	else
		error->ier = I915_READ(IER);

	if (INTEL_INFO(dev)->gen >= 6)
		error->derrmr = I915_READ(DERRMR);

	if (IS_VALLEYVIEW(dev))
		error->forcewake = I915_READ(FORCEWAKE_VLV);
	else if (INTEL_INFO(dev)->gen >= 7)
		error->forcewake = I915_READ(FORCEWAKE_MT);
	else if (INTEL_INFO(dev)->gen == 6)
		error->forcewake = I915_READ(FORCEWAKE);

	if (!HAS_PCH_SPLIT(dev))
		for_each_pipe(pipe)
			error->pipestat[pipe] = I915_READ(PIPESTAT(pipe));

	if (INTEL_INFO(dev)->gen >= 6) {
		error->error = I915_READ(ERROR_GEN6);
		error->done_reg = I915_READ(DONE_REG);
	}

	if (INTEL_INFO(dev)->gen == 7)
		error->err_int = I915_READ(GEN7_ERR_INT);

	i915_get_extra_instdone(dev, error->extra_instdone);

	i915_gem_record_fences(dev, error);
	i915_gem_record_rings(dev, error);

	/* Record buffers on the active and pinned lists. */
	error->active_bo = NULL;
	error->pinned_bo = NULL;

	i = 0;
	list_for_each_entry(obj, &dev_priv->mm.active_list, mm_list)
		i++;
	error->active_bo_count = i;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list)
		if (obj->pin_count)
			i++;
	error->pinned_bo_count = i - error->active_bo_count;

	error->active_bo = NULL;
	error->pinned_bo = NULL;
	if (i) {
		error->active_bo = kmalloc(sizeof(*error->active_bo)*i,
					   GFP_ATOMIC);
		if (error->active_bo)
			error->pinned_bo =
				error->active_bo + error->active_bo_count;
	}

	if (error->active_bo)
		error->active_bo_count =
			capture_active_bo(error->active_bo,
					  error->active_bo_count,
					  &dev_priv->mm.active_list);

	if (error->pinned_bo)
		error->pinned_bo_count =
			capture_pinned_bo(error->pinned_bo,
					  error->pinned_bo_count,
					  &dev_priv->mm.bound_list);

	do_gettimeofday(&error->time);

	error->overlay = intel_overlay_capture_error_state(dev);
	error->display = intel_display_capture_error_state(dev);

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	if (dev_priv->gpu_error.first_error == NULL) {
		dev_priv->gpu_error.first_error = error;
		error = NULL;
	}
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

	if (error)
		i915_error_state_free(&error->ref);
}

void i915_destroy_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	error = dev_priv->gpu_error.first_error;
	dev_priv->gpu_error.first_error = NULL;
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

	if (error)
		kref_put(&error->ref, i915_error_state_free);
}
#else
#define i915_capture_error_state(x)
#endif

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
	struct intel_ring_buffer *ring;
	int i;

	i915_capture_error_state(dev);
	i915_report_and_clear_eir(dev);

	if (wedged) {
		atomic_set_mask(I915_RESET_IN_PROGRESS_FLAG,
				&dev_priv->gpu_error.reset_counter);

		/*
		 * Wakeup waiting processes so that the reset work item
		 * doesn't deadlock trying to grab various locks.
		 */
		for_each_ring(ring, dev_priv, i)
			wake_up_all(&ring->irq_queue);
	}

	queue_work(dev_priv->wq, &dev_priv->gpu_error.work);
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
					obj->gtt_offset;
	} else {
		int dspaddr = DSPADDR(intel_crtc->plane);
		stall_detected = I915_READ(dspaddr) == (obj->gtt_offset +
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

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_enable_display_irq(dev_priv, (pipe == 0) ?
				    DE_PIPEA_VBLANK : DE_PIPEB_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);

	return 0;
}

static int ivybridge_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	if (!i915_pipe_enabled(dev, pipe))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_enable_display_irq(dev_priv,
				    DE_PIPEA_VBLANK_IVB << (5 * pipe));
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

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_disable_display_irq(dev_priv, (pipe == 0) ?
				     DE_PIPEA_VBLANK : DE_PIPEB_VBLANK);
	spin_unlock_irqrestore(&dev_priv->irq_lock, irqflags);
}

static void ivybridge_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->irq_lock, irqflags);
	ironlake_disable_display_irq(dev_priv,
				     DE_PIPEA_VBLANK_IVB << (pipe * 5));
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

static bool i915_hangcheck_ring_idle(struct intel_ring_buffer *ring,
				     u32 ring_seqno, bool *err)
{
	if (list_empty(&ring->request_list) ||
	    i915_seqno_passed(ring_seqno, ring_last_seqno(ring))) {
		/* Issue a wake-up to catch stuck h/w. */
		if (waitqueue_active(&ring->irq_queue)) {
			DRM_ERROR("Hangcheck timer elapsed... %s idle\n",
				  ring->name);
			wake_up_all(&ring->irq_queue);
			*err = true;
		}
		return true;
	}
	return false;
}

static bool semaphore_passed(struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u32 acthd = intel_ring_get_active_head(ring) & HEAD_ADDR;
	struct intel_ring_buffer *signaller;
	u32 cmd, ipehr, acthd_min;

	ipehr = I915_READ(RING_IPEHR(ring->mmio_base));
	if ((ipehr & ~(0x3 << 16)) !=
	    (MI_SEMAPHORE_MBOX | MI_SEMAPHORE_COMPARE | MI_SEMAPHORE_REGISTER))
		return false;

	/* ACTHD is likely pointing to the dword after the actual command,
	 * so scan backwards until we find the MBOX.
	 */
	acthd_min = max((int)acthd - 3 * 4, 0);
	do {
		cmd = ioread32(ring->virtual_start + acthd);
		if (cmd == ipehr)
			break;

		acthd -= 4;
		if (acthd < acthd_min)
			return false;
	} while (1);

	signaller = &dev_priv->ring[(ring->id + (((ipehr >> 17) & 1) + 1)) % 3];
	return i915_seqno_passed(signaller->get_seqno(signaller, false),
				 ioread32(ring->virtual_start+acthd+4)+1);
}

static bool kick_ring(struct intel_ring_buffer *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp = I915_READ_CTL(ring);
	if (tmp & RING_WAIT) {
		DRM_ERROR("Kicking stuck wait on %s\n",
			  ring->name);
		I915_WRITE_CTL(ring, tmp);
		return true;
	}

	if (INTEL_INFO(dev)->gen >= 6 &&
	    tmp & RING_WAIT_SEMAPHORE &&
	    semaphore_passed(ring)) {
		DRM_ERROR("Kicking stuck semaphore on %s\n",
			  ring->name);
		I915_WRITE_CTL(ring, tmp);
		return true;
	}
	return false;
}

static bool i915_hangcheck_hung(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (dev_priv->gpu_error.hangcheck_count++ > 1) {
		bool hung = true;

		DRM_ERROR("Hangcheck timer elapsed... GPU hung\n");
		i915_handle_error(dev, true);

		if (!IS_GEN2(dev)) {
			struct intel_ring_buffer *ring;
			int i;

			/* Is the chip hanging on a WAIT_FOR_EVENT?
			 * If so we can simply poke the RB_WAIT bit
			 * and break the hang. This should work on
			 * all but the second generation chipsets.
			 */
			for_each_ring(ring, dev_priv, i)
				hung &= !kick_ring(ring);
		}

		return hung;
	}

	return false;
}

/**
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. The first time this is called we simply record
 * ACTHD. If ACTHD hasn't changed by the time the hangcheck timer elapses
 * again, we assume the chip is wedged and try to fix it.
 */
void i915_hangcheck_elapsed(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	bool err = false, idle;
	int i;
	u32 seqno[I915_NUM_RINGS];
	bool work_done;

	if (!i915_enable_hangcheck)
		return;

	idle = true;
	for_each_ring(ring, dev_priv, i) {
		seqno[i] = ring->get_seqno(ring, false);
		idle &= i915_hangcheck_ring_idle(ring, seqno[i], &err);
	}

	/* If all work is done then ACTHD clearly hasn't advanced. */
	if (idle) {
		if (err) {
			if (i915_hangcheck_hung(dev))
				return;

			goto repeat;
		}

		dev_priv->gpu_error.hangcheck_count = 0;
		return;
	}

	work_done = false;
	for_each_ring(ring, dev_priv, i) {
		if (ring->hangcheck.seqno != seqno[i]) {
			work_done = true;
			ring->hangcheck.seqno = seqno[i];
		}
	}

	if (!work_done) {
		if (i915_hangcheck_hung(dev))
			return;
	} else {
		dev_priv->gpu_error.hangcheck_count = 0;
	}

repeat:
	/* Reset timer case chip hangs without another request being added */
	mod_timer(&dev_priv->gpu_error.hangcheck_timer,
		  round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES));
}

/* drm_dma.h hooks
*/
static void ironlake_irq_preinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	atomic_set(&dev_priv->irq_received, 0);

	I915_WRITE(HWSTAM, 0xeffe);

	/* XXX hotplug from PCH */

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	POSTING_READ(DEIER);

	/* and GT */
	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	POSTING_READ(GTIER);

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
	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	POSTING_READ(GTIER);

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
	u32 mask = ~I915_READ(SDEIMR);
	u32 hotplug;

	if (HAS_PCH_IBX(dev)) {
		mask &= ~SDE_HOTPLUG_MASK;
		list_for_each_entry(intel_encoder, &mode_config->encoder_list, base.head)
			if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
				mask |= hpd_ibx[intel_encoder->hpd_pin];
	} else {
		mask &= ~SDE_HOTPLUG_MASK_CPT;
		list_for_each_entry(intel_encoder, &mode_config->encoder_list, base.head)
			if (dev_priv->hpd_stats[intel_encoder->hpd_pin].hpd_mark == HPD_ENABLED)
				mask |= hpd_cpt[intel_encoder->hpd_pin];
	}

	I915_WRITE(SDEIMR, ~mask);

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

	if (HAS_PCH_IBX(dev)) {
		mask = SDE_GMBUS | SDE_AUX_MASK | SDE_TRANSB_FIFO_UNDER |
		       SDE_TRANSA_FIFO_UNDER | SDE_POISON;
	} else {
		mask = SDE_GMBUS_CPT | SDE_AUX_MASK_CPT | SDE_ERROR_CPT;

		I915_WRITE(SERR_INT, I915_READ(SERR_INT));
	}

	if (HAS_PCH_NOP(dev))
		return;

	I915_WRITE(SDEIIR, I915_READ(SDEIIR));
	I915_WRITE(SDEIMR, ~mask);
}

static int ironlake_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* enable kind of interrupts always enabled */
	u32 display_mask = DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
			   DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE |
			   DE_AUX_CHANNEL_A | DE_PIPEB_FIFO_UNDERRUN |
			   DE_PIPEA_FIFO_UNDERRUN | DE_POISON;
	u32 render_irqs;

	dev_priv->irq_mask = ~display_mask;

	/* should always can generate irq */
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	I915_WRITE(DEIMR, dev_priv->irq_mask);
	I915_WRITE(DEIER, display_mask | DE_PIPEA_VBLANK | DE_PIPEB_VBLANK);
	POSTING_READ(DEIER);

	dev_priv->gt_irq_mask = ~0;

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);

	if (IS_GEN6(dev))
		render_irqs =
			GT_USER_INTERRUPT |
			GEN6_BSD_USER_INTERRUPT |
			GEN6_BLITTER_USER_INTERRUPT;
	else
		render_irqs =
			GT_USER_INTERRUPT |
			GT_PIPE_NOTIFY |
			GT_BSD_USER_INTERRUPT;
	I915_WRITE(GTIER, render_irqs);
	POSTING_READ(GTIER);

	ibx_irq_postinstall(dev);

	if (IS_IRONLAKE_M(dev)) {
		/* Clear & enable PCU event interrupts */
		I915_WRITE(DEIIR, DE_PCU_EVENT);
		I915_WRITE(DEIER, I915_READ(DEIER) | DE_PCU_EVENT);
		ironlake_enable_display_irq(dev_priv, DE_PCU_EVENT);
	}

	return 0;
}

static int ivybridge_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* enable kind of interrupts always enabled */
	u32 display_mask =
		DE_MASTER_IRQ_CONTROL | DE_GSE_IVB | DE_PCH_EVENT_IVB |
		DE_PLANEC_FLIP_DONE_IVB |
		DE_PLANEB_FLIP_DONE_IVB |
		DE_PLANEA_FLIP_DONE_IVB |
		DE_AUX_CHANNEL_A_IVB |
		DE_ERR_INT_IVB;
	u32 render_irqs;

	dev_priv->irq_mask = ~display_mask;

	/* should always can generate irq */
	I915_WRITE(GEN7_ERR_INT, I915_READ(GEN7_ERR_INT));
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	I915_WRITE(DEIMR, dev_priv->irq_mask);
	I915_WRITE(DEIER,
		   display_mask |
		   DE_PIPEC_VBLANK_IVB |
		   DE_PIPEB_VBLANK_IVB |
		   DE_PIPEA_VBLANK_IVB);
	POSTING_READ(DEIER);

	dev_priv->gt_irq_mask = ~GT_GEN7_L3_PARITY_ERROR_INTERRUPT;

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);

	render_irqs = GT_USER_INTERRUPT | GEN6_BSD_USER_INTERRUPT |
		GEN6_BLITTER_USER_INTERRUPT | GT_GEN7_L3_PARITY_ERROR_INTERRUPT;
	I915_WRITE(GTIER, render_irqs);
	POSTING_READ(GTIER);

	ibx_irq_postinstall(dev);

	return 0;
}

static int valleyview_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 enable_mask;
	u32 pipestat_enable = PLANE_FLIP_DONE_INT_EN_VLV;
	u32 render_irqs;

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

	i915_enable_pipestat(dev_priv, 0, pipestat_enable);
	i915_enable_pipestat(dev_priv, 0, PIPE_GMBUS_EVENT_ENABLE);
	i915_enable_pipestat(dev_priv, 1, pipestat_enable);

	I915_WRITE(VLV_IIR, 0xffffffff);
	I915_WRITE(VLV_IIR, 0xffffffff);

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask);

	render_irqs = GT_USER_INTERRUPT | GEN6_BSD_USER_INTERRUPT |
		GEN6_BLITTER_USER_INTERRUPT;
	I915_WRITE(GTIER, render_irqs);
	POSTING_READ(GTIER);

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
	int irq_received;
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
				irq_received = 1;
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
			if (hotplug_trigger) {
				if (hotplug_irq_storm_detect(dev, hotplug_trigger, hpd_status_i915))
					i915_hpd_irq_setup(dev);
				queue_work(dev_priv->wq,
					   &dev_priv->hotplug_work);
			}
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

	i915_enable_pipestat(dev_priv, 0, PIPE_GMBUS_EVENT_ENABLE);

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
								  HOTPLUG_INT_STATUS_I965);

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
				  hotplug_status);
			if (hotplug_trigger) {
				if (hotplug_irq_storm_detect(dev, hotplug_trigger,
							    IS_G4X(dev) ? hpd_status_gen4 : hpd_status_i965))
					i915_hpd_irq_setup(dev);
				queue_work(dev_priv->wq,
					   &dev_priv->hotplug_work);
			}
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
	} else if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev)) {
		/* Share pre & uninstall handlers with ILK/SNB */
		dev->driver->irq_handler = ivybridge_irq_handler;
		dev->driver->irq_preinstall = ironlake_irq_preinstall;
		dev->driver->irq_postinstall = ivybridge_irq_postinstall;
		dev->driver->irq_uninstall = ironlake_irq_uninstall;
		dev->driver->enable_vblank = ivybridge_enable_vblank;
		dev->driver->disable_vblank = ivybridge_disable_vblank;
		dev_priv->display.hpd_irq_setup = ibx_hpd_irq_setup;
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
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev);
}
