/*
 * rmobile power management support
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_clock.h>
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
	unsigned int mask = 1 << rmobile_pd->bit_shift;

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
	unsigned int mask = 1 << rmobile_pd->bit_shift;
	unsigned int retry_count;
	int ret = 0;

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
