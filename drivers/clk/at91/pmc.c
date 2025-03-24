// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

#include <asm/proc-fns.h>

#include "pmc.h"

#define PMC_MAX_IDS 128
#define PMC_MAX_PCKS 8

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range)
{
	u32 min, max;
	int ret;

	ret = of_property_read_u32_index(np, propname, 0, &min);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, propname, 1, &max);
	if (ret)
		return ret;

	if (range) {
		range->min = min;
		range->max = max;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_at91_get_clk_range);

struct clk_hw *of_clk_hw_pmc_get(struct of_phandle_args *clkspec, void *data)
{
	unsigned int type = clkspec->args[0];
	unsigned int idx = clkspec->args[1];
	struct pmc_data *pmc_data = data;

	switch (type) {
	case PMC_TYPE_CORE:
		if (idx < pmc_data->ncore)
			return pmc_data->chws[idx];
		break;
	case PMC_TYPE_SYSTEM:
		if (idx < pmc_data->nsystem)
			return pmc_data->shws[idx];
		break;
	case PMC_TYPE_PERIPHERAL:
		if (idx < pmc_data->nperiph)
			return pmc_data->phws[idx];
		break;
	case PMC_TYPE_GCK:
		if (idx < pmc_data->ngck)
			return pmc_data->ghws[idx];
		break;
	case PMC_TYPE_PROGRAMMABLE:
		if (idx < pmc_data->npck)
			return pmc_data->pchws[idx];
		break;
	default:
		break;
	}

	pr_err("%s: invalid type (%u) or index (%u)\n", __func__, type, idx);

	return ERR_PTR(-EINVAL);
}

struct pmc_data *pmc_data_allocate(unsigned int ncore, unsigned int nsystem,
				   unsigned int nperiph, unsigned int ngck,
				   unsigned int npck)
{
	unsigned int num_clks = ncore + nsystem + nperiph + ngck + npck;
	struct pmc_data *pmc_data;

	pmc_data = kzalloc(struct_size(pmc_data, hwtable, num_clks),
			   GFP_KERNEL);
	if (!pmc_data)
		return NULL;

	pmc_data->ncore = ncore;
	pmc_data->chws = pmc_data->hwtable;

	pmc_data->nsystem = nsystem;
	pmc_data->shws = pmc_data->chws + ncore;

	pmc_data->nperiph = nperiph;
	pmc_data->phws = pmc_data->shws + nsystem;

	pmc_data->ngck = ngck;
	pmc_data->ghws = pmc_data->phws + nperiph;

	pmc_data->npck = npck;
	pmc_data->pchws = pmc_data->ghws + ngck;

	return pmc_data;
}

#ifdef CONFIG_PM

/* Address in SECURAM that say if we suspend to backup mode. */
static void __iomem *at91_pmc_backup_suspend;

static int at91_pmc_suspend(void)
{
	unsigned int backup;

	if (!at91_pmc_backup_suspend)
		return 0;

	backup = readl_relaxed(at91_pmc_backup_suspend);
	if (!backup)
		return 0;

	return clk_save_context();
}

static void at91_pmc_resume(void)
{
	unsigned int backup;

	if (!at91_pmc_backup_suspend)
		return;

	backup = readl_relaxed(at91_pmc_backup_suspend);
	if (!backup)
		return;

	clk_restore_context();
}

static struct syscore_ops pmc_syscore_ops = {
	.suspend = at91_pmc_suspend,
	.resume = at91_pmc_resume,
};

static const struct of_device_id pmc_dt_ids[] = {
	{ .compatible = "atmel,sama5d2-pmc" },
	{ .compatible = "microchip,sama7g5-pmc", },
	{ .compatible = "microchip,sama7d65-pmc", },
	{ /* sentinel */ }
};

static int __init pmc_register_ops(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, pmc_dt_ids);
	if (!np)
		return -ENODEV;

	if (!of_device_is_available(np)) {
		of_node_put(np);
		return -ENODEV;
	}
	of_node_put(np);

	np = of_find_compatible_node(NULL, NULL, "atmel,sama5d2-securam");
	if (!np)
		return -ENODEV;

	if (!of_device_is_available(np)) {
		of_node_put(np);
		return -ENODEV;
	}
	of_node_put(np);

	at91_pmc_backup_suspend = of_iomap(np, 0);
	if (!at91_pmc_backup_suspend) {
		pr_warn("%s(): unable to map securam\n", __func__);
		return -ENOMEM;
	}

	register_syscore_ops(&pmc_syscore_ops);

	return 0;
}
/* This has to happen before arch_initcall because of the tcb_clksrc driver */
postcore_initcall(pmc_register_ops);
#endif
