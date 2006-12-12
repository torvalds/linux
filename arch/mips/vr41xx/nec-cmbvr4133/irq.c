/*
 * arch/mips/vr41xx/nec-cmbvr4133/irq.c
 *
 * Interrupt routines for the NEC CMB-VR4133 board.
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for NEC-CMBVR4133 in 2.6
 * Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/vr41xx/cmbvr4133.h>

extern void enable_8259A_irq(unsigned int irq);
extern void disable_8259A_irq(unsigned int irq);
extern void mask_and_ack_8259A(unsigned int irq);
extern void init_8259A(int hoge);

extern int vr4133_rockhopper;

static void enable_i8259_irq(unsigned int irq)
{
	enable_8259A_irq(irq - I8259_IRQ_BASE);
}

static void disable_i8259_irq(unsigned int irq)
{
	disable_8259A_irq(irq - I8259_IRQ_BASE);
}

static void ack_i8259_irq(unsigned int irq)
{
	mask_and_ack_8259A(irq - I8259_IRQ_BASE);
}

static struct irq_chip i8259_irq_type = {
	.typename       = "XT-PIC",
	.ack            = ack_i8259_irq,
	.mask		= disable_i8259_irq,
	.mask_ack	= ack_i8259_irq,
	.unmask		= enable_i8259_irq,
};

static int i8259_get_irq_number(int irq)
{
	unsigned long isr;

	isr = inb(0x20);
	irq = ffz(~isr);
	if (irq == 2) {
		isr = inb(0xa0);
		irq = 8 + ffz(~isr);
	}

	if (irq < 0 || irq > 15)
		return -EINVAL;

	return I8259_IRQ_BASE + irq;
}

static struct irqaction i8259_slave_cascade = {
	.handler        = &no_action,
	.name           = "cascade",
};

void __init rockhopper_init_irq(void)
{
	int i;

	if(!vr4133_rockhopper) {
		printk(KERN_ERR "Not a Rockhopper Board \n");
		return;
	}

	for (i = I8259_IRQ_BASE; i <= I8259_IRQ_LAST; i++)
		set_irq_chip_and_handler(i, &i8259_irq_type, handle_level_irq);

	setup_irq(I8259_SLAVE_IRQ, &i8259_slave_cascade);

	vr41xx_set_irq_trigger(CMBVR41XX_INTC_PIN, TRIGGER_LEVEL, SIGNAL_THROUGH);
	vr41xx_set_irq_level(CMBVR41XX_INTC_PIN, LEVEL_HIGH);
	vr41xx_cascade_irq(CMBVR41XX_INTC_IRQ, i8259_get_irq_number);
}
