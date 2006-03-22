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

#include "interrupt.h"
#include "iommu.h"
#include "pervasive.h"

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
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

#ifdef CONFIG_SPARSEMEM
static int __init find_spu_node_id(struct device_node *spe)
{
	unsigned int *id;
#ifdef CONFIG_NUMA
	struct device_node *cpu;
	cpu = spe->parent->parent;
	id = (unsigned int *)get_property(cpu, "node-id", NULL);
#else
	id = NULL;
#endif
	return id ? *id : 0;
}

static void __init cell_spuprop_present(struct device_node *spe,
				       const char *prop, int early)
{
	struct address_prop {
		unsigned long address;
		unsigned int len;
	} __attribute__((packed)) *p;
	int proplen;

	unsigned long start_pfn, end_pfn, pfn;
	int node_id;

	p = (void*)get_property(spe, prop, &proplen);
	WARN_ON(proplen != sizeof (*p));

	node_id = find_spu_node_id(spe);

	start_pfn = p->address >> PAGE_SHIFT;
	end_pfn = (p->address + p->len + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* We need to call memory_present *before* the call to sparse_init,
	   but we can initialize the page structs only *after* that call.
	   Thus, we're being called twice. */
	if (early)
		memory_present(node_id, start_pfn, end_pfn);
	else {
		/* As the pages backing SPU LS and I/O are outside the range
		   of regular memory, their page structs were not initialized
		   by free_area_init. Do it here instead. */
		for (pfn = start_pfn; pfn < end_pfn; pfn++) {
			struct page *page = pfn_to_page(pfn);
			set_page_links(page, ZONE_DMA, node_id, pfn);
			init_page_count(page);
			reset_page_mapcount(page);
			SetPageReserved(page);
			INIT_LIST_HEAD(&page->lru);
		}
	}
}

static void __init cell_spumem_init(int early)
{
	struct device_node *node;
	for (node = of_find_node_by_type(NULL, "spe");
			node; node = of_find_node_by_type(node, "spe")) {
		cell_spuprop_present(node, "local-store", early);
		cell_spuprop_present(node, "problem", early);
		cell_spuprop_present(node, "priv1", early);
		cell_spuprop_present(node, "priv2", early);
	}
}
#else
static void __init cell_spumem_init(int early)
{
}
#endif

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
	cell_pervasive_init();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	mmio_nvram_init();

	cell_spumem_init(0);
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

	cell_spumem_init(1);

	DBG(" <- cell_init_early()\n");
}


static int __init cell_probe(int platform)
{
	if (platform != PLATFORM_CELL)
		return 0;

	return 1;
}

/*
 * Cell has no legacy IO; anything calling this function has to
 * fail or bad things will happen
 */
static int cell_check_legacy_ioport(unsigned int baseport)
{
	return -ENODEV;
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
	.check_legacy_ioport	= cell_check_legacy_ioport,
	.progress		= cell_progress,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
