/*
 * Versatile Express Serial Power Controller (SPC) support
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * Authors: Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *          Achin Gupta           <achin.gupta@arm.com>
 *          Lorenzo Pieralisi     <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/cacheflush.h>

#define SPCLOG "vexpress-spc: "

/* SPC wake-up IRQs status and mask */
#define WAKE_INT_MASK		0x24
#define WAKE_INT_RAW		0x28
#define WAKE_INT_STAT		0x2c
/* SPC power down registers */
#define A15_PWRDN_EN		0x30
#define A7_PWRDN_EN		0x34
/* SPC per-CPU mailboxes */
#define A15_BX_ADDR0		0x68
#define A7_BX_ADDR0		0x78

/* wake-up interrupt masks */
#define GBL_WAKEUP_INT_MSK	(0x3 << 10)

/* TC2 static dual-cluster configuration */
#define MAX_CLUSTERS		2

struct ve_spc_drvdata {
	void __iomem *baseaddr;
	/*
	 * A15s cluster identifier
	 * It corresponds to A15 processors MPIDR[15:8] bitfield
	 */
	u32 a15_clusid;
};

static struct ve_spc_drvdata *info;

static inline bool cluster_is_a15(u32 cluster)
{
	return cluster == info->a15_clusid;
}

/**
 * ve_spc_global_wakeup_irq()
 *
 * Function to set/clear global wakeup IRQs. Not protected by locking since
 * it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @set: if true, global wake-up IRQs are set, if false they are cleared
 */
void ve_spc_global_wakeup_irq(bool set)
{
	u32 reg;

	reg = readl_relaxed(info->baseaddr + WAKE_INT_MASK);

	if (set)
		reg |= GBL_WAKEUP_INT_MSK;
	else
		reg &= ~GBL_WAKEUP_INT_MSK;

	writel_relaxed(reg, info->baseaddr + WAKE_INT_MASK);
}

/**
 * ve_spc_cpu_wakeup_irq()
 *
 * Function to set/clear per-CPU wake-up IRQs. Not protected by locking since
 * it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @cpu: mpidr[7:0] bitfield describing cpu affinity level
 * @set: if true, wake-up IRQs are set, if false they are cleared
 */
void ve_spc_cpu_wakeup_irq(u32 cluster, u32 cpu, bool set)
{
	u32 mask, reg;

	if (cluster >= MAX_CLUSTERS)
		return;

	mask = 1 << cpu;

	if (!cluster_is_a15(cluster))
		mask <<= 4;

	reg = readl_relaxed(info->baseaddr + WAKE_INT_MASK);

	if (set)
		reg |= mask;
	else
		reg &= ~mask;

	writel_relaxed(reg, info->baseaddr + WAKE_INT_MASK);
}

/**
 * ve_spc_set_resume_addr() - set the jump address used for warm boot
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @cpu: mpidr[7:0] bitfield describing cpu affinity level
 * @addr: physical resume address
 */
void ve_spc_set_resume_addr(u32 cluster, u32 cpu, u32 addr)
{
	void __iomem *baseaddr;

	if (cluster >= MAX_CLUSTERS)
		return;

	if (cluster_is_a15(cluster))
		baseaddr = info->baseaddr + A15_BX_ADDR0 + (cpu << 2);
	else
		baseaddr = info->baseaddr + A7_BX_ADDR0 + (cpu << 2);

	writel_relaxed(addr, baseaddr);
}

/**
 * ve_spc_powerdown()
 *
 * Function to enable/disable cluster powerdown. Not protected by locking
 * since it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @enable: if true enables powerdown, if false disables it
 */
void ve_spc_powerdown(u32 cluster, bool enable)
{
	u32 pwdrn_reg;

	if (cluster >= MAX_CLUSTERS)
		return;

	pwdrn_reg = cluster_is_a15(cluster) ? A15_PWRDN_EN : A7_PWRDN_EN;
	writel_relaxed(enable, info->baseaddr + pwdrn_reg);
}

int __init ve_spc_init(void __iomem *baseaddr, u32 a15_clusid)
{
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err(SPCLOG "unable to allocate mem\n");
		return -ENOMEM;
	}

	info->baseaddr = baseaddr;
	info->a15_clusid = a15_clusid;

	/*
	 * Multi-cluster systems may need this data when non-coherent, during
	 * cluster power-up/power-down. Make sure driver info reaches main
	 * memory.
	 */
	sync_cache_w(info);
	sync_cache_w(&info);

	return 0;
}
