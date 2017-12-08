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

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/semaphore.h>

#include <asm/cacheflush.h>

#include "spc.h"

#define SPCLOG "vexpress-spc: "

#define PERF_LVL_A15		0x00
#define PERF_REQ_A15		0x04
#define PERF_LVL_A7		0x08
#define PERF_REQ_A7		0x0c
#define COMMS			0x10
#define COMMS_REQ		0x14
#define PWC_STATUS		0x18
#define PWC_FLAG		0x1c

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

/* SPC CPU/cluster reset statue */
#define STANDBYWFI_STAT		0x3c
#define STANDBYWFI_STAT_A15_CPU_MASK(cpu)	(1 << (cpu))
#define STANDBYWFI_STAT_A7_CPU_MASK(cpu)	(1 << (3 + (cpu)))

/* SPC system config interface registers */
#define SYSCFG_WDATA		0x70
#define SYSCFG_RDATA		0x74

/* A15/A7 OPP virtual register base */
#define A15_PERFVAL_BASE	0xC10
#define A7_PERFVAL_BASE		0xC30

/* Config interface control bits */
#define SYSCFG_START		(1 << 31)
#define SYSCFG_SCC		(6 << 20)
#define SYSCFG_STAT		(14 << 20)

/* wake-up interrupt masks */
#define GBL_WAKEUP_INT_MSK	(0x3 << 10)

/* TC2 static dual-cluster configuration */
#define MAX_CLUSTERS		2

/*
 * Even though the SPC takes max 3-5 ms to complete any OPP/COMMS
 * operation, the operation could start just before jiffie is about
 * to be incremented. So setting timeout value of 20ms = 2jiffies@100Hz
 */
#define TIMEOUT_US	20000

#define MAX_OPPS	8
#define CA15_DVFS	0
#define CA7_DVFS	1
#define SPC_SYS_CFG	2
#define STAT_COMPLETE(type)	((1 << 0) << (type << 2))
#define STAT_ERR(type)		((1 << 1) << (type << 2))
#define RESPONSE_MASK(type)	(STAT_COMPLETE(type) | STAT_ERR(type))

struct ve_spc_opp {
	unsigned long freq;
	unsigned long u_volt;
};

struct ve_spc_drvdata {
	void __iomem *baseaddr;
	/*
	 * A15s cluster identifier
	 * It corresponds to A15 processors MPIDR[15:8] bitfield
	 */
	u32 a15_clusid;
	uint32_t cur_rsp_mask;
	uint32_t cur_rsp_stat;
	struct semaphore sem;
	struct completion done;
	struct ve_spc_opp *opps[MAX_CLUSTERS];
	int num_opps[MAX_CLUSTERS];
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

static u32 standbywfi_cpu_mask(u32 cpu, u32 cluster)
{
	return cluster_is_a15(cluster) ?
		  STANDBYWFI_STAT_A15_CPU_MASK(cpu)
		: STANDBYWFI_STAT_A7_CPU_MASK(cpu);
}

/**
 * ve_spc_cpu_in_wfi(u32 cpu, u32 cluster)
 *
 * @cpu: mpidr[7:0] bitfield describing CPU affinity level within cluster
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 *
 * @return: non-zero if and only if the specified CPU is in WFI
 *
 * Take care when interpreting the result of this function: a CPU might
 * be in WFI temporarily due to idle, and is not necessarily safely
 * parked.
 */
int ve_spc_cpu_in_wfi(u32 cpu, u32 cluster)
{
	int ret;
	u32 mask = standbywfi_cpu_mask(cpu, cluster);

	if (cluster >= MAX_CLUSTERS)
		return 1;

	ret = readl_relaxed(info->baseaddr + STANDBYWFI_STAT);

	pr_debug("%s: PCFGREG[0x%X] = 0x%08X, mask = 0x%X\n",
		 __func__, STANDBYWFI_STAT, ret, mask);

	return ret & mask;
}

static int ve_spc_get_performance(int cluster, u32 *freq)
{
	struct ve_spc_opp *opps = info->opps[cluster];
	u32 perf_cfg_reg = 0;
	u32 perf;

	perf_cfg_reg = cluster_is_a15(cluster) ? PERF_LVL_A15 : PERF_LVL_A7;

	perf = readl_relaxed(info->baseaddr + perf_cfg_reg);
	if (perf >= info->num_opps[cluster])
		return -EINVAL;

	opps += perf;
	*freq = opps->freq;

	return 0;
}

/* find closest match to given frequency in OPP table */
static int ve_spc_round_performance(int cluster, u32 freq)
{
	int idx, max_opp = info->num_opps[cluster];
	struct ve_spc_opp *opps = info->opps[cluster];
	u32 fmin = 0, fmax = ~0, ftmp;

	freq /= 1000; /* OPP entries in kHz */
	for (idx = 0; idx < max_opp; idx++, opps++) {
		ftmp = opps->freq;
		if (ftmp >= freq) {
			if (ftmp <= fmax)
				fmax = ftmp;
		} else {
			if (ftmp >= fmin)
				fmin = ftmp;
		}
	}
	if (fmax != ~0)
		return fmax * 1000;
	else
		return fmin * 1000;
}

static int ve_spc_find_performance_index(int cluster, u32 freq)
{
	int idx, max_opp = info->num_opps[cluster];
	struct ve_spc_opp *opps = info->opps[cluster];

	for (idx = 0; idx < max_opp; idx++, opps++)
		if (opps->freq == freq)
			break;
	return (idx == max_opp) ? -EINVAL : idx;
}

static int ve_spc_waitforcompletion(int req_type)
{
	int ret = wait_for_completion_interruptible_timeout(
			&info->done, usecs_to_jiffies(TIMEOUT_US));
	if (ret == 0)
		ret = -ETIMEDOUT;
	else if (ret > 0)
		ret = info->cur_rsp_stat & STAT_COMPLETE(req_type) ? 0 : -EIO;
	return ret;
}

static int ve_spc_set_performance(int cluster, u32 freq)
{
	u32 perf_cfg_reg;
	int ret, perf, req_type;

	if (cluster_is_a15(cluster)) {
		req_type = CA15_DVFS;
		perf_cfg_reg = PERF_LVL_A15;
	} else {
		req_type = CA7_DVFS;
		perf_cfg_reg = PERF_LVL_A7;
	}

	perf = ve_spc_find_performance_index(cluster, freq);

	if (perf < 0)
		return perf;

	if (down_timeout(&info->sem, usecs_to_jiffies(TIMEOUT_US)))
		return -ETIME;

	init_completion(&info->done);
	info->cur_rsp_mask = RESPONSE_MASK(req_type);

	writel(perf, info->baseaddr + perf_cfg_reg);
	ret = ve_spc_waitforcompletion(req_type);

	info->cur_rsp_mask = 0;
	up(&info->sem);

	return ret;
}

static int ve_spc_read_sys_cfg(int func, int offset, uint32_t *data)
{
	int ret;

	if (down_timeout(&info->sem, usecs_to_jiffies(TIMEOUT_US)))
		return -ETIME;

	init_completion(&info->done);
	info->cur_rsp_mask = RESPONSE_MASK(SPC_SYS_CFG);

	/* Set the control value */
	writel(SYSCFG_START | func | offset >> 2, info->baseaddr + COMMS);
	ret = ve_spc_waitforcompletion(SPC_SYS_CFG);

	if (ret == 0)
		*data = readl(info->baseaddr + SYSCFG_RDATA);

	info->cur_rsp_mask = 0;
	up(&info->sem);

	return ret;
}

static irqreturn_t ve_spc_irq_handler(int irq, void *data)
{
	struct ve_spc_drvdata *drv_data = data;
	uint32_t status = readl_relaxed(drv_data->baseaddr + PWC_STATUS);

	if (info->cur_rsp_mask & status) {
		info->cur_rsp_stat = status;
		complete(&drv_data->done);
	}

	return IRQ_HANDLED;
}

/*
 *  +--------------------------+
 *  | 31      20 | 19        0 |
 *  +--------------------------+
 *  |   m_volt   |  freq(kHz)  |
 *  +--------------------------+
 */
#define MULT_FACTOR	20
#define VOLT_SHIFT	20
#define FREQ_MASK	(0xFFFFF)
static int ve_spc_populate_opps(uint32_t cluster)
{
	uint32_t data = 0, off, ret, idx;
	struct ve_spc_opp *opps;

	opps = kzalloc(sizeof(*opps) * MAX_OPPS, GFP_KERNEL);
	if (!opps)
		return -ENOMEM;

	info->opps[cluster] = opps;

	off = cluster_is_a15(cluster) ? A15_PERFVAL_BASE : A7_PERFVAL_BASE;
	for (idx = 0; idx < MAX_OPPS; idx++, off += 4, opps++) {
		ret = ve_spc_read_sys_cfg(SYSCFG_SCC, off, &data);
		if (!ret) {
			opps->freq = (data & FREQ_MASK) * MULT_FACTOR;
			opps->u_volt = (data >> VOLT_SHIFT) * 1000;
		} else {
			break;
		}
	}
	info->num_opps[cluster] = idx;

	return ret;
}

static int ve_init_opp_table(struct device *cpu_dev)
{
	int cluster;
	int idx, ret = 0, max_opp;
	struct ve_spc_opp *opps;

	cluster = topology_physical_package_id(cpu_dev->id);
	cluster = cluster < 0 ? 0 : cluster;

	max_opp = info->num_opps[cluster];
	opps = info->opps[cluster];

	for (idx = 0; idx < max_opp; idx++, opps++) {
		ret = dev_pm_opp_add(cpu_dev, opps->freq * 1000, opps->u_volt);
		if (ret) {
			dev_warn(cpu_dev, "failed to add opp %lu %lu\n",
				 opps->freq, opps->u_volt);
			return ret;
		}
	}
	return ret;
}

int __init ve_spc_init(void __iomem *baseaddr, u32 a15_clusid, int irq)
{
	int ret;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->baseaddr = baseaddr;
	info->a15_clusid = a15_clusid;

	if (irq <= 0) {
		pr_err(SPCLOG "Invalid IRQ %d\n", irq);
		kfree(info);
		return -EINVAL;
	}

	init_completion(&info->done);

	readl_relaxed(info->baseaddr + PWC_STATUS);

	ret = request_irq(irq, ve_spc_irq_handler, IRQF_TRIGGER_HIGH
				| IRQF_ONESHOT, "vexpress-spc", info);
	if (ret) {
		pr_err(SPCLOG "IRQ %d request failed\n", irq);
		kfree(info);
		return -ENODEV;
	}

	sema_init(&info->sem, 1);
	/*
	 * Multi-cluster systems may need this data when non-coherent, during
	 * cluster power-up/power-down. Make sure driver info reaches main
	 * memory.
	 */
	sync_cache_w(info);
	sync_cache_w(&info);

	return 0;
}

struct clk_spc {
	struct clk_hw hw;
	int cluster;
};

#define to_clk_spc(spc) container_of(spc, struct clk_spc, hw)
static unsigned long spc_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_spc *spc = to_clk_spc(hw);
	u32 freq;

	if (ve_spc_get_performance(spc->cluster, &freq))
		return -EIO;

	return freq * 1000;
}

static long spc_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *parent_rate)
{
	struct clk_spc *spc = to_clk_spc(hw);

	return ve_spc_round_performance(spc->cluster, drate);
}

static int spc_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_spc *spc = to_clk_spc(hw);

	return ve_spc_set_performance(spc->cluster, rate / 1000);
}

static struct clk_ops clk_spc_ops = {
	.recalc_rate = spc_recalc_rate,
	.round_rate = spc_round_rate,
	.set_rate = spc_set_rate,
};

static struct clk *ve_spc_clk_register(struct device *cpu_dev)
{
	struct clk_init_data init;
	struct clk_spc *spc;

	spc = kzalloc(sizeof(*spc), GFP_KERNEL);
	if (!spc)
		return ERR_PTR(-ENOMEM);

	spc->hw.init = &init;
	spc->cluster = topology_physical_package_id(cpu_dev->id);

	spc->cluster = spc->cluster < 0 ? 0 : spc->cluster;

	init.name = dev_name(cpu_dev);
	init.ops = &clk_spc_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.num_parents = 0;

	return devm_clk_register(cpu_dev, &spc->hw);
}

static int __init ve_spc_clk_init(void)
{
	int cpu;
	struct clk *clk;

	if (!info)
		return 0; /* Continue only if SPC is initialised */

	if (ve_spc_populate_opps(0) || ve_spc_populate_opps(1)) {
		pr_err("failed to build OPP table\n");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		struct device *cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_warn("failed to get cpu%d device\n", cpu);
			continue;
		}
		clk = ve_spc_clk_register(cpu_dev);
		if (IS_ERR(clk)) {
			pr_warn("failed to register cpu%d clock\n", cpu);
			continue;
		}
		if (clk_register_clkdev(clk, NULL, dev_name(cpu_dev))) {
			pr_warn("failed to register cpu%d clock lookup\n", cpu);
			continue;
		}

		if (ve_init_opp_table(cpu_dev))
			pr_warn("failed to initialise cpu%d opp table\n", cpu);
	}

	platform_device_register_simple("vexpress-spc-cpufreq", -1, NULL, 0);
	return 0;
}
device_initcall(ve_spc_clk_init);
