// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom BCM63138 PMB initialization for secondary CPU(s)
 *
 * Copyright (C) 2015 Broadcom Corporation
 * Author: Florian Fainelli <f.fainelli@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/reset/bcm63xx_pmb.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "bcm63xx_smp.h"

/* ARM Control register definitions */
#define CORE_PWR_CTRL_SHIFT	0
#define CORE_PWR_CTRL_MASK	0x3
#define PLL_PWR_ON		BIT(8)
#define PLL_LDO_PWR_ON		BIT(9)
#define PLL_CLAMP_ON		BIT(10)
#define CPU_RESET_N(x)		BIT(13 + (x))
#define NEON_RESET_N		BIT(15)
#define PWR_CTRL_STATUS_SHIFT	28
#define PWR_CTRL_STATUS_MASK	0x3
#define PWR_DOWN_SHIFT		30
#define PWR_DOWN_MASK		0x3

/* CPU Power control register definitions */
#define MEM_PWR_OK		BIT(0)
#define MEM_PWR_ON		BIT(1)
#define MEM_CLAMP_ON		BIT(2)
#define MEM_PWR_OK_STATUS	BIT(4)
#define MEM_PWR_ON_STATUS	BIT(5)
#define MEM_PDA_SHIFT		8
#define MEM_PDA_MASK		0xf
#define  MEM_PDA_CPU_MASK	0x1
#define  MEM_PDA_NEON_MASK	0xf
#define CLAMP_ON		BIT(15)
#define PWR_OK_SHIFT		16
#define PWR_OK_MASK		0xf
#define PWR_ON_SHIFT		20
#define  PWR_CPU_MASK		0x03
#define  PWR_NEON_MASK		0x01
#define PWR_ON_MASK		0xf
#define PWR_OK_STATUS_SHIFT	24
#define PWR_OK_STATUS_MASK	0xf
#define PWR_ON_STATUS_SHIFT	28
#define PWR_ON_STATUS_MASK	0xf

#define ARM_CONTROL		0x30
#define ARM_PWR_CONTROL_BASE	0x34
#define ARM_PWR_CONTROL(x)	(ARM_PWR_CONTROL_BASE + (x) * 0x4)
#define ARM_NEON_L2		0x3c

/* Perform a value write, then spin until the value shifted by
 * shift is seen, masked with mask and is different from cond.
 */
static int bpcm_wr_rd_mask(void __iomem *master,
			   unsigned int addr, u32 off, u32 *val,
			   u32 shift, u32 mask, u32 cond)
{
	int ret;

	ret = bpcm_wr(master, addr, off, *val);
	if (ret)
		return ret;

	do {
		ret = bpcm_rd(master, addr, off, val);
		if (ret)
			return ret;

		cpu_relax();
	} while (((*val >> shift) & mask) != cond);

	return ret;
}

/* Global lock to serialize accesses to the PMB registers while we
 * are bringing up the secondary CPU
 */
static DEFINE_SPINLOCK(pmb_lock);

static int bcm63xx_pmb_get_resources(struct device_node *dn,
				     void __iomem **base,
				     unsigned int *cpu,
				     unsigned int *addr)
{
	struct of_phandle_args args;
	int ret;

	*cpu = of_get_cpu_hwid(dn, 0);
	if (*cpu == ~0U) {
		pr_err("CPU is missing a reg node\n");
		return -ENODEV;
	}

	ret = of_parse_phandle_with_args(dn, "resets", "#reset-cells",
					 0, &args);
	if (ret) {
		pr_err("CPU is missing a resets phandle\n");
		return ret;
	}

	if (args.args_count != 2) {
		pr_err("reset-controller does not conform to reset-cells\n");
		return -EINVAL;
	}

	*base = of_iomap(args.np, 0);
	if (!*base) {
		pr_err("failed remapping PMB register\n");
		return -ENOMEM;
	}

	/* We do not need the number of zones */
	*addr = args.args[0];

	return 0;
}

int bcm63xx_pmb_power_on_cpu(struct device_node *dn)
{
	void __iomem *base;
	unsigned int cpu, addr;
	unsigned long flags;
	u32 val, ctrl;
	int ret;

	ret = bcm63xx_pmb_get_resources(dn, &base, &cpu, &addr);
	if (ret)
		return ret;

	/* We would not know how to enable a third and greater CPU */
	WARN_ON(cpu > 1);

	spin_lock_irqsave(&pmb_lock, flags);

	/* Check if the CPU is already on and save the ARM_CONTROL register
	 * value since we will use it later for CPU de-assert once done with
	 * the CPU-specific power sequence
	 */
	ret = bpcm_rd(base, addr, ARM_CONTROL, &ctrl);
	if (ret)
		goto out;

	if (ctrl & CPU_RESET_N(cpu)) {
		pr_info("PMB: CPU%d is already powered on\n", cpu);
		ret = 0;
		goto out;
	}

	/* Power on PLL */
	ret = bpcm_rd(base, addr, ARM_PWR_CONTROL(cpu), &val);
	if (ret)
		goto out;

	val |= (PWR_CPU_MASK << PWR_ON_SHIFT);

	ret = bpcm_wr_rd_mask(base, addr, ARM_PWR_CONTROL(cpu), &val,
			PWR_ON_STATUS_SHIFT, PWR_CPU_MASK, PWR_CPU_MASK);
	if (ret)
		goto out;

	val |= (PWR_CPU_MASK << PWR_OK_SHIFT);

	ret = bpcm_wr_rd_mask(base, addr, ARM_PWR_CONTROL(cpu), &val,
			PWR_OK_STATUS_SHIFT, PWR_CPU_MASK, PWR_CPU_MASK);
	if (ret)
		goto out;

	val &= ~CLAMP_ON;

	ret = bpcm_wr(base, addr, ARM_PWR_CONTROL(cpu), val);
	if (ret)
		goto out;

	/* Power on CPU<N> RAM */
	val &= ~(MEM_PDA_MASK << MEM_PDA_SHIFT);

	ret = bpcm_wr(base, addr, ARM_PWR_CONTROL(cpu), val);
	if (ret)
		goto out;

	val |= MEM_PWR_ON;

	ret = bpcm_wr_rd_mask(base, addr, ARM_PWR_CONTROL(cpu), &val,
			0, MEM_PWR_ON_STATUS, MEM_PWR_ON_STATUS);
	if (ret)
		goto out;

	val |= MEM_PWR_OK;

	ret = bpcm_wr_rd_mask(base, addr, ARM_PWR_CONTROL(cpu), &val,
			0, MEM_PWR_OK_STATUS, MEM_PWR_OK_STATUS);
	if (ret)
		goto out;

	val &= ~MEM_CLAMP_ON;

	ret = bpcm_wr(base, addr, ARM_PWR_CONTROL(cpu), val);
	if (ret)
		goto out;

	/* De-assert CPU reset */
	ctrl |= CPU_RESET_N(cpu);

	ret = bpcm_wr(base, addr, ARM_CONTROL, ctrl);
out:
	spin_unlock_irqrestore(&pmb_lock, flags);
	iounmap(base);
	return ret;
}
