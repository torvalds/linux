/*
 * arch/arm/mach-vexpress/dcscb.c - Dual Cluster System Configuration Block
 *
 * Created by:	Nicolas Pitre, May 2012
 * Copyright:	(C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/vexpress.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cp15.h>


#define RST_HOLD0	0x0
#define RST_HOLD1	0x4
#define SYS_SWRESET	0x8
#define RST_STAT0	0xc
#define RST_STAT1	0x10
#define EAG_CFG_R	0x20
#define EAG_CFG_W	0x24
#define KFC_CFG_R	0x28
#define KFC_CFG_W	0x2c
#define DCS_CFG_R	0x30

/*
 * We can't use regular spinlocks. In the switcher case, it is possible
 * for an outbound CPU to call power_down() while its inbound counterpart
 * is already live using the same logical CPU number which trips lockdep
 * debugging.
 */
static arch_spinlock_t dcscb_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static void __iomem *dcscb_base;

static int dcscb_power_up(unsigned int cpu, unsigned int cluster)
{
	unsigned int rst_hold, cpumask = (1 << cpu);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cpu >= 4 || cluster >= 2)
		return -EINVAL;

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&dcscb_lock);

	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	if (rst_hold & (1 << 8)) {
		/* remove cluster reset and add individual CPU's reset */
		rst_hold &= ~(1 << 8);
		rst_hold |= 0xf;
	}
	rst_hold &= ~(cpumask | (cpumask << 4));
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);

	arch_spin_unlock(&dcscb_lock);
	local_irq_enable();

	return 0;
}

static void dcscb_power_down(void)
{
	unsigned int mpidr, cpu, cluster, rst_hold, cpumask, last_man;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	cpumask = (1 << cpu);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cpu >= 4 || cluster >= 2);

	arch_spin_lock(&dcscb_lock);
	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	rst_hold |= cpumask;
	if (((rst_hold | (rst_hold >> 4)) & 0xf) == 0xf)
		rst_hold |= (1 << 8);
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);
	arch_spin_unlock(&dcscb_lock);
	last_man = (rst_hold & (1 << 8));

	/*
	 * Now let's clean our L1 cache and shut ourself down.
	 * If we're the last CPU in this cluster then clean L2 too.
	 */

	/*
	 * A15/A7 can hit in the cache with SCTLR.C=0, so we don't need
	 * a preliminary flush here for those CPUs.  At least, that's
	 * the theory -- without the extra flush, Linux explodes on
	 * RTSM (to be investigated)..
	 */
	flush_cache_louis();
	set_cr(get_cr() & ~CR_C);

	if (!last_man) {
		flush_cache_louis();
	} else {
		flush_cache_all();
		outer_flush_all();
	}

	/* Disable local coherency by clearing the ACTLR "SMP" bit: */
	set_auxcr(get_auxcr() & ~(1 << 6));

	/* Now we are prepared for power-down, do it: */
	dsb();
	wfi();

	/* Not dead at this point?  Let our caller cope. */
}

static const struct mcpm_platform_ops dcscb_power_ops = {
	.power_up	= dcscb_power_up,
	.power_down	= dcscb_power_down,
};

static int __init dcscb_init(void)
{
	struct device_node *node;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "arm,rtsm,dcscb");
	if (!node)
		return -ENODEV;
	dcscb_base = of_iomap(node, 0);
	if (!dcscb_base)
		return -EADDRNOTAVAIL;

	ret = mcpm_platform_register(&dcscb_power_ops);
	if (ret) {
		iounmap(dcscb_base);
		return ret;
	}

	pr_info("VExpress DCSCB support installed\n");

	/*
	 * Future entries into the kernel can now go
	 * through the cluster entry vectors.
	 */
	vexpress_flags_set(virt_to_phys(mcpm_entry_point));

	return 0;
}

early_initcall(dcscb_init);
