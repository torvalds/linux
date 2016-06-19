/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
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

#define pr_fmt(fmt)		"uniphier: " fmt

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sizes.h>
#include <asm/cacheflush.h>
#include <asm/hardware/cache-uniphier.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/smp_scu.h>

/*
 * The secondary CPUs check this register from the boot ROM for the jump
 * destination.  After that, it can be reused as a scratch register.
 */
#define UNIPHIER_SMPCTRL_ROM_RSV2	0x208

static void __iomem *uniphier_smp_rom_boot_rsv2;
static unsigned int uniphier_smp_max_cpus;

extern char uniphier_smp_trampoline;
extern char uniphier_smp_trampoline_jump;
extern char uniphier_smp_trampoline_poll_addr;
extern char uniphier_smp_trampoline_end;

/*
 * Copy trampoline code to the tail of the 1st section of the page table used
 * in the boot ROM.  This area is directly accessible by the secondary CPUs
 * for all the UniPhier SoCs.
 */
static const phys_addr_t uniphier_smp_trampoline_dest_end = SECTION_SIZE;
static phys_addr_t uniphier_smp_trampoline_dest;

static int __init uniphier_smp_copy_trampoline(phys_addr_t poll_addr)
{
	size_t trmp_size;
	static void __iomem *trmp_base;

	if (!uniphier_cache_l2_is_enabled()) {
		pr_warn("outer cache is needed for SMP, but not enabled\n");
		return -ENODEV;
	}

	uniphier_cache_l2_set_locked_ways(1);

	outer_flush_all();

	trmp_size = &uniphier_smp_trampoline_end - &uniphier_smp_trampoline;
	uniphier_smp_trampoline_dest = uniphier_smp_trampoline_dest_end -
								trmp_size;

	uniphier_cache_l2_touch_range(uniphier_smp_trampoline_dest,
				      uniphier_smp_trampoline_dest_end);

	trmp_base = ioremap_cache(uniphier_smp_trampoline_dest, trmp_size);
	if (!trmp_base) {
		pr_err("failed to map trampoline destination area\n");
		return -ENOMEM;
	}

	memcpy(trmp_base, &uniphier_smp_trampoline, trmp_size);

	writel(virt_to_phys(secondary_startup),
	       trmp_base + (&uniphier_smp_trampoline_jump -
			    &uniphier_smp_trampoline));

	writel(poll_addr, trmp_base + (&uniphier_smp_trampoline_poll_addr -
				       &uniphier_smp_trampoline));

	flush_cache_all();	/* flush out trampoline code to outer cache */

	iounmap(trmp_base);

	return 0;
}

static int __init uniphier_smp_prepare_trampoline(unsigned int max_cpus)
{
	struct device_node *np;
	struct resource res;
	phys_addr_t rom_rsv2_phys;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "socionext,uniphier-smpctrl");
	of_node_put(np);
	ret = of_address_to_resource(np, 0, &res);
	if (!ret) {
		rom_rsv2_phys = res.start + UNIPHIER_SMPCTRL_ROM_RSV2;
	} else {
		/* try old binding too */
		np = of_find_compatible_node(NULL, NULL,
					     "socionext,uniphier-system-bus-controller");
		of_node_put(np);
		ret = of_address_to_resource(np, 1, &res);
		if (ret) {
			pr_err("failed to get resource of SMP control\n");
			return ret;
		}
		rom_rsv2_phys = res.start + 0x1000 + UNIPHIER_SMPCTRL_ROM_RSV2;
	}

	ret = uniphier_smp_copy_trampoline(rom_rsv2_phys);
	if (ret)
		return ret;

	uniphier_smp_rom_boot_rsv2 = ioremap(rom_rsv2_phys, SZ_4);
	if (!uniphier_smp_rom_boot_rsv2) {
		pr_err("failed to map ROM_BOOT_RSV2 register\n");
		return -ENOMEM;
	}

	writel(uniphier_smp_trampoline_dest, uniphier_smp_rom_boot_rsv2);
	asm("sev"); /* Bring up all secondary CPUs to the trampoline code */

	uniphier_smp_max_cpus = max_cpus;	/* save for later use */

	return 0;
}

static void __init uniphier_smp_unprepare_trampoline(void)
{
	iounmap(uniphier_smp_rom_boot_rsv2);

	if (uniphier_smp_trampoline_dest)
		outer_inv_range(uniphier_smp_trampoline_dest,
				uniphier_smp_trampoline_dest_end);

	uniphier_cache_l2_set_locked_ways(0);
}

static int __init uniphier_smp_enable_scu(void)
{
	unsigned long scu_base_phys = 0;
	void __iomem *scu_base;

	if (scu_a9_has_base())
		scu_base_phys = scu_a9_get_base();

	if (!scu_base_phys) {
		pr_err("failed to get scu base\n");
		return -ENODEV;
	}

	scu_base = ioremap(scu_base_phys, SZ_128);
	if (!scu_base) {
		pr_err("failed to map scu base\n");
		return -ENOMEM;
	}

	scu_enable(scu_base);
	iounmap(scu_base);

	return 0;
}

static void __init uniphier_smp_prepare_cpus(unsigned int max_cpus)
{
	static cpumask_t only_cpu_0 = { CPU_BITS_CPU0 };
	int ret;

	ret = uniphier_smp_prepare_trampoline(max_cpus);
	if (ret)
		goto err;

	ret = uniphier_smp_enable_scu();
	if (ret)
		goto err;

	return;
err:
	pr_warn("disabling SMP\n");
	init_cpu_present(&only_cpu_0);
	uniphier_smp_unprepare_trampoline();
}

static int __init uniphier_smp_boot_secondary(unsigned int cpu,
					      struct task_struct *idle)
{
	if (WARN_ON_ONCE(!uniphier_smp_rom_boot_rsv2))
		return -EFAULT;

	writel(cpu, uniphier_smp_rom_boot_rsv2);
	readl(uniphier_smp_rom_boot_rsv2); /* relax */

	asm("sev"); /* wake up secondary CPUs sleeping in the trampoline */

	if (cpu == uniphier_smp_max_cpus - 1) {
		/* clean up resources if this is the last CPU */
		uniphier_smp_unprepare_trampoline();
	}

	return 0;
}

static const struct smp_operations uniphier_smp_ops __initconst = {
	.smp_prepare_cpus	= uniphier_smp_prepare_cpus,
	.smp_boot_secondary	= uniphier_smp_boot_secondary,
};
CPU_METHOD_OF_DECLARE(uniphier_smp, "socionext,uniphier-smp",
		      &uniphier_smp_ops);
