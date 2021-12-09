// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 SiFive, Inc.
 * Copyright (C) 2020 Zong Li
 */

#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include "sifive-prci.h"
#include "fu540-prci.h"
#include "fu740-prci.h"

static const struct prci_clk_desc prci_clk_fu540 = {
	.clks = __prci_init_clocks_fu540,
	.num_clks = ARRAY_SIZE(__prci_init_clocks_fu540),
};

/*
 * Private functions
 */

/**
 * __prci_readl() - read from a PRCI register
 * @pd: PRCI context
 * @offs: register offset to read from (in bytes, from PRCI base address)
 *
 * Read the register located at offset @offs from the base virtual
 * address of the PRCI register target described by @pd, and return
 * the value to the caller.
 *
 * Context: Any context.
 *
 * Return: the contents of the register described by @pd and @offs.
 */
static u32 __prci_readl(struct __prci_data *pd, u32 offs)
{
	return readl_relaxed(pd->va + offs);
}

static void __prci_writel(u32 v, u32 offs, struct __prci_data *pd)
{
	writel_relaxed(v, pd->va + offs);
}

/* WRPLL-related private functions */

/**
 * __prci_wrpll_unpack() - unpack WRPLL configuration registers into parameters
 * @c: ptr to a struct wrpll_cfg record to write config into
 * @r: value read from the PRCI PLL configuration register
 *
 * Given a value @r read from an FU740 PRCI PLL configuration register,
 * split it into fields and populate it into the WRPLL configuration record
 * pointed to by @c.
 *
 * The COREPLLCFG0 macros are used below, but the other *PLLCFG0 macros
 * have the same register layout.
 *
 * Context: Any context.
 */
static void __prci_wrpll_unpack(struct wrpll_cfg *c, u32 r)
{
	u32 v;

	v = r & PRCI_COREPLLCFG0_DIVR_MASK;
	v >>= PRCI_COREPLLCFG0_DIVR_SHIFT;
	c->divr = v;

	v = r & PRCI_COREPLLCFG0_DIVF_MASK;
	v >>= PRCI_COREPLLCFG0_DIVF_SHIFT;
	c->divf = v;

	v = r & PRCI_COREPLLCFG0_DIVQ_MASK;
	v >>= PRCI_COREPLLCFG0_DIVQ_SHIFT;
	c->divq = v;

	v = r & PRCI_COREPLLCFG0_RANGE_MASK;
	v >>= PRCI_COREPLLCFG0_RANGE_SHIFT;
	c->range = v;

	c->flags &=
	    (WRPLL_FLAGS_INT_FEEDBACK_MASK | WRPLL_FLAGS_EXT_FEEDBACK_MASK);

	/* external feedback mode not supported */
	c->flags |= WRPLL_FLAGS_INT_FEEDBACK_MASK;
}

/**
 * __prci_wrpll_pack() - pack PLL configuration parameters into a register value
 * @c: pointer to a struct wrpll_cfg record containing the PLL's cfg
 *
 * Using a set of WRPLL configuration values pointed to by @c,
 * assemble a PRCI PLL configuration register value, and return it to
 * the caller.
 *
 * Context: Any context.  Caller must ensure that the contents of the
 *          record pointed to by @c do not change during the execution
 *          of this function.
 *
 * Returns: a value suitable for writing into a PRCI PLL configuration
 *          register
 */
static u32 __prci_wrpll_pack(const struct wrpll_cfg *c)
{
	u32 r = 0;

	r |= c->divr << PRCI_COREPLLCFG0_DIVR_SHIFT;
	r |= c->divf << PRCI_COREPLLCFG0_DIVF_SHIFT;
	r |= c->divq << PRCI_COREPLLCFG0_DIVQ_SHIFT;
	r |= c->range << PRCI_COREPLLCFG0_RANGE_SHIFT;

	/* external feedback mode not supported */
	r |= PRCI_COREPLLCFG0_FSE_MASK;

	return r;
}

/**
 * __prci_wrpll_read_cfg0() - read the WRPLL configuration from the PRCI
 * @pd: PRCI context
 * @pwd: PRCI WRPLL metadata
 *
 * Read the current configuration of the PLL identified by @pwd from
 * the PRCI identified by @pd, and store it into the local configuration
 * cache in @pwd.
 *
 * Context: Any context.  Caller must prevent the records pointed to by
 *          @pd and @pwd from changing during execution.
 */
static void __prci_wrpll_read_cfg0(struct __prci_data *pd,
				   struct __prci_wrpll_data *pwd)
{
	__prci_wrpll_unpack(&pwd->c, __prci_readl(pd, pwd->cfg0_offs));
}

/**
 * __prci_wrpll_write_cfg0() - write WRPLL configuration into the PRCI
 * @pd: PRCI context
 * @pwd: PRCI WRPLL metadata
 * @c: WRPLL configuration record to write
 *
 * Write the WRPLL configuration described by @c into the WRPLL
 * configuration register identified by @pwd in the PRCI instance
 * described by @c.  Make a cached copy of the WRPLL's current
 * configuration so it can be used by other code.
 *
 * Context: Any context.  Caller must prevent the records pointed to by
 *          @pd and @pwd from changing during execution.
 */
static void __prci_wrpll_write_cfg0(struct __prci_data *pd,
				    struct __prci_wrpll_data *pwd,
				    struct wrpll_cfg *c)
{
	__prci_writel(__prci_wrpll_pack(c), pwd->cfg0_offs, pd);

	memcpy(&pwd->c, c, sizeof(*c));
}

/**
 * __prci_wrpll_write_cfg1() - write Clock enable/disable configuration
 * into the PRCI
 * @pd: PRCI context
 * @pwd: PRCI WRPLL metadata
 * @enable: Clock enable or disable value
 */
static void __prci_wrpll_write_cfg1(struct __prci_data *pd,
				    struct __prci_wrpll_data *pwd,
				    u32 enable)
{
	__prci_writel(enable, pwd->cfg1_offs, pd);
}

/*
 * Linux clock framework integration
 *
 * See the Linux clock framework documentation for more information on
 * these functions.
 */

unsigned long sifive_prci_wrpll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;

	return wrpll_calc_output_rate(&pwd->c, parent_rate);
}

long sifive_prci_wrpll_round_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long *parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;
	struct wrpll_cfg c;

	memcpy(&c, &pwd->c, sizeof(c));

	wrpll_configure_for_rate(&c, rate, *parent_rate);

	return wrpll_calc_output_rate(&c, *parent_rate);
}

int sifive_prci_wrpll_set_rate(struct clk_hw *hw,
			       unsigned long rate, unsigned long parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;
	struct __prci_data *pd = pc->pd;
	int r;

	r = wrpll_configure_for_rate(&pwd->c, rate, parent_rate);
	if (r)
		return r;

	if (pwd->enable_bypass)
		pwd->enable_bypass(pd);

	__prci_wrpll_write_cfg0(pd, pwd, &pwd->c);

	udelay(wrpll_calc_max_lock_us(&pwd->c));

	return 0;
}

int sifive_clk_is_enabled(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;
	struct __prci_data *pd = pc->pd;
	u32 r;

	r = __prci_readl(pd, pwd->cfg1_offs);

	if (r & PRCI_COREPLLCFG1_CKE_MASK)
		return 1;
	else
		return 0;
}

int sifive_prci_clock_enable(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;
	struct __prci_data *pd = pc->pd;

	if (sifive_clk_is_enabled(hw))
		return 0;

	__prci_wrpll_write_cfg1(pd, pwd, PRCI_COREPLLCFG1_CKE_MASK);

	if (pwd->disable_bypass)
		pwd->disable_bypass(pd);

	return 0;
}

void sifive_prci_clock_disable(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;
	struct __prci_data *pd = pc->pd;
	u32 r;

	if (pwd->enable_bypass)
		pwd->enable_bypass(pd);

	r = __prci_readl(pd, pwd->cfg1_offs);
	r &= ~PRCI_COREPLLCFG1_CKE_MASK;

	__prci_wrpll_write_cfg1(pd, pwd, r);
}

/* TLCLKSEL clock integration */

unsigned long sifive_prci_tlclksel_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_data *pd = pc->pd;
	u32 v;
	u8 div;

	v = __prci_readl(pd, PRCI_CLKMUXSTATUSREG_OFFSET);
	v &= PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_MASK;
	div = v ? 1 : 2;

	return div_u64(parent_rate, div);
}

/* HFPCLK clock integration */

unsigned long sifive_prci_hfpclkplldiv_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_data *pd = pc->pd;
	u32 div = __prci_readl(pd, PRCI_HFPCLKPLLDIV_OFFSET);

	return div_u64(parent_rate, div + 2);
}

/*
 * Core clock mux control
 */

/**
 * sifive_prci_coreclksel_use_hfclk() - switch the CORECLK mux to output HFCLK
 * @pd: struct __prci_data * for the PRCI containing the CORECLK mux reg
 *
 * Switch the CORECLK mux to the HFCLK input source; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_CORECLKSEL_OFFSET register.
 */
void sifive_prci_coreclksel_use_hfclk(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);
	r |= PRCI_CORECLKSEL_CORECLKSEL_MASK;
	__prci_writel(r, PRCI_CORECLKSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_coreclksel_use_corepll() - switch the CORECLK mux to output
 * COREPLL
 * @pd: struct __prci_data * for the PRCI containing the CORECLK mux reg
 *
 * Switch the CORECLK mux to the COREPLL output clock; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_CORECLKSEL_OFFSET register.
 */
void sifive_prci_coreclksel_use_corepll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);
	r &= ~PRCI_CORECLKSEL_CORECLKSEL_MASK;
	__prci_writel(r, PRCI_CORECLKSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_coreclksel_use_final_corepll() - switch the CORECLK mux to output
 * FINAL_COREPLL
 * @pd: struct __prci_data * for the PRCI containing the CORECLK mux reg
 *
 * Switch the CORECLK mux to the final COREPLL output clock; return once
 * complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_CORECLKSEL_OFFSET register.
 */
void sifive_prci_coreclksel_use_final_corepll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);
	r &= ~PRCI_CORECLKSEL_CORECLKSEL_MASK;
	__prci_writel(r, PRCI_CORECLKSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_corepllsel_use_dvfscorepll() - switch the COREPLL mux to
 * output DVFS_COREPLL
 * @pd: struct __prci_data * for the PRCI containing the COREPLL mux reg
 *
 * Switch the COREPLL mux to the DVFSCOREPLL output clock; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_COREPLLSEL_OFFSET register.
 */
void sifive_prci_corepllsel_use_dvfscorepll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_COREPLLSEL_OFFSET);
	r |= PRCI_COREPLLSEL_COREPLLSEL_MASK;
	__prci_writel(r, PRCI_COREPLLSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_COREPLLSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_corepllsel_use_corepll() - switch the COREPLL mux to
 * output COREPLL
 * @pd: struct __prci_data * for the PRCI containing the COREPLL mux reg
 *
 * Switch the COREPLL mux to the COREPLL output clock; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_COREPLLSEL_OFFSET register.
 */
void sifive_prci_corepllsel_use_corepll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_COREPLLSEL_OFFSET);
	r &= ~PRCI_COREPLLSEL_COREPLLSEL_MASK;
	__prci_writel(r, PRCI_COREPLLSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_COREPLLSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_hfpclkpllsel_use_hfclk() - switch the HFPCLKPLL mux to
 * output HFCLK
 * @pd: struct __prci_data * for the PRCI containing the HFPCLKPLL mux reg
 *
 * Switch the HFPCLKPLL mux to the HFCLK input source; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_HFPCLKPLLSEL_OFFSET register.
 */
void sifive_prci_hfpclkpllsel_use_hfclk(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_HFPCLKPLLSEL_OFFSET);
	r |= PRCI_HFPCLKPLLSEL_HFPCLKPLLSEL_MASK;
	__prci_writel(r, PRCI_HFPCLKPLLSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_HFPCLKPLLSEL_OFFSET);	/* barrier */
}

/**
 * sifive_prci_hfpclkpllsel_use_hfpclkpll() - switch the HFPCLKPLL mux to
 * output HFPCLKPLL
 * @pd: struct __prci_data * for the PRCI containing the HFPCLKPLL mux reg
 *
 * Switch the HFPCLKPLL mux to the HFPCLKPLL output clock; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_HFPCLKPLLSEL_OFFSET register.
 */
void sifive_prci_hfpclkpllsel_use_hfpclkpll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_HFPCLKPLLSEL_OFFSET);
	r &= ~PRCI_HFPCLKPLLSEL_HFPCLKPLLSEL_MASK;
	__prci_writel(r, PRCI_HFPCLKPLLSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_HFPCLKPLLSEL_OFFSET);	/* barrier */
}

/* PCIE AUX clock APIs for enable, disable. */
int sifive_prci_pcie_aux_clock_is_enabled(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_data *pd = pc->pd;
	u32 r;

	r = __prci_readl(pd, PRCI_PCIE_AUX_OFFSET);

	if (r & PRCI_PCIE_AUX_EN_MASK)
		return 1;
	else
		return 0;
}

int sifive_prci_pcie_aux_clock_enable(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_data *pd = pc->pd;
	u32 r __maybe_unused;

	if (sifive_prci_pcie_aux_clock_is_enabled(hw))
		return 0;

	__prci_writel(1, PRCI_PCIE_AUX_OFFSET, pd);
	r = __prci_readl(pd, PRCI_PCIE_AUX_OFFSET);	/* barrier */

	return 0;
}

void sifive_prci_pcie_aux_clock_disable(struct clk_hw *hw)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_data *pd = pc->pd;
	u32 r __maybe_unused;

	__prci_writel(0, PRCI_PCIE_AUX_OFFSET, pd);
	r = __prci_readl(pd, PRCI_PCIE_AUX_OFFSET);	/* barrier */

}

/**
 * __prci_register_clocks() - register clock controls in the PRCI
 * @dev: Linux struct device
 * @pd: The pointer for PRCI per-device instance data
 * @desc: The pointer for the information of clocks of each SoCs
 *
 * Register the list of clock controls described in __prci_init_clocks[] with
 * the Linux clock framework.
 *
 * Return: 0 upon success or a negative error code upon failure.
 */
static int __prci_register_clocks(struct device *dev, struct __prci_data *pd,
				  const struct prci_clk_desc *desc)
{
	struct clk_init_data init = { };
	struct __prci_clock *pic;
	int parent_count, i, r;

	parent_count = of_clk_get_parent_count(dev->of_node);
	if (parent_count != EXPECTED_CLK_PARENT_COUNT) {
		dev_err(dev, "expected only two parent clocks, found %d\n",
			parent_count);
		return -EINVAL;
	}

	/* Register PLLs */
	for (i = 0; i < desc->num_clks; ++i) {
		pic = &(desc->clks[i]);

		init.name = pic->name;
		init.parent_names = &pic->parent_name;
		init.num_parents = 1;
		init.ops = pic->ops;
		pic->hw.init = &init;

		pic->pd = pd;

		if (pic->pwd)
			__prci_wrpll_read_cfg0(pd, pic->pwd);

		r = devm_clk_hw_register(dev, &pic->hw);
		if (r) {
			dev_warn(dev, "Failed to register clock %s: %d\n",
				 init.name, r);
			return r;
		}

		r = clk_hw_register_clkdev(&pic->hw, pic->name, dev_name(dev));
		if (r) {
			dev_warn(dev, "Failed to register clkdev for %s: %d\n",
				 init.name, r);
			return r;
		}

		pd->hw_clks.hws[i] = &pic->hw;
	}

	pd->hw_clks.num = i;

	r = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					&pd->hw_clks);
	if (r) {
		dev_err(dev, "could not add hw_provider: %d\n", r);
		return r;
	}

	return 0;
}

/**
 * sifive_prci_probe() - initialize prci data and check parent count
 * @pdev: platform device pointer for the prci
 *
 * Return: 0 upon success or a negative error code upon failure.
 */
static int sifive_prci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct __prci_data *pd;
	const struct prci_clk_desc *desc;
	int r;

	desc = of_device_get_match_data(&pdev->dev);

	pd = devm_kzalloc(dev, struct_size(pd, hw_clks.hws, desc->num_clks), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pd->va = devm_ioremap_resource(dev, res);
	if (IS_ERR(pd->va))
		return PTR_ERR(pd->va);

	pd->reset.rcdev.owner = THIS_MODULE;
	pd->reset.rcdev.nr_resets = PRCI_RST_NR;
	pd->reset.rcdev.ops = &reset_simple_ops;
	pd->reset.rcdev.of_node = pdev->dev.of_node;
	pd->reset.active_low = true;
	pd->reset.membase = pd->va + PRCI_DEVICESRESETREG_OFFSET;
	spin_lock_init(&pd->reset.lock);

	r = devm_reset_controller_register(&pdev->dev, &pd->reset.rcdev);
	if (r) {
		dev_err(dev, "could not register reset controller: %d\n", r);
		return r;
	}
	r = __prci_register_clocks(dev, pd, desc);
	if (r) {
		dev_err(dev, "could not register clocks: %d\n", r);
		return r;
	}

	dev_dbg(dev, "SiFive PRCI probed\n");

	return 0;
}

static const struct of_device_id sifive_prci_of_match[] = {
	{.compatible = "sifive,fu540-c000-prci", .data = &prci_clk_fu540},
	{.compatible = "sifive,fu740-c000-prci", .data = &prci_clk_fu740},
	{}
};

static struct platform_driver sifive_prci_driver = {
	.driver = {
		.name = "sifive-clk-prci",
		.of_match_table = sifive_prci_of_match,
	},
	.probe = sifive_prci_probe,
};

static int __init sifive_prci_init(void)
{
	return platform_driver_register(&sifive_prci_driver);
}
core_initcall(sifive_prci_init);
