/*
 * SBC82XX platform support
 *
 * Author: Guy Streeter <streeter@redhat.com>
 *
 * Derived from: est8260_setup.c by Allen Curtis, ONZ
 *
 * Copyright 2004 Red Hat, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/mpc8260.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/todc.h>
#include <asm/immap_cpm2.h>
#include <asm/pci.h>

static void (*callback_init_IRQ)(void);

extern unsigned char __res[sizeof(bd_t)];

#ifdef CONFIG_GEN_RTC
TODC_ALLOC();

/*
 * Timer init happens before mem_init but after paging init, so we cannot
 * directly use ioremap() at that time.
 * late_time_init() is call after paging init.
 */

static void sbc82xx_time_init(void)
{
	volatile memctl_cpm2_t *mc = &cpm2_immr->im_memctl;

	/* Set up CS11 for RTC chip */
	mc->memc_br11=0;
	mc->memc_or11=0xffff0836;
	mc->memc_br11=SBC82xx_TODC_NVRAM_ADDR | 0x0801;

	TODC_INIT(TODC_TYPE_MK48T59, 0, 0, SBC82xx_TODC_NVRAM_ADDR, 0);

	todc_info->nvram_data =
		(unsigned int)ioremap(todc_info->nvram_data, 0x2000);
	BUG_ON(!todc_info->nvram_data);
	ppc_md.get_rtc_time	= todc_get_rtc_time;
	ppc_md.set_rtc_time	= todc_set_rtc_time;
	ppc_md.nvram_read_val	= todc_direct_read_val;
	ppc_md.nvram_write_val	= todc_direct_write_val;
	todc_time_init();
}
#endif /* CONFIG_GEN_RTC */

static volatile char *sbc82xx_i8259_map;
static char sbc82xx_i8259_mask = 0xff;
static DEFINE_SPINLOCK(sbc82xx_i8259_lock);

static void sbc82xx_i8259_mask_and_ack_irq(unsigned int irq_nr)
{
	unsigned long flags;

	irq_nr -= NR_SIU_INTS;

	spin_lock_irqsave(&sbc82xx_i8259_lock, flags);
	sbc82xx_i8259_mask |= 1 << irq_nr;
	(void) sbc82xx_i8259_map[1];	/* Dummy read */
	sbc82xx_i8259_map[1] = sbc82xx_i8259_mask;
	sbc82xx_i8259_map[0] = 0x20;	/* OCW2: Non-specific EOI */
	spin_unlock_irqrestore(&sbc82xx_i8259_lock, flags);
}

static void sbc82xx_i8259_mask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	irq_nr -= NR_SIU_INTS;

	spin_lock_irqsave(&sbc82xx_i8259_lock, flags);
	sbc82xx_i8259_mask |= 1 << irq_nr;
	sbc82xx_i8259_map[1] = sbc82xx_i8259_mask;
	spin_unlock_irqrestore(&sbc82xx_i8259_lock, flags);
}

static void sbc82xx_i8259_unmask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	irq_nr -= NR_SIU_INTS;

	spin_lock_irqsave(&sbc82xx_i8259_lock, flags);
	sbc82xx_i8259_mask &= ~(1 << irq_nr);
	sbc82xx_i8259_map[1] = sbc82xx_i8259_mask;
	spin_unlock_irqrestore(&sbc82xx_i8259_lock, flags);
}

static void sbc82xx_i8259_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS))
	    && irq_desc[irq].action)
		sbc82xx_i8259_unmask_irq(irq);
}


struct hw_interrupt_type sbc82xx_i8259_ic = {
	.typename = " i8259     ",
	.enable = sbc82xx_i8259_unmask_irq,
	.disable = sbc82xx_i8259_mask_irq,
	.ack = sbc82xx_i8259_mask_and_ack_irq,
	.end = sbc82xx_i8259_end_irq,
};

static irqreturn_t sbc82xx_i8259_demux(int dummy, void *dev_id)
{
	int irq;

	spin_lock(&sbc82xx_i8259_lock);

	sbc82xx_i8259_map[0] = 0x0c;	/* OCW3: Read IR register on RD# pulse */
	irq = sbc82xx_i8259_map[0] & 7;	/* Read IRR */

	if (irq == 7) {
		/* Possible spurious interrupt */
		int isr;
		sbc82xx_i8259_map[0] = 0x0b;	/* OCW3: Read IS register on RD# pulse */
		isr = sbc82xx_i8259_map[0];	/* Read ISR */

		if (!(isr & 0x80)) {
			printk(KERN_INFO "Spurious i8259 interrupt\n");
			return IRQ_HANDLED;
		}
	}
	__do_IRQ(NR_SIU_INTS + irq);
	return IRQ_HANDLED;
}

static struct irqaction sbc82xx_i8259_irqaction = {
	.handler = sbc82xx_i8259_demux,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "i8259 demux",
};

void __init sbc82xx_init_IRQ(void)
{
	volatile memctl_cpm2_t *mc = &cpm2_immr->im_memctl;
	volatile intctl_cpm2_t *ic = &cpm2_immr->im_intctl;
	int i;

	callback_init_IRQ();

	/* u-boot doesn't always set the board up correctly */
	mc->memc_br5 = 0;
	mc->memc_or5 = 0xfff00856;
	mc->memc_br5 = 0x22000801;

	sbc82xx_i8259_map = ioremap(0x22008000, 2);
	if (!sbc82xx_i8259_map) {
		printk(KERN_CRIT "Mapping i8259 interrupt controller failed\n");
		return;
	}
	
	/* Set up the interrupt handlers for the i8259 IRQs */
	for (i = NR_SIU_INTS; i < NR_SIU_INTS + 8; i++) {
                irq_desc[i].chip = &sbc82xx_i8259_ic;
		irq_desc[i].status |= IRQ_LEVEL;
	}

	/* make IRQ6 level sensitive */
	ic->ic_siexr &= ~(1 << (14 - (SIU_INT_IRQ6 - SIU_INT_IRQ1)));
	irq_desc[SIU_INT_IRQ6].status |= IRQ_LEVEL;

	/* Initialise the i8259 */
	sbc82xx_i8259_map[0] = 0x1b;	/* ICW1: Level, no cascade, ICW4 */
	sbc82xx_i8259_map[1] = 0x00;	/* ICW2: vector base */
					/* No ICW3 (no cascade) */
	sbc82xx_i8259_map[1] = 0x01;	/* ICW4: 8086 mode, normal EOI */

	sbc82xx_i8259_map[0] = 0x0b;	/* OCW3: Read IS register on RD# pulse */

	sbc82xx_i8259_map[1] = sbc82xx_i8259_mask; /* Set interrupt mask */

	/* Request cascade IRQ */
	if (setup_irq(SIU_INT_IRQ6, &sbc82xx_i8259_irqaction)) {
		printk("Installation of i8259 IRQ demultiplexer failed.\n");
	}
}

static int sbc82xx_pci_map_irq(struct pci_dev *dev, unsigned char idsel,
			       unsigned char pin)
{
	static char pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 *  A      B      C      D
		 */
		{ SBC82xx_PIRQA, SBC82xx_PIRQB, SBC82xx_PIRQC, SBC82xx_PIRQD },	/* IDSEL 16 - PMC slot */
		{ SBC82xx_PC_IRQA, SBC82xx_PC_IRQB, -1,  -1  },			/* IDSEL 17 - CardBus */
		{ SBC82xx_PIRQA, SBC82xx_PIRQB, SBC82xx_PIRQC, SBC82xx_PIRQD }, /* IDSEL 18 - PCI-X bridge */
	};

	const long min_idsel = 16, max_idsel = 18, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}

static void __devinit quirk_sbc8260_cardbus(struct pci_dev *pdev)
{
	uint32_t ctrl;

	if (pdev->bus->number != 0 || pdev->devfn != PCI_DEVFN(17, 0))
		return;

	printk(KERN_INFO "Setting up CardBus controller\n");

	/* Set P2CCLK bit in System Control Register */
	pci_read_config_dword(pdev, 0x80, &ctrl);
	ctrl |= (1<<27);
	pci_write_config_dword(pdev, 0x80, ctrl);

	/* Set MFUNC up for PCI IRQ routing via INTA and INTB, and LEDs. */
	pci_write_config_dword(pdev, 0x8c, 0x00c01d22);

}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1420, quirk_sbc8260_cardbus);

void __init
m82xx_board_init(void)
{
	/* u-boot may be using one of the FCC Ethernet devices.
	   Use the MAC address to the SCC. */
	__res[offsetof(bd_t, bi_enetaddr[5])] &= ~3;

	/* Anything special for this platform */
	callback_init_IRQ	= ppc_md.init_IRQ;

	ppc_md.init_IRQ		= sbc82xx_init_IRQ;
	ppc_md.pci_map_irq	= sbc82xx_pci_map_irq;
#ifdef CONFIG_GEN_RTC
	ppc_md.time_init        = NULL;
	ppc_md.get_rtc_time     = NULL;
	ppc_md.set_rtc_time     = NULL;
	ppc_md.nvram_read_val   = NULL;
	ppc_md.nvram_write_val  = NULL;
	late_time_init		= sbc82xx_time_init;
#endif /* CONFIG_GEN_RTC */
}
