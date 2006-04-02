/*
 * MPC85xx ADS board common routines
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
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
#include <linux/serial.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <asm/immap_85xx.h>
#include <asm/ppc_sys.h>

#include <mm/mmu_decl.h>

#include <syslib/ppc85xx_rio.h>

#include <platforms/85xx/mpc85xx_ads_common.h>

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

extern unsigned long total_memory;	/* in mm/init */

unsigned char __res[sizeof (bd_t)];

/* Internal interrupts are all Level Sensitive, and Positive Polarity */
static u_char mpc85xx_ads_openpic_initsenses[] __initdata = {
	MPC85XX_INTERNAL_IRQ_SENSES,
	0x0,						/* External  0: */
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 1: PCI slot 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 2: PCI slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 3: PCI slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 4: PCI slot 3 */
#else
	0x0,				/* External  1: */
	0x0,				/* External  2: */
	0x0,				/* External  3: */
	0x0,				/* External  4: */
#endif
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 5: PHY */
	0x0,				/* External  6: */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 7: PHY */
	0x0,				/* External  8: */
	0x0,				/* External  9: */
	0x0,				/* External 10: */
	0x0,				/* External 11: */
};

/* ************************************************************************ */
int
mpc85xx_ads_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: mpc%sads\n", cur_ppc_sys_spec->ppc_sys_name);
	seq_printf(m, "clock\t\t: %dMHz\n", freq / 1000000);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));

	return 0;
}

void __init
mpc85xx_ads_init_IRQ(void)
{
	bd_t *binfo = (bd_t *) __res;
	/* Determine the Physical Address of the OpenPIC regs */
	phys_addr_t OpenPIC_PAddr =
	    binfo->bi_immr_base + MPC85xx_OPENPIC_OFFSET;
	OpenPIC_Addr = ioremap(OpenPIC_PAddr, MPC85xx_OPENPIC_SIZE);
	OpenPIC_InitSenses = mpc85xx_ads_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof (mpc85xx_ads_openpic_initsenses);

	/* Skip reserved space and internal sources */
	openpic_set_sources(0, 32, OpenPIC_Addr + 0x10200);
	/* Map PIC IRQs 0-11 */
	openpic_set_sources(48, 12, OpenPIC_Addr + 0x10000);

	/* we let openpic interrupts starting from an offset, to
	 * leave space for cascading interrupts underneath.
	 */
	openpic_init(MPC85xx_OPENPIC_IRQ_OFFSET);

	return;
}

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

#ifdef CONFIG_RAPIDIO
void platform_rio_init(void)
{
	/* 512MB RIO LAW at 0xc0000000 */
	mpc85xx_rio_setup(0xc0000000, 0x20000000);
}
#endif /* CONFIG_RAPIDIO */
