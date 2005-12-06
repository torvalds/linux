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

#include <linux/config.h>
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

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
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

#include "interrupt.h"
#include "iommu.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

void cell_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

static void cell_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static void __init cell_setup_arch(void)
{
	ppc_md.init_IRQ       = iic_init_IRQ;
	ppc_md.get_irq        = iic_get_irq;

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
	spider_init_IRQ();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	mmio_nvram_init();
}

/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init cell_init_early(void)
{
	DBG(" -> cell_init_early()\n");

	hpte_init_native();

	cell_init_iommu();

	ppc64_interrupt_controller = IC_CELL_PIC;

	DBG(" <- cell_init_early()\n");
}


static int __init cell_probe(int platform)
{
	if (platform != PLATFORM_CELL)
		return 0;

	return 1;
}

struct machdep_calls __initdata cell_md = {
	.probe			= cell_probe,
	.setup_arch		= cell_setup_arch,
	.init_early		= cell_init_early,
	.show_cpuinfo		= cell_show_cpuinfo,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= cell_progress,
};
