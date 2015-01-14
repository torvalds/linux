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

static int rmobile_pd_power_down(struct generic_pm_domain *genpd)
{
	struct rmobile_pm_domain *rmobile_pd = to_rmobile_pd(genpd);
	unsigned int mask;

	if (rmobile_pd->bit_shift == ~0)
		return -EBUSY;

	mask = 1 << rmobile_pd->bit_shift;
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

	mask = 1 << rmobile_pd->bit_shift;
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

static int rmobile_pd_attach_dev(struct generic_pm_domain *domain,
				 struct device *dev)
{
	int error;

	error = pm_clk_create(dev);
	if (error) {
		dev_err(dev, "pm_clk_create failed %d\n", error);
		return error;
	}

	error = pm_clk_add(dev, NULL);
	if (error) {
		dev_err(dev, "pm_clk_add failed %d\n", error);
		goto fail;
	}

	return 0;

fail:
	pm_clk_destroy(dev);
	return error;
}

static void rmobile_pd_detach_dev(struct generic_pm_domain *domain,
				  struct device *dev)
{
	pm_clk_destroy(dev);
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
	genpd->attach_dev		= rmobile_pd_attach_dev;
	genpd->detach_dev		= rmobile_pd_detach_dev;
	__rmobile_pd_power_up(rmobile_pd, false);
}

#ifdef CONFIG_ARCH_SHMOBILE_LEGACY

void rmobile_init_domains(struct rmobile_pm_domain domains[], int num)
{
	int j;

	for (j = 0; j < num; j++)
		rmobile_init_pm_domain(&domains[j]);
}

void rmobile_add_device_to_domain_td(const char *domain_name,
				     struct platform_device *pdev,
				     struct gpd_timing_data *td)
{
	struct device *dev = &pdev->dev;

	__pm_genpd_name_add_device(domain_name, dev, td);
}

void rmobile_add_devices_to_domains(struct pm_domain_device data[],
				    int size)
{
	struct gpd_timing_data latencies = {
		.stop_latency_ns = DEFAULT_DEV_LATENCY_NS,
		.start_latency_ns = DEFAULT_DEV_LATENCY_NS,
		.save_state_latency_ns = DEFAULT_DEV_LATENCY_NS,
		.restore_state_latency_ns = DEFAULT_DEV_LATENCY_NS,
	};
	int j;

	for (j = 0; j < size; j++)
		rmobile_add_device_to_domain_td(data[j].domain_name,
						data[j].pdev, &latencies);
}

#else /* !CONFIG_ARCH_SHMOBILE_LEGACY */

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

#define MAX_NUM_CPU_PDS		8

static unsigned int num_cpu_pds __initdata;
static struct device_node *cpu_pds[MAX_NUM_CPU_PDS] __initdata;
static struct device_node *console_pd __initdata;
static struct device_node *debug_pd __initdata;

static void __init get_special_pds(void)
{
	struct device_node *np, *pd;
	unsigned int i;

	/* PM domains containing CPUs */
	for_each_node_by_type(np, "cpu") {
		pd = of_parse_phandle(np, "power-domains", 0);
		if (!pd)
			continue;

		for (i = 0; i < num_cpu_pds; i++)
			if (pd == cpu_pds[i])
				break;

		if (i < num_cpu_pds) {
			of_node_put(pd);
			continue;
		}

		if (num_cpu_pds == MAX_NUM_CPU_PDS) {
			pr_warn("Too many CPU PM domains\n");
			of_node_put(pd);
			continue;
		}

		cpu_pds[num_cpu_pds++] = pd;
	}

	/* PM domain containing console */
	if (of_stdout)
		console_pd = of_parse_phandle(of_stdout, "power-domains", 0);

	/* PM domain containing Coresight-ETM */
	np = of_find_compatible_node(NULL, NULL, "arm,coresight-etm3x");
	if (np) {
		debug_pd = of_parse_phandle(np, "power-domains", 0);
		of_node_put(np);
	}
}

static void __init put_special_pds(void)
{
	unsigned int i;

	for (i = 0; i < num_cpu_pds; i++)
		of_node_put(cpu_pds[i]);
	of_node_put(console_pd);
	of_node_put(debug_pd);
}

static bool __init pd_contains_cpu(const struct device_node *pd)
{
	unsigned int i;

	for (i = 0; i < num_cpu_pds; i++)
		if (pd == cpu_pds[i])
			return true;

	return false;
}

static void __init rmobile_setup_pm_domain(struct device_node *np,
					   struct rmobile_pm_domain *pd)
{
	const char *name = pd->genpd.name;

	if (pd_contains_cpu(np)) {
		/*
		 * This domain contains the CPU core and therefore it should
		 * only be turned off if the CPU is not in use.
		 */
		pr_debug("PM domain %s contains CPU\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_busy;
	} else if (np == console_pd) {
		pr_debug("PM domain %s contains serial console\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_console;
	} else if (np == debug_pd) {
		/*
		 * This domain contains the Coresight-ETM hardware block and
		 * therefore it should only be turned off if the debug module
		 * is not in use.
		 */
		pr_debug("PM domain %s contains Coresight-ETM\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->suspend = rmobile_pd_suspend_busy;
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
		if (!pd)
			return -ENOMEM;

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

#endif /* !CONFIG_ARCH_SHMOBILE_LEGACY */
