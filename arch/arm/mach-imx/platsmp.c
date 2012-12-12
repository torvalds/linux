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
#include <asm/page.h>
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/hardware.h>

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

static void __cpuinit imx_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);
}

static int __cpuinit imx_boot_secondary(unsigned int cpu, struct task_struct *idle)
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

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void imx_smp_prepare(void)
{
	scu_enable(scu_base);
}

static void __init imx_smp_prepare_cpus(unsigned int max_cpus)
{
	imx_smp_prepare();
}

struct smp_operations  imx_smp_ops __initdata = {
	.smp_init_cpus		= imx_smp_init_cpus,
	.smp_prepare_cpus	= imx_smp_prepare_cpus,
	.smp_secondary_init	= imx_secondary_init,
	.smp_boot_secondary	= imx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= imx_cpu_die,
#endif
};
