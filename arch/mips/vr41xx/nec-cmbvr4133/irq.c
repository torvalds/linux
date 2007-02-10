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
#include <asm/i8259.h>
#include <asm/vr41xx/cmbvr4133.h>

extern int vr4133_rockhopper;

static int i8259_get_irq_number(int irq)
{
	return i8259_irq();
}

void __init rockhopper_init_irq(void)
{
	int i;

	if(!vr4133_rockhopper) {
		printk(KERN_ERR "Not a Rockhopper Board \n");
		return;
	}

	vr41xx_set_irq_trigger(CMBVR41XX_INTC_PIN, TRIGGER_LEVEL, SIGNAL_THROUGH);
	vr41xx_set_irq_level(CMBVR41XX_INTC_PIN, LEVEL_HIGH);
	vr41xx_cascade_irq(CMBVR41XX_INTC_IRQ, i8259_get_irq_number);
}
