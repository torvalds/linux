// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Yinbo Zhu <zhuyinbo@loongson.cn>
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <dt-bindings/clock/loongson,ls2k-clk.h>

#define LOONGSON2_PLL_MULT_SHIFT		32
#define LOONGSON2_PLL_MULT_WIDTH		10
#define LOONGSON2_PLL_DIV_SHIFT			26
#define LOONGSON2_PLL_DIV_WIDTH			6
#define LOONGSON2_APB_FREQSCALE_SHIFT		20
#define LOONGSON2_APB_FREQSCALE_WIDTH		3
#define LOONGSON2_USB_FREQSCALE_SHIFT		16
#define LOONGSON2_USB_FREQSCALE_WIDTH		3
#define LOONGSON2_SATA_FREQSCALE_SHIFT		12
#define LOONGSON2_SATA_FREQSCALE_WIDTH		3
#define LOONGSON2_BOOT_FREQSCALE_SHIFT		8
#define LOONGSON2_BOOT_FREQSCALE_WIDTH		3

static void __iomem *loongson2_pll_base;

static const struct clk_parent_data pdata[] = {
	{ .fw_name = "ref_100m",},
};

static struct clk_hw *loongson2_clk_register(struct device *dev,
					  const char *name,
					  const char *parent_name,
					  const struct clk_ops *ops,
					  unsigned long flags)
{
	int ret;
	struct clk_hw *hw;
	struct clk_init_data init = { };

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = ops;
	init.flags = flags;
	init.num_parents = 1;

	if (!parent_name)
		init.parent_data = pdata;
	else
		init.parent_names = &parent_name;

	hw->init = &init;

	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		hw = ERR_PTR(ret);

	return hw;
}

static unsigned long loongson2_calc_pll_rate(int offset, unsigned long rate)
{
	u64 val;
	u32 mult, div;

	val = readq(loongson2_pll_base + offset);

	mult = (val >> LOONGSON2_PLL_MULT_SHIFT) &
			clk_div_mask(LOONGSON2_PLL_MULT_WIDTH);
	div = (val >> LOONGSON2_PLL_DIV_SHIFT) &
			clk_div_mask(LOONGSON2_PLL_DIV_WIDTH);

	return div_u64((u64)rate * mult, div);
}

static unsigned long loongson2_node_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_pll_rate(0x0, parent_rate);
}

static const struct clk_ops loongson2_node_clk_ops = {
	.recalc_rate = loongson2_node_recalc_rate,
};

static unsigned long loongson2_ddr_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_pll_rate(0x10, parent_rate);
}

static const struct clk_ops loongson2_ddr_clk_ops = {
	.recalc_rate = loongson2_ddr_recalc_rate,
};

static unsigned long loongson2_dc_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_pll_rate(0x20, parent_rate);
}

static const struct clk_ops loongson2_dc_clk_ops = {
	.recalc_rate = loongson2_dc_recalc_rate,
};

static unsigned long loongson2_pix0_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_pll_rate(0x30, parent_rate);
}

static const struct clk_ops loongson2_pix0_clk_ops = {
	.recalc_rate = loongson2_pix0_recalc_rate,
};

static unsigned long loongson2_pix1_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_pll_rate(0x40, parent_rate);
}

static const struct clk_ops loongson2_pix1_clk_ops = {
	.recalc_rate = loongson2_pix1_recalc_rate,
};

static unsigned long loongson2_calc_rate(unsigned long rate,
					 int shift, int width)
{
	u64 val;
	u32 mult;

	val = readq(loongson2_pll_base + 0x50);

	mult = (val >> shift) & clk_div_mask(width);

	return div_u64((u64)rate * (mult + 1), 8);
}

static unsigned long loongson2_boot_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_rate(parent_rate,
				   LOONGSON2_BOOT_FREQSCALE_SHIFT,
				   LOONGSON2_BOOT_FREQSCALE_WIDTH);
}

static const struct clk_ops loongson2_boot_clk_ops = {
	.recalc_rate = loongson2_boot_recalc_rate,
};

static unsigned long loongson2_apb_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_rate(parent_rate,
				   LOONGSON2_APB_FREQSCALE_SHIFT,
				   LOONGSON2_APB_FREQSCALE_WIDTH);
}

static const struct clk_ops loongson2_apb_clk_ops = {
	.recalc_rate = loongson2_apb_recalc_rate,
};

static unsigned long loongson2_usb_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_rate(parent_rate,
				   LOONGSON2_USB_FREQSCALE_SHIFT,
				   LOONGSON2_USB_FREQSCALE_WIDTH);
}

static const struct clk_ops loongson2_usb_clk_ops = {
	.recalc_rate = loongson2_usb_recalc_rate,
};

static unsigned long loongson2_sata_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	return loongson2_calc_rate(parent_rate,
				   LOONGSON2_SATA_FREQSCALE_SHIFT,
				   LOONGSON2_SATA_FREQSCALE_WIDTH);
}

static const struct clk_ops loongson2_sata_clk_ops = {
	.recalc_rate = loongson2_sata_recalc_rate,
};

static inline int loongson2_check_clk_hws(struct clk_hw *clks[], unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (IS_ERR(clks[i])) {
			pr_err("Loongson2 clk %u: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
			return PTR_ERR(clks[i]);
		}

	return 0;
}

static int loongson2_clk_probe(struct platform_device *pdev)
{
	int ret;
	struct clk_hw **hws;
	struct clk_hw_onecell_data *clk_hw_data;
	spinlock_t loongson2_clk_lock;
	struct device *dev = &pdev->dev;

	loongson2_pll_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(loongson2_pll_base))
		return PTR_ERR(loongson2_pll_base);

	clk_hw_data = devm_kzalloc(dev, struct_size(clk_hw_data, hws, LOONGSON2_CLK_END),
					GFP_KERNEL);
	if (WARN_ON(!clk_hw_data))
		return -ENOMEM;

	clk_hw_data->num = LOONGSON2_CLK_END;
	hws = clk_hw_data->hws;

	hws[LOONGSON2_NODE_PLL] = loongson2_clk_register(dev, "node_pll",
						NULL,
						&loongson2_node_clk_ops, 0);

	hws[LOONGSON2_DDR_PLL] = loongson2_clk_register(dev, "ddr_pll",
						NULL,
						&loongson2_ddr_clk_ops, 0);

	hws[LOONGSON2_DC_PLL] = loongson2_clk_register(dev, "dc_pll",
						NULL,
						&loongson2_dc_clk_ops, 0);

	hws[LOONGSON2_PIX0_PLL] = loongson2_clk_register(dev, "pix0_pll",
						NULL,
						&loongson2_pix0_clk_ops, 0);

	hws[LOONGSON2_PIX1_PLL] = loongson2_clk_register(dev, "pix1_pll",
						NULL,
						&loongson2_pix1_clk_ops, 0);

	hws[LOONGSON2_BOOT_CLK] = loongson2_clk_register(dev, "boot",
						NULL,
						&loongson2_boot_clk_ops, 0);

	hws[LOONGSON2_NODE_CLK] = devm_clk_hw_register_divider(dev, "node",
						"node_pll", 0,
						loongson2_pll_base + 0x8, 0,
						6, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	/*
	 * The hda clk divisor in the upper 32bits and the clk-prodiver
	 * layer code doesn't support 64bit io operation thus a conversion
	 * is required that subtract shift by 32 and add 4byte to the hda
	 * address
	 */
	hws[LOONGSON2_HDA_CLK] = devm_clk_hw_register_divider(dev, "hda",
						"ddr_pll", 0,
						loongson2_pll_base + 0x22, 12,
						7, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_GPU_CLK] = devm_clk_hw_register_divider(dev, "gpu",
						"ddr_pll", 0,
						loongson2_pll_base + 0x18, 22,
						6, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_DDR_CLK] = devm_clk_hw_register_divider(dev, "ddr",
						"ddr_pll", 0,
						loongson2_pll_base + 0x18, 0,
						6, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_GMAC_CLK] = devm_clk_hw_register_divider(dev, "gmac",
						"dc_pll", 0,
						loongson2_pll_base + 0x28, 22,
						6, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_DC_CLK] = devm_clk_hw_register_divider(dev, "dc",
						"dc_pll", 0,
						loongson2_pll_base + 0x28, 0,
						6, CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_APB_CLK] = loongson2_clk_register(dev, "apb",
						"gmac",
						&loongson2_apb_clk_ops, 0);

	hws[LOONGSON2_USB_CLK] = loongson2_clk_register(dev, "usb",
						"gmac",
						&loongson2_usb_clk_ops, 0);

	hws[LOONGSON2_SATA_CLK] = loongson2_clk_register(dev, "sata",
						"gmac",
						&loongson2_sata_clk_ops, 0);

	hws[LOONGSON2_PIX0_CLK] = clk_hw_register_divider(NULL, "pix0",
						"pix0_pll", 0,
						loongson2_pll_base + 0x38, 0, 6,
						CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	hws[LOONGSON2_PIX1_CLK] = clk_hw_register_divider(NULL, "pix1",
						"pix1_pll", 0,
						loongson2_pll_base + 0x48, 0, 6,
						CLK_DIVIDER_ONE_BASED,
						&loongson2_clk_lock);

	ret = loongson2_check_clk_hws(hws, LOONGSON2_CLK_END);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_hw_data);
}

static const struct of_device_id loongson2_clk_match_table[] = {
	{ .compatible = "loongson,ls2k-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, loongson2_clk_match_table);

static struct platform_driver loongson2_clk_driver = {
	.probe	= loongson2_clk_probe,
	.driver	= {
		.name	= "loongson2-clk",
		.of_match_table	= loongson2_clk_match_table,
	},
};
module_platform_driver(loongson2_clk_driver);

MODULE_DESCRIPTION("Loongson2 clock driver");
MODULE_LICENSE("GPL");
