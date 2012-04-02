/*
 * arch/arm/mach-lpc32xx/irq.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
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
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

/*
 * Default value representing the Activation polarity of all internal
 * interrupt sources
 */
#define MIC_APR_DEFAULT		0x3FF0EFE0
#define SIC1_APR_DEFAULT	0xFBD27186
#define SIC2_APR_DEFAULT	0x801810C0

/*
 * Default value representing the Activation Type of all internal
 * interrupt sources. All are level sensitive.
 */
#define MIC_ATR_DEFAULT		0x00000000
#define SIC1_ATR_DEFAULT	0x00026000
#define SIC2_ATR_DEFAULT	0x00000000

struct lpc32xx_event_group_regs {
	void __iomem *enab_reg;
	void __iomem *edge_reg;
	void __iomem *maskstat_reg;
	void __iomem *rawstat_reg;
};

static const struct lpc32xx_event_group_regs lpc32xx_event_int_regs = {
	.enab_reg = LPC32XX_CLKPWR_INT_ER,
	.edge_reg = LPC32XX_CLKPWR_INT_AP,
	.maskstat_reg = LPC32XX_CLKPWR_INT_SR,
	.rawstat_reg = LPC32XX_CLKPWR_INT_RS,
};

static const struct lpc32xx_event_group_regs lpc32xx_event_pin_regs = {
	.enab_reg = LPC32XX_CLKPWR_PIN_ER,
	.edge_reg = LPC32XX_CLKPWR_PIN_AP,
	.maskstat_reg = LPC32XX_CLKPWR_PIN_SR,
	.rawstat_reg = LPC32XX_CLKPWR_PIN_RS,
};

struct lpc32xx_event_info {
	const struct lpc32xx_event_group_regs *event_group;
	u32 mask;
};

/*
 * Maps an IRQ number to and event mask and register
 */
static const struct lpc32xx_event_info lpc32xx_events[NR_IRQS] = {
	[IRQ_LPC32XX_GPI_08] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_08_BIT,
	},
	[IRQ_LPC32XX_GPI_09] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_09_BIT,
	},
	[IRQ_LPC32XX_GPI_19] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_19_BIT,
	},
	[IRQ_LPC32XX_GPI_07] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_07_BIT,
	},
	[IRQ_LPC32XX_GPI_00] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_00_BIT,
	},
	[IRQ_LPC32XX_GPI_01] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_01_BIT,
	},
	[IRQ_LPC32XX_GPI_02] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_02_BIT,
	},
	[IRQ_LPC32XX_GPI_03] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_03_BIT,
	},
	[IRQ_LPC32XX_GPI_04] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_04_BIT,
	},
	[IRQ_LPC32XX_GPI_05] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_05_BIT,
	},
	[IRQ_LPC32XX_GPI_06] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_06_BIT,
	},
	[IRQ_LPC32XX_GPI_28] = {
		.event_group = &lpc32xx_event_pin_regs,
		.mask = LPC32XX_CLKPWR_EXTSRC_GPI_28_BIT,
	},
	[IRQ_LPC32XX_GPIO_00] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_00_BIT,
	},
	[IRQ_LPC32XX_GPIO_01] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_01_BIT,
	},
	[IRQ_LPC32XX_GPIO_02] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_02_BIT,
	},
	[IRQ_LPC32XX_GPIO_03] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_03_BIT,
	},
	[IRQ_LPC32XX_GPIO_04] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_04_BIT,
	},
	[IRQ_LPC32XX_GPIO_05] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_GPIO_05_BIT,
	},
	[IRQ_LPC32XX_KEY] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_KEY_BIT,
	},
	[IRQ_LPC32XX_ETHERNET] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_MAC_BIT,
	},
	[IRQ_LPC32XX_USB_OTG_ATX] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_USBATXINT_BIT,
	},
	[IRQ_LPC32XX_USB_HOST] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_USB_BIT,
	},
	[IRQ_LPC32XX_RTC] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_RTC_BIT,
	},
	[IRQ_LPC32XX_MSTIMER] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_MSTIMER_BIT,
	},
	[IRQ_LPC32XX_TS_AUX] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_TS_AUX_BIT,
	},
	[IRQ_LPC32XX_TS_P] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_TS_P_BIT,
	},
	[IRQ_LPC32XX_TS_IRQ] = {
		.event_group = &lpc32xx_event_int_regs,
		.mask = LPC32XX_CLKPWR_INTSRC_ADC_BIT,
	},
};

static void get_controller(unsigned int irq, unsigned int *base,
	unsigned int *irqbit)
{
	if (irq < 32) {
		*base = LPC32XX_MIC_BASE;
		*irqbit = 1 << irq;
	} else if (irq < 64) {
		*base = LPC32XX_SIC1_BASE;
		*irqbit = 1 << (irq - 32);
	} else {
		*base = LPC32XX_SIC2_BASE;
		*irqbit = 1 << (irq - 64);
	}
}

static void lpc32xx_mask_irq(struct irq_data *d)
{
	unsigned int reg, ctrl, mask;

	get_controller(d->irq, &ctrl, &mask);

	reg = __raw_readl(LPC32XX_INTC_MASK(ctrl)) & ~mask;
	__raw_writel(reg, LPC32XX_INTC_MASK(ctrl));
}

static void lpc32xx_unmask_irq(struct irq_data *d)
{
	unsigned int reg, ctrl, mask;

	get_controller(d->irq, &ctrl, &mask);

	reg = __raw_readl(LPC32XX_INTC_MASK(ctrl)) | mask;
	__raw_writel(reg, LPC32XX_INTC_MASK(ctrl));
}

static void lpc32xx_ack_irq(struct irq_data *d)
{
	unsigned int ctrl, mask;

	get_controller(d->irq, &ctrl, &mask);

	__raw_writel(mask, LPC32XX_INTC_RAW_STAT(ctrl));

	/* Also need to clear pending wake event */
	if (lpc32xx_events[d->irq].mask != 0)
		__raw_writel(lpc32xx_events[d->irq].mask,
			lpc32xx_events[d->irq].event_group->rawstat_reg);
}

static void __lpc32xx_set_irq_type(unsigned int irq, int use_high_level,
	int use_edge)
{
	unsigned int reg, ctrl, mask;

	get_controller(irq, &ctrl, &mask);

	/* Activation level, high or low */
	reg = __raw_readl(LPC32XX_INTC_POLAR(ctrl));
	if (use_high_level)
		reg |= mask;
	else
		reg &= ~mask;
	__raw_writel(reg, LPC32XX_INTC_POLAR(ctrl));

	/* Activation type, edge or level */
	reg = __raw_readl(LPC32XX_INTC_ACT_TYPE(ctrl));
	if (use_edge)
		reg |= mask;
	else
		reg &= ~mask;
	__raw_writel(reg, LPC32XX_INTC_ACT_TYPE(ctrl));

	/* Use same polarity for the wake events */
	if (lpc32xx_events[irq].mask != 0) {
		reg = __raw_readl(lpc32xx_events[irq].event_group->edge_reg);

		if (use_high_level)
			reg |= lpc32xx_events[irq].mask;
		else
			reg &= ~lpc32xx_events[irq].mask;

		__raw_writel(reg, lpc32xx_events[irq].event_group->edge_reg);
	}
}

static int lpc32xx_set_irq_type(struct irq_data *d, unsigned int type)
{
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		/* Rising edge sensitive */
		__lpc32xx_set_irq_type(d->irq, 1, 1);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		/* Falling edge sensitive */
		__lpc32xx_set_irq_type(d->irq, 0, 1);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		/* Low level sensitive */
		__lpc32xx_set_irq_type(d->irq, 0, 0);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		/* High level sensitive */
		__lpc32xx_set_irq_type(d->irq, 1, 0);
		break;

	/* Other modes are not supported */
	default:
		return -EINVAL;
	}

	/* Ok to use the level handler for all types */
	irq_set_handler(d->irq, handle_level_irq);

	return 0;
}

static int lpc32xx_irq_wake(struct irq_data *d, unsigned int state)
{
	unsigned long eventreg;

	if (lpc32xx_events[d->irq].mask != 0) {
		eventreg = __raw_readl(lpc32xx_events[d->irq].
			event_group->enab_reg);

		if (state)
			eventreg |= lpc32xx_events[d->irq].mask;
		else {
			eventreg &= ~lpc32xx_events[d->irq].mask;

			/*
			 * When disabling the wakeup, clear the latched
			 * event
			 */
			__raw_writel(lpc32xx_events[d->irq].mask,
				lpc32xx_events[d->irq].
				event_group->rawstat_reg);
		}

		__raw_writel(eventreg,
			lpc32xx_events[d->irq].event_group->enab_reg);

		return 0;
	}

	/* Clear event */
	__raw_writel(lpc32xx_events[d->irq].mask,
		lpc32xx_events[d->irq].event_group->rawstat_reg);

	return -ENODEV;
}

static void __init lpc32xx_set_default_mappings(unsigned int apr,
	unsigned int atr, unsigned int offset)
{
	unsigned int i;

	/* Set activation levels for each interrupt */
	i = 0;
	while (i < 32) {
		__lpc32xx_set_irq_type(offset + i, ((apr >> i) & 0x1),
			((atr >> i) & 0x1));
		i++;
	}
}

static struct irq_chip lpc32xx_irq_chip = {
	.irq_ack = lpc32xx_ack_irq,
	.irq_mask = lpc32xx_mask_irq,
	.irq_unmask = lpc32xx_unmask_irq,
	.irq_set_type = lpc32xx_set_irq_type,
	.irq_set_wake = lpc32xx_irq_wake
};

static void lpc32xx_sic1_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long ints = __raw_readl(LPC32XX_INTC_STAT(LPC32XX_SIC1_BASE));

	while (ints != 0) {
		int irqno = fls(ints) - 1;

		ints &= ~(1 << irqno);

		generic_handle_irq(LPC32XX_SIC1_IRQ(irqno));
	}
}

static void lpc32xx_sic2_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long ints = __raw_readl(LPC32XX_INTC_STAT(LPC32XX_SIC2_BASE));

	while (ints != 0) {
		int irqno = fls(ints) - 1;

		ints &= ~(1 << irqno);

		generic_handle_irq(LPC32XX_SIC2_IRQ(irqno));
	}
}

void __init lpc32xx_init_irq(void)
{
	unsigned int i;

	/* Setup MIC */
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_MIC_BASE));
	__raw_writel(MIC_APR_DEFAULT, LPC32XX_INTC_POLAR(LPC32XX_MIC_BASE));
	__raw_writel(MIC_ATR_DEFAULT, LPC32XX_INTC_ACT_TYPE(LPC32XX_MIC_BASE));

	/* Setup SIC1 */
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_SIC1_BASE));
	__raw_writel(SIC1_APR_DEFAULT, LPC32XX_INTC_POLAR(LPC32XX_SIC1_BASE));
	__raw_writel(SIC1_ATR_DEFAULT,
				LPC32XX_INTC_ACT_TYPE(LPC32XX_SIC1_BASE));

	/* Setup SIC2 */
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_SIC2_BASE));
	__raw_writel(SIC2_APR_DEFAULT, LPC32XX_INTC_POLAR(LPC32XX_SIC2_BASE));
	__raw_writel(SIC2_ATR_DEFAULT,
				LPC32XX_INTC_ACT_TYPE(LPC32XX_SIC2_BASE));

	/* Configure supported IRQ's */
	for (i = 0; i < NR_IRQS; i++) {
		irq_set_chip_and_handler(i, &lpc32xx_irq_chip,
					 handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Set default mappings */
	lpc32xx_set_default_mappings(MIC_APR_DEFAULT, MIC_ATR_DEFAULT, 0);
	lpc32xx_set_default_mappings(SIC1_APR_DEFAULT, SIC1_ATR_DEFAULT, 32);
	lpc32xx_set_default_mappings(SIC2_APR_DEFAULT, SIC2_ATR_DEFAULT, 64);

	/* mask all interrupts except SUBIRQ */
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_MIC_BASE));
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_SIC1_BASE));
	__raw_writel(0, LPC32XX_INTC_MASK(LPC32XX_SIC2_BASE));

	/* MIC SUBIRQx interrupts will route handling to the chain handlers */
	irq_set_chained_handler(IRQ_LPC32XX_SUB1IRQ, lpc32xx_sic1_handler);
	irq_set_chained_handler(IRQ_LPC32XX_SUB2IRQ, lpc32xx_sic2_handler);

	/* Initially disable all wake events */
	__raw_writel(0, LPC32XX_CLKPWR_P01_ER);
	__raw_writel(0, LPC32XX_CLKPWR_INT_ER);
	__raw_writel(0, LPC32XX_CLKPWR_PIN_ER);

	/*
	 * Default wake activation polarities, all pin sources are low edge
	 * triggered
	 */
	__raw_writel(LPC32XX_CLKPWR_INTSRC_TS_P_BIT |
		LPC32XX_CLKPWR_INTSRC_MSTIMER_BIT |
		LPC32XX_CLKPWR_INTSRC_RTC_BIT,
		LPC32XX_CLKPWR_INT_AP);
	__raw_writel(0, LPC32XX_CLKPWR_PIN_AP);

	/* Clear latched wake event states */
	__raw_writel(__raw_readl(LPC32XX_CLKPWR_PIN_RS),
		LPC32XX_CLKPWR_PIN_RS);
	__raw_writel(__raw_readl(LPC32XX_CLKPWR_INT_RS),
		LPC32XX_CLKPWR_INT_RS);
}
