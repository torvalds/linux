// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string_choices.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include <dt-bindings/clock/qcom,rpmh.h>

#define CLK_RPMH_ARC_EN_OFFSET		0
#define CLK_RPMH_VRM_EN_OFFSET		4

/**
 * struct bcm_db - Auxiliary data pertaining to each Bus Clock Manager(BCM)
 * @unit: divisor used to convert Hz value to an RPMh msg
 * @width: multiplier used to convert Hz value to an RPMh msg
 * @vcd: virtual clock domain that this bcm belongs to
 * @reserved: reserved to pad the struct
 */
struct bcm_db {
	__le32 unit;
	__le16 width;
	u8 vcd;
	u8 reserved;
};

/**
 * struct clk_rpmh - individual rpmh clock data structure
 * @hw:			handle between common and hardware-specific interfaces
 * @res_name:		resource name for the rpmh clock
 * @div:		clock divider to compute the clock rate
 * @res_addr:		base address of the rpmh resource within the RPMh
 * @res_on_val:		rpmh clock enable value
 * @state:		rpmh clock requested state
 * @aggr_state:		rpmh clock aggregated state
 * @last_sent_aggr_state: rpmh clock last aggr state sent to RPMh
 * @valid_state_mask:	mask to determine the state of the rpmh clock
 * @unit:		divisor to convert rate to rpmh msg in magnitudes of Khz
 * @dev:		device to which it is attached
 * @peer:		pointer to the clock rpmh sibling
 */
struct clk_rpmh {
	struct clk_hw hw;
	const char *res_name;
	u8 div;
	u32 res_addr;
	u32 res_on_val;
	u32 state;
	u32 aggr_state;
	u32 last_sent_aggr_state;
	u32 valid_state_mask;
	u32 unit;
	struct device *dev;
	struct clk_rpmh *peer;
};

struct clk_rpmh_desc {
	struct clk_hw **clks;
	size_t num_clks;
	/* RPMh clock clkaN are optional for this platform */
	bool clka_optional;
};

static DEFINE_MUTEX(rpmh_clk_lock);

#define __DEFINE_CLK_RPMH(_name, _clk_name, _res_name,			\
			  _res_en_offset, _res_on, _div)		\
	static struct clk_rpmh clk_rpmh_##_clk_name##_ao;		\
	static struct clk_rpmh clk_rpmh_##_clk_name = {			\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.peer = &clk_rpmh_##_clk_name##_ao,			\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
				      BIT(RPMH_ACTIVE_ONLY_STATE) |	\
				      BIT(RPMH_SLEEP_STATE)),		\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name,					\
			.parent_data = &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				\
		},							\
	};								\
	static struct clk_rpmh clk_rpmh_##_clk_name##_ao= {		\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.peer = &clk_rpmh_##_clk_name,				\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
					BIT(RPMH_ACTIVE_ONLY_STATE)),	\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name "_ao",				\
			.parent_data = &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				\
		},							\
	}

#define DEFINE_CLK_RPMH_ARC(_name, _res_name, _res_on, _div)		\
	__DEFINE_CLK_RPMH(_name, _name##_##div##_div, _res_name,	\
			  CLK_RPMH_ARC_EN_OFFSET, _res_on, _div)

#define DEFINE_CLK_RPMH_VRM(_name, _suffix, _res_name, _div)		\
	__DEFINE_CLK_RPMH(_name, _name##_suffix, _res_name,		\
			  CLK_RPMH_VRM_EN_OFFSET, 1, _div)

#define DEFINE_CLK_RPMH_BCM(_name, _res_name)				\
	static struct clk_rpmh clk_rpmh_##_name = {			\
		.res_name = _res_name,					\
		.valid_state_mask = BIT(RPMH_ACTIVE_ONLY_STATE),	\
		.div = 1,						\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_bcm_ops,			\
			.name = #_name,					\
		},							\
	}

static inline struct clk_rpmh *to_clk_rpmh(struct clk_hw *_hw)
{
	return container_of(_hw, struct clk_rpmh, hw);
}

static inline bool has_state_changed(struct clk_rpmh *c, u32 state)
{
	return (c->last_sent_aggr_state & BIT(state))
		!= (c->aggr_state & BIT(state));
}

static int clk_rpmh_send(struct clk_rpmh *c, enum rpmh_state state,
			 struct tcs_cmd *cmd, bool wait)
{
	if (wait)
		return rpmh_write(c->dev, state, cmd, 1);

	return rpmh_write_async(c->dev, state, cmd, 1);
}

static int clk_rpmh_send_aggregate_command(struct clk_rpmh *c)
{
	struct tcs_cmd cmd = { 0 };
	u32 cmd_state, on_val;
	enum rpmh_state state = RPMH_SLEEP_STATE;
	int ret;
	bool wait;

	cmd.addr = c->res_addr;
	cmd_state = c->aggr_state;
	on_val = c->res_on_val;

	for (; state <= RPMH_ACTIVE_ONLY_STATE; state++) {
		if (has_state_changed(c, state)) {
			if (cmd_state & BIT(state))
				cmd.data = on_val;

			wait = cmd_state && state == RPMH_ACTIVE_ONLY_STATE;
			ret = clk_rpmh_send(c, state, &cmd, wait);
			if (ret) {
				dev_err(c->dev, "set %s state of %s failed: (%d)\n",
					!state ? "sleep" :
					state == RPMH_WAKE_ONLY_STATE	?
					"wake" : "active", c->res_name, ret);
				return ret;
			}
		}
	}

	c->last_sent_aggr_state = c->aggr_state;
	c->peer->last_sent_aggr_state = c->last_sent_aggr_state;

	return 0;
}

/*
 * Update state and aggregate state values based on enable value.
 */
static int clk_rpmh_aggregate_state_send_command(struct clk_rpmh *c,
						bool enable)
{
	int ret;

	c->state = enable ? c->valid_state_mask : 0;
	c->aggr_state = c->state | c->peer->state;
	c->peer->aggr_state = c->aggr_state;

	ret = clk_rpmh_send_aggregate_command(c);
	if (!ret)
		return 0;

	if (ret && enable)
		c->state = 0;
	else if (ret)
		c->state = c->valid_state_mask;

	WARN(1, "clk: %s failed to %s\n", c->res_name,
	     str_enable_disable(enable));
	return ret;
}

static int clk_rpmh_prepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);
	int ret = 0;

	mutex_lock(&rpmh_clk_lock);
	ret = clk_rpmh_aggregate_state_send_command(c, true);
	mutex_unlock(&rpmh_clk_lock);

	return ret;
}

static void clk_rpmh_unprepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	mutex_lock(&rpmh_clk_lock);
	clk_rpmh_aggregate_state_send_command(c, false);
	mutex_unlock(&rpmh_clk_lock);
};

static unsigned long clk_rpmh_recalc_rate(struct clk_hw *hw,
					unsigned long prate)
{
	struct clk_rpmh *r = to_clk_rpmh(hw);

	/*
	 * RPMh clocks have a fixed rate. Return static rate.
	 */
	return prate / r->div;
}

static const struct clk_ops clk_rpmh_ops = {
	.prepare	= clk_rpmh_prepare,
	.unprepare	= clk_rpmh_unprepare,
	.recalc_rate	= clk_rpmh_recalc_rate,
};

static int clk_rpmh_bcm_send_cmd(struct clk_rpmh *c, bool enable)
{
	struct tcs_cmd cmd = { 0 };
	u32 cmd_state;
	int ret = 0;

	mutex_lock(&rpmh_clk_lock);
	if (enable) {
		cmd_state = 1;
		if (c->aggr_state)
			cmd_state = c->aggr_state;
	} else {
		cmd_state = 0;
	}

	cmd_state = min(cmd_state, BCM_TCS_CMD_VOTE_MASK);

	if (c->last_sent_aggr_state != cmd_state) {
		cmd.addr = c->res_addr;
		cmd.data = BCM_TCS_CMD(1, enable, 0, cmd_state);

		/*
		 * Send only an active only state request. RPMh continues to
		 * use the active state when we're in sleep/wake state as long
		 * as the sleep/wake state has never been set.
		 */
		ret = clk_rpmh_send(c, RPMH_ACTIVE_ONLY_STATE, &cmd, enable);
		if (ret) {
			dev_err(c->dev, "set active state of %s failed: (%d)\n",
				c->res_name, ret);
		} else {
			c->last_sent_aggr_state = cmd_state;
		}
	}

	mutex_unlock(&rpmh_clk_lock);

	return ret;
}

static int clk_rpmh_bcm_prepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	return clk_rpmh_bcm_send_cmd(c, true);
}

static void clk_rpmh_bcm_unprepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	clk_rpmh_bcm_send_cmd(c, false);
}

static int clk_rpmh_bcm_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	c->aggr_state = rate / c->unit;
	/*
	 * Since any non-zero value sent to hw would result in enabling the
	 * clock, only send the value if the clock has already been prepared.
	 */
	if (clk_hw_is_prepared(hw))
		clk_rpmh_bcm_send_cmd(c, true);

	return 0;
}

static int clk_rpmh_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	return 0;
}

static unsigned long clk_rpmh_bcm_recalc_rate(struct clk_hw *hw,
					unsigned long prate)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	return (unsigned long)c->aggr_state * c->unit;
}

static const struct clk_ops clk_rpmh_bcm_ops = {
	.prepare	= clk_rpmh_bcm_prepare,
	.unprepare	= clk_rpmh_bcm_unprepare,
	.set_rate	= clk_rpmh_bcm_set_rate,
	.determine_rate = clk_rpmh_determine_rate,
	.recalc_rate	= clk_rpmh_bcm_recalc_rate,
};

/* Resource name must match resource id present in cmd-db */
DEFINE_CLK_RPMH_ARC(bi_tcxo, "xo.lvl", 0x3, 1);
DEFINE_CLK_RPMH_ARC(bi_tcxo, "xo.lvl", 0x3, 2);
DEFINE_CLK_RPMH_ARC(bi_tcxo, "xo.lvl", 0x3, 4);
DEFINE_CLK_RPMH_ARC(qlink, "qphy.lvl", 0x1, 4);

DEFINE_CLK_RPMH_VRM(ln_bb_clk1, _a2, "lnbclka1", 2);
DEFINE_CLK_RPMH_VRM(ln_bb_clk2, _a2, "lnbclka2", 2);
DEFINE_CLK_RPMH_VRM(ln_bb_clk3, _a2, "lnbclka3", 2);

DEFINE_CLK_RPMH_VRM(ln_bb_clk1, _a4, "lnbclka1", 4);
DEFINE_CLK_RPMH_VRM(ln_bb_clk2, _a4, "lnbclka2", 4);
DEFINE_CLK_RPMH_VRM(ln_bb_clk3, _a4, "lnbclka3", 4);

DEFINE_CLK_RPMH_VRM(ln_bb_clk2, _g4, "lnbclkg2", 4);
DEFINE_CLK_RPMH_VRM(ln_bb_clk3, _g4, "lnbclkg3", 4);

DEFINE_CLK_RPMH_VRM(rf_clk1, _a, "rfclka1", 1);
DEFINE_CLK_RPMH_VRM(rf_clk2, _a, "rfclka2", 1);
DEFINE_CLK_RPMH_VRM(rf_clk3, _a, "rfclka3", 1);
DEFINE_CLK_RPMH_VRM(rf_clk4, _a, "rfclka4", 1);
DEFINE_CLK_RPMH_VRM(rf_clk5, _a, "rfclka5", 1);

DEFINE_CLK_RPMH_VRM(rf_clk1, _d, "rfclkd1", 1);
DEFINE_CLK_RPMH_VRM(rf_clk2, _d, "rfclkd2", 1);
DEFINE_CLK_RPMH_VRM(rf_clk3, _d, "rfclkd3", 1);
DEFINE_CLK_RPMH_VRM(rf_clk4, _d, "rfclkd4", 1);

DEFINE_CLK_RPMH_VRM(rf_clk3, _a2, "rfclka3", 2);

DEFINE_CLK_RPMH_VRM(clk1, _a1, "clka1", 1);
DEFINE_CLK_RPMH_VRM(clk2, _a1, "clka2", 1);
DEFINE_CLK_RPMH_VRM(clk3, _a1, "clka3", 1);
DEFINE_CLK_RPMH_VRM(clk4, _a1, "clka4", 1);
DEFINE_CLK_RPMH_VRM(clk5, _a1, "clka5", 1);

DEFINE_CLK_RPMH_VRM(clk3, _a2, "clka3", 2);
DEFINE_CLK_RPMH_VRM(clk4, _a2, "clka4", 2);
DEFINE_CLK_RPMH_VRM(clk5, _a2, "clka5", 2);
DEFINE_CLK_RPMH_VRM(clk6, _a2, "clka6", 2);
DEFINE_CLK_RPMH_VRM(clk7, _a2, "clka7", 2);
DEFINE_CLK_RPMH_VRM(clk8, _a2, "clka8", 2);

DEFINE_CLK_RPMH_VRM(clk7, _a4, "clka7", 4);

DEFINE_CLK_RPMH_VRM(div_clk1, _div2, "divclka1", 2);

DEFINE_CLK_RPMH_VRM(clk3, _a, "C3A_E0", 1);
DEFINE_CLK_RPMH_VRM(clk4, _a, "C4A_E0", 1);
DEFINE_CLK_RPMH_VRM(clk5, _a, "C5A_E0", 1);
DEFINE_CLK_RPMH_VRM(clk8, _a, "C8A_E0", 1);

DEFINE_CLK_RPMH_BCM(ce, "CE0");
DEFINE_CLK_RPMH_BCM(hwkm, "HK0");
DEFINE_CLK_RPMH_BCM(ipa, "IP0");
DEFINE_CLK_RPMH_BCM(pka, "PKA0");
DEFINE_CLK_RPMH_BCM(qpic_clk, "QP0");

static struct clk_hw *sar2130p_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div1.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div1_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sar2130p = {
	.clks = sar2130p_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sar2130p_rpmh_clocks),
};

static struct clk_hw *sdm845_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
	[RPMH_CE_CLK]		= &clk_rpmh_ce.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdm845 = {
	.clks = sdm845_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdm845_rpmh_clocks),
};

static struct clk_hw *sa8775p_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_ln_bb_clk1_a2.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a4_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
	[RPMH_PKA_CLK]		= &clk_rpmh_pka.hw,
	[RPMH_HWKM_CLK]		= &clk_rpmh_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sa8775p = {
	.clks = sa8775p_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sa8775p_rpmh_clocks),
};

static struct clk_hw *sdm670_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
	[RPMH_CE_CLK]		= &clk_rpmh_ce.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdm670 = {
	.clks = sdm670_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdm670_rpmh_clocks),
};

static struct clk_hw *sdx55_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_d.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_d_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_d.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_d_ao.hw,
	[RPMH_QPIC_CLK]		= &clk_rpmh_qpic_clk.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdx55 = {
	.clks = sdx55_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdx55_rpmh_clocks),
};

static struct clk_hw *sm8150_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8150 = {
	.clks = sm8150_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8150_rpmh_clocks),
};

static struct clk_hw *sc7180_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc7180 = {
	.clks = sc7180_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc7180_rpmh_clocks),
};

static struct clk_hw *sc8180x_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_d.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_d_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_d.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_d_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_d.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_d_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc8180x = {
	.clks = sc8180x_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc8180x_rpmh_clocks),
};

static struct clk_hw *milos_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_clk7_a4.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_clk7_a4_ao.hw,
	/*
	 * RPMH_LN_BB_CLK3(_A) and RPMH_LN_BB_CLK4(_A) are marked as optional
	 * downstream, but do not exist in cmd-db on SM7635, so skip them.
	 */
	[RPMH_RF_CLK1]		= &clk_rpmh_clk1_a1.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_clk1_a1_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_clk2_a1.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_clk2_a1_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_clk3_a1.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_clk3_a1_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_milos = {
	.clks = milos_rpmh_clocks,
	.num_clks = ARRAY_SIZE(milos_rpmh_clocks),
};

static struct clk_hw *sm8250_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_ln_bb_clk1_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_ln_bb_clk1_a2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8250 = {
	.clks = sm8250_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8250_rpmh_clocks),
};

static struct clk_hw *sm8350_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_DIV_CLK1]		= &clk_rpmh_div_clk1_div2.hw,
	[RPMH_DIV_CLK1_A]	= &clk_rpmh_div_clk1_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_ln_bb_clk1_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_ln_bb_clk1_a2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_RF_CLK4]		= &clk_rpmh_rf_clk4_a.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_rf_clk4_a_ao.hw,
	[RPMH_RF_CLK5]		= &clk_rpmh_rf_clk5_a.hw,
	[RPMH_RF_CLK5_A]	= &clk_rpmh_rf_clk5_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
	[RPMH_PKA_CLK]		= &clk_rpmh_pka.hw,
	[RPMH_HWKM_CLK]		= &clk_rpmh_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8350 = {
	.clks = sm8350_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8350_rpmh_clocks),
};

static struct clk_hw *sc8280xp_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK3]       = &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]     = &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_IPA_CLK]          = &clk_rpmh_ipa.hw,
	[RPMH_PKA_CLK]          = &clk_rpmh_pka.hw,
	[RPMH_HWKM_CLK]         = &clk_rpmh_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc8280xp = {
	.clks = sc8280xp_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc8280xp_rpmh_clocks),
};

static struct clk_hw *sm8450_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_ln_bb_clk1_a4.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_ln_bb_clk1_a4_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a4.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a4_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_RF_CLK4]		= &clk_rpmh_rf_clk4_a.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_rf_clk4_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8450 = {
	.clks = sm8450_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8450_rpmh_clocks),
};

static struct clk_hw *sm8550_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_clk6_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_clk6_a2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_clk7_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_clk7_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_clk8_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_clk8_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_clk1_a1.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_clk1_a1_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_clk2_a1.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_clk2_a1_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_clk3_a1.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_clk3_a1_ao.hw,
	[RPMH_RF_CLK4]		= &clk_rpmh_clk4_a1.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_clk4_a1_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8550 = {
	.clks = sm8550_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8550_rpmh_clocks),
	.clka_optional = true,
};

static struct clk_hw *sm8650_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_clk6_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_clk6_a2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_clk7_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_clk7_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_clk8_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_clk8_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_clk1_a1.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_clk1_a1_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_clk2_a1.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_clk2_a1_ao.hw,
	/*
	 * The clka3 RPMh resource is missing in cmd-db
	 * for current platforms, while the clka3 exists
	 * on the PMK8550, the clock is unconnected and
	 * unused.
	 */
	[RPMH_RF_CLK4]		= &clk_rpmh_clk4_a2.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_clk4_a2_ao.hw,
	[RPMH_RF_CLK5]		= &clk_rpmh_clk5_a2.hw,
	[RPMH_RF_CLK5_A]	= &clk_rpmh_clk5_a2_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8650 = {
	.clks = sm8650_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8650_rpmh_clocks),
	.clka_optional = true,
};

static struct clk_hw *sc7280_rpmh_clocks[] = {
	[RPMH_CXO_CLK]      = &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]    = &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK2]   = &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A] = &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_RF_CLK1]      = &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]    = &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK3]      = &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]    = &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_RF_CLK4]      = &clk_rpmh_rf_clk4_a.hw,
	[RPMH_RF_CLK4_A]    = &clk_rpmh_rf_clk4_a_ao.hw,
	[RPMH_IPA_CLK]      = &clk_rpmh_ipa.hw,
	[RPMH_PKA_CLK]      = &clk_rpmh_pka.hw,
	[RPMH_HWKM_CLK]     = &clk_rpmh_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc7280 = {
	.clks = sc7280_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc7280_rpmh_clocks),
};

static struct clk_hw *sm6350_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_g4.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_g4_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_g4.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_g4_ao.hw,
	[RPMH_QLINK_CLK]	= &clk_rpmh_qlink_div4.hw,
	[RPMH_QLINK_CLK_A]	= &clk_rpmh_qlink_div4_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm6350 = {
	.clks = sm6350_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm6350_rpmh_clocks),
};

static struct clk_hw *sdx65_rpmh_clocks[] = {
	[RPMH_CXO_CLK]          = &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]        = &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK1]       = &clk_rpmh_ln_bb_clk1_a4.hw,
	[RPMH_LN_BB_CLK1_A]     = &clk_rpmh_ln_bb_clk1_a4_ao.hw,
	[RPMH_RF_CLK1]          = &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]        = &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]          = &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]        = &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]          = &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]        = &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_RF_CLK4]          = &clk_rpmh_rf_clk4_a.hw,
	[RPMH_RF_CLK4_A]        = &clk_rpmh_rf_clk4_a_ao.hw,
	[RPMH_IPA_CLK]          = &clk_rpmh_ipa.hw,
	[RPMH_QPIC_CLK]         = &clk_rpmh_qpic_clk.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdx65 = {
	.clks = sdx65_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdx65_rpmh_clocks),
};

static struct clk_hw *qdu1000_rpmh_clocks[] = {
	[RPMH_CXO_CLK]      = &clk_rpmh_bi_tcxo_div1.hw,
	[RPMH_CXO_CLK_A]    = &clk_rpmh_bi_tcxo_div1_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_qdu1000 = {
	.clks = qdu1000_rpmh_clocks,
	.num_clks = ARRAY_SIZE(qdu1000_rpmh_clocks),
};

static struct clk_hw *sdx75_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a_ao.hw,
	[RPMH_QPIC_CLK]		= &clk_rpmh_qpic_clk.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdx75 = {
	.clks = sdx75_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdx75_rpmh_clocks),
};

static struct clk_hw *sm4450_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div4.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div4_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a4.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a4_ao.hw,
	[RPMH_LN_BB_CLK3]       = &clk_rpmh_ln_bb_clk3_a4.hw,
	[RPMH_LN_BB_CLK3_A]     = &clk_rpmh_ln_bb_clk3_a4_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK5]		= &clk_rpmh_rf_clk5_a.hw,
	[RPMH_RF_CLK5_A]	= &clk_rpmh_rf_clk5_a_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm4450 = {
	.clks = sm4450_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm4450_rpmh_clocks),
};

static struct clk_hw *x1e80100_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_clk6_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_clk6_a2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_clk7_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_clk7_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_clk8_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_clk8_a2_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_clk3_a2.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_clk3_a2_ao.hw,
	[RPMH_RF_CLK4]		= &clk_rpmh_clk4_a2.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_clk4_a2_ao.hw,
	[RPMH_RF_CLK5]		= &clk_rpmh_clk5_a2.hw,
	[RPMH_RF_CLK5_A]	= &clk_rpmh_clk5_a2_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_x1e80100 = {
	.clks = x1e80100_rpmh_clocks,
	.num_clks = ARRAY_SIZE(x1e80100_rpmh_clocks),
};

static struct clk_hw *qcs615_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK2]	= &clk_rpmh_ln_bb_clk2_a2.hw,
	[RPMH_LN_BB_CLK2_A]	= &clk_rpmh_ln_bb_clk2_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_ln_bb_clk3_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_ln_bb_clk3_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_qcs615 = {
	.clks = qcs615_rpmh_clocks,
	.num_clks = ARRAY_SIZE(qcs615_rpmh_clocks),
};

static struct clk_hw *sm8750_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_LN_BB_CLK1]	= &clk_rpmh_clk6_a2.hw,
	[RPMH_LN_BB_CLK1_A]	= &clk_rpmh_clk6_a2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &clk_rpmh_clk8_a2.hw,
	[RPMH_LN_BB_CLK3_A]	= &clk_rpmh_clk8_a2_ao.hw,
	[RPMH_RF_CLK1]		= &clk_rpmh_rf_clk1_a.hw,
	[RPMH_RF_CLK1_A]	= &clk_rpmh_rf_clk1_a_ao.hw,
	[RPMH_RF_CLK2]		= &clk_rpmh_rf_clk2_a.hw,
	[RPMH_RF_CLK2_A]	= &clk_rpmh_rf_clk2_a_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_rf_clk3_a2.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_rf_clk3_a2_ao.hw,
	[RPMH_IPA_CLK]		= &clk_rpmh_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8750 = {
	.clks = sm8750_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8750_rpmh_clocks),
	.clka_optional = true,
};

static struct clk_hw *glymur_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &clk_rpmh_bi_tcxo_div2.hw,
	[RPMH_CXO_CLK_A]	= &clk_rpmh_bi_tcxo_div2_ao.hw,
	[RPMH_RF_CLK3]		= &clk_rpmh_clk3_a.hw,
	[RPMH_RF_CLK3_A]	= &clk_rpmh_clk3_a_ao.hw,
	[RPMH_RF_CLK4]		= &clk_rpmh_clk4_a.hw,
	[RPMH_RF_CLK4_A]	= &clk_rpmh_clk4_a_ao.hw,
	[RPMH_RF_CLK5]		= &clk_rpmh_clk5_a.hw,
	[RPMH_RF_CLK5_A]	= &clk_rpmh_clk5_a_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_glymur = {
	.clks = glymur_rpmh_clocks,
	.num_clks = ARRAY_SIZE(glymur_rpmh_clocks),
};

static struct clk_hw *of_clk_rpmh_hw_get(struct of_phandle_args *clkspec,
					 void *data)
{
	struct clk_rpmh_desc *rpmh = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= rpmh->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return rpmh->clks[idx];
}

static int clk_rpmh_probe(struct platform_device *pdev)
{
	struct clk_hw **hw_clks;
	struct clk_rpmh *rpmh_clk;
	const struct clk_rpmh_desc *desc;
	int ret, i;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -ENODEV;

	hw_clks = desc->clks;

	for (i = 0; i < desc->num_clks; i++) {
		const char *name;
		u32 res_addr;
		size_t aux_data_len;
		const struct bcm_db *data;

		if (!hw_clks[i])
			continue;

		name = hw_clks[i]->init->name;

		rpmh_clk = to_clk_rpmh(hw_clks[i]);
		res_addr = cmd_db_read_addr(rpmh_clk->res_name);
		if (!res_addr) {
			hw_clks[i] = NULL;

			if (desc->clka_optional &&
			    !strncmp(rpmh_clk->res_name, "clka", sizeof("clka") - 1))
				continue;

			dev_err(&pdev->dev, "missing RPMh resource address for %s\n",
				rpmh_clk->res_name);
			return -ENODEV;
		}

		data = cmd_db_read_aux_data(rpmh_clk->res_name, &aux_data_len);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			dev_err(&pdev->dev,
				"error reading RPMh aux data for %s (%d)\n",
				rpmh_clk->res_name, ret);
			return ret;
		}

		/* Convert unit from Khz to Hz */
		if (aux_data_len == sizeof(*data))
			rpmh_clk->unit = le32_to_cpu(data->unit) * 1000ULL;

		rpmh_clk->res_addr += res_addr;
		rpmh_clk->dev = &pdev->dev;

		ret = devm_clk_hw_register(&pdev->dev, hw_clks[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register %s\n", name);
			return ret;
		}
	}

	/* typecast to silence compiler warning */
	ret = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_rpmh_hw_get,
					  (void *)desc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add clock provider\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "Registered RPMh clocks\n");

	return 0;
}

static const struct of_device_id clk_rpmh_match_table[] = {
	{ .compatible = "qcom,glymur-rpmh-clk", .data = &clk_rpmh_glymur},
	{ .compatible = "qcom,milos-rpmh-clk", .data = &clk_rpmh_milos},
	{ .compatible = "qcom,qcs615-rpmh-clk", .data = &clk_rpmh_qcs615},
	{ .compatible = "qcom,qdu1000-rpmh-clk", .data = &clk_rpmh_qdu1000},
	{ .compatible = "qcom,sa8775p-rpmh-clk", .data = &clk_rpmh_sa8775p},
	{ .compatible = "qcom,sar2130p-rpmh-clk", .data = &clk_rpmh_sar2130p},
	{ .compatible = "qcom,sc7180-rpmh-clk", .data = &clk_rpmh_sc7180},
	{ .compatible = "qcom,sc7280-rpmh-clk", .data = &clk_rpmh_sc7280},
	{ .compatible = "qcom,sc8180x-rpmh-clk", .data = &clk_rpmh_sc8180x},
	{ .compatible = "qcom,sc8280xp-rpmh-clk", .data = &clk_rpmh_sc8280xp},
	{ .compatible = "qcom,sdm845-rpmh-clk", .data = &clk_rpmh_sdm845},
	{ .compatible = "qcom,sdm670-rpmh-clk", .data = &clk_rpmh_sdm670},
	{ .compatible = "qcom,sdx55-rpmh-clk",  .data = &clk_rpmh_sdx55},
	{ .compatible = "qcom,sdx65-rpmh-clk",  .data = &clk_rpmh_sdx65},
	{ .compatible = "qcom,sdx75-rpmh-clk",  .data = &clk_rpmh_sdx75},
	{ .compatible = "qcom,sm4450-rpmh-clk", .data = &clk_rpmh_sm4450},
	{ .compatible = "qcom,sm6350-rpmh-clk", .data = &clk_rpmh_sm6350},
	{ .compatible = "qcom,sm8150-rpmh-clk", .data = &clk_rpmh_sm8150},
	{ .compatible = "qcom,sm8250-rpmh-clk", .data = &clk_rpmh_sm8250},
	{ .compatible = "qcom,sm8350-rpmh-clk", .data = &clk_rpmh_sm8350},
	{ .compatible = "qcom,sm8450-rpmh-clk", .data = &clk_rpmh_sm8450},
	{ .compatible = "qcom,sm8550-rpmh-clk", .data = &clk_rpmh_sm8550},
	{ .compatible = "qcom,sm8650-rpmh-clk", .data = &clk_rpmh_sm8650},
	{ .compatible = "qcom,sm8750-rpmh-clk", .data = &clk_rpmh_sm8750},
	{ .compatible = "qcom,x1e80100-rpmh-clk", .data = &clk_rpmh_x1e80100},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rpmh_match_table);

static struct platform_driver clk_rpmh_driver = {
	.probe		= clk_rpmh_probe,
	.driver		= {
		.name	= "clk-rpmh",
		.of_match_table = clk_rpmh_match_table,
	},
};

static int __init clk_rpmh_init(void)
{
	return platform_driver_register(&clk_rpmh_driver);
}
core_initcall(clk_rpmh_init);

static void __exit clk_rpmh_exit(void)
{
	platform_driver_unregister(&clk_rpmh_driver);
}
module_exit(clk_rpmh_exit);

MODULE_DESCRIPTION("QCOM RPMh Clock Driver");
MODULE_LICENSE("GPL v2");
