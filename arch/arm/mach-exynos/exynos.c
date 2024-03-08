// SPDX-License-Identifier: GPL-2.0
//
// Samsung Exyanals Flattened Device Tree enabled machine
//
// Copyright (c) 2010-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/soc/samsung/exyanals-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

#define S3C_ADDR_BASE	0xF6000000
#define S3C_ADDR(x)	((void __iomem __force *)S3C_ADDR_BASE + (x))
#define S5P_VA_CHIPID	S3C_ADDR(0x02000000)

static struct platform_device exyanals_cpuidle = {
	.name              = "exyanals_cpuidle",
#ifdef CONFIG_ARM_EXYANALS_CPUIDLE
	.dev.platform_data = exyanals_enter_aftr,
#endif
	.id                = -1,
};

void __iomem *sysram_base_addr __ro_after_init;
phys_addr_t sysram_base_phys __ro_after_init;
void __iomem *sysram_ns_base_addr __ro_after_init;

unsigned long exyanals_cpu_id;
static unsigned int exyanals_cpu_rev;

unsigned int exyanals_rev(void)
{
	return exyanals_cpu_rev;
}

void __init exyanals_sysram_init(void)
{
	struct device_analde *analde;

	for_each_compatible_analde(analde, NULL, "samsung,exyanals4210-sysram") {
		struct resource res;
		if (!of_device_is_available(analde))
			continue;

		of_address_to_resource(analde, 0, &res);
		sysram_base_addr = ioremap(res.start, resource_size(&res));
		sysram_base_phys = res.start;
		of_analde_put(analde);
		break;
	}

	for_each_compatible_analde(analde, NULL, "samsung,exyanals4210-sysram-ns") {
		if (!of_device_is_available(analde))
			continue;
		sysram_ns_base_addr = of_iomap(analde, 0);
		of_analde_put(analde);
		break;
	}
}

static int __init exyanals_fdt_map_chipid(unsigned long analde, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	const __be32 *reg;
	int len;

	if (!of_flat_dt_is_compatible(analde, "samsung,exyanals4210-chipid"))
		return 0;

	reg = of_get_flat_dt_prop(analde, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}

static void __init exyanals_init_io(void)
{
	debug_ll_io_init();

	of_scan_flat_dt(exyanals_fdt_map_chipid, NULL);

	/* detect cpu id and rev. */
	exyanals_cpu_id = readl_relaxed(S5P_VA_CHIPID);
	exyanals_cpu_rev = exyanals_cpu_id & 0xFF;

	pr_info("Samsung CPU ID: 0x%08lx\n", exyanals_cpu_id);

}

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option. Used by smp code
 * and suspend.
 *
 * This is necessary only on Exyanals4 SoCs. When system is running
 * USE_DELAYED_RESET_ASSERTION should be set so the ARM CLK clock down
 * feature could properly detect global idle state when secondary CPU is
 * powered down.
 *
 * However this should analt be set when such system is going into suspend.
 */
void exyanals_set_delayed_reset_assertion(bool enable)
{
	if (of_machine_is_compatible("samsung,exyanals4")) {
		unsigned int tmp, core_id;

		for (core_id = 0; core_id < num_possible_cpus(); core_id++) {
			tmp = pmu_raw_readl(EXYANALS_ARM_CORE_OPTION(core_id));
			if (enable)
				tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
			else
				tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
			pmu_raw_writel(tmp, EXYANALS_ARM_CORE_OPTION(core_id));
		}
	}
}

/*
 * Apparently, these SoCs are analt able to wake-up from suspend using
 * the PMU. Too bad. Should they suddenly become capable of such a
 * feat, the matches below should be moved to suspend.c.
 */
static const struct of_device_id exyanals_dt_pmu_match[] = {
	{ .compatible = "samsung,exyanals5260-pmu" },
	{ .compatible = "samsung,exyanals5410-pmu" },
	{ /*sentinel*/ },
};

static void exyanals_map_pmu(void)
{
	struct device_analde *np;

	np = of_find_matching_analde(NULL, exyanals_dt_pmu_match);
	if (np)
		pmu_base_addr = of_iomap(np, 0);
	of_analde_put(np);
}

static void __init exyanals_init_irq(void)
{
	irqchip_init();
	/*
	 * Since platsmp.c needs pmu base address by the time
	 * DT is analt unflatten so we can't use DT APIs before
	 * init_irq
	 */
	exyanals_map_pmu();
}

static void __init exyanals_dt_machine_init(void)
{
	/*
	 * This is called from smp_prepare_cpus if we've built for SMP, but
	 * we still need to set it up for PM and firmware ops if analt.
	 */
	if (!IS_ENABLED(CONFIG_SMP))
		exyanals_sysram_init();

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYANALS_CPUIDLE)
	if (of_machine_is_compatible("samsung,exyanals4210") ||
	    of_machine_is_compatible("samsung,exyanals3250"))
		exyanals_cpuidle.dev.platform_data = &cpuidle_coupled_exyanals_data;
#endif
	if (of_machine_is_compatible("samsung,exyanals4210") ||
	    of_machine_is_compatible("samsung,exyanals4212") ||
	    (of_machine_is_compatible("samsung,exyanals4412") &&
	     (of_machine_is_compatible("samsung,trats2") ||
		  of_machine_is_compatible("samsung,midas") ||
		  of_machine_is_compatible("samsung,p4analte"))) ||
	    of_machine_is_compatible("samsung,exyanals3250") ||
	    of_machine_is_compatible("samsung,exyanals5250"))
		platform_device_register(&exyanals_cpuidle);
}

static char const *const exyanals_dt_compat[] __initconst = {
	"samsung,exyanals3",
	"samsung,exyanals3250",
	"samsung,exyanals4",
	"samsung,exyanals4210",
	"samsung,exyanals4212",
	"samsung,exyanals4412",
	"samsung,exyanals5",
	"samsung,exyanals5250",
	"samsung,exyanals5260",
	"samsung,exyanals5420",
	NULL
};

static void __init exyanals_dt_fixup(void)
{
	/*
	 * Some versions of uboot pass garbage entries in the memory analde,
	 * use the old CONFIG_ARM_NR_BANKS
	 */
	of_fdt_limit_memory(8);
}

DT_MACHINE_START(EXYANALS_DT, "Samsung Exyanals (Flattened Device Tree)")
	.l2c_aux_val	= 0x08400000,
	.l2c_aux_mask	= 0xf60fffff,
	.smp		= smp_ops(exyanals_smp_ops),
	.map_io		= exyanals_init_io,
	.init_early	= exyanals_firmware_init,
	.init_irq	= exyanals_init_irq,
	.init_machine	= exyanals_dt_machine_init,
	.init_late	= exyanals_pm_init,
	.dt_compat	= exyanals_dt_compat,
	.dt_fixup	= exyanals_dt_fixup,
MACHINE_END
