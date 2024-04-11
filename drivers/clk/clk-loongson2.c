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

static const struct clk_parent_data pdata[] = {
	{ .fw_name = "ref_100m", },
};

enum loongson2_clk_type {
	CLK_TYPE_PLL,
	CLK_TYPE_SCALE,
	CLK_TYPE_DIVIDER,
	CLK_TYPE_NONE,
};

struct loongson2_clk_provider {
	void __iomem *base;
	struct device *dev;
	struct clk_hw_onecell_data clk_data;
	spinlock_t clk_lock;	/* protect access to DIV registers */
};

struct loongson2_clk_data {
	struct clk_hw hw;
	void __iomem *reg;
	u8 div_shift;
	u8 div_width;
	u8 mult_shift;
	u8 mult_width;
};

struct loongson2_clk_board_info {
	u8 id;
	enum loongson2_clk_type type;
	const char *name;
	const char *parent_name;
	u8 reg_offset;
	u8 div_shift;
	u8 div_width;
	u8 mult_shift;
	u8 mult_width;
};

#define CLK_DIV(_id, _name, _pname, _offset, _dshift, _dwidth)	\
	{							\
		.id		= _id,				\
		.type		= CLK_TYPE_DIVIDER,		\
		.name		= _name,			\
		.parent_name	= _pname,			\
		.reg_offset	= _offset,			\
		.div_shift	= _dshift,			\
		.div_width	= _dwidth,			\
	}

#define CLK_PLL(_id, _name, _offset, _mshift, _mwidth,		\
		_dshift, _dwidth)				\
	{							\
		.id		= _id,				\
		.type		= CLK_TYPE_PLL,			\
		.name		= _name,			\
		.parent_name	= NULL,				\
		.reg_offset	= _offset,			\
		.mult_shift	= _mshift,			\
		.mult_width	= _mwidth,			\
		.div_shift	= _dshift,			\
		.div_width	= _dwidth,			\
	}

#define CLK_SCALE(_id, _name, _pname, _offset,			\
		  _dshift, _dwidth)				\
	{							\
		.id		= _id,				\
		.type		= CLK_TYPE_SCALE,		\
		.name		= _name,			\
		.parent_name	= _pname,			\
		.reg_offset	= _offset,			\
		.div_shift	= _dshift,			\
		.div_width	= _dwidth,			\
	}

static const struct loongson2_clk_board_info ls2k1000_clks[] = {
	CLK_PLL(LOONGSON2_NODE_PLL,   "pll_node", 0,    32, 10, 26, 6),
	CLK_PLL(LOONGSON2_DDR_PLL,    "pll_ddr",  0x10, 32, 10, 26, 6),
	CLK_PLL(LOONGSON2_DC_PLL,     "pll_dc",   0x20, 32, 10, 26, 6),
	CLK_PLL(LOONGSON2_PIX0_PLL,   "pll_pix0", 0x30, 32, 10, 26, 6),
	CLK_PLL(LOONGSON2_PIX1_PLL,   "pll_pix1", 0x40, 32, 10, 26, 6),
	CLK_DIV(LOONGSON2_NODE_CLK,   "clk_node", "pll_node", 0x8,  0,  6),
	CLK_DIV(LOONGSON2_DDR_CLK,    "clk_ddr",  "pll_ddr",  0x18, 0,  6),
	CLK_DIV(LOONGSON2_GPU_CLK,    "clk_gpu",  "pll_ddr",  0x18, 22, 6),
	/*
	 * The hda clk divisor in the upper 32bits and the clk-prodiver
	 * layer code doesn't support 64bit io operation thus a conversion
	 * is required that subtract shift by 32 and add 4byte to the hda
	 * address
	 */
	CLK_DIV(LOONGSON2_HDA_CLK,    "clk_hda",  "pll_ddr",  0x22, 12, 7),
	CLK_DIV(LOONGSON2_DC_CLK,     "clk_dc",   "pll_dc",   0x28, 0,  6),
	CLK_DIV(LOONGSON2_GMAC_CLK,   "clk_gmac", "pll_dc",   0x28, 22, 6),
	CLK_DIV(LOONGSON2_PIX0_CLK,   "clk_pix0", "pll_pix0", 0x38, 0,  6),
	CLK_DIV(LOONGSON2_PIX1_CLK,   "clk_pix1", "pll_pix1", 0x38, 0,  6),
	CLK_SCALE(LOONGSON2_BOOT_CLK, "clk_boot", NULL,       0x50, 8,  3),
	CLK_SCALE(LOONGSON2_SATA_CLK, "clk_sata", "clk_gmac", 0x50, 12, 3),
	CLK_SCALE(LOONGSON2_USB_CLK,  "clk_usb",  "clk_gmac", 0x50, 16, 3),
	CLK_SCALE(LOONGSON2_APB_CLK,  "clk_apb",  "clk_gmac", 0x50, 20, 3),
	{ /* Sentinel */ },
};

static inline struct loongson2_clk_data *to_loongson2_clk(struct clk_hw *hw)
{
	return container_of(hw, struct loongson2_clk_data, hw);
}

static inline unsigned long loongson2_rate_part(u64 val, u8 shift, u8 width)
{
	return (val & GENMASK(shift + width - 1, shift)) >> shift;
}

static unsigned long loongson2_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	u64 val, mult, div;
	struct loongson2_clk_data *clk = to_loongson2_clk(hw);

	val  = readq(clk->reg);
	mult = loongson2_rate_part(val, clk->mult_shift, clk->mult_width);
	div  = loongson2_rate_part(val, clk->div_shift,  clk->div_width);

	return div_u64((u64)parent_rate * mult, div);
}

static const struct clk_ops loongson2_pll_recalc_ops = {
	.recalc_rate = loongson2_pll_recalc_rate,
};

static unsigned long loongson2_freqscale_recalc_rate(struct clk_hw *hw,
						     unsigned long parent_rate)
{
	u64 val, mult;
	struct loongson2_clk_data *clk = to_loongson2_clk(hw);

	val  = readq(clk->reg);
	mult = loongson2_rate_part(val, clk->div_shift, clk->div_width) + 1;

	return div_u64((u64)parent_rate * mult, 8);
}

static const struct clk_ops loongson2_freqscale_recalc_ops = {
	.recalc_rate = loongson2_freqscale_recalc_rate,
};

static struct clk_hw *loongson2_clk_register(struct loongson2_clk_provider *clp,
					     const struct loongson2_clk_board_info *cld,
					     const struct clk_ops *ops)
{
	int ret;
	struct clk_hw *hw;
	struct loongson2_clk_data *clk;
	struct clk_init_data init = { };

	clk = devm_kzalloc(clp->dev, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	init.name  = cld->name;
	init.ops   = ops;
	init.flags = 0;
	init.num_parents = 1;

	if (!cld->parent_name)
		init.parent_data = pdata;
	else
		init.parent_names = &cld->parent_name;

	clk->reg	= clp->base + cld->reg_offset;
	clk->div_shift	= cld->div_shift;
	clk->div_width	= cld->div_width;
	clk->mult_shift	= cld->mult_shift;
	clk->mult_width	= cld->mult_width;
	clk->hw.init	= &init;

	hw = &clk->hw;
	ret = devm_clk_hw_register(clp->dev, hw);
	if (ret)
		clk = ERR_PTR(ret);

	return hw;
}

static int loongson2_clk_probe(struct platform_device *pdev)
{
	int i, clks_num = 0;
	struct clk_hw *hw;
	struct device *dev = &pdev->dev;
	struct loongson2_clk_provider *clp;
	const struct loongson2_clk_board_info *p, *data;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	for (p = data; p->name; p++)
		clks_num++;

	clp = devm_kzalloc(dev, struct_size(clp, clk_data.hws, clks_num),
			   GFP_KERNEL);
	if (!clp)
		return -ENOMEM;

	clp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clp->base))
		return PTR_ERR(clp->base);

	spin_lock_init(&clp->clk_lock);
	clp->clk_data.num = clks_num + 1;
	clp->dev = dev;

	for (i = 0; i < clks_num; i++) {
		p = &data[i];
		switch (p->type) {
		case CLK_TYPE_PLL:
			hw = loongson2_clk_register(clp, p,
						    &loongson2_pll_recalc_ops);
			break;
		case CLK_TYPE_SCALE:
			hw = loongson2_clk_register(clp, p,
						    &loongson2_freqscale_recalc_ops);
			break;
		case CLK_TYPE_DIVIDER:
			hw = devm_clk_hw_register_divider(dev, p->name,
							  p->parent_name, 0,
							  clp->base + p->reg_offset,
							  p->div_shift, p->div_width,
							  CLK_DIVIDER_ONE_BASED,
							  &clp->clk_lock);
			break;
		default:
			return dev_err_probe(dev, -EINVAL, "Invalid clk type\n");
		}

		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw),
					     "Register clk: %s, type: %u failed!\n",
					     p->name, p->type);

		clp->clk_data.hws[p->id] = hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, &clp->clk_data);
}

static const struct of_device_id loongson2_clk_match_table[] = {
	{ .compatible = "loongson,ls2k-clk", .data = &ls2k1000_clks },
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
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
