/* linux/arch/arm/plat-s3c24xx/irq.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics 
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/sysdev.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <plat/regs-irqtype.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/irq.h>

static void
s3c_irq_mask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= IRQ_EINT0;

	mask = __raw_readl(S3C2410_INTMSK);
	mask |= 1UL << irqno;
	__raw_writel(mask, S3C2410_INTMSK);
}

static inline void
s3c_irq_ack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);

	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}

static inline void
s3c_irq_maskack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask|bitval, S3C2410_INTMSK);

	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}


static void
s3c_irq_unmask(unsigned int irqno)
{
	unsigned long mask;

	if (irqno != IRQ_TIMER4 && irqno != IRQ_EINT8t23)
		irqdbf2("s3c_irq_unmask %d\n", irqno);

	irqno -= IRQ_EINT0;

	mask = __raw_readl(S3C2410_INTMSK);
	mask &= ~(1UL << irqno);
	__raw_writel(mask, S3C2410_INTMSK);
}

struct irq_chip s3c_irq_level_chip = {
	.name		= "s3c-level",
	.ack		= s3c_irq_maskack,
	.mask		= s3c_irq_mask,
	.unmask		= s3c_irq_unmask,
	.set_wake	= s3c_irq_wake
};

struct irq_chip s3c_irq_chip = {
	.name		= "s3c",
	.ack		= s3c_irq_ack,
	.mask		= s3c_irq_mask,
	.unmask		= s3c_irq_unmask,
	.set_wake	= s3c_irq_wake
};

static void
s3c_irqext_mask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= EXTINT_OFF;

	mask = __raw_readl(S3C24XX_EINTMASK);
	mask |= ( 1UL << irqno);
	__raw_writel(mask, S3C24XX_EINTMASK);
}

static void
s3c_irqext_ack(unsigned int irqno)
{
	unsigned long req;
	unsigned long bit;
	unsigned long mask;

	bit = 1UL << (irqno - EXTINT_OFF);

	mask = __raw_readl(S3C24XX_EINTMASK);

	__raw_writel(bit, S3C24XX_EINTPEND);

	req = __raw_readl(S3C24XX_EINTPEND);
	req &= ~mask;

	/* not sure if we should be acking the parent irq... */

	if (irqno <= IRQ_EINT7 ) {
		if ((req & 0xf0) == 0)
			s3c_irq_ack(IRQ_EINT4t7);
	} else {
		if ((req >> 8) == 0)
			s3c_irq_ack(IRQ_EINT8t23);
	}
}

static void
s3c_irqext_unmask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= EXTINT_OFF;

	mask = __raw_readl(S3C24XX_EINTMASK);
	mask &= ~( 1UL << irqno);
	__raw_writel(mask, S3C24XX_EINTMASK);
}

int
s3c_irqext_type(unsigned int irq, unsigned int type)
{
	void __iomem *extint_reg;
	void __iomem *gpcon_reg;
	unsigned long gpcon_offset, extint_offset;
	unsigned long newvalue = 0, value;

	if ((irq >= IRQ_EINT0) && (irq <= IRQ_EINT3))
	{
		gpcon_reg = S3C2410_GPFCON;
		extint_reg = S3C24XX_EXTINT0;
		gpcon_offset = (irq - IRQ_EINT0) * 2;
		extint_offset = (irq - IRQ_EINT0) * 4;
	}
	else if ((irq >= IRQ_EINT4) && (irq <= IRQ_EINT7))
	{
		gpcon_reg = S3C2410_GPFCON;
		extint_reg = S3C24XX_EXTINT0;
		gpcon_offset = (irq - (EXTINT_OFF)) * 2;
		extint_offset = (irq - (EXTINT_OFF)) * 4;
	}
	else if ((irq >= IRQ_EINT8) && (irq <= IRQ_EINT15))
	{
		gpcon_reg = S3C2410_GPGCON;
		extint_reg = S3C24XX_EXTINT1;
		gpcon_offset = (irq - IRQ_EINT8) * 2;
		extint_offset = (irq - IRQ_EINT8) * 4;
	}
	else if ((irq >= IRQ_EINT16) && (irq <= IRQ_EINT23))
	{
		gpcon_reg = S3C2410_GPGCON;
		extint_reg = S3C24XX_EXTINT2;
		gpcon_offset = (irq - IRQ_EINT8) * 2;
		extint_offset = (irq - IRQ_EINT16) * 4;
	} else
		return -1;

	/* Set the GPIO to external interrupt mode */
	value = __raw_readl(gpcon_reg);
	value = (value & ~(3 << gpcon_offset)) | (0x02 << gpcon_offset);
	__raw_writel(value, gpcon_reg);

	/* Set the external interrupt to pointed trigger type */
	switch (type)
	{
		case IRQ_TYPE_NONE:
			printk(KERN_WARNING "No edge setting!\n");
			break;

		case IRQ_TYPE_EDGE_RISING:
			newvalue = S3C2410_EXTINT_RISEEDGE;
			break;

		case IRQ_TYPE_EDGE_FALLING:
			newvalue = S3C2410_EXTINT_FALLEDGE;
			break;

		case IRQ_TYPE_EDGE_BOTH:
			newvalue = S3C2410_EXTINT_BOTHEDGE;
			break;

		case IRQ_TYPE_LEVEL_LOW:
			newvalue = S3C2410_EXTINT_LOWLEV;
			break;

		case IRQ_TYPE_LEVEL_HIGH:
			newvalue = S3C2410_EXTINT_HILEV;
			break;

		default:
			printk(KERN_ERR "No such irq type %d", type);
			return -1;
	}

	value = __raw_readl(extint_reg);
	value = (value & ~(7 << extint_offset)) | (newvalue << extint_offset);
	__raw_writel(value, extint_reg);

	return 0;
}

static struct irq_chip s3c_irqext_chip = {
	.name		= "s3c-ext",
	.mask		= s3c_irqext_mask,
	.unmask		= s3c_irqext_unmask,
	.ack		= s3c_irqext_ack,
	.set_type	= s3c_irqext_type,
	.set_wake	= s3c_irqext_wake
};

static struct irq_chip s3c_irq_eint0t4 = {
	.name		= "s3c-ext0",
	.ack		= s3c_irq_ack,
	.mask		= s3c_irq_mask,
	.unmask		= s3c_irq_unmask,
	.set_wake	= s3c_irq_wake,
	.set_type	= s3c_irqext_type,
};

/* mask values for the parent registers for each of the interrupt types */

#define INTMSK_UART0	 (1UL << (IRQ_UART0 - IRQ_EINT0))
#define INTMSK_UART1	 (1UL << (IRQ_UART1 - IRQ_EINT0))
#define INTMSK_UART2	 (1UL << (IRQ_UART2 - IRQ_EINT0))
#define INTMSK_ADCPARENT (1UL << (IRQ_ADCPARENT - IRQ_EINT0))


/* UART0 */

static void
s3c_irq_uart0_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART0, 7);
}

static void
s3c_irq_uart0_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART0);
}

static void
s3c_irq_uart0_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART0, 7);
}

static struct irq_chip s3c_irq_uart0 = {
	.name		= "s3c-uart0",
	.mask		= s3c_irq_uart0_mask,
	.unmask		= s3c_irq_uart0_unmask,
	.ack		= s3c_irq_uart0_ack,
};

/* UART1 */

static void
s3c_irq_uart1_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART1, 7 << 3);
}

static void
s3c_irq_uart1_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART1);
}

static void
s3c_irq_uart1_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART1, 7 << 3);
}

static struct irq_chip s3c_irq_uart1 = {
	.name		= "s3c-uart1",
	.mask		= s3c_irq_uart1_mask,
	.unmask		= s3c_irq_uart1_unmask,
	.ack		= s3c_irq_uart1_ack,
};

/* UART2 */

static void
s3c_irq_uart2_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART2, 7 << 6);
}

static void
s3c_irq_uart2_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART2);
}

static void
s3c_irq_uart2_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART2, 7 << 6);
}

static struct irq_chip s3c_irq_uart2 = {
	.name		= "s3c-uart2",
	.mask		= s3c_irq_uart2_mask,
	.unmask		= s3c_irq_uart2_unmask,
	.ack		= s3c_irq_uart2_ack,
};

/* ADC and Touchscreen */

static void
s3c_irq_adc_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_ADCPARENT, 3 << 9);
}

static void
s3c_irq_adc_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_ADCPARENT);
}

static void
s3c_irq_adc_ack(unsigned int irqno)
{
	s3c_irqsub_ack(irqno, INTMSK_ADCPARENT, 3 << 9);
}

static struct irq_chip s3c_irq_adc = {
	.name		= "s3c-adc",
	.mask		= s3c_irq_adc_mask,
	.unmask		= s3c_irq_adc_unmask,
	.ack		= s3c_irq_adc_ack,
};

/* irq demux for adc */
static void s3c_irq_demux_adc(unsigned int irq,
			      struct irq_desc *desc)
{
	unsigned int subsrc, submsk;
	unsigned int offset = 9;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc &= ~submsk;
	subsrc >>= offset;
	subsrc &= 3;

	if (subsrc != 0) {
		if (subsrc & 1) {
			generic_handle_irq(IRQ_TC);
		}
		if (subsrc & 2) {
			generic_handle_irq(IRQ_ADC);
		}
	}
}

static void s3c_irq_demux_uart(unsigned int start)
{
	unsigned int subsrc, submsk;
	unsigned int offset = start - IRQ_S3CUART_RX0;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	irqdbf2("s3c_irq_demux_uart: start=%d (%d), subsrc=0x%08x,0x%08x\n",
		start, offset, subsrc, submsk);

	subsrc &= ~submsk;
	subsrc >>= offset;
	subsrc &= 7;

	if (subsrc != 0) {
		if (subsrc & 1)
			generic_handle_irq(start);

		if (subsrc & 2)
			generic_handle_irq(start+1);

		if (subsrc & 4)
			generic_handle_irq(start+2);
	}
}

/* uart demux entry points */

static void
s3c_irq_demux_uart0(unsigned int irq,
		    struct irq_desc *desc)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX0);
}

static void
s3c_irq_demux_uart1(unsigned int irq,
		    struct irq_desc *desc)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX1);
}

static void
s3c_irq_demux_uart2(unsigned int irq,
		    struct irq_desc *desc)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX2);
}

static void
s3c_irq_demux_extint8(unsigned int irq,
		      struct irq_desc *desc)
{
	unsigned long eintpnd = __raw_readl(S3C24XX_EINTPEND);
	unsigned long eintmsk = __raw_readl(S3C24XX_EINTMASK);

	eintpnd &= ~eintmsk;
	eintpnd &= ~0xff;	/* ignore lower irqs */

	/* we may as well handle all the pending IRQs here */

	while (eintpnd) {
		irq = __ffs(eintpnd);
		eintpnd &= ~(1<<irq);

		irq += (IRQ_EINT4 - 4);
		generic_handle_irq(irq);
	}

}

static void
s3c_irq_demux_extint4t7(unsigned int irq,
			struct irq_desc *desc)
{
	unsigned long eintpnd = __raw_readl(S3C24XX_EINTPEND);
	unsigned long eintmsk = __raw_readl(S3C24XX_EINTMASK);

	eintpnd &= ~eintmsk;
	eintpnd &= 0xff;	/* only lower irqs */

	/* we may as well handle all the pending IRQs here */

	while (eintpnd) {
		irq = __ffs(eintpnd);
		eintpnd &= ~(1<<irq);

		irq += (IRQ_EINT4 - 4);

		generic_handle_irq(irq);
	}
}

#ifdef CONFIG_FIQ
/**
 * s3c24xx_set_fiq - set the FIQ routing
 * @irq: IRQ number to route to FIQ on processor.
 * @on: Whether to route @irq to the FIQ, or to remove the FIQ routing.
 *
 * Change the state of the IRQ to FIQ routing depending on @irq and @on. If
 * @on is true, the @irq is checked to see if it can be routed and the
 * interrupt controller updated to route the IRQ. If @on is false, the FIQ
 * routing is cleared, regardless of which @irq is specified.
 */
int s3c24xx_set_fiq(unsigned int irq, bool on)
{
	u32 intmod;
	unsigned offs;

	if (on) {
		offs = irq - FIQ_START;
		if (offs > 31)
			return -EINVAL;

		intmod = 1 << offs;
	} else {
		intmod = 0;
	}

	__raw_writel(intmod, S3C2410_INTMOD);
	return 0;
}
#endif


/* s3c24xx_init_irq
 *
 * Initialise S3C2410 IRQ system
*/

void __init s3c24xx_init_irq(void)
{
	unsigned long pend;
	unsigned long last;
	int irqno;
	int i;

#ifdef CONFIG_FIQ
	init_FIQ();
#endif

	irqdbf("s3c2410_init_irq: clearing interrupt status flags\n");

	/* first, clear all interrupts pending... */

	last = 0;
	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C24XX_EINTPEND);

		if (pend == 0 || pend == last)
			break;

		__raw_writel(pend, S3C24XX_EINTPEND);
		printk("irq: clearing pending ext status %08x\n", (int)pend);
		last = pend;
	}

	last = 0;
	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2410_INTPND);

		if (pend == 0 || pend == last)
			break;

		__raw_writel(pend, S3C2410_SRCPND);
		__raw_writel(pend, S3C2410_INTPND);
		printk("irq: clearing pending status %08x\n", (int)pend);
		last = pend;
	}

	last = 0;
	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2410_SUBSRCPND);

		if (pend == 0 || pend == last)
			break;

		printk("irq: clearing subpending status %08x\n", (int)pend);
		__raw_writel(pend, S3C2410_SUBSRCPND);
		last = pend;
	}

	/* register the main interrupts */

	irqdbf("s3c2410_init_irq: registering s3c2410 interrupt handlers\n");

	for (irqno = IRQ_EINT4t7; irqno <= IRQ_ADCPARENT; irqno++) {
		/* set all the s3c2410 internal irqs */

		switch (irqno) {
			/* deal with the special IRQs (cascaded) */

		case IRQ_EINT4t7:
		case IRQ_EINT8t23:
		case IRQ_UART0:
		case IRQ_UART1:
		case IRQ_UART2:
		case IRQ_ADCPARENT:
			set_irq_chip(irqno, &s3c_irq_level_chip);
			set_irq_handler(irqno, handle_level_irq);
			break;

		case IRQ_RESERVED6:
		case IRQ_RESERVED24:
			/* no IRQ here */
			break;

		default:
			//irqdbf("registering irq %d (s3c irq)\n", irqno);
			set_irq_chip(irqno, &s3c_irq_chip);
			set_irq_handler(irqno, handle_edge_irq);
			set_irq_flags(irqno, IRQF_VALID);
		}
	}

	/* setup the cascade irq handlers */

	set_irq_chained_handler(IRQ_EINT4t7, s3c_irq_demux_extint4t7);
	set_irq_chained_handler(IRQ_EINT8t23, s3c_irq_demux_extint8);

	set_irq_chained_handler(IRQ_UART0, s3c_irq_demux_uart0);
	set_irq_chained_handler(IRQ_UART1, s3c_irq_demux_uart1);
	set_irq_chained_handler(IRQ_UART2, s3c_irq_demux_uart2);
	set_irq_chained_handler(IRQ_ADCPARENT, s3c_irq_demux_adc);

	/* external interrupts */

	for (irqno = IRQ_EINT0; irqno <= IRQ_EINT3; irqno++) {
		irqdbf("registering irq %d (ext int)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_eint0t4);
		set_irq_handler(irqno, handle_edge_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_EINT4; irqno <= IRQ_EINT23; irqno++) {
		irqdbf("registering irq %d (extended s3c irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irqext_chip);
		set_irq_handler(irqno, handle_edge_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	/* register the uart interrupts */

	irqdbf("s3c2410: registering external interrupts\n");

	for (irqno = IRQ_S3CUART_RX0; irqno <= IRQ_S3CUART_ERR0; irqno++) {
		irqdbf("registering irq %d (s3c uart0 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart0);
		set_irq_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_S3CUART_RX1; irqno <= IRQ_S3CUART_ERR1; irqno++) {
		irqdbf("registering irq %d (s3c uart1 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart1);
		set_irq_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_S3CUART_RX2; irqno <= IRQ_S3CUART_ERR2; irqno++) {
		irqdbf("registering irq %d (s3c uart2 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart2);
		set_irq_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_TC; irqno <= IRQ_ADC; irqno++) {
		irqdbf("registering irq %d (s3c adc irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_adc);
		set_irq_handler(irqno, handle_edge_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	irqdbf("s3c2410: registered interrupt handlers\n");
}
