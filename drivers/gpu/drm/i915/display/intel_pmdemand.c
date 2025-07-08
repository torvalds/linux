// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/bitops.h>

#include <drm/drm_print.h>

#include "i915_utils.h"
#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_trace.h"
#include "intel_pmdemand.h"
#include "intel_step.h"
#include "skl_watermark.h"

struct pmdemand_params {
	u16 qclk_gv_bw;
	u8 voltage_index;
	u8 qclk_gv_index;
	u8 active_pipes;
	u8 active_dbufs;	/* pre-Xe3 only */
	/* Total number of non type C active phys from active_phys_mask */
	u8 active_phys;
	u8 plls;
	u16 cdclk_freq_mhz;
	/* max from ddi_clocks[] */
	u16 ddiclk_max;
	u8 scalers;		/* pre-Xe3 only */
};

struct intel_pmdemand_state {
	struct intel_global_state base;

	/* Maintain a persistent list of port clocks across all crtcs */
	int ddi_clocks[I915_MAX_PIPES];

	/* Maintain a persistent list of non type C phys mask */
	u16 active_combo_phys_mask;

	/* Parameters to be configured in the pmdemand registers */
	struct pmdemand_params params;
};

struct intel_pmdemand_state *to_intel_pmdemand_state(struct intel_global_state *obj_state)
{
	return container_of(obj_state, struct intel_pmdemand_state, base);
}

static struct intel_global_state *
intel_pmdemand_duplicate_state(struct intel_global_obj *obj)
{
	struct intel_pmdemand_state *pmdemand_state;

	pmdemand_state = kmemdup(obj->state, sizeof(*pmdemand_state), GFP_KERNEL);
	if (!pmdemand_state)
		return NULL;

	return &pmdemand_state->base;
}

static void intel_pmdemand_destroy_state(struct intel_global_obj *obj,
					 struct intel_global_state *state)
{
	kfree(state);
}

static const struct intel_global_state_funcs intel_pmdemand_funcs = {
	.atomic_duplicate_state = intel_pmdemand_duplicate_state,
	.atomic_destroy_state = intel_pmdemand_destroy_state,
};

static struct intel_pmdemand_state *
intel_atomic_get_pmdemand_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_global_obj_state(state,
						  &display->pmdemand.obj);

	if (IS_ERR(pmdemand_state))
		return ERR_CAST(pmdemand_state);

	return to_intel_pmdemand_state(pmdemand_state);
}

static struct intel_pmdemand_state *
intel_atomic_get_old_pmdemand_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_old_global_obj_state(state,
						      &display->pmdemand.obj);

	if (!pmdemand_state)
		return NULL;

	return to_intel_pmdemand_state(pmdemand_state);
}

static struct intel_pmdemand_state *
intel_atomic_get_new_pmdemand_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_new_global_obj_state(state,
						      &display->pmdemand.obj);

	if (!pmdemand_state)
		return NULL;

	return to_intel_pmdemand_state(pmdemand_state);
}

int intel_pmdemand_init(struct intel_display *display)
{
	struct intel_pmdemand_state *pmdemand_state;

	pmdemand_state = kzalloc(sizeof(*pmdemand_state), GFP_KERNEL);
	if (!pmdemand_state)
		return -ENOMEM;

	intel_atomic_global_obj_init(display, &display->pmdemand.obj,
				     &pmdemand_state->base,
				     &intel_pmdemand_funcs);

	if (IS_DISPLAY_VERx100_STEP(display, 1400, STEP_A0, STEP_C0))
		/* Wa_14016740474 */
		intel_de_rmw(display, XELPD_CHICKEN_DCPR_3, 0, DMD_RSP_TIMEOUT_DISABLE);

	return 0;
}

void intel_pmdemand_init_early(struct intel_display *display)
{
	mutex_init(&display->pmdemand.lock);
	init_waitqueue_head(&display->pmdemand.waitqueue);
}

void
intel_pmdemand_update_phys_mask(struct intel_display *display,
				struct intel_encoder *encoder,
				struct intel_pmdemand_state *pmdemand_state,
				bool set_bit)
{
	enum phy phy;

	if (DISPLAY_VER(display) < 14)
		return;

	if (!encoder)
		return;

	if (intel_encoder_is_tc(encoder))
		return;

	phy = intel_encoder_to_phy(encoder);

	if (set_bit)
		pmdemand_state->active_combo_phys_mask |= BIT(phy);
	else
		pmdemand_state->active_combo_phys_mask &= ~BIT(phy);
}

void
intel_pmdemand_update_port_clock(struct intel_display *display,
				 struct intel_pmdemand_state *pmdemand_state,
				 enum pipe pipe, int port_clock)
{
	if (DISPLAY_VER(display) < 14)
		return;

	pmdemand_state->ddi_clocks[pipe] = port_clock;
}

static void
intel_pmdemand_update_max_ddiclk(struct intel_display *display,
				 struct intel_atomic_state *state,
				 struct intel_pmdemand_state *pmdemand_state)
{
	int max_ddiclk = 0;
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		intel_pmdemand_update_port_clock(display, pmdemand_state,
						 crtc->pipe,
						 new_crtc_state->port_clock);

	for (i = 0; i < ARRAY_SIZE(pmdemand_state->ddi_clocks); i++)
		max_ddiclk = max(pmdemand_state->ddi_clocks[i], max_ddiclk);

	pmdemand_state->params.ddiclk_max = DIV_ROUND_UP(max_ddiclk, 1000);
}

static void
intel_pmdemand_update_connector_phys(struct intel_display *display,
				     struct intel_atomic_state *state,
				     struct drm_connector_state *conn_state,
				     bool set_bit,
				     struct intel_pmdemand_state *pmdemand_state)
{
	struct intel_encoder *encoder = to_intel_encoder(conn_state->best_encoder);
	struct intel_crtc *crtc = to_intel_crtc(conn_state->crtc);
	struct intel_crtc_state *crtc_state;

	if (!crtc)
		return;

	if (set_bit)
		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	else
		crtc_state = intel_atomic_get_old_crtc_state(state, crtc);

	if (!crtc_state->hw.active)
		return;

	intel_pmdemand_update_phys_mask(display, encoder, pmdemand_state,
					set_bit);
}

static void
intel_pmdemand_update_active_non_tc_phys(struct intel_display *display,
					 struct intel_atomic_state *state,
					 struct intel_pmdemand_state *pmdemand_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, connector,
					   old_conn_state, new_conn_state, i) {
		if (!intel_connector_needs_modeset(state, connector))
			continue;

		/* First clear the active phys in the old connector state */
		intel_pmdemand_update_connector_phys(display, state,
						     old_conn_state, false,
						     pmdemand_state);

		/* Then set the active phys in new connector state */
		intel_pmdemand_update_connector_phys(display, state,
						     new_conn_state, true,
						     pmdemand_state);
	}

	pmdemand_state->params.active_phys =
		min_t(u16, hweight16(pmdemand_state->active_combo_phys_mask),
		      7);
}

static bool
intel_pmdemand_encoder_has_tc_phy(struct intel_display *display,
				  struct intel_encoder *encoder)
{
	return encoder && intel_encoder_is_tc(encoder);
}

static bool
intel_pmdemand_connector_needs_update(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, connector,
					   old_conn_state, new_conn_state, i) {
		struct intel_encoder *old_encoder =
			to_intel_encoder(old_conn_state->best_encoder);
		struct intel_encoder *new_encoder =
			to_intel_encoder(new_conn_state->best_encoder);

		if (!intel_connector_needs_modeset(state, connector))
			continue;

		if (old_encoder == new_encoder ||
		    (intel_pmdemand_encoder_has_tc_phy(display, old_encoder) &&
		     intel_pmdemand_encoder_has_tc_phy(display, new_encoder)))
			continue;

		return true;
	}

	return false;
}

static bool intel_pmdemand_needs_update(struct intel_atomic_state *state)
{
	const struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_crtc *crtc;
	int i;

	if (intel_bw_pmdemand_needs_update(state))
		return true;

	if (intel_dbuf_pmdemand_needs_update(state))
		return true;

	if (intel_cdclk_pmdemand_needs_update(state))
		return true;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i)
		if (new_crtc_state->port_clock != old_crtc_state->port_clock)
			return true;

	return intel_pmdemand_connector_needs_update(state);
}

int intel_pmdemand_atomic_check(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_bw_state *new_bw_state;
	const struct intel_cdclk_state *new_cdclk_state;
	const struct intel_dbuf_state *new_dbuf_state;
	struct intel_pmdemand_state *new_pmdemand_state;

	if (DISPLAY_VER(display) < 14)
		return 0;

	if (!intel_pmdemand_needs_update(state))
		return 0;

	new_pmdemand_state = intel_atomic_get_pmdemand_state(state);
	if (IS_ERR(new_pmdemand_state))
		return PTR_ERR(new_pmdemand_state);

	new_bw_state = intel_atomic_get_bw_state(state);
	if (IS_ERR(new_bw_state))
		return PTR_ERR(new_bw_state);

	/* firmware will calculate the qclk_gv_index, requirement is set to 0 */
	new_pmdemand_state->params.qclk_gv_index = 0;
	new_pmdemand_state->params.qclk_gv_bw = intel_bw_qgv_point_peakbw(new_bw_state);

	new_dbuf_state = intel_atomic_get_dbuf_state(state);
	if (IS_ERR(new_dbuf_state))
		return PTR_ERR(new_dbuf_state);

	if (DISPLAY_VER(display) < 30) {
		new_pmdemand_state->params.active_dbufs =
			min_t(u8, intel_dbuf_num_enabled_slices(new_dbuf_state), 3);
		new_pmdemand_state->params.active_pipes =
			min_t(u8, intel_dbuf_num_active_pipes(new_dbuf_state), 3);
	} else {
		new_pmdemand_state->params.active_pipes =
			min_t(u8, intel_dbuf_num_active_pipes(new_dbuf_state), INTEL_NUM_PIPES(display));
	}

	new_cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(new_cdclk_state))
		return PTR_ERR(new_cdclk_state);

	new_pmdemand_state->params.voltage_index =
		intel_cdclk_actual_voltage_level(new_cdclk_state);
	new_pmdemand_state->params.cdclk_freq_mhz =
		DIV_ROUND_UP(intel_cdclk_actual(new_cdclk_state), 1000);

	intel_pmdemand_update_max_ddiclk(display, state, new_pmdemand_state);

	intel_pmdemand_update_active_non_tc_phys(display, state, new_pmdemand_state);

	/*
	 * Active_PLLs starts with 1 because of CDCLK PLL.
	 * TODO: Missing to account genlock filter when it gets used.
	 */
	new_pmdemand_state->params.plls =
		min_t(u16, new_pmdemand_state->params.active_phys + 1, 7);

	/*
	 * Setting scalers to max as it can not be calculated during flips and
	 * fastsets without taking global states locks.
	 */
	new_pmdemand_state->params.scalers = 7;

	if (state->base.allow_modeset)
		return intel_atomic_serialize_global_state(&new_pmdemand_state->base);
	else
		return intel_atomic_lock_global_state(&new_pmdemand_state->base);
}

static bool intel_pmdemand_check_prev_transaction(struct intel_display *display)
{
	return !(intel_de_wait_for_clear(display,
					 XELPDP_INITIATE_PMDEMAND_REQUEST(1),
					 XELPDP_PMDEMAND_REQ_ENABLE, 10) ||
		 intel_de_wait_for_clear(display,
					 GEN12_DCPR_STATUS_1,
					 XELPDP_PMDEMAND_INFLIGHT_STATUS, 10));
}

void
intel_pmdemand_init_pmdemand_params(struct intel_display *display,
				    struct intel_pmdemand_state *pmdemand_state)
{
	u32 reg1, reg2;

	if (DISPLAY_VER(display) < 14)
		return;

	mutex_lock(&display->pmdemand.lock);
	if (drm_WARN_ON(display->drm,
			!intel_pmdemand_check_prev_transaction(display))) {
		memset(&pmdemand_state->params, 0,
		       sizeof(pmdemand_state->params));
		goto unlock;
	}

	reg1 = intel_de_read(display, XELPDP_INITIATE_PMDEMAND_REQUEST(0));

	reg2 = intel_de_read(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1));

	pmdemand_state->params.qclk_gv_bw =
		REG_FIELD_GET(XELPDP_PMDEMAND_QCLK_GV_BW_MASK, reg1);
	pmdemand_state->params.voltage_index =
		REG_FIELD_GET(XELPDP_PMDEMAND_VOLTAGE_INDEX_MASK, reg1);
	pmdemand_state->params.qclk_gv_index =
		REG_FIELD_GET(XELPDP_PMDEMAND_QCLK_GV_INDEX_MASK, reg1);
	pmdemand_state->params.active_phys =
		REG_FIELD_GET(XELPDP_PMDEMAND_PHYS_MASK, reg1);

	pmdemand_state->params.cdclk_freq_mhz =
		REG_FIELD_GET(XELPDP_PMDEMAND_CDCLK_FREQ_MASK, reg2);
	pmdemand_state->params.ddiclk_max =
		REG_FIELD_GET(XELPDP_PMDEMAND_DDICLK_FREQ_MASK, reg2);

	if (DISPLAY_VER(display) >= 30) {
		pmdemand_state->params.active_pipes =
			REG_FIELD_GET(XE3_PMDEMAND_PIPES_MASK, reg1);
	} else {
		pmdemand_state->params.active_pipes =
			REG_FIELD_GET(XELPDP_PMDEMAND_PIPES_MASK, reg1);
		pmdemand_state->params.active_dbufs =
			REG_FIELD_GET(XELPDP_PMDEMAND_DBUFS_MASK, reg1);

		pmdemand_state->params.scalers =
			REG_FIELD_GET(XELPDP_PMDEMAND_SCALERS_MASK, reg2);
	}

unlock:
	mutex_unlock(&display->pmdemand.lock);
}

static bool intel_pmdemand_req_complete(struct intel_display *display)
{
	return !(intel_de_read(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1)) &
		 XELPDP_PMDEMAND_REQ_ENABLE);
}

static void intel_pmdemand_poll(struct intel_display *display)
{
	const unsigned int timeout_ms = 10;
	u32 status;
	int ret;

	ret = intel_de_wait_custom(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1),
				   XELPDP_PMDEMAND_REQ_ENABLE, 0,
				   50, timeout_ms, &status);

	if (ret == -ETIMEDOUT)
		drm_err(display->drm,
			"timed out waiting for Punit PM Demand Response within %ums (status 0x%08x)\n",
			timeout_ms, status);
}

static void intel_pmdemand_wait(struct intel_display *display)
{
	/* Wa_14024400148 For lnl use polling method */
	if (DISPLAY_VER(display) == 20) {
		intel_pmdemand_poll(display);
	} else {
		if (!wait_event_timeout(display->pmdemand.waitqueue,
					intel_pmdemand_req_complete(display),
					msecs_to_jiffies_timeout(10)))
			drm_err(display->drm,
				"timed out waiting for Punit PM Demand Response\n");
	}
}

/* Required to be programmed during Display Init Sequences. */
void intel_pmdemand_program_dbuf(struct intel_display *display,
				 u8 dbuf_slices)
{
	u32 dbufs = min_t(u32, hweight8(dbuf_slices), 3);

	/* PM Demand only tracks active dbufs on pre-Xe3 platforms */
	if (DISPLAY_VER(display) >= 30)
		return;

	mutex_lock(&display->pmdemand.lock);
	if (drm_WARN_ON(display->drm,
			!intel_pmdemand_check_prev_transaction(display)))
		goto unlock;

	intel_de_rmw(display, XELPDP_INITIATE_PMDEMAND_REQUEST(0),
		     XELPDP_PMDEMAND_DBUFS_MASK,
		     REG_FIELD_PREP(XELPDP_PMDEMAND_DBUFS_MASK, dbufs));
	intel_de_rmw(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1), 0,
		     XELPDP_PMDEMAND_REQ_ENABLE);

	intel_pmdemand_wait(display);

unlock:
	mutex_unlock(&display->pmdemand.lock);
}

static void
intel_pmdemand_update_params(struct intel_display *display,
			     const struct intel_pmdemand_state *new,
			     const struct intel_pmdemand_state *old,
			     u32 *reg1, u32 *reg2, bool serialized)
{
	/*
	 * The pmdemand parameter updates happens in two steps. Pre plane and
	 * post plane updates. During the pre plane, as DE might still be
	 * handling with some old operations, to avoid unexpected performance
	 * issues, program the pmdemand parameters with higher of old and new
	 * values. And then after once settled, use the new parameter values
	 * as part of the post plane update.
	 *
	 * If the pmdemand params update happens without modeset allowed, this
	 * means we can't serialize the updates. So that implies possibility of
	 * some parallel atomic commits affecting the pmdemand parameters. In
	 * that case, we need to consider the current values from the register
	 * as well. So in pre-plane case, we need to check the max of old, new
	 * and current register value if not serialized. In post plane update
	 * we need to consider max of new and current register value if not
	 * serialized
	 */

#define update_reg(reg, field, mask) do { \
	u32 current_val = serialized ? 0 : REG_FIELD_GET((mask), *(reg)); \
	u32 old_val = old ? old->params.field : 0; \
	u32 new_val = new->params.field; \
\
	*(reg) &= ~(mask); \
	*(reg) |= REG_FIELD_PREP((mask), max3(old_val, new_val, current_val)); \
} while (0)

	/* Set 1*/
	update_reg(reg1, qclk_gv_bw, XELPDP_PMDEMAND_QCLK_GV_BW_MASK);
	update_reg(reg1, voltage_index, XELPDP_PMDEMAND_VOLTAGE_INDEX_MASK);
	update_reg(reg1, qclk_gv_index, XELPDP_PMDEMAND_QCLK_GV_INDEX_MASK);
	update_reg(reg1, active_phys, XELPDP_PMDEMAND_PHYS_MASK);

	/* Set 2*/
	update_reg(reg2, cdclk_freq_mhz, XELPDP_PMDEMAND_CDCLK_FREQ_MASK);
	update_reg(reg2, ddiclk_max, XELPDP_PMDEMAND_DDICLK_FREQ_MASK);
	update_reg(reg2, plls, XELPDP_PMDEMAND_PLLS_MASK);

	if (DISPLAY_VER(display) >= 30) {
		update_reg(reg1, active_pipes, XE3_PMDEMAND_PIPES_MASK);
	} else {
		update_reg(reg1, active_pipes, XELPDP_PMDEMAND_PIPES_MASK);
		update_reg(reg1, active_dbufs, XELPDP_PMDEMAND_DBUFS_MASK);

		update_reg(reg2, scalers, XELPDP_PMDEMAND_SCALERS_MASK);
	}

#undef update_reg
}

static void
intel_pmdemand_program_params(struct intel_display *display,
			      const struct intel_pmdemand_state *new,
			      const struct intel_pmdemand_state *old,
			      bool serialized)
{
	bool changed = false;
	u32 reg1, mod_reg1;
	u32 reg2, mod_reg2;

	mutex_lock(&display->pmdemand.lock);
	if (drm_WARN_ON(display->drm,
			!intel_pmdemand_check_prev_transaction(display)))
		goto unlock;

	reg1 = intel_de_read(display, XELPDP_INITIATE_PMDEMAND_REQUEST(0));
	mod_reg1 = reg1;

	reg2 = intel_de_read(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1));
	mod_reg2 = reg2;

	intel_pmdemand_update_params(display, new, old, &mod_reg1, &mod_reg2,
				     serialized);

	if (reg1 != mod_reg1) {
		intel_de_write(display, XELPDP_INITIATE_PMDEMAND_REQUEST(0),
			       mod_reg1);
		changed = true;
	}

	if (reg2 != mod_reg2) {
		intel_de_write(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1),
			       mod_reg2);
		changed = true;
	}

	/* Initiate pm demand request only if register values are changed */
	if (!changed)
		goto unlock;

	drm_dbg_kms(display->drm,
		    "initiate pmdemand request values: (0x%x 0x%x)\n",
		    mod_reg1, mod_reg2);

	intel_de_rmw(display, XELPDP_INITIATE_PMDEMAND_REQUEST(1), 0,
		     XELPDP_PMDEMAND_REQ_ENABLE);

	intel_pmdemand_wait(display);

unlock:
	mutex_unlock(&display->pmdemand.lock);
}

static bool
intel_pmdemand_state_changed(const struct intel_pmdemand_state *new,
			     const struct intel_pmdemand_state *old)
{
	return memcmp(&new->params, &old->params, sizeof(new->params)) != 0;
}

void intel_pmdemand_pre_plane_update(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_pmdemand_state *new_pmdemand_state =
		intel_atomic_get_new_pmdemand_state(state);
	const struct intel_pmdemand_state *old_pmdemand_state =
		intel_atomic_get_old_pmdemand_state(state);

	if (DISPLAY_VER(display) < 14)
		return;

	if (!new_pmdemand_state ||
	    !intel_pmdemand_state_changed(new_pmdemand_state,
					  old_pmdemand_state))
		return;

	WARN_ON(!new_pmdemand_state->base.changed);

	intel_pmdemand_program_params(display, new_pmdemand_state,
				      old_pmdemand_state,
				      intel_atomic_global_state_is_serialized(state));
}

void intel_pmdemand_post_plane_update(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_pmdemand_state *new_pmdemand_state =
		intel_atomic_get_new_pmdemand_state(state);
	const struct intel_pmdemand_state *old_pmdemand_state =
		intel_atomic_get_old_pmdemand_state(state);

	if (DISPLAY_VER(display) < 14)
		return;

	if (!new_pmdemand_state ||
	    !intel_pmdemand_state_changed(new_pmdemand_state,
					  old_pmdemand_state))
		return;

	WARN_ON(!new_pmdemand_state->base.changed);

	intel_pmdemand_program_params(display, new_pmdemand_state, NULL,
				      intel_atomic_global_state_is_serialized(state));
}
