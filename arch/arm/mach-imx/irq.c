/*
 *  linux/arch/arm/mach-imx/irq.c
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
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
 *
 *  03/03/2004   Sascha Hauer <sascha@saschahauer.de>
 *               Copied from the motorola bsp package and added gpio demux
 *               interrupt handler
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/timer.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>

/*
 *
 * We simply use the ENABLE DISABLE registers inside of the IMX
 * to turn on/off specific interrupts.
 *
 */

#define INTCNTL_OFF               0x00
#define NIMASK_OFF                0x04
#define INTENNUM_OFF              0x08
#define INTDISNUM_OFF             0x0C
#define INTENABLEH_OFF            0x10
#define INTENABLEL_OFF            0x14
#define INTTYPEH_OFF              0x18
#define INTTYPEL_OFF              0x1C
#define NIPRIORITY_OFF(x)         (0x20+4*(7-(x)))
#define NIVECSR_OFF               0x40
#define FIVECSR_OFF               0x44
#define INTSRCH_OFF               0x48
#define INTSRCL_OFF               0x4C
#define INTFRCH_OFF               0x50
#define INTFRCL_OFF               0x54
#define NIPNDH_OFF                0x58
#define NIPNDL_OFF                0x5C
#define FIPNDH_OFF                0x60
#define FIPNDL_OFF                0x64

#define VA_AITC_BASE              IO_ADDRESS(IMX_AITC_BASE)
#define IMX_AITC_INTCNTL         (VA_AITC_BASE + INTCNTL_OFF)
#define IMX_AITC_NIMASK          (VA_AITC_BASE + NIMASK_OFF)
#define IMX_AITC_INTENNUM        (VA_AITC_BASE + INTENNUM_OFF)
#define IMX_AITC_INTDISNUM       (VA_AITC_BASE + INTDISNUM_OFF)
#define IMX_AITC_INTENABLEH      (VA_AITC_BASE + INTENABLEH_OFF)
#define IMX_AITC_INTENABLEL      (VA_AITC_BASE + INTENABLEL_OFF)
#define IMX_AITC_INTTYPEH        (VA_AITC_BASE + INTTYPEH_OFF)
#define IMX_AITC_INTTYPEL        (VA_AITC_BASE + INTTYPEL_OFF)
#define IMX_AITC_NIPRIORITY(x)   (VA_AITC_BASE + NIPRIORITY_OFF(x))
#define IMX_AITC_NIVECSR         (VA_AITC_BASE + NIVECSR_OFF)
#define IMX_AITC_FIVECSR         (VA_AITC_BASE + FIVECSR_OFF)
#define IMX_AITC_INTSRCH         (VA_AITC_BASE + INTSRCH_OFF)
#define IMX_AITC_INTSRCL         (VA_AITC_BASE + INTSRCL_OFF)
#define IMX_AITC_INTFRCH         (VA_AITC_BASE + INTFRCH_OFF)
#define IMX_AITC_INTFRCL         (VA_AITC_BASE + INTFRCL_OFF)
#define IMX_AITC_NIPNDH          (VA_AITC_BASE + NIPNDH_OFF)
#define IMX_AITC_NIPNDL          (VA_AITC_BASE + NIPNDL_OFF)
#define IMX_AITC_FIPNDH          (VA_AITC_BASE + FIPNDH_OFF)
#define IMX_AITC_FIPNDL          (VA_AITC_BASE + FIPNDL_OFF)

#if 0
#define DEBUG_IRQ(fmt...)	printk(fmt)
#else
#define DEBUG_IRQ(fmt...)	do { } while (0)
#endif

static void
imx_mask_irq(unsigned int irq)
{
	__raw_writel(irq, IMX_AITC_INTDISNUM);
}

static void
imx_unmask_irq(unsigned int irq)
{
	__raw_writel(irq, IMX_AITC_INTENNUM);
}

#ifdef CONFIG_FIQ
int imx_set_irq_fiq(unsigned int irq, unsigned int type)
{
	unsigned int irqt;

	if (irq >= IMX_IRQS)
		return -EINVAL;

	if (irq < IMX_IRQS / 2) {
		irqt = __raw_readl(IMX_AITC_INTTYPEL) & ~(1 << irq);
		__raw_writel(irqt | (!!type << irq), IMX_AITC_INTTYPEL);
	} else {
		irq -= IMX_IRQS / 2;
		irqt = __raw_readl(IMX_AITC_INTTYPEH) & ~(1 << irq);
		__raw_writel(irqt | (!!type << irq), IMX_AITC_INTTYPEH);
	}

	return 0;
}
EXPORT_SYMBOL(imx_set_irq_fiq);
#endif /* CONFIG_FIQ */

static int
imx_gpio_irq_type(unsigned int _irq, unsigned int type)
{
	unsigned int irq_type = 0, irq, reg, bit;

	irq = _irq - IRQ_GPIOA(0);
	reg = irq >> 5;
	bit = 1 << (irq % 32);

	if (type == IRQ_TYPE_PROBE) {
		/* Don't mess with enabled GPIOs using preconfigured edges or
		   GPIOs set to alternate function during probe */
		/* TODO: support probe */
//              if ((GPIO_IRQ_rising_edge[idx] | GPIO_IRQ_falling_edge[idx]) &
//                  GPIO_bit(gpio))
//                      return 0;
//              if (GAFR(gpio) & (0x3 << (((gpio) & 0xf)*2)))
//                      return 0;
//              type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	GIUS(reg) |= bit;
	DDIR(reg) &= ~(bit);

	DEBUG_IRQ("setting type of irq %d to ", _irq);

	if (type & IRQ_TYPE_EDGE_RISING) {
		DEBUG_IRQ("rising edges\n");
		irq_type = 0x0;
	}
	if (type & IRQ_TYPE_EDGE_FALLING) {
		DEBUG_IRQ("falling edges\n");
		irq_type = 0x1;
	}
	if (type & IRQ_TYPE_LEVEL_LOW) {
		DEBUG_IRQ("low level\n");
		irq_type = 0x3;
	}
	if (type & IRQ_TYPE_LEVEL_HIGH) {
		DEBUG_IRQ("high level\n");
		irq_type = 0x2;
	}

	if (irq % 32 < 16) {
		ICR1(reg) = (ICR1(reg) & ~(0x3 << ((irq % 16) * 2))) |
		    (irq_type << ((irq % 16) * 2));
	} else {
		ICR2(reg) = (ICR2(reg) & ~(0x3 << ((irq % 16) * 2))) |
		    (irq_type << ((irq % 16) * 2));
	}

	return 0;

}

static void
imx_gpio_ack_irq(unsigned int irq)
{
	DEBUG_IRQ("%s: irq %d\n", __func__, irq);
	ISR(IRQ_TO_REG(irq)) = 1 << ((irq - IRQ_GPIOA(0)) % 32);
}

static void
imx_gpio_mask_irq(unsigned int irq)
{
	DEBUG_IRQ("%s: irq %d\n", __func__, irq);
	IMR(IRQ_TO_REG(irq)) &= ~( 1 << ((irq - IRQ_GPIOA(0)) % 32));
}

static void
imx_gpio_unmask_irq(unsigned int irq)
{
	DEBUG_IRQ("%s: irq %d\n", __func__, irq);
	IMR(IRQ_TO_REG(irq)) |= 1 << ((irq - IRQ_GPIOA(0)) % 32);
}

static void
imx_gpio_handler(unsigned int mask, unsigned int irq,
                 struct irq_desc *desc)
{
	desc = irq_desc + irq;
	while (mask) {
		if (mask & 1) {
			DEBUG_IRQ("handling irq %d\n", irq);
			desc_handle_irq(irq, desc);
		}
		irq++;
		desc++;
		mask >>= 1;
	}
}

static void
imx_gpioa_demux_handler(unsigned int irq_unused, struct irq_desc *desc)
{
	unsigned int mask, irq;

	mask = ISR(0);
	irq = IRQ_GPIOA(0);
	imx_gpio_handler(mask, irq, desc);
}

static void
imx_gpiob_demux_handler(unsigned int irq_unused, struct irq_desc *desc)
{
	unsigned int mask, irq;

	mask = ISR(1);
	irq = IRQ_GPIOB(0);
	imx_gpio_handler(mask, irq, desc);
}

static void
imx_gpioc_demux_handler(unsigned int irq_unused, struct irq_desc *desc)
{
	unsigned int mask, irq;

	mask = ISR(2);
	irq = IRQ_GPIOC(0);
	imx_gpio_handler(mask, irq, desc);
}

static void
imx_gpiod_demux_handler(unsigned int irq_unused, struct irq_desc *desc)
{
	unsigned int mask, irq;

	mask = ISR(3);
	irq = IRQ_GPIOD(0);
	imx_gpio_handler(mask, irq, desc);
}

static struct irq_chip imx_internal_chip = {
	.name = "MPU",
	.ack = imx_mask_irq,
	.mask = imx_mask_irq,
	.unmask = imx_unmask_irq,
};

static struct irq_chip imx_gpio_chip = {
	.name = "GPIO",
	.ack = imx_gpio_ack_irq,
	.mask = imx_gpio_mask_irq,
	.unmask = imx_gpio_unmask_irq,
	.set_type = imx_gpio_irq_type,
};

void __init
imx_init_irq(void)
{
	unsigned int irq;

	DEBUG_IRQ("Initializing imx interrupts\n");

	/* Disable all interrupts initially. */
	/* Do not rely on the bootloader. */
	__raw_writel(0, IMX_AITC_INTENABLEH);
	__raw_writel(0, IMX_AITC_INTENABLEL);

	/* Mask all GPIO interrupts as well */
	IMR(0) = 0;
	IMR(1) = 0;
	IMR(2) = 0;
	IMR(3) = 0;

	for (irq = 0; irq < IMX_IRQS; irq++) {
		set_irq_chip(irq, &imx_internal_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	for (irq = IRQ_GPIOA(0); irq < IRQ_GPIOD(32); irq++) {
		set_irq_chip(irq, &imx_gpio_chip);
		set_irq_handler(irq, handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	set_irq_chained_handler(GPIO_INT_PORTA, imx_gpioa_demux_handler);
	set_irq_chained_handler(GPIO_INT_PORTB, imx_gpiob_demux_handler);
	set_irq_chained_handler(GPIO_INT_PORTC, imx_gpioc_demux_handler);
	set_irq_chained_handler(GPIO_INT_PORTD, imx_gpiod_demux_handler);

	/* Release masking of interrupts according to priority */
	__raw_writel(-1, IMX_AITC_NIMASK);

#ifdef CONFIG_FIQ
	/* Initialize FIQ */
	init_FIQ();
#endif
}
