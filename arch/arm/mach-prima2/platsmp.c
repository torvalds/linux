/*
 * plat smp support for CSR Marco dual-core SMP SoCs
 *
 * Copyright (c) 2012 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>

#include "common.h"

static void __iomem *scu_base;
static void __iomem *rsc_base;

static DEFINE_SPINLOCK(boot_lock);

static struct map_desc scu_io_desc __initdata = {
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init sirfsoc_map_scu(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.virtual = SIRFSOC_VA(base);
	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);

	scu_base = (void __iomem *)SIRFSOC_VA(base);
}

static void __cpuinit sirfsoc_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	pen_release = -1;
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static struct of_device_id rsc_ids[]  = {
	{ .compatible = "sirf,marco-rsc" },
	{},
};

static int __cpuinit sirfsoc_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	struct device_node *np;

	np = of_find_matching_node(NULL, rsc_ids);
	if (!np)
		return -ENODEV;

	rsc_base = of_iomap(np, 0);
	if (!rsc_base)
		return -ENOMEM;

	/*
	 * write the address of secondary startup into the sram register
	 * at offset 0x2C, then write the magic number 0x3CAF5D62 to the
	 * RSC register at offset 0x28, which is what boot rom code is
	 * waiting for. This would wake up the secondary core from WFE
	 */
#define SIRFSOC_CPU1_JUMPADDR_OFFSET 0x2C
	__raw_writel(virt_to_phys(sirfsoc_secondary_startup),
		rsc_base + SIRFSOC_CPU1_JUMPADDR_OFFSET);

#define SIRFSOC_CPU1_WAKEMAGIC_OFFSET 0x28
	__raw_writel(0x3CAF5D62,
		rsc_base + SIRFSOC_CPU1_WAKEMAGIC_OFFSET);

	/* make sure write buffer is drained */
	mb();

	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	pen_release = cpu_logical_map(cpu);
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));

	/*
	 * Send the secondary CPU SEV, thereby causing the boot monitor to read
	 * the JUMPADDR and WAKEMAGIC, and branch to the address found there.
	 */
	dsb_sev();

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

static void __init sirfsoc_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(scu_base);
}

struct smp_operations sirfsoc_smp_ops __initdata = {
        .smp_prepare_cpus       = sirfsoc_smp_prepare_cpus,
        .smp_secondary_init     = sirfsoc_secondary_init,
        .smp_boot_secondary     = sirfsoc_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die                = sirfsoc_cpu_die,
#endif
};
