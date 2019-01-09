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

static int tb_lc_configure_lane(struct tb_port *port, bool configure)
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

	if (configure) {
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
 * tb_lc_configure_link() - Let LC know about configured link
 * @sw: Switch that is being added
 *
 * Informs LC of both parent switch and @sw that there is established
 * link between the two.
 */
int tb_lc_configure_link(struct tb_switch *sw)
{
	struct tb_port *up, *down;
	int ret;

	if (!sw->config.enabled || !tb_route(sw))
		return 0;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), tb_to_switch(sw->dev.parent));

	/* Configure parent link toward this switch */
	ret = tb_lc_configure_lane(down, true);
	if (ret)
		return ret;

	/* Configure upstream link from this switch to the parent */
	ret = tb_lc_configure_lane(up, true);
	if (ret)
		tb_lc_configure_lane(down, false);

	return ret;
}

/**
 * tb_lc_unconfigure_link() - Let LC know about unconfigured link
 * @sw: Switch to unconfigure
 *
 * Informs LC of both parent switch and @sw that the link between the
 * two does not exist anymore.
 */
void tb_lc_unconfigure_link(struct tb_switch *sw)
{
	struct tb_port *up, *down;

	if (sw->is_unplugged || !sw->config.enabled || !tb_route(sw))
		return;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), tb_to_switch(sw->dev.parent));

	tb_lc_configure_lane(up, false);
	tb_lc_configure_lane(down, false);
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
