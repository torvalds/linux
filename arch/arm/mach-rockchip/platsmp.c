// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/reset.h>
#include <linux/cpu.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include <asm/mach/map.h>

#include "core.h"

static void __iomem *scu_base_addr;
static void __iomem *sram_base_addr;
static int ncores;

#define PMU_PWRDN_CON		0x08
#define PMU_PWRDN_ST		0x0c

#define PMU_PWRDN_SCU		4

static struct regmap *pmu;
static int has_pmu = true;

static int pmu_power_domain_is_on(int pd)
{
	u32 val;
	int ret;

	ret = regmap_read(pmu, PMU_PWRDN_ST, &val);
	if (ret < 0)
		return ret;

	return !(val & BIT(pd));
}

static struct reset_control *rockchip_get_core_reset(int cpu)
{
	struct device *dev = get_cpu_device(cpu);
	struct device_node *np;

	/* The cpu device is only available after the initial core bringup */
	if (dev)
		np = dev->of_node;
	else
		np = of_get_cpu_node(cpu, NULL);

	return of_reset_control_get_exclusive(np, NULL);
}

static int pmu_set_power_domain(int pd, bool on)
{
	u32 val = (on) ? 0 : BIT(pd);
	struct reset_control *rstc = rockchip_get_core_reset(pd);
	int ret;

	if (IS_ERR(rstc) && read_cpuid_part() != ARM_CPU_PART_CORTEX_A9) {
		pr_err("%s: could not get reset control for core %d\n",
		       __func__, pd);
		return PTR_ERR(rstc);
	}

	/*
	 * We need to soft reset the cpu when we turn off the cpu power domain,
	 * or else the active processors might be stalled when the individual
	 * processor is powered down.
	 */
	if (!IS_ERR(rstc) && !on)
		reset_control_assert(rstc);

	if (has_pmu) {
		ret = regmap_update_bits(pmu, PMU_PWRDN_CON, BIT(pd), val);
		if (ret < 0) {
			pr_err("%s: could not update power domain\n",
			       __func__);
			return ret;
		}

		ret = -1;
		while (ret != on) {
			ret = pmu_power_domain_is_on(pd);
			if (ret < 0) {
				pr_err("%s: could not read power domain state\n",
				       __func__);
				return ret;
			}
		}
	}

	if (!IS_ERR(rstc)) {
		if (on)
			reset_control_deassert(rstc);
		reset_control_put(rstc);
	}

	return 0;
}

/*
 * Handling of CPU cores
 */

static int rockchip_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	if (!sram_base_addr || (has_pmu && !pmu)) {
		pr_err("%s: sram or pmu missing for cpu boot\n", __func__);
		return -ENXIO;
	}

	if (cpu >= ncores) {
		pr_err("%s: cpu %d outside maximum number of cpus %d\n",
		       __func__, cpu, ncores);
		return -ENXIO;
	}

	/* start the core */
	ret = pmu_set_power_domain(0 + cpu, true);
	if (ret < 0)
		return ret;

	if (read_cpuid_part() != ARM_CPU_PART_CORTEX_A9) {
		/*
		 * We communicate with the bootrom to active the cpus other
		 * than cpu0, after a blob of initialize code, they will
		 * stay at wfe state, once they are actived, they will check
		 * the mailbox:
		 * sram_base_addr + 4: 0xdeadbeaf
		 * sram_base_addr + 8: start address for pc
		 * The cpu0 need to wait the other cpus other than cpu0 entering
		 * the wfe state.The wait time is affected by many aspects.
		 * (e.g: cpu frequency, bootrom frequency, sram frequency, ...)
		 */
		mdelay(1); /* ensure the cpus other than cpu0 to startup */

		writel(__pa_symbol(secondary_startup), sram_base_addr + 8);
		writel(0xDEADBEAF, sram_base_addr + 4);
		dsb_sev();
	}

	return 0;
}

/**
 * rockchip_smp_prepare_sram - populate necessary sram block
 * Starting cores execute the code residing at the start of the on-chip sram
 * after power-on. Therefore make sure, this sram region is reserved and
 * big enough. After this check, copy the trampoline code that directs the
 * core to the real startup code in ram into the sram-region.
 * @node: mmio-sram device node
 */
static int __init rockchip_smp_prepare_sram(struct device_node *node)
{
	unsigned int trampoline_sz = &rockchip_secondary_trampoline_end -
					    &rockchip_secondary_trampoline;
	struct resource res;
	unsigned int rsize;
	int ret;

	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("%s: could not get address for node %pOF\n",
		       __func__, node);
		return ret;
	}

	rsize = resource_size(&res);
	if (rsize < trampoline_sz) {
		pr_err("%s: reserved block with size 0x%x is to small for trampoline size 0x%x\n",
		       __func__, rsize, trampoline_sz);
		return -EINVAL;
	}

	/* set the boot function for the sram code */
	rockchip_boot_fn = __pa_symbol(secondary_startup);

	/* copy the trampoline to sram, that runs during startup of the core */
	memcpy(sram_base_addr, &rockchip_secondary_trampoline, trampoline_sz);
	flush_cache_all();
	outer_clean_range(0, trampoline_sz);

	dsb_sev();

	return 0;
}

static const struct regmap_config rockchip_pmu_regmap_config = {
	.name = "rockchip-pmu",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int __init rockchip_smp_prepare_pmu(void)
{
	struct device_node *node;
	void __iomem *pmu_base;

	/*
	 * This function is only called via smp_ops->smp_prepare_cpu().
	 * That only happens if a "/cpus" device tree node exists
	 * and has an "enable-method" property that selects the SMP
	 * operations defined herein.
	 */
	node = of_find_node_by_path("/cpus");

	pmu = syscon_regmap_lookup_by_phandle(node, "rockchip,pmu");
	of_node_put(node);
	if (!IS_ERR(pmu))
		return 0;

	pmu = syscon_regmap_lookup_by_compatible("rockchip,rk3066-pmu");
	if (!IS_ERR(pmu))
		return 0;

	/* fallback, create our own regmap for the pmu area */
	pmu = NULL;
	node = of_find_compatible_node(NULL, NULL, "rockchip,rk3066-pmu");
	if (!node) {
		pr_err("%s: could not find pmu dt node\n", __func__);
		return -ENODEV;
	}

	pmu_base = of_iomap(node, 0);
	of_node_put(node);
	if (!pmu_base) {
		pr_err("%s: could not map pmu registers\n", __func__);
		return -ENOMEM;
	}

	pmu = regmap_init_mmio(NULL, pmu_base, &rockchip_pmu_regmap_config);
	if (IS_ERR(pmu)) {
		int ret = PTR_ERR(pmu);

		iounmap(pmu_base);
		pmu = NULL;
		pr_err("%s: regmap init failed\n", __func__);
		return ret;
	}

	return 0;
}

static void __init rockchip_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *node;
	unsigned int i;

	node = of_find_compatible_node(NULL, NULL, "rockchip,rk3066-smp-sram");
	if (!node) {
		pr_err("%s: could not find sram dt node\n", __func__);
		return;
	}

	sram_base_addr = of_iomap(node, 0);
	if (!sram_base_addr) {
		pr_err("%s: could not map sram registers\n", __func__);
		of_node_put(node);
		return;
	}

	if (has_pmu && rockchip_smp_prepare_pmu()) {
		of_node_put(node);
		return;
	}

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9) {
		if (rockchip_smp_prepare_sram(node)) {
			of_node_put(node);
			return;
		}

		/* enable the SCU power domain */
		pmu_set_power_domain(PMU_PWRDN_SCU, true);

		of_node_put(node);
		node = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");
		if (!node) {
			pr_err("%s: missing scu\n", __func__);
			return;
		}

		scu_base_addr = of_iomap(node, 0);
		if (!scu_base_addr) {
			pr_err("%s: could not map scu registers\n", __func__);
			of_node_put(node);
			return;
		}

		/*
		 * While the number of cpus is gathered from dt, also get the
		 * number of cores from the scu to verify this value when
		 * booting the cores.
		 */
		ncores = scu_get_core_count(scu_base_addr);
		pr_err("%s: ncores %d\n", __func__, ncores);

		scu_enable(scu_base_addr);
	} else {
		unsigned int l2ctlr;

		asm ("mrc p15, 1, %0, c9, c0, 2\n" : "=r" (l2ctlr));
		ncores = ((l2ctlr >> 24) & 0x3) + 1;
	}
	of_node_put(node);

	/* Make sure that all cores except the first are really off */
	for (i = 1; i < ncores; i++)
		pmu_set_power_domain(0 + i, false);
}

static void __init rk3036_smp_prepare_cpus(unsigned int max_cpus)
{
	has_pmu = false;

	rockchip_smp_prepare_cpus(max_cpus);
}

#ifdef CONFIG_HOTPLUG_CPU
static int rockchip_cpu_kill(unsigned int cpu)
{
	/*
	 * We need a delay here to ensure that the dying CPU can finish
	 * executing v7_coherency_exit() and reach the WFI/WFE state
	 * prior to having the power domain disabled.
	 */
	mdelay(1);

	pmu_set_power_domain(0 + cpu, false);
	return 1;
}

static void rockchip_cpu_die(unsigned int cpu)
{
	v7_exit_coherency_flush(louis);
	while (1)
		cpu_do_idle();
}
#endif

static const struct smp_operations rk3036_smp_ops __initconst = {
	.smp_prepare_cpus	= rk3036_smp_prepare_cpus,
	.smp_boot_secondary	= rockchip_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= rockchip_cpu_kill,
	.cpu_die		= rockchip_cpu_die,
#endif
};

static const struct smp_operations rockchip_smp_ops __initconst = {
	.smp_prepare_cpus	= rockchip_smp_prepare_cpus,
	.smp_boot_secondary	= rockchip_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= rockchip_cpu_kill,
	.cpu_die		= rockchip_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(rk3036_smp, "rockchip,rk3036-smp", &rk3036_smp_ops);
CPU_METHOD_OF_DECLARE(rk3066_smp, "rockchip,rk3066-smp", &rockchip_smp_ops);
