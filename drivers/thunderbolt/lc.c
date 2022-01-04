// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt link controller support
 *
 * Copyright (C) 2019, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include "tb.h"

/**
 * tb_lc_read_uuid() - Read switch UUID from link controller common register
 * @sw: Switch whose UUID is read
 * @uuid: UUID is placed here
 */
int tb_lc_read_uuid(struct tb_switch *sw, u32 *uuid)
{
	if (!sw->cap_lc)
		return -EINVAL;
	return tb_sw_read(sw, uuid, TB_CFG_SWITCH, sw->cap_lc + TB_LC_FUSE, 4);
}

static int read_lc_desc(struct tb_switch *sw, u32 *desc)
{
	if (!sw->cap_lc)
		return -EINVAL;
	return tb_sw_read(sw, desc, TB_CFG_SWITCH, sw->cap_lc + TB_LC_DESC, 1);
}

static int find_port_lc_cap(struct tb_port *port)
{
	struct tb_switch *sw = port->sw;
	int start, phys, ret, size;
	u32 desc;

	ret = read_lc_desc(sw, &desc);
	if (ret)
		return ret;

	/* Start of port LC registers */
	start = (desc & TB_LC_DESC_SIZE_MASK) >> TB_LC_DESC_SIZE_SHIFT;
	size = (desc & TB_LC_DESC_PORT_SIZE_MASK) >> TB_LC_DESC_PORT_SIZE_SHIFT;
	phys = tb_phy_port_from_link(port->port);

	return sw->cap_lc + start + phys * size;
}

static int tb_lc_set_port_configured(struct tb_port *port, bool configured)
{
	bool upstream = tb_is_upstream_port(port);
	struct tb_switch *sw = port->sw;
	u32 ctrl, lane;
	int cap, ret;

	if (sw->generation < 2)
		return 0;

	cap = find_port_lc_cap(port);
	if (cap < 0)
		return cap;

	ret = tb_sw_read(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
	if (ret)
		return ret;

	/* Resolve correct lane */
	if (port->port % 2)
		lane = TB_LC_SX_CTRL_L1C;
	else
		lane = TB_LC_SX_CTRL_L2C;

	if (configured) {
		ctrl |= lane;
		if (upstream)
			ctrl |= TB_LC_SX_CTRL_UPSTREAM;
	} else {
		ctrl &= ~lane;
		if (upstream)
			ctrl &= ~TB_LC_SX_CTRL_UPSTREAM;
	}

	return tb_sw_write(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
}

/**
 * tb_lc_configure_port() - Let LC know about configured port
 * @port: Port that is set as configured
 *
 * Sets the port configured for power management purposes.
 */
int tb_lc_configure_port(struct tb_port *port)
{
	return tb_lc_set_port_configured(port, true);
}

/**
 * tb_lc_unconfigure_port() - Let LC know about unconfigured port
 * @port: Port that is set as configured
 *
 * Sets the port unconfigured for power management purposes.
 */
void tb_lc_unconfigure_port(struct tb_port *port)
{
	tb_lc_set_port_configured(port, false);
}

static int tb_lc_set_xdomain_configured(struct tb_port *port, bool configure)
{
	struct tb_switch *sw = port->sw;
	u32 ctrl, lane;
	int cap, ret;

	if (sw->generation < 2)
		return 0;

	cap = find_port_lc_cap(port);
	if (cap < 0)
		return cap;

	ret = tb_sw_read(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
	if (ret)
		return ret;

	/* Resolve correct lane */
	if (port->port % 2)
		lane = TB_LC_SX_CTRL_L1D;
	else
		lane = TB_LC_SX_CTRL_L2D;

	if (configure)
		ctrl |= lane;
	else
		ctrl &= ~lane;

	return tb_sw_write(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
}

/**
 * tb_lc_configure_xdomain() - Inform LC that the link is XDomain
 * @port: Switch downstream port connected to another host
 *
 * Sets the lane configured for XDomain accordingly so that the LC knows
 * about this. Returns %0 in success and negative errno in failure.
 */
int tb_lc_configure_xdomain(struct tb_port *port)
{
	return tb_lc_set_xdomain_configured(port, true);
}

/**
 * tb_lc_unconfigure_xdomain() - Unconfigure XDomain from port
 * @port: Switch downstream port that was connected to another host
 *
 * Unsets the lane XDomain configuration.
 */
void tb_lc_unconfigure_xdomain(struct tb_port *port)
{
	tb_lc_set_xdomain_configured(port, false);
}

/**
 * tb_lc_start_lane_initialization() - Start lane initialization
 * @port: Device router lane 0 adapter
 *
 * Starts lane initialization for @port after the router resumed from
 * sleep. Should be called for those downstream lane adapters that were
 * not connected (tb_lc_configure_port() was not called) before sleep.
 *
 * Returns %0 in success and negative errno in case of failure.
 */
int tb_lc_start_lane_initialization(struct tb_port *port)
{
	struct tb_switch *sw = port->sw;
	int ret, cap;
	u32 ctrl;

	if (!tb_route(sw))
		return 0;

	if (sw->generation < 2)
		return 0;

	cap = find_port_lc_cap(port);
	if (cap < 0)
		return cap;

	ret = tb_sw_read(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
	if (ret)
		return ret;

	ctrl |= TB_LC_SX_CTRL_SLI;

	return tb_sw_write(sw, &ctrl, TB_CFG_SWITCH, cap + TB_LC_SX_CTRL, 1);
}

static int tb_lc_set_wake_one(struct tb_switch *sw, unsigned int offset,
			      unsigned int flags)
{
	u32 ctrl;
	int ret;

	/*
	 * Enable wake on PCIe and USB4 (wake coming from another
	 * router).
	 */
	ret = tb_sw_read(sw, &ctrl, TB_CFG_SWITCH,
			 offset + TB_LC_SX_CTRL, 1);
	if (ret)
		return ret;

	ctrl &= ~(TB_LC_SX_CTRL_WOC | TB_LC_SX_CTRL_WOD | TB_LC_SX_CTRL_WODPC |
		  TB_LC_SX_CTRL_WODPD | TB_LC_SX_CTRL_WOP | TB_LC_SX_CTRL_WOU4);

	if (flags & TB_WAKE_ON_CONNECT)
		ctrl |= TB_LC_SX_CTRL_WOC | TB_LC_SX_CTRL_WOD;
	if (flags & TB_WAKE_ON_USB4)
		ctrl |= TB_LC_SX_CTRL_WOU4;
	if (flags & TB_WAKE_ON_PCIE)
		ctrl |= TB_LC_SX_CTRL_WOP;
	if (flags & TB_WAKE_ON_DP)
		ctrl |= TB_LC_SX_CTRL_WODPC | TB_LC_SX_CTRL_WODPD;

	return tb_sw_write(sw, &ctrl, TB_CFG_SWITCH, offset + TB_LC_SX_CTRL, 1);
}

/**
 * tb_lc_set_wake() - Enable/disable wake
 * @sw: Switch whose wakes to configure
 * @flags: Wakeup flags (%0 to disable)
 *
 * For each LC sets wake bits accordingly.
 */
int tb_lc_set_wake(struct tb_switch *sw, unsigned int flags)
{
	int start, size, nlc, ret, i;
	u32 desc;

	if (sw->generation < 2)
		return 0;

	if (!tb_route(sw))
		return 0;

	ret = read_lc_desc(sw, &desc);
	if (ret)
		return ret;

	/* Figure out number of link controllers */
	nlc = desc & TB_LC_DESC_NLC_MASK;
	start = (desc & TB_LC_DESC_SIZE_MASK) >> TB_LC_DESC_SIZE_SHIFT;
	size = (desc & TB_LC_DESC_PORT_SIZE_MASK) >> TB_LC_DESC_PORT_SIZE_SHIFT;

	/* For each link controller set sleep bit */
	for (i = 0; i < nlc; i++) {
		unsigned int offset = sw->cap_lc + start + i * size;

		ret = tb_lc_set_wake_one(sw, offset, flags);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tb_lc_set_sleep() - Inform LC that the switch is going to sleep
 * @sw: Switch to set sleep
 *
 * Let the switch link controllers know that the switch is going to
 * sleep.
 */
int tb_lc_set_sleep(struct tb_switch *sw)
{
	int start, size, nlc, ret, i;
	u32 desc;

	if (sw->generation < 2)
		return 0;

	ret = read_lc_desc(sw, &desc);
	if (ret)
		return ret;

	/* Figure out number of link controllers */
	nlc = desc & TB_LC_DESC_NLC_MASK;
	start = (desc & TB_LC_DESC_SIZE_MASK) >> TB_LC_DESC_SIZE_SHIFT;
	size = (desc & TB_LC_DESC_PORT_SIZE_MASK) >> TB_LC_DESC_PORT_SIZE_SHIFT;

	/* For each link controller set sleep bit */
	for (i = 0; i < nlc; i++) {
		unsigned int offset = sw->cap_lc + start + i * size;
		u32 ctrl;

		ret = tb_sw_read(sw, &ctrl, TB_CFG_SWITCH,
				 offset + TB_LC_SX_CTRL, 1);
		if (ret)
			return ret;

		ctrl |= TB_LC_SX_CTRL_SLP;
		ret = tb_sw_write(sw, &ctrl, TB_CFG_SWITCH,
				  offset + TB_LC_SX_CTRL, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * tb_lc_lane_bonding_possible() - Is lane bonding possible towards switch
 * @sw: Switch to check
 *
 * Checks whether conditions for lane bonding from parent to @sw are
 * possible.
 */
bool tb_lc_lane_bonding_possible(struct tb_switch *sw)
{
	struct tb_port *up;
	int cap, ret;
	u32 val;

	if (sw->generation < 2)
		return false;

	up = tb_upstream_port(sw);
	cap = find_port_lc_cap(up);
	if (cap < 0)
		return false;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, cap + TB_LC_PORT_ATTR, 1);
	if (ret)
		return false;

	return !!(val & TB_LC_PORT_ATTR_BE);
}

static int tb_lc_dp_sink_from_port(const struct tb_switch *sw,
				   struct tb_port *in)
{
	struct tb_port *port;

	/* The first DP IN port is sink 0 and second is sink 1 */
	tb_switch_for_each_port(sw, port) {
		if (tb_port_is_dpin(port))
			return in != port;
	}

	return -EINVAL;
}

static int tb_lc_dp_sink_available(struct tb_switch *sw, int sink)
{
	u32 val, alloc;
	int ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_lc + TB_LC_SNK_ALLOCATION, 1);
	if (ret)
		return ret;

	/*
	 * Sink is available for CM/SW to use if the allocation valie is
	 * either 0 or 1.
	 */
	if (!sink) {
		alloc = val & TB_LC_SNK_ALLOCATION_SNK0_MASK;
		if (!alloc || alloc == TB_LC_SNK_ALLOCATION_SNK0_CM)
			return 0;
	} else {
		alloc = (val & TB_LC_SNK_ALLOCATION_SNK1_MASK) >>
			TB_LC_SNK_ALLOCATION_SNK1_SHIFT;
		if (!alloc || alloc == TB_LC_SNK_ALLOCATION_SNK1_CM)
			return 0;
	}

	return -EBUSY;
}

/**
 * tb_lc_dp_sink_query() - Is DP sink available for DP IN port
 * @sw: Switch whose DP sink is queried
 * @in: DP IN port to check
 *
 * Queries through LC SNK_ALLOCATION registers whether DP sink is available
 * for the given DP IN port or not.
 */
bool tb_lc_dp_sink_query(struct tb_switch *sw, struct tb_port *in)
{
	int sink;

	/*
	 * For older generations sink is always available as there is no
	 * allocation mechanism.
	 */
	if (sw->generation < 3)
		return true;

	sink = tb_lc_dp_sink_from_port(sw, in);
	if (sink < 0)
		return false;

	return !tb_lc_dp_sink_available(sw, sink);
}

/**
 * tb_lc_dp_sink_alloc() - Allocate DP sink
 * @sw: Switch whose DP sink is allocated
 * @in: DP IN port the DP sink is allocated for
 *
 * Allocate DP sink for @in via LC SNK_ALLOCATION registers. If the
 * resource is available and allocation is successful returns %0. In all
 * other cases returs negative errno. In particular %-EBUSY is returned if
 * the resource was not available.
 */
int tb_lc_dp_sink_alloc(struct tb_switch *sw, struct tb_port *in)
{
	int ret, sink;
	u32 val;

	if (sw->generation < 3)
		return 0;

	sink = tb_lc_dp_sink_from_port(sw, in);
	if (sink < 0)
		return sink;

	ret = tb_lc_dp_sink_available(sw, sink);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_lc + TB_LC_SNK_ALLOCATION, 1);
	if (ret)
		return ret;

	if (!sink) {
		val &= ~TB_LC_SNK_ALLOCATION_SNK0_MASK;
		val |= TB_LC_SNK_ALLOCATION_SNK0_CM;
	} else {
		val &= ~TB_LC_SNK_ALLOCATION_SNK1_MASK;
		val |= TB_LC_SNK_ALLOCATION_SNK1_CM <<
			TB_LC_SNK_ALLOCATION_SNK1_SHIFT;
	}

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
			  sw->cap_lc + TB_LC_SNK_ALLOCATION, 1);

	if (ret)
		return ret;

	tb_port_dbg(in, "sink %d allocated\n", sink);
	return 0;
}

/**
 * tb_lc_dp_sink_dealloc() - De-allocate DP sink
 * @sw: Switch whose DP sink is de-allocated
 * @in: DP IN port whose DP sink is de-allocated
 *
 * De-allocate DP sink from @in using LC SNK_ALLOCATION registers.
 */
int tb_lc_dp_sink_dealloc(struct tb_switch *sw, struct tb_port *in)
{
	int ret, sink;
	u32 val;

	if (sw->generation < 3)
		return 0;

	sink = tb_lc_dp_sink_from_port(sw, in);
	if (sink < 0)
		return sink;

	/* Needs to be owned by CM/SW */
	ret = tb_lc_dp_sink_available(sw, sink);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_lc + TB_LC_SNK_ALLOCATION, 1);
	if (ret)
		return ret;

	if (!sink)
		val &= ~TB_LC_SNK_ALLOCATION_SNK0_MASK;
	else
		val &= ~TB_LC_SNK_ALLOCATION_SNK1_MASK;

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
			  sw->cap_lc + TB_LC_SNK_ALLOCATION, 1);
	if (ret)
		return ret;

	tb_port_dbg(in, "sink %d de-allocated\n", sink);
	return 0;
}

/**
 * tb_lc_force_power() - Forces LC to be powered on
 * @sw: Thunderbolt switch
 *
 * This is useful to let authentication cycle pass even without
 * a Thunderbolt link present.
 */
int tb_lc_force_power(struct tb_switch *sw)
{
	u32 in = 0xffff;

	return tb_sw_write(sw, &in, TB_CFG_SWITCH, TB_LC_POWER, 1);
}
