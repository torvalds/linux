// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 *
 * High level crtc/connector/encoder modeset state verification.
 */

#include <drm/drm_atomic_state_helper.h>

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fdi.h"
#include "intel_modeset_verify.h"
#include "intel_pm.h"
#include "intel_snps_phy.h"

/*
 * Cross check the actual hw state with our own modeset state tracking (and its
 * internal consistency).
 */
static void intel_connector_verify_state(struct intel_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.base.id, connector->base.name);

	if (connector->get_hw_state(connector)) {
		struct intel_encoder *encoder = intel_attached_encoder(connector);

		I915_STATE_WARN(!crtc_state,
				"connector enabled without attached crtc\n");

		if (!crtc_state)
			return;

		I915_STATE_WARN(!crtc_state->hw.active,
				"connector is active, but attached crtc isn't\n");

		if (!encoder || encoder->type == INTEL_OUTPUT_DP_MST)
			return;

		I915_STATE_WARN(conn_state->best_encoder != &encoder->base,
				"atomic encoder doesn't match attached encoder\n");

		I915_STATE_WARN(conn_state->crtc != encoder->base.crtc,
				"attached encoder crtc differs from connector crtc\n");
	} else {
		I915_STATE_WARN(crtc_state && crtc_state->hw.active,
				"attached crtc is active, but connector isn't\n");
		I915_STATE_WARN(!crtc_state && conn_state->best_encoder,
				"best encoder set without crtc!\n");
	}
}

static void
verify_connector_state(struct intel_atomic_state *state,
		       struct intel_crtc *crtc)
{
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_connector_in_state(&state->base, connector, new_conn_state, i) {
		struct drm_encoder *encoder = connector->encoder;
		struct intel_crtc_state *crtc_state = NULL;

		if (new_conn_state->crtc != &crtc->base)
			continue;

		if (crtc)
			crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		intel_connector_verify_state(crtc_state, new_conn_state);

		I915_STATE_WARN(new_conn_state->best_encoder != encoder,
				"connector's atomic encoder doesn't match legacy encoder\n");
	}
}

static void intel_pipe_config_sanity_check(struct drm_i915_private *dev_priv,
					   const struct intel_crtc_state *pipe_config)
{
	if (pipe_config->has_pch_encoder) {
		int fdi_dotclock = intel_dotclock_calculate(intel_fdi_link_freq(dev_priv, pipe_config),
							    &pipe_config->fdi_m_n);
		int dotclock = pipe_config->hw.adjusted_mode.crtc_clock;

		/*
		 * FDI already provided one idea for the dotclock.
		 * Yell if the encoder disagrees.
		 */
		drm_WARN(&dev_priv->drm,
			 !intel_fuzzy_clock_check(fdi_dotclock, dotclock),
			 "FDI dotclock and encoder dotclock mismatch, fdi: %i, encoder: %i\n",
			 fdi_dotclock, dotclock);
	}
}

static void
verify_encoder_state(struct drm_i915_private *dev_priv, struct intel_atomic_state *state)
{
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	int i;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		bool enabled = false, found = false;
		enum pipe pipe;

		drm_dbg_kms(&dev_priv->drm, "[ENCODER:%d:%s]\n",
			    encoder->base.base.id,
			    encoder->base.name);

		for_each_oldnew_connector_in_state(&state->base, connector, old_conn_state,
						   new_conn_state, i) {
			if (old_conn_state->best_encoder == &encoder->base)
				found = true;

			if (new_conn_state->best_encoder != &encoder->base)
				continue;

			found = true;
			enabled = true;

			I915_STATE_WARN(new_conn_state->crtc !=
					encoder->base.crtc,
					"connector's crtc doesn't match encoder crtc\n");
		}

		if (!found)
			continue;

		I915_STATE_WARN(!!encoder->base.crtc != enabled,
				"encoder's enabled state mismatch (expected %i, found %i)\n",
				!!encoder->base.crtc, enabled);

		if (!encoder->base.crtc) {
			bool active;

			active = encoder->get_hw_state(encoder, &pipe);
			I915_STATE_WARN(active,
					"encoder detached but still enabled on pipe %c.\n",
					pipe_name(pipe));
		}
	}
}

static void
verify_crtc_state(struct intel_crtc *crtc,
		  struct intel_crtc_state *old_crtc_state,
		  struct intel_crtc_state *new_crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *encoder;
	struct intel_crtc_state *pipe_config = old_crtc_state;
	struct drm_atomic_state *state = old_crtc_state->uapi.state;
	struct intel_crtc *master_crtc;

	__drm_atomic_helper_crtc_destroy_state(&old_crtc_state->uapi);
	intel_crtc_free_hw_state(old_crtc_state);
	intel_crtc_state_reset(old_crtc_state, crtc);
	old_crtc_state->uapi.state = state;

	drm_dbg_kms(&dev_priv->drm, "[CRTC:%d:%s]\n", crtc->base.base.id,
		    crtc->base.name);

	pipe_config->hw.enable = new_crtc_state->hw.enable;

	intel_crtc_get_pipe_config(pipe_config);

	/* we keep both pipes enabled on 830 */
	if (IS_I830(dev_priv) && pipe_config->hw.active)
		pipe_config->hw.active = new_crtc_state->hw.active;

	I915_STATE_WARN(new_crtc_state->hw.active != pipe_config->hw.active,
			"crtc active state doesn't match with hw state (expected %i, found %i)\n",
			new_crtc_state->hw.active, pipe_config->hw.active);

	I915_STATE_WARN(crtc->active != new_crtc_state->hw.active,
			"transitional active state does not match atomic hw state (expected %i, found %i)\n",
			new_crtc_state->hw.active, crtc->active);

	master_crtc = intel_master_crtc(new_crtc_state);

	for_each_encoder_on_crtc(dev, &master_crtc->base, encoder) {
		enum pipe pipe;
		bool active;

		active = encoder->get_hw_state(encoder, &pipe);
		I915_STATE_WARN(active != new_crtc_state->hw.active,
				"[ENCODER:%i] active %i with crtc active %i\n",
				encoder->base.base.id, active,
				new_crtc_state->hw.active);

		I915_STATE_WARN(active && master_crtc->pipe != pipe,
				"Encoder connected to wrong pipe %c\n",
				pipe_name(pipe));

		if (active)
			intel_encoder_get_config(encoder, pipe_config);
	}

	if (!new_crtc_state->hw.active)
		return;

	intel_pipe_config_sanity_check(dev_priv, pipe_config);

	if (!intel_pipe_config_compare(new_crtc_state,
				       pipe_config, false)) {
		I915_STATE_WARN(1, "pipe state doesn't match!\n");
		intel_crtc_state_dump(pipe_config, NULL, "hw state");
		intel_crtc_state_dump(new_crtc_state, NULL, "sw state");
	}
}

void intel_modeset_verify_crtc(struct intel_crtc *crtc,
			       struct intel_atomic_state *state,
			       struct intel_crtc_state *old_crtc_state,
			       struct intel_crtc_state *new_crtc_state)
{
	if (!intel_crtc_needs_modeset(new_crtc_state) && !new_crtc_state->update_pipe)
		return;

	intel_wm_state_verify(crtc, new_crtc_state);
	verify_connector_state(state, crtc);
	verify_crtc_state(crtc, old_crtc_state, new_crtc_state);
	intel_shared_dpll_state_verify(crtc, old_crtc_state, new_crtc_state);
	intel_mpllb_state_verify(state, new_crtc_state);
}

void intel_modeset_verify_disabled(struct drm_i915_private *dev_priv,
				   struct intel_atomic_state *state)
{
	verify_encoder_state(dev_priv, state);
	verify_connector_state(state, NULL);
	intel_shared_dpll_verify_disabled(dev_priv);
}
