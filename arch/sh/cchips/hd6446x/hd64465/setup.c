/*
 * $Id: setup.c,v 1.4 2003/08/03 03:05:10 lethal Exp $
 *
 * Setup and IRQ handling code for the HD64465 companion chip.
 * by Greg Banks <gbanks@pocketpenguins.com>
 * Copyright (c) 2000 PocketPenguins Inc
 *
 * Derived from setup_hd64461.c which bore the message:
 * Copyright (C) 2000 YAEGASHI Takeshi
 */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/hd64465/hd64465.h>

static void disable_hd64465_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64465_IRQ_BASE);

    	pr_debug("disable_hd64465_irq(%d): mask=%x\n", irq, mask);
	nimr = inw(HD64465_REG_NIMR);
	nimr |= mask;
	outw(nimr, HD64465_REG_NIMR);
}


static void enable_hd64465_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64465_IRQ_BASE);

    	pr_debug("enable_hd64465_irq(%d): mask=%x\n", irq, mask);
	nimr = inw(HD64465_REG_NIMR);
	nimr &= ~mask;
	outw(nimr, HD64465_REG_NIMR);
}


static void mask_and_ack_hd64465(unsigned int irq)
{
	disable_hd64465_irq(irq);
}


static void end_hd64465_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_hd64465_irq(irq);
}


static unsigned int startup_hd64465_irq(unsigned int irq)
{ 
	enable_hd64465_irq(irq);
	return 0;
}


static void shutdown_hd64465_irq(unsigned int irq)
{
	disable_hd64465_irq(irq);
}


static struct hw_interrupt_type hd64465_irq_type = {
	.typename	= "HD64465-IRQ",
	.startup	= startup_hd64465_irq,
	.shutdown	= shutdown_hd64465_irq,
	.enable		= enable_hd64465_irq,
	.disable	= disable_hd64465_irq,
	.ack		= mask_and_ack_hd64465,
	.end		= end_hd64465_irq,
};


static irqreturn_t hd64465_interrupt(int irq, void *dev_id)
{
	printk(KERN_INFO
	       "HD64465: spurious interrupt, nirr: 0x%x nimr: 0x%x\n",
	       inw(HD64465_REG_NIRR), inw(HD64465_REG_NIMR));

	return IRQ_NONE;
}


/*====================================================*/

/*
 * Support for a secondary IRQ demux step.  This is necessary
 * because the HD64465 presents a very thin interface to the
 * PCMCIA bus; a lot of features (such as remapping interrupts)
 * normally done in hardware by other PCMCIA host bridges is
 * instead done in software.
 */
static struct
{
    int (*func)(int, void *);
    void *dev;
} hd64465_demux[HD64465_IRQ_NUM];

void hd64465_register_irq_demux(int irq,
		int (*demux)(int irq, void *dev), void *dev)
{
    	hd64465_demux[irq - HD64465_IRQ_BASE].func = demux;
    	hd64465_demux[irq - HD64465_IRQ_BASE].dev = dev;
}
EXPORT_SYMBOL(hd64465_register_irq_demux);

void hd64465_unregister_irq_demux(int irq)
{
    	hd64465_demux[irq - HD64465_IRQ_BASE].func = 0;
}
EXPORT_SYMBOL(hd64465_unregister_irq_demux);



int hd64465_irq_demux(int irq)
{
	if (irq == CONFIG_HD64465_IRQ) {
		unsigned short i, bit;
		unsigned short nirr = inw(HD64465_REG_NIRR);
		unsigned short nimr = inw(HD64465_REG_NIMR);

    	    	pr_debug("hd64465_irq_demux, nirr=%04x, nimr=%04x\n", nirr, nimr);
		nirr &= ~nimr;
		for (bit = 1, i = 0 ; i < HD64465_IRQ_NUM ; bit <<= 1, i++)
		    if (nirr & bit)
		    	break;

    	    	if (i < HD64465_IRQ_NUM) {
		    irq = HD64465_IRQ_BASE + i;
    	    	    if (hd64465_demux[i].func != 0)
		    	irq = hd64465_demux[i].func(irq, hd64465_demux[i].dev);
		}
	}
	return irq;
}

static struct irqaction irq0  = {
	.handler = hd64465_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "HD64465",
};


static int __init setup_hd64465(void)
{
	int i;
	unsigned short rev;
	unsigned short smscr;

	if (!MACH_HD64465)
		return 0;

	printk(KERN_INFO "HD64465 configured at 0x%x on irq %d(mapped into %d to %d)\n",
	       CONFIG_HD64465_IOBASE,
	       CONFIG_HD64465_IRQ,
	       HD64465_IRQ_BASE,
	       HD64465_IRQ_BASE+HD64465_IRQ_NUM-1);

	if (inw(HD64465_REG_SDID) != HD64465_SDID) {
		printk(KERN_ERR "HD64465 device ID not found, check base address\n");
	}

	rev = inw(HD64465_REG_SRR);
	printk(KERN_INFO "HD64465 hardware revision %d.%d\n", (rev >> 8) & 0xff, rev & 0xff);
	       
	outw(0xffff, HD64465_REG_NIMR); 	/* mask all interrupts */

	for (i = 0; i < HD64465_IRQ_NUM ; i++) {
		irq_desc[HD64465_IRQ_BASE + i].chip = &hd64465_irq_type;
	}

	setup_irq(CONFIG_HD64465_IRQ, &irq0);

#ifdef CONFIG_SERIAL
	/* wake up the UART from STANDBY at this point */
	smscr = inw(HD64465_REG_SMSCR);
	outw(smscr & (~HD64465_SMSCR_UARTST), HD64465_REG_SMSCR);

	/* remap IO ports for first ISA serial port to HD64465 UART */
	hd64465_port_map(0x3f8, 8, CONFIG_HD64465_IOBASE + 0x8000, 1);
#endif

	return 0;
}

module_init(setup_hd64465);
