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

int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	intel_wakeref_t wakeref;
	u32 lane_info;

	if (dig_port->tc_mode != TC_PORT_DP_ALT)
		return 4;

	lane_info = 0;
	with_intel_display_power(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		lane_info = (I915_READ(PORT_TX_DFLEXDPSP) &
			     DP_LANE_ASSIGNMENT_MASK(tc_port)) >>
				DP_LANE_ASSIGNMENT_SHIFT(tc_port);

	switch (lane_info) {
	default:
		MISSING_CASE(lane_info);
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
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 val;

	if (dig_port->tc_mode != TC_PORT_LEGACY &&
	    dig_port->tc_mode != TC_PORT_DP_ALT)
		return true;

	val = I915_READ(PORT_TX_DFLEXDPPMS);
	if (!(val & DP_PHY_MODE_STATUS_COMPLETED(tc_port))) {
		DRM_DEBUG_KMS("DP PHY for TC port %d not ready\n", tc_port);
		WARN_ON(dig_port->tc_legacy_port);
		return false;
	}

	/*
	 * This function may be called many times in a row without an HPD event
	 * in between, so try to avoid the write when we can.
	 */
	val = I915_READ(PORT_TX_DFLEXDPCSSS);
	if (!(val & DP_PHY_MODE_STATUS_NOT_SAFE(tc_port))) {
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);
		I915_WRITE(PORT_TX_DFLEXDPCSSS, val);
	}

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (dig_port->tc_mode == TC_PORT_DP_ALT &&
	    !(I915_READ(PORT_TX_DFLEXDPSP) & TC_LIVE_STATE_TC(tc_port))) {
		DRM_DEBUG_KMS("TC PHY %d sudden disconnect.\n", tc_port);
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
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);

	/*
	 * TBT disconnection flow is read the live status, what was done in
	 * caller.
	 */
	if (dig_port->tc_mode == TC_PORT_DP_ALT ||
	    dig_port->tc_mode == TC_PORT_LEGACY) {
		u32 val;

		val = I915_READ(PORT_TX_DFLEXDPCSSS);
		val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);
		I915_WRITE(PORT_TX_DFLEXDPCSSS, val);
	}

	DRM_DEBUG_KMS("Port %c TC type %s disconnected\n",
		      port_name(dig_port->base.port),
		      tc_port_mode_name(dig_port->tc_mode));

	dig_port->tc_mode = TC_PORT_TBT_ALT;
}

static void icl_update_tc_port_type(struct drm_i915_private *dev_priv,
				    struct intel_digital_port *intel_dig_port,
				    bool is_legacy, bool is_typec, bool is_tbt)
{
	enum port port = intel_dig_port->base.port;
	enum tc_port_mode old_mode = intel_dig_port->tc_mode;

	WARN_ON(is_legacy + is_typec + is_tbt != 1);

	if (is_legacy)
		intel_dig_port->tc_mode = TC_PORT_LEGACY;
	else if (is_typec)
		intel_dig_port->tc_mode = TC_PORT_DP_ALT;
	else if (is_tbt)
		intel_dig_port->tc_mode = TC_PORT_TBT_ALT;
	else
		return;

	if (old_mode != intel_dig_port->tc_mode)
		DRM_DEBUG_KMS("Port %c has TC type %s\n", port_name(port),
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
	enum port port = dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(dev_priv, port);
	bool is_legacy, is_typec, is_tbt;
	u32 dpsp;

	/*
	 * Complain if we got a legacy port HPD, but VBT didn't mark the port as
	 * legacy. Treat the port as legacy from now on.
	 */
	if (!dig_port->tc_legacy_port &&
	    I915_READ(SDEISR) & SDE_TC_HOTPLUG_ICP(tc_port)) {
		DRM_ERROR("VBT incorrectly claims port %c is not TypeC legacy\n",
			  port_name(port));
		dig_port->tc_legacy_port = true;
	}
	is_legacy = dig_port->tc_legacy_port;

	/*
	 * The spec says we shouldn't be using the ISR bits for detecting
	 * between TC and TBT. We should use DFLEXDPSP.
	 */
	dpsp = I915_READ(PORT_TX_DFLEXDPSP);
	is_typec = dpsp & TC_LIVE_STATE_TC(tc_port);
	is_tbt = dpsp & TC_LIVE_STATE_TBT(tc_port);

	if (!is_legacy && !is_typec && !is_tbt) {
		icl_tc_phy_disconnect(dig_port);

		return false;
	}

	icl_update_tc_port_type(dev_priv, dig_port, is_legacy, is_typec,
				is_tbt);

	if (!icl_tc_phy_connect(dig_port))
		return false;

	return true;
}

