// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 *
 * Read out the current hardware modeset state, and sanitize it to the current
 * state.
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_state_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_crtc_state_dump.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_modeset_setup.h"
#include "intel_pch_display.h"
#include "intel_pm.h"
#include "skl_watermark.h"

static void intel_crtc_disable_noatomic(struct intel_crtc *crtc,
					struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_bw_state *bw_state =
		to_intel_bw_state(i915->display.bw.obj.state);
	struct intel_cdclk_state *cdclk_state =
		to_intel_cdclk_state(i915->display.cdclk.obj.state);
	struct intel_dbuf_state *dbuf_state =
		to_intel_dbuf_state(i915->display.dbuf.obj.state);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_plane *plane;
	struct drm_atomic_state *state;
	struct intel_crtc_state *temp_crtc_state;
	enum pipe pipe = crtc->pipe;
	int ret;

	if (!crtc_state->hw.active)
		return;

	for_each_intel_plane_on_crtc(&i915->drm, crtc, plane) {
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane_state->uapi.visible)
			intel_plane_disable_noatomic(crtc, plane);
	}

	state = drm_atomic_state_alloc(&i915->drm);
	if (!state) {
		drm_dbg_kms(&i915->drm,
			    "failed to disable [CRTC:%d:%s], out of memory",
			    crtc->base.base.id, crtc->base.name);
		return;
	}

	state->acquire_ctx = ctx;

	/* Everything's already locked, -EDEADLK can't happen. */
	temp_crtc_state = intel_atomic_get_crtc_state(state, crtc);
	ret = drm_atomic_add_affected_connectors(state, &crtc->base);

	drm_WARN_ON(&i915->drm, IS_ERR(temp_crtc_state) || ret);

	i915->display.funcs.display->crtc_disable(to_intel_atomic_state(state), crtc);

	drm_atomic_state_put(state);

	drm_dbg_kms(&i915->drm,
		    "[CRTC:%d:%s] hw state adjusted, was enabled, now disabled\n",
		    crtc->base.base.id, crtc->base.name);

	crtc->active = false;
	crtc->base.enabled = false;

	drm_WARN_ON(&i915->drm,
		    drm_atomic_set_mode_for_crtc(&crtc_state->uapi, NULL) < 0);
	crtc_state->uapi.active = false;
	crtc_state->uapi.connector_mask = 0;
	crtc_state->uapi.encoder_mask = 0;
	intel_crtc_free_hw_state(crtc_state);
	memset(&crtc_state->hw, 0, sizeof(crtc_state->hw));

	for_each_encoder_on_crtc(&i915->drm, &crtc->base, encoder)
		encoder->base.crtc = NULL;

	intel_fbc_disable(crtc);
	intel_update_watermarks(i915);
	intel_disable_shared_dpll(crtc_state);

	intel_display_power_put_all_in_set(i915, &crtc->enabled_power_domains);

	cdclk_state->min_cdclk[pipe] = 0;
	cdclk_state->min_voltage_level[pipe] = 0;
	cdclk_state->active_pipes &= ~BIT(pipe);

	dbuf_state->active_pipes &= ~BIT(pipe);

	bw_state->data_rate[pipe] = 0;
	bw_state->num_active_planes[pipe] = 0;
}

static void intel_modeset_update_connector_atomic_state(struct drm_i915_private *i915)
{
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *conn_state = connector->base.state;
		struct intel_encoder *encoder =
			to_intel_encoder(connector->base.encoder);

		if (conn_state->crtc)
			drm_connector_put(&connector->base);

		if (encoder) {
			struct intel_crtc *crtc =
				to_intel_crtc(encoder->base.crtc);
			const struct intel_crtc_state *crtc_state =
				to_intel_crtc_state(crtc->base.state);

			conn_state->best_encoder = &encoder->base;
			conn_state->crtc = &crtc->base;
			conn_state->max_bpc = (crtc_state->pipe_bpp ?: 24) / 3;

			drm_connector_get(&connector->base);
		} else {
			conn_state->best_encoder = NULL;
			conn_state->crtc = NULL;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
}

static void intel_crtc_copy_hw_to_uapi_state(struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_is_bigjoiner_slave(crtc_state))
		return;

	crtc_state->uapi.enable = crtc_state->hw.enable;
	crtc_state->uapi.active = crtc_state->hw.active;
	drm_WARN_ON(crtc_state->uapi.crtc->dev,
		    drm_atomic_set_mode_for_crtc(&crtc_state->uapi, &crtc_state->hw.mode) < 0);

	crtc_state->uapi.adjusted_mode = crtc_state->hw.adjusted_mode;
	crtc_state->uapi.scaling_filter = crtc_state->hw.scaling_filter;

	/* assume 1:1 mapping */
	drm_property_replace_blob(&crtc_state->hw.degamma_lut,
				  crtc_state->pre_csc_lut);
	drm_property_replace_blob(&crtc_state->hw.gamma_lut,
				  crtc_state->post_csc_lut);

	drm_property_replace_blob(&crtc_state->uapi.degamma_lut,
				  crtc_state->hw.degamma_lut);
	drm_property_replace_blob(&crtc_state->uapi.gamma_lut,
				  crtc_state->hw.gamma_lut);
	drm_property_replace_blob(&crtc_state->uapi.ctm,
				  crtc_state->hw.ctm);
}

static void
intel_sanitize_plane_mapping(struct drm_i915_private *i915)
{
	struct intel_crtc *crtc;

	if (DISPLAY_VER(i915) >= 4)
		return;

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		struct intel_crtc *plane_crtc;
		enum pipe pipe;

		if (!plane->get_hw_state(plane, &pipe))
			continue;

		if (pipe == crtc->pipe)
			continue;

		drm_dbg_kms(&i915->drm,
			    "[PLANE:%d:%s] attached to the wrong pipe, disabling plane\n",
			    plane->base.base.id, plane->base.name);

		plane_crtc = intel_crtc_for_pipe(i915, pipe);
		intel_plane_disable_noatomic(plane_crtc, plane);
	}
}

static bool intel_crtc_has_encoders(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder)
		return true;

	return false;
}

static struct intel_connector *intel_encoder_find_connector(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	struct intel_connector *found_connector = NULL;

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (&encoder->base == connector->base.encoder) {
			found_connector = connector;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return found_connector;
}

static void intel_sanitize_fifo_underrun_reporting(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (!crtc_state->hw.active && !HAS_GMCH(i915))
		return;

	/*
	 * We start out with underrun reporting disabled to avoid races.
	 * For correct bookkeeping mark this on active crtcs.
	 *
	 * Also on gmch platforms we dont have any hardware bits to
	 * disable the underrun reporting. Which means we need to start
	 * out with underrun reporting disabled also on inactive pipes,
	 * since otherwise we'll complain about the garbage we read when
	 * e.g. coming up after runtime pm.
	 *
	 * No protection against concurrent access is required - at
	 * worst a fifo underrun happens which also sets this to false.
	 */
	crtc->cpu_fifo_underrun_disabled = true;

	/*
	 * We track the PCH trancoder underrun reporting state
	 * within the crtc. With crtc for pipe A housing the underrun
	 * reporting state for PCH transcoder A, crtc for pipe B housing
	 * it for PCH transcoder B, etc. LPT-H has only PCH transcoder A,
	 * and marking underrun reporting as disabled for the non-existing
	 * PCH transcoders B and C would prevent enabling the south
	 * error interrupt (see cpt_can_enable_serr_int()).
	 */
	if (intel_has_pch_trancoder(i915, crtc->pipe))
		crtc->pch_fifo_underrun_disabled = true;
}

static void intel_sanitize_crtc(struct intel_crtc *crtc,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_crtc_state *crtc_state = to_intel_crtc_state(crtc->base.state);

	if (crtc_state->hw.active) {
		struct intel_plane *plane;

		/* Disable everything but the primary plane */
		for_each_intel_plane_on_crtc(&i915->drm, crtc, plane) {
			const struct intel_plane_state *plane_state =
				to_intel_plane_state(plane->base.state);

			if (plane_state->uapi.visible &&
			    plane->base.type != DRM_PLANE_TYPE_PRIMARY)
				intel_plane_disable_noatomic(crtc, plane);
		}

		/* Disable any background color/etc. set by the BIOS */
		intel_color_commit_noarm(crtc_state);
		intel_color_commit_arm(crtc_state);
	}

	/*
	 * Adjust the state of the output pipe according to whether we have
	 * active connectors/encoders.
	 */
	if (crtc_state->hw.active && !intel_crtc_has_encoders(crtc) &&
	    !intel_crtc_is_bigjoiner_slave(crtc_state))
		intel_crtc_disable_noatomic(crtc, ctx);
}

static bool has_bogus_dpll_config(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	/*
	 * Some SNB BIOSen (eg. ASUS K53SV) are known to misprogram
	 * the hardware when a high res displays plugged in. DPLL P
	 * divider is zero, and the pipe timings are bonkers. We'll
	 * try to disable everything in that case.
	 *
	 * FIXME would be nice to be able to sanitize this state
	 * without several WARNs, but for now let's take the easy
	 * road.
	 */
	return IS_SANDYBRIDGE(i915) &&
		crtc_state->hw.active &&
		crtc_state->shared_dpll &&
		crtc_state->port_clock == 0;
}

static void intel_sanitize_encoder(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_connector *connector;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_crtc_state *crtc_state = crtc ?
		to_intel_crtc_state(crtc->base.state) : NULL;

	/*
	 * We need to check both for a crtc link (meaning that the encoder is
	 * active and trying to read from a pipe) and the pipe itself being
	 * active.
	 */
	bool has_active_crtc = crtc_state &&
		crtc_state->hw.active;

	if (crtc_state && has_bogus_dpll_config(crtc_state)) {
		drm_dbg_kms(&i915->drm,
			    "BIOS has misprogrammed the hardware. Disabling pipe %c\n",
			    pipe_name(crtc->pipe));
		has_active_crtc = false;
	}

	connector = intel_encoder_find_connector(encoder);
	if (connector && !has_active_crtc) {
		drm_dbg_kms(&i915->drm,
			    "[ENCODER:%d:%s] has active connectors but no active pipe!\n",
			    encoder->base.base.id,
			    encoder->base.name);

		/*
		 * Connector is active, but has no active pipe. This is fallout
		 * from our resume register restoring. Disable the encoder
		 * manually again.
		 */
		if (crtc_state) {
			struct drm_encoder *best_encoder;

			drm_dbg_kms(&i915->drm,
				    "[ENCODER:%d:%s] manually disabled\n",
				    encoder->base.base.id,
				    encoder->base.name);

			/* avoid oopsing in case the hooks consult best_encoder */
			best_encoder = connector->base.state->best_encoder;
			connector->base.state->best_encoder = &encoder->base;

			/* FIXME NULL atomic state passed! */
			if (encoder->disable)
				encoder->disable(NULL, encoder, crtc_state,
						 connector->base.state);
			if (encoder->post_disable)
				encoder->post_disable(NULL, encoder, crtc_state,
						      connector->base.state);

			connector->base.state->best_encoder = best_encoder;
		}
		encoder->base.crtc = NULL;

		/*
		 * Inconsistent output/port/pipe state happens presumably due to
		 * a bug in one of the get_hw_state functions. Or someplace else
		 * in our code, like the register restore mess on resume. Clamp
		 * things to off as a safer default.
		 */
		connector->base.dpms = DRM_MODE_DPMS_OFF;
		connector->base.encoder = NULL;
	}

	/* notify opregion of the sanitized encoder state */
	intel_opregion_notify_encoder(encoder, connector && has_active_crtc);

	if (HAS_DDI(i915))
		intel_ddi_sanitize_encoder_pll_mapping(encoder);
}

/* FIXME read out full plane state for all planes */
static void readout_plane_state(struct drm_i915_private *i915)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	for_each_intel_plane(&i915->drm, plane) {
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct intel_crtc_state *crtc_state;
		enum pipe pipe = PIPE_A;
		bool visible;

		visible = plane->get_hw_state(plane, &pipe);

		crtc = intel_crtc_for_pipe(i915, pipe);
		crtc_state = to_intel_crtc_state(crtc->base.state);

		intel_set_plane_visible(crtc_state, plane_state, visible);

		drm_dbg_kms(&i915->drm,
			    "[PLANE:%d:%s] hw state readout: %s, pipe %c\n",
			    plane->base.base.id, plane->base.name,
			    str_enabled_disabled(visible), pipe_name(pipe));
	}

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_plane_fixup_bitmasks(crtc_state);
	}
}

static void intel_modeset_readout_hw_state(struct drm_i915_private *i915)
{
	struct intel_cdclk_state *cdclk_state =
		to_intel_cdclk_state(i915->display.cdclk.obj.state);
	struct intel_dbuf_state *dbuf_state =
		to_intel_dbuf_state(i915->display.dbuf.obj.state);
	enum pipe pipe;
	struct intel_crtc *crtc;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u8 active_pipes = 0;

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		__drm_atomic_helper_crtc_destroy_state(&crtc_state->uapi);
		intel_crtc_free_hw_state(crtc_state);
		intel_crtc_state_reset(crtc_state, crtc);

		intel_crtc_get_pipe_config(crtc_state);

		crtc_state->hw.enable = crtc_state->hw.active;

		crtc->base.enabled = crtc_state->hw.enable;
		crtc->active = crtc_state->hw.active;

		if (crtc_state->hw.active)
			active_pipes |= BIT(crtc->pipe);

		drm_dbg_kms(&i915->drm,
			    "[CRTC:%d:%s] hw state readout: %s\n",
			    crtc->base.base.id, crtc->base.name,
			    str_enabled_disabled(crtc_state->hw.active));
	}

	cdclk_state->active_pipes = active_pipes;
	dbuf_state->active_pipes = active_pipes;

	readout_plane_state(i915);

	for_each_intel_encoder(&i915->drm, encoder) {
		struct intel_crtc_state *crtc_state = NULL;

		pipe = 0;

		if (encoder->get_hw_state(encoder, &pipe)) {
			crtc = intel_crtc_for_pipe(i915, pipe);
			crtc_state = to_intel_crtc_state(crtc->base.state);

			encoder->base.crtc = &crtc->base;
			intel_encoder_get_config(encoder, crtc_state);

			/* read out to slave crtc as well for bigjoiner */
			if (crtc_state->bigjoiner_pipes) {
				struct intel_crtc *slave_crtc;

				/* encoder should read be linked to bigjoiner master */
				WARN_ON(intel_crtc_is_bigjoiner_slave(crtc_state));

				for_each_intel_crtc_in_pipe_mask(&i915->drm, slave_crtc,
								 intel_crtc_bigjoiner_slave_pipes(crtc_state)) {
					struct intel_crtc_state *slave_crtc_state;

					slave_crtc_state = to_intel_crtc_state(slave_crtc->base.state);
					intel_encoder_get_config(encoder, slave_crtc_state);
				}
			}
		} else {
			encoder->base.crtc = NULL;
		}

		if (encoder->sync_state)
			encoder->sync_state(encoder, crtc_state);

		drm_dbg_kms(&i915->drm,
			    "[ENCODER:%d:%s] hw state readout: %s, pipe %c\n",
			    encoder->base.base.id, encoder->base.name,
			    str_enabled_disabled(encoder->base.crtc),
			    pipe_name(pipe));
	}

	intel_dpll_readout_hw_state(i915);

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (connector->get_hw_state(connector)) {
			struct intel_crtc_state *crtc_state;
			struct intel_crtc *crtc;

			connector->base.dpms = DRM_MODE_DPMS_ON;

			encoder = intel_attached_encoder(connector);
			connector->base.encoder = &encoder->base;

			crtc = to_intel_crtc(encoder->base.crtc);
			crtc_state = crtc ? to_intel_crtc_state(crtc->base.state) : NULL;

			if (crtc_state && crtc_state->hw.active) {
				/*
				 * This has to be done during hardware readout
				 * because anything calling .crtc_disable may
				 * rely on the connector_mask being accurate.
				 */
				crtc_state->uapi.connector_mask |=
					drm_connector_mask(&connector->base);
				crtc_state->uapi.encoder_mask |=
					drm_encoder_mask(&encoder->base);
			}
		} else {
			connector->base.dpms = DRM_MODE_DPMS_OFF;
			connector->base.encoder = NULL;
		}
		drm_dbg_kms(&i915->drm,
			    "[CONNECTOR:%d:%s] hw state readout: %s\n",
			    connector->base.base.id, connector->base.name,
			    str_enabled_disabled(connector->base.encoder));
	}
	drm_connector_list_iter_end(&conn_iter);

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_bw_state *bw_state =
			to_intel_bw_state(i915->display.bw.obj.state);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane *plane;
		int min_cdclk = 0;

		if (crtc_state->hw.active) {
			/*
			 * The initial mode needs to be set in order to keep
			 * the atomic core happy. It wants a valid mode if the
			 * crtc's enabled, so we do the above call.
			 *
			 * But we don't set all the derived state fully, hence
			 * set a flag to indicate that a full recalculation is
			 * needed on the next commit.
			 */
			crtc_state->inherited = true;

			intel_crtc_update_active_timings(crtc_state);

			intel_crtc_copy_hw_to_uapi_state(crtc_state);
		}

		for_each_intel_plane_on_crtc(&i915->drm, crtc, plane) {
			const struct intel_plane_state *plane_state =
				to_intel_plane_state(plane->base.state);

			/*
			 * FIXME don't have the fb yet, so can't
			 * use intel_plane_data_rate() :(
			 */
			if (plane_state->uapi.visible)
				crtc_state->data_rate[plane->id] =
					4 * crtc_state->pixel_rate;
			/*
			 * FIXME don't have the fb yet, so can't
			 * use plane->min_cdclk() :(
			 */
			if (plane_state->uapi.visible && plane->min_cdclk) {
				if (crtc_state->double_wide || DISPLAY_VER(i915) >= 10)
					crtc_state->min_cdclk[plane->id] =
						DIV_ROUND_UP(crtc_state->pixel_rate, 2);
				else
					crtc_state->min_cdclk[plane->id] =
						crtc_state->pixel_rate;
			}
			drm_dbg_kms(&i915->drm,
				    "[PLANE:%d:%s] min_cdclk %d kHz\n",
				    plane->base.base.id, plane->base.name,
				    crtc_state->min_cdclk[plane->id]);
		}

		if (crtc_state->hw.active) {
			min_cdclk = intel_crtc_compute_min_cdclk(crtc_state);
			if (drm_WARN_ON(&i915->drm, min_cdclk < 0))
				min_cdclk = 0;
		}

		cdclk_state->min_cdclk[crtc->pipe] = min_cdclk;
		cdclk_state->min_voltage_level[crtc->pipe] =
			crtc_state->min_voltage_level;

		intel_bw_crtc_update(bw_state, crtc_state);
	}
}

static void
get_encoder_power_domains(struct drm_i915_private *i915)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&i915->drm, encoder) {
		struct intel_crtc_state *crtc_state;

		if (!encoder->get_power_domains)
			continue;

		/*
		 * MST-primary and inactive encoders don't have a crtc state
		 * and neither of these require any power domain references.
		 */
		if (!encoder->base.crtc)
			continue;

		crtc_state = to_intel_crtc_state(encoder->base.crtc->state);
		encoder->get_power_domains(encoder, crtc_state);
	}
}

static void intel_early_display_was(struct drm_i915_private *i915)
{
	/*
	 * Display WA #1185 WaDisableDARBFClkGating:glk,icl,ehl,tgl
	 * Also known as Wa_14010480278.
	 */
	if (IS_DISPLAY_VER(i915, 10, 12))
		intel_de_write(i915, GEN9_CLKGATE_DIS_0,
			       intel_de_read(i915, GEN9_CLKGATE_DIS_0) | DARBF_GATING_DIS);

	if (IS_HASWELL(i915)) {
		/*
		 * WaRsPkgCStateDisplayPMReq:hsw
		 * System hang if this isn't done before disabling all planes!
		 */
		intel_de_write(i915, CHICKEN_PAR1_1,
			       intel_de_read(i915, CHICKEN_PAR1_1) | FORCE_ARB_IDLE_PLANES);
	}

	if (IS_KABYLAKE(i915) || IS_COFFEELAKE(i915) || IS_COMETLAKE(i915)) {
		/* Display WA #1142:kbl,cfl,cml */
		intel_de_rmw(i915, CHICKEN_PAR1_1,
			     KBL_ARB_FILL_SPARE_22, KBL_ARB_FILL_SPARE_22);
		intel_de_rmw(i915, CHICKEN_MISC_2,
			     KBL_ARB_FILL_SPARE_13 | KBL_ARB_FILL_SPARE_14,
			     KBL_ARB_FILL_SPARE_14);
	}
}

void intel_modeset_setup_hw_state(struct drm_i915_private *i915,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	intel_wakeref_t wakeref;

	wakeref = intel_display_power_get(i915, POWER_DOMAIN_INIT);

	intel_early_display_was(i915);
	intel_modeset_readout_hw_state(i915);

	/* HW state is read out, now we need to sanitize this mess. */
	get_encoder_power_domains(i915);

	intel_pch_sanitize(i915);

	/*
	 * intel_sanitize_plane_mapping() may need to do vblank
	 * waits, so we need vblank interrupts restored beforehand.
	 */
	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_sanitize_fifo_underrun_reporting(crtc_state);

		drm_crtc_vblank_reset(&crtc->base);

		if (crtc_state->hw.active) {
			intel_dmc_enable_pipe(i915, crtc->pipe);
			intel_crtc_vblank_on(crtc_state);
		}
	}

	intel_fbc_sanitize(i915);

	intel_sanitize_plane_mapping(i915);

	for_each_intel_encoder(&i915->drm, encoder)
		intel_sanitize_encoder(encoder);

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		intel_sanitize_crtc(crtc, ctx);
		intel_crtc_state_dump(crtc_state, NULL, "setup_hw_state");
	}

	intel_modeset_update_connector_atomic_state(i915);

	intel_dpll_sanitize_state(i915);

	if (IS_G4X(i915)) {
		g4x_wm_get_hw_state(i915);
		g4x_wm_sanitize(i915);
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		vlv_wm_get_hw_state(i915);
		vlv_wm_sanitize(i915);
	} else if (DISPLAY_VER(i915) >= 9) {
		skl_wm_get_hw_state(i915);
		skl_wm_sanitize(i915);
	} else if (HAS_PCH_SPLIT(i915)) {
		ilk_wm_get_hw_state(i915);
	}

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_power_domain_mask put_domains;

		intel_modeset_get_crtc_power_domains(crtc_state, &put_domains);
		if (drm_WARN_ON(&i915->drm, !bitmap_empty(put_domains.bits, POWER_DOMAIN_NUM)))
			intel_modeset_put_crtc_power_domains(crtc, &put_domains);
	}

	intel_display_power_put(i915, POWER_DOMAIN_INIT, wakeref);

	intel_power_domains_sanitize_state(i915);
}
