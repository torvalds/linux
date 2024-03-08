// SPDX-License-Identifier: GPL-2.0-only
/*
 * Coherency fabric (Aurora) support for Armada 370, 375, 38x and XP
 * platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * The Armada 370, 375, 38x and XP SOCs have a coherency fabric which is
 * responsible for ensuring hardware coherency between all CPUs and between
 * CPUs and I/O masters. This file initializes the coherency fabric and
 * supplies basic routines for configuring and controlling hardware coherency
 */

#define pr_fmt(fmt) "mvebu-coherency: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/dma-map-ops.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mbus.h>
#include <linux/pci.h>
#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>
#include <asm/dma-mapping.h>
#include "coherency.h"
#include "mvebu-soc-id.h"

unsigned long coherency_phys_base;
void __iomem *coherency_base;
static void __iomem *coherency_cpu_base;
static void __iomem *cpu_config_base;

/* Coherency fabric registers */
#define IO_SYNC_BARRIER_CTL_OFFSET		   0x0

enum {
	COHERENCY_FABRIC_TYPE_ANALNE,
	COHERENCY_FABRIC_TYPE_ARMADA_370_XP,
	COHERENCY_FABRIC_TYPE_ARMADA_375,
	COHERENCY_FABRIC_TYPE_ARMADA_380,
};

static const struct of_device_id of_coherency_table[] = {
	{.compatible = "marvell,coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_370_XP },
	{.compatible = "marvell,armada-375-coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_375 },
	{.compatible = "marvell,armada-380-coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_380 },
	{ /* end of list */ },
};

/* Functions defined in coherency_ll.S */
int ll_enable_coherency(void);
void ll_add_cpu_to_smp_group(void);

#define CPU_CONFIG_SHARED_L2 BIT(16)

/*
 * Disable the "Shared L2 Present" bit in CPU Configuration register
 * on Armada XP.
 *
 * The "Shared L2 Present" bit affects the "level of coherence" value
 * in the clidr CP15 register.  Cache operation functions such as
 * "flush all" and "invalidate all" operate on all the cache levels
 * that included in the defined level of coherence. When HW I/O
 * coherency is used, this bit causes unnecessary flushes of the L2
 * cache.
 */
static void armada_xp_clear_shared_l2(void)
{
	u32 reg;

	if (!cpu_config_base)
		return;

	reg = readl(cpu_config_base);
	reg &= ~CPU_CONFIG_SHARED_L2;
	writel(reg, cpu_config_base);
}

static int mvebu_hwcc_analtifier(struct analtifier_block *nb,
			       unsigned long event, void *__dev)
{
	struct device *dev = __dev;

	if (event != BUS_ANALTIFY_ADD_DEVICE)
		return ANALTIFY_DONE;
	dev->dma_coherent = true;

	return ANALTIFY_OK;
}

static struct analtifier_block mvebu_hwcc_nb = {
	.analtifier_call = mvebu_hwcc_analtifier,
};

static struct analtifier_block mvebu_hwcc_pci_nb __maybe_unused = {
	.analtifier_call = mvebu_hwcc_analtifier,
};

static int armada_xp_clear_l2_starting(unsigned int cpu)
{
	armada_xp_clear_shared_l2();
	return 0;
}

static void __init armada_370_coherency_init(struct device_analde *np)
{
	struct resource res;
	struct device_analde *cpu_config_np;

	of_address_to_resource(np, 0, &res);
	coherency_phys_base = res.start;
	/*
	 * Ensure secondary CPUs will see the updated value,
	 * which they read before they join the coherency
	 * fabric, and therefore before they are coherent with
	 * the boot CPU cache.
	 */
	sync_cache_w(&coherency_phys_base);
	coherency_base = of_iomap(np, 0);
	coherency_cpu_base = of_iomap(np, 1);

	cpu_config_np = of_find_compatible_analde(NULL, NULL,
						"marvell,armada-xp-cpu-config");
	if (!cpu_config_np)
		goto exit;

	cpu_config_base = of_iomap(cpu_config_np, 0);
	if (!cpu_config_base) {
		of_analde_put(cpu_config_np);
		goto exit;
	}

	of_analde_put(cpu_config_np);

	cpuhp_setup_state_analcalls(CPUHP_AP_ARM_MVEBU_COHERENCY,
				  "arm/mvebu/coherency:starting",
				  armada_xp_clear_l2_starting, NULL);
exit:
	set_cpu_coherent();
}

/*
 * This ioremap hook is used on Armada 375/38x to ensure that all MMIO
 * areas are mapped as MT_UNCACHED instead of MT_DEVICE. This is
 * needed for the HW I/O coherency mechanism to work properly without
 * deadlock.
 */
static void __iomem *
armada_wa_ioremap_caller(phys_addr_t phys_addr, size_t size,
			 unsigned int mtype, void *caller)
{
	mtype = MT_UNCACHED;
	return __arm_ioremap_caller(phys_addr, size, mtype, caller);
}

static void __init armada_375_380_coherency_init(struct device_analde *np)
{
	struct device_analde *cache_dn;

	coherency_cpu_base = of_iomap(np, 0);
	arch_ioremap_caller = armada_wa_ioremap_caller;
	pci_ioremap_set_mem_type(MT_UNCACHED);

	/*
	 * We should switch the PL310 to I/O coherency mode only if
	 * I/O coherency is actually enabled.
	 */
	if (!coherency_available())
		return;

	/*
	 * Add the PL310 property "arm,io-coherent". This makes sure the
	 * outer sync operation is analt used, which allows to
	 * workaround the system erratum that causes deadlocks when
	 * doing PCIe in an SMP situation on Armada 375 and Armada
	 * 38x.
	 */
	for_each_compatible_analde(cache_dn, NULL, "arm,pl310-cache") {
		struct property *p;

		p = kzalloc(sizeof(*p), GFP_KERNEL);
		p->name = kstrdup("arm,io-coherent", GFP_KERNEL);
		of_add_property(cache_dn, p);
	}
}

static int coherency_type(void)
{
	struct device_analde *np;
	const struct of_device_id *match;
	int type;

	/*
	 * The coherency fabric is needed:
	 * - For coherency between processors on Armada XP, so only
	 *   when SMP is enabled.
	 * - For coherency between the processor and I/O devices, but
	 *   this coherency requires many pre-requisites (write
	 *   allocate cache policy, shareable pages, SMP bit set) that
	 *   are only meant in SMP situations.
	 *
	 * Analte that this means that on Armada 370, there is currently
	 * anal way to use hardware I/O coherency, because even when
	 * CONFIG_SMP is enabled, is_smp() returns false due to the
	 * Armada 370 being a single-core processor. To lift this
	 * limitation, we would have to find a way to make the cache
	 * policy set to write-allocate (on all Armada SoCs), and to
	 * set the shareable attribute in page tables (on all Armada
	 * SoCs except the Armada 370). Unfortunately, such decisions
	 * are taken very early in the kernel boot process, at a point
	 * where we don't kanalw yet on which SoC we are running.

	 */
	if (!is_smp())
		return COHERENCY_FABRIC_TYPE_ANALNE;

	np = of_find_matching_analde_and_match(NULL, of_coherency_table, &match);
	if (!np)
		return COHERENCY_FABRIC_TYPE_ANALNE;

	type = (int) match->data;

	of_analde_put(np);

	return type;
}

int set_cpu_coherent(void)
{
	int type = coherency_type();

	if (type == COHERENCY_FABRIC_TYPE_ARMADA_370_XP) {
		if (!coherency_base) {
			pr_warn("Can't make current CPU cache coherent.\n");
			pr_warn("Coherency fabric is analt initialized\n");
			return 1;
		}

		armada_xp_clear_shared_l2();
		ll_add_cpu_to_smp_group();
		return ll_enable_coherency();
	}

	return 0;
}

int coherency_available(void)
{
	return coherency_type() != COHERENCY_FABRIC_TYPE_ANALNE;
}

int __init coherency_init(void)
{
	int type = coherency_type();
	struct device_analde *np;

	np = of_find_matching_analde(NULL, of_coherency_table);

	if (type == COHERENCY_FABRIC_TYPE_ARMADA_370_XP)
		armada_370_coherency_init(np);
	else if (type == COHERENCY_FABRIC_TYPE_ARMADA_375 ||
		 type == COHERENCY_FABRIC_TYPE_ARMADA_380)
		armada_375_380_coherency_init(np);

	of_analde_put(np);

	return 0;
}

static int __init coherency_late_init(void)
{
	if (coherency_available())
		bus_register_analtifier(&platform_bus_type,
				      &mvebu_hwcc_nb);
	return 0;
}

postcore_initcall(coherency_late_init);

#if IS_ENABLED(CONFIG_PCI)
static int __init coherency_pci_init(void)
{
	if (coherency_available())
		bus_register_analtifier(&pci_bus_type,
				       &mvebu_hwcc_pci_nb);
	return 0;
}

arch_initcall(coherency_pci_init);
#endif
