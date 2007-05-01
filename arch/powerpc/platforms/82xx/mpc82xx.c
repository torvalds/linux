/*
 * MPC82xx setup and early boot code plus other random bits.
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/fsl_devices.h>
#include <linux/fs_uart_pd.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc8260.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/cpm2.h>
#include <asm/udbg.h>
#include <asm/i8259.h>
#include <linux/fs_enet_pd.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/cpm2_pic.h>

#include "pq2ads.h"

static int __init get_freq(char *name, unsigned long *val)
{
	struct device_node *cpu;
	const unsigned int *fp;
	int found = 0;

	/* The cpu node should have timebase and clock frequency properties */
	cpu = of_find_node_by_type(NULL, "cpu");

	if (cpu) {
		fp = of_get_property(cpu, name, NULL);
		if (fp) {
			found = 1;
			*val = *fp;
		}

		of_node_put(cpu);
	}

	return found;
}

void __init m82xx_calibrate_decr(void)
{
	ppc_tb_freq = 125000000;
	if (!get_freq("bus-frequency", &ppc_tb_freq)) {
		printk(KERN_ERR "WARNING: Estimating decrementer frequency "
				"(not found)\n");
	}
	ppc_tb_freq /= 4;
	ppc_proc_freq = 1000000000;
	if (!get_freq("clock-frequency", &ppc_proc_freq))
		printk(KERN_ERR "WARNING: Estimating processor frequency"
				"(not found)\n");
}

void mpc82xx_ads_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: %s\n", CPUINFO_MACHINE);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}
