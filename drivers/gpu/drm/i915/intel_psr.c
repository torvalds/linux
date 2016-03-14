/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * DOC: Panel Self Refresh (PSR/SRD)
 *
 * Since Haswell Display controller supports Panel Self-Refresh on display
 * panels witch have a remote frame buffer (RFB) implemented according to PSR
 * spec in eDP1.3. PSR feature allows the display to go to lower standby states
 * when system is idle but display is on as it eliminates display refresh
 * request to DDR memory completely as long as the frame buffer for that
 * display is unchanged.
 *
 * Panel Self Refresh must be supported by both Hardware (source) and
 * Panel (sink).
 *
 * PSR saves power by caching the framebuffer in the panel RFB, which allows us
 * to power down the link and memory controller. For DSI panels the same idea
 * is called "manual mode".
 *
 * The implementation uses the hardware-based PSR support which automatically
 * enters/exits self-refresh mode. The hardware takes care of sending the
 * required DP aux message and could even retrain the link (that part isn't
 * enabled yet though). The hardware also keeps track of any frontbuffer
 * changes to know when to exit self-refresh mode again. Unfortunately that
 * part doesn't work too well, hence why the i915 PSR support uses the
 * software frontbuffer tracking to make sure it doesn't miss a screen
 * update. For this integration intel_psr_invalidate() and intel_psr_flush()
 * get called by the frontbuffer tracking code. Note that because of locking
 * issues the self-refresh re-enable code is done from a work queue, which
 * must be correctly synchronized/cancelled when shutting down the pipe."
 */

#include <drm/drmP.h>

#include "intel_drv.h"
#include "i915_drv.h"

static bool is_edp_psr(struct intel_dp *intel_dp)
{
	return intel_dp->psr_dpcd[0] & DP_PSR_IS_SUPPORTED;
}

static bool vlv_is_psr_active_on_pipe(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val;

	val = I915_READ(VLV_PSRSTAT(pipe)) &
	      VLV_EDP_PSR_CURR_STATE_MASK;
	return (val == VLV_EDP_PSR_ACTIVE_NORFB_UP) ||
	       (val == VLV_EDP_PSR_ACTIVE_SF_UPDATE);
}

static void intel_psr_write_vsc(struct intel_dp *intel_dp,
				const struct edp_vsc_psr *vsc_psr)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	enum transcoder cpu_transcoder = crtc->config->cpu_transcoder;
	i915_reg_t ctl_reg = HSW_TVIDEO_DIP_CTL(cpu_transcoder);
	uint32_t *data = (uint32_t *) vsc_psr;
	unsigned int i;

	/* As per BSPec (Pipe Video Data Island Packet), we need to disable
	   the video DIP being updated before program video DIP data buffer
	   registers for DIP being updated. */
	I915_WRITE(ctl_reg, 0);
	POSTING_READ(ctl_reg);

	for (i = 0; i < sizeof(*vsc_psr); i += 4) {
		I915_WRITE(HSW_TVIDEO_DIP_VSC_DATA(cpu_transcoder,
						   i >> 2), *data);
		data++;
	}
	for (; i < VIDEO_DIP_VSC_DATA_SIZE; i += 4)
		I915_WRITE(HSW_TVIDEO_DIP_VSC_DATA(cpu_transcoder,
						   i >> 2), 0);

	I915_WRITE(ctl_reg, VIDEO_DIP_ENABLE_VSC_HSW);
	POSTING_READ(ctl_reg);
}

static void vlv_psr_setup_vsc(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = intel_dig_port->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	uint32_t val;

	/* VLV auto-generate VSC package as per EDP 1.3 spec, Table 3.10 */
	val  = I915_READ(VLV_VSCSDP(pipe));
	val &= ~VLV_EDP_PSR_SDP_FREQ_MASK;
	val |= VLV_EDP_PSR_SDP_FREQ_EVFRAME;
	I915_WRITE(VLV_VSCSDP(pipe), val);
}

static void skl_psr_setup_su_vsc(struct intel_dp *intel_dp)
{
	struct edp_vsc_psr psr_vsc;

	/* Prepare VSC Header for SU as per EDP 1.4 spec, Table 6.11 */
	memset(&psr_vsc, 0, sizeof(psr_vsc));
	psr_vsc.sdp_header.HB0 = 0;
	psr_vsc.sdp_header.HB1 = 0x7;
	psr_vsc.sdp_header.HB2 = 0x3;
	psr_vsc.sdp_header.HB3 = 0xb;
	intel_psr_write_vsc(intel_dp, &psr_vsc);
}

static void hsw_psr_setup_vsc(struct intel_dp *intel_dp)
{
	struct edp_vsc_psr psr_vsc;

	/* Prepare VSC packet as per EDP 1.3 spec, Table 3.10 */
	memset(&psr_vsc, 0, sizeof(psr_vsc));
	psr_vsc.sdp_header.HB0 = 0;
	psr_vsc.sdp_header.HB1 = 0x7;
	psr_vsc.sdp_header.HB2 = 0x2;
	psr_vsc.sdp_header.HB3 = 0x8;
	intel_psr_write_vsc(intel_dp, &psr_vsc);
}

static void vlv_psr_enable_sink(struct intel_dp *intel_dp)
{
	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG,
			   DP_PSR_ENABLE | DP_PSR_MAIN_LINK_ACTIVE);
}

static i915_reg_t psr_aux_ctl_reg(struct drm_i915_private *dev_priv,
				       enum port port)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return DP_AUX_CH_CTL(port);
	else
		return EDP_PSR_AUX_CTL;
}

static i915_reg_t psr_aux_data_reg(struct drm_i915_private *dev_priv,
					enum port port, int index)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return DP_AUX_CH_DATA(port, index);
	else
		return EDP_PSR_AUX_DATA(index);
}

static void hsw_psr_enable_sink(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t aux_clock_divider;
	i915_reg_t aux_ctl_reg;
	int precharge = 0x3;
	static const uint8_t aux_msg[] = {
		[0] = DP_AUX_NATIVE_WRITE << 4,
		[1] = DP_SET_POWER >> 8,
		[2] = DP_SET_POWER & 0xff,
		[3] = 1 - 1,
		[4] = DP_SET_POWER_D0,
	};
	enum port port = dig_port->port;
	int i;

	BUILD_BUG_ON(sizeof(aux_msg) > 20);

	aux_clock_divider = intel_dp->get_aux_clock_divider(intel_dp, 0);

	/* Enable AUX frame sync at sink */
	if (dev_priv->psr.aux_frame_sync)
		drm_dp_dpcd_writeb(&intel_dp->aux,
				DP_SINK_DEVICE_AUX_FRAME_SYNC_CONF,
				DP_AUX_FRAME_SYNC_ENABLE);

	aux_ctl_reg = psr_aux_ctl_reg(dev_priv, port);

	/* Setup AUX registers */
	for (i = 0; i < sizeof(aux_msg); i += 4)
		I915_WRITE(psr_aux_data_reg(dev_priv, port, i >> 2),
			   intel_dp_pack_aux(&aux_msg[i], sizeof(aux_msg) - i));

	if (INTEL_INFO(dev)->gen >= 9) {
		uint32_t val;

		val = I915_READ(aux_ctl_reg);
		val &= ~DP_AUX_CH_CTL_TIME_OUT_MASK;
		val |= DP_AUX_CH_CTL_TIME_OUT_1600us;
		val &= ~DP_AUX_CH_CTL_MESSAGE_SIZE_MASK;
		val |= (sizeof(aux_msg) << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT);
		/* Use hardcoded data values for PSR, frame sync and GTC */
		val &= ~DP_AUX_CH_CTL_PSR_DATA_AUX_REG_SKL;
		val &= ~DP_AUX_CH_CTL_FS_DATA_AUX_REG_SKL;
		val &= ~DP_AUX_CH_CTL_GTC_DATA_AUX_REG_SKL;
		I915_WRITE(aux_ctl_reg, val);
	} else {
		I915_WRITE(aux_ctl_reg,
		   DP_AUX_CH_CTL_TIME_OUT_400us |
		   (sizeof(aux_msg) << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
		   (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
		   (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT));
	}

	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG, DP_PSR_ENABLE);
}

static void vlv_psr_enable_source(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	/* Transition from PSR_state 0 to PSR_state 1, i.e. PSR Inactive */
	I915_WRITE(VLV_PSRCTL(pipe),
		   VLV_EDP_PSR_MODE_SW_TIMER |
		   VLV_EDP_PSR_SRC_TRANSMITTER_STATE |
		   VLV_EDP_PSR_ENABLE);
}

static void vlv_psr_activate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	/* Let's do the transition from PSR_state 1 to PSR_state 2
	 * that is PSR transition to active - static frame transmission.
	 * Then Hardware is responsible for the transition to PSR_state 3
	 * that is PSR active - no Remote Frame Buffer (RFB) update.
	 */
	I915_WRITE(VLV_PSRCTL(pipe), I915_READ(VLV_PSRCTL(pipe)) |
		   VLV_EDP_PSR_ACTIVE_ENTRY);
}

static void hsw_psr_enable_source(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	uint32_t max_sleep_time = 0x1f;
	/*
	 * Let's respect VBT in case VBT asks a higher idle_frame value.
	 * Let's use 6 as the minimum to cover all known cases including
	 * the off-by-one issue that HW has in some cases. Also there are
	 * cases where sink should be able to train
	 * with the 5 or 6 idle patterns.
	 */
	uint32_t idle_frames = max(6, dev_priv->vbt.psr.idle_frames);
	uint32_t val = 0x0;

	if (IS_HASWELL(dev))
		val |= EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES;

	I915_WRITE(EDP_PSR_CTL, val |
		   max_sleep_time << EDP_PSR_MAX_SLEEP_TIME_SHIFT |
		   idle_frames << EDP_PSR_IDLE_FRAME_SHIFT |
		   EDP_PSR_ENABLE);

	if (dev_priv->psr.psr2_support)
		I915_WRITE(EDP_PSR2_CTL, EDP_PSR2_ENABLE |
				EDP_SU_TRACK_ENABLE | EDP_PSR2_TP2_TIME_100);
}

static bool intel_psr_match_conditions(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	lockdep_assert_held(&dev_priv->psr.lock);
	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	dev_priv->psr.source_ok = false;

	if (IS_HASWELL(dev) && dig_port->port != PORT_A) {
		DRM_DEBUG_KMS("HSW ties PSR to DDI A (eDP)\n");
		return false;
	}

	if (!i915.enable_psr) {
		DRM_DEBUG_KMS("PSR disable by flag\n");
		return false;
	}

	if (IS_HASWELL(dev) &&
	    I915_READ(HSW_STEREO_3D_CTL(intel_crtc->config->cpu_transcoder)) &
		      S3D_ENABLE) {
		DRM_DEBUG_KMS("PSR condition failed: Stereo 3D is Enabled\n");
		return false;
	}

	if (IS_HASWELL(dev) &&
	    intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		DRM_DEBUG_KMS("PSR condition failed: Interlaced is Enabled\n");
		return false;
	}

	if (!IS_VALLEYVIEW(dev) && !IS_CHERRYVIEW(dev) &&
	    ((dev_priv->vbt.psr.full_link) || (dig_port->port != PORT_A))) {
		DRM_DEBUG_KMS("PSR condition failed: Link Standby requested/needed but not supported on this platform\n");
		return false;
	}

	dev_priv->psr.source_ok = true;
	return true;
}

static void intel_psr_activate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE);
	WARN_ON(dev_priv->psr.active);
	lockdep_assert_held(&dev_priv->psr.lock);

	/* Enable/Re-enable PSR on the host */
	if (HAS_DDI(dev))
		/* On HSW+ after we enable PSR on source it will activate it
		 * as soon as it match configure idle_frame count. So
		 * we just actually enable it here on activation time.
		 */
		hsw_psr_enable_source(intel_dp);
	else
		vlv_psr_activate(intel_dp);

	dev_priv->psr.active = true;
}

/**
 * intel_psr_enable - Enable PSR
 * @intel_dp: Intel DP
 *
 * This function can only be called after the pipe is fully trained and enabled.
 */
void intel_psr_enable(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(intel_dig_port->base.base.crtc);

	if (!HAS_PSR(dev)) {
		DRM_DEBUG_KMS("PSR not supported on this platform\n");
		return;
	}

	if (!is_edp_psr(intel_dp)) {
		DRM_DEBUG_KMS("PSR not supported by this panel\n");
		return;
	}

	mutex_lock(&dev_priv->psr.lock);
	if (dev_priv->psr.enabled) {
		DRM_DEBUG_KMS("PSR already in use\n");
		goto unlock;
	}

	if (!intel_psr_match_conditions(intel_dp))
		goto unlock;

	dev_priv->psr.busy_frontbuffer_bits = 0;

	if (HAS_DDI(dev)) {
		hsw_psr_setup_vsc(intel_dp);

		if (dev_priv->psr.psr2_support) {
			/* PSR2 is restricted to work with panel resolutions upto 3200x2000 */
			if (crtc->config->pipe_src_w > 3200 ||
				crtc->config->pipe_src_h > 2000)
				dev_priv->psr.psr2_support = false;
			else
				skl_psr_setup_su_vsc(intel_dp);
		}

		/*
		 * Per Spec: Avoid continuous PSR exit by masking MEMUP and HPD.
		 * Also mask LPSP to avoid dependency on other drivers that
		 * might block runtime_pm besides preventing other hw tracking
		 * issues now we can rely on frontbuffer tracking.
		 */
		I915_WRITE(EDP_PSR_DEBUG_CTL, EDP_PSR_DEBUG_MASK_MEMUP |
			   EDP_PSR_DEBUG_MASK_HPD | EDP_PSR_DEBUG_MASK_LPSP);

		/* Enable PSR on the panel */
		hsw_psr_enable_sink(intel_dp);

		if (INTEL_INFO(dev)->gen >= 9)
			intel_psr_activate(intel_dp);
	} else {
		vlv_psr_setup_vsc(intel_dp);

		/* Enable PSR on the panel */
		vlv_psr_enable_sink(intel_dp);

		/* On HSW+ enable_source also means go to PSR entry/active
		 * state as soon as idle_frame achieved and here would be
		 * to soon. However on VLV enable_source just enable PSR
		 * but let it on inactive state. So we might do this prior
		 * to active transition, i.e. here.
		 */
		vlv_psr_enable_source(intel_dp);
	}

	/*
	 * FIXME: Activation should happen immediately since this function
	 * is just called after pipe is fully trained and enabled.
	 * However on every platform we face issues when first activation
	 * follows a modeset so quickly.
	 *     - On VLV/CHV we get bank screen on first activation
	 *     - On HSW/BDW we get a recoverable frozen screen until next
	 *       exit-activate sequence.
	 */
	if (INTEL_INFO(dev)->gen < 9)
		schedule_delayed_work(&dev_priv->psr.work,
				      msecs_to_jiffies(intel_dp->panel_power_cycle_delay * 5));

	dev_priv->psr.enabled = intel_dp;
unlock:
	mutex_unlock(&dev_priv->psr.lock);
}

static void vlv_psr_disable(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(intel_dig_port->base.base.crtc);
	uint32_t val;

	if (dev_priv->psr.active) {
		/* Put VLV PSR back to PSR_state 0 that is PSR Disabled. */
		if (wait_for((I915_READ(VLV_PSRSTAT(intel_crtc->pipe)) &
			      VLV_EDP_PSR_IN_TRANS) == 0, 1))
			WARN(1, "PSR transition took longer than expected\n");

		val = I915_READ(VLV_PSRCTL(intel_crtc->pipe));
		val &= ~VLV_EDP_PSR_ACTIVE_ENTRY;
		val &= ~VLV_EDP_PSR_ENABLE;
		val &= ~VLV_EDP_PSR_MODE_MASK;
		I915_WRITE(VLV_PSRCTL(intel_crtc->pipe), val);

		dev_priv->psr.active = false;
	} else {
		WARN_ON(vlv_is_psr_active_on_pipe(dev, intel_crtc->pipe));
	}
}

static void hsw_psr_disable(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->psr.active) {
		I915_WRITE(EDP_PSR_CTL,
			   I915_READ(EDP_PSR_CTL) & ~EDP_PSR_ENABLE);

		/* Wait till PSR is idle */
		if (_wait_for((I915_READ(EDP_PSR_STATUS_CTL) &
			       EDP_PSR_STATUS_STATE_MASK) == 0, 2000, 10))
			DRM_ERROR("Timed out waiting for PSR Idle State\n");

		dev_priv->psr.active = false;
	} else {
		WARN_ON(I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE);
	}
}

/**
 * intel_psr_disable - Disable PSR
 * @intel_dp: Intel DP
 *
 * This function needs to be called before disabling pipe.
 */
void intel_psr_disable(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	mutex_lock(&dev_priv->psr.lock);
	if (!dev_priv->psr.enabled) {
		mutex_unlock(&dev_priv->psr.lock);
		return;
	}

	/* Disable PSR on Source */
	if (HAS_DDI(dev))
		hsw_psr_disable(intel_dp);
	else
		vlv_psr_disable(intel_dp);

	/* Disable PSR on Sink */
	drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG, 0);

	dev_priv->psr.enabled = NULL;
	mutex_unlock(&dev_priv->psr.lock);

	cancel_delayed_work_sync(&dev_priv->psr.work);
}

static void intel_psr_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), psr.work.work);
	struct intel_dp *intel_dp = dev_priv->psr.enabled;
	struct drm_crtc *crtc = dp_to_dig_port(intel_dp)->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	/* We have to make sure PSR is ready for re-enable
	 * otherwise it keeps disabled until next full enable/disable cycle.
	 * PSR might take some time to get fully disabled
	 * and be ready for re-enable.
	 */
	if (HAS_DDI(dev_priv->dev)) {
		if (wait_for((I915_READ(EDP_PSR_STATUS_CTL) &
			      EDP_PSR_STATUS_STATE_MASK) == 0, 50)) {
			DRM_ERROR("Timed out waiting for PSR Idle for re-enable\n");
			return;
		}
	} else {
		if (wait_for((I915_READ(VLV_PSRSTAT(pipe)) &
			      VLV_EDP_PSR_IN_TRANS) == 0, 1)) {
			DRM_ERROR("Timed out waiting for PSR Idle for re-enable\n");
			return;
		}
	}
	mutex_lock(&dev_priv->psr.lock);
	intel_dp = dev_priv->psr.enabled;

	if (!intel_dp)
		goto unlock;

	/*
	 * The delayed work can race with an invalidate hence we need to
	 * recheck. Since psr_flush first clears this and then reschedules we
	 * won't ever miss a flush when bailing out here.
	 */
	if (dev_priv->psr.busy_frontbuffer_bits)
		goto unlock;

	intel_psr_activate(intel_dp);
unlock:
	mutex_unlock(&dev_priv->psr.lock);
}

static void intel_psr_exit(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp *intel_dp = dev_priv->psr.enabled;
	struct drm_crtc *crtc = dp_to_dig_port(intel_dp)->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	u32 val;

	if (!dev_priv->psr.active)
		return;

	if (HAS_DDI(dev)) {
		val = I915_READ(EDP_PSR_CTL);

		WARN_ON(!(val & EDP_PSR_ENABLE));

		I915_WRITE(EDP_PSR_CTL, val & ~EDP_PSR_ENABLE);
	} else {
		val = I915_READ(VLV_PSRCTL(pipe));

		/* Here we do the transition from PSR_state 3 to PSR_state 5
		 * directly once PSR State 4 that is active with single frame
		 * update can be skipped. PSR_state 5 that is PSR exit then
		 * Hardware is responsible to transition back to PSR_state 1
		 * that is PSR inactive. Same state after
		 * vlv_edp_psr_enable_source.
		 */
		val &= ~VLV_EDP_PSR_ACTIVE_ENTRY;
		I915_WRITE(VLV_PSRCTL(pipe), val);

		/* Send AUX wake up - Spec says after transitioning to PSR
		 * active we have to send AUX wake up by writing 01h in DPCD
		 * 600h of sink device.
		 * XXX: This might slow down the transition, but without this
		 * HW doesn't complete the transition to PSR_state 1 and we
		 * never get the screen updated.
		 */
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER,
				   DP_SET_POWER_D0);
	}

	dev_priv->psr.active = false;
}

/**
 * intel_psr_single_frame_update - Single Frame Update
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * Some platforms support a single frame update feature that is used to
 * send and update only one frame on Remote Frame Buffer.
 * So far it is only implemented for Valleyview and Cherryview because
 * hardware requires this to be done before a page flip.
 */
void intel_psr_single_frame_update(struct drm_device *dev,
				   unsigned frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	enum pipe pipe;
	u32 val;

	/*
	 * Single frame update is already supported on BDW+ but it requires
	 * many W/A and it isn't really needed.
	 */
	if (!IS_VALLEYVIEW(dev) && !IS_CHERRYVIEW(dev))
		return;

	mutex_lock(&dev_priv->psr.lock);
	if (!dev_priv->psr.enabled) {
		mutex_unlock(&dev_priv->psr.lock);
		return;
	}

	crtc = dp_to_dig_port(dev_priv->psr.enabled)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	if (frontbuffer_bits & INTEL_FRONTBUFFER_ALL_MASK(pipe)) {
		val = I915_READ(VLV_PSRCTL(pipe));

		/*
		 * We need to set this bit before writing registers for a flip.
		 * This bit will be self-clear when it gets to the PSR active state.
		 */
		I915_WRITE(VLV_PSRCTL(pipe), val | VLV_EDP_PSR_SINGLE_FRAME_UPDATE);
	}
	mutex_unlock(&dev_priv->psr.lock);
}

/**
 * intel_psr_invalidate - Invalidade PSR
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * Since the hardware frontbuffer tracking has gaps we need to integrate
 * with the software frontbuffer tracking. This function gets called every
 * time frontbuffer rendering starts and a buffer gets dirtied. PSR must be
 * disabled if the frontbuffer mask contains a buffer relevant to PSR.
 *
 * Dirty frontbuffers relevant to PSR are tracked in busy_frontbuffer_bits."
 */
void intel_psr_invalidate(struct drm_device *dev,
			  unsigned frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	enum pipe pipe;

	mutex_lock(&dev_priv->psr.lock);
	if (!dev_priv->psr.enabled) {
		mutex_unlock(&dev_priv->psr.lock);
		return;
	}

	crtc = dp_to_dig_port(dev_priv->psr.enabled)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->psr.busy_frontbuffer_bits |= frontbuffer_bits;

	if (frontbuffer_bits)
		intel_psr_exit(dev);

	mutex_unlock(&dev_priv->psr.lock);
}

/**
 * intel_psr_flush - Flush PSR
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the flush
 *
 * Since the hardware frontbuffer tracking has gaps we need to integrate
 * with the software frontbuffer tracking. This function gets called every
 * time frontbuffer rendering has completed and flushed out to memory. PSR
 * can be enabled again if no other frontbuffer relevant to PSR is dirty.
 *
 * Dirty frontbuffers relevant to PSR are tracked in busy_frontbuffer_bits.
 */
void intel_psr_flush(struct drm_device *dev,
		     unsigned frontbuffer_bits, enum fb_op_origin origin)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;
	enum pipe pipe;

	mutex_lock(&dev_priv->psr.lock);
	if (!dev_priv->psr.enabled) {
		mutex_unlock(&dev_priv->psr.lock);
		return;
	}

	crtc = dp_to_dig_port(dev_priv->psr.enabled)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->psr.busy_frontbuffer_bits &= ~frontbuffer_bits;

	/* By definition flush = invalidate + flush */
	if (frontbuffer_bits)
		intel_psr_exit(dev);

	if (!dev_priv->psr.active && !dev_priv->psr.busy_frontbuffer_bits)
		if (!work_busy(&dev_priv->psr.work.work))
			schedule_delayed_work(&dev_priv->psr.work,
					      msecs_to_jiffies(100));
	mutex_unlock(&dev_priv->psr.lock);
}

/**
 * intel_psr_init - Init basic PSR work and mutex.
 * @dev: DRM device
 *
 * This function is  called only once at driver load to initialize basic
 * PSR stuff.
 */
void intel_psr_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->psr_mmio_base = IS_HASWELL(dev_priv) ?
		HSW_EDP_PSR_BASE : BDW_EDP_PSR_BASE;

	INIT_DELAYED_WORK(&dev_priv->psr.work, intel_psr_work);
	mutex_init(&dev_priv->psr.lock);
}
