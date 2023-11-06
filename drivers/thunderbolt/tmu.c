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

static const unsigned int tmu_rates[] = {
	[TB_SWITCH_TMU_MODE_OFF] = 0,
	[TB_SWITCH_TMU_MODE_LOWRES] = 1000,
	[TB_SWITCH_TMU_MODE_HIFI_UNI] = 16,
	[TB_SWITCH_TMU_MODE_HIFI_BI] = 16,
	[TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI] = 16,
};

const struct {
	unsigned int freq_meas_window;
	unsigned int avg_const;
	unsigned int delta_avg_const;
	unsigned int repl_timeout;
	unsigned int repl_threshold;
	unsigned int repl_n;
	unsigned int dirswitch_n;
} tmu_params[] = {
	[TB_SWITCH_TMU_MODE_OFF] = { },
	[TB_SWITCH_TMU_MODE_LOWRES] = { 30, 4, },
	[TB_SWITCH_TMU_MODE_HIFI_UNI] = { 800, 8, },
	[TB_SWITCH_TMU_MODE_HIFI_BI] = { 800, 8, },
	[TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI] = {
		800, 4, 0, 3125, 25, 128, 255,
	},
};

static const char *tmu_mode_name(enum tb_switch_tmu_mode mode)
{
	switch (mode) {
	case TB_SWITCH_TMU_MODE_OFF:
		return "off";
	case TB_SWITCH_TMU_MODE_LOWRES:
		return "uni-directional, LowRes";
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		return "uni-directional, HiFi";
	case TB_SWITCH_TMU_MODE_HIFI_BI:
		return "bi-directional, HiFi";
	case TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI:
		return "enhanced uni-directional, MedRes";
	default:
		return "unknown";
	}
}

static bool tb_switch_tmu_enhanced_is_supported(const struct tb_switch *sw)
{
	return usb4_switch_version(sw) > 1;
}

static int tb_switch_set_tmu_mode_params(struct tb_switch *sw,
					 enum tb_switch_tmu_mode mode)
{
	u32 freq, avg, val;
	int ret;

	freq = tmu_params[mode].freq_meas_window;
	avg = tmu_params[mode].avg_const;

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

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
			 sw->tmu.cap + TMU_RTR_CS_15, 1);
	if (ret)
		return ret;

	if (tb_switch_tmu_enhanced_is_supported(sw)) {
		u32 delta_avg = tmu_params[mode].delta_avg_const;

		ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
				 sw->tmu.cap + TMU_RTR_CS_18, 1);
		if (ret)
			return ret;

		val &= ~TMU_RTR_CS_18_DELTA_AVG_CONST_MASK;
		val |= FIELD_PREP(TMU_RTR_CS_18_DELTA_AVG_CONST_MASK, delta_avg);

		ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
				  sw->tmu.cap + TMU_RTR_CS_18, 1);
	}

	return ret;
}

static bool tb_switch_tmu_ucap_is_supported(struct tb_switch *sw)
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

static bool tb_port_tmu_is_enhanced(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_8, 1);
	if (ret)
		return false;

	return val & TMU_ADP_CS_8_EUDM;
}

/* Can be called to non-v2 lane adapters too */
static int tb_port_tmu_enhanced_enable(struct tb_port *port, bool enable)
{
	int ret;
	u32 val;

	if (!tb_switch_tmu_enhanced_is_supported(port->sw))
		return 0;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_8, 1);
	if (ret)
		return ret;

	if (enable)
		val |= TMU_ADP_CS_8_EUDM;
	else
		val &= ~TMU_ADP_CS_8_EUDM;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_tmu + TMU_ADP_CS_8, 1);
}

static int tb_port_set_tmu_mode_params(struct tb_port *port,
				       enum tb_switch_tmu_mode mode)
{
	u32 repl_timeout, repl_threshold, repl_n, dirswitch_n, val;
	int ret;

	repl_timeout = tmu_params[mode].repl_timeout;
	repl_threshold = tmu_params[mode].repl_threshold;
	repl_n = tmu_params[mode].repl_n;
	dirswitch_n = tmu_params[mode].dirswitch_n;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_8, 1);
	if (ret)
		return ret;

	val &= ~TMU_ADP_CS_8_REPL_TIMEOUT_MASK;
	val &= ~TMU_ADP_CS_8_REPL_THRESHOLD_MASK;
	val |= FIELD_PREP(TMU_ADP_CS_8_REPL_TIMEOUT_MASK, repl_timeout);
	val |= FIELD_PREP(TMU_ADP_CS_8_REPL_THRESHOLD_MASK, repl_threshold);

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_tmu + TMU_ADP_CS_8, 1);
	if (ret)
		return ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_9, 1);
	if (ret)
		return ret;

	val &= ~TMU_ADP_CS_9_REPL_N_MASK;
	val &= ~TMU_ADP_CS_9_DIRSWITCH_N_MASK;
	val |= FIELD_PREP(TMU_ADP_CS_9_REPL_N_MASK, repl_n);
	val |= FIELD_PREP(TMU_ADP_CS_9_DIRSWITCH_N_MASK, dirswitch_n);

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_tmu + TMU_ADP_CS_9, 1);
}

/* Can be called to non-v2 lane adapters too */
static int tb_port_tmu_rate_write(struct tb_port *port, int rate)
{
	int ret;
	u32 val;

	if (!tb_switch_tmu_enhanced_is_supported(port->sw))
		return 0;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_tmu + TMU_ADP_CS_9, 1);
	if (ret)
		return ret;

	val &= ~TMU_ADP_CS_9_ADP_TS_INTERVAL_MASK;
	val |= FIELD_PREP(TMU_ADP_CS_9_ADP_TS_INTERVAL_MASK, rate);

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_tmu + TMU_ADP_CS_9, 1);
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

static int tmu_mode_init(struct tb_switch *sw)
{
	bool enhanced, ucap;
	int ret, rate;

	ucap = tb_switch_tmu_ucap_is_supported(sw);
	if (ucap)
		tb_sw_dbg(sw, "TMU: supports uni-directional mode\n");
	enhanced = tb_switch_tmu_enhanced_is_supported(sw);
	if (enhanced)
		tb_sw_dbg(sw, "TMU: supports enhanced uni-directional mode\n");

	ret = tb_switch_tmu_rate_read(sw);
	if (ret < 0)
		return ret;
	rate = ret;

	/* Off by default */
	sw->tmu.mode = TB_SWITCH_TMU_MODE_OFF;

	if (tb_route(sw)) {
		struct tb_port *up = tb_upstream_port(sw);

		if (enhanced && tb_port_tmu_is_enhanced(up)) {
			sw->tmu.mode = TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI;
		} else if (ucap && tb_port_tmu_is_unidirectional(up)) {
			if (tmu_rates[TB_SWITCH_TMU_MODE_LOWRES] == rate)
				sw->tmu.mode = TB_SWITCH_TMU_MODE_LOWRES;
			else if (tmu_rates[TB_SWITCH_TMU_MODE_LOWRES] == rate)
				sw->tmu.mode = TB_SWITCH_TMU_MODE_HIFI_UNI;
		} else if (rate) {
			sw->tmu.mode = TB_SWITCH_TMU_MODE_HIFI_BI;
		}
	} else if (rate) {
		sw->tmu.mode = TB_SWITCH_TMU_MODE_HIFI_BI;
	}

	/* Update the initial request to match the current mode */
	sw->tmu.mode_request = sw->tmu.mode;
	sw->tmu.has_ucap = ucap;

	return 0;
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

	ret = tmu_mode_init(sw);
	if (ret)
		return ret;

	tb_sw_dbg(sw, "TMU: current mode: %s\n", tmu_mode_name(sw->tmu.mode));
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
		tb_sw_dbg(root_switch, "TMU: local_time[%d]=0x%08x\n", i,
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

static int disable_enhanced(struct tb_port *up, struct tb_port *down)
{
	int ret;

	/*
	 * Router may already been disconnected so ignore errors on the
	 * upstream port.
	 */
	tb_port_tmu_rate_write(up, 0);
	tb_port_tmu_enhanced_enable(up, false);

	ret = tb_port_tmu_rate_write(down, 0);
	if (ret)
		return ret;
	return tb_port_tmu_enhanced_enable(down, false);
}

/**
 * tb_switch_tmu_disable() - Disable TMU of a switch
 * @sw: Switch whose TMU to disable
 *
 * Turns off TMU of @sw if it is enabled. If not enabled does nothing.
 */
int tb_switch_tmu_disable(struct tb_switch *sw)
{
	/* Already disabled? */
	if (sw->tmu.mode == TB_SWITCH_TMU_MODE_OFF)
		return 0;

	if (tb_route(sw)) {
		struct tb_port *down, *up;
		int ret;

		down = tb_switch_downstream_port(sw);
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
		ret = tb_switch_tmu_rate_write(sw, tmu_rates[TB_SWITCH_TMU_MODE_OFF]);
		if (ret)
			return ret;

		tb_port_tmu_time_sync_disable(up);
		ret = tb_port_tmu_time_sync_disable(down);
		if (ret)
			return ret;

		switch (sw->tmu.mode) {
		case TB_SWITCH_TMU_MODE_LOWRES:
		case TB_SWITCH_TMU_MODE_HIFI_UNI:
			/* The switch may be unplugged so ignore any errors */
			tb_port_tmu_unidirectional_disable(up);
			ret = tb_port_tmu_unidirectional_disable(down);
			if (ret)
				return ret;
			break;

		case TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI:
			ret = disable_enhanced(up, down);
			if (ret)
				return ret;
			break;

		default:
			break;
		}
	} else {
		tb_switch_tmu_rate_write(sw, tmu_rates[TB_SWITCH_TMU_MODE_OFF]);
	}

	sw->tmu.mode = TB_SWITCH_TMU_MODE_OFF;

	tb_sw_dbg(sw, "TMU: disabled\n");
	return 0;
}

/* Called only when there is failure enabling requested mode */
static void tb_switch_tmu_off(struct tb_switch *sw)
{
	unsigned int rate = tmu_rates[TB_SWITCH_TMU_MODE_OFF];
	struct tb_port *down, *up;

	down = tb_switch_downstream_port(sw);
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

	switch (sw->tmu.mode_request) {
	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		tb_switch_tmu_rate_write(tb_switch_parent(sw), rate);
		break;
	case TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI:
		disable_enhanced(up, down);
		break;
	default:
		break;
	}

	/* Always set the rate to 0 */
	tb_switch_tmu_rate_write(sw, rate);

	tb_switch_set_tmu_mode_params(sw, sw->tmu.mode);
	tb_port_tmu_unidirectional_disable(down);
	tb_port_tmu_unidirectional_disable(up);
}

/*
 * This function is called when the previous TMU mode was
 * TB_SWITCH_TMU_MODE_OFF.
 */
static int tb_switch_tmu_enable_bidirectional(struct tb_switch *sw)
{
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_switch_downstream_port(sw);

	ret = tb_port_tmu_unidirectional_disable(up);
	if (ret)
		return ret;

	ret = tb_port_tmu_unidirectional_disable(down);
	if (ret)
		goto out;

	ret = tb_switch_tmu_rate_write(sw, tmu_rates[TB_SWITCH_TMU_MODE_HIFI_BI]);
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
	tb_switch_tmu_off(sw);
	return ret;
}

/* Only needed for Titan Ridge */
static int tb_switch_tmu_disable_objections(struct tb_switch *sw)
{
	struct tb_port *up = tb_upstream_port(sw);
	u32 val;
	int ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_vsec_tmu + TB_TIME_VSEC_3_CS_9, 1);
	if (ret)
		return ret;

	val &= ~TB_TIME_VSEC_3_CS_9_TMU_OBJ_MASK;

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH,
			  sw->cap_vsec_tmu + TB_TIME_VSEC_3_CS_9, 1);
	if (ret)
		return ret;

	return tb_port_tmu_write(up, TMU_ADP_CS_6,
				 TMU_ADP_CS_6_DISABLE_TMU_OBJ_MASK,
				 TMU_ADP_CS_6_DISABLE_TMU_OBJ_CL1 |
				 TMU_ADP_CS_6_DISABLE_TMU_OBJ_CL2);
}

/*
 * This function is called when the previous TMU mode was
 * TB_SWITCH_TMU_MODE_OFF.
 */
static int tb_switch_tmu_enable_unidirectional(struct tb_switch *sw)
{
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_switch_downstream_port(sw);
	ret = tb_switch_tmu_rate_write(tb_switch_parent(sw),
				       tmu_rates[sw->tmu.mode_request]);
	if (ret)
		return ret;

	ret = tb_switch_set_tmu_mode_params(sw, sw->tmu.mode_request);
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
	tb_switch_tmu_off(sw);
	return ret;
}

/*
 * This function is called when the previous TMU mode was
 * TB_SWITCH_TMU_RATE_OFF.
 */
static int tb_switch_tmu_enable_enhanced(struct tb_switch *sw)
{
	unsigned int rate = tmu_rates[sw->tmu.mode_request];
	struct tb_port *up, *down;
	int ret;

	/* Router specific parameters first */
	ret = tb_switch_set_tmu_mode_params(sw, sw->tmu.mode_request);
	if (ret)
		return ret;

	up = tb_upstream_port(sw);
	down = tb_switch_downstream_port(sw);

	ret = tb_port_set_tmu_mode_params(up, sw->tmu.mode_request);
	if (ret)
		goto out;

	ret = tb_port_tmu_rate_write(up, rate);
	if (ret)
		goto out;

	ret = tb_port_tmu_enhanced_enable(up, true);
	if (ret)
		goto out;

	ret = tb_port_set_tmu_mode_params(down, sw->tmu.mode_request);
	if (ret)
		goto out;

	ret = tb_port_tmu_rate_write(down, rate);
	if (ret)
		goto out;

	ret = tb_port_tmu_enhanced_enable(down, true);
	if (ret)
		goto out;

	return 0;

out:
	tb_switch_tmu_off(sw);
	return ret;
}

static void tb_switch_tmu_change_mode_prev(struct tb_switch *sw)
{
	unsigned int rate = tmu_rates[sw->tmu.mode];
	struct tb_port *down, *up;

	down = tb_switch_downstream_port(sw);
	up = tb_upstream_port(sw);
	/*
	 * In case of any failure in one of the steps when change mode,
	 * get back to the TMU configurations in previous mode.
	 * In case of additional failures in the functions below,
	 * ignore them since the caller shall already report a failure.
	 */
	switch (sw->tmu.mode) {
	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		tb_port_tmu_set_unidirectional(down, true);
		tb_switch_tmu_rate_write(tb_switch_parent(sw), rate);
		break;

	case TB_SWITCH_TMU_MODE_HIFI_BI:
		tb_port_tmu_set_unidirectional(down, false);
		tb_switch_tmu_rate_write(sw, rate);
		break;

	default:
		break;
	}

	tb_switch_set_tmu_mode_params(sw, sw->tmu.mode);

	switch (sw->tmu.mode) {
	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		tb_port_tmu_set_unidirectional(up, true);
		break;

	case TB_SWITCH_TMU_MODE_HIFI_BI:
		tb_port_tmu_set_unidirectional(up, false);
		break;

	default:
		break;
	}
}

static int tb_switch_tmu_change_mode(struct tb_switch *sw)
{
	unsigned int rate = tmu_rates[sw->tmu.mode_request];
	struct tb_port *up, *down;
	int ret;

	up = tb_upstream_port(sw);
	down = tb_switch_downstream_port(sw);

	/* Program the upstream router downstream facing lane adapter */
	switch (sw->tmu.mode_request) {
	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		ret = tb_port_tmu_set_unidirectional(down, true);
		if (ret)
			goto out;
		ret = tb_switch_tmu_rate_write(tb_switch_parent(sw), rate);
		if (ret)
			goto out;
		break;

	case TB_SWITCH_TMU_MODE_HIFI_BI:
		ret = tb_port_tmu_set_unidirectional(down, false);
		if (ret)
			goto out;
		ret = tb_switch_tmu_rate_write(sw, rate);
		if (ret)
			goto out;
		break;

	default:
		/* Not allowed to change modes from other than above */
		return -EINVAL;
	}

	ret = tb_switch_set_tmu_mode_params(sw, sw->tmu.mode_request);
	if (ret)
		return ret;

	/* Program the new mode and the downstream router lane adapter */
	switch (sw->tmu.mode_request) {
	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		ret = tb_port_tmu_set_unidirectional(up, true);
		if (ret)
			goto out;
		break;

	case TB_SWITCH_TMU_MODE_HIFI_BI:
		ret = tb_port_tmu_set_unidirectional(up, false);
		if (ret)
			goto out;
		break;

	default:
		/* Not allowed to change modes from other than above */
		return -EINVAL;
	}

	ret = tb_port_tmu_time_sync_enable(down);
	if (ret)
		goto out;

	ret = tb_port_tmu_time_sync_enable(up);
	if (ret)
		goto out;

	return 0;

out:
	tb_switch_tmu_change_mode_prev(sw);
	return ret;
}

/**
 * tb_switch_tmu_enable() - Enable TMU on a router
 * @sw: Router whose TMU to enable
 *
 * Enables TMU of a router to be in uni-directional Normal/HiFi or
 * bi-directional HiFi mode. Calling tb_switch_tmu_configure() is
 * required before calling this function.
 */
int tb_switch_tmu_enable(struct tb_switch *sw)
{
	int ret;

	if (tb_switch_tmu_is_enabled(sw))
		return 0;

	if (tb_switch_is_titan_ridge(sw) &&
	    (sw->tmu.mode_request == TB_SWITCH_TMU_MODE_LOWRES ||
	     sw->tmu.mode_request == TB_SWITCH_TMU_MODE_HIFI_UNI)) {
		ret = tb_switch_tmu_disable_objections(sw);
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
		if (sw->tmu.mode == TB_SWITCH_TMU_MODE_OFF) {
			switch (sw->tmu.mode_request) {
			case TB_SWITCH_TMU_MODE_LOWRES:
			case TB_SWITCH_TMU_MODE_HIFI_UNI:
				ret = tb_switch_tmu_enable_unidirectional(sw);
				break;

			case TB_SWITCH_TMU_MODE_HIFI_BI:
				ret = tb_switch_tmu_enable_bidirectional(sw);
				break;
			case TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI:
				ret = tb_switch_tmu_enable_enhanced(sw);
				break;
			default:
				ret = -EINVAL;
				break;
			}
		} else if (sw->tmu.mode == TB_SWITCH_TMU_MODE_LOWRES ||
			   sw->tmu.mode == TB_SWITCH_TMU_MODE_HIFI_UNI ||
			   sw->tmu.mode == TB_SWITCH_TMU_MODE_HIFI_BI) {
			ret = tb_switch_tmu_change_mode(sw);
		} else {
			ret = -EINVAL;
		}
	} else {
		/*
		 * Host router port configurations are written as
		 * part of configurations for downstream port of the parent
		 * of the child node - see above.
		 * Here only the host router' rate configuration is written.
		 */
		ret = tb_switch_tmu_rate_write(sw, tmu_rates[sw->tmu.mode_request]);
	}

	if (ret) {
		tb_sw_warn(sw, "TMU: failed to enable mode %s: %d\n",
			   tmu_mode_name(sw->tmu.mode_request), ret);
	} else {
		sw->tmu.mode = sw->tmu.mode_request;
		tb_sw_dbg(sw, "TMU: mode set to: %s\n", tmu_mode_name(sw->tmu.mode));
	}

	return tb_switch_tmu_set_time_disruption(sw, false);
}

/**
 * tb_switch_tmu_configure() - Configure the TMU mode
 * @sw: Router whose mode to change
 * @mode: Mode to configure
 *
 * Selects the TMU mode that is enabled when tb_switch_tmu_enable() is
 * next called.
 *
 * Returns %0 in success and negative errno otherwise. Specifically
 * returns %-EOPNOTSUPP if the requested mode is not possible (not
 * supported by the router and/or topology).
 */
int tb_switch_tmu_configure(struct tb_switch *sw, enum tb_switch_tmu_mode mode)
{
	switch (mode) {
	case TB_SWITCH_TMU_MODE_OFF:
		break;

	case TB_SWITCH_TMU_MODE_LOWRES:
	case TB_SWITCH_TMU_MODE_HIFI_UNI:
		if (!sw->tmu.has_ucap)
			return -EOPNOTSUPP;
		break;

	case TB_SWITCH_TMU_MODE_HIFI_BI:
		break;

	case TB_SWITCH_TMU_MODE_MEDRES_ENHANCED_UNI: {
		const struct tb_switch *parent_sw = tb_switch_parent(sw);

		if (!parent_sw || !tb_switch_tmu_enhanced_is_supported(parent_sw))
			return -EOPNOTSUPP;
		if (!tb_switch_tmu_enhanced_is_supported(sw))
			return -EOPNOTSUPP;

		break;
	}

	default:
		tb_sw_warn(sw, "TMU: unsupported mode %u\n", mode);
		return -EINVAL;
	}

	if (sw->tmu.mode_request != mode) {
		tb_sw_dbg(sw, "TMU: mode change %s -> %s requested\n",
			  tmu_mode_name(sw->tmu.mode), tmu_mode_name(mode));
		sw->tmu.mode_request = mode;
	}

	return 0;
}
