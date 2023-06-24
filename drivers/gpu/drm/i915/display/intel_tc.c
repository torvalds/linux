// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_display.h"
#include "intel_display_power_map.h"
#include "intel_display_types.h"
#include "intel_dp_mst.h"
#include "intel_tc.h"
#include "intel_tc_phy_regs.h"

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

static bool intel_tc_port_in_mode(struct intel_digital_port *dig_port,
				  enum tc_port_mode mode)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);

	return intel_phy_is_tc(i915, phy) && dig_port->tc_mode == mode;
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

	return (DISPLAY_VER(i915) == 11 && dig_port->tc_legacy_port) ||
		IS_ALDERLAKE_P(i915);
}

static enum intel_display_power_domain
tc_cold_get_power_domain(struct intel_digital_port *dig_port, enum tc_port_mode mode)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (mode == TC_PORT_TBT_ALT || !intel_tc_cold_requires_aux_pw(dig_port))
		return POWER_DOMAIN_TC_COLD_OFF;

	return intel_display_power_legacy_aux_domain(i915, dig_port->aux_ch);
}

static intel_wakeref_t
tc_cold_block_in_mode(struct intel_digital_port *dig_port, enum tc_port_mode mode,
		      enum intel_display_power_domain *domain)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	*domain = tc_cold_get_power_domain(dig_port, mode);

	return intel_display_power_get(i915, *domain);
}

static intel_wakeref_t
tc_cold_block(struct intel_digital_port *dig_port, enum intel_display_power_domain *domain)
{
	return tc_cold_block_in_mode(dig_port, dig_port->tc_mode, domain);
}

static void
tc_cold_unblock(struct intel_digital_port *dig_port, enum intel_display_power_domain domain,
		intel_wakeref_t wakeref)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

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
assert_tc_cold_blocked(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	bool enabled;

	enabled = intel_display_power_is_enabled(i915,
						 tc_cold_get_power_domain(dig_port,
									  dig_port->tc_mode));
	drm_WARN_ON(&i915->drm, !enabled);
}

u32 intel_tc_port_get_lane_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 lane_mask;

	lane_mask = intel_uncore_read(uncore,
				      PORT_TX_DFLEXDPSP(dig_port->tc_phy_fia));

	drm_WARN_ON(&i915->drm, lane_mask == 0xffffffff);
	assert_tc_cold_blocked(dig_port);

	lane_mask &= DP_LANE_ASSIGNMENT_MASK(dig_port->tc_phy_fia_idx);
	return lane_mask >> DP_LANE_ASSIGNMENT_SHIFT(dig_port->tc_phy_fia_idx);
}

u32 intel_tc_port_get_pin_assignment_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 pin_mask;

	pin_mask = intel_uncore_read(uncore,
				     PORT_TX_DFLEXPA1(dig_port->tc_phy_fia));

	drm_WARN_ON(&i915->drm, pin_mask == 0xffffffff);
	assert_tc_cold_blocked(dig_port);

	return (pin_mask & DP_PIN_ASSIGNMENT_MASK(dig_port->tc_phy_fia_idx)) >>
	       DP_PIN_ASSIGNMENT_SHIFT(dig_port->tc_phy_fia_idx);
}

int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	intel_wakeref_t wakeref;
	u32 lane_mask;

	if (dig_port->tc_mode != TC_PORT_DP_ALT)
		return 4;

	assert_tc_cold_blocked(dig_port);

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
	bool lane_reversal = dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;
	struct intel_uncore *uncore = &i915->uncore;
	u32 val;

	drm_WARN_ON(&i915->drm,
		    lane_reversal && dig_port->tc_mode != TC_PORT_LEGACY);

	assert_tc_cold_blocked(dig_port);

	val = intel_uncore_read(uncore,
				PORT_TX_DFLEXDPMLE1(dig_port->tc_phy_fia));
	val &= ~DFLEXDPMLE1_DPMLETC_MASK(dig_port->tc_phy_fia_idx);

	switch (required_lanes) {
	case 1:
		val |= lane_reversal ?
			DFLEXDPMLE1_DPMLETC_ML3(dig_port->tc_phy_fia_idx) :
			DFLEXDPMLE1_DPMLETC_ML0(dig_port->tc_phy_fia_idx);
		break;
	case 2:
		val |= lane_reversal ?
			DFLEXDPMLE1_DPMLETC_ML3_2(dig_port->tc_phy_fia_idx) :
			DFLEXDPMLE1_DPMLETC_ML1_0(dig_port->tc_phy_fia_idx);
		break;
	case 4:
		val |= DFLEXDPMLE1_DPMLETC_ML3_0(dig_port->tc_phy_fia_idx);
		break;
	default:
		MISSING_CASE(required_lanes);
	}

	intel_uncore_write(uncore,
			   PORT_TX_DFLEXDPMLE1(dig_port->tc_phy_fia), val);
}

static void tc_port_fixup_legacy_flag(struct intel_digital_port *dig_port,
				      u32 live_status_mask)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	u32 valid_hpd_mask;

	if (dig_port->tc_legacy_port)
		valid_hpd_mask = BIT(TC_PORT_LEGACY);
	else
		valid_hpd_mask = BIT(TC_PORT_DP_ALT) |
				 BIT(TC_PORT_TBT_ALT);

	if (!(live_status_mask & ~valid_hpd_mask))
		return;

	/* If live status mismatches the VBT flag, trust the live status. */
	drm_dbg_kms(&i915->drm,
		    "Port %s: live status %08x mismatches the legacy port flag %08x, fixing flag\n",
		    dig_port->tc_port_name, live_status_mask, valid_hpd_mask);

	dig_port->tc_legacy_port = !dig_port->tc_legacy_port;
}

static u32 icl_tc_port_live_status_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 isr_bit = i915->display.hotplug.pch_hpd[dig_port->base.hpd_pin];
	u32 mask = 0;
	u32 val;

	val = intel_uncore_read(uncore,
				PORT_TX_DFLEXDPSP(dig_port->tc_phy_fia));

	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, nothing connected\n",
			    dig_port->tc_port_name);
		return mask;
	}

	if (val & TC_LIVE_STATE_TBT(dig_port->tc_phy_fia_idx))
		mask |= BIT(TC_PORT_TBT_ALT);
	if (val & TC_LIVE_STATE_TC(dig_port->tc_phy_fia_idx))
		mask |= BIT(TC_PORT_DP_ALT);

	if (intel_uncore_read(uncore, SDEISR) & isr_bit)
		mask |= BIT(TC_PORT_LEGACY);

	/* The sink can be connected only in a single mode. */
	if (!drm_WARN_ON_ONCE(&i915->drm, hweight32(mask) > 1))
		tc_port_fixup_legacy_flag(dig_port, mask);

	return mask;
}

static u32 adl_tc_port_live_status_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(i915, dig_port->base.port);
	u32 isr_bit = i915->display.hotplug.pch_hpd[dig_port->base.hpd_pin];
	struct intel_uncore *uncore = &i915->uncore;
	u32 val, mask = 0;

	/*
	 * On ADL-P HW/FW will wake from TCCOLD to complete the read access of
	 * registers in IOM. Note that this doesn't apply to PHY and FIA
	 * registers.
	 */
	val = intel_uncore_read(uncore, TCSS_DDI_STATUS(tc_port));
	if (val & TCSS_DDI_STATUS_HPD_LIVE_STATUS_ALT)
		mask |= BIT(TC_PORT_DP_ALT);
	if (val & TCSS_DDI_STATUS_HPD_LIVE_STATUS_TBT)
		mask |= BIT(TC_PORT_TBT_ALT);

	if (intel_uncore_read(uncore, SDEISR) & isr_bit)
		mask |= BIT(TC_PORT_LEGACY);

	/* The sink can be connected only in a single mode. */
	if (!drm_WARN_ON(&i915->drm, hweight32(mask) > 1))
		tc_port_fixup_legacy_flag(dig_port, mask);

	return mask;
}

static u32 tc_port_live_status_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (IS_ALDERLAKE_P(i915))
		return adl_tc_port_live_status_mask(dig_port);

	return icl_tc_port_live_status_mask(dig_port);
}

/*
 * Return the PHY status complete flag indicating that display can acquire the
 * PHY ownership. The IOM firmware sets this flag when a DP-alt or legacy sink
 * is connected and it's ready to switch the ownership to display. The flag
 * will be left cleared when a TBT-alt sink is connected, where the PHY is
 * owned by the TBT subsystem and so switching the ownership to display is not
 * required.
 */
static bool icl_tc_phy_status_complete(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 val;

	val = intel_uncore_read(uncore,
				PORT_TX_DFLEXDPPMS(dig_port->tc_phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assuming not complete\n",
			    dig_port->tc_port_name);
		return false;
	}

	return val & DP_PHY_MODE_STATUS_COMPLETED(dig_port->tc_phy_fia_idx);
}

/*
 * Return the PHY status complete flag indicating that display can acquire the
 * PHY ownership. The IOM firmware sets this flag when it's ready to switch
 * the ownership to display, regardless of what sink is connected (TBT-alt,
 * DP-alt, legacy or nothing). For TBT-alt sinks the PHY is owned by the TBT
 * subsystem and so switching the ownership to display is not required.
 */
static bool adl_tc_phy_status_complete(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(i915, dig_port->base.port);
	struct intel_uncore *uncore = &i915->uncore;
	u32 val;

	val = intel_uncore_read(uncore, TCSS_DDI_STATUS(tc_port));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assuming not complete\n",
			    dig_port->tc_port_name);
		return false;
	}

	return val & TCSS_DDI_STATUS_READY;
}

static bool tc_phy_status_complete(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (IS_ALDERLAKE_P(i915))
		return adl_tc_phy_status_complete(dig_port);

	return icl_tc_phy_status_complete(dig_port);
}

static bool icl_tc_phy_take_ownership(struct intel_digital_port *dig_port,
				      bool take)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 val;

	val = intel_uncore_read(uncore,
				PORT_TX_DFLEXDPCSSS(dig_port->tc_phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, can't %s ownership\n",
			    dig_port->tc_port_name, take ? "take" : "release");

		return false;
	}

	val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(dig_port->tc_phy_fia_idx);
	if (take)
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(dig_port->tc_phy_fia_idx);

	intel_uncore_write(uncore,
			   PORT_TX_DFLEXDPCSSS(dig_port->tc_phy_fia), val);

	return true;
}

static bool adl_tc_phy_take_ownership(struct intel_digital_port *dig_port,
				      bool take)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	enum port port = dig_port->base.port;
	u32 val;

	val = intel_uncore_read(uncore, DDI_BUF_CTL(port));
	if (take)
		val |= DDI_BUF_CTL_TC_PHY_OWNERSHIP;
	else
		val &= ~DDI_BUF_CTL_TC_PHY_OWNERSHIP;
	intel_uncore_write(uncore, DDI_BUF_CTL(port), val);

	return true;
}

static bool tc_phy_take_ownership(struct intel_digital_port *dig_port, bool take)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (IS_ALDERLAKE_P(i915))
		return adl_tc_phy_take_ownership(dig_port, take);

	return icl_tc_phy_take_ownership(dig_port, take);
}

static bool icl_tc_phy_is_owned(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	u32 val;

	val = intel_uncore_read(uncore,
				PORT_TX_DFLEXDPCSSS(dig_port->tc_phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY in TCCOLD, assume safe mode\n",
			    dig_port->tc_port_name);
		return true;
	}

	return val & DP_PHY_MODE_STATUS_NOT_SAFE(dig_port->tc_phy_fia_idx);
}

static bool adl_tc_phy_is_owned(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	enum port port = dig_port->base.port;
	u32 val;

	val = intel_uncore_read(uncore, DDI_BUF_CTL(port));
	return val & DDI_BUF_CTL_TC_PHY_OWNERSHIP;
}

static bool tc_phy_is_owned(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (IS_ALDERLAKE_P(i915))
		return adl_tc_phy_is_owned(dig_port);

	return icl_tc_phy_is_owned(dig_port);
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
static void icl_tc_phy_connect(struct intel_digital_port *dig_port,
			       int required_lanes)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	u32 live_status_mask;
	int max_lanes;

	if (!tc_phy_status_complete(dig_port)) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY not ready\n",
			    dig_port->tc_port_name);
		goto out_set_tbt_alt_mode;
	}

	live_status_mask = tc_port_live_status_mask(dig_port);
	if (!(live_status_mask & (BIT(TC_PORT_DP_ALT) | BIT(TC_PORT_LEGACY))) &&
	    !dig_port->tc_legacy_port) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY ownership not required (live status %02x)\n",
			    dig_port->tc_port_name, live_status_mask);
		goto out_set_tbt_alt_mode;
	}

	if (!tc_phy_take_ownership(dig_port, true) &&
	    !drm_WARN_ON(&i915->drm, dig_port->tc_legacy_port))
		goto out_set_tbt_alt_mode;

	max_lanes = intel_tc_port_fia_max_lane_count(dig_port);
	if (dig_port->tc_legacy_port) {
		drm_WARN_ON(&i915->drm, max_lanes != 4);
		dig_port->tc_mode = TC_PORT_LEGACY;

		return;
	}

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (!(tc_port_live_status_mask(dig_port) & BIT(TC_PORT_DP_ALT))) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY sudden disconnect\n",
			    dig_port->tc_port_name);
		goto out_release_phy;
	}

	if (max_lanes < required_lanes) {
		drm_dbg_kms(&i915->drm,
			    "Port %s: PHY max lanes %d < required lanes %d\n",
			    dig_port->tc_port_name,
			    max_lanes, required_lanes);
		goto out_release_phy;
	}

	dig_port->tc_mode = TC_PORT_DP_ALT;

	return;

out_release_phy:
	tc_phy_take_ownership(dig_port, false);
out_set_tbt_alt_mode:
	dig_port->tc_mode = TC_PORT_TBT_ALT;
}

/*
 * See the comment at the connect function. This implements the Disconnect
 * Flow.
 */
static void icl_tc_phy_disconnect(struct intel_digital_port *dig_port)
{
	switch (dig_port->tc_mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		tc_phy_take_ownership(dig_port, false);
		fallthrough;
	case TC_PORT_TBT_ALT:
		dig_port->tc_mode = TC_PORT_DISCONNECTED;
		fallthrough;
	case TC_PORT_DISCONNECTED:
		break;
	default:
		MISSING_CASE(dig_port->tc_mode);
	}
}

static bool icl_tc_phy_is_connected(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (!tc_phy_status_complete(dig_port)) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY status not complete\n",
			    dig_port->tc_port_name);
		return dig_port->tc_mode == TC_PORT_TBT_ALT;
	}

	/* On ADL-P the PHY complete flag is set in TBT mode as well. */
	if (IS_ALDERLAKE_P(i915) && dig_port->tc_mode == TC_PORT_TBT_ALT)
		return true;

	if (!tc_phy_is_owned(dig_port)) {
		drm_dbg_kms(&i915->drm, "Port %s: PHY not owned\n",
			    dig_port->tc_port_name);

		return false;
	}

	return dig_port->tc_mode == TC_PORT_DP_ALT ||
	       dig_port->tc_mode == TC_PORT_LEGACY;
}

static enum tc_port_mode
intel_tc_port_get_current_mode(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	u32 live_status_mask = tc_port_live_status_mask(dig_port);
	enum tc_port_mode mode;

	if (!tc_phy_is_owned(dig_port) ||
	    drm_WARN_ON(&i915->drm, !tc_phy_status_complete(dig_port)))
		return TC_PORT_TBT_ALT;

	mode = dig_port->tc_legacy_port ? TC_PORT_LEGACY : TC_PORT_DP_ALT;
	if (live_status_mask) {
		enum tc_port_mode live_mode = fls(live_status_mask) - 1;

		if (!drm_WARN_ON(&i915->drm, live_mode == TC_PORT_TBT_ALT))
			mode = live_mode;
	}

	return mode;
}

static enum tc_port_mode
intel_tc_port_get_target_mode(struct intel_digital_port *dig_port)
{
	u32 live_status_mask = tc_port_live_status_mask(dig_port);

	if (live_status_mask)
		return fls(live_status_mask) - 1;

	return TC_PORT_TBT_ALT;
}

static void intel_tc_port_reset_mode(struct intel_digital_port *dig_port,
				     int required_lanes, bool force_disconnect)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum tc_port_mode old_tc_mode = dig_port->tc_mode;

	intel_display_power_flush_work(i915);
	if (!intel_tc_cold_requires_aux_pw(dig_port)) {
		enum intel_display_power_domain aux_domain;
		bool aux_powered;

		aux_domain = intel_aux_power_domain(dig_port);
		aux_powered = intel_display_power_is_enabled(i915, aux_domain);
		drm_WARN_ON(&i915->drm, aux_powered);
	}

	icl_tc_phy_disconnect(dig_port);
	if (!force_disconnect)
		icl_tc_phy_connect(dig_port, required_lanes);

	drm_dbg_kms(&i915->drm, "Port %s: TC port mode reset (%s -> %s)\n",
		    dig_port->tc_port_name,
		    tc_port_mode_name(old_tc_mode),
		    tc_port_mode_name(dig_port->tc_mode));
}

static bool intel_tc_port_needs_reset(struct intel_digital_port *dig_port)
{
	return intel_tc_port_get_target_mode(dig_port) != dig_port->tc_mode;
}

static void intel_tc_port_update_mode(struct intel_digital_port *dig_port,
				      int required_lanes, bool force_disconnect)
{
	enum intel_display_power_domain domain;
	intel_wakeref_t wref;
	bool needs_reset = force_disconnect;

	if (!needs_reset) {
		/* Get power domain required to check the hotplug live status. */
		wref = tc_cold_block(dig_port, &domain);
		needs_reset = intel_tc_port_needs_reset(dig_port);
		tc_cold_unblock(dig_port, domain, wref);
	}

	if (!needs_reset)
		return;

	/* Get power domain required for resetting the mode. */
	wref = tc_cold_block_in_mode(dig_port, TC_PORT_DISCONNECTED, &domain);

	intel_tc_port_reset_mode(dig_port, required_lanes, force_disconnect);

	/* Get power domain matching the new mode after reset. */
	tc_cold_unblock(dig_port, dig_port->tc_lock_power_domain,
			fetch_and_zero(&dig_port->tc_lock_wakeref));
	if (dig_port->tc_mode != TC_PORT_DISCONNECTED)
		dig_port->tc_lock_wakeref = tc_cold_block(dig_port,
							  &dig_port->tc_lock_power_domain);

	tc_cold_unblock(dig_port, domain, wref);
}

static void
intel_tc_port_link_init_refcount(struct intel_digital_port *dig_port,
				 int refcount)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	drm_WARN_ON(&i915->drm, dig_port->tc_link_refcount);
	dig_port->tc_link_refcount = refcount;
}

void intel_tc_port_sanitize(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_encoder *encoder = &dig_port->base;
	intel_wakeref_t tc_cold_wref;
	enum intel_display_power_domain domain;
	int active_links = 0;

	mutex_lock(&dig_port->tc_lock);

	if (dig_port->dp.is_mst)
		active_links = intel_dp_mst_encoder_active_links(dig_port);
	else if (encoder->base.crtc)
		active_links = to_intel_crtc(encoder->base.crtc)->active;

	drm_WARN_ON(&i915->drm, dig_port->tc_mode != TC_PORT_DISCONNECTED);
	drm_WARN_ON(&i915->drm, dig_port->tc_lock_wakeref);

	tc_cold_wref = tc_cold_block(dig_port, &domain);

	dig_port->tc_mode = intel_tc_port_get_current_mode(dig_port);
	if (active_links) {
		if (!icl_tc_phy_is_connected(dig_port))
			drm_dbg_kms(&i915->drm,
				    "Port %s: PHY disconnected with %d active link(s)\n",
				    dig_port->tc_port_name, active_links);
		intel_tc_port_link_init_refcount(dig_port, active_links);

		dig_port->tc_lock_wakeref = tc_cold_block(dig_port,
							  &dig_port->tc_lock_power_domain);
	} else {
		/*
		 * TBT-alt is the default mode in any case the PHY ownership is not
		 * held (regardless of the sink's connected live state), so
		 * we'll just switch to disconnected mode from it here without
		 * a note.
		 */
		if (dig_port->tc_mode != TC_PORT_TBT_ALT)
			drm_dbg_kms(&i915->drm,
				    "Port %s: PHY left in %s mode on disabled port, disconnecting it\n",
				    dig_port->tc_port_name,
				    tc_port_mode_name(dig_port->tc_mode));
		icl_tc_phy_disconnect(dig_port);
	}

	tc_cold_unblock(dig_port, domain, tc_cold_wref);

	drm_dbg_kms(&i915->drm, "Port %s: sanitize mode (%s)\n",
		    dig_port->tc_port_name,
		    tc_port_mode_name(dig_port->tc_mode));

	mutex_unlock(&dig_port->tc_lock);
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
bool intel_tc_port_connected(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_connected;

	intel_tc_port_lock(dig_port);

	is_connected = tc_port_live_status_mask(dig_port) &
		       BIT(dig_port->tc_mode);

	intel_tc_port_unlock(dig_port);

	return is_connected;
}

static void __intel_tc_port_lock(struct intel_digital_port *dig_port,
				 int required_lanes)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	mutex_lock(&dig_port->tc_lock);

	cancel_delayed_work(&dig_port->tc_disconnect_phy_work);

	if (!dig_port->tc_link_refcount)
		intel_tc_port_update_mode(dig_port, required_lanes,
					  false);

	drm_WARN_ON(&i915->drm, dig_port->tc_mode == TC_PORT_DISCONNECTED);
	drm_WARN_ON(&i915->drm, dig_port->tc_mode != TC_PORT_TBT_ALT &&
				!tc_phy_is_owned(dig_port));
}

void intel_tc_port_lock(struct intel_digital_port *dig_port)
{
	__intel_tc_port_lock(dig_port, 1);
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
	struct intel_digital_port *dig_port =
		container_of(work, struct intel_digital_port, tc_disconnect_phy_work.work);

	mutex_lock(&dig_port->tc_lock);

	if (!dig_port->tc_link_refcount)
		intel_tc_port_update_mode(dig_port, 1, true);

	mutex_unlock(&dig_port->tc_lock);
}

/**
 * intel_tc_port_flush_work: flush the work disconnecting the PHY
 * @dig_port: digital port
 *
 * Flush the delayed work disconnecting an idle PHY.
 */
void intel_tc_port_flush_work(struct intel_digital_port *dig_port)
{
	flush_delayed_work(&dig_port->tc_disconnect_phy_work);
}

void intel_tc_port_unlock(struct intel_digital_port *dig_port)
{
	if (!dig_port->tc_link_refcount && dig_port->tc_mode != TC_PORT_DISCONNECTED)
		queue_delayed_work(system_unbound_wq, &dig_port->tc_disconnect_phy_work,
				   msecs_to_jiffies(1000));

	mutex_unlock(&dig_port->tc_lock);
}

bool intel_tc_port_ref_held(struct intel_digital_port *dig_port)
{
	return mutex_is_locked(&dig_port->tc_lock) ||
	       dig_port->tc_link_refcount;
}

void intel_tc_port_get_link(struct intel_digital_port *dig_port,
			    int required_lanes)
{
	__intel_tc_port_lock(dig_port, required_lanes);
	dig_port->tc_link_refcount++;
	intel_tc_port_unlock(dig_port);
}

void intel_tc_port_put_link(struct intel_digital_port *dig_port)
{
	intel_tc_port_lock(dig_port);
	--dig_port->tc_link_refcount;
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
tc_has_modular_fia(struct drm_i915_private *i915, struct intel_digital_port *dig_port)
{
	enum intel_display_power_domain domain;
	intel_wakeref_t wakeref;
	u32 val;

	if (!INTEL_INFO(i915)->display.has_modular_fia)
		return false;

	mutex_lock(&dig_port->tc_lock);
	wakeref = tc_cold_block(dig_port, &domain);
	val = intel_uncore_read(&i915->uncore, PORT_TX_DFLEXDPSP(FIA1));
	tc_cold_unblock(dig_port, domain, wakeref);
	mutex_unlock(&dig_port->tc_lock);

	drm_WARN_ON(&i915->drm, val == 0xffffffff);

	return val & MODULAR_FIA_MASK;
}

static void
tc_port_load_fia_params(struct drm_i915_private *i915, struct intel_digital_port *dig_port)
{
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(i915, port);

	/*
	 * Each Modular FIA instance houses 2 TC ports. In SOC that has more
	 * than two TC ports, there are multiple instances of Modular FIA.
	 */
	if (tc_has_modular_fia(i915, dig_port)) {
		dig_port->tc_phy_fia = tc_port / 2;
		dig_port->tc_phy_fia_idx = tc_port % 2;
	} else {
		dig_port->tc_phy_fia = FIA1;
		dig_port->tc_phy_fia_idx = tc_port;
	}
}

void intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(i915, port);

	if (drm_WARN_ON(&i915->drm, tc_port == TC_PORT_NONE))
		return;

	snprintf(dig_port->tc_port_name, sizeof(dig_port->tc_port_name),
		 "%c/TC#%d", port_name(port), tc_port + 1);

	mutex_init(&dig_port->tc_lock);
	INIT_DELAYED_WORK(&dig_port->tc_disconnect_phy_work, intel_tc_port_disconnect_phy_work);
	dig_port->tc_legacy_port = is_legacy;
	dig_port->tc_mode = TC_PORT_DISCONNECTED;
	dig_port->tc_link_refcount = 0;
	tc_port_load_fia_params(i915, dig_port);
}
