/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/smp_scu.h>
#include <asm/mach/map.h>

#include "common.h"
#include "hardware.h"

#define SCU_STANDBY_ENABLE	(1 << 5)

u32 g_diag_reg;
static void __iomem *scu_base;

static struct map_desc scu_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init imx_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.virtual = IMX_IO_P2V(base);
	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);

	scu_base = IMX_IO_ADDRESS(base);
}

void imx_scu_standby_enable(void)
{
	u32 val = readl_relaxed(scu_base);

	val |= SCU_STANDBY_ENABLE;
	writel_relaxed(val, scu_base);
}

static int imx_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	imx_set_cpu_jump(cpu, v7_secondary_startup);
	imx_enable_cpu(cpu, true);
	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init imx_smp_init_cpus(void)
{
	int i, ncores;

	ncores = scu_get_core_count(scu_base);

	for (i = ncores; i < NR_CPUS; i++)
		set_cpu_possible(i, false);
}

void imx_smp_prepare(void)
{
	scu_enable(scu_base);
}

static void __init imx_smp_prepare_cpus(unsigned int max_cpus)
{
	imx_smp_prepare();

	/*
	 * The diagnostic register holds the errata bits.  Mostly bootloader
	 * does not bring up secondary cores, so that when errata bits are set
	 * in bootloader, they are set only for boot cpu.  But on a SMP
	 * configuration, it should be equally done on every single core.
	 * Read the register from boot cpu here, and will replicate it into
	 * secondary cores when booting them.
	 */
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r" (g_diag_reg) : : "cc");
	sync_cache_w(&g_diag_reg);
}

struct smp_operations  imx_smp_ops __initdata = {
	.smp_init_cpus		= imx_smp_init_cpus,
	.smp_prepare_cpus	= imx_smp_prepare_cpus,
	.smp_boot_secondary	= imx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= imx_cpu_die,
	.cpu_kill		= imx_cpu_kill,
#endif
};
