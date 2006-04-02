/*
 * arch/powerpc/platforms/83xx/mpc834x_sys.c
 *
 * MPC834x SYS board specific routines
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>

#include "mpc83xx.h"

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

#ifdef CONFIG_PCI
static int
mpc83xx_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      PCI IDSEL/INTPIN->INTLINE
	     *       A      B      C      D
	     */
	{
		{PIRQA, PIRQB, PIRQC, PIRQD},	/* idsel 0x11 */
		{PIRQC, PIRQD, PIRQA, PIRQB},	/* idsel 0x12 */
		{PIRQD, PIRQA, PIRQB, PIRQC},	/* idsel 0x13 */
		{0, 0, 0, 0},
		{PIRQA, PIRQB, PIRQC, PIRQD},	/* idsel 0x15 */
		{PIRQD, PIRQA, PIRQB, PIRQC},	/* idsel 0x16 */
		{PIRQC, PIRQD, PIRQA, PIRQB},	/* idsel 0x17 */
		{PIRQB, PIRQC, PIRQD, PIRQA},	/* idsel 0x18 */
		{0, 0, 0, 0},			/* idsel 0x19 */
		{0, 0, 0, 0},			/* idsel 0x20 */
	};

	const long min_idsel = 0x11, max_idsel = 0x20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}
#endif				/* CONFIG_PCI */

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc834x_sys_setup_arch(void)
{
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("mpc834x_sys_setup_arch()", 0);

	np = of_find_node_by_type(NULL, "cpu");
	if (np != 0) {
		unsigned int *fp =
		    (int *)get_property(np, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(np);
	}
#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mpc83xx_map_irq;
	ppc_md.pci_exclude_device = mpc83xx_exclude_device;
#endif

#ifdef  CONFIG_ROOT_NFS
	ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}

void __init mpc834x_sys_init_IRQ(void)
{
	u8 senses[8] = {
		0,			/* EXT 0 */
		IRQ_SENSE_LEVEL,	/* EXT 1 */
		IRQ_SENSE_LEVEL,	/* EXT 2 */
		0,			/* EXT 3 */
#ifdef CONFIG_PCI
		IRQ_SENSE_LEVEL,	/* EXT 4 */
		IRQ_SENSE_LEVEL,	/* EXT 5 */
		IRQ_SENSE_LEVEL,	/* EXT 6 */
		IRQ_SENSE_LEVEL,	/* EXT 7 */
#else
		0,			/* EXT 4 */
		0,			/* EXT 5 */
		0,			/* EXT 6 */
		0,			/* EXT 7 */
#endif
	};

	ipic_init(get_immrbase() + 0x00700, 0, 0, senses, 8);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
}

#if defined(CONFIG_I2C_MPC) && defined(CONFIG_SENSORS_DS1374)
extern ulong ds1374_get_rtc_time(void);
extern int ds1374_set_rtc_time(ulong);

static int __init mpc834x_rtc_hookup(void)
{
	struct timespec tv;

	ppc_md.get_rtc_time = ds1374_get_rtc_time;
	ppc_md.set_rtc_time = ds1374_set_rtc_time;

	tv.tv_nsec = 0;
	tv.tv_sec = (ppc_md.get_rtc_time) ();
	do_settimeofday(&tv);

	return 0;
}

late_initcall(mpc834x_rtc_hookup);
#endif

void __init platform_init(void)
{
	/* setup the PowerPC module struct */
	ppc_md.setup_arch = mpc834x_sys_setup_arch;

	ppc_md.init_IRQ = mpc834x_sys_init_IRQ;
	ppc_md.get_irq = ipic_get_irq;

	ppc_md.restart = mpc83xx_restart;

	ppc_md.time_init = mpc83xx_time_init;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = generic_calibrate_decr;

	ppc_md.progress = udbg_progress;

	if (ppc_md.progress)
		ppc_md.progress("mpc834x_sys_init(): exit", 0);

	return;
}
