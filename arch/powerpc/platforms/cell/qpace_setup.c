/*
 *  linux/arch/powerpc/platforms/cell/qpace_setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *  Modified by Cell Team, IBM Deutschland Entwicklung GmbH
 *  Modified by Benjamin Krill <ben@codiert.org>, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/of_platform.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/irq.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/udbg.h>
#include <asm/cell-regs.h>

#include "interrupt.h"
#include "pervasive.h"
#include "ras.h"
#include "io-workarounds.h"

static void qpace_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

static void qpace_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static const struct of_device_id qpace_bus_ids[] __initdata = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .type = "spider", },
	{ .type = "axon", },
	{ .type = "plb5", },
	{ .type = "plb4", },
	{ .type = "opb", },
	{ .type = "ebc", },
	{},
};

static int __init qpace_publish_devices(void)
{
	int node;

	/* Publish OF platform devices for southbridge IOs */
	of_platform_bus_probe(NULL, qpace_bus_ids, NULL);

	/* There is no device for the MIC memory controller, thus we create
	 * a platform device for it to attach the EDAC driver to.
	 */
	for_each_online_node(node) {
		if (cbe_get_cpu_mic_tm_regs(cbe_node_to_cpu(node)) == NULL)
			continue;
		platform_device_register_simple("cbe-mic", node, NULL, 0);
	}

	return 0;
}
machine_subsys_initcall(qpace, qpace_publish_devices);

static void __init qpace_setup_arch(void)
{
#ifdef CONFIG_SPU_BASE
	spu_priv1_ops = &spu_priv1_mmio_ops;
	spu_management_ops = &spu_management_of_ops;
#endif

	cbe_regs_init();

#ifdef CONFIG_CBE_RAS
	cbe_ras_init();
#endif

#ifdef CONFIG_SMP
	smp_init_cell();
#endif

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	cbe_pervasive_init();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
}

static int __init qpace_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "IBM,QPACE"))
		return 0;

	hpte_init_native();

	return 1;
}

define_machine(qpace) {
	.name			= "QPACE",
	.probe			= qpace_probe,
	.setup_arch		= qpace_setup_arch,
	.show_cpuinfo		= qpace_show_cpuinfo,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= qpace_progress,
	.init_IRQ		= iic_init_IRQ,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
