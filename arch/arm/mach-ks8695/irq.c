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
#include <linux/device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>

static void ks8695_irq_mask(struct irq_data *d)
{
	unsigned long inten;

	inten = __raw_readl(KS8695_IRQ_VA + KS8695_INTEN);
	inten &= ~(1 << d->irq);

	__raw_writel(inten, KS8695_IRQ_VA + KS8695_INTEN);
}

static void ks8695_irq_unmask(struct irq_data *d)
{
	unsigned long inten;

	inten = __raw_readl(KS8695_IRQ_VA + KS8695_INTEN);
	inten |= (1 << d->irq);

	__raw_writel(inten, KS8695_IRQ_VA + KS8695_INTEN);
}

static void ks8695_irq_ack(struct irq_data *d)
{
	__raw_writel((1 << d->irq), KS8695_IRQ_VA + KS8695_INTST);
}


static struct irq_chip ks8695_irq_level_chip;
static struct irq_chip ks8695_irq_edge_chip;


static int ks8695_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned long ctrl, mode;
	unsigned short level_triggered = 0;

	ctrl = __raw_readl(KS8695_GPIO_VA + KS8695_IOPC);

	switch (type) {
		case IRQ_TYPE_LEVEL_HIGH:
			mode = IOPC_TM_HIGH;
			level_triggered = 1;
			break;
		case IRQ_TYPE_LEVEL_LOW:
			mode = IOPC_TM_LOW;
			level_triggered = 1;
			break;
		case IRQ_TYPE_EDGE_RISING:
			mode = IOPC_TM_RISING;
			break;
		case IRQ_TYPE_EDGE_FALLING:
			mode = IOPC_TM_FALLING;
			break;
		case IRQ_TYPE_EDGE_BOTH:
			mode = IOPC_TM_EDGE;
			break;
		default:
			return -EINVAL;
	}

	switch (d->irq) {
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
		irq_set_chip_and_handler(d->irq, &ks8695_irq_level_chip,
					 handle_level_irq);
	}
	else {
		irq_set_chip_and_handler(d->irq, &ks8695_irq_edge_chip,
					 handle_edge_irq);
	}

	__raw_writel(ctrl, KS8695_GPIO_VA + KS8695_IOPC);
	return 0;
}

static struct irq_chip ks8695_irq_level_chip = {
	.irq_ack	= ks8695_irq_mask,
	.irq_mask	= ks8695_irq_mask,
	.irq_unmask	= ks8695_irq_unmask,
	.irq_set_type	= ks8695_irq_set_type,
};

static struct irq_chip ks8695_irq_edge_chip = {
	.irq_ack	= ks8695_irq_ack,
	.irq_mask	= ks8695_irq_mask,
	.irq_unmask	= ks8695_irq_unmask,
	.irq_set_type	= ks8695_irq_set_type,
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
				irq_set_chip_and_handler(irq,
							 &ks8695_irq_level_chip,
							 handle_level_irq);
				break;

			/* Edge-triggered interrupts */
			default:
				/* clear pending bit */
				ks8695_irq_ack(irq_get_irq_data(irq));
				irq_set_chip_and_handler(irq,
							 &ks8695_irq_edge_chip,
							 handle_edge_irq);
		}

		irq_clear_status_flags(irq, IRQ_NOREQUEST);
	}
}
