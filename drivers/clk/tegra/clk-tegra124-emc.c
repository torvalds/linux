// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/clk/tegra/clk-emc.c
 *
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/string.h>

#include <soc/tegra/fuse.h>

#include "clk.h"

#define CLK_SOURCE_EMC 0x19c

#define CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR_SHIFT 0
#define CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR_MASK 0xff
#define CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR(x) (((x) & CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR_MASK) << \
					      CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR_SHIFT)

#define CLK_SOURCE_EMC_EMC_2X_CLK_SRC_SHIFT 29
#define CLK_SOURCE_EMC_EMC_2X_CLK_SRC_MASK 0x7
#define CLK_SOURCE_EMC_EMC_2X_CLK_SRC(x) (((x) & CLK_SOURCE_EMC_EMC_2X_CLK_SRC_MASK) << \
					  CLK_SOURCE_EMC_EMC_2X_CLK_SRC_SHIFT)

static const char * const emc_parent_clk_names[] = {
	"pll_m", "pll_c", "pll_p", "clk_m", "pll_m_ud",
	"pll_c2", "pll_c3", "pll_c_ud"
};

/*
 * List of clock sources for various parents the EMC clock can have.
 * When we change the timing to a timing with a parent that has the same
 * clock source as the current parent, we must first change to a backup
 * timing that has a different clock source.
 */

#define EMC_SRC_PLL_M 0
#define EMC_SRC_PLL_C 1
#define EMC_SRC_PLL_P 2
#define EMC_SRC_CLK_M 3
#define EMC_SRC_PLL_C2 4
#define EMC_SRC_PLL_C3 5

static const char emc_parent_clk_sources[] = {
	EMC_SRC_PLL_M, EMC_SRC_PLL_C, EMC_SRC_PLL_P, EMC_SRC_CLK_M,
	EMC_SRC_PLL_M, EMC_SRC_PLL_C2, EMC_SRC_PLL_C3, EMC_SRC_PLL_C
};

struct emc_timing {
	unsigned long rate, parent_rate;
	u8 parent_index;
	struct clk *parent;
	u32 ram_code;
};

struct tegra_clk_emc {
	struct clk_hw hw;
	void __iomem *clk_regs;
	struct clk *prev_parent;
	bool changing_timing;

	struct device_node *emc_node;
	struct tegra_emc *emc;

	int num_timings;
	struct emc_timing *timings;
	spinlock_t *lock;

	tegra124_emc_prepare_timing_change_cb *prepare_timing_change;
	tegra124_emc_complete_timing_change_cb *complete_timing_change;
};

/* Common clock framework callback implementations */

static unsigned long emc_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct tegra_clk_emc *tegra;
	u32 val, div;

	tegra = container_of(hw, struct tegra_clk_emc, hw);

	/*
	 * CCF wrongly assumes that the parent won't change during set_rate,
	 * so get the parent rate explicitly.
	 */
	parent_rate = clk_hw_get_rate(clk_hw_get_parent(hw));

	val = readl(tegra->clk_regs + CLK_SOURCE_EMC);
	div = val & CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR_MASK;

	return parent_rate / (div + 2) * 2;
}

/*
 * Rounds up unless no higher rate exists, in which case down. This way is
 * safer since things have EMC rate floors. Also don't touch parent_rate
 * since we don't want the CCF to play with our parent clocks.
 */
static int emc_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct tegra_clk_emc *tegra;
	u8 ram_code = tegra_read_ram_code();
	struct emc_timing *timing = NULL;
	int i, k, t;

	tegra = container_of(hw, struct tegra_clk_emc, hw);

	for (k = 0; k < tegra->num_timings; k++) {
		if (tegra->timings[k].ram_code == ram_code)
			break;
	}

	for (t = k; t < tegra->num_timings; t++) {
		if (tegra->timings[t].ram_code != ram_code)
			break;
	}

	for (i = k; i < t; i++) {
		timing = tegra->timings + i;

		if (timing->rate < req->rate && i != t - 1)
			continue;

		if (timing->rate > req->max_rate) {
			i = max(i, k + 1);
			req->rate = tegra->timings[i - 1].rate;
			return 0;
		}

		if (timing->rate < req->min_rate)
			continue;

		req->rate = timing->rate;
		return 0;
	}

	if (timing) {
		req->rate = timing->rate;
		return 0;
	}

	req->rate = clk_hw_get_rate(hw);
	return 0;
}

static u8 emc_get_parent(struct clk_hw *hw)
{
	struct tegra_clk_emc *tegra;
	u32 val;

	tegra = container_of(hw, struct tegra_clk_emc, hw);

	val = readl(tegra->clk_regs + CLK_SOURCE_EMC);

	return (val >> CLK_SOURCE_EMC_EMC_2X_CLK_SRC_SHIFT)
		& CLK_SOURCE_EMC_EMC_2X_CLK_SRC_MASK;
}

static struct tegra_emc *emc_ensure_emc_driver(struct tegra_clk_emc *tegra)
{
	struct platform_device *pdev;

	if (tegra->emc)
		return tegra->emc;

	if (!tegra->prepare_timing_change || !tegra->complete_timing_change)
		return NULL;

	if (!tegra->emc_node)
		return NULL;

	pdev = of_find_device_by_node(tegra->emc_node);
	if (!pdev) {
		pr_err("%s: could not get external memory controller\n",
		       __func__);
		return NULL;
	}

	of_node_put(tegra->emc_node);
	tegra->emc_node = NULL;

	tegra->emc = platform_get_drvdata(pdev);
	if (!tegra->emc) {
		put_device(&pdev->dev);
		pr_err("%s: cannot find EMC driver\n", __func__);
		return NULL;
	}

	return tegra->emc;
}

static int emc_set_timing(struct tegra_clk_emc *tegra,
			  struct emc_timing *timing)
{
	int err;
	u8 div;
	u32 car_value;
	unsigned long flags = 0;
	struct tegra_emc *emc = emc_ensure_emc_driver(tegra);

	if (!emc)
		return -ENOENT;

	pr_debug("going to rate %ld prate %ld p %s\n", timing->rate,
		 timing->parent_rate, __clk_get_name(timing->parent));

	if (emc_get_parent(&tegra->hw) == timing->parent_index &&
	    clk_get_rate(timing->parent) != timing->parent_rate) {
		WARN_ONCE(1, "parent %s rate mismatch %lu %lu\n",
			  __clk_get_name(timing->parent),
			  clk_get_rate(timing->parent),
			  timing->parent_rate);
		return -EINVAL;
	}

	tegra->changing_timing = true;

	err = clk_set_rate(timing->parent, timing->parent_rate);
	if (err) {
		pr_err("cannot change parent %s rate to %ld: %d\n",
		       __clk_get_name(timing->parent), timing->parent_rate,
		       err);

		return err;
	}

	err = clk_prepare_enable(timing->parent);
	if (err) {
		pr_err("cannot enable parent clock: %d\n", err);
		return err;
	}

	div = timing->parent_rate / (timing->rate / 2) - 2;

	err = tegra->prepare_timing_change(emc, timing->rate);
	if (err) {
		clk_disable_unprepare(timing->parent);
		return err;
	}

	spin_lock_irqsave(tegra->lock, flags);

	car_value = readl(tegra->clk_regs + CLK_SOURCE_EMC);

	car_value &= ~CLK_SOURCE_EMC_EMC_2X_CLK_SRC(~0);
	car_value |= CLK_SOURCE_EMC_EMC_2X_CLK_SRC(timing->parent_index);

	car_value &= ~CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR(~0);
	car_value |= CLK_SOURCE_EMC_EMC_2X_CLK_DIVISOR(div);

	writel(car_value, tegra->clk_regs + CLK_SOURCE_EMC);

	spin_unlock_irqrestore(tegra->lock, flags);

	tegra->complete_timing_change(emc, timing->rate);

	clk_hw_reparent(&tegra->hw, __clk_get_hw(timing->parent));
	clk_disable_unprepare(tegra->prev_parent);

	tegra->prev_parent = timing->parent;
	tegra->changing_timing = false;

	return 0;
}

/*
 * Get backup timing to use as an intermediate step when a change between
 * two timings with the same clock source has been requested. First try to
 * find a timing with a higher clock rate to avoid a rate below any set rate
 * floors. If that is not possible, find a lower rate.
 */
static struct emc_timing *get_backup_timing(struct tegra_clk_emc *tegra,
					    int timing_index)
{
	int i;
	u32 ram_code = tegra_read_ram_code();
	struct emc_timing *timing;

	for (i = timing_index+1; i < tegra->num_timings; i++) {
		timing = tegra->timings + i;
		if (timing->ram_code != ram_code)
			break;

		if (emc_parent_clk_sources[timing->parent_index] !=
		    emc_parent_clk_sources[
		      tegra->timings[timing_index].parent_index])
			return timing;
	}

	for (i = timing_index-1; i >= 0; --i) {
		timing = tegra->timings + i;
		if (timing->ram_code != ram_code)
			break;

		if (emc_parent_clk_sources[timing->parent_index] !=
		    emc_parent_clk_sources[
		      tegra->timings[timing_index].parent_index])
			return timing;
	}

	return NULL;
}

static int emc_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct tegra_clk_emc *tegra;
	struct emc_timing *timing = NULL;
	int i, err;
	u32 ram_code = tegra_read_ram_code();

	tegra = container_of(hw, struct tegra_clk_emc, hw);

	if (clk_hw_get_rate(hw) == rate)
		return 0;

	/*
	 * When emc_set_timing changes the parent rate, CCF will propagate
	 * that downward to us, so ignore any set_rate calls while a rate
	 * change is already going on.
	 */
	if (tegra->changing_timing)
		return 0;

	for (i = 0; i < tegra->num_timings; i++) {
		if (tegra->timings[i].rate == rate &&
		    tegra->timings[i].ram_code == ram_code) {
			timing = tegra->timings + i;
			break;
		}
	}

	if (!timing) {
		pr_err("cannot switch to rate %ld without emc table\n", rate);
		return -EINVAL;
	}

	if (emc_parent_clk_sources[emc_get_parent(hw)] ==
	    emc_parent_clk_sources[timing->parent_index] &&
	    clk_get_rate(timing->parent) != timing->parent_rate) {
		/*
		 * Parent clock source not changed but parent rate has changed,
		 * need to temporarily switch to another parent
		 */

		struct emc_timing *backup_timing;

		backup_timing = get_backup_timing(tegra, i);
		if (!backup_timing) {
			pr_err("cannot find backup timing\n");
			return -EINVAL;
		}

		pr_debug("using %ld as backup rate when going to %ld\n",
			 backup_timing->rate, rate);

		err = emc_set_timing(tegra, backup_timing);
		if (err) {
			pr_err("cannot set backup timing: %d\n", err);
			return err;
		}
	}

	return emc_set_timing(tegra, timing);
}

/* Initialization and deinitialization */

static int load_one_timing_from_dt(struct tegra_clk_emc *tegra,
				   struct emc_timing *timing,
				   struct device_node *node)
{
	int err, i;
	u32 tmp;

	err = of_property_read_u32(node, "clock-frequency", &tmp);
	if (err) {
		pr_err("timing %pOF: failed to read rate\n", node);
		return err;
	}

	timing->rate = tmp;

	err = of_property_read_u32(node, "nvidia,parent-clock-frequency", &tmp);
	if (err) {
		pr_err("timing %pOF: failed to read parent rate\n", node);
		return err;
	}

	timing->parent_rate = tmp;

	timing->parent = of_clk_get_by_name(node, "emc-parent");
	if (IS_ERR(timing->parent)) {
		pr_err("timing %pOF: failed to get parent clock\n", node);
		return PTR_ERR(timing->parent);
	}

	timing->parent_index = 0xff;
	i = match_string(emc_parent_clk_names, ARRAY_SIZE(emc_parent_clk_names),
			 __clk_get_name(timing->parent));
	if (i < 0) {
		pr_err("timing %pOF: %s is not a valid parent\n",
		       node, __clk_get_name(timing->parent));
		clk_put(timing->parent);
		return -EINVAL;
	}

	timing->parent_index = i;
	return 0;
}

static int cmp_timings(const void *_a, const void *_b)
{
	const struct emc_timing *a = _a;
	const struct emc_timing *b = _b;

	if (a->rate < b->rate)
		return -1;
	else if (a->rate == b->rate)
		return 0;
	else
		return 1;
}

static int load_timings_from_dt(struct tegra_clk_emc *tegra,
				struct device_node *node,
				u32 ram_code)
{
	struct emc_timing *timings_ptr;
	struct device_node *child;
	int child_count = of_get_child_count(node);
	int i = 0, err;
	size_t size;

	size = (tegra->num_timings + child_count) * sizeof(struct emc_timing);

	tegra->timings = krealloc(tegra->timings, size, GFP_KERNEL);
	if (!tegra->timings)
		return -ENOMEM;

	timings_ptr = tegra->timings + tegra->num_timings;
	tegra->num_timings += child_count;

	for_each_child_of_node(node, child) {
		struct emc_timing *timing = timings_ptr + (i++);

		err = load_one_timing_from_dt(tegra, timing, child);
		if (err) {
			of_node_put(child);
			kfree(tegra->timings);
			return err;
		}

		timing->ram_code = ram_code;
	}

	sort(timings_ptr, child_count, sizeof(struct emc_timing),
	     cmp_timings, NULL);

	return 0;
}

static const struct clk_ops tegra_clk_emc_ops = {
	.recalc_rate = emc_recalc_rate,
	.determine_rate = emc_determine_rate,
	.set_rate = emc_set_rate,
	.get_parent = emc_get_parent,
};

struct clk *tegra124_clk_register_emc(void __iomem *base, struct device_node *np,
				      spinlock_t *lock)
{
	struct tegra_clk_emc *tegra;
	struct clk_init_data init;
	struct device_node *node;
	u32 node_ram_code;
	struct clk *clk;
	int err;

	tegra = kcalloc(1, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return ERR_PTR(-ENOMEM);

	tegra->clk_regs = base;
	tegra->lock = lock;

	tegra->num_timings = 0;

	for_each_child_of_node(np, node) {
		err = of_property_read_u32(node, "nvidia,ram-code",
					   &node_ram_code);
		if (err)
			continue;

		/*
		 * Store timings for all ram codes as we cannot read the
		 * fuses until the apbmisc driver is loaded.
		 */
		err = load_timings_from_dt(tegra, node, node_ram_code);
		if (err) {
			of_node_put(node);
			kfree(tegra);
			return ERR_PTR(err);
		}
	}

	if (tegra->num_timings == 0)
		pr_warn("%s: no memory timings registered\n", __func__);

	tegra->emc_node = of_parse_phandle(np,
			"nvidia,external-memory-controller", 0);
	if (!tegra->emc_node)
		pr_warn("%s: couldn't find node for EMC driver\n", __func__);

	init.name = "emc";
	init.ops = &tegra_clk_emc_ops;
	init.flags = CLK_IS_CRITICAL;
	init.parent_names = emc_parent_clk_names;
	init.num_parents = ARRAY_SIZE(emc_parent_clk_names);

	tegra->hw.init = &init;

	clk = clk_register(NULL, &tegra->hw);
	if (IS_ERR(clk))
		return clk;

	tegra->prev_parent = clk_hw_get_parent_by_index(
		&tegra->hw, emc_get_parent(&tegra->hw))->clk;
	tegra->changing_timing = false;

	/* Allow debugging tools to see the EMC clock */
	clk_register_clkdev(clk, "emc", "tegra-clk-debug");

	return clk;
};

void tegra124_clk_set_emc_callbacks(tegra124_emc_prepare_timing_change_cb *prep_cb,
				    tegra124_emc_complete_timing_change_cb *complete_cb)
{
	struct clk *clk = __clk_lookup("emc");
	struct tegra_clk_emc *tegra;
	struct clk_hw *hw;

	if (clk) {
		hw = __clk_get_hw(clk);
		tegra = container_of(hw, struct tegra_clk_emc, hw);

		tegra->prepare_timing_change = prep_cb;
		tegra->complete_timing_change = complete_cb;
	}
}
EXPORT_SYMBOL_GPL(tegra124_clk_set_emc_callbacks);

bool tegra124_clk_emc_driver_available(struct clk_hw *hw)
{
	struct tegra_clk_emc *tegra = container_of(hw, struct tegra_clk_emc, hw);

	return tegra->prepare_timing_change && tegra->complete_timing_change;
}
