// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *   Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * Baikal-T1 CCU PLL clocks driver
 */

#define pr_fmt(fmt) "bt1-ccu-pll: " fmt

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/ioport.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/bt1-ccu.h>

#include "ccu-pll.h"

#define CCU_CPU_PLL_BASE		0x000
#define CCU_SATA_PLL_BASE		0x008
#define CCU_DDR_PLL_BASE		0x010
#define CCU_PCIE_PLL_BASE		0x018
#define CCU_ETH_PLL_BASE		0x020

#define CCU_PLL_INFO(_id, _name, _pname, _base, _flags)	\
	{						\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _pname,			\
		.base = _base,				\
		.flags = _flags				\
	}

#define CCU_PLL_NUM			ARRAY_SIZE(pll_info)

struct ccu_pll_info {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned int base;
	unsigned long flags;
};

/*
 * Alas we have to mark all PLLs as critical. CPU and DDR PLLs are sources of
 * CPU cores and DDR controller reference clocks, due to which they obviously
 * shouldn't be ever gated. SATA and PCIe PLLs are the parents of APB-bus and
 * DDR controller AXI-bus clocks. If they are gated the system will be
 * unusable. Moreover disabling SATA and Ethernet PLLs causes automatic reset
 * of the corresponding subsystems. So until we aren't ready to re-initialize
 * all the devices consuming those PLLs, they will be marked as critical too.
 */
static const struct ccu_pll_info pll_info[] = {
	CCU_PLL_INFO(CCU_CPU_PLL, "cpu_pll", "ref_clk", CCU_CPU_PLL_BASE,
		     CLK_IS_CRITICAL),
	CCU_PLL_INFO(CCU_SATA_PLL, "sata_pll", "ref_clk", CCU_SATA_PLL_BASE,
		     CLK_IS_CRITICAL | CLK_SET_RATE_GATE),
	CCU_PLL_INFO(CCU_DDR_PLL, "ddr_pll", "ref_clk", CCU_DDR_PLL_BASE,
		     CLK_IS_CRITICAL | CLK_SET_RATE_GATE),
	CCU_PLL_INFO(CCU_PCIE_PLL, "pcie_pll", "ref_clk", CCU_PCIE_PLL_BASE,
		     CLK_IS_CRITICAL),
	CCU_PLL_INFO(CCU_ETH_PLL, "eth_pll", "ref_clk", CCU_ETH_PLL_BASE,
		     CLK_IS_CRITICAL | CLK_SET_RATE_GATE)
};

struct ccu_pll_data {
	struct device_node *np;
	struct regmap *sys_regs;
	struct ccu_pll *plls[CCU_PLL_NUM];
};

static struct ccu_pll *ccu_pll_find_desc(struct ccu_pll_data *data,
					 unsigned int clk_id)
{
	struct ccu_pll *pll;
	int idx;

	for (idx = 0; idx < CCU_PLL_NUM; ++idx) {
		pll = data->plls[idx];
		if (pll && pll->id == clk_id)
			return pll;
	}

	return ERR_PTR(-EINVAL);
}

static struct ccu_pll_data *ccu_pll_create_data(struct device_node *np)
{
	struct ccu_pll_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->np = np;

	return data;
}

static void ccu_pll_free_data(struct ccu_pll_data *data)
{
	kfree(data);
}

static int ccu_pll_find_sys_regs(struct ccu_pll_data *data)
{
	data->sys_regs = syscon_node_to_regmap(data->np->parent);
	if (IS_ERR(data->sys_regs)) {
		pr_err("Failed to find syscon regs for '%s'\n",
			of_node_full_name(data->np));
		return PTR_ERR(data->sys_regs);
	}

	return 0;
}

static struct clk_hw *ccu_pll_of_clk_hw_get(struct of_phandle_args *clkspec,
					    void *priv)
{
	struct ccu_pll_data *data = priv;
	struct ccu_pll *pll;
	unsigned int clk_id;

	clk_id = clkspec->args[0];
	pll = ccu_pll_find_desc(data, clk_id);
	if (IS_ERR(pll)) {
		pr_info("Invalid PLL clock ID %d specified\n", clk_id);
		return ERR_CAST(pll);
	}

	return ccu_pll_get_clk_hw(pll);
}

static int ccu_pll_clk_register(struct ccu_pll_data *data)
{
	int idx, ret;

	for (idx = 0; idx < CCU_PLL_NUM; ++idx) {
		const struct ccu_pll_info *info = &pll_info[idx];
		struct ccu_pll_init_data init = {0};

		init.id = info->id;
		init.name = info->name;
		init.parent_name = info->parent_name;
		init.base = info->base;
		init.sys_regs = data->sys_regs;
		init.np = data->np;
		init.flags = info->flags;

		data->plls[idx] = ccu_pll_hw_register(&init);
		if (IS_ERR(data->plls[idx])) {
			ret = PTR_ERR(data->plls[idx]);
			pr_err("Couldn't register PLL hw '%s'\n",
				init.name);
			goto err_hw_unregister;
		}
	}

	ret = of_clk_add_hw_provider(data->np, ccu_pll_of_clk_hw_get, data);
	if (ret) {
		pr_err("Couldn't register PLL provider of '%s'\n",
			of_node_full_name(data->np));
		goto err_hw_unregister;
	}

	return 0;

err_hw_unregister:
	for (--idx; idx >= 0; --idx)
		ccu_pll_hw_unregister(data->plls[idx]);

	return ret;
}

static __init void ccu_pll_init(struct device_node *np)
{
	struct ccu_pll_data *data;
	int ret;

	data = ccu_pll_create_data(np);
	if (IS_ERR(data))
		return;

	ret = ccu_pll_find_sys_regs(data);
	if (ret)
		goto err_free_data;

	ret = ccu_pll_clk_register(data);
	if (ret)
		goto err_free_data;

	return;

err_free_data:
	ccu_pll_free_data(data);
}
CLK_OF_DECLARE(ccu_pll, "baikal,bt1-ccu-pll", ccu_pll_init);
