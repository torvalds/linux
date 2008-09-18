/*
 * Interrupt handler for DaVinci boards.
 *
 * Copyright (C) 2006 Texas Instruments.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/cputype.h>
#include <asm/mach/irq.h>

#define IRQ_BIT(irq)		((irq) & 0x1f)

#define FIQ_REG0_OFFSET		0x0000
#define FIQ_REG1_OFFSET		0x0004
#define IRQ_REG0_OFFSET		0x0008
#define IRQ_REG1_OFFSET		0x000C
#define IRQ_ENT_REG0_OFFSET	0x0018
#define IRQ_ENT_REG1_OFFSET	0x001C
#define IRQ_INCTL_REG_OFFSET	0x0020
#define IRQ_EABASE_REG_OFFSET	0x0024
#define IRQ_INTPRI0_REG_OFFSET	0x0030
#define IRQ_INTPRI7_REG_OFFSET	0x004C

const u8 *davinci_def_priorities;

#define INTC_BASE IO_ADDRESS(DAVINCI_ARM_INTC_BASE)

static inline unsigned int davinci_irq_readl(int offset)
{
	return __raw_readl(INTC_BASE + offset);
}

static inline void davinci_irq_writel(unsigned long value, int offset)
{
	__raw_writel(value, INTC_BASE + offset);
}

/* Disable interrupt */
static void davinci_mask_irq(unsigned int irq)
{
	unsigned int mask;
	u32 l;

	mask = 1 << IRQ_BIT(irq);

	if (irq > 31) {
		l = davinci_irq_readl(IRQ_ENT_REG1_OFFSET);
		l &= ~mask;
		davinci_irq_writel(l, IRQ_ENT_REG1_OFFSET);
	} else {
		l = davinci_irq_readl(IRQ_ENT_REG0_OFFSET);
		l &= ~mask;
		davinci_irq_writel(l, IRQ_ENT_REG0_OFFSET);
	}
}

/* Enable interrupt */
static void davinci_unmask_irq(unsigned int irq)
{
	unsigned int mask;
	u32 l;

	mask = 1 << IRQ_BIT(irq);

	if (irq > 31) {
		l = davinci_irq_readl(IRQ_ENT_REG1_OFFSET);
		l |= mask;
		davinci_irq_writel(l, IRQ_ENT_REG1_OFFSET);
	} else {
		l = davinci_irq_readl(IRQ_ENT_REG0_OFFSET);
		l |= mask;
		davinci_irq_writel(l, IRQ_ENT_REG0_OFFSET);
	}
}

/* EOI interrupt */
static void davinci_ack_irq(unsigned int irq)
{
	unsigned int mask;

	mask = 1 << IRQ_BIT(irq);

	if (irq > 31)
		davinci_irq_writel(mask, IRQ_REG1_OFFSET);
	else
		davinci_irq_writel(mask, IRQ_REG0_OFFSET);
}

static struct irq_chip davinci_irq_chip_0 = {
	.name	= "AINTC",
	.ack	= davinci_ack_irq,
	.mask	= davinci_mask_irq,
	.unmask = davinci_unmask_irq,
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static const u8 dm644x_default_priorities[DAVINCI_N_AINTC_IRQ] __initdata = {
	[IRQ_VDINT0]		= 2,
	[IRQ_VDINT1]		= 6,
	[IRQ_VDINT2]		= 6,
	[IRQ_HISTINT]		= 6,
	[IRQ_H3AINT]		= 6,
	[IRQ_PRVUINT]		= 6,
	[IRQ_RSZINT]		= 6,
	[7]			= 7,
	[IRQ_VENCINT]		= 6,
	[IRQ_ASQINT]		= 6,
	[IRQ_IMXINT]		= 6,
	[IRQ_VLCDINT]		= 6,
	[IRQ_USBINT]		= 4,
	[IRQ_EMACINT]		= 4,
	[14]			= 7,
	[15]			= 7,
	[IRQ_CCINT0]		= 5,	/* dma */
	[IRQ_CCERRINT]		= 5,	/* dma */
	[IRQ_TCERRINT0]		= 5,	/* dma */
	[IRQ_TCERRINT]		= 5,	/* dma */
	[IRQ_PSCIN]		= 7,
	[21]			= 7,
	[IRQ_IDE]		= 4,
	[23]			= 7,
	[IRQ_MBXINT]		= 7,
	[IRQ_MBRINT]		= 7,
	[IRQ_MMCINT]		= 7,
	[IRQ_SDIOINT]		= 7,
	[28]			= 7,
	[IRQ_DDRINT]		= 7,
	[IRQ_AEMIFINT]		= 7,
	[IRQ_VLQINT]		= 4,
	[IRQ_TINT0_TINT12]	= 2,	/* clockevent */
	[IRQ_TINT0_TINT34]	= 2,	/* clocksource */
	[IRQ_TINT1_TINT12]	= 7,	/* DSP timer */
	[IRQ_TINT1_TINT34]	= 7,	/* system tick */
	[IRQ_PWMINT0]		= 7,
	[IRQ_PWMINT1]		= 7,
	[IRQ_PWMINT2]		= 7,
	[IRQ_I2C]		= 3,
	[IRQ_UARTINT0]		= 3,
	[IRQ_UARTINT1]		= 3,
	[IRQ_UARTINT2]		= 3,
	[IRQ_SPINT0]		= 3,
	[IRQ_SPINT1]		= 3,
	[45]			= 7,
	[IRQ_DSP2ARM0]		= 4,
	[IRQ_DSP2ARM1]		= 4,
	[IRQ_GPIO0]		= 7,
	[IRQ_GPIO1]		= 7,
	[IRQ_GPIO2]		= 7,
	[IRQ_GPIO3]		= 7,
	[IRQ_GPIO4]		= 7,
	[IRQ_GPIO5]		= 7,
	[IRQ_GPIO6]		= 7,
	[IRQ_GPIO7]		= 7,
	[IRQ_GPIOBNK0]		= 7,
	[IRQ_GPIOBNK1]		= 7,
	[IRQ_GPIOBNK2]		= 7,
	[IRQ_GPIOBNK3]		= 7,
	[IRQ_GPIOBNK4]		= 7,
	[IRQ_COMMTX]		= 7,
	[IRQ_COMMRX]		= 7,
	[IRQ_EMUINT]		= 7,
};

static const u8 dm646x_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_DM646X_VP_VERTINT0]        = 7,
	[IRQ_DM646X_VP_VERTINT1]        = 7,
	[IRQ_DM646X_VP_VERTINT2]        = 7,
	[IRQ_DM646X_VP_VERTINT3]        = 7,
	[IRQ_DM646X_VP_ERRINT]          = 7,
	[IRQ_DM646X_RESERVED_1]         = 7,
	[IRQ_DM646X_RESERVED_2]         = 7,
	[IRQ_DM646X_WDINT]              = 7,
	[IRQ_DM646X_CRGENINT0]          = 7,
	[IRQ_DM646X_CRGENINT1]          = 7,
	[IRQ_DM646X_TSIFINT0]           = 7,
	[IRQ_DM646X_TSIFINT1]           = 7,
	[IRQ_DM646X_VDCEINT]            = 7,
	[IRQ_DM646X_USBINT]             = 7,
	[IRQ_DM646X_USBDMAINT]          = 7,
	[IRQ_DM646X_PCIINT]             = 7,
	[IRQ_CCINT0]                    = 7,    /* dma */
	[IRQ_CCERRINT]                  = 7,    /* dma */
	[IRQ_TCERRINT0]                 = 7,    /* dma */
	[IRQ_TCERRINT]                  = 7,    /* dma */
	[IRQ_DM646X_TCERRINT2]          = 7,
	[IRQ_DM646X_TCERRINT3]          = 7,
	[IRQ_DM646X_IDE]                = 7,
	[IRQ_DM646X_HPIINT]             = 7,
	[IRQ_DM646X_EMACRXTHINT]        = 7,
	[IRQ_DM646X_EMACRXINT]          = 7,
	[IRQ_DM646X_EMACTXINT]          = 7,
	[IRQ_DM646X_EMACMISCINT]        = 7,
	[IRQ_DM646X_MCASP0TXINT]        = 7,
	[IRQ_DM646X_MCASP0RXINT]        = 7,
	[IRQ_AEMIFINT]                  = 7,
	[IRQ_DM646X_RESERVED_3]         = 7,
	[IRQ_DM646X_MCASP1TXINT]        = 7,    /* clockevent */
	[IRQ_TINT0_TINT34]              = 7,    /* clocksource */
	[IRQ_TINT1_TINT12]              = 7,    /* DSP timer */
	[IRQ_TINT1_TINT34]              = 7,    /* system tick */
	[IRQ_PWMINT0]                   = 7,
	[IRQ_PWMINT1]                   = 7,
	[IRQ_DM646X_VLQINT]             = 7,
	[IRQ_I2C]                       = 7,
	[IRQ_UARTINT0]                  = 7,
	[IRQ_UARTINT1]                  = 7,
	[IRQ_DM646X_UARTINT2]           = 7,
	[IRQ_DM646X_SPINT0]             = 7,
	[IRQ_DM646X_SPINT1]             = 7,
	[IRQ_DM646X_DSP2ARMINT]         = 7,
	[IRQ_DM646X_RESERVED_4]         = 7,
	[IRQ_DM646X_PSCINT]             = 7,
	[IRQ_DM646X_GPIO0]              = 7,
	[IRQ_DM646X_GPIO1]              = 7,
	[IRQ_DM646X_GPIO2]              = 7,
	[IRQ_DM646X_GPIO3]              = 7,
	[IRQ_DM646X_GPIO4]              = 7,
	[IRQ_DM646X_GPIO5]              = 7,
	[IRQ_DM646X_GPIO6]              = 7,
	[IRQ_DM646X_GPIO7]              = 7,
	[IRQ_DM646X_GPIOBNK0]           = 7,
	[IRQ_DM646X_GPIOBNK1]           = 7,
	[IRQ_DM646X_GPIOBNK2]           = 7,
	[IRQ_DM646X_DDRINT]             = 7,
	[IRQ_DM646X_AEMIFINT]           = 7,
	[IRQ_COMMTX]                    = 7,
	[IRQ_COMMRX]                    = 7,
	[IRQ_EMUINT]                    = 7,
};

static const u8 dm355_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_DM355_CCDC_VDINT0]		= 2,
	[IRQ_DM355_CCDC_VDINT1]		= 6,
	[IRQ_DM355_CCDC_VDINT2]		= 6,
	[IRQ_DM355_IPIPE_HST]		= 6,
	[IRQ_DM355_H3AINT]		= 6,
	[IRQ_DM355_IPIPE_SDR]		= 6,
	[IRQ_DM355_IPIPEIFINT]		= 6,
	[IRQ_DM355_OSDINT]		= 7,
	[IRQ_DM355_VENCINT]		= 6,
	[IRQ_ASQINT]			= 6,
	[IRQ_IMXINT]			= 6,
	[IRQ_USBINT]			= 4,
	[IRQ_DM355_RTOINT]		= 4,
	[IRQ_DM355_UARTINT2]		= 7,
	[IRQ_DM355_TINT6]		= 7,
	[IRQ_CCINT0]			= 5,	/* dma */
	[IRQ_CCERRINT]			= 5,	/* dma */
	[IRQ_TCERRINT0]			= 5,	/* dma */
	[IRQ_TCERRINT]			= 5,	/* dma */
	[IRQ_DM355_SPINT2_1]		= 7,
	[IRQ_DM355_TINT7]		= 4,
	[IRQ_DM355_SDIOINT0]		= 7,
	[IRQ_MBXINT]			= 7,
	[IRQ_MBRINT]			= 7,
	[IRQ_MMCINT]			= 7,
	[IRQ_DM355_MMCINT1]		= 7,
	[IRQ_DM355_PWMINT3]		= 7,
	[IRQ_DDRINT]			= 7,
	[IRQ_AEMIFINT]			= 7,
	[IRQ_DM355_SDIOINT1]		= 4,
	[IRQ_TINT0_TINT12]		= 2,	/* clockevent */
	[IRQ_TINT0_TINT34]		= 2,	/* clocksource */
	[IRQ_TINT1_TINT12]		= 7,	/* DSP timer */
	[IRQ_TINT1_TINT34]		= 7,	/* system tick */
	[IRQ_PWMINT0]			= 7,
	[IRQ_PWMINT1]			= 7,
	[IRQ_PWMINT2]			= 7,
	[IRQ_I2C]			= 3,
	[IRQ_UARTINT0]			= 3,
	[IRQ_UARTINT1]			= 3,
	[IRQ_DM355_SPINT0_0]		= 3,
	[IRQ_DM355_SPINT0_1]		= 3,
	[IRQ_DM355_GPIO0]		= 3,
	[IRQ_DM355_GPIO1]		= 7,
	[IRQ_DM355_GPIO2]		= 4,
	[IRQ_DM355_GPIO3]		= 4,
	[IRQ_DM355_GPIO4]		= 7,
	[IRQ_DM355_GPIO5]		= 7,
	[IRQ_DM355_GPIO6]		= 7,
	[IRQ_DM355_GPIO7]		= 7,
	[IRQ_DM355_GPIO8]		= 7,
	[IRQ_DM355_GPIO9]		= 7,
	[IRQ_DM355_GPIOBNK0]		= 7,
	[IRQ_DM355_GPIOBNK1]		= 7,
	[IRQ_DM355_GPIOBNK2]		= 7,
	[IRQ_DM355_GPIOBNK3]		= 7,
	[IRQ_DM355_GPIOBNK4]		= 7,
	[IRQ_DM355_GPIOBNK5]		= 7,
	[IRQ_DM355_GPIOBNK6]		= 7,
	[IRQ_COMMTX]			= 7,
	[IRQ_COMMRX]			= 7,
	[IRQ_EMUINT]			= 7,
};

/* ARM Interrupt Controller Initialization */
void __init davinci_irq_init(void)
{
	unsigned i;

	if (cpu_is_davinci_dm644x())
		davinci_def_priorities = dm644x_default_priorities;
	else if (cpu_is_davinci_dm646x())
		davinci_def_priorities = dm646x_default_priorities;
	else if (cpu_is_davinci_dm355())
		davinci_def_priorities = dm355_default_priorities;

	/* Clear all interrupt requests */
	davinci_irq_writel(~0x0, FIQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, FIQ_REG1_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG1_OFFSET);

	/* Disable all interrupts */
	davinci_irq_writel(0x0, IRQ_ENT_REG0_OFFSET);
	davinci_irq_writel(0x0, IRQ_ENT_REG1_OFFSET);

	/* Interrupts disabled immediately, IRQ entry reflects all */
	davinci_irq_writel(0x0, IRQ_INCTL_REG_OFFSET);

	/* we don't use the hardware vector table, just its entry addresses */
	davinci_irq_writel(0, IRQ_EABASE_REG_OFFSET);

	/* Clear all interrupt requests */
	davinci_irq_writel(~0x0, FIQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, FIQ_REG1_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG1_OFFSET);

	for (i = IRQ_INTPRI0_REG_OFFSET; i <= IRQ_INTPRI7_REG_OFFSET; i += 4) {
		unsigned	j;
		u32		pri;

		for (j = 0, pri = 0; j < 32; j += 4, davinci_def_priorities++)
			pri |= (*davinci_def_priorities & 0x07) << j;
		davinci_irq_writel(pri, i);
	}

	/* set up genirq dispatch for ARM INTC */
	for (i = 0; i < DAVINCI_N_AINTC_IRQ; i++) {
		set_irq_chip(i, &davinci_irq_chip_0);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		if (i != IRQ_TINT1_TINT34)
			set_irq_handler(i, handle_edge_irq);
		else
			set_irq_handler(i, handle_level_irq);
	}
}
