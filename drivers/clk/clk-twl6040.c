/*
* TWL6040 clock module driver for OMAP4 McPDM functional clock
*
* Copyright (C) 2012 Texas Instruments Inc.
* Peter Ujfalusi <peter.ujfalusi@ti.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
* 02110-1301 USA
*
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/twl6040.h>
#include <linux/clk-provider.h>

struct twl6040_clk {
	struct twl6040 *twl6040;
	struct device *dev;
	struct clk_hw mcpdm_fclk;
	struct clk *clk;
	int enabled;
};

static int twl6040_bitclk_is_enabled(struct clk_hw *hw)
{
	struct twl6040_clk *twl6040_clk = container_of(hw, struct twl6040_clk,
						       mcpdm_fclk);
	return twl6040_clk->enabled;
}

static int twl6040_bitclk_prepare(struct clk_hw *hw)
{
	struct twl6040_clk *twl6040_clk = container_of(hw, struct twl6040_clk,
						       mcpdm_fclk);
	int ret;

	ret = twl6040_power(twl6040_clk->twl6040, 1);
	if (!ret)
		twl6040_clk->enabled = 1;

	return ret;
}

static void twl6040_bitclk_unprepare(struct clk_hw *hw)
{
	struct twl6040_clk *twl6040_clk = container_of(hw, struct twl6040_clk,
						       mcpdm_fclk);
	int ret;

	ret = twl6040_power(twl6040_clk->twl6040, 0);
	if (!ret)
		twl6040_clk->enabled = 0;
}

static const struct clk_ops twl6040_mcpdm_ops = {
	.is_enabled = twl6040_bitclk_is_enabled,
	.prepare = twl6040_bitclk_prepare,
	.unprepare = twl6040_bitclk_unprepare,
};

static struct clk_init_data wm831x_clkout_init = {
	.name = "mcpdm_fclk",
	.ops = &twl6040_mcpdm_ops,
};

static int twl6040_clk_probe(struct platform_device *pdev)
{
	struct twl6040 *twl6040 = dev_get_drvdata(pdev->dev.parent);
	struct twl6040_clk *clkdata;

	clkdata = devm_kzalloc(&pdev->dev, sizeof(*clkdata), GFP_KERNEL);
	if (!clkdata)
		return -ENOMEM;

	clkdata->dev = &pdev->dev;
	clkdata->twl6040 = twl6040;

	clkdata->mcpdm_fclk.init = &wm831x_clkout_init;
	clkdata->clk = devm_clk_register(&pdev->dev, &clkdata->mcpdm_fclk);
	if (IS_ERR(clkdata->clk))
		return PTR_ERR(clkdata->clk);

	platform_set_drvdata(pdev, clkdata);

	return 0;
}

static struct platform_driver twl6040_clk_driver = {
	.driver = {
		.name = "twl6040-clk",
	},
	.probe = twl6040_clk_probe,
};

module_platform_driver(twl6040_clk_driver);

MODULE_DESCRIPTION("TWL6040 clock driver for McPDM functional clock");
MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_ALIAS("platform:twl6040-clk");
MODULE_LICENSE("GPL");
