/*
 * Power domain support for Rockchip RK3368
 *
 * Copyright (C) 2014-2015 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/rockchip/pmu.h>
#include <linux/rockchip/cru.h>

#include "clk-ops.h"

static void __iomem *rk_pmu_base;

static u32 pmu_readl(u32 offset)
{
	return readl_relaxed(rk_pmu_base + (offset));
}

static void pmu_writel(u32 val, u32 offset)
{
	writel_relaxed(val, rk_pmu_base + (offset));
	dsb(sy);
}

static const u8 pmu_pd_map[] = {
	[PD_PERI] = 13,
	[PD_VIDEO] = 14,
	[PD_VIO] = 15,
	[PD_GPU_0] = 16,
	[PD_GPU_1] = 17,
};

static const u8 pmu_st_map[] = {
	[PD_PERI] = 12,
	[PD_VIDEO] = 13,
	[PD_VIO] = 14,
	[PD_GPU_0] = 15,
	[PD_GPU_1] = 16,
};

static bool rk3368_pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	/* 1'b0: power on, 1'b1: power off */
	return !(pmu_readl(RK3368_PMU_PWRDN_ST) & BIT(pmu_st_map[pd]));
}

static DEFINE_SPINLOCK(pmu_idle_lock);

static const u8 pmu_idle_map[] = {
	[IDLE_REQ_GPU] = 2,
	[IDLE_REQ_BUS] = 4,
	[IDLE_REQ_PERI] = 6,
	[IDLE_REQ_VIDEO] = 7,
	[IDLE_REQ_VIO] = 8,
};

static int rk3368_pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 bit = pmu_idle_map[req];
	u32 idle_mask = BIT(bit) | BIT(bit + 16);
	u32 idle_target = (idle << bit) | (idle << (bit + 16));
	u32 mask = BIT(bit);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pmu_idle_lock, flags);

	val = pmu_readl(RK3368_PMU_IDLE_REQ);
	if (idle)
		val |=	mask;
	else
		val &= ~mask;
	pmu_writel(val, RK3368_PMU_IDLE_REQ);
	dsb(sy);

	while ((pmu_readl(RK3368_PMU_IDLE_ST) & idle_mask) != idle_target)
		;

	spin_unlock_irqrestore(&pmu_idle_lock, flags);

	return 0;
}

static DEFINE_SPINLOCK(pmu_pd_lock);

static noinline void rk3368_do_pmu_set_power_domain
					(enum pmu_power_domain domain, bool on)
{
	u8 pd = pmu_pd_map[domain];
	u32 val = pmu_readl(RK3368_PMU_PWRDN_CON);

	if (on)
		val &= ~BIT(pd);
	else
		val |=	BIT(pd);

	pmu_writel(val, RK3368_PMU_PWRDN_CON);
	dsb(sy);

	while ((pmu_readl(RK3368_PMU_PWRDN_ST) & BIT(pmu_st_map[domain])) == on)
		;
}

static int rk3368_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);

	if (rk3368_pmu_power_domain_is_on(pd) == on)
		goto out;

	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO)
			rk3368_pmu_set_idle_request(IDLE_REQ_VIO, true);
		else if (pd == PD_VIDEO)
			rk3368_pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		else if (pd == PD_GPU_0)
			rk3368_pmu_set_idle_request(IDLE_REQ_GPU, true);
		else if (pd == PD_PERI)
			rk3368_pmu_set_idle_request(IDLE_REQ_PERI, true);
	}

	rk3368_do_pmu_set_power_domain(pd, on);

	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO)
			rk3368_pmu_set_idle_request(IDLE_REQ_VIO, false);
		else if (pd == PD_VIDEO)
			rk3368_pmu_set_idle_request(IDLE_REQ_VIDEO, false);
		else if (pd == PD_GPU_0)
			rk3368_pmu_set_idle_request(IDLE_REQ_GPU, false);
		else if (pd == PD_PERI)
			rk3368_pmu_set_idle_request(IDLE_REQ_PERI, false);
	}

out:
	spin_unlock_irqrestore(&pmu_pd_lock, flags);
	return 0;
}

static int rk3368_sys_set_power_domain(enum pmu_power_domain pd, bool on)
{
	u32 clks_ungating[RK3368_CRU_CLKGATES_CON_CNT];
	u32 clks_save[RK3368_CRU_CLKGATES_CON_CNT];
	u32 i, ret;

	for (i = 0; i < RK3368_CRU_CLKGATES_CON_CNT; i++) {
		clks_save[i] = cru_readl(RK3368_CRU_CLKGATES_CON(i));
		clks_ungating[i] = 0;
	}

	for (i = 0; i < RK3368_CRU_CLKGATES_CON_CNT; i++)
		cru_writel(0xffff0000, RK3368_CRU_CLKGATES_CON(i));

	ret = rk3368_pmu_set_power_domain(pd, on);

	for (i = 0; i < RK3368_CRU_CLKGATES_CON_CNT; i++)
		cru_writel(clks_save[i] | 0xffff0000,
			   RK3368_CRU_CLKGATES_CON(i));

	return ret;
}

static int __init rk3368_init_rockchip_pmu_ops(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "rockchip,rk3368-pmu");
	if (!node) {
		pr_err("%s: could not find pmu dt node\n", __func__);
		return -ENXIO;
	}

	rk_pmu_base = of_iomap(node, 0);
	if (!rk_pmu_base) {
		pr_err("%s: could not map pmu registers\n", __func__);
		return -ENXIO;
	}

	rockchip_pmu_ops.set_power_domain = rk3368_sys_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk3368_pmu_power_domain_is_on;

	return 0;
}

arch_initcall(rk3368_init_rockchip_pmu_ops);
