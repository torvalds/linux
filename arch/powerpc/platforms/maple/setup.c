/*
 *  Maple (970 eval board) setup code
 *
 *  (c) Copyright 2004 Benjamin Herrenschmidt (benh@kernel.crashing.org),
 *                     IBM Corp. 
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#define DEBUG

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/dma.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/of_device.h>
#include <asm/lmb.h>
#include <asm/mpic.h>
#include <asm/udbg.h>

#include "maple.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static unsigned long maple_find_nvram_base(void)
{
	struct device_node *rtcs;
	unsigned long result = 0;

	/* find NVRAM device */
	rtcs = of_find_compatible_node(NULL, "nvram", "AMD8111");
	if (rtcs) {
		struct resource r;
		if (of_address_to_resource(rtcs, 0, &r)) {
			printk(KERN_EMERG "Maple: Unable to translate NVRAM"
			       " address\n");
			goto bail;
		}
		if (!(r.flags & IORESOURCE_IO)) {
			printk(KERN_EMERG "Maple: NVRAM address isn't PIO!\n");
			goto bail;
		}
		result = r.start;
	} else
		printk(KERN_EMERG "Maple: Unable to find NVRAM\n");
 bail:
	of_node_put(rtcs);
	return result;
}

static void maple_restart(char *cmd)
{
	unsigned int maple_nvram_base;
	unsigned int maple_nvram_offset;
	unsigned int maple_nvram_command;
	struct device_node *sp;

	maple_nvram_base = maple_find_nvram_base();
	if (maple_nvram_base == 0)
		goto fail;

	/* find service processor device */
	sp = of_find_node_by_name(NULL, "service-processor");
	if (!sp) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		goto fail;
	}
	maple_nvram_offset = *(unsigned int*) get_property(sp,
			"restart-addr", NULL);
	maple_nvram_command = *(unsigned int*) get_property(sp,
			"restart-value", NULL);
	of_node_put(sp);

	/* send command */
	outb_p(maple_nvram_command, maple_nvram_base + maple_nvram_offset);
	for (;;) ;
 fail:
	printk(KERN_EMERG "Maple: Manual Restart Required\n");
}

static void maple_power_off(void)
{
	unsigned int maple_nvram_base;
	unsigned int maple_nvram_offset;
	unsigned int maple_nvram_command;
	struct device_node *sp;

	maple_nvram_base = maple_find_nvram_base();
	if (maple_nvram_base == 0)
		goto fail;

	/* find service processor device */
	sp = of_find_node_by_name(NULL, "service-processor");
	if (!sp) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		goto fail;
	}
	maple_nvram_offset = *(unsigned int*) get_property(sp,
			"power-off-addr", NULL);
	maple_nvram_command = *(unsigned int*) get_property(sp,
			"power-off-value", NULL);
	of_node_put(sp);

	/* send command */
	outb_p(maple_nvram_command, maple_nvram_base + maple_nvram_offset);
	for (;;) ;
 fail:
	printk(KERN_EMERG "Maple: Manual Power-Down Required\n");
}

static void maple_halt(void)
{
	maple_power_off();
}

#ifdef CONFIG_SMP
struct smp_ops_t maple_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
};
#endif /* CONFIG_SMP */

void __init maple_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	/* Setup SMP callback */
#ifdef CONFIG_SMP
	smp_ops = &maple_smp_ops;
#endif
	/* Lookup PCI hosts */
       	maple_pci_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	printk(KERN_INFO "Using native/NAP idle loop\n");
}

/* 
 * Early initialization.
 */
static void __init maple_init_early(void)
{
	DBG(" -> maple_init_early\n");

	/* Initialize hash table, from now on, we can take hash faults
	 * and call ioremap
	 */
	hpte_init_native();

	/* Setup interrupt mapping options */
	ppc64_interrupt_controller = IC_OPEN_PIC;

	iommu_init_early_dart();

	DBG(" <- maple_init_early\n");
}


static __init void maple_init_IRQ(void)
{
	struct device_node *root;
	unsigned int *opprop;
	unsigned long opic_addr;
	struct mpic *mpic;
	unsigned char senses[128];
	int n;

	DBG(" -> maple_init_IRQ\n");

	/* XXX: Non standard, replace that with a proper openpic/mpic node
	 * in the device-tree. Find the Open PIC if present */
	root = of_find_node_by_path("/");
	opprop = (unsigned int *) get_property(root,
				"platform-open-pic", NULL);
	if (opprop == 0)
		panic("OpenPIC not found !\n");

	n = prom_n_addr_cells(root);
	for (opic_addr = 0; n > 0; --n)
		opic_addr = (opic_addr << 32) + *opprop++;
	of_node_put(root);

	/* Obtain sense values from device-tree */
	prom_get_irq_senses(senses, 0, 128);

	mpic = mpic_alloc(opic_addr,
			  MPIC_PRIMARY | MPIC_BIG_ENDIAN |
			  MPIC_BROKEN_U3 | MPIC_WANTS_RESET,
			  0, 0, 128, 128, senses, 128, "U3-MPIC");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);

	DBG(" <- maple_init_IRQ\n");
}

static void __init maple_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}


/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init maple_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(root, "Momentum,Maple"))
		return 0;
	/*
	 * On U3, the DART (iommu) must be allocated now since it
	 * has an impact on htab_initialize (due to the large page it
	 * occupies having to be broken up so the DART itself is not
	 * part of the cacheable linar mapping
	 */
	alloc_dart_table();

	return 1;
}

define_machine(maple_md) {
	.name			= "Maple",
	.probe			= maple_probe,
	.setup_arch		= maple_setup_arch,
	.init_early		= maple_init_early,
	.init_IRQ		= maple_init_IRQ,
	.get_irq		= mpic_get_irq,
	.pcibios_fixup		= maple_pcibios_fixup,
	.pci_get_legacy_ide_irq	= maple_pci_get_legacy_ide_irq,
	.restart		= maple_restart,
	.power_off		= maple_power_off,
	.halt			= maple_halt,
       	.get_boot_time		= maple_get_boot_time,
       	.set_rtc_time		= maple_set_rtc_time,
       	.get_rtc_time		= maple_get_rtc_time,
      	.calibrate_decr		= generic_calibrate_decr,
	.progress		= maple_progress,
	.power_save		= power4_idle,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
