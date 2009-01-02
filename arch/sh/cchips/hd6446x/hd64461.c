/*
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Hitachi HD64461 companion chip support
 */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/hd64461.h>

/* This belongs in cpu specific */
#define INTC_ICR1 0xA4140010UL

static void hd64461_mask_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = __raw_readw(HD64461_NIMR);
	nimr |= mask;
	__raw_writew(nimr, HD64461_NIMR);
}

static void hd64461_unmask_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = __raw_readw(HD64461_NIMR);
	nimr &= ~mask;
	__raw_writew(nimr, HD64461_NIMR);
}

static void hd64461_mask_and_ack_irq(unsigned int irq)
{
	hd64461_mask_irq(irq);
#ifdef CONFIG_HD64461_ENABLER
	if (irq == HD64461_IRQBASE + 13)
		__raw_writeb(0x00, HD64461_PCC1CSCR);
#endif
}

static struct irq_chip hd64461_irq_chip = {
	.name		= "HD64461-IRQ",
	.mask		= hd64461_mask_irq,
	.mask_ack	= hd64461_mask_and_ack_irq,
	.unmask		= hd64461_unmask_irq,
};

int hd64461_irq_demux(int irq)
{
	if (irq == CONFIG_HD64461_IRQ) {
		unsigned short bit;
		unsigned short nirr = inw(HD64461_NIRR);
		unsigned short nimr = inw(HD64461_NIMR);
		int i;

		nirr &= ~nimr;
		for (bit = 1, i = 0; i < 16; bit <<= 1, i++)
			if (nirr & bit)
				break;
		irq = HD64461_IRQBASE + i;
	}
	return irq;
}

int __init setup_hd64461(void)
{
	int i;

	if (!MACH_HD64461)
		return 0;

	printk(KERN_INFO
	       "HD64461 configured at 0x%x on irq %d(mapped into %d to %d)\n",
	       CONFIG_HD64461_IOBASE, CONFIG_HD64461_IRQ, HD64461_IRQBASE,
	       HD64461_IRQBASE + 15);

/* Should be at processor specific part.. */
#if defined(CONFIG_CPU_SUBTYPE_SH7709)
	__raw_writew(0x2240, INTC_ICR1);
#endif
	__raw_writew(0xffff, HD64461_NIMR);

	/*  IRQ 80 -> 95 belongs to HD64461  */
	for (i = HD64461_IRQBASE; i < HD64461_IRQBASE + 16; i++)
		set_irq_chip_and_handler(i, &hd64461_irq_chip,
					 handle_level_irq);

#ifdef CONFIG_HD64461_ENABLER
	printk(KERN_INFO "HD64461: enabling PCMCIA devices\n");
	__raw_writeb(0x4c, HD64461_PCC1CSCIER);
	__raw_writeb(0x00, HD64461_PCC1CSCR);
#endif

	return 0;
}

module_init(setup_hd64461);
