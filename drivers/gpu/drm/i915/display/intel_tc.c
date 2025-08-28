// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/iopoll.h>

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_atomic.h"
#include "intel_cx0_phy_regs.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_driver.h"
#include "intel_display_power_map.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_dkl_phy_regs.h"
#include "intel_dp.h"
#include "intel_dp_mst.h"
#include "intel_mg_phy_regs.h"
#include "intel_modeset_lock.h"
#include "intel_tc.h"

enum tc_port_mode {
	TC_PORT_DISCONNECTED,
	TC_PORT_TBT_ALT,
	TC_PORT_DP_ALT,
	TC_PORT_LEGACY,
};

struct intel_tc_port;

struct intel_tc_phy_ops {
	enum intel_display_power_domain (*cold_off_domain)(struct intel_tc_port *tc);
	u32 (*hpd_live_status)(struct intel_tc_port *tc);
	bool (*is_ready)(struct intel_tc_port *tc);
	bool (*is_owned)(struct intel_tc_port *tc);
	void (*get_hw_state)(struct intel_tc_port *tc);
	bool (*connect)(struct intel_tc_port *tc, int required_lanes);
	void (*disconnect)(struct intel_tc_port *tc);
	void (*init)(struct intel_tc_port *tc);
};

struct intel_tc_port {
	struct intel_digital_port *dig_port;

	const struct intel_tc_phy_ops *phy_ops;

	struct mutex lock;	/* protects the TypeC port mode */
	intel_wakeref_t lock_wakeref;
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
	enum intel_display_power_domain lock_power_domain;
#endif
	struct delayed_work disconnect_phy_work;
	struct delayed_work link_reset_work;
	int link_refcount;
	bool legacy_port:1;
	const char *port_name;
	enum tc_port_mode mode;
	enum tc_port_mode init_mode;
	enum phy_fia phy_fia;
	enum intel_tc_pin_assignment pin_assignment;
	u8 phy_fia_idx;
	u8 max_lane_count;
};

static enum intel_display_power_domain
tc_phy_cold_off_domain(struct intel_tc_port *);
static u32 tc_phy_hpd_live_status(struct intel_tc_port *tc);
static bool tc_phy_is_ready(struct intel_tc_port *tc);
static bool tc_phy_wait_for_ready(struct intel_tc_port *tc);
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

static bool intel_tc_port_in_mode(struct intel_digital_port *dig_port,
				  enum tc_port_mode mode)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return intel_encoder_is_tc(&dig_port->base) && tc->mode == mode;
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

bool intel_tc_port_handles_hpd_glitches(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return intel_encoder_is_tc(&dig_port->base) && !tc->legacy_port;
}

/*
 * The display power domains used for TC ports depending on the
 * platform and TC mode (legacy, DP-alt, TBT):
 *
 * POWER_DOMAIN_DISPLAY_CORE:
 * --------------------------
 * ADLP/all modes:
 *   - TCSS/IOM access for PHY ready state.
 * ADLP+/all modes:
 *   - DE/north-,south-HPD ISR access for HPD live state.
 *
 * POWER_DOMAIN_PORT_DDI_LANES_<port>:
 * -----------------------------------
 * ICL+/all modes:
 *   - DE/DDI_BUF access for port enabled state.
 * ADLP/all modes:
 *   - DE/DDI_BUF access for PHY owned state.
 *
 * POWER_DOMAIN_AUX_USBC<TC port index>:
 * -------------------------------------
 * ICL/legacy mode:
 *   - TCSS/IOM,FIA access for PHY ready, owned and HPD live state
 *   - TCSS/PHY: block TC-cold power state for using the PHY AUX and
 *     main lanes.
 * ADLP/legacy, DP-alt modes:
 *   - TCSS/PHY: block TC-cold power state for using the PHY AUX and
 *     main lanes.
 *
 * POWER_DOMAIN_TC_COLD_OFF:
 * -------------------------
 * ICL/DP-alt, TBT mode:
 *   - TCSS/TBT: block TC-cold power state for using the (direct or
 *     TBT DP-IN) AUX and main lanes.
 *
 * TGL/all modes:
 *   - TCSS/IOM,FIA access for PHY ready, owned and HPD live state
 *   - TCSS/PHY: block TC-cold power state for using the (direct or
 *     TBT DP-IN) AUX and main lanes.
 *
 * ADLP/TBT mode:
 *   - TCSS/TBT: block TC-cold power state for using the (TBT DP-IN)
 *     AUX and main lanes.
 *
 * XELPDP+/all modes:
 *   - TCSS/IOM,FIA access for PHY ready, owned state
 *   - TCSS/PHY: block TC-cold power state for using the (direct or
 *     TBT DP-IN) AUX and main lanes.
 */
bool intel_tc_cold_requires_aux_pw(struct intel_digital_port *dig_port)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	return tc_phy_cold_off_domain(tc) ==
	       intel_display_power_legacy_aux_domain(display, dig_port->aux_ch);
}

static intel_wakeref_t
__tc_cold_block(struct intel_tc_port *tc, enum intel_display_power_domain *domain)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	*domain = tc_phy_cold_off_domain(tc);

	return intel_display_power_get(display, *domain);
}

static intel_wakeref_t
tc_cold_block(struct intel_tc_port *tc)
{
	enum intel_display_power_domain domain;
	intel_wakeref_t wakeref;

	wakeref = __tc_cold_block(tc, &domain);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
	tc->lock_power_domain = domain;
#endif
	return wakeref;
}

static void
__tc_cold_unblock(struct intel_tc_port *tc, enum intel_display_power_domain domain,
		  intel_wakeref_t wakeref)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	intel_display_power_put(display, domain, wakeref);
}

static void
tc_cold_unblock(struct intel_tc_port *tc, intel_wakeref_t wakeref)
{
	struct intel_display __maybe_unused *display = to_intel_display(tc->dig_port);
	enum intel_display_power_domain domain = tc_phy_cold_off_domain(tc);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
	drm_WARN_ON(display->drm, tc->lock_power_domain != domain);
#endif
	__tc_cold_unblock(tc, domain, wakeref);
}

static void
assert_display_core_power_enabled(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	drm_WARN_ON(display->drm,
		    !intel_display_power_is_enabled(display, POWER_DOMAIN_DISPLAY_CORE));
}

static void
assert_tc_cold_blocked(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	bool enabled;

	enabled = intel_display_power_is_enabled(display,
						 tc_phy_cold_off_domain(tc));
	drm_WARN_ON(display->drm, !enabled);
}

static enum intel_display_power_domain
tc_port_power_domain(struct intel_tc_port *tc)
{
	enum tc_port tc_port = intel_encoder_to_tc(&tc->dig_port->base);

	if (tc_port == TC_PORT_NONE)
		return POWER_DOMAIN_INVALID;

	return POWER_DOMAIN_PORT_DDI_LANES_TC1 + tc_port - TC_PORT_1;
}

static void
assert_tc_port_power_enabled(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	drm_WARN_ON(display->drm,
		    !intel_display_power_is_enabled(display, tc_port_power_domain(tc)));
}

static u32 get_lane_mask(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	intel_wakeref_t wakeref;
	u32 lane_mask;

	with_intel_display_power(display, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		lane_mask = intel_de_read(display, PORT_TX_DFLEXDPSP(tc->phy_fia));

	drm_WARN_ON(display->drm, lane_mask == 0xffffffff);
	assert_tc_cold_blocked(tc);

	lane_mask &= DP_LANE_ASSIGNMENT_MASK(tc->phy_fia_idx);
	return lane_mask >> DP_LANE_ASSIGNMENT_SHIFT(tc->phy_fia_idx);
}

static char pin_assignment_name(enum intel_tc_pin_assignment pin_assignment)
{
	if (pin_assignment == INTEL_TC_PIN_ASSIGNMENT_NONE)
		return '-';

	return 'A' + pin_assignment - INTEL_TC_PIN_ASSIGNMENT_A;
}

static enum intel_tc_pin_assignment
get_pin_assignment(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum tc_port tc_port = intel_encoder_to_tc(&tc->dig_port->base);
	enum intel_tc_pin_assignment pin_assignment;
	intel_wakeref_t wakeref;
	i915_reg_t reg;
	u32 mask;
	u32 val;

	if (tc->mode == TC_PORT_TBT_ALT)
		return INTEL_TC_PIN_ASSIGNMENT_NONE;

	if (DISPLAY_VER(display) >= 20) {
		reg = TCSS_DDI_STATUS(tc_port);
		mask = TCSS_DDI_STATUS_PIN_ASSIGNMENT_MASK;
	} else {
		reg = PORT_TX_DFLEXPA1(tc->phy_fia);
		mask = DP_PIN_ASSIGNMENT_MASK(tc->phy_fia_idx);
	}

	with_intel_display_power(display, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		val = intel_de_read(display, reg);

	drm_WARN_ON(display->drm, val == 0xffffffff);
	assert_tc_cold_blocked(tc);

	pin_assignment = (val & mask) >> (ffs(mask) - 1);

	switch (pin_assignment) {
	case INTEL_TC_PIN_ASSIGNMENT_A:
	case INTEL_TC_PIN_ASSIGNMENT_B:
	case INTEL_TC_PIN_ASSIGNMENT_F:
		drm_WARN_ON(display->drm, DISPLAY_VER(display) > 11);
		break;
	case INTEL_TC_PIN_ASSIGNMENT_NONE:
	case INTEL_TC_PIN_ASSIGNMENT_C:
	case INTEL_TC_PIN_ASSIGNMENT_D:
	case INTEL_TC_PIN_ASSIGNMENT_E:
		break;
	default:
		MISSING_CASE(pin_assignment);
	}

	return pin_assignment;
}

static int mtl_get_max_lane_count(struct intel_tc_port *tc)
{
	enum intel_tc_pin_assignment pin_assignment;

	pin_assignment = get_pin_assignment(tc);

	switch (pin_assignment) {
	case INTEL_TC_PIN_ASSIGNMENT_NONE:
		return 0;
	default:
		MISSING_CASE(pin_assignment);
		fallthrough;
	case INTEL_TC_PIN_ASSIGNMENT_D:
		return 2;
	case INTEL_TC_PIN_ASSIGNMENT_C:
	case INTEL_TC_PIN_ASSIGNMENT_E:
		return 4;
	}
}

static int icl_get_max_lane_count(struct intel_tc_port *tc)
{
	u32 lane_mask = 0;

	lane_mask = get_lane_mask(tc);

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

static int get_max_lane_count(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	if (tc->mode != TC_PORT_DP_ALT)
		return 4;

	if (DISPLAY_VER(display) >= 14)
		return mtl_get_max_lane_count(tc);

	return icl_get_max_lane_count(tc);
}

static void read_pin_configuration(struct intel_tc_port *tc)
{
	tc->pin_assignment = get_pin_assignment(tc);
	tc->max_lane_count = get_max_lane_count(tc);
}

int intel_tc_port_max_lane_count(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	if (!intel_encoder_is_tc(&dig_port->base))
		return 4;

	return tc->max_lane_count;
}

enum intel_tc_pin_assignment
intel_tc_port_get_pin_assignment(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	if (!intel_encoder_is_tc(&dig_port->base))
		return INTEL_TC_PIN_ASSIGNMENT_NONE;

	return tc->pin_assignment;
}

void intel_tc_port_set_fia_lane_count(struct intel_digital_port *dig_port,
				      int required_lanes)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	bool lane_reversal = dig_port->lane_reversal;
	u32 val;

	if (DISPLAY_VER(display) >= 14)
		return;

	drm_WARN_ON(display->drm,
		    lane_reversal && tc->mode != TC_PORT_LEGACY);

	assert_tc_cold_blocked(tc);

	val = intel_de_read(display, PORT_TX_DFLEXDPMLE1(tc->phy_fia));
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

	intel_de_write(display, PORT_TX_DFLEXDPMLE1(tc->phy_fia), val);
}

static void tc_port_fixup_legacy_flag(struct intel_tc_port *tc,
				      u32 live_status_mask)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 valid_hpd_mask;

	drm_WARN_ON(display->drm, tc->mode != TC_PORT_DISCONNECTED);

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
	drm_dbg_kms(display->drm,
		    "Port %s: live status %08x mismatches the legacy port flag %08x, fixing flag\n",
		    tc->port_name, live_status_mask, valid_hpd_mask);

	tc->legacy_port = !tc->legacy_port;
}

static void tc_phy_load_fia_params(struct intel_tc_port *tc, bool modular_fia)
{
	enum tc_port tc_port = intel_encoder_to_tc(&tc->dig_port->base);

	/*
	 * Each Modular FIA instance houses 2 TC ports. In SOC that has more
	 * than two TC ports, there are multiple instances of Modular FIA.
	 */
	if (modular_fia) {
		tc->phy_fia = tc_port / 2;
		tc->phy_fia_idx = tc_port % 2;
	} else {
		tc->phy_fia = FIA1;
		tc->phy_fia_idx = tc_port;
	}
}

/*
 * ICL TC PHY handlers
 * -------------------
 */
static enum intel_display_power_domain
icl_tc_phy_cold_off_domain(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;

	if (tc->legacy_port)
		return intel_display_power_legacy_aux_domain(display, dig_port->aux_ch);

	return POWER_DOMAIN_TC_COLD_OFF;
}

static u32 icl_tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	u32 isr_bit = display->hotplug.pch_hpd[dig_port->base.hpd_pin];
	intel_wakeref_t wakeref;
	u32 fia_isr;
	u32 pch_isr;
	u32 mask = 0;

	with_intel_display_power(display, tc_phy_cold_off_domain(tc), wakeref) {
		fia_isr = intel_de_read(display, PORT_TX_DFLEXDPSP(tc->phy_fia));
		pch_isr = intel_de_read(display, SDEISR);
	}

	if (fia_isr == 0xffffffff) {
		drm_dbg_kms(display->drm,
			    "Port %s: PHY in TCCOLD, nothing connected\n",
			    tc->port_name);
		return mask;
	}

	if (fia_isr & TC_LIVE_STATE_TBT(tc->phy_fia_idx))
		mask |= BIT(TC_PORT_TBT_ALT);
	if (fia_isr & TC_LIVE_STATE_TC(tc->phy_fia_idx))
		mask |= BIT(TC_PORT_DP_ALT);

	if (pch_isr & isr_bit)
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
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 val;

	assert_tc_cold_blocked(tc);

	val = intel_de_read(display, PORT_TX_DFLEXDPPMS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(display->drm,
			    "Port %s: PHY in TCCOLD, assuming not ready\n",
			    tc->port_name);
		return false;
	}

	return val & DP_PHY_MODE_STATUS_COMPLETED(tc->phy_fia_idx);
}

static bool icl_tc_phy_take_ownership(struct intel_tc_port *tc,
				      bool take)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 val;

	assert_tc_cold_blocked(tc);

	val = intel_de_read(display, PORT_TX_DFLEXDPCSSS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(display->drm,
			    "Port %s: PHY in TCCOLD, can't %s ownership\n",
			    tc->port_name, take ? "take" : "release");

		return false;
	}

	val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(tc->phy_fia_idx);
	if (take)
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(tc->phy_fia_idx);

	intel_de_write(display, PORT_TX_DFLEXDPCSSS(tc->phy_fia), val);

	return true;
}

static bool icl_tc_phy_is_owned(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 val;

	assert_tc_cold_blocked(tc);

	val = intel_de_read(display, PORT_TX_DFLEXDPCSSS(tc->phy_fia));
	if (val == 0xffffffff) {
		drm_dbg_kms(display->drm,
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

	tc_cold_wref = __tc_cold_block(tc, &domain);

	tc->mode = tc_phy_get_current_mode(tc);
	if (tc->mode != TC_PORT_DISCONNECTED) {
		tc->lock_wakeref = tc_cold_block(tc);

		read_pin_configuration(tc);
	}

	__tc_cold_unblock(tc, domain, tc_cold_wref);
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
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	int max_lanes;

	max_lanes = intel_tc_port_max_lane_count(dig_port);
	if (tc->mode == TC_PORT_LEGACY) {
		drm_WARN_ON(display->drm, max_lanes != 4);
		return true;
	}

	drm_WARN_ON(display->drm, tc->mode != TC_PORT_DP_ALT);

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (!(tc_phy_hpd_live_status(tc) & BIT(TC_PORT_DP_ALT))) {
		drm_dbg_kms(display->drm, "Port %s: PHY sudden disconnect\n",
			    tc->port_name);
		return false;
	}

	if (max_lanes < required_lanes) {
		drm_dbg_kms(display->drm,
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
	struct intel_display *display = to_intel_display(tc->dig_port);

	tc->lock_wakeref = tc_cold_block(tc);

	if (tc->mode == TC_PORT_TBT_ALT) {
		read_pin_configuration(tc);

		return true;
	}

	if ((!tc_phy_is_ready(tc) ||
	     !icl_tc_phy_take_ownership(tc, true)) &&
	    !drm_WARN_ON(display->drm, tc->mode == TC_PORT_LEGACY)) {
		drm_dbg_kms(display->drm, "Port %s: can't take PHY ownership (ready %s)\n",
			    tc->port_name,
			    str_yes_no(tc_phy_is_ready(tc)));
		goto out_unblock_tc_cold;
	}

	read_pin_configuration(tc);

	if (!tc_phy_verify_legacy_or_dp_alt_mode(tc, required_lanes))
		goto out_release_phy;

	return true;

out_release_phy:
	icl_tc_phy_take_ownership(tc, false);
out_unblock_tc_cold:
	tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));

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
		icl_tc_phy_take_ownership(tc, false);
		fallthrough;
	case TC_PORT_TBT_ALT:
		tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));
		break;
	default:
		MISSING_CASE(tc->mode);
	}
}

static void icl_tc_phy_init(struct intel_tc_port *tc)
{
	tc_phy_load_fia_params(tc, false);
}

static const struct intel_tc_phy_ops icl_tc_phy_ops = {
	.cold_off_domain = icl_tc_phy_cold_off_domain,
	.hpd_live_status = icl_tc_phy_hpd_live_status,
	.is_ready = icl_tc_phy_is_ready,
	.is_owned = icl_tc_phy_is_owned,
	.get_hw_state = icl_tc_phy_get_hw_state,
	.connect = icl_tc_phy_connect,
	.disconnect = icl_tc_phy_disconnect,
	.init = icl_tc_phy_init,
};

/*
 * TGL TC PHY handlers
 * -------------------
 */
static enum intel_display_power_domain
tgl_tc_phy_cold_off_domain(struct intel_tc_port *tc)
{
	return POWER_DOMAIN_TC_COLD_OFF;
}

static void tgl_tc_phy_init(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	intel_wakeref_t wakeref;
	u32 val;

	with_intel_display_power(display, tc_phy_cold_off_domain(tc), wakeref)
		val = intel_de_read(display, PORT_TX_DFLEXDPSP(FIA1));

	drm_WARN_ON(display->drm, val == 0xffffffff);

	tc_phy_load_fia_params(tc, val & MODULAR_FIA_MASK);
}

static const struct intel_tc_phy_ops tgl_tc_phy_ops = {
	.cold_off_domain = tgl_tc_phy_cold_off_domain,
	.hpd_live_status = icl_tc_phy_hpd_live_status,
	.is_ready = icl_tc_phy_is_ready,
	.is_owned = icl_tc_phy_is_owned,
	.get_hw_state = icl_tc_phy_get_hw_state,
	.connect = icl_tc_phy_connect,
	.disconnect = icl_tc_phy_disconnect,
	.init = tgl_tc_phy_init,
};

/*
 * ADLP TC PHY handlers
 * --------------------
 */
static enum intel_display_power_domain
adlp_tc_phy_cold_off_domain(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;

	if (tc->mode != TC_PORT_TBT_ALT)
		return intel_display_power_legacy_aux_domain(display, dig_port->aux_ch);

	return POWER_DOMAIN_TC_COLD_OFF;
}

static u32 adlp_tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum hpd_pin hpd_pin = dig_port->base.hpd_pin;
	u32 cpu_isr_bits = display->hotplug.hpd[hpd_pin];
	u32 pch_isr_bit = display->hotplug.pch_hpd[hpd_pin];
	intel_wakeref_t wakeref;
	u32 cpu_isr;
	u32 pch_isr;
	u32 mask = 0;

	with_intel_display_power(display, POWER_DOMAIN_DISPLAY_CORE, wakeref) {
		cpu_isr = intel_de_read(display, GEN11_DE_HPD_ISR);
		pch_isr = intel_de_read(display, SDEISR);
	}

	if (cpu_isr & (cpu_isr_bits & GEN11_DE_TC_HOTPLUG_MASK))
		mask |= BIT(TC_PORT_DP_ALT);
	if (cpu_isr & (cpu_isr_bits & GEN11_DE_TBT_HOTPLUG_MASK))
		mask |= BIT(TC_PORT_TBT_ALT);

	if (pch_isr & pch_isr_bit)
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
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum tc_port tc_port = intel_encoder_to_tc(&tc->dig_port->base);
	u32 val;

	assert_display_core_power_enabled(tc);

	val = intel_de_read(display, TCSS_DDI_STATUS(tc_port));
	if (val == 0xffffffff) {
		drm_dbg_kms(display->drm,
			    "Port %s: PHY in TCCOLD, assuming not ready\n",
			    tc->port_name);
		return false;
	}

	return val & TCSS_DDI_STATUS_READY;
}

static bool adlp_tc_phy_take_ownership(struct intel_tc_port *tc,
				       bool take)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;

	assert_tc_port_power_enabled(tc);

	intel_de_rmw(display, DDI_BUF_CTL(port), DDI_BUF_CTL_TC_PHY_OWNERSHIP,
		     take ? DDI_BUF_CTL_TC_PHY_OWNERSHIP : 0);

	return true;
}

static bool adlp_tc_phy_is_owned(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;
	u32 val;

	assert_tc_port_power_enabled(tc);

	val = intel_de_read(display, DDI_BUF_CTL(port));
	return val & DDI_BUF_CTL_TC_PHY_OWNERSHIP;
}

static void adlp_tc_phy_get_hw_state(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum intel_display_power_domain port_power_domain =
		tc_port_power_domain(tc);
	intel_wakeref_t port_wakeref;

	port_wakeref = intel_display_power_get(display, port_power_domain);

	tc->mode = tc_phy_get_current_mode(tc);
	if (tc->mode != TC_PORT_DISCONNECTED) {
		tc->lock_wakeref = tc_cold_block(tc);

		read_pin_configuration(tc);
	}

	intel_display_power_put(display, port_power_domain, port_wakeref);
}

static bool adlp_tc_phy_connect(struct intel_tc_port *tc, int required_lanes)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum intel_display_power_domain port_power_domain =
		tc_port_power_domain(tc);
	intel_wakeref_t port_wakeref;

	if (tc->mode == TC_PORT_TBT_ALT) {
		tc->lock_wakeref = tc_cold_block(tc);

		read_pin_configuration(tc);

		return true;
	}

	port_wakeref = intel_display_power_get(display, port_power_domain);

	if (!adlp_tc_phy_take_ownership(tc, true) &&
	    !drm_WARN_ON(display->drm, tc->mode == TC_PORT_LEGACY)) {
		drm_dbg_kms(display->drm, "Port %s: can't take PHY ownership\n",
			    tc->port_name);
		goto out_put_port_power;
	}

	if (!tc_phy_is_ready(tc) &&
	    !drm_WARN_ON(display->drm, tc->mode == TC_PORT_LEGACY)) {
		drm_dbg_kms(display->drm, "Port %s: PHY not ready\n",
			    tc->port_name);
		goto out_release_phy;
	}

	tc->lock_wakeref = tc_cold_block(tc);

	read_pin_configuration(tc);

	if (!tc_phy_verify_legacy_or_dp_alt_mode(tc, required_lanes))
		goto out_unblock_tc_cold;

	intel_display_power_put(display, port_power_domain, port_wakeref);

	return true;

out_unblock_tc_cold:
	tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));
out_release_phy:
	adlp_tc_phy_take_ownership(tc, false);
out_put_port_power:
	intel_display_power_put(display, port_power_domain, port_wakeref);

	return false;
}

static void adlp_tc_phy_disconnect(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum intel_display_power_domain port_power_domain =
		tc_port_power_domain(tc);
	intel_wakeref_t port_wakeref;

	port_wakeref = intel_display_power_get(display, port_power_domain);

	tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));

	switch (tc->mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		adlp_tc_phy_take_ownership(tc, false);
		fallthrough;
	case TC_PORT_TBT_ALT:
		break;
	default:
		MISSING_CASE(tc->mode);
	}

	intel_display_power_put(display, port_power_domain, port_wakeref);
}

static void adlp_tc_phy_init(struct intel_tc_port *tc)
{
	tc_phy_load_fia_params(tc, true);
}

static const struct intel_tc_phy_ops adlp_tc_phy_ops = {
	.cold_off_domain = adlp_tc_phy_cold_off_domain,
	.hpd_live_status = adlp_tc_phy_hpd_live_status,
	.is_ready = adlp_tc_phy_is_ready,
	.is_owned = adlp_tc_phy_is_owned,
	.get_hw_state = adlp_tc_phy_get_hw_state,
	.connect = adlp_tc_phy_connect,
	.disconnect = adlp_tc_phy_disconnect,
	.init = adlp_tc_phy_init,
};

/*
 * XELPDP TC PHY handlers
 * ----------------------
 */
static u32 xelpdp_tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum hpd_pin hpd_pin = dig_port->base.hpd_pin;
	u32 pica_isr_bits = display->hotplug.hpd[hpd_pin];
	u32 pch_isr_bit = display->hotplug.pch_hpd[hpd_pin];
	intel_wakeref_t wakeref;
	u32 pica_isr;
	u32 pch_isr;
	u32 mask = 0;

	with_intel_display_power(display, POWER_DOMAIN_DISPLAY_CORE, wakeref) {
		pica_isr = intel_de_read(display, PICAINTERRUPT_ISR);
		pch_isr = intel_de_read(display, SDEISR);
	}

	if (pica_isr & (pica_isr_bits & XELPDP_DP_ALT_HOTPLUG_MASK))
		mask |= BIT(TC_PORT_DP_ALT);
	if (pica_isr & (pica_isr_bits & XELPDP_TBT_HOTPLUG_MASK))
		mask |= BIT(TC_PORT_TBT_ALT);

	if (tc->legacy_port && (pch_isr & pch_isr_bit))
		mask |= BIT(TC_PORT_LEGACY);

	return mask;
}

static bool
xelpdp_tc_phy_tcss_power_is_enabled(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;
	i915_reg_t reg = XELPDP_PORT_BUF_CTL1(display, port);

	assert_tc_cold_blocked(tc);

	return intel_de_read(display, reg) & XELPDP_TCSS_POWER_STATE;
}

static bool
xelpdp_tc_phy_wait_for_tcss_power(struct intel_tc_port *tc, bool enabled)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	bool is_enabled;
	int ret;

	ret = poll_timeout_us(is_enabled = xelpdp_tc_phy_tcss_power_is_enabled(tc),
			      is_enabled == enabled,
			      200, 5000, false);
	if (ret) {
		drm_dbg_kms(display->drm,
			    "Port %s: timeout waiting for TCSS power to get %s\n",
			    str_enabled_disabled(enabled),
			    tc->port_name);
		return false;
	}

	return true;
}

/*
 * Gfx driver WA 14020908590 for PTL tcss_rxdetect_clkswb_req/ack
 * handshake violation when pwwreq= 0->1 during TC7/10 entry
 */
static void xelpdp_tc_power_request_wa(struct intel_display *display, bool enable)
{
	/* check if mailbox is running busy */
	if (intel_de_wait_for_clear(display, TCSS_DISP_MAILBOX_IN_CMD,
				    TCSS_DISP_MAILBOX_IN_CMD_RUN_BUSY, 10)) {
		drm_dbg_kms(display->drm,
			    "Timeout waiting for TCSS mailbox run/busy bit to clear\n");
		return;
	}

	intel_de_write(display, TCSS_DISP_MAILBOX_IN_DATA, enable ? 1 : 0);
	intel_de_write(display, TCSS_DISP_MAILBOX_IN_CMD,
		       TCSS_DISP_MAILBOX_IN_CMD_RUN_BUSY |
		       TCSS_DISP_MAILBOX_IN_CMD_DATA(0x1));

	/* wait to clear mailbox running busy bit before continuing */
	if (intel_de_wait_for_clear(display, TCSS_DISP_MAILBOX_IN_CMD,
				    TCSS_DISP_MAILBOX_IN_CMD_RUN_BUSY, 10)) {
		drm_dbg_kms(display->drm,
			    "Timeout after writing data to mailbox. Mailbox run/busy bit did not clear\n");
		return;
	}
}

static void __xelpdp_tc_phy_enable_tcss_power(struct intel_tc_port *tc, bool enable)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;
	i915_reg_t reg = XELPDP_PORT_BUF_CTL1(display, port);
	u32 val;

	assert_tc_cold_blocked(tc);

	if (DISPLAY_VER(display) == 30)
		xelpdp_tc_power_request_wa(display, enable);

	val = intel_de_read(display, reg);
	if (enable)
		val |= XELPDP_TCSS_POWER_REQUEST;
	else
		val &= ~XELPDP_TCSS_POWER_REQUEST;
	intel_de_write(display, reg, val);
}

static bool xelpdp_tc_phy_enable_tcss_power(struct intel_tc_port *tc, bool enable)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	__xelpdp_tc_phy_enable_tcss_power(tc, enable);

	if (enable && !tc_phy_wait_for_ready(tc))
		goto out_disable;

	if (!xelpdp_tc_phy_wait_for_tcss_power(tc, enable))
		goto out_disable;

	return true;

out_disable:
	if (drm_WARN_ON(display->drm, tc->mode == TC_PORT_LEGACY))
		return false;

	if (!enable)
		return false;

	__xelpdp_tc_phy_enable_tcss_power(tc, false);
	xelpdp_tc_phy_wait_for_tcss_power(tc, false);

	return false;
}

static void xelpdp_tc_phy_take_ownership(struct intel_tc_port *tc, bool take)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;
	i915_reg_t reg = XELPDP_PORT_BUF_CTL1(display, port);
	u32 val;

	assert_tc_cold_blocked(tc);

	val = intel_de_read(display, reg);
	if (take)
		val |= XELPDP_TC_PHY_OWNERSHIP;
	else
		val &= ~XELPDP_TC_PHY_OWNERSHIP;
	intel_de_write(display, reg, val);
}

static bool xelpdp_tc_phy_is_owned(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	enum port port = tc->dig_port->base.port;
	i915_reg_t reg = XELPDP_PORT_BUF_CTL1(display, port);

	assert_tc_cold_blocked(tc);

	return intel_de_read(display, reg) & XELPDP_TC_PHY_OWNERSHIP;
}

static void xelpdp_tc_phy_get_hw_state(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	intel_wakeref_t tc_cold_wref;
	enum intel_display_power_domain domain;

	tc_cold_wref = __tc_cold_block(tc, &domain);

	tc->mode = tc_phy_get_current_mode(tc);
	if (tc->mode != TC_PORT_DISCONNECTED) {
		tc->lock_wakeref = tc_cold_block(tc);

		read_pin_configuration(tc);
		/*
		 * Set a valid lane count value for a DP-alt sink which got
		 * disconnected. The driver can only disable the output on this PHY.
		 */
		if (tc->max_lane_count == 0)
			tc->max_lane_count = 4;
	}

	drm_WARN_ON(display->drm,
		    (tc->mode == TC_PORT_DP_ALT || tc->mode == TC_PORT_LEGACY) &&
		    !xelpdp_tc_phy_tcss_power_is_enabled(tc));

	__tc_cold_unblock(tc, domain, tc_cold_wref);
}

static bool xelpdp_tc_phy_connect(struct intel_tc_port *tc, int required_lanes)
{
	tc->lock_wakeref = tc_cold_block(tc);

	if (tc->mode == TC_PORT_TBT_ALT) {
		read_pin_configuration(tc);

		return true;
	}

	if (!xelpdp_tc_phy_enable_tcss_power(tc, true))
		goto out_unblock_tccold;

	xelpdp_tc_phy_take_ownership(tc, true);

	read_pin_configuration(tc);

	if (!tc_phy_verify_legacy_or_dp_alt_mode(tc, required_lanes))
		goto out_release_phy;

	return true;

out_release_phy:
	xelpdp_tc_phy_take_ownership(tc, false);
	xelpdp_tc_phy_wait_for_tcss_power(tc, false);

out_unblock_tccold:
	tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));

	return false;
}

static void xelpdp_tc_phy_disconnect(struct intel_tc_port *tc)
{
	switch (tc->mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		xelpdp_tc_phy_take_ownership(tc, false);
		xelpdp_tc_phy_enable_tcss_power(tc, false);
		fallthrough;
	case TC_PORT_TBT_ALT:
		tc_cold_unblock(tc, fetch_and_zero(&tc->lock_wakeref));
		break;
	default:
		MISSING_CASE(tc->mode);
	}
}

static const struct intel_tc_phy_ops xelpdp_tc_phy_ops = {
	.cold_off_domain = tgl_tc_phy_cold_off_domain,
	.hpd_live_status = xelpdp_tc_phy_hpd_live_status,
	.is_ready = adlp_tc_phy_is_ready,
	.is_owned = xelpdp_tc_phy_is_owned,
	.get_hw_state = xelpdp_tc_phy_get_hw_state,
	.connect = xelpdp_tc_phy_connect,
	.disconnect = xelpdp_tc_phy_disconnect,
	.init = adlp_tc_phy_init,
};

/*
 * Generic TC PHY handlers
 * -----------------------
 */
static enum intel_display_power_domain
tc_phy_cold_off_domain(struct intel_tc_port *tc)
{
	return tc->phy_ops->cold_off_domain(tc);
}

static u32 tc_phy_hpd_live_status(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 mask;

	mask = tc->phy_ops->hpd_live_status(tc);

	/* The sink can be connected only in a single mode. */
	drm_WARN_ON_ONCE(display->drm, hweight32(mask) > 1);

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

/* Is the PHY owned by display i.e. is it in legacy or DP-alt mode? */
static bool tc_phy_owned_by_display(struct intel_tc_port *tc,
				    bool phy_is_ready, bool phy_is_owned)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	if (DISPLAY_VER(display) < 20) {
		drm_WARN_ON(display->drm, phy_is_owned && !phy_is_ready);

		return phy_is_ready && phy_is_owned;
	} else {
		return phy_is_owned;
	}
}

static bool tc_phy_is_connected(struct intel_tc_port *tc,
				enum icl_port_dpll_id port_pll_type)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	bool phy_is_ready = tc_phy_is_ready(tc);
	bool phy_is_owned = tc_phy_is_owned(tc);
	bool is_connected;

	if (tc_phy_owned_by_display(tc, phy_is_ready, phy_is_owned))
		is_connected = port_pll_type == ICL_PORT_DPLL_MG_PHY;
	else
		is_connected = port_pll_type == ICL_PORT_DPLL_DEFAULT;

	drm_dbg_kms(display->drm,
		    "Port %s: PHY connected: %s (ready: %s, owned: %s, pll_type: %s)\n",
		    tc->port_name,
		    str_yes_no(is_connected),
		    str_yes_no(phy_is_ready),
		    str_yes_no(phy_is_owned),
		    port_pll_type == ICL_PORT_DPLL_DEFAULT ? "tbt" : "non-tbt");

	return is_connected;
}

static bool tc_phy_wait_for_ready(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	bool is_ready;
	int ret;

	ret = poll_timeout_us(is_ready = tc_phy_is_ready(tc),
			      is_ready,
			      1000, 500 * 1000, false);
	if (ret) {
		drm_err(display->drm, "Port %s: timeout waiting for PHY ready\n",
			tc->port_name);

		return false;
	}

	return true;
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
	struct intel_display *display = to_intel_display(tc->dig_port);
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

	if (!tc_phy_owned_by_display(tc, phy_is_ready, phy_is_owned)) {
		mode = get_tc_mode_in_phy_not_owned_state(tc, live_mode);
	} else {
		drm_WARN_ON(display->drm, live_mode == TC_PORT_TBT_ALT);
		mode = get_tc_mode_in_phy_owned_state(tc, live_mode);
	}

	drm_dbg_kms(display->drm,
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
	struct intel_display *display = to_intel_display(tc->dig_port);
	u32 live_status_mask = tc_phy_hpd_live_status(tc);
	bool connected;

	tc_port_fixup_legacy_flag(tc, live_status_mask);

	tc->mode = hpd_mask_to_target_mode(tc, live_status_mask);

	connected = tc->phy_ops->connect(tc, required_lanes);
	if (!connected && tc->mode != default_tc_mode(tc)) {
		tc->mode = default_tc_mode(tc);
		connected = tc->phy_ops->connect(tc, required_lanes);
	}

	drm_WARN_ON(display->drm, !connected);
}

static void tc_phy_disconnect(struct intel_tc_port *tc)
{
	if (tc->mode != TC_PORT_DISCONNECTED) {
		tc->phy_ops->disconnect(tc);
		tc->mode = TC_PORT_DISCONNECTED;
	}
}

static void tc_phy_init(struct intel_tc_port *tc)
{
	mutex_lock(&tc->lock);
	tc->phy_ops->init(tc);
	mutex_unlock(&tc->lock);
}

static void intel_tc_port_reset_mode(struct intel_tc_port *tc,
				     int required_lanes, bool force_disconnect)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum tc_port_mode old_tc_mode = tc->mode;

	intel_display_power_flush_work(display);
	if (!intel_tc_cold_requires_aux_pw(dig_port)) {
		enum intel_display_power_domain aux_domain;

		aux_domain = intel_aux_power_domain(dig_port);
		if (intel_display_power_is_enabled(display, aux_domain))
			drm_dbg_kms(display->drm, "Port %s: AUX unexpectedly powered\n",
				    tc->port_name);
	}

	tc_phy_disconnect(tc);
	if (!force_disconnect)
		tc_phy_connect(tc, required_lanes);

	drm_dbg_kms(display->drm,
		    "Port %s: TC port mode reset (%s -> %s) pin assignment: %c max lanes: %d\n",
		    tc->port_name,
		    tc_port_mode_name(old_tc_mode),
		    tc_port_mode_name(tc->mode),
		    pin_assignment_name(tc->pin_assignment),
		    tc->max_lane_count);
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
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;

	assert_tc_port_power_enabled(tc);

	return intel_de_read(display, DDI_BUF_CTL(dig_port->base.port)) &
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
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	bool update_mode = false;

	mutex_lock(&tc->lock);

	drm_WARN_ON(display->drm, tc->mode != TC_PORT_DISCONNECTED);
	drm_WARN_ON(display->drm, tc->lock_wakeref);
	drm_WARN_ON(display->drm, tc->link_refcount);

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
		drm_WARN_ON(display->drm, !tc->legacy_port);
		drm_err(display->drm,
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

static bool tc_port_has_active_streams(struct intel_tc_port *tc,
				       const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	enum icl_port_dpll_id pll_type = ICL_PORT_DPLL_DEFAULT;
	int active_streams = 0;

	if (dig_port->dp.is_mst) {
		/* TODO: get the PLL type for MST, once HW readout is done for it. */
		active_streams = intel_dp_mst_active_streams(&dig_port->dp);
	} else if (crtc_state && crtc_state->hw.active) {
		pll_type = intel_ddi_port_pll_type(&dig_port->base, crtc_state);
		active_streams = 1;
	}

	if (active_streams && !tc_phy_is_connected(tc, pll_type))
		drm_err(display->drm,
			"Port %s: PHY disconnected with %d active stream(s)\n",
			tc->port_name, active_streams);

	return active_streams;
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
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_tc_port *tc = to_tc_port(dig_port);

	mutex_lock(&tc->lock);

	drm_WARN_ON(display->drm, tc->link_refcount != 1);
	if (!tc_port_has_active_streams(tc, crtc_state)) {
		/*
		 * TBT-alt is the default mode in any case the PHY ownership is not
		 * held (regardless of the sink's connected live state), so
		 * we'll just switch to disconnected mode from it here without
		 * a note.
		 */
		if (tc->init_mode != TC_PORT_TBT_ALT &&
		    tc->init_mode != TC_PORT_DISCONNECTED)
			drm_dbg_kms(display->drm,
				    "Port %s: PHY left in %s mode on disabled port, disconnecting it\n",
				    tc->port_name,
				    tc_port_mode_name(tc->init_mode));
		tc_phy_disconnect(tc);
		__intel_tc_port_put_link(tc);
	}

	drm_dbg_kms(display->drm, "Port %s: sanitize mode (%s) pin assignment: %c max lanes: %d\n",
		    tc->port_name,
		    tc_port_mode_name(tc->mode),
		    pin_assignment_name(tc->pin_assignment),
		    tc->max_lane_count);

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
bool intel_tc_port_connected(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_tc_port *tc = to_tc_port(dig_port);
	u32 mask = ~0;

	drm_WARN_ON(display->drm, !intel_tc_port_ref_held(dig_port));

	if (tc->mode != TC_PORT_DISCONNECTED)
		mask = BIT(tc->mode);

	return tc_phy_hpd_live_status(tc) & mask;
}

static bool __intel_tc_port_link_needs_reset(struct intel_tc_port *tc)
{
	bool ret;

	mutex_lock(&tc->lock);

	ret = tc->link_refcount &&
	      tc->mode == TC_PORT_DP_ALT &&
	      intel_tc_port_needs_reset(tc);

	mutex_unlock(&tc->lock);

	return ret;
}

bool intel_tc_port_link_needs_reset(struct intel_digital_port *dig_port)
{
	if (!intel_encoder_is_tc(&dig_port->base))
		return false;

	return __intel_tc_port_link_needs_reset(to_tc_port(dig_port));
}

static int reset_link_commit(struct intel_tc_port *tc,
			     struct intel_atomic_state *state,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct intel_digital_port *dig_port = tc->dig_port;
	struct intel_dp *intel_dp = enc_to_intel_dp(&dig_port->base);
	struct intel_crtc *crtc;
	u8 pipe_mask;
	int ret;

	ret = drm_modeset_lock(&display->drm->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	ret = intel_dp_get_active_pipes(intel_dp, ctx, &pipe_mask);
	if (ret)
		return ret;

	if (!pipe_mask)
		return 0;

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, pipe_mask) {
		struct intel_crtc_state *crtc_state;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->uapi.connectors_changed = true;
	}

	if (!__intel_tc_port_link_needs_reset(tc))
		return 0;

	return drm_atomic_commit(&state->base);
}

static int reset_link(struct intel_tc_port *tc)
{
	struct intel_display *display = to_intel_display(tc->dig_port);
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *_state;
	struct intel_atomic_state *state;
	int ret;

	_state = drm_atomic_state_alloc(display->drm);
	if (!_state)
		return -ENOMEM;

	state = to_intel_atomic_state(_state);
	state->internal = true;

	intel_modeset_lock_ctx_retry(&ctx, state, 0, ret)
		ret = reset_link_commit(tc, state, &ctx);

	drm_atomic_state_put(&state->base);

	return ret;
}

static void intel_tc_port_link_reset_work(struct work_struct *work)
{
	struct intel_tc_port *tc =
		container_of(work, struct intel_tc_port, link_reset_work.work);
	struct intel_display *display = to_intel_display(tc->dig_port);
	int ret;

	if (!__intel_tc_port_link_needs_reset(tc))
		return;

	mutex_lock(&display->drm->mode_config.mutex);

	drm_dbg_kms(display->drm,
		    "Port %s: TypeC DP-alt sink disconnected, resetting link\n",
		    tc->port_name);
	ret = reset_link(tc);
	drm_WARN_ON(display->drm, ret);

	mutex_unlock(&display->drm->mode_config.mutex);
}

bool intel_tc_port_link_reset(struct intel_digital_port *dig_port)
{
	if (!intel_tc_port_link_needs_reset(dig_port))
		return false;

	queue_delayed_work(system_unbound_wq,
			   &to_tc_port(dig_port)->link_reset_work,
			   msecs_to_jiffies(2000));

	return true;
}

void intel_tc_port_link_cancel_reset_work(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	if (!intel_encoder_is_tc(&dig_port->base))
		return;

	cancel_delayed_work(&tc->link_reset_work);
}

static void __intel_tc_port_lock(struct intel_tc_port *tc,
				 int required_lanes)
{
	struct intel_display *display = to_intel_display(tc->dig_port);

	mutex_lock(&tc->lock);

	cancel_delayed_work(&tc->disconnect_phy_work);

	if (!tc->link_refcount)
		intel_tc_port_update_mode(tc, required_lanes,
					  false);

	drm_WARN_ON(display->drm, tc->mode == TC_PORT_DISCONNECTED);
	drm_WARN_ON(display->drm, tc->mode != TC_PORT_TBT_ALT && !tc_phy_is_owned(tc));
}

void intel_tc_port_lock(struct intel_digital_port *dig_port)
{
	__intel_tc_port_lock(to_tc_port(dig_port), 1);
}

/*
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
static void intel_tc_port_flush_work(struct intel_digital_port *dig_port)
{
	flush_delayed_work(&to_tc_port(dig_port)->disconnect_phy_work);
}

void intel_tc_port_suspend(struct intel_digital_port *dig_port)
{
	struct intel_tc_port *tc = to_tc_port(dig_port);

	cancel_delayed_work_sync(&tc->link_reset_work);
	intel_tc_port_flush_work(dig_port);
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
	 * The firmware will not update the HPD status of other TypeC ports
	 * that are active in DP-alt mode with their sink disconnected, until
	 * this port is disabled and its PHY gets disconnected. Make sure this
	 * happens in a timely manner by disconnecting the PHY synchronously.
	 */
	intel_tc_port_flush_work(dig_port);
}

int intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_tc_port *tc;
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_encoder_to_tc(&dig_port->base);

	if (drm_WARN_ON(display->drm, tc_port == TC_PORT_NONE))
		return -EINVAL;

	tc = kzalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	dig_port->tc = tc;
	tc->dig_port = dig_port;

	if (DISPLAY_VER(display) >= 14)
		tc->phy_ops = &xelpdp_tc_phy_ops;
	else if (DISPLAY_VER(display) >= 13)
		tc->phy_ops = &adlp_tc_phy_ops;
	else if (DISPLAY_VER(display) >= 12)
		tc->phy_ops = &tgl_tc_phy_ops;
	else
		tc->phy_ops = &icl_tc_phy_ops;

	tc->port_name = kasprintf(GFP_KERNEL, "%c/TC#%d", port_name(port),
				  tc_port + 1);
	if (!tc->port_name) {
		kfree(tc);
		return -ENOMEM;
	}

	mutex_init(&tc->lock);
	/* TODO: Combine the two works */
	INIT_DELAYED_WORK(&tc->disconnect_phy_work, intel_tc_port_disconnect_phy_work);
	INIT_DELAYED_WORK(&tc->link_reset_work, intel_tc_port_link_reset_work);
	tc->legacy_port = is_legacy;
	tc->mode = TC_PORT_DISCONNECTED;
	tc->link_refcount = 0;

	tc_phy_init(tc);

	intel_tc_port_init_mode(dig_port);

	return 0;
}

void intel_tc_port_cleanup(struct intel_digital_port *dig_port)
{
	intel_tc_port_suspend(dig_port);

	kfree(dig_port->tc->port_name);
	kfree(dig_port->tc);
	dig_port->tc = NULL;
}
