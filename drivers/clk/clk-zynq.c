/*
 * Copyright (c) 2012 National Instruments
 *
 * Josh Cartwright <josh.cartwright@ni.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/clk-provider.h>

static void __iomem *slcr_base;

struct zynq_pll_clk {
	struct clk_hw	hw;
	void __iomem	*pll_ctrl;
	void __iomem	*pll_cfg;
};

#define to_zynq_pll_clk(hw)	container_of(hw, struct zynq_pll_clk, hw)

#define CTRL_PLL_FDIV(x)	((x) >> 12)

static unsigned long zynq_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct zynq_pll_clk *pll = to_zynq_pll_clk(hw);
	return parent_rate * CTRL_PLL_FDIV(ioread32(pll->pll_ctrl));
}

static const struct clk_ops zynq_pll_clk_ops = {
	.recalc_rate	= zynq_pll_recalc_rate,
};

static void __init zynq_pll_clk_setup(struct device_node *np)
{
	struct clk_init_data init;
	struct zynq_pll_clk *pll;
	const char *parent_name;
	struct clk *clk;
	u32 regs[2];
	int ret;

	ret = of_property_read_u32_array(np, "reg", regs, ARRAY_SIZE(regs));
	if (WARN_ON(ret))
		return;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (WARN_ON(!pll))
		return;

	pll->pll_ctrl = slcr_base + regs[0];
	pll->pll_cfg  = slcr_base + regs[1];

	of_property_read_string(np, "clock-output-names", &init.name);

	init.ops = &zynq_pll_clk_ops;
	parent_name = of_clk_get_parent_name(np, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		return;

	ret = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (WARN_ON(ret))
		return;
}
CLK_OF_DECLARE(zynq_pll, "xlnx,zynq-pll", zynq_pll_clk_setup);

struct zynq_periph_clk {
	struct clk_hw		hw;
	struct clk_onecell_data	onecell_data;
	struct clk		*gates[2];
	void __iomem		*clk_ctrl;
	spinlock_t		clkact_lock;
};

#define to_zynq_periph_clk(hw)	container_of(hw, struct zynq_periph_clk, hw)

static const u8 periph_clk_parent_map[] = {
	0, 0, 1, 2
};
#define PERIPH_CLK_CTRL_SRC(x)	(periph_clk_parent_map[((x) & 0x30) >> 4])
#define PERIPH_CLK_CTRL_DIV(x)	(((x) & 0x3F00) >> 8)

static unsigned long zynq_periph_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct zynq_periph_clk *periph = to_zynq_periph_clk(hw);
	return parent_rate / PERIPH_CLK_CTRL_DIV(ioread32(periph->clk_ctrl));
}

static u8 zynq_periph_get_parent(struct clk_hw *hw)
{
	struct zynq_periph_clk *periph = to_zynq_periph_clk(hw);
	return PERIPH_CLK_CTRL_SRC(ioread32(periph->clk_ctrl));
}

static const struct clk_ops zynq_periph_clk_ops = {
	.recalc_rate	= zynq_periph_recalc_rate,
	.get_parent	= zynq_periph_get_parent,
};

static void __init zynq_periph_clk_setup(struct device_node *np)
{
	struct zynq_periph_clk *periph;
	const char *parent_names[3];
	struct clk_init_data init;
	int clk_num = 0, err;
	const char *name;
	struct clk *clk;
	u32 reg;
	int i;

	err = of_property_read_u32(np, "reg", &reg);
	if (WARN_ON(err))
		return;

	periph = kzalloc(sizeof(*periph), GFP_KERNEL);
	if (WARN_ON(!periph))
		return;

	periph->clk_ctrl = slcr_base + reg;
	spin_lock_init(&periph->clkact_lock);

	init.name = np->name;
	init.ops = &zynq_periph_clk_ops;
	for (i = 0; i < ARRAY_SIZE(parent_names); i++)
		parent_names[i] = of_clk_get_parent_name(np, i);
	init.parent_names = parent_names;
	init.num_parents = ARRAY_SIZE(parent_names);

	periph->hw.init = &init;

	clk = clk_register(NULL, &periph->hw);
	if (WARN_ON(IS_ERR(clk)))
		return;

	err = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (WARN_ON(err))
		return;

	err = of_property_read_string_index(np, "clock-output-names", 0,
					    &name);
	if (WARN_ON(err))
		return;

	periph->gates[0] = clk_register_gate(NULL, name, np->name, 0,
					     periph->clk_ctrl, 0, 0,
					     &periph->clkact_lock);
	if (WARN_ON(IS_ERR(periph->gates[0])))
		return;
	clk_num++;

	/* some periph clks have 2 downstream gates */
	err = of_property_read_string_index(np, "clock-output-names", 1,
					    &name);
	if (err != -ENODATA) {
		periph->gates[1] = clk_register_gate(NULL, name, np->name, 0,
						     periph->clk_ctrl, 1, 0,
						     &periph->clkact_lock);
		if (WARN_ON(IS_ERR(periph->gates[1])))
			return;
		clk_num++;
	}

	periph->onecell_data.clks = periph->gates;
	periph->onecell_data.clk_num = clk_num;

	err = of_clk_add_provider(np, of_clk_src_onecell_get,
				  &periph->onecell_data);
	if (WARN_ON(err))
		return;
}
CLK_OF_DECLARE(zynq_periph, "xlnx,zynq-periph-clock", zynq_periph_clk_setup);

/* CPU Clock domain is modelled as a mux with 4 children subclks, whose
 * derivative rates depend on CLK_621_TRUE
 */

struct zynq_cpu_clk {
	struct clk_hw		hw;
	struct clk_onecell_data	onecell_data;
	struct clk		*subclks[4];
	void __iomem		*clk_ctrl;
	spinlock_t		clkact_lock;
};

#define to_zynq_cpu_clk(hw)	container_of(hw, struct zynq_cpu_clk, hw)

static const u8 zynq_cpu_clk_parent_map[] = {
	1, 1, 2, 0
};
#define CPU_CLK_SRCSEL(x)	(zynq_cpu_clk_parent_map[(((x) & 0x30) >> 4)])
#define CPU_CLK_CTRL_DIV(x)	(((x) & 0x3F00) >> 8)

static u8 zynq_cpu_clk_get_parent(struct clk_hw *hw)
{
	struct zynq_cpu_clk *cpuclk = to_zynq_cpu_clk(hw);
	return CPU_CLK_SRCSEL(ioread32(cpuclk->clk_ctrl));
}

static unsigned long zynq_cpu_clk_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct zynq_cpu_clk *cpuclk = to_zynq_cpu_clk(hw);
	return parent_rate / CPU_CLK_CTRL_DIV(ioread32(cpuclk->clk_ctrl));
}

static const struct clk_ops zynq_cpu_clk_ops = {
	.get_parent	= zynq_cpu_clk_get_parent,
	.recalc_rate	= zynq_cpu_clk_recalc_rate,
};

struct zynq_cpu_subclk {
	struct clk_hw	hw;
	void __iomem	*clk_621;
	enum {
		CPU_SUBCLK_6X4X,
		CPU_SUBCLK_3X2X,
		CPU_SUBCLK_2X,
		CPU_SUBCLK_1X,
	} which;
};

#define CLK_621_TRUE(x)	((x) & 1)

#define to_zynq_cpu_subclk(hw)	container_of(hw, struct zynq_cpu_subclk, hw);

static unsigned long zynq_cpu_subclk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	unsigned long uninitialized_var(rate);
	struct zynq_cpu_subclk *subclk;
	bool is_621;

	subclk = to_zynq_cpu_subclk(hw)
	is_621 = CLK_621_TRUE(ioread32(subclk->clk_621));

	switch (subclk->which) {
	case CPU_SUBCLK_6X4X:
		rate = parent_rate;
		break;
	case CPU_SUBCLK_3X2X:
		rate = parent_rate / 2;
		break;
	case CPU_SUBCLK_2X:
		rate = parent_rate / (is_621 ? 3 : 2);
		break;
	case CPU_SUBCLK_1X:
		rate = parent_rate / (is_621 ? 6 : 4);
		break;
	};

	return rate;
}

static const struct clk_ops zynq_cpu_subclk_ops = {
	.recalc_rate	= zynq_cpu_subclk_recalc_rate,
};

static struct clk *zynq_cpu_subclk_setup(struct device_node *np, u8 which,
					 void __iomem *clk_621)
{
	struct zynq_cpu_subclk *subclk;
	struct clk_init_data init;
	struct clk *clk;
	int err;

	err = of_property_read_string_index(np, "clock-output-names",
					    which, &init.name);
	if (WARN_ON(err))
		goto err_read_output_name;

	subclk = kzalloc(sizeof(*subclk), GFP_KERNEL);
	if (!subclk)
		goto err_subclk_alloc;

	subclk->clk_621 = clk_621;
	subclk->which = which;

	init.ops = &zynq_cpu_subclk_ops;
	init.parent_names = &np->name;
	init.num_parents = 1;

	subclk->hw.init = &init;

	clk = clk_register(NULL, &subclk->hw);
	if (WARN_ON(IS_ERR(clk)))
		goto err_clk_register;

	return clk;

err_clk_register:
	kfree(subclk);
err_subclk_alloc:
err_read_output_name:
	return ERR_PTR(-EINVAL);
}

static void __init zynq_cpu_clk_setup(struct device_node *np)
{
	struct zynq_cpu_clk *cpuclk;
	const char *parent_names[3];
	struct clk_init_data init;
	void __iomem *clk_621;
	struct clk *clk;
	u32 reg[2];
	int err;
	int i;

	err = of_property_read_u32_array(np, "reg", reg, ARRAY_SIZE(reg));
	if (WARN_ON(err))
		return;

	cpuclk = kzalloc(sizeof(*cpuclk), GFP_KERNEL);
	if (WARN_ON(!cpuclk))
		return;

	cpuclk->clk_ctrl = slcr_base + reg[0];
	clk_621 = slcr_base + reg[1];
	spin_lock_init(&cpuclk->clkact_lock);

	init.name = np->name;
	init.ops = &zynq_cpu_clk_ops;
	for (i = 0; i < ARRAY_SIZE(parent_names); i++)
		parent_names[i] = of_clk_get_parent_name(np, i);
	init.parent_names = parent_names;
	init.num_parents = ARRAY_SIZE(parent_names);

	cpuclk->hw.init = &init;

	clk = clk_register(NULL, &cpuclk->hw);
	if (WARN_ON(IS_ERR(clk)))
		return;

	err = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (WARN_ON(err))
		return;

	for (i = 0; i < 4; i++) {
		cpuclk->subclks[i] = zynq_cpu_subclk_setup(np, i, clk_621);
		if (WARN_ON(IS_ERR(cpuclk->subclks[i])))
			return;
	}

	cpuclk->onecell_data.clks = cpuclk->subclks;
	cpuclk->onecell_data.clk_num = i;

	err = of_clk_add_provider(np, of_clk_src_onecell_get,
				  &cpuclk->onecell_data);
	if (WARN_ON(err))
		return;
}
CLK_OF_DECLARE(zynq_cpu, "xlnx,zynq-cpu-clock", zynq_cpu_clk_setup);

void __init xilinx_zynq_clocks_init(void __iomem *slcr)
{
	slcr_base = slcr;
	of_clk_init(NULL);
}
