/*
 * WM831x clock control
 *
 * Copyright 2011-2 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/wm831x/core.h>

struct wm831x_clk {
	struct wm831x *wm831x;
	struct clk_hw xtal_hw;
	struct clk_hw fll_hw;
	struct clk_hw clkout_hw;
	struct clk *xtal;
	struct clk *fll;
	struct clk *clkout;
	bool xtal_ena;
};

static int wm831x_xtal_is_enabled(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  xtal_hw);

	return clkdata->xtal_ena;
}

static unsigned long wm831x_xtal_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  xtal_hw);

	if (clkdata->xtal_ena)
		return 32768;
	else
		return 0;
}

static const struct clk_ops wm831x_xtal_ops = {
	.is_enabled = wm831x_xtal_is_enabled,
	.recalc_rate = wm831x_xtal_recalc_rate,
};

static struct clk_init_data wm831x_xtal_init = {
	.name = "xtal",
	.ops = &wm831x_xtal_ops,
	.flags = CLK_IS_ROOT,
};

static const unsigned long wm831x_fll_auto_rates[] = {
	 2048000,
	11289600,
	12000000,
	12288000,
	19200000,
	22579600,
	24000000,
	24576000,
};

static int wm831x_fll_is_enabled(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_FLL_CONTROL_1);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read FLL_CONTROL_1: %d\n",
			ret);
		return true;
	}

	return (ret & WM831X_FLL_ENA) != 0;
}

static int wm831x_fll_prepare(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_set_bits(wm831x, WM831X_FLL_CONTROL_2,
			      WM831X_FLL_ENA, WM831X_FLL_ENA);
	if (ret != 0)
		dev_crit(wm831x->dev, "Failed to enable FLL: %d\n", ret);

	usleep_range(2000, 2000);

	return ret;
}

static void wm831x_fll_unprepare(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_set_bits(wm831x, WM831X_FLL_CONTROL_2, WM831X_FLL_ENA, 0);
	if (ret != 0)
		dev_crit(wm831x->dev, "Failed to disaable FLL: %d\n", ret);
}

static unsigned long wm831x_fll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_CLOCK_CONTROL_2);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read CLOCK_CONTROL_2: %d\n",
			ret);
		return 0;
	}

	if (ret & WM831X_FLL_AUTO)
		return wm831x_fll_auto_rates[ret & WM831X_FLL_AUTO_FREQ_MASK];

	dev_err(wm831x->dev, "FLL only supported in AUTO mode\n");

	return 0;
}

static long wm831x_fll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *unused)
{
	int best = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(wm831x_fll_auto_rates); i++)
		if (abs(wm831x_fll_auto_rates[i] - rate) <
		    abs(wm831x_fll_auto_rates[best] - rate))
			best = i;

	return wm831x_fll_auto_rates[best];
}

static int wm831x_fll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int i;

	for (i = 0; i < ARRAY_SIZE(wm831x_fll_auto_rates); i++)
		if (wm831x_fll_auto_rates[i] == rate)
			break;
	if (i == ARRAY_SIZE(wm831x_fll_auto_rates))
		return -EINVAL;

	if (wm831x_fll_is_enabled(hw))
		return -EPERM;

	return wm831x_set_bits(wm831x, WM831X_CLOCK_CONTROL_2,
			       WM831X_FLL_AUTO_FREQ_MASK, i);
}

static const char *wm831x_fll_parents[] = {
	"xtal",
	"clkin",
};

static u8 wm831x_fll_get_parent(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  fll_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	/* AUTO mode is always clocked from the crystal */
	ret = wm831x_reg_read(wm831x, WM831X_CLOCK_CONTROL_2);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read CLOCK_CONTROL_2: %d\n",
			ret);
		return 0;
	}

	if (ret & WM831X_FLL_AUTO)
		return 0;

	ret = wm831x_reg_read(wm831x, WM831X_FLL_CONTROL_5);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read FLL_CONTROL_5: %d\n",
			ret);
		return 0;
	}

	switch (ret & WM831X_FLL_CLK_SRC_MASK) {
	case 0:
		return 0;
	case 1:
		return 1;
	default:
		dev_err(wm831x->dev, "Unsupported FLL clock source %d\n",
			ret & WM831X_FLL_CLK_SRC_MASK);
		return 0;
	}
}

static const struct clk_ops wm831x_fll_ops = {
	.is_enabled = wm831x_fll_is_enabled,
	.prepare = wm831x_fll_prepare,
	.unprepare = wm831x_fll_unprepare,
	.round_rate = wm831x_fll_round_rate,
	.recalc_rate = wm831x_fll_recalc_rate,
	.set_rate = wm831x_fll_set_rate,
	.get_parent = wm831x_fll_get_parent,
};

static struct clk_init_data wm831x_fll_init = {
	.name = "fll",
	.ops = &wm831x_fll_ops,
	.parent_names = wm831x_fll_parents,
	.num_parents = ARRAY_SIZE(wm831x_fll_parents),
	.flags = CLK_SET_RATE_GATE,
};

static int wm831x_clkout_is_enabled(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  clkout_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_CLOCK_CONTROL_1);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read CLOCK_CONTROL_1: %d\n",
			ret);
		return true;
	}

	return (ret & WM831X_CLKOUT_ENA) != 0;
}

static int wm831x_clkout_prepare(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  clkout_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_unlock(wm831x);
	if (ret != 0) {
		dev_crit(wm831x->dev, "Failed to lock registers: %d\n", ret);
		return ret;
	}

	ret = wm831x_set_bits(wm831x, WM831X_CLOCK_CONTROL_1,
			      WM831X_CLKOUT_ENA, WM831X_CLKOUT_ENA);
	if (ret != 0)
		dev_crit(wm831x->dev, "Failed to enable CLKOUT: %d\n", ret);

	wm831x_reg_lock(wm831x);

	return ret;
}

static void wm831x_clkout_unprepare(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  clkout_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_unlock(wm831x);
	if (ret != 0) {
		dev_crit(wm831x->dev, "Failed to lock registers: %d\n", ret);
		return;
	}

	ret = wm831x_set_bits(wm831x, WM831X_CLOCK_CONTROL_1,
			      WM831X_CLKOUT_ENA, 0);
	if (ret != 0)
		dev_crit(wm831x->dev, "Failed to disable CLKOUT: %d\n", ret);

	wm831x_reg_lock(wm831x);
}

static const char *wm831x_clkout_parents[] = {
	"xtal",
	"fll",
};

static u8 wm831x_clkout_get_parent(struct clk_hw *hw)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  clkout_hw);
	struct wm831x *wm831x = clkdata->wm831x;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_CLOCK_CONTROL_1);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read CLOCK_CONTROL_1: %d\n",
			ret);
		return 0;
	}

	if (ret & WM831X_CLKOUT_SRC)
		return 0;
	else
		return 1;
}

static int wm831x_clkout_set_parent(struct clk_hw *hw, u8 parent)
{
	struct wm831x_clk *clkdata = container_of(hw, struct wm831x_clk,
						  clkout_hw);
	struct wm831x *wm831x = clkdata->wm831x;

	return wm831x_set_bits(wm831x, WM831X_CLOCK_CONTROL_1,
			       WM831X_CLKOUT_SRC,
			       parent << WM831X_CLKOUT_SRC_SHIFT);
}

static const struct clk_ops wm831x_clkout_ops = {
	.is_enabled = wm831x_clkout_is_enabled,
	.prepare = wm831x_clkout_prepare,
	.unprepare = wm831x_clkout_unprepare,
	.get_parent = wm831x_clkout_get_parent,
	.set_parent = wm831x_clkout_set_parent,
};

static struct clk_init_data wm831x_clkout_init = {
	.name = "clkout",
	.ops = &wm831x_clkout_ops,
	.parent_names = wm831x_clkout_parents,
	.num_parents = ARRAY_SIZE(wm831x_clkout_parents),
	.flags = CLK_SET_RATE_PARENT,
};

static __devinit int wm831x_clk_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_clk *clkdata;
	int ret;

	clkdata = devm_kzalloc(&pdev->dev, sizeof(*clkdata), GFP_KERNEL);
	if (!clkdata)
		return -ENOMEM;

	/* XTAL_ENA can only be set via OTP/InstantConfig so just read once */
	ret = wm831x_reg_read(wm831x, WM831X_CLOCK_CONTROL_2);
	if (ret < 0) {
		dev_err(wm831x->dev, "Unable to read CLOCK_CONTROL_2: %d\n",
			ret);
		return ret;
	}
	clkdata->xtal_ena = ret & WM831X_XTAL_ENA;

	clkdata->xtal_hw.init = &wm831x_xtal_init;
	clkdata->xtal = clk_register(&pdev->dev, &clkdata->xtal_hw);
	if (!clkdata->xtal)
		return -EINVAL;

	clkdata->fll_hw.init = &wm831x_fll_init;
	clkdata->fll = clk_register(&pdev->dev, &clkdata->fll_hw);
	if (!clkdata->fll) {
		ret = -EINVAL;
		goto err_xtal;
	}

	clkdata->clkout_hw.init = &wm831x_clkout_init;
	clkdata->clkout = clk_register(&pdev->dev, &clkdata->clkout_hw);
	if (!clkdata->clkout) {
		ret = -EINVAL;
		goto err_fll;
	}

	dev_set_drvdata(&pdev->dev, clkdata);

	return 0;

err_fll:
	clk_unregister(clkdata->fll);
err_xtal:
	clk_unregister(clkdata->xtal);
	return ret;
}

static int __devexit wm831x_clk_remove(struct platform_device *pdev)
{
	struct wm831x_clk *clkdata = dev_get_drvdata(&pdev->dev);

	clk_unregister(clkdata->clkout);
	clk_unregister(clkdata->fll);
	clk_unregister(clkdata->xtal);

	return 0;
}

static struct platform_driver wm831x_clk_driver = {
	.probe = wm831x_clk_probe,
	.remove = __devexit_p(wm831x_clk_remove),
	.driver		= {
		.name	= "wm831x-clk",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(wm831x_clk_driver);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM831x clock driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-clk");
