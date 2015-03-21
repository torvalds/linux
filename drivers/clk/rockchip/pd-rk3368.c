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
#include <linux/rockchip/cpu_axi.h>

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

/* PD_VIO */
static void __iomem *iep_qos_base;
static u32 iep_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *isp_r0_qos_base;
static u32 isp_r0_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *isp_r1_qos_base;
static u32 isp_r1_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *isp_w0_qos_base;
static u32 isp_w0_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *isp_w1_qos_base;
static u32 isp_w1_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *vip_qos_base;
static u32 vip_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *vop_qos_base;
static u32 vop_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *rga_r_qos_base;
static u32 rga_r_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *rga_w_qos_base;
static u32 rga_w_qos[CPU_AXI_QOS_NUM_REGS];
/* PD_VIDEO */
static void __iomem *hevc_r_qos_base;
static u32 hevc_r_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *vpu_r_qos_base;
static u32 vpu_r_qos[CPU_AXI_QOS_NUM_REGS];
static void __iomem *vpu_w_qos_base;
static u32 vpu_w_qos[CPU_AXI_QOS_NUM_REGS];
/* PD_GPU_0 */
static void __iomem *gpu_qos_base;
static u32 gpu_qos[CPU_AXI_QOS_NUM_REGS];
/* PD_PERI */
static void __iomem *peri_qos_base;
static u32 peri_qos[CPU_AXI_QOS_NUM_REGS];

#define PD_SAVE_QOS(name) CPU_AXI_SAVE_QOS(name##_qos, name##_qos_base)
#define PD_RESTORE_QOS(name) CPU_AXI_RESTORE_QOS(name##_qos, name##_qos_base)

static int rk3368_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);

	if (rk3368_pmu_power_domain_is_on(pd) == on)
		goto out;

	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO) {
			PD_SAVE_QOS(iep);
			PD_SAVE_QOS(isp_r0);
			PD_SAVE_QOS(isp_r1);
			PD_SAVE_QOS(isp_w0);
			PD_SAVE_QOS(isp_w1);
			PD_SAVE_QOS(vip);
			PD_SAVE_QOS(vop);
			PD_SAVE_QOS(rga_r);
			PD_SAVE_QOS(rga_w);
			rk3368_pmu_set_idle_request(IDLE_REQ_VIO, true);
		} else if (pd == PD_VIDEO) {
			PD_SAVE_QOS(hevc_r);
			PD_SAVE_QOS(vpu_r);
			PD_SAVE_QOS(vpu_w);
			rk3368_pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		} else if (pd == PD_GPU_0) {
			PD_SAVE_QOS(gpu);
			rk3368_pmu_set_idle_request(IDLE_REQ_GPU, true);
		} else if (pd == PD_PERI) {
			PD_SAVE_QOS(peri);
			rk3368_pmu_set_idle_request(IDLE_REQ_PERI, true);
		}
	}

	rk3368_do_pmu_set_power_domain(pd, on);

	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO) {
			rk3368_pmu_set_idle_request(IDLE_REQ_VIO, false);
			PD_RESTORE_QOS(iep);
			PD_RESTORE_QOS(isp_r0);
			PD_RESTORE_QOS(isp_r1);
			PD_RESTORE_QOS(isp_w0);
			PD_RESTORE_QOS(isp_w1);
			PD_RESTORE_QOS(vip);
			PD_RESTORE_QOS(vop);
			PD_RESTORE_QOS(rga_r);
			PD_RESTORE_QOS(rga_w);
		} else if (pd == PD_VIDEO) {
			rk3368_pmu_set_idle_request(IDLE_REQ_VIDEO, false);
			PD_RESTORE_QOS(hevc_r);
			PD_RESTORE_QOS(vpu_r);
			PD_RESTORE_QOS(vpu_w);
		} else if (pd == PD_GPU_0) {
			rk3368_pmu_set_idle_request(IDLE_REQ_GPU, false);
			PD_RESTORE_QOS(gpu);
		} else if (pd == PD_PERI) {
			rk3368_pmu_set_idle_request(IDLE_REQ_PERI, false);
			PD_RESTORE_QOS(peri);
		}
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
	struct device_node *node, *gp, *cp;

	node = of_find_compatible_node(NULL, NULL, "rockchip,rk3368-pmu");
	if (!node) {
		pr_err("%s: could not find pmu dt node\n", __func__);
		return -ENODEV;
	}

	rk_pmu_base = of_iomap(node, 0);
	if (!rk_pmu_base) {
		pr_err("%s: could not map pmu registers\n", __func__);
		return -ENXIO;
	}

	node = of_find_compatible_node(NULL, NULL, "rockchip,cpu_axi_bus");
	if (!node)
		return -ENODEV;

#define MAP(name)							\
	do {								\
		cp = of_get_child_by_name(gp, #name);			\
		if (cp)							\
			name##_qos_base = of_iomap(cp, 0);		\
		if (!name##_qos_base)					\
			pr_err("%s: could not map qos %s register\n",	\
			       __func__, #name);			\
	} while (0)

	gp = of_get_child_by_name(node, "qos");
	if (gp) {
		MAP(peri);
		MAP(iep);
		MAP(isp_r0);
		MAP(isp_r1);
		MAP(isp_w0);
		MAP(isp_w1);
		MAP(vip);
		MAP(vop);
		MAP(rga_r);
		MAP(rga_w);
		MAP(hevc_r);
		MAP(vpu_r);
		MAP(vpu_w);
		MAP(gpu);
	}

#undef MAP

	rockchip_pmu_ops.set_power_domain = rk3368_sys_set_power_domain;
	rockchip_pmu_ops.power_domain_is_on = rk3368_pmu_power_domain_is_on;

	return 0;
}

arch_initcall(rk3368_init_rockchip_pmu_ops);
