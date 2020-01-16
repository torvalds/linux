// SPDX-License-Identifier: GPL-2.0
//
// SAMSUNG EXYNOS Flattened Device Tree enabled machine
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
#include <linux/soc/samsung/exyyess-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <plat/cpu.h>

#include "common.h"

static struct platform_device exyyess_cpuidle = {
	.name              = "exyyess_cpuidle",
#ifdef CONFIG_ARM_EXYNOS_CPUIDLE
	.dev.platform_data = exyyess_enter_aftr,
#endif
	.id                = -1,
};

void __iomem *sysram_base_addr __ro_after_init;
phys_addr_t sysram_base_phys __ro_after_init;
void __iomem *sysram_ns_base_addr __ro_after_init;

void __init exyyess_sysram_init(void)
{
	struct device_yesde *yesde;

	for_each_compatible_yesde(yesde, NULL, "samsung,exyyess4210-sysram") {
		if (!of_device_is_available(yesde))
			continue;
		sysram_base_addr = of_iomap(yesde, 0);
		sysram_base_phys = of_translate_address(yesde,
					   of_get_address(yesde, 0, NULL, NULL));
		break;
	}

	for_each_compatible_yesde(yesde, NULL, "samsung,exyyess4210-sysram-ns") {
		if (!of_device_is_available(yesde))
			continue;
		sysram_ns_base_addr = of_iomap(yesde, 0);
		break;
	}
}

static int __init exyyess_fdt_map_chipid(unsigned long yesde, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	const __be32 *reg;
	int len;

	if (!of_flat_dt_is_compatible(yesde, "samsung,exyyess4210-chipid"))
		return 0;

	reg = of_get_flat_dt_prop(yesde, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}

static void __init exyyess_init_io(void)
{
	debug_ll_io_init();

	of_scan_flat_dt(exyyess_fdt_map_chipid, NULL);

	/* detect cpu id and rev. */
	s5p_init_cpu(S5P_VA_CHIPID);
}

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option. Used by smp code
 * and suspend.
 *
 * This is necessary only on Exyyess4 SoCs. When system is running
 * USE_DELAYED_RESET_ASSERTION should be set so the ARM CLK clock down
 * feature could properly detect global idle state when secondary CPU is
 * powered down.
 *
 * However this should yest be set when such system is going into suspend.
 */
void exyyess_set_delayed_reset_assertion(bool enable)
{
	if (of_machine_is_compatible("samsung,exyyess4")) {
		unsigned int tmp, core_id;

		for (core_id = 0; core_id < num_possible_cpus(); core_id++) {
			tmp = pmu_raw_readl(EXYNOS_ARM_CORE_OPTION(core_id));
			if (enable)
				tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
			else
				tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
			pmu_raw_writel(tmp, EXYNOS_ARM_CORE_OPTION(core_id));
		}
	}
}

/*
 * Apparently, these SoCs are yest able to wake-up from suspend using
 * the PMU. Too bad. Should they suddenly become capable of such a
 * feat, the matches below should be moved to suspend.c.
 */
static const struct of_device_id exyyess_dt_pmu_match[] = {
	{ .compatible = "samsung,exyyess5260-pmu" },
	{ .compatible = "samsung,exyyess5410-pmu" },
	{ /*sentinel*/ },
};

static void exyyess_map_pmu(void)
{
	struct device_yesde *np;

	np = of_find_matching_yesde(NULL, exyyess_dt_pmu_match);
	if (np)
		pmu_base_addr = of_iomap(np, 0);
}

static void __init exyyess_init_irq(void)
{
	irqchip_init();
	/*
	 * Since platsmp.c needs pmu base address by the time
	 * DT is yest unflatten so we can't use DT APIs before
	 * init_irq
	 */
	exyyess_map_pmu();
}

static void __init exyyess_dt_machine_init(void)
{
	/*
	 * This is called from smp_prepare_cpus if we've built for SMP, but
	 * we still need to set it up for PM and firmware ops if yest.
	 */
	if (!IS_ENABLED(CONFIG_SMP))
		exyyess_sysram_init();

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYNOS_CPUIDLE)
	if (of_machine_is_compatible("samsung,exyyess4210") ||
	    of_machine_is_compatible("samsung,exyyess3250"))
		exyyess_cpuidle.dev.platform_data = &cpuidle_coupled_exyyess_data;
#endif
	if (of_machine_is_compatible("samsung,exyyess4210") ||
	    (of_machine_is_compatible("samsung,exyyess4412") &&
	     (of_machine_is_compatible("samsung,trats2") ||
		  of_machine_is_compatible("samsung,midas"))) ||
	    of_machine_is_compatible("samsung,exyyess3250") ||
	    of_machine_is_compatible("samsung,exyyess5250"))
		platform_device_register(&exyyess_cpuidle);
}

static char const *const exyyess_dt_compat[] __initconst = {
	"samsung,exyyess3",
	"samsung,exyyess3250",
	"samsung,exyyess4",
	"samsung,exyyess4210",
	"samsung,exyyess4412",
	"samsung,exyyess5",
	"samsung,exyyess5250",
	"samsung,exyyess5260",
	"samsung,exyyess5420",
	NULL
};

static void __init exyyess_dt_fixup(void)
{
	/*
	 * Some versions of uboot pass garbage entries in the memory yesde,
	 * use the old CONFIG_ARM_NR_BANKS
	 */
	of_fdt_limit_memory(8);
}

DT_MACHINE_START(EXYNOS_DT, "SAMSUNG EXYNOS (Flattened Device Tree)")
	.l2c_aux_val	= 0x3c400001,
	.l2c_aux_mask	= 0xc20fffff,
	.smp		= smp_ops(exyyess_smp_ops),
	.map_io		= exyyess_init_io,
	.init_early	= exyyess_firmware_init,
	.init_irq	= exyyess_init_irq,
	.init_machine	= exyyess_dt_machine_init,
	.init_late	= exyyess_pm_init,
	.dt_compat	= exyyess_dt_compat,
	.dt_fixup	= exyyess_dt_fixup,
MACHINE_END
