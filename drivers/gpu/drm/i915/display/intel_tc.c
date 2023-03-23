// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_power_map.h"
#include "intel_display_types.h"
#include "intel_dkl_phy_regs.h"
#include "intel_dp_mst.h"
#include "intel_mg_phy_regs.h"
#include "intel_tc.h"

enum tc_port_mode {
	TC_PORT_DISCONNECTED,
	TC_PORT_TBT_ALT,
	TC_PORT_DP_ALT,
	TC_PORT_LEGACY,
};

struct intel_tc_port;

struct intel_tc_phy_ops {
	u32 (*hpd_live_status)(struct intel_tc_port *tc);
	bool (*is_ready)(struct intel_tc_port *tc);
	bool (*is_owned)(struct intel_tc_port *tc);
	void (*get_hw_state)(struct intel_tc_port *tc);
	bool (*connect)(struct intel_tc_port *tc, int required_lanes);
	void (*disconnect)(struct intel_tc_port *tc);
};

struct intel_tc_port {
	struct intel_digital_port *dig_port;

	const struct intel_tc_phy_ops *phy_ops;

	struct mutex lock;	/* protects the TypeC port mode */
	intel_wakeref_t lock_wakeref;
	enum intel_display_power_domain lock_power_domain;
	struct delayed_work disconnect_phy_work;
	int link_refcount;
	bool legacy_port:1;
	char port_name[8];
	enum tc_port_mode mode;
	enum tc_port_mode init_mode;
	enum phy_fia phy_fia;
	u8 phy_fia_idx;
};

static u32 tc_phy_hpd_live_status(struct intel_tc_port *tc);
static bool tc_phy_is_ready(struct intel_tc_port *tc);
static bool tc_phy_take_ownership(struct intel_tc_port *tc, bool take);
static enum tc_port_mode tc_phy_get_current_mode(struct intel_tc_port *tc);

static const char *tc_port_mode_name(enum tc_port_mode mode)
{
	static const char * const names[] = {
		[TC_PORT_DISCONNECTED] = "disconnected",
		[TC_PORT_TBT_ALT] = "tbt-alt",
		[TC_PORT_DP_ALT] = "dp-alt",
		[TC_PORT_LEGACY] = "legacy",
	};

	if (WARN_ON(mode >= ARRAY_SIZE(names)))
		mode = TC_PORT_DISCONNECTED;

	return names[mode];
}

static struct intel_tc_port *to_tc_port(struct intel_digital_port *dig_port)
{
	return dig_port->tc;
}

static struct drm_i915_private *tc_to_i915(struct intel_tc_port *tc)
{
	return to_i915(tc->dig_port->base.base.dev);
}

static bool intel_tc_port_in_mode(struct intel_digital_port *dig_port,
				  enum tc_port_mode mode)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return intel_phy_is_tc(i915, phy) && tc->mode == mode;
}

bool intel_tc_port_in_tbt_alt_mode(struct intel_digital_port *dig_port)
{
	return intel_tc_port_in_mode(dig_port, TC_PORT_TBT_ALT);
}

bool intel_tc_port_in_dp_alt_mode(struct intel_digital_port *dig_port)
{
	return intel_tc_port_in_mode(dig_port, TC_PORT_DP_ALT);
}

bool intel_tc_port_in_legacy_mode(struct intel_digital_port *dig_port)
{
	return intel_tc_port_in_mode(dig_port, TC_PORT_LEGACY);
}

bool intel_tc_cold_requires_aux_pw(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return (DISPLAY_VER(i915) == 11 && tc->legacy_port) ||
		IS_ALDERLAKE_P(i915);
}

static enum intel_display_power_domain
tc_cold_get_power_domain(struct intel_tc_port *tc, enum tc_port_mode mode)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;

	if (mode == TC_PORT_TBT_ALT || !intel_tc_cold_requires_aux_pw(dig_port))
		return POWER_DOMAIN_TC_COLD_OFF;

	return intel_display_power_legacy_aux_domain(i915, dig_port->aux_ch);
}

static intel_wakeref_t
tc_cold_block_in_mode(struct intel_tc_port *tc, enum tc_port_mode mode,
		      enum intel_display_power_domain *domain)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	*domain = tc_cold_get_power_domain(tc, mode);

	return intel_display_power_get(i915, *domain);
}

static intel_wakeref_t
tc_cold_block(struct intel_tc_port *tc, enum intel_display_power_domain *domain)
{
	return tc_cold_block_in_mode(tc, tc->mode, domain);
}

static void
tc_cold_unblock(struct intel_tc_port *tc, enum intel_display_power_domain domain,
		intel_wakeref_t wakeref)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	/*
	 * wakeref == -1, means some error happened saving save_depot_stack but
	 * power should still be put down and 0 is a invalid save_depot_stack
	 * id so can be used to skip it for non TC legacy ports.
	 */
	if (wakeref == 0)
		return;

	intel_display_power_put(i915, domain, wakeref);
}

static void
assert_tc_cold_blocked(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	bool enabled;

	enabled = intel_display_power_is_enabled(i915,
						 tc_cold_get_power_domain(tc,
									  tc->mode));
	drm_WARN_ON(&i915->drm, !enabled);
}

static enum intel_display_power_domain
tc_port_power_domain(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	enum tc_port tc_port = intel_port_to_tc(i915, tc->dig_port->base.port);

	return POWER_DOMAIN_PORT_DDI_LANES_TC1 + tc_port - TC_PORT_1;
}

static void
assert_tc_port_power_enabled(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	drm_WARN_ON(&i915->drm,
		    !intel_display_power_is_enabled(i915, tc_port_power_domain(tc)));
}

u32 intel_tc_port_get_lane_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	u32 lane_mask;

	lane_mask = intel_de_read(i915, PORT_TX_DFLEXDPSP(tc->phy_fia));

	drm_WARN_ON(&i915->drm, lane_mask == 0xffffffff);
	assert_tc_cold_blocked(tc);

	lane_mask &= DP_LANE_ASSIGNMENT_MASK(tc->phy_fia_idx);
	return lane_mask >> DP_LANE_ASSIGNMENT_SHIFT(tc->phy_fia_idx);
}

u32 intel_tc_port_get_pin_assignment_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	u32 pin_mask;

	pin_mask = intel_de_read(i915, PORT_TX_DFLEXPA1(tc->phy_fia));

	drm_WARN_ON(&i915->drm, pin_mask == 0xffffffff);
	assert_tc_cold_blocked(tc);

	return (pin_mask & DP_PIN_ASSIGNMENT_MASK(tc->phy_fia_idx)) >>
	       DP_PIN_ASSIGNMENT_SHIFT(tc->phy_fia_idx);
}

int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);
	intel_wakeref_t wakeref;
	u32 lane_mask;

	if (!intel_phy_is_tc(i915, phy) || tc->mode != TC_PORT_DP_ALT)
		return 4;

	assert_tc_cold_blocked(tc);

	lane_mask = 0;
	with_intel_display_power(i915, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		lane_mask = intel_tc_port_get_lane_mask(dig_port);

	switch (lane_mask) {
	default:
		MISSING_CASE(lane_mask);
		fallthrough;
	case 0x1:
	case 0x2:
	case 0x4:
	case 0x8:
		return 1;
	case 0x3:
	case 0xc:
		return 2;
	case 0xf:
		return 4;
	}
}

void intel_tc_port_set_fia_lane_count(struct intel_digital_port *dig_port,
				      int required_lanes)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	bool lane_reversal = dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;
	u32 val;

	drm_WARN_ON(&i915->drm,
		    lane_reversal && tc->mode != TC_PORT_LEGACY);

	assert_tc_cold_blocked(tc);

	val = intel_de_read(i915, PORT_TX_DFLEXDPMLE1(tc->phy_fia));
	val &= ~DFLEXDPMLE1_DPMLETC_MASK(tc->phy_fia_idx);

	switch (required_lanes) {
	case 1:
		val |= lane_reversal ?
			DFLEXDPMLE1_DPMLETC_ML3(tc->phy_fia_idx) :
			DFLEXDPMLE1_DPMLETC_ML0(tc->phy_fia_idx);
		break;
	case 2:
		val |= lane_reversal ?
			DFLEXDPMLE1_DPMLETC_ML3_2(tc->phy_fia_idx) :
			DFLEXDPMLE1_DPMLETC_ML1_0(tc->phy_fia_idx);
		break;
	case 4:
		val |= DFLEXDPMLE1_DPMLETC_ML3_0(tc->phy_fia_idx);
		break;
	default:
		MISSING_CASE(required_lanes);
	}

	intel_de_write(i915, PORT_TX_DFLEXDPMLE1(tc->phy_fia), val);
}

static void tc_port_fixup_legacy_flag(struct intel_tc_port *tc,
				      u32 live_status_mask)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 valid_hpd_mask;

	drm_WARN_ON(&i915->drm, tc->mode != TC_PORT_DISCONNECTED);

	if (hweight32(live_status_mask) != 1)
		return;

	if (tc->legacy_port)
		valid_hpd_mask = BIT(TC_PORT_LEGACY);
	else
		valid_hpd_mask = BIT(TC_PORT_DP_ALT) |
				 BIT(TC_PORT_TBT_ALT);

	if (!(live_status_mask & ~valid_hpd_mask))
		return;

	/* If live status mismatches the VBT flag, trust the live status. */
	drm_dbg_kms(&i915->drm,
		    "Port %s: live status %08x mismatches the legacy port flag %08x, fixing flag\n",
		    tc->port_name, live_status_mask, valid_hpd_mask);

	tc->legacy_port = !tc->legacy_port;
}

/*
 * ICL TC PHY handlers
 * -------------------
 */
static u32 icl_tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;
	u32 isr_bit = i915->display.hotplug.pch_hpd[dig_port->base.hpd_pin];
	u32 mask = 0;
	u32 val;

	val = intel_de_read(i915, PORT_TX_DFLEXDPSP(tc->phy_fia));

	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, nothing connected\n",
			    tc->port_name);
		return mask;
	}

	if (val & TC_LIVE_STATE_TBT(tc->phy_fia_idx))
		mask |= BIT(TC_PORT_TBT_ALT);
	if (val & TC_LIVE_STATE_TC(tc->phy_fia_idx))
		mask |= BIT(TC_PORT_DP_ALT);

	if (intel_de_read(i915, SDEISR) & isr_bit)
		mask |= BIT(TC_PORT_LEGACY);

	return mask;
}

/*
 * Return the PHY status complete flag indicating that display can acquire the
 * PHY ownership. The IOM firmware sets this flag when a DP-alt or legacy sink
 * is connected and it's ready to switch the ownership to display. The flag
 * will be left cleared when a TBT-alt sink is connected, where the PHY is
 * owned by the TBT subsystem and so switching the ownership to display is not
 * required.
 */
static bool icl_tc_phy_is_ready(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 val;

	val = intel_de_read(i915, PORT_TX_DFLEXDPPMS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assuming not ready\n",
			    tc->port_name);
		return false;
	}

	return val & DP_PHY_MODE_STATUS_COMPLETED(tc->phy_fia_idx);
}

static bool icl_tc_phy_take_ownership(struct intel_tc_port *tc,
				      bool take)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 val;

	val = intel_de_read(i915, PORT_TX_DFLEXDPCSSS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, can't %s ownership\n",
			    tc->port_name, take ? "take" : "release");

		return false;
	}

	val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(tc->phy_fia_idx);
	if (take)
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(tc->phy_fia_idx);

	intel_de_write(i915, PORT_TX_DFLEXDPCSSS(tc->phy_fia), val);

	return true;
}

static bool icl_tc_phy_is_owned(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 val;

	val = intel_de_read(i915, PORT_TX_DFLEXDPCSSS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assume not owned\n",
			    tc->port_name);
		return false;
	}

	return val & DP_PHY_MODE_STATUS_NOT_SAFE(tc->phy_fia_idx);
}

static void icl_tc_phy_get_hw_state(struct intel_tc_port *tc)
{
	enum intel_display_power_domain domain;
	intel_wakeref_t tc_cold_wref;

	tc_cold_wref = tc_cold_block(tc, &domain);

	tc->mode = tc_phy_get_current_mode(tc);
	if (tc->mode != TC_PORT_DISCONNECTED)
		tc->lock_wakeref = tc_cold_block(tc, &tc->lock_power_domain);

	tc_cold_unblock(tc, domain, tc_cold_wref);
}

/*
 * This function implements the first part of the Connect Flow described by our
 * specification, Gen11 TypeC Programming chapter. The rest of the flow (reading
 * lanes, EDID, etc) is done as needed in the typical places.
 *
 * Unlike the other ports, type-C ports are not available to use as soon as we
 * get a hotplug. The type-C PHYs can be shared between multiple controllers:
 * display, USB, etc. As a result, handshaking through FIA is required around
 * connect and disconnect to cleanly transfer ownership with the controller and
 * set the type-C power state.
 */
static bool tc_phy_verify_legacy_or_dp_alt_mode(struct intel_tc_port *tc,
						int required_lanes)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;
	int max_lanes;

	max_lanes = intel_tc_port_fia_max_lane_count(dig_port);
	if (tc->mode == TC_PORT_LEGACY) {
		drm_WARN_ON(&i915->drm, max_lanes != 4);
		return true;
	}

	drm_WARN_ON(&i915->drm, tc->mode != TC_PORT_DP_ALT);

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (!(tc_phy_hpd_live_status(tc) & BIT(TC_PORT_DP_ALT))) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY sudden disconnect\n",
			    tc->port_name);
		return false;
	}

	if (max_lanes < required_lanes) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY max lanes %d < required lanes %d\n",
			    tc->port_name,
			    max_lanes, required_lanes);
		return false;
	}

	return true;
}

static bool icl_tc_phy_connect(struct intel_tc_port *tc,
			       int required_lanes)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	tc->lock_wakeref = tc_cold_block(tc, &tc->lock_power_domain);

	if (tc->mode == TC_PORT_TBT_ALT)
		return true;

	if ((!tc_phy_is_ready(tc) ||
	     !tc_phy_take_ownership(tc, true)) &&
	    !drm_WARN_ON(&i915->drm, tc->mode == TC_PORT_LEGACY)) {
		drm_dbg_kms(&i915->drm, "Port %s: can't take PHY ownership (ready %s)\n",
			    tc->port_name,
			    str_yes_no(tc_phy_is_ready(tc)));
		goto out_unblock_tc_cold;
	}


	if (!tc_phy_verify_legacy_or_dp_alt_mode(tc, required_lanes))
		goto out_release_phy;

	return true;

out_release_phy:
	tc_phy_take_ownership(tc, false);
out_unblock_tc_cold:
	tc_cold_unblock(tc,
			tc->lock_power_domain,
			fetch_and_zero(&tc->lock_wakeref));

	return false;
}

/*
 * See the comment at the connect function. This implements the Disconnect
 * Flow.
 */
static void icl_tc_phy_disconnect(struct intel_tc_port *tc)
{
	switch (tc->mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		tc_phy_take_ownership(tc, false);
		fallthrough;
	case TC_PORT_TBT_ALT:
		tc_cold_unblock(tc,
				tc->lock_power_domain,
				fetch_and_zero(&tc->lock_wakeref));
		break;
	default:
		MISSING_CASE(tc->mode);
	}
}

static const struct intel_tc_phy_ops icl_tc_phy_ops = {
	.hpd_live_status = icl_tc_phy_hpd_live_status,
	.is_ready = icl_tc_phy_is_ready,
	.is_owned = icl_tc_phy_is_owned,
	.get_hw_state = icl_tc_phy_get_hw_state,
	.connect = icl_tc_phy_connect,
	.disconnect = icl_tc_phy_disconnect,
};

/*
 * ADLP TC PHY handlers
 * --------------------
 */
static u32 adlp_tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum tc_port tc_port = intel_port_to_tc(i915, dig_port->base.port);
	u32 isr_bit = i915->display.hotplug.pch_hpd[dig_port->base.hpd_pin];
	u32 val, mask = 0;

	/*
	 * On ADL-P HW/FW will wake from TCCOLD to complete the read access of
	 * registers in IOM. Note that this doesn't apply to PHY and FIA
	 * registers.
	 */
	val = intel_de_read(i915, TCSS_DDI_STATUS(tc_port));
	if (val & TCSS_DDI_STATUS_HPD_LIVE_STATUS_ALT)
		mask |= BIT(TC_PORT_DP_ALT);
	if (val & TCSS_DDI_STATUS_HPD_LIVE_STATUS_TBT)
		mask |= BIT(TC_PORT_TBT_ALT);

	if (intel_de_read(i915, SDEISR) & isr_bit)
		mask |= BIT(TC_PORT_LEGACY);

	return mask;
}

/*
 * Return the PHY status complete flag indicating that display can acquire the
 * PHY ownership. The IOM firmware sets this flag when it's ready to switch
 * the ownership to display, regardless of what sink is connected (TBT-alt,
 * DP-alt, legacy or nothing). For TBT-alt sinks the PHY is owned by the TBT
 * subsystem and so switching the ownership to display is not required.
 */
static bool adlp_tc_phy_is_ready(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	enum tc_port tc_port = intel_port_to_tc(i915, tc->dig_port->base.port);
	u32 val;

	val = intel_de_read(i915, TCSS_DDI_STATUS(tc_port));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assuming not ready\n",
			    tc->port_name);
		return false;
	}

	return val & TCSS_DDI_STATUS_READY;
}

static bool adlp_tc_phy_take_ownership(struct intel_tc_port *tc,
				       bool take)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	enum port port = tc->dig_port->base.port;

	intel_de_rmw(i915, DDI_BUF_CTL(port), DDI_BUF_CTL_TC_PHY_OWNERSHIP,
		     take ? DDI_BUF_CTL_TC_PHY_OWNERSHIP : 0);

	return true;
}

static bool adlp_tc_phy_is_owned(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	enum port port = tc->dig_port->base.port;
	u32 val;

	val = intel_de_read(i915, DDI_BUF_CTL(port));
	return val & DDI_BUF_CTL_TC_PHY_OWNERSHIP;
}

static const struct intel_tc_phy_ops adlp_tc_phy_ops = {
	.hpd_live_status = adlp_tc_phy_hpd_live_status,
	.is_ready = adlp_tc_phy_is_ready,
	.is_owned = adlp_tc_phy_is_owned,
	.get_hw_state = icl_tc_phy_get_hw_state,
	.connect = icl_tc_phy_connect,
	.disconnect = icl_tc_phy_disconnect,
};

/*
 * Generic TC PHY handlers
 * -----------------------
 */
static u32 tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 mask;

	mask = tc->phy_ops->hpd_live_status(tc);

	/* The sink can be connected only in a single mode. */
	drm_WARN_ON_ONCE(&i915->drm, hweight32(mask) > 1);

	return mask;
}

static bool tc_phy_is_ready(struct intel_tc_port *tc)
{
	return tc->phy_ops->is_ready(tc);
}

static bool tc_phy_is_owned(struct intel_tc_port *tc)
{
	return tc->phy_ops->is_owned(tc);
}

static void tc_phy_get_hw_state(struct intel_tc_port *tc)
{
	tc->phy_ops->get_hw_state(tc);
}

static bool tc_phy_take_ownership(struct intel_tc_port *tc, bool take)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	if (IS_ALDERLAKE_P(i915))
		return adlp_tc_phy_take_ownership(tc, take);

	return icl_tc_phy_take_ownership(tc, take);
}

static bool tc_phy_is_ready_and_owned(struct intel_tc_port *tc,
				      bool phy_is_ready, bool phy_is_owned)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	drm_WARN_ON(&i915->drm, phy_is_owned && !phy_is_ready);

	return phy_is_ready && phy_is_owned;
}

static bool tc_phy_is_connected(struct intel_tc_port *tc,
				enum icl_port_dpll_id port_pll_type)
{
	struct intel_encoder *encoder = &tc->dig_port->base;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	bool phy_is_ready = tc_phy_is_ready(tc);
	bool phy_is_owned = tc_phy_is_owned(tc);
	bool is_connected;

	if (tc_phy_is_ready_and_owned(tc, phy_is_ready, phy_is_owned))
		is_connected = port_pll_type == ICL_PORT_DPLL_MG_PHY;
	else
		is_connected = port_pll_type == ICL_PORT_DPLL_DEFAULT;

	drm_dbg_kms(&i915->drm,
		    "Port %s: PHY connected: %s (ready: %s, owned: %s, pll_type: %s)\n",
		    tc->port_name,
		    str_yes_no(is_connected),
		    str_yes_no(phy_is_ready),
		    str_yes_no(phy_is_owned),
		    port_pll_type == ICL_PORT_DPLL_DEFAULT ? "tbt" : "non-tbt");

	return is_connected;
}

static void tc_phy_wait_for_ready(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	if (wait_for(tc_phy_is_ready(tc), 100))
		drm_err(&i915->drm, "Port %s: timeout waiting for PHY ready\n",
			tc->port_name);
}

static enum tc_port_mode
hpd_mask_to_tc_mode(u32 live_status_mask)
{
	if (live_status_mask)
		return fls(live_status_mask) - 1;

	return TC_PORT_DISCONNECTED;
}

static enum tc_port_mode
tc_phy_hpd_live_mode(struct intel_tc_port *tc)
{
	u32 live_status_mask = tc_phy_hpd_live_status(tc);

	return hpd_mask_to_tc_mode(live_status_mask);
}

static enum tc_port_mode
get_tc_mode_in_phy_owned_state(struct intel_tc_port *tc,
			       enum tc_port_mode live_mode)
{
	switch (live_mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		return live_mode;
	default:
		MISSING_CASE(live_mode);
		fallthrough;
	case TC_PORT_TBT_ALT:
	case TC_PORT_DISCONNECTED:
		if (tc->legacy_port)
			return TC_PORT_LEGACY;
		else
			return TC_PORT_DP_ALT;
	}
}

static enum tc_port_mode
get_tc_mode_in_phy_not_owned_state(struct intel_tc_port *tc,
				   enum tc_port_mode live_mode)
{
	switch (live_mode) {
	case TC_PORT_LEGACY:
		return TC_PORT_DISCONNECTED;
	case TC_PORT_DP_ALT:
	case TC_PORT_TBT_ALT:
		return TC_PORT_TBT_ALT;
	default:
		MISSING_CASE(live_mode);
		fallthrough;
	case TC_PORT_DISCONNECTED:
		if (tc->legacy_port)
			return TC_PORT_DISCONNECTED;
		else
			return TC_PORT_TBT_ALT;
	}
}

static enum tc_port_mode
tc_phy_get_current_mode(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	enum tc_port_mode live_mode = tc_phy_hpd_live_mode(tc);
	bool phy_is_ready;
	bool phy_is_owned;
	enum tc_port_mode mode;

	/*
	 * For legacy ports the IOM firmware initializes the PHY during boot-up
	 * and system resume whether or not a sink is connected. Wait here for
	 * the initialization to get ready.
	 */
	if (tc->legacy_port)
		tc_phy_wait_for_ready(tc);

	phy_is_ready = tc_phy_is_ready(tc);
	phy_is_owned = tc_phy_is_owned(tc);

	if (!tc_phy_is_ready_and_owned(tc, phy_is_ready, phy_is_owned)) {
		mode = get_tc_mode_in_phy_not_owned_state(tc, live_mode);
	} else {
		drm_WARN_ON(&i915->drm, live_mode == TC_PORT_TBT_ALT);
		mode = get_tc_mode_in_phy_owned_state(tc, live_mode);
	}

	drm_dbg_kms(&i915->drm,
		    "Port %s: PHY mode: %s (ready: %s, owned: %s, HPD: %s)\n",
		    tc->port_name,
		    tc_port_mode_name(mode),
		    str_yes_no(phy_is_ready),
		    str_yes_no(phy_is_owned),
		    tc_port_mode_name(live_mode));

	return mode;
}

static enum tc_port_mode default_tc_mode(struct intel_tc_port *tc)
{
	if (tc->legacy_port)
		return TC_PORT_LEGACY;

	return TC_PORT_TBT_ALT;
}

static enum tc_port_mode
hpd_mask_to_target_mode(struct intel_tc_port *tc, u32 live_status_mask)
{
	enum tc_port_mode mode = hpd_mask_to_tc_mode(live_status_mask);

	if (mode != TC_PORT_DISCONNECTED)
		return mode;

	return default_tc_mode(tc);
}

static enum tc_port_mode
tc_phy_get_target_mode(struct intel_tc_port *tc)
{
	u32 live_status_mask = tc_phy_hpd_live_status(tc);

	return hpd_mask_to_target_mode(tc, live_status_mask);
}

static void tc_phy_connect(struct intel_tc_port *tc, int required_lanes)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	u32 live_status_mask = tc_phy_hpd_live_status(tc);
	bool connected;

	tc_port_fixup_legacy_flag(tc, live_status_mask);

	tc->mode = hpd_mask_to_target_mode(tc, live_status_mask);

	connected = tc->phy_ops->connect(tc, required_lanes);
	if (!connected && tc->mode != default_tc_mode(tc)) {
		tc->mode = default_tc_mode(tc);
		connected = tc->phy_ops->connect(tc, required_lanes);
	}

	drm_WARN_ON(&i915->drm, !connected);
}

static void tc_phy_disconnect(struct intel_tc_port *tc)
{
	if (tc->mode != TC_PORT_DISCONNECTED) {
		tc->phy_ops->disconnect(tc);
		tc->mode = TC_PORT_DISCONNECTED;
	}
}

static void intel_tc_port_reset_mode(struct intel_tc_port *tc,
				     int required_lanes, bool force_disconnect)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum tc_port_mode old_tc_mode = tc->mode;

	intel_display_power_flush_work(i915);
	if (!intel_tc_cold_requires_aux_pw(dig_port)) {
		enum intel_display_power_domain aux_domain;
		bool aux_powered;

		aux_domain = intel_aux_power_domain(dig_port);
		aux_powered = intel_display_power_is_enabled(i915, aux_domain);
		drm_WARN_ON(&i915->drm, aux_powered);
	}

	tc_phy_disconnect(tc);
	if (!force_disconnect)
		tc_phy_connect(tc, required_lanes);

	drm_dbg_kms(&i915->drm, "Port %s: TC port mode reset (%s -> %s)\n",
		    tc->port_name,
		    tc_port_mode_name(old_tc_mode),
		    tc_port_mode_name(tc->mode));
}

static bool intel_tc_port_needs_reset(struct intel_tc_port *tc)
{
	return tc_phy_get_target_mode(tc) != tc->mode;
}

static void intel_tc_port_update_mode(struct intel_tc_port *tc,
				      int required_lanes, bool force_disconnect)
{
	if (force_disconnect ||
	    intel_tc_port_needs_reset(tc))
		intel_tc_port_reset_mode(tc, required_lanes, force_disconnect);
}

static void __intel_tc_port_get_link(struct intel_tc_port *tc)
{
	tc->link_refcount++;
}

static void __intel_tc_port_put_link(struct intel_tc_port *tc)
{
	tc->link_refcount--;
}

static bool tc_port_is_enabled(struct intel_tc_port *tc)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;

	assert_tc_port_power_enabled(tc);

	return intel_de_read(i915, DDI_BUF_CTL(dig_port->base.port)) &
	       DDI_BUF_CTL_ENABLE;
}

/**
 * intel_tc_port_init_mode: Read out HW state and init the given port's TypeC mode
 * @dig_port: digital port
 *
 * Read out the HW state and initialize the TypeC mode of @dig_port. The mode
 * will be locked until intel_tc_port_sanitize_mode() is called.
 */
void intel_tc_port_init_mode(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	bool update_mode = false;

	mutex_lock(&tc->lock);

	drm_WARN_ON(&i915->drm, tc->mode != TC_PORT_DISCONNECTED);
	drm_WARN_ON(&i915->drm, tc->lock_wakeref);
	drm_WARN_ON(&i915->drm, tc->link_refcount);

	tc_phy_get_hw_state(tc);
	/*
	 * Save the initial mode for the state check in
	 * intel_tc_port_sanitize_mode().
	 */
	tc->init_mode = tc->mode;

	/*
	 * The PHY needs to be connected for AUX to work during HW readout and
	 * MST topology resume, but the PHY mode can only be changed if the
	 * port is disabled.
	 *
	 * An exception is the case where BIOS leaves the PHY incorrectly
	 * disconnected on an enabled legacy port. Work around that by
	 * connecting the PHY even though the port is enabled. This doesn't
	 * cause a problem as the PHY ownership state is ignored by the
	 * IOM/TCSS firmware (only display can own the PHY in that case).
	 */
	if (!tc_port_is_enabled(tc)) {
		update_mode = true;
	} else if (tc->mode == TC_PORT_DISCONNECTED) {
		drm_WARN_ON(&i915->drm, !tc->legacy_port);
		drm_err(&i915->drm,
			"Port %s: PHY disconnected on enabled port, connecting it\n",
			tc->port_name);
		update_mode = true;
	}

	if (update_mode)
		intel_tc_port_update_mode(tc, 1, false);

	/* Prevent changing tc->mode until intel_tc_port_sanitize_mode() is called. */
	__intel_tc_port_get_link(tc);

	mutex_unlock(&tc->lock);
}

static bool tc_port_has_active_links(struct intel_tc_port *tc,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum icl_port_dpll_id pll_type = ICL_PORT_DPLL_DEFAULT;
	int active_links = 0;

	if (dig_port->dp.is_mst) {
		/* TODO: get the PLL type for MST, once HW readout is done for it. */
		active_links = intel_dp_mst_encoder_active_links(dig_port);
	} else if (crtc_state && crtc_state->hw.active) {
		pll_type = intel_ddi_port_pll_type(&dig_port->base, crtc_state);
		active_links = 1;
	}

	if (active_links && !tc_phy_is_connected(tc, pll_type))
		drm_err(&i915->drm,
			"Port %s: PHY disconnected with %d active link(s)\n",
			tc->port_name, active_links);

	return active_links;
}

/**
 * intel_tc_port_sanitize_mode: Sanitize the given port's TypeC mode
 * @dig_port: digital port
 * @crtc_state: atomic state of CRTC connected to @dig_port
 *
 * Sanitize @dig_port's TypeC mode wrt. the encoder's state right after driver
 * loading and system resume:
 * If the encoder is enabled keep the TypeC mode/PHY connected state locked until
 * the encoder is disabled.
 * If the encoder is disabled make sure the PHY is disconnected.
 * @crtc_state is valid if @dig_port is enabled, NULL otherwise.
 */
void intel_tc_port_sanitize_mode(struct intel_digital_port *dig_port,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	mutex_lock(&tc->lock);

	drm_WARN_ON(&i915->drm, tc->link_refcount != 1);
	if (!tc_port_has_active_links(tc, crtc_state)) {
		/*
		 * TBT-alt is the default mode in any case the PHY ownership is not
		 * held (regardless of the sink's connected live state), so
		 * we'll just switch to disconnected mode from it here without
		 * a note.
		 */
		if (tc->init_mode != TC_PORT_TBT_ALT &&
		    tc->init_mode != TC_PORT_DISCONNECTED)
			drm_dbg_kms(&i915->drm,
				    "Port %s: PHY left in %s mode on disabled port, disconnecting it\n",
				    tc->port_name,
				    tc_port_mode_name(tc->init_mode));
		tc_phy_disconnect(tc);
		__intel_tc_port_put_link(tc);
	}

	drm_dbg_kms(&i915->drm, "Port %s: sanitize mode (%s)\n",
		    tc->port_name,
		    tc_port_mode_name(tc->mode));

	mutex_unlock(&tc->lock);
}

/*
 * The type-C ports are different because even when they are connected, they may
 * not be available/usable by the graphics driver: see the comment on
 * icl_tc_phy_connect(). So in our driver instead of adding the additional
 * concept of "usable" and make everything check for "connected and usable" we
 * define a port as "connected" when it is not only connected, but also when it
 * is usable by the rest of the driver. That maintains the old assumption that
 * connected ports are usable, and avoids exposing to the users objects they
 * can't really use.
 */
bool intel_tc_port_connected_locked(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	drm_WARN_ON(&i915->drm, !intel_tc_port_ref_held(dig_port));

	return tc_phy_hpd_live_status(tc) & BIT(tc->mode);
}

bool intel_tc_port_connected(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_connected;

	intel_tc_port_lock(dig_port);
	is_connected = intel_tc_port_connected_locked(encoder);
	intel_tc_port_unlock(dig_port);

	return is_connected;
}

static void __intel_tc_port_lock(struct intel_tc_port *tc,
				 int required_lanes)
{
	struct drm_i915_private *i915 = tc_to_i915(tc);

	mutex_lock(&tc->lock);

	cancel_delayed_work(&tc->disconnect_phy_work);

	if (!tc->link_refcount)
		intel_tc_port_update_mode(tc, required_lanes,
					  false);

	drm_WARN_ON(&i915->drm, tc->mode == TC_PORT_DISCONNECTED);
	drm_WARN_ON(&i915->drm, tc->mode != TC_PORT_TBT_ALT &&
				!tc_phy_is_owned(tc));
}

void intel_tc_port_lock(struct intel_digital_port *dig_port)
{
	__intel_tc_port_lock(to_tc_port(dig_port), 1);
}

/**
 * intel_tc_port_disconnect_phy_work: disconnect TypeC PHY from display port
 * @dig_port: digital port
 *
 * Disconnect the given digital port from its TypeC PHY (handing back the
 * control of the PHY to the TypeC subsystem). This will happen in a delayed
 * manner after each aux transactions and modeset disables.
 */
static void intel_tc_port_disconnect_phy_work(struct work_struct *work)
{
	struct intel_tc_port *tc =
		container_of(work, struct intel_tc_port, disconnect_phy_work.work);

	mutex_lock(&tc->lock);

	if (!tc->link_refcount)
		intel_tc_port_update_mode(tc, 1, true);

	mutex_unlock(&tc->lock);
}

/**
 * intel_tc_port_flush_work: flush the work disconnecting the PHY
 * @dig_port: digital port
 *
 * Flush the delayed work disconnecting an idle PHY.
 */
void intel_tc_port_flush_work(struct intel_digital_port *dig_port)
{
	flush_delayed_work(&to_tc_port(dig_port)->disconnect_phy_work);
}

void intel_tc_port_unlock(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	if (!tc->link_refcount && tc->mode != TC_PORT_DISCONNECTED)
		queue_delayed_work(system_unbound_wq, &tc->disconnect_phy_work,
				   msecs_to_jiffies(1000));

	mutex_unlock(&tc->lock);
}

bool intel_tc_port_ref_held(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return mutex_is_locked(&tc->lock) ||
	       tc->link_refcount;
}

void intel_tc_port_get_link(struct intel_digital_port *dig_port,
			    int required_lanes)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	__intel_tc_port_lock(tc, required_lanes);
	__intel_tc_port_get_link(tc);
	intel_tc_port_unlock(dig_port);
}

void intel_tc_port_put_link(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	intel_tc_port_lock(dig_port);
	__intel_tc_port_put_link(tc);
	intel_tc_port_unlock(dig_port);

	/*
	 * Disconnecting the PHY after the PHY's PLL gets disabled may
	 * hang the system on ADL-P, so disconnect the PHY here synchronously.
	 * TODO: remove this once the root cause of the ordering requirement
	 * is found/fixed.
	 */
	intel_tc_port_flush_work(dig_port);
}

static bool
tc_has_modular_fia(struct drm_i915_private *i915, struct intel_tc_port *tc)
{
	enum intel_display_power_domain domain;
	intel_wakeref_t wakeref;
	u32 val;

	if (!INTEL_INFO(i915)->display.has_modular_fia)
		return false;

	mutex_lock(&tc->lock);
	wakeref = tc_cold_block(tc, &domain);
	val = intel_de_read(i915, PORT_TX_DFLEXDPSP(FIA1));
	tc_cold_unblock(tc, domain, wakeref);
	mutex_unlock(&tc->lock);

	drm_WARN_ON(&i915->drm, val == 0xffffffff);

	return val & MODULAR_FIA_MASK;
}

static void
tc_port_load_fia_params(struct drm_i915_private *i915, struct intel_tc_port *tc)
{
	enum port port = tc->dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(i915, port);

	/*
	 * Each Modular FIA instance houses 2 TC ports. In SOC that has more
	 * than two TC ports, there are multiple instances of Modular FIA.
	 */
	if (tc_has_modular_fia(i915, tc)) {
		tc->phy_fia = tc_port / 2;
		tc->phy_fia_idx = tc_port % 2;
	} else {
		tc->phy_fia = FIA1;
		tc->phy_fia_idx = tc_port;
	}
}

int intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_tc_port *tc;
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(i915, port);

	if (drm_WARN_ON(&i915->drm, tc_port == TC_PORT_NONE))
		return -EINVAL;

	tc = kzalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	dig_port->tc = tc;
	tc->dig_port = dig_port;

	if (DISPLAY_VER(i915) >= 13)
		tc->phy_ops = &adlp_tc_phy_ops;
	else
		tc->phy_ops = &icl_tc_phy_ops;

	snprintf(tc->port_name, sizeof(tc->port_name),
		 "%c/TC#%d", port_name(port), tc_port + 1);

	mutex_init(&tc->lock);
	INIT_DELAYED_WORK(&tc->disconnect_phy_work, intel_tc_port_disconnect_phy_work);
	tc->legacy_port = is_legacy;
	tc->mode = TC_PORT_DISCONNECTED;
	tc->link_refcount = 0;
	tc_port_load_fia_params(i915, tc);

	intel_tc_port_init_mode(dig_port);

	return 0;
}

void intel_tc_port_cleanup(struct intel_digital_port *dig_port)
{
	intel_tc_port_flush_work(dig_port);

	kfree(dig_port->tc);
	dig_port->tc = NULL;
}
