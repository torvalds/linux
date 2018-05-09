// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>

#include <dt-bindings/clock/qcom,rpmh.h>

#define CLK_RPMH_ARC_EN_OFFSET		0
#define CLK_RPMH_VRM_EN_OFFSET		4

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
	struct device *dev;
	struct clk_rpmh *peer;
};

struct clk_rpmh_desc {
	struct clk_hw **clks;
	size_t num_clks;
};

static DEFINE_MUTEX(rpmh_clk_lock);

#define __DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  _res_en_offset, _res_on, _div)		\
	static struct clk_rpmh _platform##_##_name_active;		\
	static struct clk_rpmh _platform##_##_name = {			\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.peer = &_platform##_##_name_active,			\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
				      BIT(RPMH_ACTIVE_ONLY_STATE) |	\
				      BIT(RPMH_SLEEP_STATE)),		\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name,					\
			.parent_names = (const char *[]){ "xo_board" },	\
			.num_parents = 1,				\
		},							\
	};								\
	static struct clk_rpmh _platform##_##_name_active = {		\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.peer = &_platform##_##_name,				\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
					BIT(RPMH_ACTIVE_ONLY_STATE)),	\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name_active,				\
			.parent_names = (const char *[]){ "xo_board" },	\
			.num_parents = 1,				\
		},							\
	}

#define DEFINE_CLK_RPMH_ARC(_platform, _name, _name_active, _res_name,	\
			    _res_on, _div)				\
	__DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  CLK_RPMH_ARC_EN_OFFSET, _res_on, _div)

#define DEFINE_CLK_RPMH_VRM(_platform, _name, _name_active, _res_name,	\
				_div)					\
	__DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  CLK_RPMH_VRM_EN_OFFSET, 1, _div)

static inline struct clk_rpmh *to_clk_rpmh(struct clk_hw *_hw)
{
	return container_of(_hw, struct clk_rpmh, hw);
}

static inline bool has_state_changed(struct clk_rpmh *c, u32 state)
{
	return (c->last_sent_aggr_state & BIT(state))
		!= (c->aggr_state & BIT(state));
}

static int clk_rpmh_send_aggregate_command(struct clk_rpmh *c)
{
	struct tcs_cmd cmd = { 0 };
	u32 cmd_state, on_val;
	enum rpmh_state state = RPMH_SLEEP_STATE;
	int ret;

	cmd.addr = c->res_addr;
	cmd_state = c->aggr_state;
	on_val = c->res_on_val;

	for (; state <= RPMH_ACTIVE_ONLY_STATE; state++) {
		if (has_state_changed(c, state)) {
			if (cmd_state & BIT(state))
				cmd.data = on_val;

			ret = rpmh_write_async(c->dev, state, &cmd, 1);
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
	c->peer->last_sent_aggr_state =  c->last_sent_aggr_state;

	return 0;
}

/*
 * Update state and aggregate state values based on enable value.
 */
static int clk_rpmh_aggregate_state_send_command(struct clk_rpmh *c,
						bool enable)
{
	int ret;

	/* Nothing required to be done if already off or on */
	if (enable == c->state)
		return 0;

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
	     enable ? "enable" : "disable");
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
};

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

/* Resource name must match resource id present in cmd-db. */
DEFINE_CLK_RPMH_ARC(sdm845, bi_tcxo, bi_tcxo_ao, "xo.lvl", 0x3, 2);
DEFINE_CLK_RPMH_VRM(sdm845, ln_bb_clk2, ln_bb_clk2_ao, "lnbclka2", 2);
DEFINE_CLK_RPMH_VRM(sdm845, ln_bb_clk3, ln_bb_clk3_ao, "lnbclka3", 2);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk1, rf_clk1_ao, "rfclka1", 1);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk2, rf_clk2_ao, "rfclka2", 1);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk3, rf_clk3_ao, "rfclka3", 1);

static struct clk_hw *sdm845_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdm845 = {
	.clks = sdm845_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdm845_rpmh_clocks),
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
		u32 res_addr;

		rpmh_clk = to_clk_rpmh(hw_clks[i]);
		res_addr = cmd_db_read_addr(rpmh_clk->res_name);
		if (!res_addr) {
			dev_err(&pdev->dev, "missing RPMh resource address for %s\n",
				rpmh_clk->res_name);
			return -ENODEV;
		}
		rpmh_clk->res_addr += res_addr;
		rpmh_clk->dev = &pdev->dev;

		ret = devm_clk_hw_register(&pdev->dev, hw_clks[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register %s\n",
				hw_clks[i]->init->name);
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
	{ .compatible = "qcom,sdm845-rpmh-clk", .data = &clk_rpmh_sdm845},
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
subsys_initcall(clk_rpmh_init);

static void __exit clk_rpmh_exit(void)
{
	platform_driver_unregister(&clk_rpmh_driver);
}
module_exit(clk_rpmh_exit);

MODULE_DESCRIPTION("QCOM RPMh Clock Driver");
MODULE_LICENSE("GPL v2");
