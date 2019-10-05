// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_display.h"
#include "i915_drv.h"
#include "intel_tc.h"

static const char *tc_port_mode_name(enum tc_port_mode mode)
{
	static const char * const names[] = {
		[TC_PORT_TBT_ALT] = "tbt-alt",
		[TC_PORT_DP_ALT] = "dp-alt",
		[TC_PORT_LEGACY] = "legacy",
	};

	if (WARN_ON(mode >= ARRAY_SIZE(names)))
		mode = TC_PORT_TBT_ALT;

	return names[mode];
}

u32 intel_tc_port_get_lane_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 lane_mask;

	lane_mask = I915_READ(PORT_TX_DFLEXDPSP);

	WARN_ON(lane_mask == 0xffffffff);

	return (lane_mask & DP_LANE_ASSIGNMENT_MASK(tc_port)) >>
	       DP_LANE_ASSIGNMENT_SHIFT(tc_port);
}

int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	intel_wakeref_t wakeref;
	u32 lane_mask;

	if (dig_port->tc_mode != TC_PORT_DP_ALT)
		return 4;

	lane_mask = 0;
	with_intel_display_power(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		lane_mask = intel_tc_port_get_lane_mask(dig_port);

	switch (lane_mask) {
	default:
		MISSING_CASE(lane_mask);
	case 1:
	case 2:
	case 4:
	case 8:
		return 1;
	case 3:
	case 12:
		return 2;
	case 15:
		return 4;
	}
}

static void tc_port_fixup_legacy_flag(struct intel_digital_port *dig_port,
				      u32 live_status_mask)
{
	u32 valid_hpd_mask;

	if (dig_port->tc_legacy_port)
		valid_hpd_mask = BIT(TC_PORT_LEGACY);
	else
		valid_hpd_mask = BIT(TC_PORT_DP_ALT) |
				 BIT(TC_PORT_TBT_ALT);

	if (!(live_status_mask & ~valid_hpd_mask))
		return;

	/* If live status mismatches the VBT flag, trust the live status. */
	DRM_ERROR("Port %s: live status %08x mismatches the legacy port flag, fix flag\n",
		  dig_port->tc_port_name, live_status_mask);

	dig_port->tc_legacy_port = !dig_port->tc_legacy_port;
}

static u32 tc_port_live_status_mask(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 mask = 0;
	u32 val;

	val = I915_READ(PORT_TX_DFLEXDPSP);

	if (val == 0xffffffff) {
		DRM_DEBUG_KMS("Port %s: PHY in TCCOLD, nothing connected\n",
			      dig_port->tc_port_name);
		return mask;
	}

	if (val & TC_LIVE_STATE_TBT(tc_port))
		mask |= BIT(TC_PORT_TBT_ALT);
	if (val & TC_LIVE_STATE_TC(tc_port))
		mask |= BIT(TC_PORT_DP_ALT);

	if (I915_READ(SDEISR) & SDE_TC_HOTPLUG_ICP(tc_port))
		mask |= BIT(TC_PORT_LEGACY);

	/* The sink can be connected only in a single mode. */
	if (!WARN_ON(hweight32(mask) > 1))
		tc_port_fixup_legacy_flag(dig_port, mask);

	return mask;
}

static bool icl_tc_phy_status_complete(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 val;

	val = I915_READ(PORT_TX_DFLEXDPPMS);
	if (val == 0xffffffff) {
		DRM_DEBUG_KMS("Port %s: PHY in TCCOLD, assuming not complete\n",
			      dig_port->tc_port_name);
		return false;
	}

	return val & DP_PHY_MODE_STATUS_COMPLETED(tc_port);
}

static bool icl_tc_phy_set_safe_mode(struct intel_digital_port *dig_port,
				     bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 val;

	val = I915_READ(PORT_TX_DFLEXDPCSSS);
	if (val == 0xffffffff) {
		DRM_DEBUG_KMS("Port %s: PHY in TCCOLD, can't set safe-mode to %s\n",
			      dig_port->tc_port_name,
			      enableddisabled(enable));

		return false;
	}

	val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);
	if (!enable)
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);

	I915_WRITE(PORT_TX_DFLEXDPCSSS, val);

	if (enable && wait_for(!icl_tc_phy_status_complete(dig_port), 10))
		DRM_DEBUG_KMS("Port %s: PHY complete clear timed out\n",
			      dig_port->tc_port_name);

	return true;
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
 *
 * We could opt to only do the connect flow when we actually try to use the AUX
 * channels or do a modeset, then immediately run the disconnect flow after
 * usage, but there are some implications on this for a dynamic environment:
 * things may go away or change behind our backs. So for now our driver is
 * always trying to acquire ownership of the controller as soon as it gets an
 * interrupt (or polls state and sees a port is connected) and only gives it
 * back when it sees a disconnect. Implementation of a more fine-grained model
 * will require a lot of coordination with user space and thorough testing for
 * the extra possible cases.
 */
static bool icl_tc_phy_connect(struct intel_digital_port *dig_port)
{
	u32 live_status_mask;

	if (dig_port->tc_mode != TC_PORT_LEGACY &&
	    dig_port->tc_mode != TC_PORT_DP_ALT)
		return true;

	if (!icl_tc_phy_status_complete(dig_port)) {
		DRM_DEBUG_KMS("Port %s: PHY not ready\n",
			      dig_port->tc_port_name);
		WARN_ON(dig_port->tc_legacy_port);
		return false;
	}

	if (!icl_tc_phy_set_safe_mode(dig_port, false))
		return false;

	if (dig_port->tc_mode == TC_PORT_LEGACY)
		return true;

	live_status_mask = tc_port_live_status_mask(dig_port);

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (!(live_status_mask & BIT(TC_PORT_DP_ALT))) {
		DRM_DEBUG_KMS("Port %s: PHY sudden disconnect\n",
			      dig_port->tc_port_name);
		icl_tc_phy_disconnect(dig_port);
		return false;
	}

	return true;
}

/*
 * See the comment at the connect function. This implements the Disconnect
 * Flow.
 */
void icl_tc_phy_disconnect(struct intel_digital_port *dig_port)
{
	switch (dig_port->tc_mode) {
	case TC_PORT_LEGACY:
	case TC_PORT_DP_ALT:
		icl_tc_phy_set_safe_mode(dig_port, true);
		dig_port->tc_mode = TC_PORT_TBT_ALT;
		break;
	case TC_PORT_TBT_ALT:
		/* Nothing to do, we stay in TBT-alt mode */
		break;
	default:
		MISSING_CASE(dig_port->tc_mode);
	}

	DRM_DEBUG_KMS("Port %s: mode %s disconnected\n",
		      dig_port->tc_port_name,
		      tc_port_mode_name(dig_port->tc_mode));
}

static void icl_update_tc_port_type(struct drm_i915_private *dev_priv,
				    struct intel_digital_port *intel_dig_port,
				    u32 live_status_mask)
{
	enum tc_port_mode old_mode = intel_dig_port->tc_mode;

	if (!live_status_mask)
		return;

	intel_dig_port->tc_mode = fls(live_status_mask) - 1;

	if (old_mode != intel_dig_port->tc_mode)
		DRM_DEBUG_KMS("Port %s: port has mode %s\n",
			      intel_dig_port->tc_port_name,
			      tc_port_mode_name(intel_dig_port->tc_mode));
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
bool intel_tc_port_connected(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	u32 live_status_mask = tc_port_live_status_mask(dig_port);

	/*
	 * The spec says we shouldn't be using the ISR bits for detecting
	 * between TC and TBT. We should use DFLEXDPSP.
	 */
	if (!live_status_mask && !dig_port->tc_legacy_port) {
		icl_tc_phy_disconnect(dig_port);

		return false;
	}

	icl_update_tc_port_type(dev_priv, dig_port, live_status_mask);
	if (!icl_tc_phy_connect(dig_port))
		return false;

	return true;
}

void intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(i915, port);

	if (WARN_ON(tc_port == PORT_TC_NONE))
		return;

	snprintf(dig_port->tc_port_name, sizeof(dig_port->tc_port_name),
		 "%c/TC#%d", port_name(port), tc_port + 1);

	dig_port->tc_legacy_port = is_legacy;
}
