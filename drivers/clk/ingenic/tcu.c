// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU clocks driver
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clockchips.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/ingenic,tcu.h>

/* 8 channels max + watchdog + OST */
#define TCU_CLK_COUNT	10

#undef pr_fmt
#define pr_fmt(fmt) "ingenic-tcu-clk: " fmt

enum tcu_clk_parent {
	TCU_PARENT_PCLK,
	TCU_PARENT_RTC,
	TCU_PARENT_EXT,
};

struct ingenic_soc_info {
	unsigned int num_channels;
	bool has_ost;
	bool has_tcu_clk;
};

struct ingenic_tcu_clk_info {
	struct clk_init_data init_data;
	u8 gate_bit;
	u8 tcsr_reg;
};

struct ingenic_tcu_clk {
	struct clk_hw hw;
	unsigned int idx;
	struct ingenic_tcu *tcu;
	const struct ingenic_tcu_clk_info *info;
};

struct ingenic_tcu {
	const struct ingenic_soc_info *soc_info;
	struct regmap *map;
	struct clk *clk;

	struct clk_hw_onecell_data *clocks;
};

static struct ingenic_tcu *ingenic_tcu;

static inline struct ingenic_tcu_clk *to_tcu_clk(struct clk_hw *hw)
{
	return container_of(hw, struct ingenic_tcu_clk, hw);
}

static int ingenic_tcu_enable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;

	regmap_write(tcu->map, TCU_REG_TSCR, BIT(info->gate_bit));

	return 0;
}

static void ingenic_tcu_disable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;

	regmap_write(tcu->map, TCU_REG_TSSR, BIT(info->gate_bit));
}

static int ingenic_tcu_is_enabled(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int value;

	regmap_read(tcu_clk->tcu->map, TCU_REG_TSR, &value);

	return !(value & BIT(info->gate_bit));
}

static bool ingenic_tcu_enable_regs(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;
	bool enabled = false;

	/*
	 * If the SoC has no global TCU clock, we must ungate the channel's
	 * clock to be able to access its registers.
	 * If we have a TCU clock, it will be enabled automatically as it has
	 * been attached to the regmap.
	 */
	if (!tcu->clk) {
		enabled = !!ingenic_tcu_is_enabled(hw);
		regmap_write(tcu->map, TCU_REG_TSCR, BIT(info->gate_bit));
	}

	return enabled;
}

static void ingenic_tcu_disable_regs(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;

	if (!tcu->clk)
		regmap_write(tcu->map, TCU_REG_TSSR, BIT(info->gate_bit));
}

static u8 ingenic_tcu_get_parent(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int val = 0;
	int ret;

	ret = regmap_read(tcu_clk->tcu->map, info->tcsr_reg, &val);
	WARN_ONCE(ret < 0, "Unable to read TCSR %d", tcu_clk->idx);

	return ffs(val & TCU_TCSR_PARENT_CLOCK_MASK) - 1;
}

static int ingenic_tcu_set_parent(struct clk_hw *hw, u8 idx)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	bool was_enabled;
	int ret;

	was_enabled = ingenic_tcu_enable_regs(hw);

	ret = regmap_update_bits(tcu_clk->tcu->map, info->tcsr_reg,
				 TCU_TCSR_PARENT_CLOCK_MASK, BIT(idx));
	WARN_ONCE(ret < 0, "Unable to update TCSR %d", tcu_clk->idx);

	if (!was_enabled)
		ingenic_tcu_disable_regs(hw);

	return 0;
}

static unsigned long ingenic_tcu_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int prescale;
	int ret;

	ret = regmap_read(tcu_clk->tcu->map, info->tcsr_reg, &prescale);
	WARN_ONCE(ret < 0, "Unable to read TCSR %d", tcu_clk->idx);

	prescale = (prescale & TCU_TCSR_PRESCALE_MASK) >> TCU_TCSR_PRESCALE_LSB;

	return parent_rate >> (prescale * 2);
}

static u8 ingenic_tcu_get_prescale(unsigned long rate, unsigned long req_rate)
{
	u8 prescale;

	for (prescale = 0; prescale < 5; prescale++)
		if ((rate >> (prescale * 2)) <= req_rate)
			return prescale;

	return 5; /* /1024 divider */
}

static long ingenic_tcu_round_rate(struct clk_hw *hw, unsigned long req_rate,
		unsigned long *parent_rate)
{
	unsigned long rate = *parent_rate;
	u8 prescale;

	if (req_rate > rate)
		return rate;

	prescale = ingenic_tcu_get_prescale(rate, req_rate);

	return rate >> (prescale * 2);
}

static int ingenic_tcu_set_rate(struct clk_hw *hw, unsigned long req_rate,
		unsigned long parent_rate)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	u8 prescale = ingenic_tcu_get_prescale(parent_rate, req_rate);
	bool was_enabled;
	int ret;

	was_enabled = ingenic_tcu_enable_regs(hw);

	ret = regmap_update_bits(tcu_clk->tcu->map, info->tcsr_reg,
				 TCU_TCSR_PRESCALE_MASK,
				 prescale << TCU_TCSR_PRESCALE_LSB);
	WARN_ONCE(ret < 0, "Unable to update TCSR %d", tcu_clk->idx);

	if (!was_enabled)
		ingenic_tcu_disable_regs(hw);

	return 0;
}

static const struct clk_ops ingenic_tcu_clk_ops = {
	.get_parent	= ingenic_tcu_get_parent,
	.set_parent	= ingenic_tcu_set_parent,

	.recalc_rate	= ingenic_tcu_recalc_rate,
	.round_rate	= ingenic_tcu_round_rate,
	.set_rate	= ingenic_tcu_set_rate,

	.enable		= ingenic_tcu_enable,
	.disable	= ingenic_tcu_disable,
	.is_enabled	= ingenic_tcu_is_enabled,
};

static const char * const ingenic_tcu_timer_parents[] = {
	[TCU_PARENT_PCLK] = "pclk",
	[TCU_PARENT_RTC]  = "rtc",
	[TCU_PARENT_EXT]  = "ext",
};

#define DEF_TIMER(_name, _gate_bit, _tcsr)				\
	{								\
		.init_data = {						\
			.name = _name,					\
			.parent_names = ingenic_tcu_timer_parents,	\
			.num_parents = ARRAY_SIZE(ingenic_tcu_timer_parents),\
			.ops = &ingenic_tcu_clk_ops,			\
			.flags = CLK_SET_RATE_UNGATE,			\
		},							\
		.gate_bit = _gate_bit,					\
		.tcsr_reg = _tcsr,					\
	}
static const struct ingenic_tcu_clk_info ingenic_tcu_clk_info[] = {
	[TCU_CLK_TIMER0] = DEF_TIMER("timer0", 0, TCU_REG_TCSRc(0)),
	[TCU_CLK_TIMER1] = DEF_TIMER("timer1", 1, TCU_REG_TCSRc(1)),
	[TCU_CLK_TIMER2] = DEF_TIMER("timer2", 2, TCU_REG_TCSRc(2)),
	[TCU_CLK_TIMER3] = DEF_TIMER("timer3", 3, TCU_REG_TCSRc(3)),
	[TCU_CLK_TIMER4] = DEF_TIMER("timer4", 4, TCU_REG_TCSRc(4)),
	[TCU_CLK_TIMER5] = DEF_TIMER("timer5", 5, TCU_REG_TCSRc(5)),
	[TCU_CLK_TIMER6] = DEF_TIMER("timer6", 6, TCU_REG_TCSRc(6)),
	[TCU_CLK_TIMER7] = DEF_TIMER("timer7", 7, TCU_REG_TCSRc(7)),
};

static const struct ingenic_tcu_clk_info ingenic_tcu_watchdog_clk_info =
					 DEF_TIMER("wdt", 16, TCU_REG_WDT_TCSR);
static const struct ingenic_tcu_clk_info ingenic_tcu_ost_clk_info =
					 DEF_TIMER("ost", 15, TCU_REG_OST_TCSR);
#undef DEF_TIMER

static int __init ingenic_tcu_register_clock(struct ingenic_tcu *tcu,
			unsigned int idx, enum tcu_clk_parent parent,
			const struct ingenic_tcu_clk_info *info,
			struct clk_hw_onecell_data *clocks)
{
	struct ingenic_tcu_clk *tcu_clk;
	int err;

	tcu_clk = kzalloc(sizeof(*tcu_clk), GFP_KERNEL);
	if (!tcu_clk)
		return -ENOMEM;

	tcu_clk->hw.init = &info->init_data;
	tcu_clk->idx = idx;
	tcu_clk->info = info;
	tcu_clk->tcu = tcu;

	/* Reset channel and clock divider, set default parent */
	ingenic_tcu_enable_regs(&tcu_clk->hw);
	regmap_update_bits(tcu->map, info->tcsr_reg, 0xffff, BIT(parent));
	ingenic_tcu_disable_regs(&tcu_clk->hw);

	err = clk_hw_register(NULL, &tcu_clk->hw);
	if (err) {
		kfree(tcu_clk);
		return err;
	}

	clocks->hws[idx] = &tcu_clk->hw;

	return 0;
}

static const struct ingenic_soc_info jz4740_soc_info = {
	.num_channels = 8,
	.has_ost = false,
	.has_tcu_clk = true,
};

static const struct ingenic_soc_info jz4725b_soc_info = {
	.num_channels = 6,
	.has_ost = true,
	.has_tcu_clk = true,
};

static const struct ingenic_soc_info jz4770_soc_info = {
	.num_channels = 8,
	.has_ost = true,
	.has_tcu_clk = false,
};

static const struct ingenic_soc_info x1000_soc_info = {
	.num_channels = 8,
	.has_ost = false, /* X1000 has OST, but it not belong TCU */
	.has_tcu_clk = false,
};

static const struct of_device_id ingenic_tcu_of_match[] __initconst = {
	{ .compatible = "ingenic,jz4740-tcu", .data = &jz4740_soc_info, },
	{ .compatible = "ingenic,jz4725b-tcu", .data = &jz4725b_soc_info, },
	{ .compatible = "ingenic,jz4770-tcu", .data = &jz4770_soc_info, },
	{ .compatible = "ingenic,x1000-tcu", .data = &x1000_soc_info, },
	{ /* sentinel */ }
};

static int __init ingenic_tcu_probe(struct device_node *np)
{
	const struct of_device_id *id = of_match_node(ingenic_tcu_of_match, np);
	struct ingenic_tcu *tcu;
	struct regmap *map;
	unsigned int i;
	int ret;

	map = device_node_to_regmap(np);
	if (IS_ERR(map))
		return PTR_ERR(map);

	tcu = kzalloc(sizeof(*tcu), GFP_KERNEL);
	if (!tcu)
		return -ENOMEM;

	tcu->map = map;
	tcu->soc_info = id->data;

	if (tcu->soc_info->has_tcu_clk) {
		tcu->clk = of_clk_get_by_name(np, "tcu");
		if (IS_ERR(tcu->clk)) {
			ret = PTR_ERR(tcu->clk);
			pr_crit("Cannot get TCU clock\n");
			goto err_free_tcu;
		}

		ret = clk_prepare_enable(tcu->clk);
		if (ret) {
			pr_crit("Unable to enable TCU clock\n");
			goto err_put_clk;
		}
	}

	tcu->clocks = kzalloc(struct_size(tcu->clocks, hws, TCU_CLK_COUNT),
			      GFP_KERNEL);
	if (!tcu->clocks) {
		ret = -ENOMEM;
		goto err_clk_disable;
	}

	tcu->clocks->num = TCU_CLK_COUNT;

	for (i = 0; i < tcu->soc_info->num_channels; i++) {
		ret = ingenic_tcu_register_clock(tcu, i, TCU_PARENT_EXT,
						 &ingenic_tcu_clk_info[i],
						 tcu->clocks);
		if (ret) {
			pr_crit("cannot register clock %d\n", i);
			goto err_unregister_timer_clocks;
		}
	}

	/*
	 * We set EXT as the default parent clock for all the TCU clocks
	 * except for the watchdog one, where we set the RTC clock as the
	 * parent. Since the EXT and PCLK are much faster than the RTC clock,
	 * the watchdog would kick after a maximum time of 5s, and we might
	 * want a slower kicking time.
	 */
	ret = ingenic_tcu_register_clock(tcu, TCU_CLK_WDT, TCU_PARENT_RTC,
					 &ingenic_tcu_watchdog_clk_info,
					 tcu->clocks);
	if (ret) {
		pr_crit("cannot register watchdog clock\n");
		goto err_unregister_timer_clocks;
	}

	if (tcu->soc_info->has_ost) {
		ret = ingenic_tcu_register_clock(tcu, TCU_CLK_OST,
						 TCU_PARENT_EXT,
						 &ingenic_tcu_ost_clk_info,
						 tcu->clocks);
		if (ret) {
			pr_crit("cannot register ost clock\n");
			goto err_unregister_watchdog_clock;
		}
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, tcu->clocks);
	if (ret) {
		pr_crit("cannot add OF clock provider\n");
		goto err_unregister_ost_clock;
	}

	ingenic_tcu = tcu;

	return 0;

err_unregister_ost_clock:
	if (tcu->soc_info->has_ost)
		clk_hw_unregister(tcu->clocks->hws[i + 1]);
err_unregister_watchdog_clock:
	clk_hw_unregister(tcu->clocks->hws[i]);
err_unregister_timer_clocks:
	for (i = 0; i < tcu->clocks->num; i++)
		if (tcu->clocks->hws[i])
			clk_hw_unregister(tcu->clocks->hws[i]);
	kfree(tcu->clocks);
err_clk_disable:
	if (tcu->soc_info->has_tcu_clk)
		clk_disable_unprepare(tcu->clk);
err_put_clk:
	if (tcu->soc_info->has_tcu_clk)
		clk_put(tcu->clk);
err_free_tcu:
	kfree(tcu);
	return ret;
}

static int __maybe_unused tcu_pm_suspend(void)
{
	struct ingenic_tcu *tcu = ingenic_tcu;

	if (tcu->clk)
		clk_disable(tcu->clk);

	return 0;
}

static void __maybe_unused tcu_pm_resume(void)
{
	struct ingenic_tcu *tcu = ingenic_tcu;

	if (tcu->clk)
		clk_enable(tcu->clk);
}

static struct syscore_ops __maybe_unused tcu_pm_ops = {
	.suspend = tcu_pm_suspend,
	.resume = tcu_pm_resume,
};

static void __init ingenic_tcu_init(struct device_node *np)
{
	int ret = ingenic_tcu_probe(np);

	if (ret)
		pr_crit("Failed to initialize TCU clocks: %d\n", ret);

	if (IS_ENABLED(CONFIG_PM_SLEEP))
		register_syscore_ops(&tcu_pm_ops);
}

CLK_OF_DECLARE_DRIVER(jz4740_cgu, "ingenic,jz4740-tcu", ingenic_tcu_init);
CLK_OF_DECLARE_DRIVER(jz4725b_cgu, "ingenic,jz4725b-tcu", ingenic_tcu_init);
CLK_OF_DECLARE_DRIVER(jz4770_cgu, "ingenic,jz4770-tcu", ingenic_tcu_init);
CLK_OF_DECLARE_DRIVER(x1000_cgu, "ingenic,x1000-tcu", ingenic_tcu_init);
