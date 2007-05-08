/*
 *  linux/arch/powerpc/platforms/cell/cell_setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *  Modified by Cell Team, IBM Deutschland Entwicklung GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#undef DEBUG

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/mutex.h>
#include <linux/memory_hotplug.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include <asm/cputable.h>
#include <asm/ppc-pci.h>
#include <asm/irq.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/of_platform.h>

#include "interrupt.h"
#include "cbe_regs.h"
#include "pervasive.h"
#include "ras.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static void cell_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

static void cell_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static int __init cell_publish_devices(void)
{
	if (!machine_is(cell))
		return 0;

	/* Publish OF platform devices for southbridge IOs */
	of_platform_bus_probe(NULL, NULL, NULL);

	return 0;
}
device_initcall(cell_publish_devices);

static void cell_mpic_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct mpic *mpic = desc->handler_data;
	unsigned int virq;

	virq = mpic_get_one_irq(mpic);
	if (virq != NO_IRQ)
		generic_handle_irq(virq);
	desc->chip->eoi(irq);
}

static void __init mpic_init_IRQ(void)
{
	struct device_node *dn;
	struct mpic *mpic;
	unsigned int virq;

	for (dn = NULL;
	     (dn = of_find_node_by_name(dn, "interrupt-controller"));) {
		if (!of_device_is_compatible(dn, "CBEA,platform-open-pic"))
			continue;

		/* The MPIC driver will get everything it needs from the
		 * device-tree, just pass 0 to all arguments
		 */
		mpic = mpic_alloc(dn, 0, 0, 0, 0, " MPIC     ");
		if (mpic == NULL)
			continue;
		mpic_init(mpic);

		virq = irq_of_parse_and_map(dn, 0);
		if (virq == NO_IRQ)
			continue;

		printk(KERN_INFO "%s : hooking up to IRQ %d\n",
		       dn->full_name, virq);
		set_irq_data(virq, mpic);
		set_irq_chained_handler(virq, cell_mpic_cascade);
	}
}


static void __init cell_init_irq(void)
{
	iic_init_IRQ();
	spider_init_IRQ();
	mpic_init_IRQ();
}

static void __init cell_setup_arch(void)
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

	if (ROOT_DEV == 0) {
		printk("No ramdisk, default root is /dev/hda2\n");
		ROOT_DEV = Root_HDA2;
	}

	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
	find_and_init_phbs();
	cbe_pervasive_init();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	mmio_nvram_init();
}

static int __init cell_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "IBM,CBEA") &&
	    !of_flat_dt_is_compatible(root, "IBM,CPBW-1.0"))
		return 0;

	hpte_init_native();

	return 1;
}

define_machine(cell) {
	.name			= "Cell",
	.probe			= cell_probe,
	.setup_arch		= cell_setup_arch,
	.show_cpuinfo		= cell_show_cpuinfo,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= cell_progress,
	.init_IRQ       	= cell_init_irq,
	.pci_setup_phb		= rtas_setup_phb,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
