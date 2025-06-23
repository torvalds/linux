// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 *
 * High level crtc/connector/encoder modeset state verification.
 */

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_print.h>

#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_cx0_phy.h"
#include "intel_display.h"
#include "intel_display_core.h"
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
	struct intel_display *display = to_intel_display(connector);

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.base.id, connector->base.name);

	if (connector->get_hw_state(connector)) {
		struct intel_encoder *encoder = intel_attached_encoder(connector);

		INTEL_DISPLAY_STATE_WARN(display, !crtc_state,
					 "connector enabled without attached crtc\n");

		if (!crtc_state)
			return;

		INTEL_DISPLAY_STATE_WARN(display, !crtc_state->hw.active,
					 "connector is active, but attached crtc isn't\n");

		if (!encoder || encoder->type == INTEL_OUTPUT_DP_MST)
			return;

		INTEL_DISPLAY_STATE_WARN(display,
					 conn_state->best_encoder != &encoder->base,
					 "atomic encoder doesn't match attached encoder\n");

		INTEL_DISPLAY_STATE_WARN(display, conn_state->crtc != encoder->base.crtc,
					 "attached encoder crtc differs from connector crtc\n");
	} else {
		INTEL_DISPLAY_STATE_WARN(display, crtc_state && crtc_state->hw.active,
					 "attached crtc is active, but connector isn't\n");
		INTEL_DISPLAY_STATE_WARN(display, !crtc_state && conn_state->best_encoder,
					 "best encoder set without crtc!\n");
	}
}

static void
verify_connector_state(struct intel_atomic_state *state,
		       struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
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

		INTEL_DISPLAY_STATE_WARN(display, new_conn_state->best_encoder != encoder,
					 "connector's atomic encoder doesn't match legacy encoder\n");
	}
}

static void intel_pipe_config_sanity_check(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (crtc_state->has_pch_encoder) {
		int fdi_dotclock = intel_dotclock_calculate(intel_fdi_link_freq(display, crtc_state),
							    &crtc_state->fdi_m_n);
		int dotclock = crtc_state->hw.adjusted_mode.crtc_clock;

		/*
		 * FDI already provided one idea for the dotclock.
		 * Yell if the encoder disagrees. Allow for slight
		 * rounding differences.
		 */
		drm_WARN(display->drm, abs(fdi_dotclock - dotclock) > 1,
			 "FDI dotclock and encoder dotclock mismatch, fdi: %i, encoder: %i\n",
			 fdi_dotclock, dotclock);
	}
}

static void
verify_encoder_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_encoder *encoder;
	struct drm_connector *connector;
	const struct drm_connector_state *old_conn_state, *new_conn_state;
	int i;

	for_each_intel_encoder(display->drm, encoder) {
		bool enabled = false, found = false;
		enum pipe pipe;

		drm_dbg_kms(display->drm, "[ENCODER:%d:%s]\n",
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

			INTEL_DISPLAY_STATE_WARN(display,
						 new_conn_state->crtc != encoder->base.crtc,
						 "connector's crtc doesn't match encoder crtc\n");
		}

		if (!found)
			continue;

		INTEL_DISPLAY_STATE_WARN(display, !!encoder->base.crtc != enabled,
					 "encoder's enabled state mismatch (expected %i, found %i)\n",
					 !!encoder->base.crtc, enabled);

		if (!encoder->base.crtc) {
			bool active;

			active = encoder->get_hw_state(encoder, &pipe);
			INTEL_DISPLAY_STATE_WARN(display, active,
						 "encoder detached but still enabled on pipe %c.\n",
						 pipe_name(pipe));
		}
	}
}

static void
verify_crtc_state(struct intel_atomic_state *state,
		  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *sw_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_crtc_state *hw_crtc_state;
	struct intel_crtc *primary_crtc;
	struct intel_encoder *encoder;

	hw_crtc_state = intel_crtc_state_alloc(crtc);
	if (!hw_crtc_state)
		return;

	drm_dbg_kms(display->drm, "[CRTC:%d:%s]\n", crtc->base.base.id,
		    crtc->base.name);

	hw_crtc_state->hw.enable = sw_crtc_state->hw.enable;

	intel_crtc_get_pipe_config(hw_crtc_state);

	/* we keep both pipes enabled on 830 */
	if (display->platform.i830 && hw_crtc_state->hw.active)
		hw_crtc_state->hw.active = sw_crtc_state->hw.active;

	INTEL_DISPLAY_STATE_WARN(display,
				 sw_crtc_state->hw.active != hw_crtc_state->hw.active,
				 "crtc active state doesn't match with hw state (expected %i, found %i)\n",
				 sw_crtc_state->hw.active, hw_crtc_state->hw.active);

	INTEL_DISPLAY_STATE_WARN(display, crtc->active != sw_crtc_state->hw.active,
				 "transitional active state does not match atomic hw state (expected %i, found %i)\n",
				 sw_crtc_state->hw.active, crtc->active);

	primary_crtc = intel_primary_crtc(sw_crtc_state);

	for_each_encoder_on_crtc(display->drm, &primary_crtc->base, encoder) {
		enum pipe pipe;
		bool active;

		active = encoder->get_hw_state(encoder, &pipe);
		INTEL_DISPLAY_STATE_WARN(display, active != sw_crtc_state->hw.active,
					 "[ENCODER:%i] active %i with crtc active %i\n",
					 encoder->base.base.id, active,
					 sw_crtc_state->hw.active);

		INTEL_DISPLAY_STATE_WARN(display, active && primary_crtc->pipe != pipe,
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
		INTEL_DISPLAY_STATE_WARN(display, 1, "pipe state doesn't match!\n");
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
	intel_dpll_state_verify(state, crtc);
	intel_mpllb_state_verify(state, crtc);
	intel_cx0pll_state_verify(state, crtc);
}

void intel_modeset_verify_disabled(struct intel_atomic_state *state)
{
	verify_encoder_state(state);
	verify_connector_state(state, NULL);
	intel_dpll_verify_disabled(state);
}
