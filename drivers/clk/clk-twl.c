// SPDX-License-Identifier: GPL-2.0
/*
 * Clock driver for twl device.
 *
 * inspired by the driver for the Palmas device
 */

#include <linux/clk-provider.h>
#include <linux/mfd/twl.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define VREG_STATE              2
#define TWL6030_CFG_STATE_OFF   0x00
#define TWL6030_CFG_STATE_ON    0x01
#define TWL6030_CFG_STATE_MASK  0x03

struct twl_clock_info {
	struct device *dev;
	u8 base;
	struct clk_hw hw;
};

static inline int
twlclk_read(struct twl_clock_info *info, unsigned int slave_subgp,
	    unsigned int offset)
{
	u8 value;
	int status;

	status = twl_i2c_read_u8(slave_subgp, &value,
				 info->base + offset);
	return (status < 0) ? status : value;
}

static inline int
twlclk_write(struct twl_clock_info *info, unsigned int slave_subgp,
	     unsigned int offset, u8 value)
{
	return twl_i2c_write_u8(slave_subgp, value,
				info->base + offset);
}

static inline struct twl_clock_info *to_twl_clks_info(struct clk_hw *hw)
{
	return container_of(hw, struct twl_clock_info, hw);
}

static unsigned long twl_clks_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return 32768;
}

static int twl6032_clks_prepare(struct clk_hw *hw)
{
	struct twl_clock_info *cinfo = to_twl_clks_info(hw);
	int ret;

	ret = twlclk_write(cinfo, TWL_MODULE_PM_RECEIVER, VREG_STATE,
			   TWL6030_CFG_STATE_ON);
	if (ret < 0)
		dev_err(cinfo->dev, "clk prepare failed\n");

	return ret;
}

static void twl6032_clks_unprepare(struct clk_hw *hw)
{
	struct twl_clock_info *cinfo = to_twl_clks_info(hw);
	int ret;

	ret = twlclk_write(cinfo, TWL_MODULE_PM_RECEIVER, VREG_STATE,
			   TWL6030_CFG_STATE_OFF);
	if (ret < 0)
		dev_err(cinfo->dev, "clk unprepare failed\n");
}

static int twl6032_clks_is_prepared(struct clk_hw *hw)
{
	struct twl_clock_info *cinfo = to_twl_clks_info(hw);
	int val;

	val = twlclk_read(cinfo, TWL_MODULE_PM_RECEIVER, VREG_STATE);
	if (val < 0) {
		dev_err(cinfo->dev, "clk read failed\n");
		return val;
	}

	val &= TWL6030_CFG_STATE_MASK;

	return val == TWL6030_CFG_STATE_ON;
}

static const struct clk_ops twl6032_clks_ops = {
	.prepare	= twl6032_clks_prepare,
	.unprepare	= twl6032_clks_unprepare,
	.is_prepared	= twl6032_clks_is_prepared,
	.recalc_rate	= twl_clks_recalc_rate,
};

struct twl_clks_data {
	struct clk_init_data init;
	u8 base;
};

static const struct twl_clks_data twl6032_clks[] = {
	{
		.init = {
			.name = "clk32kg",
			.ops = &twl6032_clks_ops,
			.flags = CLK_IGNORE_UNUSED,
		},
		.base = 0x8C,
	},
	{
		.init = {
			.name = "clk32kaudio",
			.ops = &twl6032_clks_ops,
			.flags = CLK_IGNORE_UNUSED,
		},
		.base = 0x8F,
	},
	{
		/* sentinel */
	}
};

static int twl_clks_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	const struct twl_clks_data *hw_data;

	struct twl_clock_info *cinfo;
	int ret;
	int i;
	int count;

	hw_data = twl6032_clks;
	for (count = 0; hw_data[count].init.name; count++)
		;

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, hws, count),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = count;
	cinfo = devm_kcalloc(&pdev->dev, count, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		cinfo[i].base = hw_data[i].base;
		cinfo[i].dev = &pdev->dev;
		cinfo[i].hw.init = &hw_data[i].init;
		ret = devm_clk_hw_register(&pdev->dev, &cinfo[i].hw);
		if (ret) {
			return dev_err_probe(&pdev->dev, ret,
					     "Fail to register clock %s\n",
					     hw_data[i].init.name);
		}
		clk_data->hws[i] = &cinfo[i].hw;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev,
					  of_clk_hw_onecell_get, clk_data);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "Fail to add clock driver\n");

	return 0;
}

static const struct platform_device_id twl_clks_id[] = {
	{
		.name = "twl6032-clk",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, twl_clks_id);

static struct platform_driver twl_clks_driver = {
	.driver = {
		.name = "twl-clk",
	},
	.probe = twl_clks_probe,
	.id_table = twl_clks_id,
};

module_platform_driver(twl_clks_driver);

MODULE_DESCRIPTION("Clock driver for TWL Series Devices");
MODULE_LICENSE("GPL");
