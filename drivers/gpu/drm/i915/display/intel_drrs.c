// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_drrs.h"
#include "intel_panel.h"

/**
 * DOC: Display Refresh Rate Switching (DRRS)
 *
 * Display Refresh Rate Switching (DRRS) is a power conservation feature
 * which enables swtching between low and high refresh rates,
 * dynamically, based on the usage scenario. This feature is applicable
 * for internal panels.
 *
 * Indication that the panel supports DRRS is given by the panel EDID, which
 * would list multiple refresh rates for one resolution.
 *
 * DRRS is of 2 types - static and seamless.
 * Static DRRS involves changing refresh rate (RR) by doing a full modeset
 * (may appear as a blink on screen) and is used in dock-undock scenario.
 * Seamless DRRS involves changing RR without any visual effect to the user
 * and can be used during normal system usage. This is done by programming
 * certain registers.
 *
 * Support for static/seamless DRRS may be indicated in the VBT based on
 * inputs from the panel spec.
 *
 * DRRS saves power by switching to low RR based on usage scenarios.
 *
 * The implementation is based on frontbuffer tracking implementation.  When
 * there is a disturbance on the screen triggered by user activity or a periodic
 * system activity, DRRS is disabled (RR is changed to high RR).  When there is
 * no movement on screen, after a timeout of 1 second, a switch to low RR is
 * made.
 *
 * For integration with frontbuffer tracking code, intel_drrs_invalidate()
 * and intel_drrs_flush() are called.
 *
 * DRRS can be further extended to support other internal panels and also
 * the scenario of video playback wherein RR is set based on the rate
 * requested by userspace.
 */

const char *intel_drrs_type_str(enum drrs_type drrs_type)
{
	static const char * const str[] = {
		[DRRS_TYPE_NONE] = "none",
		[DRRS_TYPE_STATIC] = "static",
		[DRRS_TYPE_SEAMLESS] = "seamless",
	};

	if (drrs_type >= ARRAY_SIZE(str))
		return "<invalid>";

	return str[drrs_type];
}

static bool can_enable_drrs(struct intel_connector *connector,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_display_mode *downclock_mode)
{
	if (pipe_config->vrr.enable)
		return false;

	/*
	 * DRRS and PSR can't be enable together, so giving preference to PSR
	 * as it allows more power-savings by complete shutting down display,
	 * so to guarantee this, intel_drrs_compute_config() must be called
	 * after intel_psr_compute_config().
	 */
	if (pipe_config->has_psr)
		return false;

	return downclock_mode &&
		intel_panel_drrs_type(connector) == DRRS_TYPE_SEAMLESS;
}

void
intel_drrs_compute_config(struct intel_connector *connector,
			  struct intel_crtc_state *pipe_config,
			  int output_bpp, bool constant_n)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *downclock_mode =
		intel_panel_downclock_mode(connector, &pipe_config->hw.adjusted_mode);
	int pixel_clock;

	if (!can_enable_drrs(connector, pipe_config, downclock_mode)) {
		if (intel_cpu_transcoder_has_m2_n2(i915, pipe_config->cpu_transcoder))
			intel_zero_m_n(&pipe_config->dp_m2_n2);
		return;
	}

	if (IS_IRONLAKE(i915) || IS_SANDYBRIDGE(i915) || IS_IVYBRIDGE(i915))
		pipe_config->msa_timing_delay = i915->vbt.edp.drrs_msa_timing_delay;

	pipe_config->has_drrs = true;

	pixel_clock = downclock_mode->clock;
	if (pipe_config->splitter.enable)
		pixel_clock /= pipe_config->splitter.link_count;

	intel_link_compute_m_n(output_bpp, pipe_config->lane_count, pixel_clock,
			       pipe_config->port_clock, &pipe_config->dp_m2_n2,
			       constant_n, pipe_config->fec_enable);

	/* FIXME: abstract this better */
	if (pipe_config->splitter.enable)
		pipe_config->dp_m2_n2.data_m *= pipe_config->splitter.link_count;
}

static void
intel_drrs_set_refresh_rate_pipeconf(struct intel_crtc *crtc,
				     enum drrs_refresh_rate refresh_rate)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc->drrs.cpu_transcoder;
	u32 val, bit;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		bit = PIPECONF_REFRESH_RATE_ALT_VLV;
	else
		bit = PIPECONF_REFRESH_RATE_ALT_ILK;

	val = intel_de_read(dev_priv, PIPECONF(cpu_transcoder));

	if (refresh_rate == DRRS_REFRESH_RATE_LOW)
		val |= bit;
	else
		val &= ~bit;

	intel_de_write(dev_priv, PIPECONF(cpu_transcoder), val);
}

static void
intel_drrs_set_refresh_rate_m_n(struct intel_crtc *crtc,
				enum drrs_refresh_rate refresh_rate)
{
	intel_cpu_transcoder_set_m1_n1(crtc, crtc->drrs.cpu_transcoder,
				       refresh_rate == DRRS_REFRESH_RATE_LOW ?
				       &crtc->drrs.m2_n2 : &crtc->drrs.m_n);
}

bool intel_drrs_is_enabled(struct intel_crtc *crtc)
{
	return crtc->drrs.cpu_transcoder != INVALID_TRANSCODER;
}

static void intel_drrs_set_state(struct intel_crtc *crtc,
				 enum drrs_refresh_rate refresh_rate)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (refresh_rate == crtc->drrs.refresh_rate)
		return;

	if (intel_cpu_transcoder_has_m2_n2(dev_priv, crtc->drrs.cpu_transcoder))
		intel_drrs_set_refresh_rate_pipeconf(crtc, refresh_rate);
	else
		intel_drrs_set_refresh_rate_m_n(crtc, refresh_rate);

	crtc->drrs.refresh_rate = refresh_rate;
}

/**
 * intel_drrs_enable - init drrs struct if supported
 * @crtc_state: A pointer to the active crtc state.
 *
 * Initializes frontbuffer_bits and drrs.dp
 */
void intel_drrs_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (!crtc_state->has_drrs)
		return;

	drm_dbg_kms(&dev_priv->drm, "[CRTC:%d:%s] Enabling DRRS\n",
		    crtc->base.base.id, crtc->base.name);

	mutex_lock(&crtc->drrs.mutex);

	crtc->drrs.cpu_transcoder = crtc_state->cpu_transcoder;
	crtc->drrs.m_n = crtc_state->dp_m_n;
	crtc->drrs.m2_n2 = crtc_state->dp_m2_n2;
	crtc->drrs.busy_frontbuffer_bits = 0;

	mutex_unlock(&crtc->drrs.mutex);
}

/**
 * intel_drrs_disable - Disable DRRS
 * @old_crtc_state: Pointer to old crtc_state.
 */
void intel_drrs_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (!old_crtc_state->has_drrs)
		return;

	drm_dbg_kms(&dev_priv->drm, "[CRTC:%d:%s] Disabling DRRS\n",
		    crtc->base.base.id, crtc->base.name);

	mutex_lock(&crtc->drrs.mutex);

	if (intel_drrs_is_enabled(crtc))
		intel_drrs_set_state(crtc, DRRS_REFRESH_RATE_HIGH);

	crtc->drrs.cpu_transcoder = INVALID_TRANSCODER;
	crtc->drrs.busy_frontbuffer_bits = 0;

	mutex_unlock(&crtc->drrs.mutex);

	cancel_delayed_work_sync(&crtc->drrs.work);
}

/**
 * intel_drrs_update - Update DRRS during fastset
 * @state: atomic state
 * @crtc: crtc
 */
void intel_drrs_update(struct intel_atomic_state *state,
		       struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (old_crtc_state->has_drrs == new_crtc_state->has_drrs)
		return;

	if (new_crtc_state->has_drrs)
		intel_drrs_enable(new_crtc_state);
	else
		intel_drrs_disable(old_crtc_state);
}

static void intel_drrs_downclock_work(struct work_struct *work)
{
	struct intel_crtc *crtc = container_of(work, typeof(*crtc), drrs.work.work);

	mutex_lock(&crtc->drrs.mutex);

	if (intel_drrs_is_enabled(crtc) && !crtc->drrs.busy_frontbuffer_bits)
		intel_drrs_set_state(crtc, DRRS_REFRESH_RATE_LOW);

	mutex_unlock(&crtc->drrs.mutex);
}

static void intel_drrs_frontbuffer_update(struct drm_i915_private *dev_priv,
					  unsigned int frontbuffer_bits,
					  bool invalidate)
{
	struct intel_crtc *crtc;

	if (dev_priv->vbt.drrs_type != DRRS_TYPE_SEAMLESS)
		return;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		cancel_delayed_work(&crtc->drrs.work);

		mutex_lock(&crtc->drrs.mutex);

		if (!intel_drrs_is_enabled(crtc)) {
			mutex_unlock(&crtc->drrs.mutex);
			continue;
		}

		frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(crtc->pipe);
		if (invalidate)
			crtc->drrs.busy_frontbuffer_bits |= frontbuffer_bits;
		else
			crtc->drrs.busy_frontbuffer_bits &= ~frontbuffer_bits;

		/* flush/invalidate means busy screen hence upclock */
		if (frontbuffer_bits)
			intel_drrs_set_state(crtc, DRRS_REFRESH_RATE_HIGH);

		/*
		 * flush also means no more activity hence schedule downclock, if all
		 * other fbs are quiescent too
		 */
		if (!invalidate && !crtc->drrs.busy_frontbuffer_bits)
			schedule_delayed_work(&crtc->drrs.work,
					      msecs_to_jiffies(1000));

		mutex_unlock(&crtc->drrs.mutex);
	}
}

/**
 * intel_drrs_invalidate - Disable Idleness DRRS
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called everytime rendering on the given planes start.
 * Hence DRRS needs to be Upclocked, i.e. (LOW_RR -> HIGH_RR).
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_drrs_invalidate(struct drm_i915_private *dev_priv,
			   unsigned int frontbuffer_bits)
{
	intel_drrs_frontbuffer_update(dev_priv, frontbuffer_bits, true);
}

/**
 * intel_drrs_flush - Restart Idleness DRRS
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called every time rendering on the given planes has
 * completed or flip on a crtc is completed. So DRRS should be upclocked
 * (LOW_RR -> HIGH_RR). And also Idleness detection should be started again,
 * if no other planes are dirty.
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_drrs_flush(struct drm_i915_private *dev_priv,
		      unsigned int frontbuffer_bits)
{
	intel_drrs_frontbuffer_update(dev_priv, frontbuffer_bits, false);
}

void intel_drrs_page_flip(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	unsigned int frontbuffer_bits = INTEL_FRONTBUFFER_ALL_MASK(crtc->pipe);

	intel_drrs_frontbuffer_update(dev_priv, frontbuffer_bits, false);
}

/**
 * intel_crtc_drrs_init - Init DRRS for CRTC
 * @crtc: crtc
 *
 * This function is called only once at driver load to initialize basic
 * DRRS stuff.
 *
 */
void intel_crtc_drrs_init(struct intel_crtc *crtc)
{
	INIT_DELAYED_WORK(&crtc->drrs.work, intel_drrs_downclock_work);
	mutex_init(&crtc->drrs.mutex);
	crtc->drrs.cpu_transcoder = INVALID_TRANSCODER;
}

/**
 * intel_drrs_init - Init DRRS for eDP connector
 * @connector: eDP connector
 * @fixed_mode: preferred mode of panel
 *
 * This function is called only once at driver load to initialize
 * DRRS support for the connector.
 *
 * Returns:
 * Downclock mode if panel supports it, else return NULL.
 * DRRS support is determined by the presence of downclock mode (apart
 * from VBT setting).
 */
struct drm_display_mode *
intel_drrs_init(struct intel_connector *connector,
		const struct drm_display_mode *fixed_mode)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_encoder *encoder = connector->encoder;
	struct drm_display_mode *downclock_mode;

	if (DISPLAY_VER(dev_priv) < 5) {
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] DRRS not supported on platform\n",
			    connector->base.base.id, connector->base.name);
		return NULL;
	}

	if ((DISPLAY_VER(dev_priv) < 8 && !HAS_GMCH(dev_priv)) &&
	    encoder->port != PORT_A) {
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] DRRS not supported on [ENCODER:%d:%s]\n",
			    connector->base.base.id, connector->base.name,
			    encoder->base.base.id, encoder->base.name);
		return NULL;
	}

	if (dev_priv->vbt.drrs_type == DRRS_TYPE_NONE) {
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] DRRS not supported according to VBT\n",
			    connector->base.base.id, connector->base.name);
		return NULL;
	}

	downclock_mode = intel_panel_edid_downclock_mode(connector, fixed_mode);
	if (!downclock_mode) {
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] DRRS not supported due to lack of downclock mode\n",
			    connector->base.base.id, connector->base.name);
		return NULL;
	}

	drm_dbg_kms(&dev_priv->drm,
		    "[CONNECTOR:%d:%s] %s DRRS supported\n",
		    connector->base.base.id, connector->base.name,
		    intel_drrs_type_str(dev_priv->vbt.drrs_type));

	return downclock_mode;
}
