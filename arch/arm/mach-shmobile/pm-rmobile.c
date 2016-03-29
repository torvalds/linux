/*
 * rmobile power management support
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2014  Glider bvba
 *
 * based on pm-sh7372.c
 *  Copyright (C) 2011 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/clk/renesas.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_clock.h>
#include <linux/slab.h>

#include <asm/io.h>

#include "pm-rmobile.h"

/* SYSC */
#define SPDCR		0x08	/* SYS Power Down Control Register */
#define SWUCR		0x14	/* SYS Wakeup Control Register */
#define PSTR		0x80	/* Power Status Register */

#define PSTR_RETRIES	100
#define PSTR_DELAY_US	10

static inline
struct rmobile_pm_domain *to_rmobile_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rmobile_pm_domain, genpd);
}

static int rmobile_pd_power_down(struct generic_pm_domain *genpd)
{
	struct rmobile_pm_domain *rmobile_pd = to_rmobile_pd(genpd);
	unsigned int mask;

	if (rmobile_pd->bit_shift == ~0)
		return -EBUSY;

	mask = BIT(rmobile_pd->bit_shift);
	if (rmobile_pd->suspend) {
		int ret = rmobile_pd->suspend();

		if (ret)
			return ret;
	}

	if (__raw_readl(rmobile_pd->base + PSTR) & mask) {
		unsigned int retry_count;
		__raw_writel(mask, rmobile_pd->base + SPDCR);

		for (retry_count = PSTR_RETRIES; retry_count; retry_count--) {
			if (!(__raw_readl(rmobile_pd->base + SPDCR) & mask))
				break;
			cpu_relax();
		}
	}

	if (!rmobile_pd->no_debug)
		pr_debug("%s: Power off, 0x%08x -> PSTR = 0x%08x\n",
			 genpd->name, mask,
			 __raw_readl(rmobile_pd->base + PSTR));

	return 0;
}

static int __rmobile_pd_power_up(struct rmobile_pm_domain *rmobile_pd,
				 bool do_resume)
{
	unsigned int mask;
	unsigned int retry_count;
	int ret = 0;

	if (rmobile_pd->bit_shift == ~0)
		return 0;

	mask = BIT(rmobile_pd->bit_shift);
	if (__raw_readl(rmobile_pd->base + PSTR) & mask)
		goto out;

	__raw_writel(mask, rmobile_pd->base + SWUCR);

	for (retry_count = 2 * PSTR_RETRIES; retry_count; retry_count--) {
		if (!(__raw_readl(rmobile_pd->base + SWUCR) & mask))
			break;
		if (retry_count > PSTR_RETRIES)
			udelay(PSTR_DELAY_US);
		else
			cpu_relax();
	}
	if (!retry_count)
		ret = -EIO;

	if (!rmobile_pd->no_debug)
		pr_debug("%s: Power on, 0x%08x -> PSTR = 0x%08x\n",
			 rmobile_pd->genpd.name, mask,
			 __raw_readl(rmobile_pd->base + PSTR));

out:
	if (ret == 0 && rmobile_pd->resume && do_resume)
		rmobile_pd->resume();

	return ret;
}

static int rmobile_pd_power_up(struct generic_pm_domain *genpd)
{
	return __rmobile_pd_power_up(to_rmobile_pd(genpd), true);
}

static bool rmobile_pd_active_wakeup(struct device *dev)
{
	return true;
}

static void rmobile_init_pm_domain(struct rmobile_pm_domain *rmobile_pd)
{
	struct generic_pm_domain *genpd = &rmobile_pd->genpd;
	struct dev_power_governor *gov = rmobile_pd->gov;

	genpd->flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(genpd, gov ? : &simple_qos_governor, false);
	genpd->dev_ops.active_wakeup	= rmobile_pd_active_wakeup;
	genpd->power_off		= rmobile_pd_power_down;
	genpd->power_on			= rmobile_pd_power_up;
	genpd->attach_dev		= cpg_mstp_attach_dev;
	genpd->detach_dev		= cpg_mstp_detach_dev;
	__rmobile_pd_power_up(rmobile_pd, false);
}

static int rmobile_pd_suspend_busy(void)
{
	/*
	 * This domain should not be turned off.
	 */
	return -EBUSY;
}

static int rmobile_pd_suspend_console(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in this domain,
	 * hence keep the power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

enum pd_types {
	PD_NORMAL,
	PD_CPU,
	PD_CONSOLE,
	PD_DEBUG,
	PD_MEMCTL,
};

#define MAX_NUM_SPECIAL_PDS	16

static struct special_pd {
	struct device_node *pd;
	enum pd_types type;
} special_pds[MAX_NUM_SPECIAL_PDS] __initdata;

static unsigned int num_special_pds __initdata;

static const struct of_device_id special_ids[] __initconst = {
	{ .compatible = "arm,coresight-etm3x", .data = (void *)PD_DEBUG },
	{ .compatible = "renesas,dbsc-r8a73a4", .data = (void *)PD_MEMCTL, },
	{ .compatible = "renesas,dbsc3-r8a7740", .data = (void *)PD_MEMCTL, },
	{ .compatible = "renesas,sbsc-sh73a0", .data = (void *)PD_MEMCTL, },
	{ /* sentinel */ },
};

static void __init add_special_pd(struct device_node *np, enum pd_types type)
{
	unsigned int i;
	struct device_node *pd;

	pd = of_parse_phandle(np, "power-domains", 0);
	if (!pd)
		return;

	for (i = 0; i < num_special_pds; i++)
		if (pd == special_pds[i].pd && type == special_pds[i].type) {
			of_node_put(pd);
			return;
		}

	if (num_special_pds == ARRAY_SIZE(special_pds)) {
		pr_warn("Too many special PM domains\n");
		of_node_put(pd);
		return;
	}

	pr_debug("Special PM domain %s type %d for %s\n", pd->name, type,
		 np->full_name);

	special_pds[num_special_pds].pd = pd;
	special_pds[num_special_pds].type = type;
	num_special_pds++;
}

static void __init get_special_pds(void)
{
	struct device_node *np;
	const struct of_device_id *id;

	/* PM domains containing CPUs */
	for_each_node_by_type(np, "cpu")
		add_special_pd(np, PD_CPU);

	/* PM domain containing console */
	if (of_stdout)
		add_special_pd(of_stdout, PD_CONSOLE);

	/* PM domains containing other special devices */
	for_each_matching_node_and_match(np, special_ids, &id)
		add_special_pd(np, (enum pd_types)id->data);
}

static void __init put_special_pds(void)
{
	unsigned int i;

	for (i = 0; i < num_special_pds; i++)
		of_node_put(special_pds[i].pd);
}

static enum pd_types __init pd_type(const struct device_node *pd)
{
	unsigned int i;

	for (i = 0; i < num_special_pds; i++)
		if (pd == special_pds[i].pd)
			return special_pds[i].type;

	return PD_NORMAL;
}

static void __init rmobile_setup_pm_domain(struct device_node *np,
					   struct rmobile_pm_domain *pd)
{
	const char *name = pd->genpd.name;

	switch (pd_type(np)) {
	case PD_CPU:
		/*
		 * This domain contains the CPU core and therefore it should
		 * only be turned off if the CPU is not in use.
		 */
		pr_debug("PM domain %s contains CPU\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_busy;
		break;

	case PD_CONSOLE:
		pr_debug("PM domain %s contains serial console\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_console;
		break;

	case PD_DEBUG:
		/*
		 * This domain contains the Coresight-ETM hardware block and
		 * therefore it should only be turned off if the debug module
		 * is not in use.
		 */
		pr_debug("PM domain %s contains Coresight-ETM\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_busy;
		break;

	case PD_MEMCTL:
		/*
		 * This domain contains a memory-controller and therefore it
		 * should only be turned off if memory is not in use.
		 */
		pr_debug("PM domain %s contains MEMCTL\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_busy;
		break;

	case PD_NORMAL:
		break;
	}

	rmobile_init_pm_domain(pd);
}

static int __init rmobile_add_pm_domains(void __iomem *base,
					 struct device_node *parent,
					 struct generic_pm_domain *genpd_parent)
{
	struct device_node *np;

	for_each_child_of_node(parent, np) {
		struct rmobile_pm_domain *pd;
		u32 idx = ~0;

		if (of_property_read_u32(np, "reg", &idx)) {
			/* always-on domain */
		}

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			of_node_put(np);
			return -ENOMEM;
		}

		pd->genpd.name = np->name;
		pd->base = base;
		pd->bit_shift = idx;

		rmobile_setup_pm_domain(np, pd);
		if (genpd_parent)
			pm_genpd_add_subdomain(genpd_parent, &pd->genpd);
		of_genpd_add_provider_simple(np, &pd->genpd);

		rmobile_add_pm_domains(base, np, &pd->genpd);
	}
	return 0;
}

static int __init rmobile_init_pm_domains(void)
{
	struct device_node *np, *pmd;
	bool scanned = false;
	void __iomem *base;
	int ret = 0;

	for_each_compatible_node(np, NULL, "renesas,sysc-rmobile") {
		base = of_iomap(np, 0);
		if (!base) {
			pr_warn("%s cannot map reg 0\n", np->full_name);
			continue;
		}

		pmd = of_get_child_by_name(np, "pm-domains");
		if (!pmd) {
			pr_warn("%s lacks pm-domains node\n", np->full_name);
			continue;
		}

		if (!scanned) {
			/* Find PM domains containing special blocks */
			get_special_pds();
			scanned = true;
		}

		ret = rmobile_add_pm_domains(base, pmd, NULL);
		of_node_put(pmd);
		if (ret) {
			of_node_put(np);
			break;
		}
	}

	put_special_pds();

	return ret;
}

core_initcall(rmobile_init_pm_domains);
