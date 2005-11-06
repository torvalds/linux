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

extern void generic_find_legacy_serial_ports(u64 *physport,
		unsigned int *default_speed);

static void maple_restart(char *cmd)
{
	unsigned int maple_nvram_base;
	unsigned int maple_nvram_offset;
	unsigned int maple_nvram_command;
	struct device_node *rtcs;

	/* find NVRAM device */
	rtcs = find_compatible_devices("nvram", "AMD8111");
	if (rtcs && rtcs->addrs) {
		maple_nvram_base = rtcs->addrs[0].address;
	} else {
		printk(KERN_EMERG "Maple: Unable to find NVRAM\n");
		printk(KERN_EMERG "Maple: Manual Restart Required\n");
		return;
	}

	/* find service processor device */
	rtcs = find_devices("service-processor");
	if (!rtcs) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		printk(KERN_EMERG "Maple: Manual Restart Required\n");
		return;
	}
	maple_nvram_offset = *(unsigned int*) get_property(rtcs,
			"restart-addr", NULL);
	maple_nvram_command = *(unsigned int*) get_property(rtcs,
			"restart-value", NULL);

	/* send command */
	outb_p(maple_nvram_command, maple_nvram_base + maple_nvram_offset);
	for (;;) ;
}

static void maple_power_off(void)
{
	unsigned int maple_nvram_base;
	unsigned int maple_nvram_offset;
	unsigned int maple_nvram_command;
	struct device_node *rtcs;

	/* find NVRAM device */
	rtcs = find_compatible_devices("nvram", "AMD8111");
	if (rtcs && rtcs->addrs) {
		maple_nvram_base = rtcs->addrs[0].address;
	} else {
		printk(KERN_EMERG "Maple: Unable to find NVRAM\n");
		printk(KERN_EMERG "Maple: Manual Power-Down Required\n");
		return;
	}

	/* find service processor device */
	rtcs = find_devices("service-processor");
	if (!rtcs) {
		printk(KERN_EMERG "Maple: Unable to find Service Processor\n");
		printk(KERN_EMERG "Maple: Manual Power-Down Required\n");
		return;
	}
	maple_nvram_offset = *(unsigned int*) get_property(rtcs,
			"power-off-addr", NULL);
	maple_nvram_command = *(unsigned int*) get_property(rtcs,
			"power-off-value", NULL);

	/* send command */
	outb_p(maple_nvram_command, maple_nvram_base + maple_nvram_offset);
	for (;;) ;
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
	unsigned int default_speed;
	u64 physport;

	DBG(" -> maple_init_early\n");

	/* Initialize hash table, from now on, we can take hash faults
	 * and call ioremap
	 */
	hpte_init_native();

	/* Find the serial port */
	generic_find_legacy_serial_ports(&physport, &default_speed);

	DBG("phys port addr: %lx\n", (long)physport);

	if (physport) {
		void *comport;
		/* Map the uart for udbg. */
		comport = (void *)ioremap(physport, 16);
		udbg_init_uart(comport, default_speed);

		DBG("Hello World !\n");
	}

	/* Setup interrupt mapping options */
	ppc64_interrupt_controller = IC_OPEN_PIC;

	iommu_init_early_u3();

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
static int __init maple_probe(int platform)
{
	if (platform != PLATFORM_MAPLE)
		return 0;
	/*
	 * On U3, the DART (iommu) must be allocated now since it
	 * has an impact on htab_initialize (due to the large page it
	 * occupies having to be broken up so the DART itself is not
	 * part of the cacheable linar mapping
	 */
	alloc_u3_dart_table();

	return 1;
}

struct machdep_calls __initdata maple_md = {
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
	.idle_loop		= native_idle,
};
