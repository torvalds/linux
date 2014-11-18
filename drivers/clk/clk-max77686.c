/*
 * clk-max77686.c - Clock driver for Maxim 77686
 *
 * Copyright (C) 2012 Samsung Electornics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/clkdev.h>

#include <dt-bindings/clock/maxim,max77686.h>
#include "clk-max-gen.h"

static struct clk_init_data max77686_clks_init[MAX77686_CLKS_NUM] = {
	[MAX77686_CLK_AP] = {
		.name = "32khz_ap",
		.ops = &max_gen_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[MAX77686_CLK_CP] = {
		.name = "32khz_cp",
		.ops = &max_gen_clk_ops,
		.flags = CLK_IS_ROOT,
	},
	[MAX77686_CLK_PMIC] = {
		.name = "32khz_pmic",
		.ops = &max_gen_clk_ops,
		.flags = CLK_IS_ROOT,
	},
};

static int max77686_clk_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);

	return max_gen_clk_probe(pdev, iodev->regmap, MAX77686_REG_32KHZ,
				 max77686_clks_init, MAX77686_CLKS_NUM);
}

static int max77686_clk_remove(struct platform_device *pdev)
{
	return max_gen_clk_remove(pdev, MAX77686_CLKS_NUM);
}

static const struct platform_device_id max77686_clk_id[] = {
	{ "max77686-clk", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77686_clk_id);

static struct platform_driver max77686_clk_driver = {
	.driver = {
		.name  = "max77686-clk",
	},
	.probe = max77686_clk_probe,
	.remove = max77686_clk_remove,
	.id_table = max77686_clk_id,
};

module_platform_driver(max77686_clk_driver);

MODULE_DESCRIPTION("MAXIM 77686 Clock Driver");
MODULE_AUTHOR("Jonghwa Lee <jonghwa3.lee@samsung.com>");
MODULE_LICENSE("GPL");
