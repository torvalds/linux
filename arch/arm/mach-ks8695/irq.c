/*
 * arch/arm/mach-ks8695/irq.c
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
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

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>

#include <asm/arch/regs-irq.h>
#include <asm/arch/regs-gpio.h>

static void ks8695_irq_mask(unsigned int irqno)
{
	unsigned long inten;

	inten = __raw_readl(KS8695_IRQ_VA + KS8695_INTEN);
	inten &= ~(1 << irqno);

	__raw_writel(inten, KS8695_IRQ_VA + KS8695_INTEN);
}

static void ks8695_irq_unmask(unsigned int irqno)
{
	unsigned long inten;

	inten = __raw_readl(KS8695_IRQ_VA + KS8695_INTEN);
	inten |= (1 << irqno);

	__raw_writel(inten, KS8695_IRQ_VA + KS8695_INTEN);
}

static void ks8695_irq_ack(unsigned int irqno)
{
	__raw_writel((1 << irqno), KS8695_IRQ_VA + KS8695_INTST);
}


static struct irq_chip ks8695_irq_level_chip;
static struct irq_chip ks8695_irq_edge_chip;


static int ks8695_irq_set_type(unsigned int irqno, unsigned int type)
{
	unsigned long ctrl, mode;
	unsigned short level_triggered = 0;

	ctrl = __raw_readl(KS8695_GPIO_VA + KS8695_IOPC);

	switch (type) {
		case IRQT_HIGH:
			mode = IOPC_TM_HIGH;
			level_triggered = 1;
			break;
		case IRQT_LOW:
			mode = IOPC_TM_LOW;
			level_triggered = 1;
			break;
		case IRQT_RISING:
			mode = IOPC_TM_RISING;
			break;
		case IRQT_FALLING:
			mode = IOPC_TM_FALLING;
			break;
		case IRQT_BOTHEDGE:
			mode = IOPC_TM_EDGE;
			break;
		default:
			return -EINVAL;
	}

	switch (irqno) {
		case KS8695_IRQ_EXTERN0:
			ctrl &= ~IOPC_IOEINT0TM;
			ctrl |= IOPC_IOEINT0_MODE(mode);
			break;
		case KS8695_IRQ_EXTERN1:
			ctrl &= ~IOPC_IOEINT1TM;
			ctrl |= IOPC_IOEINT1_MODE(mode);
			break;
		case KS8695_IRQ_EXTERN2:
			ctrl &= ~IOPC_IOEINT2TM;
			ctrl |= IOPC_IOEINT2_MODE(mode);
			break;
		case KS8695_IRQ_EXTERN3:
			ctrl &= ~IOPC_IOEINT3TM;
			ctrl |= IOPC_IOEINT3_MODE(mode);
			break;
		default:
			return -EINVAL;
	}

	if (level_triggered) {
		set_irq_chip(irqno, &ks8695_irq_level_chip);
		set_irq_handler(irqno, handle_level_irq);
	}
	else {
		set_irq_chip(irqno, &ks8695_irq_edge_chip);
		set_irq_handler(irqno, handle_edge_irq);
	}

	__raw_writel(ctrl, KS8695_GPIO_VA + KS8695_IOPC);
	return 0;
}

static struct irq_chip ks8695_irq_level_chip = {
	.ack		= ks8695_irq_mask,
	.mask		= ks8695_irq_mask,
	.unmask		= ks8695_irq_unmask,
	.set_type	= ks8695_irq_set_type,
};

static struct irq_chip ks8695_irq_edge_chip = {
	.ack		= ks8695_irq_ack,
	.mask		= ks8695_irq_mask,
	.unmask		= ks8695_irq_unmask,
	.set_type	= ks8695_irq_set_type,
};

void __init ks8695_init_irq(void)
{
	unsigned int irq;

	/* Disable all interrupts initially */
	__raw_writel(0, KS8695_IRQ_VA + KS8695_INTMC);
	__raw_writel(0, KS8695_IRQ_VA + KS8695_INTEN);

	for (irq = 0; irq < NR_IRQS; irq++) {
		switch (irq) {
			/* Level-triggered interrupts */
			case KS8695_IRQ_BUS_ERROR:
			case KS8695_IRQ_UART_MODEM_STATUS:
			case KS8695_IRQ_UART_LINE_STATUS:
			case KS8695_IRQ_UART_RX:
			case KS8695_IRQ_COMM_TX:
			case KS8695_IRQ_COMM_RX:
				set_irq_chip(irq, &ks8695_irq_level_chip);
				set_irq_handler(irq, handle_level_irq);
				break;

			/* Edge-triggered interrupts */
			default:
				ks8695_irq_ack(irq);	/* clear pending bit */
				set_irq_chip(irq, &ks8695_irq_edge_chip);
				set_irq_handler(irq, handle_edge_irq);
		}

		set_irq_flags(irq, IRQF_VALID);
	}
}
