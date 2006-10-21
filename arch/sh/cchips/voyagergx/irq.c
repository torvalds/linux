/* -------------------------------------------------------------------- */
/* setup_voyagergx.c:                                                     */
/* -------------------------------------------------------------------- */
/*  This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Copyright 2003 (c) Lineo uSolutions,Inc.
*/
/* -------------------------------------------------------------------- */

#undef DEBUG

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
#include <asm/voyagergx.h>

static void disable_voyagergx_irq(unsigned int irq)
{
	unsigned long val;
	unsigned long mask = 1 << (irq - VOYAGER_IRQ_BASE);

    	pr_debug("disable_voyagergx_irq(%d): mask=%x\n", irq, mask);
        val = inl(VOYAGER_INT_MASK);
        val &= ~mask;
        outl(val, VOYAGER_INT_MASK);
}

static void enable_voyagergx_irq(unsigned int irq)
{
        unsigned long val;
        unsigned long mask = 1 << (irq - VOYAGER_IRQ_BASE);

        pr_debug("disable_voyagergx_irq(%d): mask=%x\n", irq, mask);
        val = inl(VOYAGER_INT_MASK);
        val |= mask;
        outl(val, VOYAGER_INT_MASK);
}

static void mask_and_ack_voyagergx(unsigned int irq)
{
	disable_voyagergx_irq(irq);
}

static void end_voyagergx_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_voyagergx_irq(irq);
}

static unsigned int startup_voyagergx_irq(unsigned int irq)
{
	enable_voyagergx_irq(irq);
	return 0;
}

static void shutdown_voyagergx_irq(unsigned int irq)
{
	disable_voyagergx_irq(irq);
}

static struct hw_interrupt_type voyagergx_irq_type = {
	.typename = "VOYAGERGX-IRQ",
	.startup = startup_voyagergx_irq,
	.shutdown = shutdown_voyagergx_irq,
	.enable = enable_voyagergx_irq,
	.disable = disable_voyagergx_irq,
	.ack = mask_and_ack_voyagergx,
	.end = end_voyagergx_irq,
};

static irqreturn_t voyagergx_interrupt(int irq, void *dev_id)
{
	printk(KERN_INFO
	       "VoyagerGX: spurious interrupt, status: 0x%x\n",
	       		inl(INT_STATUS));
	return IRQ_HANDLED;
}

static struct {
	int (*func)(int, void *);
	void *dev;
} voyagergx_demux[VOYAGER_IRQ_NUM];

void voyagergx_register_irq_demux(int irq,
		int (*demux)(int irq, void *dev), void *dev)
{
    	voyagergx_demux[irq - VOYAGER_IRQ_BASE].func = demux;
    	voyagergx_demux[irq - VOYAGER_IRQ_BASE].dev = dev;
}

void voyagergx_unregister_irq_demux(int irq)
{
    	voyagergx_demux[irq - VOYAGER_IRQ_BASE].func = 0;
}

int voyagergx_irq_demux(int irq)
{

	if (irq == IRQ_VOYAGER ) {
		unsigned long i = 0, bit __attribute__ ((unused));
		unsigned long val  = inl(INT_STATUS);
#if 1
		if ( val & ( 1 << 1 )){
			i = 1;
		} else if ( val & ( 1 << 2 )){
			i = 2;
		} else if ( val & ( 1 << 6 )){
			i = 6;
		} else if( val & ( 1 << 10 )){
			i = 10;
		} else if( val & ( 1 << 11 )){
			i = 11;
		} else if( val & ( 1 << 12 )){
			i = 12;
		} else if( val & ( 1 << 17 )){
			i = 17;
		} else {
			printk("Unexpected IRQ irq = %d status = 0x%08lx\n", irq, val);
		}
		pr_debug("voyagergx_irq_demux %d \n", i);
#else
		for (bit = 1, i = 0 ; i < VOYAGER_IRQ_NUM ; bit <<= 1, i++)
			if (val & bit)
				break;
#endif
    	    	if (i < VOYAGER_IRQ_NUM) {
			irq = VOYAGER_IRQ_BASE + i;
    	    		if (voyagergx_demux[i].func != 0)
				irq = voyagergx_demux[i].func(irq, voyagergx_demux[i].dev);
		}
	}
	return irq;
}

static struct irqaction irq0  = {
	.name		= "voyagergx",
	.handler	= voyagergx_interrupt,
	.flags		= IRQF_DISABLED,
	.mask		= CPU_MASK_NONE,
};

void __init setup_voyagergx_irq(void)
{
	int i, flag;

	printk(KERN_INFO "VoyagerGX configured at 0x%x on irq %d(mapped into %d to %d)\n",
	       VOYAGER_BASE,
	       IRQ_VOYAGER,
	       VOYAGER_IRQ_BASE,
	       VOYAGER_IRQ_BASE + VOYAGER_IRQ_NUM - 1);

	for (i=0; i<VOYAGER_IRQ_NUM; i++) {
		flag = 0;
		switch (VOYAGER_IRQ_BASE + i) {
		case VOYAGER_USBH_IRQ:
		case VOYAGER_8051_IRQ:
		case VOYAGER_UART0_IRQ:
		case VOYAGER_UART1_IRQ:
		case VOYAGER_AC97_IRQ:
			flag = 1;
		}
		if (flag == 1)
			irq_desc[VOYAGER_IRQ_BASE + i].chip = &voyagergx_irq_type;
	}

	setup_irq(IRQ_VOYAGER, &irq0);
}

