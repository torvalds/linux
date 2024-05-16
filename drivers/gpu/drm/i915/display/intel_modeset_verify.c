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
#include "intel_cx0_phy.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fdi.h"
#include "intel_modeset_verify.h"
#include "intel_snps_phy.h"
#include "skl_watermark.h"

/*
 * Cross check the actual hw state with our own modeset state tracking (and its
 * internal consistency).
 */
static void intel_connector_verify_state(const struct intel_crtc_state *crtc_state,
					 const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.base.id, connector->base.name);

	if (connector->get_hw_state(connector)) {
		struct intel_encoder *encoder = intel_attached_encoder(connector);

		I915_STATE_WARN(i915, !crtc_state,
				"connector enabled without attached crtc\n");

		if (!crtc_state)
			return;

		I915_STATE_WARN(i915, !crtc_state->hw.active,
				"connector is active, but attached crtc isn't\n");

		if (!encoder || encoder->type == INTEL_OUTPUT_DP_MST)
			return;

		I915_STATE_WARN(i915,
				conn_state->best_encoder != &encoder->base,
				"atomic encoder doesn't match attached encoder\n");

		I915_STATE_WARN(i915, conn_state->crtc != encoder->base.crtc,
				"attached encoder crtc differs from connector crtc\n");
	} else {
		I915_STATE_WARN(i915, crtc_state && crtc_state->hw.active,
				"attached crtc is active, but connector isn't\n");
		I915_STATE_WARN(i915, !crtc_state && conn_state->best_encoder,
				"best encoder set without crtc!\n");
	}
}

static void
verify_connector_state(struct intel_atomic_state *state,
		       struct intel_crtc *crtc)
{
	struct drm_connector *connector;
	const struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_connector_in_state(&state->base, connector, new_conn_state, i) {
		struct drm_encoder *encoder = connector->encoder;
		const struct intel_crtc_state *crtc_state = NULL;

		if (new_conn_state->crtc != &crtc->base)
			continue;

		if (crtc)
			crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		intel_connector_verify_state(crtc_state, new_conn_state);

		I915_STATE_WARN(to_i915(connector->dev), new_conn_state->best_encoder != encoder,
				"connector's atomic encoder doesn't match legacy encoder\n");
	}
}

static void intel_pipe_config_sanity_check(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->has_pch_encoder) {
		int fdi_dotclock = intel_dotclock_calculate(intel_fdi_link_freq(i915, crtc_state),
							    &crtc_state->fdi_m_n);
		int dotclock = crtc_state->hw.adjusted_mode.crtc_clock;

		/*
		 * FDI already provided one idea for the dotclock.
		 * Yell if the encoder disagrees. Allow for slight
		 * rounding differences.
		 */
		drm_WARN(&i915->drm, abs(fdi_dotclock - dotclock) > 1,
			 "FDI dotclock and encoder dotclock mismatch, fdi: %i, encoder: %i\n",
			 fdi_dotclock, dotclock);
	}
}

static void
verify_encoder_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	const struct drm_connector_state *old_conn_state, *new_conn_state;
	int i;

	for_each_intel_encoder(&i915->drm, encoder) {
		bool enabled = false, found = false;
		enum pipe pipe;

		drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s]\n",
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

			I915_STATE_WARN(i915,
					new_conn_state->crtc != encoder->base.crtc,
					"connector's crtc doesn't match encoder crtc\n");
		}

		if (!found)
			continue;

		I915_STATE_WARN(i915, !!encoder->base.crtc != enabled,
				"encoder's enabled state mismatch (expected %i, found %i)\n",
				!!encoder->base.crtc, enabled);

		if (!encoder->base.crtc) {
			bool active;

			active = encoder->get_hw_state(encoder, &pipe);
			I915_STATE_WARN(i915, active,
					"encoder detached but still enabled on pipe %c.\n",
					pipe_name(pipe));
		}
	}
}

static void
verify_crtc_state(struct intel_atomic_state *state,
		  struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	const struct intel_crtc_state *sw_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_state *hw_crtc_state;
	struct intel_crtc *master_crtc;
	struct intel_encoder *encoder;

	hw_crtc_state = intel_crtc_state_alloc(crtc);
	if (!hw_crtc_state)
		return;

	drm_dbg_kms(&i915->drm, "[CRTC:%d:%s]\n", crtc->base.base.id,
		    crtc->base.name);

	hw_crtc_state->hw.enable = sw_crtc_state->hw.enable;

	intel_crtc_get_pipe_config(hw_crtc_state);

	/* we keep both pipes enabled on 830 */
	if (IS_I830(i915) && hw_crtc_state->hw.active)
		hw_crtc_state->hw.active = sw_crtc_state->hw.active;

	I915_STATE_WARN(i915,
			sw_crtc_state->hw.active != hw_crtc_state->hw.active,
			"crtc active state doesn't match with hw state (expected %i, found %i)\n",
			sw_crtc_state->hw.active, hw_crtc_state->hw.active);

	I915_STATE_WARN(i915, crtc->active != sw_crtc_state->hw.active,
			"transitional active state does not match atomic hw state (expected %i, found %i)\n",
			sw_crtc_state->hw.active, crtc->active);

	master_crtc = intel_master_crtc(sw_crtc_state);

	for_each_encoder_on_crtc(dev, &master_crtc->base, encoder) {
		enum pipe pipe;
		bool active;

		active = encoder->get_hw_state(encoder, &pipe);
		I915_STATE_WARN(i915, active != sw_crtc_state->hw.active,
				"[ENCODER:%i] active %i with crtc active %i\n",
				encoder->base.base.id, active,
				sw_crtc_state->hw.active);

		I915_STATE_WARN(i915, active && master_crtc->pipe != pipe,
				"Encoder connected to wrong pipe %c\n",
				pipe_name(pipe));

		if (active)
			intel_encoder_get_config(encoder, hw_crtc_state);
	}

	if (!sw_crtc_state->hw.active)
		goto destroy_state;

	intel_pipe_config_sanity_check(hw_crtc_state);

	if (!intel_pipe_config_compare(sw_crtc_state,
				       hw_crtc_state, false)) {
		I915_STATE_WARN(i915, 1, "pipe state doesn't match!\n");
		intel_crtc_state_dump(hw_crtc_state, NULL, "hw state");
		intel_crtc_state_dump(sw_crtc_state, NULL, "sw state");
	}

destroy_state:
	intel_crtc_destroy_state(&crtc->base, &hw_crtc_state->uapi);
}

void intel_modeset_verify_crtc(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!intel_crtc_needs_modeset(new_crtc_state) &&
	    !intel_crtc_needs_fastset(new_crtc_state))
		return;

	intel_wm_state_verify(state, crtc);
	verify_connector_state(state, crtc);
	verify_crtc_state(state, crtc);
	intel_shared_dpll_state_verify(state, crtc);
	intel_mpllb_state_verify(state, crtc);
	intel_cx0pll_state_verify(state, crtc);
}

void intel_modeset_verify_disabled(struct intel_atomic_state *state)
{
	verify_encoder_state(state);
	verify_connector_state(state, NULL);
	intel_shared_dpll_verify_disabled(state);
}
