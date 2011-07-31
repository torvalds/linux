/*
 * arch/arm/mach-at91/at572d940hf.c
 *
 * Antonio R. Costa <costa.antonior@gmail.com>
 * Copyright (C) 2008 Atmel
 *
 * Copyright (C) 2005 SAN People
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
 */

#include <linux/module.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/at572d940hf.h>
#include <mach/at91_pmc.h>
#include <mach/at91_rstc.h>

#include "generic.h"
#include "clock.h"

static struct map_desc at572d940hf_io_desc[] __initdata = {
	{
		.virtual	= AT91_VA_BASE_SYS,
		.pfn		= __phys_to_pfn(AT91_BASE_SYS),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_IO_VIRT_BASE - AT572D940HF_SRAM_SIZE,
		.pfn		= __phys_to_pfn(AT572D940HF_SRAM_BASE),
		.length		= AT572D940HF_SRAM_SIZE,
		.type		= MT_DEVICE,
	},
};

/* --------------------------------------------------------------------
 *  Clocks
 * -------------------------------------------------------------------- */

/*
 * The peripheral clocks.
 */
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk macb_clk = {
	.name		= "macb_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_EMAC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udc_clk = {
	.name		= "udc_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi0_clk = {
	.name		= "twi0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_TWI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc2_clk = {
	.name		= "ssc2_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SSC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc0_clk = {
	.name		= "tc0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_TC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc1_clk = {
	.name		= "tc1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_TC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc2_clk = {
	.name		= "tc2_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_TC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc3_clk = {
	.name		= "ssc3_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_SSC3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi1_clk = {
	.name		= "twi1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_TWI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk can0_clk = {
	.name		= "can0_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_CAN0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk can1_clk = {
	.name		= "can1_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_CAN1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mAgicV_clk = {
	.name		= "mAgicV_clk",
	.pmc_mask	= 1 << AT572D940HF_ID_MSIRQ0,
	.type		= CLK_TYPE_PERIPHERAL,
};


static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&macb_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&mmc_clk,
	&udc_clk,
	&twi0_clk,
	&spi0_clk,
	&spi1_clk,
	&ssc0_clk,
	&ssc1_clk,
	&ssc2_clk,
	&tc0_clk,
	&tc1_clk,
	&tc2_clk,
	&ohci_clk,
	&ssc3_clk,
	&twi1_clk,
	&can0_clk,
	&can1_clk,
	&mAgicV_clk,
	/* irq0 .. irq2 */
};

/*
 * The five programmable clocks.
 * You must configure pin multiplexing to bring these signals out.
 */
static struct clk pck0 = {
	.name		= "pck0",
	.pmc_mask	= AT91_PMC_PCK0,
	.type		= CLK_TYPE_PROGRAMMABLE,
	.id		= 0,
};
static struct clk pck1 = {
	.name		= "pck1",
	.pmc_mask	= AT91_PMC_PCK1,
	.type		= CLK_TYPE_PROGRAMMABLE,
	.id		= 1,
};
static struct clk pck2 = {
	.name		= "pck2",
	.pmc_mask	= AT91_PMC_PCK2,
	.type		= CLK_TYPE_PROGRAMMABLE,
	.id		= 2,
};
static struct clk pck3 = {
	.name		= "pck3",
	.pmc_mask	= AT91_PMC_PCK3,
	.type		= CLK_TYPE_PROGRAMMABLE,
	.id		= 3,
};

static struct clk mAgicV_mem_clk = {
	.name		= "mAgicV_mem_clk",
	.pmc_mask	= AT91_PMC_PCK4,
	.type		= CLK_TYPE_PROGRAMMABLE,
	.id		= 4,
};

/* HClocks */
static struct clk hck0 = {
	.name		= "hck0",
	.pmc_mask	= AT91_PMC_HCK0,
	.type		= CLK_TYPE_SYSTEM,
	.id		= 0,
};
static struct clk hck1 = {
	.name		= "hck1",
	.pmc_mask	= AT91_PMC_HCK1,
	.type		= CLK_TYPE_SYSTEM,
	.id		= 1,
};

static void __init at572d940hf_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);

	clk_register(&pck0);
	clk_register(&pck1);
	clk_register(&pck2);
	clk_register(&pck3);
	clk_register(&mAgicV_mem_clk);

	clk_register(&hck0);
	clk_register(&hck1);
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at572d940hf_gpio[] = {
	{
		.id		= AT572D940HF_ID_PIOA,
		.offset		= AT91_PIOA,
		.clock		= &pioA_clk,
	}, {
		.id		= AT572D940HF_ID_PIOB,
		.offset		= AT91_PIOB,
		.clock		= &pioB_clk,
	}, {
		.id		= AT572D940HF_ID_PIOC,
		.offset		= AT91_PIOC,
		.clock		= &pioC_clk,
	}
};

static void at572d940hf_reset(void)
{
	at91_sys_write(AT91_RSTC_CR, AT91_RSTC_KEY | AT91_RSTC_PROCRST | AT91_RSTC_PERRST);
}


/* --------------------------------------------------------------------
 *  AT572D940HF processor initialization
 * -------------------------------------------------------------------- */

void __init at572d940hf_initialize(unsigned long main_clock)
{
	/* Map peripherals */
	iotable_init(at572d940hf_io_desc, ARRAY_SIZE(at572d940hf_io_desc));

	at91_arch_reset = at572d940hf_reset;
	at91_extern_irq = (1 << AT572D940HF_ID_IRQ0) | (1 << AT572D940HF_ID_IRQ1)
			| (1 << AT572D940HF_ID_IRQ2);

	/* Init clock subsystem */
	at91_clock_init(main_clock);

	/* Register the processor-specific clocks */
	at572d940hf_register_clocks();

	/* Register GPIO subsystem */
	at91_gpio_init(at572d940hf_gpio, 3);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at572d940hf_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller */
	7,	/* System Peripherals */
	0,	/* Parallel IO Controller A */
	0,	/* Parallel IO Controller B */
	0,	/* Parallel IO Controller C */
	3,	/* Ethernet */
	6,	/* USART 0 */
	6,	/* USART 1 */
	6,	/* USART 2 */
	0,	/* Multimedia Card Interface */
	4,	/* USB Device Port */
	0,	/* Two-Wire Interface 0 */
	6,	/* Serial Peripheral Interface 0 */
	6,	/* Serial Peripheral Interface 1 */
	5,	/* Serial Synchronous Controller 0 */
	5,	/* Serial Synchronous Controller 1 */
	5,	/* Serial Synchronous Controller 2 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	3,	/* USB Host port */
	3,	/* Serial Synchronous Controller 3 */
	0,	/* Two-Wire Interface 1 */
	0,	/* CAN Controller 0 */
	0,	/* CAN Controller 1 */
	0,	/* mAgicV HALT line */
	0,	/* mAgicV SIRQ0 line */
	0,	/* mAgicV exception line */
	0,	/* mAgicV end of DMA line */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
};

void __init at572d940hf_init_interrupts(unsigned int priority[NR_AIC_IRQS])
{
	if (!priority)
		priority = at572d940hf_default_irq_priority;

	/* Initialize the AIC interrupt controller */
	at91_aic_init(priority);

	/* Enable GPIO interrupts */
	at91_gpio_irq_setup();
}

