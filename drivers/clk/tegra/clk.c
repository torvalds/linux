/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/clk/tegra.h>
#include <linux/reset-controller.h>

#include <soc/tegra/fuse.h>

#include "clk.h"

#define CLK_OUT_ENB_L			0x010
#define CLK_OUT_ENB_H			0x014
#define CLK_OUT_ENB_U			0x018
#define CLK_OUT_ENB_V			0x360
#define CLK_OUT_ENB_W			0x364
#define CLK_OUT_ENB_X			0x280
#define CLK_OUT_ENB_Y			0x298
#define CLK_OUT_ENB_SET_L		0x320
#define CLK_OUT_ENB_CLR_L		0x324
#define CLK_OUT_ENB_SET_H		0x328
#define CLK_OUT_ENB_CLR_H		0x32c
#define CLK_OUT_ENB_SET_U		0x330
#define CLK_OUT_ENB_CLR_U		0x334
#define CLK_OUT_ENB_SET_V		0x440
#define CLK_OUT_ENB_CLR_V		0x444
#define CLK_OUT_ENB_SET_W		0x448
#define CLK_OUT_ENB_CLR_W		0x44c
#define CLK_OUT_ENB_SET_X		0x284
#define CLK_OUT_ENB_CLR_X		0x288
#define CLK_OUT_ENB_SET_Y		0x29c
#define CLK_OUT_ENB_CLR_Y		0x2a0

#define RST_DEVICES_L			0x004
#define RST_DEVICES_H			0x008
#define RST_DEVICES_U			0x00C
#define RST_DEVICES_V			0x358
#define RST_DEVICES_W			0x35C
#define RST_DEVICES_X			0x28C
#define RST_DEVICES_Y			0x2a4
#define RST_DEVICES_SET_L		0x300
#define RST_DEVICES_CLR_L		0x304
#define RST_DEVICES_SET_H		0x308
#define RST_DEVICES_CLR_H		0x30c
#define RST_DEVICES_SET_U		0x310
#define RST_DEVICES_CLR_U		0x314
#define RST_DEVICES_SET_V		0x430
#define RST_DEVICES_CLR_V		0x434
#define RST_DEVICES_SET_W		0x438
#define RST_DEVICES_CLR_W		0x43c
#define RST_DEVICES_SET_X		0x290
#define RST_DEVICES_CLR_X		0x294
#define RST_DEVICES_SET_Y		0x2a8
#define RST_DEVICES_CLR_Y		0x2ac

/* Global data of Tegra CPU CAR ops */
static struct tegra_cpu_car_ops dummy_car_ops;
struct tegra_cpu_car_ops *tegra_cpu_car_ops = &dummy_car_ops;

int *periph_clk_enb_refcnt;
static int periph_banks;
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
	if (!clks)
		kfree(periph_clk_enb_refcnt);

	clk_num = num;

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

void __init tegra_init_from_table(struct tegra_clk_init_table *tbl,
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

void __init tegra_register_devclks(struct tegra_devclk *dev_clks, int num)
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

tegra_clk_apply_init_table_func tegra_clk_apply_init_table;

static int __init tegra_clocks_apply_init_table(void)
{
	if (!tegra_clk_apply_init_table)
		return 0;

	tegra_clk_apply_init_table();

	return 0;
}
arch_initcall(tegra_clocks_apply_init_table);
