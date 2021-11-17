// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt Time Management Unit (TMU) support
 *
 * Copyright (C) 2019, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Rajmohan Mani <rajmohan.mani@intel.com>
 */

#include <linux/delay.h>

#include "tb.h"

static const char *tb_switch_tmu_mode_name(const struct tb_switch *sw)
{
	bool root_switch = !tb_route(sw);

	switch (sw->tmu.rate) {
	case TB_SWITCH_TMU_RATE_OFF:
		return "off";

	case TB_SWITCH_TMU_RATE_HIFI:
		/* Root switch does not have upstream directionality */
		if (root_switch)
			return "HiFi";
		if (sw->tmu.unidirectional)
			return "uni-directional, HiFi";
		return "bi-directional, HiFi";

	case TB_SWITCH_TMU_RATE_NORMAL:
		if (root_switch)
			return "normal";
		return "uni-directional, normal";

	default:
		return "unknown";
	}
}

static bool tb_switch_tmu_ucap_supported(struct tb_switch *sw)
{
	int ret;
	u32 val;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_0, 1);
	if (ret)
		return false;

	return !!(val & TMU_RTR_CS_0_UCAP);
}

static int tb_switch_tmu_rate_read(struct tb_switch *sw)
{
	int ret;
	u32 val;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_3, 1);
	if (ret)
		return ret;

	val >>= TMU_RTR_CS_3_TS_PACKET_INTERVAL_SHIFT;
	return val;
}

static int tb_switch_tmu_rate_write(struct tb_switch *sw, int rate)
{
	int ret;
	u32 val;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_3, 1);
	if (ret)
		return ret;

	val &= ~TMU_RTR_CS_3_TS_PACKET_INTERVAL_MASK;
	val |= rate << TMU_RTR_CS_3_TS_PACKET_INTERVAL_SHIFT;

	return tb_sw_write(sw, &val, TB_CFG_SWITCH,
			   sw->tmu.cap + TMU_RTR_CS_3, 1);
}

static int tb_port_tmu_write(struct tb_port *port, u8 offset, u32 mask,
			     u32 value)
{
	u32 data;
	int ret;

	ret = tb_port_read(port, &data, TB_CFG_PORT, port->cap_tmu + offset, 1);
	if (ret)
		return ret;

	data &= ~mask;
	data |= value;

	return tb_port_write(port, &data, TB_CFG_PORT,
			     port->cap_tmu + offset, 1);
}

static int tb_port_tmu_set_unidirectional(struct tb_port *port,
					  bool unidirectional)
{
	u32 val;

	if (!port->sw->tmu.has_ucap)
		return 0;

	val = unidirectional ? TMU_ADP_CS_3_UDM : 0;
	return tb_port_tmu_write(port, TMU_ADP_CS_3, TMU_ADP_CS_3_UDM, val);
}

static inline int tb_port_tmu_unidirectional_disable(struct tb_port *port)
{
	return tb_port_tmu_set_unidirectional(port, false);
}

static bool tb_port_tmu_is_unidirectional(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_3, 1);
	if (ret)
		return false;

	return val & TMU_ADP_CS_3_UDM;
}

static int tb_switch_tmu_set_time_disruption(struct tb_switch *sw, bool set)
{
	int ret;
	u32 val;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_0, 1);
	if (ret)
		return ret;

	if (set)
		val |= TMU_RTR_CS_0_TD;
	else
		val &= ~TMU_RTR_CS_0_TD;

	return tb_sw_write(sw, &val, TB_CFG_SWITCH,
			   sw->tmu.cap + TMU_RTR_CS_0, 1);
}

/**
 * tb_switch_tmu_init() - Initialize switch TMU structures
 * @sw: Switch to initialized
 *
 * This function must be called before other TMU related functions to
 * makes the internal structures are filled in correctly. Does not
 * change any hardware configuration.
 */
int tb_switch_tmu_init(struct tb_switch *sw)
{
	struct tb_port *port;
	int ret;

	if (tb_switch_is_icm(sw))
		return 0;

	ret = tb_switch_find_cap(sw, TB_SWITCH_CAP_TMU);
	if (ret > 0)
		sw->tmu.cap = ret;

	tb_switch_for_each_port(sw, port) {
		int cap;

		cap = tb_port_find_cap(port, TB_PORT_CAP_TIME1);
		if (cap > 0)
			port->cap_tmu = cap;
	}

	ret = tb_switch_tmu_rate_read(sw);
	if (ret < 0)
		return ret;

	sw->tmu.rate = ret;

	sw->tmu.has_ucap = tb_switch_tmu_ucap_supported(sw);
	if (sw->tmu.has_ucap) {
		tb_sw_dbg(sw, "TMU: supports uni-directional mode\n");

		if (tb_route(sw)) {
			struct tb_port *up = tb_upstream_port(sw);

			sw->tmu.unidirectional =
				tb_port_tmu_is_unidirectional(up);
		}
	} else {
		sw->tmu.unidirectional = false;
	}

	tb_sw_dbg(sw, "TMU: current mode: %s\n", tb_switch_tmu_mode_name(sw));
	return 0;
}

/**
 * tb_switch_tmu_post_time() - Update switch local time
 * @sw: Switch whose time to update
 *
 * Updates switch local time using time posting procedure.
 */
int tb_switch_tmu_post_time(struct tb_switch *sw)
{
	unsigned int  post_local_time_offset, post_time_offset;
	struct tb_switch *root_switch = sw->tb->root_switch;
	u64 hi, mid, lo, local_time, post_time;
	int i, ret, retries = 100;
	u32 gm_local_time[3];

	if (!tb_route(sw))
		return 0;

	if (!tb_switch_is_usb4(sw))
		return 0;

	/* Need to be able to read the grand master time */
	if (!root_switch->tmu.cap)
		return 0;

	ret = tb_sw_read(root_switch, gm_local_time, TB_CFG_SWITCH,
			 root_switch->tmu.cap + TMU_RTR_CS_1,
			 ARRAY_SIZE(gm_local_time));
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(gm_local_time); i++)
		tb_sw_dbg(root_switch, "local_time[%d]=0x%08x\n", i,
			  gm_local_time[i]);

	/* Convert to nanoseconds (drop fractional part) */
	hi = gm_local_time[2] & TMU_RTR_CS_3_LOCAL_TIME_NS_MASK;
	mid = gm_local_time[1];
	lo = (gm_local_time[0] & TMU_RTR_CS_1_LOCAL_TIME_NS_MASK) >>
		TMU_RTR_CS_1_LOCAL_TIME_NS_SHIFT;
	local_time = hi << 48 | mid << 16 | lo;

	/* Tell the switch that time sync is disrupted for a while */
	ret = tb_switch_tmu_set_time_disruption(sw, true);
	if (ret)
		return ret;

	post_local_time_offset = sw->tmu.cap + TMU_RTR_CS_22;
	post_time_offset = sw->tmu.cap + TMU_RTR_CS_24;

	/*
	 * Write the Grandmaster time to the Post Local Time registers
	 * of the new switch.
	 */
	ret = tb_sw_write(sw, &local_time, TB_CFG_SWITCH,
			  post_local_time_offset, 2);
	if (ret)
		goto out;

	/*
	 * Have the new switch update its local time (by writing 1 to
	 * the post_time registers) and wait for the completion of the
	 * same (post_time register becomes 0). This means the time has
	 * been converged properly.
	 */
	post_time = 1;

	ret = tb_sw_write(sw, &post_time, TB_CFG_SWITCH, post_time_offset, 2);
	if (ret)
		goto out;

	do {
		usleep_range(5, 10);
		ret = tb_sw_read(sw, &post_time, TB_CFG_SWITCH,
				 post_time_offset, 2);
		if (ret)
			goto out;
	} while (--retries && post_time);

	if (!retries) {
		ret = -ETIMEDOUT;
		goto out;
	}

	tb_sw_dbg(sw, "TMU: updated local time to %#llx\n", local_time);

out:
	tb_switch_tmu_set_time_disruption(sw, false);
	return ret;
}

/**
 * tb_switch_tmu_disable() - Disable TMU of a switch
 * @sw: Switch whose TMU to disable
 *
 * Turns off TMU of @sw if it is enabled. If not enabled does nothing.
 */
int tb_switch_tmu_disable(struct tb_switch *sw)
{
	int ret;

	if (!tb_switch_is_usb4(sw))
		return 0;

	/* Already disabled? */
	if (sw->tmu.rate == TB_SWITCH_TMU_RATE_OFF)
		return 0;

	if (sw->tmu.unidirectional) {
		struct tb_switch *parent = tb_switch_parent(sw);
		struct tb_port *up, *down;

		up = tb_upstream_port(sw);
		down = tb_port_at(tb_route(sw), parent);

		/* The switch may be unplugged so ignore any errors */
		tb_port_tmu_unidirectional_disable(up);
		ret = tb_port_tmu_unidirectional_disable(down);
		if (ret)
			return ret;
	}

	tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_OFF);

	sw->tmu.unidirectional = false;
	sw->tmu.rate = TB_SWITCH_TMU_RATE_OFF;

	tb_sw_dbg(sw, "TMU: disabled\n");
	return 0;
}

/**
 * tb_switch_tmu_enable() - Enable TMU on a switch
 * @sw: Switch whose TMU to enable
 *
 * Enables TMU of a switch to be in bi-directional, HiFi mode. In this mode
 * all tunneling should work.
 */
int tb_switch_tmu_enable(struct tb_switch *sw)
{
	int ret;

	if (!tb_switch_is_usb4(sw))
		return 0;

	if (tb_switch_tmu_is_enabled(sw))
		return 0;

	ret = tb_switch_tmu_set_time_disruption(sw, true);
	if (ret)
		return ret;

	/* Change mode to bi-directional */
	if (tb_route(sw) && sw->tmu.unidirectional) {
		struct tb_switch *parent = tb_switch_parent(sw);
		struct tb_port *up, *down;

		up = tb_upstream_port(sw);
		down = tb_port_at(tb_route(sw), parent);

		ret = tb_port_tmu_unidirectional_disable(down);
		if (ret)
			return ret;

		ret = tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_HIFI);
		if (ret)
			return ret;

		ret = tb_port_tmu_unidirectional_disable(up);
		if (ret)
			return ret;
	} else {
		ret = tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_HIFI);
		if (ret)
			return ret;
	}

	sw->tmu.unidirectional = false;
	sw->tmu.rate = TB_SWITCH_TMU_RATE_HIFI;
	tb_sw_dbg(sw, "TMU: mode set to: %s\n", tb_switch_tmu_mode_name(sw));

	return tb_switch_tmu_set_time_disruption(sw, false);
}
