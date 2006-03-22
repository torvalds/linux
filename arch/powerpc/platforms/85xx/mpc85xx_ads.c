/*
 * MPC85xx setup and early boot code plus other random bits.
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/prom.h>
#include <asm/mpic.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include "mpc85xx.h"

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

/*
 * Internal interrupts are all Level Sensitive, and Positive Polarity
 *
 * Note:  Likely, this table and the following function should be
 *        obtained and derived from the OF Device Tree.
 */
static u_char mpc85xx_ads_openpic_initsenses[] __initdata = {
	MPC85XX_INTERNAL_IRQ_SENSES,
	0x0,			/* External  0: */
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 1: PCI slot 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 2: PCI slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 3: PCI slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 4: PCI slot 3 */
#else
	0x0,			/* External  1: */
	0x0,			/* External  2: */
	0x0,			/* External  3: */
	0x0,			/* External  4: */
#endif
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 5: PHY */
	0x0,			/* External  6: */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 7: PHY */
	0x0,			/* External  8: */
	0x0,			/* External  9: */
	0x0,			/* External 10: */
	0x0,			/* External 11: */
};

#ifdef CONFIG_PCI
/*
 * interrupt routing
 */

int
mpc85xx_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     * This is little evil, but works around the fact
	     * that revA boards have IDSEL starting at 18
	     * and others boards (older) start at 12
	     *
	     *      PCI IDSEL/INTPIN->INTLINE
	     *       A      B      C      D
	     */
	{
		{PIRQA, PIRQB, PIRQC, PIRQD},	/* IDSEL 2 */
		{PIRQD, PIRQA, PIRQB, PIRQC},
		{PIRQC, PIRQD, PIRQA, PIRQB},
		{PIRQB, PIRQC, PIRQD, PIRQA},	/* IDSEL 5 */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{PIRQA, PIRQB, PIRQC, PIRQD},	/* IDSEL 12 */
		{PIRQD, PIRQA, PIRQB, PIRQC},
		{PIRQC, PIRQD, PIRQA, PIRQB},
		{PIRQB, PIRQC, PIRQD, PIRQA},	/* IDSEL 15 */
		{0, 0, 0, 0},	/* -- */
		{0, 0, 0, 0},	/* -- */
		{PIRQA, PIRQB, PIRQC, PIRQD},	/* IDSEL 18 */
		{PIRQD, PIRQA, PIRQB, PIRQC},
		{PIRQC, PIRQD, PIRQA, PIRQB},
		{PIRQB, PIRQC, PIRQD, PIRQA},	/* IDSEL 21 */
	};

	const long min_idsel = 2, max_idsel = 21, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

int
mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

#endif /* CONFIG_PCI */


void __init mpc85xx_ads_pic_init(void)
{
	struct mpic *mpic1;
	phys_addr_t OpenPIC_PAddr;

	/* Determine the Physical Address of the OpenPIC regs */
	OpenPIC_PAddr = get_immrbase() + MPC85xx_OPENPIC_OFFSET;

	mpic1 = mpic_alloc(OpenPIC_PAddr,
			   MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			   4, MPC85xx_OPENPIC_IRQ_OFFSET, 0, 250,
			   mpc85xx_ads_openpic_initsenses,
			   sizeof(mpc85xx_ads_openpic_initsenses),
			   " OpenPIC  ");
	BUG_ON(mpic1 == NULL);
	mpic_assign_isu(mpic1, 0, OpenPIC_PAddr + 0x10200);
	mpic_assign_isu(mpic1, 1, OpenPIC_PAddr + 0x10280);
	mpic_assign_isu(mpic1, 2, OpenPIC_PAddr + 0x10300);
	mpic_assign_isu(mpic1, 3, OpenPIC_PAddr + 0x10380);
	mpic_assign_isu(mpic1, 4, OpenPIC_PAddr + 0x10400);
	mpic_assign_isu(mpic1, 5, OpenPIC_PAddr + 0x10480);
	mpic_assign_isu(mpic1, 6, OpenPIC_PAddr + 0x10500);
	mpic_assign_isu(mpic1, 7, OpenPIC_PAddr + 0x10580);

	/* dummy mappings to get to 48 */
	mpic_assign_isu(mpic1, 8, OpenPIC_PAddr + 0x10600);
	mpic_assign_isu(mpic1, 9, OpenPIC_PAddr + 0x10680);
	mpic_assign_isu(mpic1, 10, OpenPIC_PAddr + 0x10700);
	mpic_assign_isu(mpic1, 11, OpenPIC_PAddr + 0x10780);

	/* External ints */
	mpic_assign_isu(mpic1, 12, OpenPIC_PAddr + 0x10000);
	mpic_assign_isu(mpic1, 13, OpenPIC_PAddr + 0x10080);
	mpic_assign_isu(mpic1, 14, OpenPIC_PAddr + 0x10100);
	mpic_init(mpic1);
}

/*
 * Setup the architecture
 */
static void __init mpc85xx_ads_setup_arch(void)
{
	struct device_node *cpu;
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ads_setup_arch()", 0);

	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu != 0) {
		unsigned int *fp;

		fp = (int *)get_property(cpu, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(cpu);
	}

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mpc85xx_map_irq;
	ppc_md.pci_exclude_device = mpc85xx_exclude_device;
#endif

#ifdef  CONFIG_ROOT_NFS
	ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}

void mpc85xx_ads_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: mpc85xx\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}

void __init platform_init(void)
{
	ppc_md.setup_arch = mpc85xx_ads_setup_arch;
	ppc_md.show_cpuinfo = mpc85xx_ads_show_cpuinfo;

	ppc_md.init_IRQ = mpc85xx_ads_pic_init;
	ppc_md.get_irq = mpic_get_irq;

	ppc_md.restart = mpc85xx_restart;
	ppc_md.power_off = NULL;
	ppc_md.halt = NULL;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = generic_calibrate_decr;

	ppc_md.progress = udbg_progress;

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ads platform_init(): exit", 0);
}
