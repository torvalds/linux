// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2015 Broadcom Corporation
 * Copyright 2014 Linaro Limited
 */

#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip/irq-bcm2836.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include "platsmp.h"

/* Size of mapped Cortex A9 SCU address space */
#define CORTEX_A9_SCU_SIZE	0x58

#define SECONDARY_TIMEOUT_NS	NSEC_PER_MSEC	/* 1 msec (in nanoseconds) */
#define BOOT_ADDR_CPUID_MASK	0x3

/* Name of device node property defining secondary boot register location */
#define OF_SECONDARY_BOOT	"secondary-boot-reg"
#define MPIDR_CPUID_BITMASK	0x3

/*
 * Enable the Cortex A9 Snoop Control Unit
 *
 * By the time this is called we already know there are multiple
 * cores present.  We assume we're running on a Cortex A9 processor,
 * so any trouble getting the base address register or getting the
 * SCU base is a problem.
 *
 * Return 0 if successful or an error code otherwise.
 */
static int __init scu_a9_enable(void)
{
	unsigned long config_base;
	void __iomem *scu_base;

	if (!scu_a9_has_base()) {
		pr_err("no configuration base address register!\n");
		return -ENXIO;
	}

	/* Config base address register value is zero for uniprocessor */
	config_base = scu_a9_get_base();
	if (!config_base) {
		pr_err("hardware reports only one core\n");
		return -ENOENT;
	}

	scu_base = ioremap((phys_addr_t)config_base, CORTEX_A9_SCU_SIZE);
	if (!scu_base) {
		pr_err("failed to remap config base (%lu/%u) for SCU\n",
			config_base, CORTEX_A9_SCU_SIZE);
		return -ENOMEM;
	}

	scu_enable(scu_base);

	iounmap(scu_base);	/* That's the last we'll need of this */

	return 0;
}

static u32 secondary_boot_addr_for(unsigned int cpu)
{
	u32 secondary_boot_addr = 0;
	struct device_node *cpu_node = of_get_cpu_node(cpu, NULL);

        if (!cpu_node) {
		pr_err("Failed to find device tree node for CPU%u\n", cpu);
		return 0;
	}

	if (of_property_read_u32(cpu_node,
				 OF_SECONDARY_BOOT,
				 &secondary_boot_addr))
		pr_err("required secondary boot register not specified for CPU%u\n",
			cpu);

	of_node_put(cpu_node);

	return secondary_boot_addr;
}

static int nsp_write_lut(unsigned int cpu)
{
	void __iomem *sku_rom_lut;
	phys_addr_t secondary_startup_phy;
	const u32 secondary_boot_addr = secondary_boot_addr_for(cpu);

	if (!secondary_boot_addr)
		return -EINVAL;

	sku_rom_lut = ioremap((phys_addr_t)secondary_boot_addr,
				      sizeof(phys_addr_t));
	if (!sku_rom_lut) {
		pr_warn("unable to ioremap SKU-ROM LUT register for cpu %u\n", cpu);
		return -ENOMEM;
	}

	secondary_startup_phy = __pa_symbol(secondary_startup);
	BUG_ON(secondary_startup_phy > (phys_addr_t)U32_MAX);

	writel_relaxed(secondary_startup_phy, sku_rom_lut);

	/* Ensure the write is visible to the secondary core */
	smp_wmb();

	iounmap(sku_rom_lut);

	return 0;
}

static void __init bcm_smp_prepare_cpus(unsigned int max_cpus)
{
	const cpumask_t only_cpu_0 = { CPU_BITS_CPU0 };

	/* Enable the SCU on Cortex A9 based SoCs */
	if (scu_a9_enable()) {
		/* Update the CPU present map to reflect uniprocessor mode */
		pr_warn("failed to enable A9 SCU - disabling SMP\n");
		init_cpu_present(&only_cpu_0);
	}
}

/*
 * The ROM code has the secondary cores looping, waiting for an event.
 * When an event occurs each core examines the bottom two bits of the
 * secondary boot register.  When a core finds those bits contain its
 * own core id, it performs initialization, including computing its boot
 * address by clearing the boot register value's bottom two bits.  The
 * core signals that it is beginning its execution by writing its boot
 * address back to the secondary boot register, and finally jumps to
 * that address.
 *
 * So to start a core executing we need to:
 * - Encode the (hardware) CPU id with the bottom bits of the secondary
 *   start address.
 * - Write that value into the secondary boot register.
 * - Generate an event to wake up the secondary CPU(s).
 * - Wait for the secondary boot register to be re-written, which
 *   indicates the secondary core has started.
 */
static int kona_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	void __iomem *boot_reg;
	phys_addr_t boot_func;
	u64 start_clock;
	u32 cpu_id;
	u32 boot_val;
	bool timeout = false;
	const u32 secondary_boot_addr = secondary_boot_addr_for(cpu);

	cpu_id = cpu_logical_map(cpu);
	if (cpu_id & ~BOOT_ADDR_CPUID_MASK) {
		pr_err("bad cpu id (%u > %u)\n", cpu_id, BOOT_ADDR_CPUID_MASK);
		return -EINVAL;
	}

	if (!secondary_boot_addr)
		return -EINVAL;

	boot_reg = ioremap((phys_addr_t)secondary_boot_addr,
				   sizeof(phys_addr_t));
	if (!boot_reg) {
		pr_err("unable to map boot register for cpu %u\n", cpu_id);
		return -ENOMEM;
	}

	/*
	 * Secondary cores will start in secondary_startup(),
	 * defined in "arch/arm/kernel/head.S"
	 */
	boot_func = __pa_symbol(secondary_startup);
	BUG_ON(boot_func & BOOT_ADDR_CPUID_MASK);
	BUG_ON(boot_func > (phys_addr_t)U32_MAX);

	/* The core to start is encoded in the low bits */
	boot_val = (u32)boot_func | cpu_id;
	writel_relaxed(boot_val, boot_reg);

	sev();

	/* The low bits will be cleared once the core has started */
	start_clock = local_clock();
	while (!timeout && readl_relaxed(boot_reg) == boot_val)
		timeout = local_clock() - start_clock > SECONDARY_TIMEOUT_NS;

	iounmap(boot_reg);

	if (!timeout)
		return 0;

	pr_err("timeout waiting for cpu %u to start\n", cpu_id);

	return -ENXIO;
}

/* Cluster Dormant Control command to bring CPU into a running state */
#define CDC_CMD			6
#define CDC_CMD_OFFSET		0
#define CDC_CMD_REG(cpu)	(CDC_CMD_OFFSET + 4*(cpu))

/*
 * BCM23550 has a Cluster Dormant Control block that keeps the core in
 * idle state. A command needs to be sent to the block to bring the CPU
 * into running state.
 */
static int bcm23550_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	void __iomem *cdc_base;
	struct device_node *dn;
	char *name;
	int ret;

	/* Make sure a CDC node exists before booting the
	 * secondary core.
	 */
	name = "brcm,bcm23550-cdc";
	dn = of_find_compatible_node(NULL, NULL, name);
	if (!dn) {
		pr_err("unable to find cdc node\n");
		return -ENODEV;
	}

	cdc_base = of_iomap(dn, 0);
	of_node_put(dn);

	if (!cdc_base) {
		pr_err("unable to remap cdc base register\n");
		return -ENOMEM;
	}

	/* Boot the secondary core */
	ret = kona_boot_secondary(cpu, idle);
	if (ret)
		goto out;

	/* Bring this CPU to RUN state so that nIRQ nFIQ
	 * signals are unblocked.
	 */
	writel_relaxed(CDC_CMD, cdc_base + CDC_CMD_REG(cpu));

out:
	iounmap(cdc_base);

	return ret;
}

static int nsp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/*
	 * After wake up, secondary core branches to the startup
	 * address programmed at SKU ROM LUT location.
	 */
	ret = nsp_write_lut(cpu);
	if (ret) {
		pr_err("unable to write startup addr to SKU ROM LUT\n");
		goto out;
	}

	/* Send a CPU wakeup interrupt to the secondary core */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

out:
	return ret;
}

static int bcm2836_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	void __iomem *intc_base;
	struct device_node *dn;
	char *name;

	name = "brcm,bcm2836-l1-intc";
	dn = of_find_compatible_node(NULL, NULL, name);
	if (!dn) {
		pr_err("unable to find intc node\n");
		return -ENODEV;
	}

	intc_base = of_iomap(dn, 0);
	of_node_put(dn);

	if (!intc_base) {
		pr_err("unable to remap intc base register\n");
		return -ENOMEM;
	}

	writel(virt_to_phys(secondary_startup),
	       intc_base + LOCAL_MAILBOX3_SET0 + 16 * cpu);

	dsb(sy);
	sev();

	iounmap(intc_base);

	return 0;
}

static const struct smp_operations kona_smp_ops __initconst = {
	.smp_prepare_cpus	= bcm_smp_prepare_cpus,
	.smp_boot_secondary	= kona_boot_secondary,
};
CPU_METHOD_OF_DECLARE(bcm_smp_bcm281xx, "brcm,bcm11351-cpu-method",
			&kona_smp_ops);

static const struct smp_operations bcm23550_smp_ops __initconst = {
	.smp_boot_secondary	= bcm23550_boot_secondary,
};
CPU_METHOD_OF_DECLARE(bcm_smp_bcm23550, "brcm,bcm23550",
			&bcm23550_smp_ops);

static const struct smp_operations nsp_smp_ops __initconst = {
	.smp_prepare_cpus	= bcm_smp_prepare_cpus,
	.smp_boot_secondary	= nsp_boot_secondary,
};
CPU_METHOD_OF_DECLARE(bcm_smp_nsp, "brcm,bcm-nsp-smp", &nsp_smp_ops);

const struct smp_operations bcm2836_smp_ops __initconst = {
	.smp_boot_secondary	= bcm2836_boot_secondary,
};
CPU_METHOD_OF_DECLARE(bcm_smp_bcm2836, "brcm,bcm2836-smp", &bcm2836_smp_ops);
