// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 SiFive, Inc.
 * Wesley Terpstra
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The FU540 PRCI implements clock and reset control for the SiFive
 * FU540-C000 chip.  This driver assumes that it has sole control
 * over all PRCI resources.
 *
 * This driver is based on the PRCI driver written by Wesley Terpstra:
 * https://github.com/riscv/riscv-linux/commit/999529edf517ed75b56659d456d221b2ee56bb60
 *
 * References:
 * - SiFive FU540-C000 manual v1p0, Chapter 7 "Clocking and Reset"
 */

#include <dt-bindings/clock/sifive-fu540-prci.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/clk/analogbits-wrpll-cln28hpc.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * EXPECTED_CLK_PARENT_COUNT: how many parent clocks this driver expects:
 *     hfclk and rtcclk
 */
#define EXPECTED_CLK_PARENT_COUNT		2

/*
 * Register offsets and bitmasks
 */

/* COREPLLCFG0 */
#define PRCI_COREPLLCFG0_OFFSET			0x4
# define PRCI_COREPLLCFG0_DIVR_SHIFT		0
# define PRCI_COREPLLCFG0_DIVR_MASK		(0x3f << PRCI_COREPLLCFG0_DIVR_SHIFT)
# define PRCI_COREPLLCFG0_DIVF_SHIFT		6
# define PRCI_COREPLLCFG0_DIVF_MASK		(0x1ff << PRCI_COREPLLCFG0_DIVF_SHIFT)
# define PRCI_COREPLLCFG0_DIVQ_SHIFT		15
# define PRCI_COREPLLCFG0_DIVQ_MASK		(0x7 << PRCI_COREPLLCFG0_DIVQ_SHIFT)
# define PRCI_COREPLLCFG0_RANGE_SHIFT		18
# define PRCI_COREPLLCFG0_RANGE_MASK		(0x7 << PRCI_COREPLLCFG0_RANGE_SHIFT)
# define PRCI_COREPLLCFG0_BYPASS_SHIFT		24
# define PRCI_COREPLLCFG0_BYPASS_MASK		(0x1 << PRCI_COREPLLCFG0_BYPASS_SHIFT)
# define PRCI_COREPLLCFG0_FSE_SHIFT		25
# define PRCI_COREPLLCFG0_FSE_MASK		(0x1 << PRCI_COREPLLCFG0_FSE_SHIFT)
# define PRCI_COREPLLCFG0_LOCK_SHIFT		31
# define PRCI_COREPLLCFG0_LOCK_MASK		(0x1 << PRCI_COREPLLCFG0_LOCK_SHIFT)

/* DDRPLLCFG0 */
#define PRCI_DDRPLLCFG0_OFFSET			0xc
# define PRCI_DDRPLLCFG0_DIVR_SHIFT		0
# define PRCI_DDRPLLCFG0_DIVR_MASK		(0x3f << PRCI_DDRPLLCFG0_DIVR_SHIFT)
# define PRCI_DDRPLLCFG0_DIVF_SHIFT		6
# define PRCI_DDRPLLCFG0_DIVF_MASK		(0x1ff << PRCI_DDRPLLCFG0_DIVF_SHIFT)
# define PRCI_DDRPLLCFG0_DIVQ_SHIFT		15
# define PRCI_DDRPLLCFG0_DIVQ_MASK		(0x7 << PRCI_DDRPLLCFG0_DIVQ_SHIFT)
# define PRCI_DDRPLLCFG0_RANGE_SHIFT		18
# define PRCI_DDRPLLCFG0_RANGE_MASK		(0x7 << PRCI_DDRPLLCFG0_RANGE_SHIFT)
# define PRCI_DDRPLLCFG0_BYPASS_SHIFT		24
# define PRCI_DDRPLLCFG0_BYPASS_MASK		(0x1 << PRCI_DDRPLLCFG0_BYPASS_SHIFT)
# define PRCI_DDRPLLCFG0_FSE_SHIFT		25
# define PRCI_DDRPLLCFG0_FSE_MASK		(0x1 << PRCI_DDRPLLCFG0_FSE_SHIFT)
# define PRCI_DDRPLLCFG0_LOCK_SHIFT		31
# define PRCI_DDRPLLCFG0_LOCK_MASK		(0x1 << PRCI_DDRPLLCFG0_LOCK_SHIFT)

/* DDRPLLCFG1 */
#define PRCI_DDRPLLCFG1_OFFSET			0x10
# define PRCI_DDRPLLCFG1_CKE_SHIFT		24
# define PRCI_DDRPLLCFG1_CKE_MASK		(0x1 << PRCI_DDRPLLCFG1_CKE_SHIFT)

/* GEMGXLPLLCFG0 */
#define PRCI_GEMGXLPLLCFG0_OFFSET		0x1c
# define PRCI_GEMGXLPLLCFG0_DIVR_SHIFT		0
# define PRCI_GEMGXLPLLCFG0_DIVR_MASK		(0x3f << PRCI_GEMGXLPLLCFG0_DIVR_SHIFT)
# define PRCI_GEMGXLPLLCFG0_DIVF_SHIFT		6
# define PRCI_GEMGXLPLLCFG0_DIVF_MASK		(0x1ff << PRCI_GEMGXLPLLCFG0_DIVF_SHIFT)
# define PRCI_GEMGXLPLLCFG0_DIVQ_SHIFT		15
# define PRCI_GEMGXLPLLCFG0_DIVQ_MASK		(0x7 << PRCI_GEMGXLPLLCFG0_DIVQ_SHIFT)
# define PRCI_GEMGXLPLLCFG0_RANGE_SHIFT		18
# define PRCI_GEMGXLPLLCFG0_RANGE_MASK		(0x7 << PRCI_GEMGXLPLLCFG0_RANGE_SHIFT)
# define PRCI_GEMGXLPLLCFG0_BYPASS_SHIFT	24
# define PRCI_GEMGXLPLLCFG0_BYPASS_MASK		(0x1 << PRCI_GEMGXLPLLCFG0_BYPASS_SHIFT)
# define PRCI_GEMGXLPLLCFG0_FSE_SHIFT		25
# define PRCI_GEMGXLPLLCFG0_FSE_MASK		(0x1 << PRCI_GEMGXLPLLCFG0_FSE_SHIFT)
# define PRCI_GEMGXLPLLCFG0_LOCK_SHIFT		31
# define PRCI_GEMGXLPLLCFG0_LOCK_MASK		(0x1 << PRCI_GEMGXLPLLCFG0_LOCK_SHIFT)

/* GEMGXLPLLCFG1 */
#define PRCI_GEMGXLPLLCFG1_OFFSET		0x20
# define PRCI_GEMGXLPLLCFG1_CKE_SHIFT		24
# define PRCI_GEMGXLPLLCFG1_CKE_MASK		(0x1 << PRCI_GEMGXLPLLCFG1_CKE_SHIFT)

/* CORECLKSEL */
#define PRCI_CORECLKSEL_OFFSET			0x24
# define PRCI_CORECLKSEL_CORECLKSEL_SHIFT	0
# define PRCI_CORECLKSEL_CORECLKSEL_MASK	(0x1 << PRCI_CORECLKSEL_CORECLKSEL_SHIFT)

/* DEVICESRESETREG */
#define PRCI_DEVICESRESETREG_OFFSET			0x28
# define PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_SHIFT	0
# define PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_MASK	(0x1 << PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_SHIFT)
# define PRCI_DEVICESRESETREG_DDR_AXI_RST_N_SHIFT	1
# define PRCI_DEVICESRESETREG_DDR_AXI_RST_N_MASK	(0x1 << PRCI_DEVICESRESETREG_DDR_AXI_RST_N_SHIFT)
# define PRCI_DEVICESRESETREG_DDR_AHB_RST_N_SHIFT	2
# define PRCI_DEVICESRESETREG_DDR_AHB_RST_N_MASK	(0x1 << PRCI_DEVICESRESETREG_DDR_AHB_RST_N_SHIFT)
# define PRCI_DEVICESRESETREG_DDR_PHY_RST_N_SHIFT	3
# define PRCI_DEVICESRESETREG_DDR_PHY_RST_N_MASK	(0x1 << PRCI_DEVICESRESETREG_DDR_PHY_RST_N_SHIFT)
# define PRCI_DEVICESRESETREG_GEMGXL_RST_N_SHIFT	5
# define PRCI_DEVICESRESETREG_GEMGXL_RST_N_MASK		(0x1 << PRCI_DEVICESRESETREG_GEMGXL_RST_N_SHIFT)

/* CLKMUXSTATUSREG */
#define PRCI_CLKMUXSTATUSREG_OFFSET			0x2c
# define PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_SHIFT	1
# define PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_MASK	(0x1 << PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_SHIFT)

/*
 * Private structures
 */

/**
 * struct __prci_data - per-device-instance data
 * @va: base virtual address of the PRCI IP block
 * @hw_clks: encapsulates struct clk_hw records
 *
 * PRCI per-device instance data
 */
struct __prci_data {
	void __iomem *va;
	struct clk_hw_onecell_data hw_clks;
};

/**
 * struct __prci_wrpll_data - WRPLL configuration and integration data
 * @c: WRPLL current configuration record
 * @enable_bypass: fn ptr to code to bypass the WRPLL (if applicable; else NULL)
 * @disable_bypass: fn ptr to code to not bypass the WRPLL (or NULL)
 * @cfg0_offs: WRPLL CFG0 register offset (in bytes) from the PRCI base address
 *
 * @enable_bypass and @disable_bypass are used for WRPLL instances
 * that contain a separate external glitchless clock mux downstream
 * from the PLL.  The WRPLL internal bypass mux is not glitchless.
 */
struct __prci_wrpll_data {
	struct wrpll_cfg c;
	void (*enable_bypass)(struct __prci_data *pd);
	void (*disable_bypass)(struct __prci_data *pd);
	u8 cfg0_offs;
};

/**
 * struct __prci_clock - describes a clock device managed by PRCI
 * @name: user-readable clock name string - should match the manual
 * @parent_name: parent name for this clock
 * @ops: struct clk_ops for the Linux clock framework to use for control
 * @hw: Linux-private clock data
 * @pwd: WRPLL-specific data, associated with this clock (if not NULL)
 * @pd: PRCI-specific data associated with this clock (if not NULL)
 *
 * PRCI clock data.  Used by the PRCI driver to register PRCI-provided
 * clocks to the Linux clock infrastructure.
 */
struct __prci_clock {
	const char *name;
	const char *parent_name;
	const struct clk_ops *ops;
	struct clk_hw hw;
	struct __prci_wrpll_data *pwd;
	struct __prci_data *pd;
};

#define clk_hw_to_prci_clock(pwd) container_of(pwd, struct __prci_clock, hw)

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
 * Given a value @r read from an FU540 PRCI PLL configuration register,
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

	c->flags &= (WRPLL_FLAGS_INT_FEEDBACK_MASK |
		     WRPLL_FLAGS_EXT_FEEDBACK_MASK);

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
 * __prci_wrpll_read_cfg() - read the WRPLL configuration from the PRCI
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
static void __prci_wrpll_read_cfg(struct __prci_data *pd,
				  struct __prci_wrpll_data *pwd)
{
	__prci_wrpll_unpack(&pwd->c, __prci_readl(pd, pwd->cfg0_offs));
}

/**
 * __prci_wrpll_write_cfg() - write WRPLL configuration into the PRCI
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
static void __prci_wrpll_write_cfg(struct __prci_data *pd,
				   struct __prci_wrpll_data *pwd,
				   struct wrpll_cfg *c)
{
	__prci_writel(__prci_wrpll_pack(c), pwd->cfg0_offs, pd);

	memcpy(&pwd->c, c, sizeof(*c));
}

/* Core clock mux control */

/**
 * __prci_coreclksel_use_hfclk() - switch the CORECLK mux to output HFCLK
 * @pd: struct __prci_data * for the PRCI containing the CORECLK mux reg
 *
 * Switch the CORECLK mux to the HFCLK input source; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_CORECLKSEL_OFFSET register.
 */
static void __prci_coreclksel_use_hfclk(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);
	r |= PRCI_CORECLKSEL_CORECLKSEL_MASK;
	__prci_writel(r, PRCI_CORECLKSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET); /* barrier */
}

/**
 * __prci_coreclksel_use_corepll() - switch the CORECLK mux to output COREPLL
 * @pd: struct __prci_data * for the PRCI containing the CORECLK mux reg
 *
 * Switch the CORECLK mux to the PLL output clock; return once complete.
 *
 * Context: Any context.  Caller must prevent concurrent changes to the
 *          PRCI_CORECLKSEL_OFFSET register.
 */
static void __prci_coreclksel_use_corepll(struct __prci_data *pd)
{
	u32 r;

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET);
	r &= ~PRCI_CORECLKSEL_CORECLKSEL_MASK;
	__prci_writel(r, PRCI_CORECLKSEL_OFFSET, pd);

	r = __prci_readl(pd, PRCI_CORECLKSEL_OFFSET); /* barrier */
}

/*
 * Linux clock framework integration
 *
 * See the Linux clock framework documentation for more information on
 * these functions.
 */

static unsigned long sifive_fu540_prci_wrpll_recalc_rate(struct clk_hw *hw,
							 unsigned long parent_rate)
{
	struct __prci_clock *pc = clk_hw_to_prci_clock(hw);
	struct __prci_wrpll_data *pwd = pc->pwd;

	return wrpll_calc_output_rate(&pwd->c, parent_rate);
}

static long sifive_fu540_prci_wrpll_round_rate(struct clk_hw *hw,
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

static int sifive_fu540_prci_wrpll_set_rate(struct clk_hw *hw,
					    unsigned long rate,
					    unsigned long parent_rate)
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

	__prci_wrpll_write_cfg(pd, pwd, &pwd->c);

	udelay(wrpll_calc_max_lock_us(&pwd->c));

	if (pwd->disable_bypass)
		pwd->disable_bypass(pd);

	return 0;
}

static const struct clk_ops sifive_fu540_prci_wrpll_clk_ops = {
	.set_rate = sifive_fu540_prci_wrpll_set_rate,
	.round_rate = sifive_fu540_prci_wrpll_round_rate,
	.recalc_rate = sifive_fu540_prci_wrpll_recalc_rate,
};

static const struct clk_ops sifive_fu540_prci_wrpll_ro_clk_ops = {
	.recalc_rate = sifive_fu540_prci_wrpll_recalc_rate,
};

/* TLCLKSEL clock integration */

static unsigned long sifive_fu540_prci_tlclksel_recalc_rate(struct clk_hw *hw,
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

static const struct clk_ops sifive_fu540_prci_tlclksel_clk_ops = {
	.recalc_rate = sifive_fu540_prci_tlclksel_recalc_rate,
};

/*
 * PRCI integration data for each WRPLL instance
 */

static struct __prci_wrpll_data __prci_corepll_data = {
	.cfg0_offs = PRCI_COREPLLCFG0_OFFSET,
	.enable_bypass = __prci_coreclksel_use_hfclk,
	.disable_bypass = __prci_coreclksel_use_corepll,
};

static struct __prci_wrpll_data __prci_ddrpll_data = {
	.cfg0_offs = PRCI_DDRPLLCFG0_OFFSET,
};

static struct __prci_wrpll_data __prci_gemgxlpll_data = {
	.cfg0_offs = PRCI_GEMGXLPLLCFG0_OFFSET,
};

/*
 * List of clock controls provided by the PRCI
 */

static struct __prci_clock __prci_init_clocks[] = {
	[PRCI_CLK_COREPLL] = {
		.name = "corepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_clk_ops,
		.pwd = &__prci_corepll_data,
	},
	[PRCI_CLK_DDRPLL] = {
		.name = "ddrpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_ro_clk_ops,
		.pwd = &__prci_ddrpll_data,
	},
	[PRCI_CLK_GEMGXLPLL] = {
		.name = "gemgxlpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_clk_ops,
		.pwd = &__prci_gemgxlpll_data,
	},
	[PRCI_CLK_TLCLK] = {
		.name = "tlclk",
		.parent_name = "corepll",
		.ops = &sifive_fu540_prci_tlclksel_clk_ops,
	},
};

/**
 * __prci_register_clocks() - register clock controls in the PRCI with Linux
 * @dev: Linux struct device *
 *
 * Register the list of clock controls described in __prci_init_plls[] with
 * the Linux clock framework.
 *
 * Return: 0 upon success or a negative error code upon failure.
 */
static int __prci_register_clocks(struct device *dev, struct __prci_data *pd)
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
	for (i = 0; i < ARRAY_SIZE(__prci_init_clocks); ++i) {
		pic = &__prci_init_clocks[i];

		init.name = pic->name;
		init.parent_names = &pic->parent_name;
		init.num_parents = 1;
		init.ops = pic->ops;
		pic->hw.init = &init;

		pic->pd = pd;

		if (pic->pwd)
			__prci_wrpll_read_cfg(pd, pic->pwd);

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

/*
 * Linux device model integration
 *
 * See the Linux device model documentation for more information about
 * these functions.
 */
static int sifive_fu540_prci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct __prci_data *pd;
	int r;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pd->va = devm_ioremap_resource(dev, res);
	if (IS_ERR(pd->va))
		return PTR_ERR(pd->va);

	r = __prci_register_clocks(dev, pd);
	if (r) {
		dev_err(dev, "could not register clocks: %d\n", r);
		return r;
	}

	dev_dbg(dev, "SiFive FU540 PRCI probed\n");

	return 0;
}

static const struct of_device_id sifive_fu540_prci_of_match[] = {
	{ .compatible = "sifive,fu540-c000-prci", },
	{}
};
MODULE_DEVICE_TABLE(of, sifive_fu540_prci_of_match);

static struct platform_driver sifive_fu540_prci_driver = {
	.driver	= {
		.name = "sifive-fu540-prci",
		.of_match_table = sifive_fu540_prci_of_match,
	},
	.probe = sifive_fu540_prci_probe,
};

static int __init sifive_fu540_prci_init(void)
{
	return platform_driver_register(&sifive_fu540_prci_driver);
}
core_initcall(sifive_fu540_prci_init);
