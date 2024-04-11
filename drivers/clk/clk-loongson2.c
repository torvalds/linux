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
	CLK_TYPE_GATE,
	CLK_TYPE_FIXED,
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
	unsigned long fixed_rate;
	u8 reg_offset;
	u8 div_shift;
	u8 div_width;
	u8 mult_shift;
	u8 mult_width;
	u8 bit_idx;
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

#define CLK_GATE(_id, _name, _pname, _offset, _bidx)		\
	{							\
		.id		= _id,				\
		.type		= CLK_TYPE_GATE,		\
		.name		= _name,			\
		.parent_name	= _pname,			\
		.reg_offset	= _offset,			\
		.bit_idx	= _bidx,			\
	}

#define CLK_FIXED(_id, _name, _pname, _rate)			\
	{							\
		.id		= _id,				\
		.type		= CLK_TYPE_FIXED,		\
		.name		= _name,			\
		.parent_name	= _pname,			\
		.fixed_rate	= _rate,			\
	}

static const struct loongson2_clk_board_info ls2k0500_clks[] = {
	CLK_PLL(LOONGSON2_NODE_PLL,   "pll_node", 0,    16, 8, 8, 6),
	CLK_PLL(LOONGSON2_DDR_PLL,    "pll_ddr",  0x8,  16, 8, 8, 6),
	CLK_PLL(LOONGSON2_DC_PLL,     "pll_soc",  0x10, 16, 8, 8, 6),
	CLK_PLL(LOONGSON2_PIX0_PLL,   "pll_pix0", 0x18, 16, 8, 8, 6),
	CLK_PLL(LOONGSON2_PIX1_PLL,   "pll_pix1", 0x20, 16, 8, 8, 6),
	CLK_DIV(LOONGSON2_NODE_CLK,   "clk_node", "pll_node", 0,    24, 6),
	CLK_DIV(LOONGSON2_DDR_CLK,    "clk_ddr",  "pll_ddr",  0x8,  24, 6),
	CLK_DIV(LOONGSON2_HDA_CLK,    "clk_hda",  "pll_ddr",  0xc,  8,  6),
	CLK_DIV(LOONGSON2_GPU_CLK,    "clk_gpu",  "pll_soc",  0x10, 24, 6),
	CLK_DIV(LOONGSON2_DC_CLK,     "clk_sb",   "pll_soc",  0x14, 0,  6),
	CLK_DIV(LOONGSON2_GMAC_CLK,   "clk_gmac", "pll_soc",  0x14, 8,  6),
	CLK_DIV(LOONGSON2_PIX0_CLK,   "clk_pix0", "pll_pix0", 0x18, 24, 6),
	CLK_DIV(LOONGSON2_PIX1_CLK,   "clk_pix1", "pll_pix1", 0x20, 24, 6),
	CLK_SCALE(LOONGSON2_BOOT_CLK, "clk_boot", "clk_sb",   0x28, 8,  3),
	CLK_SCALE(LOONGSON2_SATA_CLK, "clk_sata", "clk_sb",   0x28, 12, 3),
	CLK_SCALE(LOONGSON2_USB_CLK,  "clk_usb",  "clk_sb",   0x28, 16, 3),
	CLK_SCALE(LOONGSON2_APB_CLK,  "clk_apb",  "clk_sb",   0x28, 20, 3),
	{ /* Sentinel */ },
};

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

static const struct loongson2_clk_board_info ls2k2000_clks[] = {
	CLK_PLL(LOONGSON2_DC_PLL,     "pll_0",    0,    21, 9, 32, 6),
	CLK_PLL(LOONGSON2_DDR_PLL,    "pll_1",    0x10, 21, 9, 32, 6),
	CLK_PLL(LOONGSON2_NODE_PLL,   "pll_2",    0x20, 21, 9, 32, 6),
	CLK_PLL(LOONGSON2_PIX0_PLL,   "pll_pix0", 0x30, 21, 9, 32, 6),
	CLK_PLL(LOONGSON2_PIX1_PLL,   "pll_pix1", 0x40, 21, 9, 32, 6),
	CLK_GATE(LOONGSON2_OUT0_GATE, "out0_gate", "pll_0",    0,    40),
	CLK_GATE(LOONGSON2_GMAC_GATE, "gmac_gate", "pll_0",    0,    41),
	CLK_GATE(LOONGSON2_RIO_GATE,  "rio_gate",  "pll_0",    0,    42),
	CLK_GATE(LOONGSON2_DC_GATE,   "dc_gate",   "pll_1",    0x10, 40),
	CLK_GATE(LOONGSON2_DDR_GATE,  "ddr_gate",  "pll_1",    0x10, 41),
	CLK_GATE(LOONGSON2_GPU_GATE,  "gpu_gate",  "pll_1",    0x10, 42),
	CLK_GATE(LOONGSON2_HDA_GATE,  "hda_gate",  "pll_2",    0x20, 40),
	CLK_GATE(LOONGSON2_NODE_GATE, "node_gate", "pll_2",    0x20, 41),
	CLK_GATE(LOONGSON2_EMMC_GATE, "emmc_gate", "pll_2",    0x20, 42),
	CLK_GATE(LOONGSON2_PIX0_GATE, "pix0_gate", "pll_pix0", 0x30, 40),
	CLK_GATE(LOONGSON2_PIX1_GATE, "pix1_gate", "pll_pix1", 0x40, 40),
	CLK_DIV(LOONGSON2_OUT0_CLK,   "clk_out0", "out0_gate", 0,    0,  6),
	CLK_DIV(LOONGSON2_GMAC_CLK,   "clk_gmac", "gmac_gate", 0,    7,  6),
	CLK_DIV(LOONGSON2_RIO_CLK,    "clk_rio",  "rio_gate",  0,    14, 6),
	CLK_DIV(LOONGSON2_DC_CLK,     "clk_dc",   "dc_gate",   0x10, 0,  6),
	CLK_DIV(LOONGSON2_GPU_CLK,    "clk_gpu",  "gpu_gate",  0x10, 7,  6),
	CLK_DIV(LOONGSON2_DDR_CLK,    "clk_ddr",  "ddr_gate",  0x10, 14, 6),
	CLK_DIV(LOONGSON2_HDA_CLK,    "clk_hda",  "hda_gate",  0x20, 0,  6),
	CLK_DIV(LOONGSON2_NODE_CLK,   "clk_node", "node_gate", 0x20, 7,  6),
	CLK_DIV(LOONGSON2_EMMC_CLK,   "clk_emmc", "emmc_gate", 0x20, 14, 6),
	CLK_DIV(LOONGSON2_PIX0_CLK,   "clk_pix0", "pll_pix0",  0x30, 0,  6),
	CLK_DIV(LOONGSON2_PIX1_CLK,   "clk_pix1", "pll_pix1",  0x40, 0,  6),
	CLK_SCALE(LOONGSON2_SATA_CLK, "clk_sata", "clk_out0",  0x50, 12, 3),
	CLK_SCALE(LOONGSON2_USB_CLK,  "clk_usb",  "clk_out0",  0x50, 16, 3),
	CLK_SCALE(LOONGSON2_APB_CLK,  "clk_apb",  "clk_node",  0x50, 20, 3),
	CLK_SCALE(LOONGSON2_BOOT_CLK, "clk_boot", NULL,        0x50, 23, 3),
	CLK_SCALE(LOONGSON2_DES_CLK,  "clk_des",  "clk_node",  0x50, 40, 3),
	CLK_SCALE(LOONGSON2_I2S_CLK,  "clk_i2s",  "clk_node",  0x50, 44, 3),
	CLK_FIXED(LOONGSON2_MISC_CLK, "clk_misc", NULL, 50000000),
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
		case CLK_TYPE_GATE:
			hw = devm_clk_hw_register_gate(dev, p->name, p->parent_name, 0,
						       clp->base + p->reg_offset,
						       p->bit_idx, 0,
						       &clp->clk_lock);
			break;
		case CLK_TYPE_FIXED:
			hw = clk_hw_register_fixed_rate_parent_data(dev, p->name, pdata,
								    0, p->fixed_rate);
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
	{ .compatible = "loongson,ls2k0500-clk", .data = &ls2k0500_clks },
	{ .compatible = "loongson,ls2k-clk", .data = &ls2k1000_clks },
	{ .compatible = "loongson,ls2k2000-clk", .data = &ls2k2000_clks },
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
