/*
 * arch/ppc/platform/85xx/sbc85xx.c
 * 
 * WindRiver PowerQUICC III SBC85xx board common routines
 *
 * Copyright 2002, 2003 Motorola Inc.
 * Copyright 2004 Red Hat, Inc.
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

#include <platforms/85xx/sbc85xx.h>

unsigned char __res[sizeof (bd_t)];

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
unsigned long pci_dram_offset = 0;
#endif

extern unsigned long total_memory;	/* in mm/init */

/* Internal interrupts are all Level Sensitive, and Positive Polarity */
static u_char sbc8560_openpic_initsenses[] __initdata = {
	MPC85XX_INTERNAL_IRQ_SENSES,
	0x0,				/* External  0: */
	0x0,				/* External  1: */
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 2: PCI slot 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 3: PCI slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 4: PCI slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 5: PCI slot 3 */
#else
	0x0,				/* External  2: */
	0x0,				/* External  3: */
	0x0,				/* External  4: */
	0x0,				/* External  5: */
#endif
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 6: PHY */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 7: PHY */
	0x0,				/* External  8: */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* External 9: PHY */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* External 10: PHY */
	0x0,				/* External 11: */
};

/* ************************************************************************ */
int
sbc8560_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Wind River\n");
	seq_printf(m, "Machine\t\t: SBC%s\n", cur_ppc_sys_spec->ppc_sys_name);
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
sbc8560_init_IRQ(void)
{
	bd_t *binfo = (bd_t *) __res;
	/* Determine the Physical Address of the OpenPIC regs */
	phys_addr_t OpenPIC_PAddr =
	    binfo->bi_immr_base + MPC85xx_OPENPIC_OFFSET;
	OpenPIC_Addr = ioremap(OpenPIC_PAddr, MPC85xx_OPENPIC_SIZE);
	OpenPIC_InitSenses = sbc8560_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof (sbc8560_openpic_initsenses);

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

/*
 * interrupt routing
 */

#ifdef CONFIG_PCI
int mpc85xx_map_irq(struct pci_dev *dev, unsigned char idsel,
		    unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      PCI IDSEL/INTPIN->INTLINE
	     *        A      B      C      D
	     */
	{
		{PIRQA, PIRQB, PIRQC, PIRQD},
		{PIRQD, PIRQA, PIRQB, PIRQC},
		{PIRQC, PIRQD, PIRQA, PIRQB},
		{PIRQB, PIRQC, PIRQD, PIRQA},
	};

	const long min_idsel = 12, max_idsel = 15, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

int mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}
#endif /* CONFIG_PCI */
