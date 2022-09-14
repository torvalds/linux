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

static int tb_switch_set_tmu_mode_params(struct tb_switch *sw,
					 enum tb_switch_tmu_rate rate)
{
	u32 freq_meas_wind[2] = { 30, 800 };
	u32 avg_const[2] = { 4, 8 };
	u32 freq, avg, val;
	int ret;

	if (rate == TB_SWITCH_TMU_RATE_NORMAL) {
		freq = freq_meas_wind[0];
		avg = avg_const[0];
	} else if (rate == TB_SWITCH_TMU_RATE_HIFI) {
		freq = freq_meas_wind[1];
		avg = avg_const[1];
	} else {
		return 0;
	}

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_0, 1);
	if (ret)
		return ret;

	val &= ~TMU_RTR_CS_0_FREQ_WIND_MASK;
	val |= FIELD_PREP(TMU_RTR_CS_0_FREQ_WIND_MASK, freq);

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
			  sw->tmu.cap + TMU_RTR_CS_0, 1);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_15, 1);
	if (ret)
		return ret;

	val &= ~TMU_RTR_CS_15_FREQ_AVG_MASK &
		~TMU_RTR_CS_15_DELAY_AVG_MASK &
		~TMU_RTR_CS_15_OFFSET_AVG_MASK &
		~TMU_RTR_CS_15_ERROR_AVG_MASK;
	val |=  FIELD_PREP(TMU_RTR_CS_15_FREQ_AVG_MASK, avg) |
		FIELD_PREP(TMU_RTR_CS_15_DELAY_AVG_MASK, avg) |
		FIELD_PREP(TMU_RTR_CS_15_OFFSET_AVG_MASK, avg) |
		FIELD_PREP(TMU_RTR_CS_15_ERROR_AVG_MASK, avg);

	return tb_sw_write(sw, &val, TB_CFG_SWITCH,
			   sw->tmu.cap + TMU_RTR_CS_15, 1);
}

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

static inline int tb_port_tmu_unidirectional_enable(struct tb_port *port)
{
	return tb_port_tmu_set_unidirectional(port, true);
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

static int tb_port_tmu_time_sync(struct tb_port *port, bool time_sync)
{
	u32 val = time_sync ? TMU_ADP_CS_6_DTS : 0;

	return tb_port_tmu_write(port, TMU_ADP_CS_6, TMU_ADP_CS_6_DTS, val);
}

static int tb_port_tmu_time_sync_disable(struct tb_port *port)
{
	return tb_port_tmu_time_sync(port, true);
}

static int tb_port_tmu_time_sync_enable(struct tb_port *port)
{
	return tb_port_tmu_time_sync(port, false);
}

static int tb_switch_tmu_set_time_disruption(struct tb_switch *sw, bool set)
{
	u32 val, offset, bit;
	int ret;

	if (tb_switch_is_usb4(sw)) {
		offset = sw->tmu.cap + TMU_RTR_CS_0;
		bit = TMU_RTR_CS_0_TD;
	} else {
		offset = sw->cap_vsec_tmu + TB_TIME_VSEC_3_CS_26;
		bit = TB_TIME_VSEC_3_CS_26_TD;
	}

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, offset, 1);
	if (ret)
		return ret;

	if (set)
		val |= bit;
	else
		val &= ~bit;

	return tb_sw_write(sw, &val, TB_CFG_SWITCH, offset, 1);
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
	unsigned int post_time_high_offset, post_time_high = 0;
	unsigned int post_local_time_offset, post_time_offset;
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
	post_time_high_offset = sw->tmu.cap + TMU_RTR_CS_25;

	/*
	 * Write the Grandmaster time to the Post Local Time registers
	 * of the new switch.
	 */
	ret = tb_sw_write(sw, &local_time, TB_CFG_SWITCH,
			  post_local_time_offset, 2);
	if (ret)
		goto out;

	/*
	 * Have the new switch update its local time by:
	 * 1) writing 0x1 to the Post Time Low register and 0xffffffff to
	 * Post Time High register.
	 * 2) write 0 to Post Time High register and then wait for
	 * the completion of the post_time register becomes 0.
	 * This means the time has been converged properly.
	 */
	post_time = 0xffffffff00000001ULL;

	ret = tb_sw_write(sw, &post_time, TB_CFG_SWITCH, post_time_offset, 2);
	if (ret)
		goto out;

	ret = tb_sw_write(sw, &post_time_high, TB_CFG_SWITCH,
			  post_time_high_offset, 1);
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
	/*
	 * No need to disable TMU on devices that don't support CLx since
	 * on these devices e.g. Alpine Ridge and earlier, the TMU mode
	 * HiFi bi-directional is enabled by default and we don't change it.
	 */
	if (!tb_switch_is_clx_supported(sw))
		return 0;

	/* Already disabled? */
	if (sw->tmu.rate == TB_SWITCH_TMU_RATE_OFF)
		return 0;


	if (tb_route(sw)) {
		bool unidirectional = sw->tmu.unidirectional;
		struct tb_switch *parent = tb_switch_parent(sw);
		struct tb_port *down, *up;
		int ret;

		down = tb_port_at(tb_route(sw), parent);
		up = tb_upstream_port(sw);
		/*
		 * In case of uni-directional time sync, TMU handshake is
		 * initiated by upstream router. In case of bi-directional
		 * time sync, TMU handshake is initiated by downstream router.
		 * We change downstream router's rate to off for both uni/bidir
		 * cases although it is needed only for the bi-directional mode.
		 * We avoid changing upstream router's mode since it might
		 * have another downstream router plugged, that is set to
		 * uni-directional mode and we don't want to change it's TMU
		 * mode.
		 */
		tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_OFF);

		tb_port_tmu_time_sync_disable(up);
		ret = tb_port_tmu_time_sync_disable(down);
		if (ret)
			return ret;

		if (unidirectional) {
			/* The switch may be unplugged so ignore any errors */
			tb_port_tmu_unidirectional_disable(up);
			ret = tb_port_tmu_unidirectional_disable(down);
			if (ret)
				return ret;
		}
	} else {
		tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_OFF);
	}

	sw->tmu.unidirectional = false;
	sw->tmu.rate = TB_SWITCH_TMU_RATE_OFF;

	tb_sw_dbg(sw, "TMU: disabled\n");
	return 0;
}

static void __tb_switch_tmu_off(struct tb_switch *sw, bool unidirectional)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *down, *up;

	down = tb_port_at(tb_route(sw), parent);
	up = tb_upstream_port(sw);
	/*
	 * In case of any failure in one of the steps when setting
	 * bi-directional or uni-directional TMU mode, get back to the TMU
	 * configurations in off mode. In case of additional failures in
	 * the functions below, ignore them since the caller shall already
	 * report a failure.
	 */
	tb_port_tmu_time_sync_disable(down);
	tb_port_tmu_time_sync_disable(up);
	if (unidirectional)
		tb_switch_tmu_rate_write(parent, TB_SWITCH_TMU_RATE_OFF);
	else
		tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_OFF);

	tb_switch_set_tmu_mode_params(sw, sw->tmu.rate);
	tb_port_tmu_unidirectional_disable(down);
	tb_port_tmu_unidirectional_disable(up);
}

/*
 * This function is called when the previous TMU mode was
 * TB_SWITCH_TMU_RATE_OFF.
 */
static int __tb_switch_tmu_enable_bidirectional(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);

	ret = tb_port_tmu_unidirectional_disable(up);
	if (ret)
		return ret;

	ret = tb_port_tmu_unidirectional_disable(down);
	if (ret)
		goto out;

	ret = tb_switch_tmu_rate_write(sw, TB_SWITCH_TMU_RATE_HIFI);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(up);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(down);
	if (ret)
		goto out;

	return 0;

out:
	__tb_switch_tmu_off(sw, false);
	return ret;
}

static int tb_switch_tmu_objection_mask(struct tb_switch *sw)
{
	u32 val;
	int ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_vsec_tmu + TB_TIME_VSEC_3_CS_9, 1);
	if (ret)
		return ret;

	val &= ~TB_TIME_VSEC_3_CS_9_TMU_OBJ_MASK;

	return tb_sw_write(sw, &val, TB_CFG_SWITCH,
			   sw->cap_vsec_tmu + TB_TIME_VSEC_3_CS_9, 1);
}

static int tb_switch_tmu_unidirectional_enable(struct tb_switch *sw)
{
	struct tb_port *up = tb_upstream_port(sw);

	return tb_port_tmu_write(up, TMU_ADP_CS_6,
				 TMU_ADP_CS_6_DISABLE_TMU_OBJ_MASK,
				 TMU_ADP_CS_6_DISABLE_TMU_OBJ_MASK);
}

/*
 * This function is called when the previous TMU mode was
 * TB_SWITCH_TMU_RATE_OFF.
 */
static int __tb_switch_tmu_enable_unidirectional(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);
	ret = tb_switch_tmu_rate_write(parent, sw->tmu.rate_request);
	if (ret)
		return ret;

	ret = tb_switch_set_tmu_mode_params(sw, sw->tmu.rate_request);
	if (ret)
		return ret;

	ret = tb_port_tmu_unidirectional_enable(up);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(up);
	if (ret)
		goto out;

	ret = tb_port_tmu_unidirectional_enable(down);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(down);
	if (ret)
		goto out;

	return 0;

out:
	__tb_switch_tmu_off(sw, true);
	return ret;
}

static void __tb_switch_tmu_change_mode_prev(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *down, *up;

	down = tb_port_at(tb_route(sw), parent);
	up = tb_upstream_port(sw);
	/*
	 * In case of any failure in one of the steps when change mode,
	 * get back to the TMU configurations in previous mode.
	 * In case of additional failures in the functions below,
	 * ignore them since the caller shall already report a failure.
	 */
	tb_port_tmu_set_unidirectional(down, sw->tmu.unidirectional);
	if (sw->tmu.unidirectional_request)
		tb_switch_tmu_rate_write(parent, sw->tmu.rate);
	else
		tb_switch_tmu_rate_write(sw, sw->tmu.rate);

	tb_switch_set_tmu_mode_params(sw, sw->tmu.rate);
	tb_port_tmu_set_unidirectional(up, sw->tmu.unidirectional);
}

static int __tb_switch_tmu_change_mode(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);
	ret = tb_port_tmu_set_unidirectional(down, sw->tmu.unidirectional_request);
	if (ret)
		goto out;

	if (sw->tmu.unidirectional_request)
		ret = tb_switch_tmu_rate_write(parent, sw->tmu.rate_request);
	else
		ret = tb_switch_tmu_rate_write(sw, sw->tmu.rate_request);
	if (ret)
		return ret;

	ret = tb_switch_set_tmu_mode_params(sw, sw->tmu.rate_request);
	if (ret)
		return ret;

	ret = tb_port_tmu_set_unidirectional(up, sw->tmu.unidirectional_request);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(down);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(up);
	if (ret)
		goto out;

	return 0;

out:
	__tb_switch_tmu_change_mode_prev(sw);
	return ret;
}

/**
 * tb_switch_tmu_enable() - Enable TMU on a router
 * @sw: Router whose TMU to enable
 *
 * Enables TMU of a router to be in uni-directional Normal/HiFi
 * or bi-directional HiFi mode. Calling tb_switch_tmu_configure() is required
 * before calling this function, to select the mode Normal/HiFi and
 * directionality (uni-directional/bi-directional).
 * In HiFi mode all tunneling should work. In Normal mode, DP tunneling can't
 * work. Uni-directional mode is required for CLx (Link Low-Power) to work.
 */
int tb_switch_tmu_enable(struct tb_switch *sw)
{
	bool unidirectional = sw->tmu.unidirectional_request;
	int ret;

	if (unidirectional && !sw->tmu.has_ucap)
		return -EOPNOTSUPP;

	/*
	 * No need to enable TMU on devices that don't support CLx since on
	 * these devices e.g. Alpine Ridge and earlier, the TMU mode HiFi
	 * bi-directional is enabled by default.
	 */
	if (!tb_switch_is_clx_supported(sw))
		return 0;

	if (tb_switch_tmu_is_enabled(sw, sw->tmu.unidirectional_request))
		return 0;

	if (tb_switch_is_titan_ridge(sw) && unidirectional) {
		/*
		 * Titan Ridge supports CL0s and CL1 only. CL0s and CL1 are
		 * enabled and supported together.
		 */
		if (!tb_switch_is_clx_enabled(sw, TB_CL1))
			return -EOPNOTSUPP;

		ret = tb_switch_tmu_objection_mask(sw);
		if (ret)
			return ret;

		ret = tb_switch_tmu_unidirectional_enable(sw);
		if (ret)
			return ret;
	}

	ret = tb_switch_tmu_set_time_disruption(sw, true);
	if (ret)
		return ret;

	if (tb_route(sw)) {
		/*
		 * The used mode changes are from OFF to
		 * HiFi-Uni/HiFi-BiDir/Normal-Uni or from Normal-Uni to
		 * HiFi-Uni.
		 */
		if (sw->tmu.rate == TB_SWITCH_TMU_RATE_OFF) {
			if (unidirectional)
				ret = __tb_switch_tmu_enable_unidirectional(sw);
			else
				ret = __tb_switch_tmu_enable_bidirectional(sw);
			if (ret)
				return ret;
		} else if (sw->tmu.rate == TB_SWITCH_TMU_RATE_NORMAL) {
			ret = __tb_switch_tmu_change_mode(sw);
			if (ret)
				return ret;
		}
		sw->tmu.unidirectional = unidirectional;
	} else {
		/*
		 * Host router port configurations are written as
		 * part of configurations for downstream port of the parent
		 * of the child node - see above.
		 * Here only the host router' rate configuration is written.
		 */
		ret = tb_switch_tmu_rate_write(sw, sw->tmu.rate_request);
		if (ret)
			return ret;
	}

	sw->tmu.rate = sw->tmu.rate_request;

	tb_sw_dbg(sw, "TMU: mode set to: %s\n", tb_switch_tmu_mode_name(sw));
	return tb_switch_tmu_set_time_disruption(sw, false);
}

/**
 * tb_switch_tmu_configure() - Configure the TMU rate and directionality
 * @sw: Router whose mode to change
 * @rate: Rate to configure Off/Normal/HiFi
 * @unidirectional: If uni-directional (bi-directional otherwise)
 *
 * Selects the rate of the TMU and directionality (uni-directional or
 * bi-directional). Must be called before tb_switch_tmu_enable().
 */
void tb_switch_tmu_configure(struct tb_switch *sw,
			     enum tb_switch_tmu_rate rate, bool unidirectional)
{
	sw->tmu.unidirectional_request = unidirectional;
	sw->tmu.rate_request = rate;
}

static int tb_switch_tmu_config_enable(struct device *dev, void *rate)
{
	if (tb_is_switch(dev)) {
		struct tb_switch *sw = tb_to_switch(dev);

		tb_switch_tmu_configure(sw, *(enum tb_switch_tmu_rate *)rate,
					tb_switch_is_clx_enabled(sw, TB_CL1));
		if (tb_switch_tmu_enable(sw))
			tb_sw_dbg(sw, "fail switching TMU mode for 1st depth router\n");
	}

	return 0;
}

/**
 * tb_switch_enable_tmu_1st_child - Configure and enable TMU for 1st chidren
 * @sw: The router to configure and enable it's children TMU
 * @rate: Rate of the TMU to configure the router's chidren to
 *
 * Configures and enables the TMU mode of 1st depth children of the specified
 * router to the specified rate.
 */
void tb_switch_enable_tmu_1st_child(struct tb_switch *sw,
				    enum tb_switch_tmu_rate rate)
{
	device_for_each_child(&sw->dev, &rate,
			      tb_switch_tmu_config_enable);
}
