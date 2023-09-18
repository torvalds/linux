// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk/tegra.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset-controller.h>
#include <linux/string_helpers.h>

#include <soc/tegra/fuse.h>

#include "clk.h"

/* Global data of Tegra CPU CAR ops */
static struct device_node *tegra_car_np;
static struct tegra_cpu_car_ops dummy_car_ops;
struct tegra_cpu_car_ops *tegra_cpu_car_ops = &dummy_car_ops;

int *periph_clk_enb_refcnt;
static int periph_banks;
static u32 *periph_state_ctx;
static struct clk **clks;
static int clk_num;
static struct clk_onecell_data clk_data;

/* Handlers for SoC-specific reset lines */
static int (*special_reset_assert)(unsigned long);
static int (*special_reset_deassert)(unsigned long);
static unsigned int num_special_reset;

static const struct tegra_clk_periph_regs periph_regs[] = {
	[0] = {
		.enb_reg = CLK_OUT_ENB_L,
		.enb_set_reg = CLK_OUT_ENB_SET_L,
		.enb_clr_reg = CLK_OUT_ENB_CLR_L,
		.rst_reg = RST_DEVICES_L,
		.rst_set_reg = RST_DEVICES_SET_L,
		.rst_clr_reg = RST_DEVICES_CLR_L,
	},
	[1] = {
		.enb_reg = CLK_OUT_ENB_H,
		.enb_set_reg = CLK_OUT_ENB_SET_H,
		.enb_clr_reg = CLK_OUT_ENB_CLR_H,
		.rst_reg = RST_DEVICES_H,
		.rst_set_reg = RST_DEVICES_SET_H,
		.rst_clr_reg = RST_DEVICES_CLR_H,
	},
	[2] = {
		.enb_reg = CLK_OUT_ENB_U,
		.enb_set_reg = CLK_OUT_ENB_SET_U,
		.enb_clr_reg = CLK_OUT_ENB_CLR_U,
		.rst_reg = RST_DEVICES_U,
		.rst_set_reg = RST_DEVICES_SET_U,
		.rst_clr_reg = RST_DEVICES_CLR_U,
	},
	[3] = {
		.enb_reg = CLK_OUT_ENB_V,
		.enb_set_reg = CLK_OUT_ENB_SET_V,
		.enb_clr_reg = CLK_OUT_ENB_CLR_V,
		.rst_reg = RST_DEVICES_V,
		.rst_set_reg = RST_DEVICES_SET_V,
		.rst_clr_reg = RST_DEVICES_CLR_V,
	},
	[4] = {
		.enb_reg = CLK_OUT_ENB_W,
		.enb_set_reg = CLK_OUT_ENB_SET_W,
		.enb_clr_reg = CLK_OUT_ENB_CLR_W,
		.rst_reg = RST_DEVICES_W,
		.rst_set_reg = RST_DEVICES_SET_W,
		.rst_clr_reg = RST_DEVICES_CLR_W,
	},
	[5] = {
		.enb_reg = CLK_OUT_ENB_X,
		.enb_set_reg = CLK_OUT_ENB_SET_X,
		.enb_clr_reg = CLK_OUT_ENB_CLR_X,
		.rst_reg = RST_DEVICES_X,
		.rst_set_reg = RST_DEVICES_SET_X,
		.rst_clr_reg = RST_DEVICES_CLR_X,
	},
	[6] = {
		.enb_reg = CLK_OUT_ENB_Y,
		.enb_set_reg = CLK_OUT_ENB_SET_Y,
		.enb_clr_reg = CLK_OUT_ENB_CLR_Y,
		.rst_reg = RST_DEVICES_Y,
		.rst_set_reg = RST_DEVICES_SET_Y,
		.rst_clr_reg = RST_DEVICES_CLR_Y,
	},
};

static void __iomem *clk_base;

static int tegra_clk_rst_assert(struct reset_controller_dev *rcdev,
		unsigned long id)
{
	/*
	 * If peripheral is on the APB bus then we must read the APB bus to
	 * flush the write operation in apb bus. This will avoid peripheral
	 * access after disabling clock. Since the reset driver has no
	 * knowledge of which reset IDs represent which devices, simply do
	 * this all the time.
	 */
	tegra_read_chipid();

	if (id < periph_banks * 32) {
		writel_relaxed(BIT(id % 32),
			       clk_base + periph_regs[id / 32].rst_set_reg);
		return 0;
	} else if (id < periph_banks * 32 + num_special_reset) {
		return special_reset_assert(id);
	}

	return -EINVAL;
}

static int tegra_clk_rst_deassert(struct reset_controller_dev *rcdev,
		unsigned long id)
{
	if (id < periph_banks * 32) {
		writel_relaxed(BIT(id % 32),
			       clk_base + periph_regs[id / 32].rst_clr_reg);
		return 0;
	} else if (id < periph_banks * 32 + num_special_reset) {
		return special_reset_deassert(id);
	}

	return -EINVAL;
}

static int tegra_clk_rst_reset(struct reset_controller_dev *rcdev,
		unsigned long id)
{
	int err;

	err = tegra_clk_rst_assert(rcdev, id);
	if (err)
		return err;

	udelay(1);

	return tegra_clk_rst_deassert(rcdev, id);
}

const struct tegra_clk_periph_regs *get_reg_bank(int clkid)
{
	int reg_bank = clkid / 32;

	if (reg_bank < periph_banks)
		return &periph_regs[reg_bank];
	else {
		WARN_ON(1);
		return NULL;
	}
}

void tegra_clk_set_pllp_out_cpu(bool enable)
{
	u32 val;

	val = readl_relaxed(clk_base + CLK_OUT_ENB_Y);
	if (enable)
		val |= CLK_ENB_PLLP_OUT_CPU;
	else
		val &= ~CLK_ENB_PLLP_OUT_CPU;

	writel_relaxed(val, clk_base + CLK_OUT_ENB_Y);
}

void tegra_clk_periph_suspend(void)
{
	unsigned int i, idx;

	idx = 0;
	for (i = 0; i < periph_banks; i++, idx++)
		periph_state_ctx[idx] =
			readl_relaxed(clk_base + periph_regs[i].enb_reg);

	for (i = 0; i < periph_banks; i++, idx++)
		periph_state_ctx[idx] =
			readl_relaxed(clk_base + periph_regs[i].rst_reg);
}

void tegra_clk_periph_resume(void)
{
	unsigned int i, idx;

	idx = 0;
	for (i = 0; i < periph_banks; i++, idx++)
		writel_relaxed(periph_state_ctx[idx],
			       clk_base + periph_regs[i].enb_reg);
	/*
	 * All non-boot peripherals will be in reset state on resume.
	 * Wait for 5us of reset propagation delay before de-asserting
	 * the peripherals based on the saved context.
	 */
	fence_udelay(5, clk_base);

	for (i = 0; i < periph_banks; i++, idx++)
		writel_relaxed(periph_state_ctx[idx],
			       clk_base + periph_regs[i].rst_reg);

	fence_udelay(2, clk_base);
}

static int tegra_clk_periph_ctx_init(int banks)
{
	periph_state_ctx = kcalloc(2 * banks, sizeof(*periph_state_ctx),
				   GFP_KERNEL);
	if (!periph_state_ctx)
		return -ENOMEM;

	return 0;
}

struct clk ** __init tegra_clk_init(void __iomem *regs, int num, int banks)
{
	clk_base = regs;

	if (WARN_ON(banks > ARRAY_SIZE(periph_regs)))
		return NULL;

	periph_clk_enb_refcnt = kcalloc(32 * banks,
					sizeof(*periph_clk_enb_refcnt),
					GFP_KERNEL);
	if (!periph_clk_enb_refcnt)
		return NULL;

	periph_banks = banks;

	clks = kcalloc(num, sizeof(struct clk *), GFP_KERNEL);
	if (!clks) {
		kfree(periph_clk_enb_refcnt);
		return NULL;
	}

	clk_num = num;

	if (IS_ENABLED(CONFIG_PM_SLEEP)) {
		if (tegra_clk_periph_ctx_init(banks)) {
			kfree(periph_clk_enb_refcnt);
			kfree(clks);
			return NULL;
		}
	}

	return clks;
}

void __init tegra_init_dup_clks(struct tegra_clk_duplicate *dup_list,
				struct clk *clks[], int clk_max)
{
	struct clk *clk;

	for (; dup_list->clk_id < clk_max; dup_list++) {
		clk = clks[dup_list->clk_id];
		dup_list->lookup.clk = clk;
		clkdev_add(&dup_list->lookup);
	}
}

void tegra_init_from_table(struct tegra_clk_init_table *tbl,
			   struct clk *clks[], int clk_max)
{
	struct clk *clk;

	for (; tbl->clk_id < clk_max; tbl++) {
		clk = clks[tbl->clk_id];
		if (IS_ERR_OR_NULL(clk)) {
			pr_err("%s: invalid entry %ld in clks array for id %d\n",
			       __func__, PTR_ERR(clk), tbl->clk_id);
			WARN_ON(1);

			continue;
		}

		if (tbl->parent_id < clk_max) {
			struct clk *parent = clks[tbl->parent_id];
			if (clk_set_parent(clk, parent)) {
				pr_err("%s: Failed to set parent %s of %s\n",
				       __func__, __clk_get_name(parent),
				       __clk_get_name(clk));
				WARN_ON(1);
			}
		}

		if (tbl->rate)
			if (clk_set_rate(clk, tbl->rate)) {
				pr_err("%s: Failed to set rate %lu of %s\n",
				       __func__, tbl->rate,
				       __clk_get_name(clk));
				WARN_ON(1);
			}

		if (tbl->state)
			if (clk_prepare_enable(clk)) {
				pr_err("%s: Failed to enable %s\n", __func__,
				       __clk_get_name(clk));
				WARN_ON(1);
			}
	}
}

static const struct reset_control_ops rst_ops = {
	.assert = tegra_clk_rst_assert,
	.deassert = tegra_clk_rst_deassert,
	.reset = tegra_clk_rst_reset,
};

static struct reset_controller_dev rst_ctlr = {
	.ops = &rst_ops,
	.owner = THIS_MODULE,
	.of_reset_n_cells = 1,
};

void __init tegra_add_of_provider(struct device_node *np,
				  void *clk_src_onecell_get)
{
	int i;

	tegra_car_np = np;

	for (i = 0; i < clk_num; i++) {
		if (IS_ERR(clks[i])) {
			pr_err
			    ("Tegra clk %d: register failed with %ld\n",
			     i, PTR_ERR(clks[i]));
		}
		if (!clks[i])
			clks[i] = ERR_PTR(-EINVAL);
	}

	clk_data.clks = clks;
	clk_data.clk_num = clk_num;
	of_clk_add_provider(np, clk_src_onecell_get, &clk_data);

	rst_ctlr.of_node = np;
	rst_ctlr.nr_resets = periph_banks * 32 + num_special_reset;
	reset_controller_register(&rst_ctlr);
}

void __init tegra_init_special_resets(unsigned int num,
				      int (*assert)(unsigned long),
				      int (*deassert)(unsigned long))
{
	num_special_reset = num;
	special_reset_assert = assert;
	special_reset_deassert = deassert;
}

void tegra_register_devclks(struct tegra_devclk *dev_clks, int num)
{
	int i;

	for (i = 0; i < num; i++, dev_clks++)
		clk_register_clkdev(clks[dev_clks->dt_id], dev_clks->con_id,
				dev_clks->dev_id);

	for (i = 0; i < clk_num; i++) {
		if (!IS_ERR_OR_NULL(clks[i]))
			clk_register_clkdev(clks[i], __clk_get_name(clks[i]),
				"tegra-clk-debug");
	}
}

struct clk ** __init tegra_lookup_dt_id(int clk_id,
					struct tegra_clk *tegra_clk)
{
	if (tegra_clk[clk_id].present)
		return &clks[tegra_clk[clk_id].dt_id];
	else
		return NULL;
}

static struct device_node *tegra_clk_get_of_node(struct clk_hw *hw)
{
	struct device_node *np;
	char *node_name;

	node_name = kstrdup_and_replace(hw->init->name, '_', '-', GFP_KERNEL);
	if (!node_name)
		return NULL;

	for_each_child_of_node(tegra_car_np, np) {
		if (!strcmp(np->name, node_name))
			break;
	}

	kfree(node_name);

	return np;
}

struct clk *tegra_clk_dev_register(struct clk_hw *hw)
{
	struct platform_device *pdev, *parent;
	const char *dev_name = NULL;
	struct device *dev = NULL;
	struct device_node *np;

	np = tegra_clk_get_of_node(hw);

	if (!of_device_is_available(np))
		goto put_node;

	dev_name = kasprintf(GFP_KERNEL, "tegra_clk_%s", hw->init->name);
	if (!dev_name)
		goto put_node;

	parent = of_find_device_by_node(tegra_car_np);
	if (parent) {
		pdev = of_platform_device_create(np, dev_name, &parent->dev);
		put_device(&parent->dev);

		if (!pdev) {
			pr_err("%s: failed to create device for %pOF\n",
			       __func__, np);
			goto free_name;
		}

		dev = &pdev->dev;
		pm_runtime_enable(dev);
	} else {
		WARN(1, "failed to find device for %pOF\n", tegra_car_np);
	}

free_name:
	kfree(dev_name);
put_node:
	of_node_put(np);

	return clk_register(dev, hw);
}

tegra_clk_apply_init_table_func tegra_clk_apply_init_table;

static int __init tegra_clocks_apply_init_table(void)
{
	if (!tegra_clk_apply_init_table)
		return 0;

	tegra_clk_apply_init_table();

	return 0;
}
arch_initcall(tegra_clocks_apply_init_table);
