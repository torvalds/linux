// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Conor Dooley <conor.dooley@microchip.com>
 *
 * Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries
 */
#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/microchip,mpfs-clock.h>

/* address offset of control registers */
#define MPFS_CCC_PLL_CR			0x04u
#define MPFS_CCC_REF_CR			0x08u
#define MPFS_CCC_SSCG_2_CR		0x2Cu
#define MPFS_CCC_POSTDIV01_CR		0x10u
#define MPFS_CCC_POSTDIV23_CR		0x14u

#define MPFS_CCC_FBDIV_SHIFT		0x00u
#define MPFS_CCC_FBDIV_WIDTH		0x0Cu
#define MPFS_CCC_POSTDIV0_SHIFT		0x08u
#define MPFS_CCC_POSTDIV1_SHIFT		0x18u
#define MPFS_CCC_POSTDIV2_SHIFT		MPFS_CCC_POSTDIV0_SHIFT
#define MPFS_CCC_POSTDIV3_SHIFT		MPFS_CCC_POSTDIV1_SHIFT
#define MPFS_CCC_POSTDIV_WIDTH		0x06u
#define MPFS_CCC_REFCLK_SEL		BIT(6)
#define MPFS_CCC_REFDIV_SHIFT		0x08u
#define MPFS_CCC_REFDIV_WIDTH		0x06u

#define MPFS_CCC_FIXED_DIV		4
#define MPFS_CCC_OUTPUTS_PER_PLL	4
#define MPFS_CCC_REFS_PER_PLL		2

struct mpfs_ccc_data {
	void __iomem **pll_base;
	struct device *dev;
	struct clk_hw_onecell_data hw_data;
};

struct mpfs_ccc_pll_hw_clock {
	void __iomem *base;
	const char *name;
	const struct clk_parent_data *parents;
	unsigned int id;
	u32 reg_offset;
	u32 shift;
	u32 width;
	u32 flags;
	struct clk_hw hw;
	struct clk_init_data init;
};

#define to_mpfs_ccc_clk(_hw) container_of(_hw, struct mpfs_ccc_pll_hw_clock, hw)

/*
 * mpfs_ccc_lock prevents anything else from writing to a fabric ccc
 * while a software locked register is being written.
 */
static DEFINE_SPINLOCK(mpfs_ccc_lock);

static const struct clk_parent_data mpfs_ccc_pll0_refs[] = {
	{ .fw_name = "pll0_ref0" },
	{ .fw_name = "pll0_ref1" },
};

static const struct clk_parent_data mpfs_ccc_pll1_refs[] = {
	{ .fw_name = "pll1_ref0" },
	{ .fw_name = "pll1_ref1" },
};

static unsigned long mpfs_ccc_pll_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct mpfs_ccc_pll_hw_clock *ccc_hw = to_mpfs_ccc_clk(hw);
	void __iomem *mult_addr = ccc_hw->base + ccc_hw->reg_offset;
	void __iomem *ref_div_addr = ccc_hw->base + MPFS_CCC_REF_CR;
	u32 mult, ref_div;

	mult = readl_relaxed(mult_addr) >> MPFS_CCC_FBDIV_SHIFT;
	mult &= clk_div_mask(MPFS_CCC_FBDIV_WIDTH);
	ref_div = readl_relaxed(ref_div_addr) >> MPFS_CCC_REFDIV_SHIFT;
	ref_div &= clk_div_mask(MPFS_CCC_REFDIV_WIDTH);

	return prate * mult / (ref_div * MPFS_CCC_FIXED_DIV);
}

static u8 mpfs_ccc_pll_get_parent(struct clk_hw *hw)
{
	struct mpfs_ccc_pll_hw_clock *ccc_hw = to_mpfs_ccc_clk(hw);
	void __iomem *pll_cr_addr = ccc_hw->base + MPFS_CCC_PLL_CR;

	return !!(readl_relaxed(pll_cr_addr) & MPFS_CCC_REFCLK_SEL);
}

static const struct clk_ops mpfs_ccc_pll_ops = {
	.recalc_rate = mpfs_ccc_pll_recalc_rate,
	.get_parent = mpfs_ccc_pll_get_parent,
};

#define CLK_CCC_PLL(_id, _parents, _shift, _width, _flags, _offset) {	\
	.id = _id,							\
	.shift = _shift,						\
	.width = _width,						\
	.reg_offset = _offset,						\
	.flags = _flags,						\
	.parents = _parents,						\
}

static struct mpfs_ccc_pll_hw_clock mpfs_ccc_pll_clks[] = {
	CLK_CCC_PLL(CLK_CCC_PLL0, mpfs_ccc_pll0_refs, MPFS_CCC_FBDIV_SHIFT,
		    MPFS_CCC_FBDIV_WIDTH, 0, MPFS_CCC_SSCG_2_CR),
	CLK_CCC_PLL(CLK_CCC_PLL1, mpfs_ccc_pll1_refs, MPFS_CCC_FBDIV_SHIFT,
		    MPFS_CCC_FBDIV_WIDTH, 0, MPFS_CCC_SSCG_2_CR),
};

struct mpfs_ccc_out_hw_clock {
	struct clk_divider divider;
	struct clk_init_data init;
	unsigned int id;
	u32 reg_offset;
};

#define CLK_CCC_OUT(_id, _shift, _width, _flags, _offset) {	\
	.id = _id,						\
	.divider.shift = _shift,				\
	.divider.width = _width,				\
	.reg_offset = _offset,					\
	.divider.flags = _flags,				\
	.divider.lock = &mpfs_ccc_lock,				\
}

static struct mpfs_ccc_out_hw_clock mpfs_ccc_pll0out_clks[] = {
	CLK_CCC_OUT(CLK_CCC_PLL0_OUT0, MPFS_CCC_POSTDIV0_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV01_CR),
	CLK_CCC_OUT(CLK_CCC_PLL0_OUT1, MPFS_CCC_POSTDIV1_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV01_CR),
	CLK_CCC_OUT(CLK_CCC_PLL0_OUT2, MPFS_CCC_POSTDIV2_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV23_CR),
	CLK_CCC_OUT(CLK_CCC_PLL0_OUT3, MPFS_CCC_POSTDIV3_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV23_CR),
};

static struct mpfs_ccc_out_hw_clock mpfs_ccc_pll1out_clks[] = {
	CLK_CCC_OUT(CLK_CCC_PLL1_OUT0, MPFS_CCC_POSTDIV0_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV01_CR),
	CLK_CCC_OUT(CLK_CCC_PLL1_OUT1, MPFS_CCC_POSTDIV1_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV01_CR),
	CLK_CCC_OUT(CLK_CCC_PLL1_OUT2, MPFS_CCC_POSTDIV2_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV23_CR),
	CLK_CCC_OUT(CLK_CCC_PLL1_OUT3, MPFS_CCC_POSTDIV3_SHIFT, MPFS_CCC_POSTDIV_WIDTH,
		    CLK_DIVIDER_ONE_BASED, MPFS_CCC_POSTDIV23_CR),
};

static struct mpfs_ccc_out_hw_clock *mpfs_ccc_pllout_clks[] = {
	mpfs_ccc_pll0out_clks, mpfs_ccc_pll1out_clks
};

static int mpfs_ccc_register_outputs(struct device *dev, struct mpfs_ccc_out_hw_clock *out_hws,
				     unsigned int num_clks, struct mpfs_ccc_data *data,
				     struct mpfs_ccc_pll_hw_clock *parent)
{
	int ret;

	for (unsigned int i = 0; i < num_clks; i++) {
		struct mpfs_ccc_out_hw_clock *out_hw = &out_hws[i];
		char *name = devm_kasprintf(dev, GFP_KERNEL, "%s_out%u", parent->name, i);

		if (!name)
			return -ENOMEM;

		out_hw->divider.hw.init = CLK_HW_INIT_HW(name, &parent->hw, &clk_divider_ops, 0);
		out_hw->divider.reg = data->pll_base[i / MPFS_CCC_OUTPUTS_PER_PLL] +
			out_hw->reg_offset;

		ret = devm_clk_hw_register(dev, &out_hw->divider.hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register clock id: %d\n",
					     out_hw->id);

		data->hw_data.hws[out_hw->id] = &out_hw->divider.hw;
	}

	return 0;
}

#define CLK_HW_INIT_PARENTS_DATA_FIXED_SIZE(_name, _parents, _ops, _flags)	\
	(&(struct clk_init_data) {						\
		.flags		= _flags,					\
		.name		= _name,					\
		.parent_data	= _parents,					\
		.num_parents	= MPFS_CCC_REFS_PER_PLL,			\
		.ops		= _ops,						\
	})

static int mpfs_ccc_register_plls(struct device *dev, struct mpfs_ccc_pll_hw_clock *pll_hws,
				  unsigned int num_clks, struct mpfs_ccc_data *data)
{
	int ret;

	for (unsigned int i = 0; i < num_clks; i++) {
		struct mpfs_ccc_pll_hw_clock *pll_hw = &pll_hws[i];

		pll_hw->name = devm_kasprintf(dev, GFP_KERNEL, "ccc%s_pll%u",
					      strchrnul(dev->of_node->full_name, '@'), i);
		if (!pll_hw->name)
			return -ENOMEM;

		pll_hw->base = data->pll_base[i];
		pll_hw->hw.init = CLK_HW_INIT_PARENTS_DATA_FIXED_SIZE(pll_hw->name,
								      pll_hw->parents,
								      &mpfs_ccc_pll_ops, 0);

		ret = devm_clk_hw_register(dev, &pll_hw->hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register ccc id: %d\n",
					     pll_hw->id);

		data->hw_data.hws[pll_hw->id] = &pll_hw->hw;

		ret = mpfs_ccc_register_outputs(dev, mpfs_ccc_pllout_clks[i],
						MPFS_CCC_OUTPUTS_PER_PLL, data, pll_hw);
		if (ret)
			return ret;
	}

	return 0;
}

static int mpfs_ccc_probe(struct platform_device *pdev)
{
	struct mpfs_ccc_data *clk_data;
	void __iomem *pll_base[ARRAY_SIZE(mpfs_ccc_pll_clks)];
	unsigned int num_clks;
	int ret;

	num_clks = ARRAY_SIZE(mpfs_ccc_pll_clks) + ARRAY_SIZE(mpfs_ccc_pll0out_clks) +
		   ARRAY_SIZE(mpfs_ccc_pll1out_clks);

	clk_data = devm_kzalloc(&pdev->dev, struct_size(clk_data, hw_data.hws, num_clks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	pll_base[0] = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pll_base[0]))
		return PTR_ERR(pll_base[0]);

	pll_base[1] = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pll_base[1]))
		return PTR_ERR(pll_base[1]);

	clk_data->pll_base = pll_base;
	clk_data->hw_data.num = num_clks;
	clk_data->dev = &pdev->dev;

	ret = mpfs_ccc_register_plls(clk_data->dev, mpfs_ccc_pll_clks,
				     ARRAY_SIZE(mpfs_ccc_pll_clks), clk_data);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(clk_data->dev, of_clk_hw_onecell_get,
					   &clk_data->hw_data);
}

static const struct of_device_id mpfs_ccc_of_match_table[] = {
	{ .compatible = "microchip,mpfs-ccc", },
	{}
};
MODULE_DEVICE_TABLE(of, mpfs_ccc_of_match_table);

static struct platform_driver mpfs_ccc_driver = {
	.probe = mpfs_ccc_probe,
	.driver	= {
		.name = "microchip-mpfs-ccc",
		.of_match_table = mpfs_ccc_of_match_table,
	},
};

static int __init clk_ccc_init(void)
{
	return platform_driver_register(&mpfs_ccc_driver);
}
core_initcall(clk_ccc_init);

static void __exit clk_ccc_exit(void)
{
	platform_driver_unregister(&mpfs_ccc_driver);
}
module_exit(clk_ccc_exit);

MODULE_DESCRIPTION("Microchip PolarFire SoC Clock Conditioning Circuitry Driver");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
