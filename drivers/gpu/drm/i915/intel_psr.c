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
	if (!intel_dp_is_edp(intel_dp))
		return false;

	return intel_dp->psr_dpcd[0] & DP_PSR_IS_SUPPORTED;
}

static bool vlv_is_psr_active_on_pipe(struct drm_device *dev, int pipe)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t val;

	val = I915_READ(VLV_PSRSTAT(pipe)) &
	      VLV_EDP_PSR_CURR_STATE_MASK;
	return (val == VLV_EDP_PSR_ACTIVE_NORFB_UP) ||
	       (val == VLV_EDP_PSR_ACTIVE_SF_UPDATE);
}

static void vlv_psr_setup_vsc(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	uint32_t val;

	/* VLV auto-generate VSC package as per EDP 1.3 spec, Table 3.10 */
	val  = I915_READ(VLV_VSCSDP(crtc->pipe));
	val &= ~VLV_EDP_PSR_SDP_FREQ_MASK;
	val |= VLV_EDP_PSR_SDP_FREQ_EVFRAME;
	I915_WRITE(VLV_VSCSDP(crtc->pipe), val);
}

static void hsw_psr_setup_vsc(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(intel_dig_port->base.base.dev);
	struct edp_vsc_psr psr_vsc;

	if (dev_priv->psr.psr2_support) {
		/* Prepare VSC Header for SU as per EDP 1.4 spec, Table 6.11 */
		memset(&psr_vsc, 0, sizeof(psr_vsc));
		psr_vsc.sdp_header.HB0 = 0;
		psr_vsc.sdp_header.HB1 = 0x7;
		if (dev_priv->psr.colorimetry_support &&
		    dev_priv->psr.y_cord_support) {
			psr_vsc.sdp_header.HB2 = 0x5;
			psr_vsc.sdp_header.HB3 = 0x13;
		} else if (dev_priv->psr.y_cord_support) {
			psr_vsc.sdp_header.HB2 = 0x4;
			psr_vsc.sdp_header.HB3 = 0xe;
		} else {
			psr_vsc.sdp_header.HB2 = 0x3;
			psr_vsc.sdp_header.HB3 = 0xc;
		}
	} else {
		/* Prepare VSC packet as per EDP 1.3 spec, Table 3.10 */
		memset(&psr_vsc, 0, sizeof(psr_vsc));
		psr_vsc.sdp_header.HB0 = 0;
		psr_vsc.sdp_header.HB1 = 0x7;
		psr_vsc.sdp_header.HB2 = 0x2;
		psr_vsc.sdp_header.HB3 = 0x8;
	}

	intel_dig_port->write_infoframe(&intel_dig_port->base.base, crtc_state,
					DP_SDP_VSC, &psr_vsc, sizeof(psr_vsc));
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
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t aux_clock_divider;
	i915_reg_t aux_ctl_reg;
	static const uint8_t aux_msg[] = {
		[0] = DP_AUX_NATIVE_WRITE << 4,
		[1] = DP_SET_POWER >> 8,
		[2] = DP_SET_POWER & 0xff,
		[3] = 1 - 1,
		[4] = DP_SET_POWER_D0,
	};
	enum port port = dig_port->base.port;
	u32 aux_ctl;
	int i;

	BUILD_BUG_ON(sizeof(aux_msg) > 20);

	aux_clock_divider = intel_dp->get_aux_clock_divider(intel_dp, 0);

	/* Enable AUX frame sync at sink */
	if (dev_priv->psr.aux_frame_sync)
		drm_dp_dpcd_writeb(&intel_dp->aux,
				DP_SINK_DEVICE_AUX_FRAME_SYNC_CONF,
				DP_AUX_FRAME_SYNC_ENABLE);
	/* Enable ALPM at sink for psr2 */
	if (dev_priv->psr.psr2_support && dev_priv->psr.alpm)
		drm_dp_dpcd_writeb(&intel_dp->aux,
				DP_RECEIVER_ALPM_CONFIG,
				DP_ALPM_ENABLE);
	if (dev_priv->psr.link_standby)
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG,
				   DP_PSR_ENABLE | DP_PSR_MAIN_LINK_ACTIVE);
	else
		drm_dp_dpcd_writeb(&intel_dp->aux, DP_PSR_EN_CFG,
				   DP_PSR_ENABLE);

	aux_ctl_reg = psr_aux_ctl_reg(dev_priv, port);

	/* Setup AUX registers */
	for (i = 0; i < sizeof(aux_msg); i += 4)
		I915_WRITE(psr_aux_data_reg(dev_priv, port, i >> 2),
			   intel_dp_pack_aux(&aux_msg[i], sizeof(aux_msg) - i));

	aux_ctl = intel_dp->get_aux_send_ctl(intel_dp, 0, sizeof(aux_msg),
					     aux_clock_divider);
	I915_WRITE(aux_ctl_reg, aux_ctl);
}

static void vlv_psr_enable_source(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	/* Transition from PSR_state 0 (disabled) to PSR_state 1 (inactive) */
	I915_WRITE(VLV_PSRCTL(crtc->pipe),
		   VLV_EDP_PSR_MODE_SW_TIMER |
		   VLV_EDP_PSR_SRC_TRANSMITTER_STATE |
		   VLV_EDP_PSR_ENABLE);
}

static void vlv_psr_activate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	/*
	 * Let's do the transition from PSR_state 1 (inactive) to
	 * PSR_state 2 (transition to active - static frame transmission).
	 * Then Hardware is responsible for the transition to
	 * PSR_state 3 (active - no Remote Frame Buffer (RFB) update).
	 */
	I915_WRITE(VLV_PSRCTL(pipe), I915_READ(VLV_PSRCTL(pipe)) |
		   VLV_EDP_PSR_ACTIVE_ENTRY);
}

static void hsw_activate_psr1(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	uint32_t max_sleep_time = 0x1f;
	/*
	 * Let's respect VBT in case VBT asks a higher idle_frame value.
	 * Let's use 6 as the minimum to cover all known cases including
	 * the off-by-one issue that HW has in some cases. Also there are
	 * cases where sink should be able to train
	 * with the 5 or 6 idle patterns.
	 */
	uint32_t idle_frames = max(6, dev_priv->vbt.psr.idle_frames);
	uint32_t val = EDP_PSR_ENABLE;

	val |= max_sleep_time << EDP_PSR_MAX_SLEEP_TIME_SHIFT;
	val |= idle_frames << EDP_PSR_IDLE_FRAME_SHIFT;

	if (IS_HASWELL(dev_priv))
		val |= EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES;

	if (dev_priv->psr.link_standby)
		val |= EDP_PSR_LINK_STANDBY;

	if (dev_priv->vbt.psr.tp1_wakeup_time > 5)
		val |= EDP_PSR_TP1_TIME_2500us;
	else if (dev_priv->vbt.psr.tp1_wakeup_time > 1)
		val |= EDP_PSR_TP1_TIME_500us;
	else if (dev_priv->vbt.psr.tp1_wakeup_time > 0)
		val |= EDP_PSR_TP1_TIME_100us;
	else
		val |= EDP_PSR_TP1_TIME_0us;

	if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 5)
		val |= EDP_PSR_TP2_TP3_TIME_2500us;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 1)
		val |= EDP_PSR_TP2_TP3_TIME_500us;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 0)
		val |= EDP_PSR_TP2_TP3_TIME_100us;
	else
		val |= EDP_PSR_TP2_TP3_TIME_0us;

	if (intel_dp_source_supports_hbr2(intel_dp) &&
	    drm_dp_tps3_supported(intel_dp->dpcd))
		val |= EDP_PSR_TP1_TP3_SEL;
	else
		val |= EDP_PSR_TP1_TP2_SEL;

	val |= I915_READ(EDP_PSR_CTL) & EDP_PSR_RESTORE_PSR_ACTIVE_CTX_MASK;
	I915_WRITE(EDP_PSR_CTL, val);
}

static void hsw_activate_psr2(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	/*
	 * Let's respect VBT in case VBT asks a higher idle_frame value.
	 * Let's use 6 as the minimum to cover all known cases including
	 * the off-by-one issue that HW has in some cases. Also there are
	 * cases where sink should be able to train
	 * with the 5 or 6 idle patterns.
	 */
	uint32_t idle_frames = max(6, dev_priv->vbt.psr.idle_frames);
	uint32_t val;
	uint8_t sink_latency;

	val = idle_frames << EDP_PSR_IDLE_FRAME_SHIFT;

	/* FIXME: selective update is probably totally broken because it doesn't
	 * mesh at all with our frontbuffer tracking. And the hw alone isn't
	 * good enough. */
	val |= EDP_PSR2_ENABLE |
		EDP_SU_TRACK_ENABLE;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
				DP_SYNCHRONIZATION_LATENCY_IN_SINK,
				&sink_latency) == 1) {
		sink_latency &= DP_MAX_RESYNC_FRAME_COUNT_MASK;
	} else {
		sink_latency = 0;
	}
	val |= EDP_PSR2_FRAME_BEFORE_SU(sink_latency + 1);

	if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 5)
		val |= EDP_PSR2_TP2_TIME_2500;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 1)
		val |= EDP_PSR2_TP2_TIME_500;
	else if (dev_priv->vbt.psr.tp2_tp3_wakeup_time > 0)
		val |= EDP_PSR2_TP2_TIME_100;
	else
		val |= EDP_PSR2_TP2_TIME_50;

	I915_WRITE(EDP_PSR2_CTL, val);
}

static void hsw_psr_activate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* On HSW+ after we enable PSR on source it will activate it
	 * as soon as it match configure idle_frame count. So
	 * we just actually enable it here on activation time.
	 */

	/* psr1 and psr2 are mutually exclusive.*/
	if (dev_priv->psr.psr2_support)
		hsw_activate_psr2(intel_dp);
	else
		hsw_activate_psr1(intel_dp);
}

void intel_psr_compute_config(struct intel_dp *intel_dp,
			      struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int psr_setup_time;

	if (!HAS_PSR(dev_priv))
		return;

	if (!is_edp_psr(intel_dp))
		return;

	if (!i915_modparams.enable_psr) {
		DRM_DEBUG_KMS("PSR disable by flag\n");
		return;
	}

	/*
	 * HSW spec explicitly says PSR is tied to port A.
	 * BDW+ platforms with DDI implementation of PSR have different
	 * PSR registers per transcoder and we only implement transcoder EDP
	 * ones. Since by Display design transcoder EDP is tied to port A
	 * we can safely escape based on the port A.
	 */
	if (HAS_DDI(dev_priv) && dig_port->base.port != PORT_A) {
		DRM_DEBUG_KMS("PSR condition failed: Port not supported\n");
		return;
	}

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    !dev_priv->psr.link_standby) {
		DRM_ERROR("PSR condition failed: Link off requested but not supported on this platform\n");
		return;
	}

	if (IS_HASWELL(dev_priv) &&
	    I915_READ(HSW_STEREO_3D_CTL(crtc_state->cpu_transcoder)) &
		      S3D_ENABLE) {
		DRM_DEBUG_KMS("PSR condition failed: Stereo 3D is Enabled\n");
		return;
	}

	if (IS_HASWELL(dev_priv) &&
	    adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		DRM_DEBUG_KMS("PSR condition failed: Interlaced is Enabled\n");
		return;
	}

	psr_setup_time = drm_dp_psr_setup_time(intel_dp->psr_dpcd);
	if (psr_setup_time < 0) {
		DRM_DEBUG_KMS("PSR condition failed: Invalid PSR setup time (0x%02x)\n",
			      intel_dp->psr_dpcd[1]);
		return;
	}

	if (intel_usecs_to_scanlines(adjusted_mode, psr_setup_time) >
	    adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vdisplay - 1) {
		DRM_DEBUG_KMS("PSR condition failed: PSR setup time (%d us) too long\n",
			      psr_setup_time);
		return;
	}

	/*
	 * FIXME psr2_support is messed up. It's both computed
	 * dynamically during PSR enable, and extracted from sink
	 * caps during eDP detection.
	 */
	if (!dev_priv->psr.psr2_support) {
		crtc_state->has_psr = true;
		return;
	}

	/* PSR2 is restricted to work with panel resolutions upto 3200x2000 */
	if (adjusted_mode->crtc_hdisplay > 3200 ||
	    adjusted_mode->crtc_vdisplay > 2000) {
		DRM_DEBUG_KMS("PSR2 disabled, panel resolution too big\n");
		return;
	}

	/*
	 * FIXME:enable psr2 only for y-cordinate psr2 panels
	 * After gtc implementation , remove this restriction.
	 */
	if (!dev_priv->psr.y_cord_support) {
		DRM_DEBUG_KMS("PSR2 disabled, panel does not support Y coordinate\n");
		return;
	}

	crtc_state->has_psr = true;
	crtc_state->has_psr2 = true;
}

static void intel_psr_activate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (dev_priv->psr.psr2_support)
		WARN_ON(I915_READ(EDP_PSR2_CTL) & EDP_PSR2_ENABLE);
	else
		WARN_ON(I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE);
	WARN_ON(dev_priv->psr.active);
	lockdep_assert_held(&dev_priv->psr.lock);

	dev_priv->psr.activate(intel_dp);
	dev_priv->psr.active = true;
}

static void hsw_psr_enable_source(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 chicken;

	if (dev_priv->psr.psr2_support) {
		chicken = PSR2_VSC_ENABLE_PROG_HEADER;
		if (dev_priv->psr.y_cord_support)
			chicken |= PSR2_ADD_VERTICAL_LINE_COUNT;
		I915_WRITE(CHICKEN_TRANS(cpu_transcoder), chicken);

		I915_WRITE(EDP_PSR_DEBUG_CTL,
			   EDP_PSR_DEBUG_MASK_MEMUP |
			   EDP_PSR_DEBUG_MASK_HPD |
			   EDP_PSR_DEBUG_MASK_LPSP |
			   EDP_PSR_DEBUG_MASK_MAX_SLEEP |
			   EDP_PSR_DEBUG_MASK_DISP_REG_WRITE);
	} else {
		/*
		 * Per Spec: Avoid continuous PSR exit by masking MEMUP
		 * and HPD. also mask LPSP to avoid dependency on other
		 * drivers that might block runtime_pm besides
		 * preventing  other hw tracking issues now we can rely
		 * on frontbuffer tracking.
		 */
		I915_WRITE(EDP_PSR_DEBUG_CTL,
			   EDP_PSR_DEBUG_MASK_MEMUP |
			   EDP_PSR_DEBUG_MASK_HPD |
			   EDP_PSR_DEBUG_MASK_LPSP);
	}
}

/**
 * intel_psr_enable - Enable PSR
 * @intel_dp: Intel DP
 * @crtc_state: new CRTC state
 *
 * This function can only be called after the pipe is fully trained and enabled.
 */
void intel_psr_enable(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!crtc_state->has_psr)
		return;

	WARN_ON(dev_priv->drrs.dp);
	mutex_lock(&dev_priv->psr.lock);
	if (dev_priv->psr.enabled) {
		DRM_DEBUG_KMS("PSR already in use\n");
		goto unlock;
	}

	dev_priv->psr.psr2_support = crtc_state->has_psr2;
	dev_priv->psr.source_ok = true;

	dev_priv->psr.busy_frontbuffer_bits = 0;

	dev_priv->psr.setup_vsc(intel_dp, crtc_state);
	dev_priv->psr.enable_sink(intel_dp);
	dev_priv->psr.enable_source(intel_dp, crtc_state);
	dev_priv->psr.enabled = intel_dp;

	if (INTEL_GEN(dev_priv) >= 9) {
		intel_psr_activate(intel_dp);
	} else {
		/*
		 * FIXME: Activation should happen immediately since this
		 * function is just called after pipe is fully trained and
		 * enabled.
		 * However on some platforms we face issues when first
		 * activation follows a modeset so quickly.
		 *     - On VLV/CHV we get bank screen on first activation
		 *     - On HSW/BDW we get a recoverable frozen screen until
		 *       next exit-activate sequence.
		 */
		schedule_delayed_work(&dev_priv->psr.work,
				      msecs_to_jiffies(intel_dp->panel_power_cycle_delay * 5));
	}

unlock:
	mutex_unlock(&dev_priv->psr.lock);
}

static void vlv_psr_disable(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *old_crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	uint32_t val;

	if (dev_priv->psr.active) {
		/* Put VLV PSR back to PSR_state 0 (disabled). */
		if (intel_wait_for_register(dev_priv,
					    VLV_PSRSTAT(crtc->pipe),
					    VLV_EDP_PSR_IN_TRANS,
					    0,
					    1))
			WARN(1, "PSR transition took longer than expected\n");

		val = I915_READ(VLV_PSRCTL(crtc->pipe));
		val &= ~VLV_EDP_PSR_ACTIVE_ENTRY;
		val &= ~VLV_EDP_PSR_ENABLE;
		val &= ~VLV_EDP_PSR_MODE_MASK;
		I915_WRITE(VLV_PSRCTL(crtc->pipe), val);

		dev_priv->psr.active = false;
	} else {
		WARN_ON(vlv_is_psr_active_on_pipe(dev, crtc->pipe));
	}
}

static void hsw_psr_disable(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *old_crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (dev_priv->psr.active) {
		i915_reg_t psr_status;
		u32 psr_status_mask;

		if (dev_priv->psr.aux_frame_sync)
			drm_dp_dpcd_writeb(&intel_dp->aux,
					DP_SINK_DEVICE_AUX_FRAME_SYNC_CONF,
					0);

		if (dev_priv->psr.psr2_support) {
			psr_status = EDP_PSR2_STATUS_CTL;
			psr_status_mask = EDP_PSR2_STATUS_STATE_MASK;

			I915_WRITE(EDP_PSR2_CTL,
				   I915_READ(EDP_PSR2_CTL) &
				   ~(EDP_PSR2_ENABLE | EDP_SU_TRACK_ENABLE));

		} else {
			psr_status = EDP_PSR_STATUS_CTL;
			psr_status_mask = EDP_PSR_STATUS_STATE_MASK;

			I915_WRITE(EDP_PSR_CTL,
				   I915_READ(EDP_PSR_CTL) & ~EDP_PSR_ENABLE);
		}

		/* Wait till PSR is idle */
		if (intel_wait_for_register(dev_priv,
					    psr_status, psr_status_mask, 0,
					    2000))
			DRM_ERROR("Timed out waiting for PSR Idle State\n");

		dev_priv->psr.active = false;
	} else {
		if (dev_priv->psr.psr2_support)
			WARN_ON(I915_READ(EDP_PSR2_CTL) & EDP_PSR2_ENABLE);
		else
			WARN_ON(I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE);
	}
}

/**
 * intel_psr_disable - Disable PSR
 * @intel_dp: Intel DP
 * @old_crtc_state: old CRTC state
 *
 * This function needs to be called before disabling pipe.
 */
void intel_psr_disable(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *old_crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!old_crtc_state->has_psr)
		return;

	mutex_lock(&dev_priv->psr.lock);
	if (!dev_priv->psr.enabled) {
		mutex_unlock(&dev_priv->psr.lock);
		return;
	}

	dev_priv->psr.disable_source(intel_dp, old_crtc_state);

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
	if (HAS_DDI(dev_priv)) {
		if (dev_priv->psr.psr2_support) {
			if (intel_wait_for_register(dev_priv,
						EDP_PSR2_STATUS_CTL,
						EDP_PSR2_STATUS_STATE_MASK,
						0,
						50)) {
				DRM_ERROR("Timed out waiting for PSR2 Idle for re-enable\n");
				return;
			}
		} else {
			if (intel_wait_for_register(dev_priv,
						EDP_PSR_STATUS_CTL,
						EDP_PSR_STATUS_STATE_MASK,
						0,
						50)) {
				DRM_ERROR("Timed out waiting for PSR Idle for re-enable\n");
				return;
			}
		}
	} else {
		if (intel_wait_for_register(dev_priv,
					    VLV_PSRSTAT(pipe),
					    VLV_EDP_PSR_IN_TRANS,
					    0,
					    1)) {
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

static void intel_psr_exit(struct drm_i915_private *dev_priv)
{
	struct intel_dp *intel_dp = dev_priv->psr.enabled;
	struct drm_crtc *crtc = dp_to_dig_port(intel_dp)->base.base.crtc;
	enum pipe pipe = to_intel_crtc(crtc)->pipe;
	u32 val;

	if (!dev_priv->psr.active)
		return;

	if (HAS_DDI(dev_priv)) {
		if (dev_priv->psr.aux_frame_sync)
			drm_dp_dpcd_writeb(&intel_dp->aux,
					DP_SINK_DEVICE_AUX_FRAME_SYNC_CONF,
					0);
		if (dev_priv->psr.psr2_support) {
			val = I915_READ(EDP_PSR2_CTL);
			WARN_ON(!(val & EDP_PSR2_ENABLE));
			I915_WRITE(EDP_PSR2_CTL, val & ~EDP_PSR2_ENABLE);
		} else {
			val = I915_READ(EDP_PSR_CTL);
			WARN_ON(!(val & EDP_PSR_ENABLE));
			I915_WRITE(EDP_PSR_CTL, val & ~EDP_PSR_ENABLE);
		}
	} else {
		val = I915_READ(VLV_PSRCTL(pipe));

		/*
		 * Here we do the transition drirectly from
		 * PSR_state 3 (active - no Remote Frame Buffer (RFB) update) to
		 * PSR_state 5 (exit).
		 * PSR State 4 (active with single frame update) can be skipped.
		 * On PSR_state 5 (exit) Hardware is responsible to transition
		 * back to PSR_state 1 (inactive).
		 * Now we are at Same state after vlv_psr_enable_source.
		 */
		val &= ~VLV_EDP_PSR_ACTIVE_ENTRY;
		I915_WRITE(VLV_PSRCTL(pipe), val);

		/*
		 * Send AUX wake up - Spec says after transitioning to PSR
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
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * Some platforms support a single frame update feature that is used to
 * send and update only one frame on Remote Frame Buffer.
 * So far it is only implemented for Valleyview and Cherryview because
 * hardware requires this to be done before a page flip.
 */
void intel_psr_single_frame_update(struct drm_i915_private *dev_priv,
				   unsigned frontbuffer_bits)
{
	struct drm_crtc *crtc;
	enum pipe pipe;
	u32 val;

	if (!HAS_PSR(dev_priv))
		return;

	/*
	 * Single frame update is already supported on BDW+ but it requires
	 * many W/A and it isn't really needed.
	 */
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
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
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * Since the hardware frontbuffer tracking has gaps we need to integrate
 * with the software frontbuffer tracking. This function gets called every
 * time frontbuffer rendering starts and a buffer gets dirtied. PSR must be
 * disabled if the frontbuffer mask contains a buffer relevant to PSR.
 *
 * Dirty frontbuffers relevant to PSR are tracked in busy_frontbuffer_bits."
 */
void intel_psr_invalidate(struct drm_i915_private *dev_priv,
			  unsigned frontbuffer_bits)
{
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (!HAS_PSR(dev_priv))
		return;

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
		intel_psr_exit(dev_priv);

	mutex_unlock(&dev_priv->psr.lock);
}

/**
 * intel_psr_flush - Flush PSR
 * @dev_priv: i915 device
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
void intel_psr_flush(struct drm_i915_private *dev_priv,
		     unsigned frontbuffer_bits, enum fb_op_origin origin)
{
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (!HAS_PSR(dev_priv))
		return;

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
		intel_psr_exit(dev_priv);

	if (!dev_priv->psr.active && !dev_priv->psr.busy_frontbuffer_bits)
		if (!work_busy(&dev_priv->psr.work.work))
			schedule_delayed_work(&dev_priv->psr.work,
					      msecs_to_jiffies(100));
	mutex_unlock(&dev_priv->psr.lock);
}

/**
 * intel_psr_init - Init basic PSR work and mutex.
 * @dev_priv: i915 device private
 *
 * This function is  called only once at driver load to initialize basic
 * PSR stuff.
 */
void intel_psr_init(struct drm_i915_private *dev_priv)
{
	if (!HAS_PSR(dev_priv))
		return;

	dev_priv->psr_mmio_base = IS_HASWELL(dev_priv) ?
		HSW_EDP_PSR_BASE : BDW_EDP_PSR_BASE;

	/* Per platform default: all disabled. */
	if (i915_modparams.enable_psr == -1)
		i915_modparams.enable_psr = 0;

	/* Set link_standby x link_off defaults */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		/* HSW and BDW require workarounds that we don't implement. */
		dev_priv->psr.link_standby = false;
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		/* On VLV and CHV only standby mode is supported. */
		dev_priv->psr.link_standby = true;
	else
		/* For new platforms let's respect VBT back again */
		dev_priv->psr.link_standby = dev_priv->vbt.psr.full_link;

	/* Override link_standby x link_off defaults */
	if (i915_modparams.enable_psr == 2 && !dev_priv->psr.link_standby) {
		DRM_DEBUG_KMS("PSR: Forcing link standby\n");
		dev_priv->psr.link_standby = true;
	}
	if (i915_modparams.enable_psr == 3 && dev_priv->psr.link_standby) {
		DRM_DEBUG_KMS("PSR: Forcing main link off\n");
		dev_priv->psr.link_standby = false;
	}

	INIT_DELAYED_WORK(&dev_priv->psr.work, intel_psr_work);
	mutex_init(&dev_priv->psr.lock);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		dev_priv->psr.enable_source = vlv_psr_enable_source;
		dev_priv->psr.disable_source = vlv_psr_disable;
		dev_priv->psr.enable_sink = vlv_psr_enable_sink;
		dev_priv->psr.activate = vlv_psr_activate;
		dev_priv->psr.setup_vsc = vlv_psr_setup_vsc;
	} else {
		dev_priv->psr.enable_source = hsw_psr_enable_source;
		dev_priv->psr.disable_source = hsw_psr_disable;
		dev_priv->psr.enable_sink = hsw_psr_enable_sink;
		dev_priv->psr.activate = hsw_psr_activate;
		dev_priv->psr.setup_vsc = hsw_psr_setup_vsc;
	}
}
