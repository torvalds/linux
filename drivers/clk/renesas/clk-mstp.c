// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car MSTP clocks
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2015 Glider bvba
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/renesas.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/spinlock.h>

/*
 * MSTP clocks. We can't use standard gate clocks as we need to poll on the
 * status register when enabling the clock.
 */

#define MSTP_MAX_CLOCKS		32

/**
 * struct mstp_clock_group - MSTP gating clocks group
 *
 * @data: clock specifier translation for clocks in this group
 * @smstpcr: module stop control register
 * @mstpsr: module stop status register (optional)
 * @lock: protects writes to SMSTPCR
 * @width_8bit: registers are 8-bit, not 32-bit
 * @clks: clocks in this group
 */
struct mstp_clock_group {
	struct clk_onecell_data data;
	void __iomem *smstpcr;
	void __iomem *mstpsr;
	spinlock_t lock;
	bool width_8bit;
	struct clk *clks[];
};

/**
 * struct mstp_clock - MSTP gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @bit_index: control bit index
 * @group: MSTP clocks group
 */
struct mstp_clock {
	struct clk_hw hw;
	u32 bit_index;
	struct mstp_clock_group *group;
};

#define to_mstp_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static inline u32 cpg_mstp_read(struct mstp_clock_group *group,
				u32 __iomem *reg)
{
	return group->width_8bit ? readb(reg) : readl(reg);
}

static inline void cpg_mstp_write(struct mstp_clock_group *group, u32 val,
				  u32 __iomem *reg)
{
	group->width_8bit ? writeb(val, reg) : writel(val, reg);
}

static int cpg_mstp_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct mstp_clock_group *group = clock->group;
	u32 bitmask = BIT(clock->bit_index);
	unsigned long flags;
	unsigned int i;
	u32 value;

	spin_lock_irqsave(&group->lock, flags);

	value = cpg_mstp_read(group, group->smstpcr);
	if (enable)
		value &= ~bitmask;
	else
		value |= bitmask;
	cpg_mstp_write(group, value, group->smstpcr);

	if (!group->mstpsr) {
		/* dummy read to ensure write has completed */
		cpg_mstp_read(group, group->smstpcr);
		barrier_data(group->smstpcr);
	}

	spin_unlock_irqrestore(&group->lock, flags);

	if (!enable || !group->mstpsr)
		return 0;

	for (i = 1000; i > 0; --i) {
		if (!(cpg_mstp_read(group, group->mstpsr) & bitmask))
			break;
		cpu_relax();
	}

	if (!i) {
		pr_err("%s: failed to enable %p[%d]\n", __func__,
		       group->smstpcr, clock->bit_index);
		return -ETIMEDOUT;
	}

	return 0;
}

static int cpg_mstp_clock_enable(struct clk_hw *hw)
{
	return cpg_mstp_clock_endisable(hw, true);
}

static void cpg_mstp_clock_disable(struct clk_hw *hw)
{
	cpg_mstp_clock_endisable(hw, false);
}

static int cpg_mstp_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct mstp_clock_group *group = clock->group;
	u32 value;

	if (group->mstpsr)
		value = cpg_mstp_read(group, group->mstpsr);
	else
		value = cpg_mstp_read(group, group->smstpcr);

	return !(value & BIT(clock->bit_index));
}

static const struct clk_ops cpg_mstp_clock_ops = {
	.enable = cpg_mstp_clock_enable,
	.disable = cpg_mstp_clock_disable,
	.is_enabled = cpg_mstp_clock_is_enabled,
};

static struct clk * __init cpg_mstp_clock_register(const char *name,
	const char *parent_name, unsigned int index,
	struct mstp_clock_group *group)
{
	struct clk_init_data init;
	struct mstp_clock *clock;
	struct clk *clk;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_mstp_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	/* INTC-SYS is the module clock of the GIC, and must not be disabled */
	if (!strcmp(name, "intc-sys")) {
		pr_debug("MSTP %s setting CLK_IS_CRITICAL\n", name);
		init.flags |= CLK_IS_CRITICAL;
	}
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->bit_index = index;
	clock->group = group;
	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);

	if (IS_ERR(clk))
		kfree(clock);

	return clk;
}

static void __init cpg_mstp_clocks_init(struct device_node *np)
{
	struct mstp_clock_group *group;
	const char *idxname;
	struct clk **clks;
	unsigned int i;

	group = kzalloc(struct_size(group, clks, MSTP_MAX_CLOCKS), GFP_KERNEL);
	if (!group)
		return;

	clks = group->clks;
	spin_lock_init(&group->lock);
	group->data.clks = clks;

	group->smstpcr = of_iomap(np, 0);
	group->mstpsr = of_iomap(np, 1);

	if (group->smstpcr == NULL) {
		pr_err("%s: failed to remap SMSTPCR\n", __func__);
		kfree(group);
		return;
	}

	if (of_device_is_compatible(np, "renesas,r7s72100-mstp-clocks"))
		group->width_8bit = true;

	for (i = 0; i < MSTP_MAX_CLOCKS; ++i)
		clks[i] = ERR_PTR(-ENOENT);

	if (of_find_property(np, "clock-indices", &i))
		idxname = "clock-indices";
	else
		idxname = "renesas,clock-indices";

	for (i = 0; i < MSTP_MAX_CLOCKS; ++i) {
		const char *parent_name;
		const char *name;
		u32 clkidx;
		int ret;

		/* Skip clocks with no name. */
		ret = of_property_read_string_index(np, "clock-output-names",
						    i, &name);
		if (ret < 0 || strlen(name) == 0)
			continue;

		parent_name = of_clk_get_parent_name(np, i);
		ret = of_property_read_u32_index(np, idxname, i, &clkidx);
		if (parent_name == NULL || ret < 0)
			break;

		if (clkidx >= MSTP_MAX_CLOCKS) {
			pr_err("%s: invalid clock %pOFn %s index %u\n",
			       __func__, np, name, clkidx);
			continue;
		}

		clks[clkidx] = cpg_mstp_clock_register(name, parent_name,
						       clkidx, group);
		if (!IS_ERR(clks[clkidx])) {
			group->data.clk_num = max(group->data.clk_num,
						  clkidx + 1);
			/*
			 * Register a clkdev to let board code retrieve the
			 * clock by name and register aliases for non-DT
			 * devices.
			 *
			 * FIXME: Remove this when all devices that require a
			 * clock will be instantiated from DT.
			 */
			clk_register_clkdev(clks[clkidx], name, NULL);
		} else {
			pr_err("%s: failed to register %pOFn %s clock (%ld)\n",
			       __func__, np, name, PTR_ERR(clks[clkidx]));
		}
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &group->data);
}
CLK_OF_DECLARE(cpg_mstp_clks, "renesas,cpg-mstp-clocks", cpg_mstp_clocks_init);

int cpg_mstp_attach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	struct clk *clk;
	int i = 0;
	int error;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (of_device_is_compatible(clkspec.np,
					    "renesas,cpg-mstp-clocks"))
			goto found;

		/* BSC on r8a73a4/sh73a0 uses zb_clk instead of an mstp clock */
		if (of_node_name_eq(clkspec.np, "zb_clk"))
			goto found;

		of_node_put(clkspec.np);
		i++;
	}

	return 0;

found:
	clk = of_clk_get_from_provider(&clkspec);
	of_node_put(clkspec.np);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	error = pm_clk_create(dev);
	if (error)
		goto fail_put;

	error = pm_clk_add_clk(dev, clk);
	if (error)
		goto fail_destroy;

	return 0;

fail_destroy:
	pm_clk_destroy(dev);
fail_put:
	clk_put(clk);
	return error;
}

void cpg_mstp_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

void __init cpg_mstp_add_clk_domain(struct device_node *np)
{
	struct generic_pm_domain *pd;
	u32 ncells;

	if (of_property_read_u32(np, "#power-domain-cells", &ncells)) {
		pr_warn("%pOF lacks #power-domain-cells\n", np);
		return;
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return;

	pd->name = np->name;
	pd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		    GENPD_FLAG_ACTIVE_WAKEUP;
	pd->attach_dev = cpg_mstp_attach_dev;
	pd->detach_dev = cpg_mstp_detach_dev;
	pm_genpd_init(pd, &pm_domain_always_on_gov, false);

	of_genpd_add_provider_simple(np, pd);
}
