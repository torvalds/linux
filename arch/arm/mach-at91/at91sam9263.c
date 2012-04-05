/*
 * arch/arm/mach-at91/at91sam9263.c
 *
 *  Copyright (C) 2007 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>

#include <asm/proc-fns.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/at91sam9263.h>
#include <mach/at91_pmc.h>
#include <mach/at91_rstc.h>

#include "soc.h"
#include "generic.h"
#include "clock.h"
#include "sam9_smc.h"

/* --------------------------------------------------------------------
 *  Clocks
 * -------------------------------------------------------------------- */

/*
 * The peripheral clocks.
 */
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioCDE_clk = {
	.name		= "pioCDE_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_PIOCDE,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc0_clk = {
	.name		= "mci0_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_MCI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc1_clk = {
	.name		= "mci1_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_MCI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk can_clk = {
	.name		= "can_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_CAN,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi_clk = {
	.name		= "twi_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_TWI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ac97_clk = {
	.name		= "ac97_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_AC97C,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tcb_clk = {
	.name		= "tcb_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_TCB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pwm_clk = {
	.name		= "pwm_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_PWMC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk macb_clk = {
	.name		= "pclk",
	.pmc_mask	= 1 << AT91SAM9263_ID_EMAC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk dma_clk = {
	.name		= "dma_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_DMA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twodge_clk = {
	.name		= "2dge_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_2DGE,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udc_clk = {
	.name		= "udc_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk isi_clk = {
	.name		= "isi_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_ISI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 1 << AT91SAM9263_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioCDE_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&mmc0_clk,
	&mmc1_clk,
	&can_clk,
	&twi_clk,
	&spi0_clk,
	&spi1_clk,
	&ssc0_clk,
	&ssc1_clk,
	&ac97_clk,
	&tcb_clk,
	&pwm_clk,
	&macb_clk,
	&twodge_clk,
	&udc_clk,
	&isi_clk,
	&lcdc_clk,
	&dma_clk,
	&ohci_clk,
	// irq0 .. irq1
};

static struct clk_lookup periph_clocks_lookups[] = {
	/* One additional fake clock for macb_hclk */
	CLKDEV_CON_ID("hclk", &macb_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.0", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.1", &ssc1_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "at91_mci.0", &mmc0_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "at91_mci.1", &mmc1_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.0", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.1", &spi1_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tcb_clk),
	/* fake hclk clock */
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &ohci_clk),
	CLKDEV_CON_ID("pioA", &pioA_clk),
	CLKDEV_CON_ID("pioB", &pioB_clk),
	CLKDEV_CON_ID("pioC", &pioCDE_clk),
	CLKDEV_CON_ID("pioD", &pioCDE_clk),
	CLKDEV_CON_ID("pioE", &pioCDE_clk),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.1", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.2", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.3", &usart2_clk),
};

/*
 * The four programmable clocks.
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

static void __init at91sam9263_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);

	clkdev_add_table(periph_clocks_lookups,
			 ARRAY_SIZE(periph_clocks_lookups));
	clkdev_add_table(usart_clocks_lookups,
			 ARRAY_SIZE(usart_clocks_lookups));

	clk_register(&pck0);
	clk_register(&pck1);
	clk_register(&pck2);
	clk_register(&pck3);
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at91sam9263_gpio[] __initdata = {
	{
		.id		= AT91SAM9263_ID_PIOA,
		.regbase	= AT91SAM9263_BASE_PIOA,
	}, {
		.id		= AT91SAM9263_ID_PIOB,
		.regbase	= AT91SAM9263_BASE_PIOB,
	}, {
		.id		= AT91SAM9263_ID_PIOCDE,
		.regbase	= AT91SAM9263_BASE_PIOC,
	}, {
		.id		= AT91SAM9263_ID_PIOCDE,
		.regbase	= AT91SAM9263_BASE_PIOD,
	}, {
		.id		= AT91SAM9263_ID_PIOCDE,
		.regbase	= AT91SAM9263_BASE_PIOE,
	}
};

/* --------------------------------------------------------------------
 *  AT91SAM9263 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9263_map_io(void)
{
	at91_init_sram(0, AT91SAM9263_SRAM0_BASE, AT91SAM9263_SRAM0_SIZE);
	at91_init_sram(1, AT91SAM9263_SRAM1_BASE, AT91SAM9263_SRAM1_SIZE);
}

static void __init at91sam9263_ioremap_registers(void)
{
	at91_ioremap_shdwc(AT91SAM9263_BASE_SHDWC);
	at91_ioremap_rstc(AT91SAM9263_BASE_RSTC);
	at91_ioremap_ramc(0, AT91SAM9263_BASE_SDRAMC0, 512);
	at91_ioremap_ramc(1, AT91SAM9263_BASE_SDRAMC1, 512);
	at91sam926x_ioremap_pit(AT91SAM9263_BASE_PIT);
	at91sam9_ioremap_smc(0, AT91SAM9263_BASE_SMC0);
	at91sam9_ioremap_smc(1, AT91SAM9263_BASE_SMC1);
	at91_ioremap_matrix(AT91SAM9263_BASE_MATRIX);
}

static void __init at91sam9263_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
	arm_pm_restart = at91sam9_alt_restart;
	at91_extern_irq = (1 << AT91SAM9263_ID_IRQ0) | (1 << AT91SAM9263_ID_IRQ1);

	/* Register GPIO subsystem */
	at91_gpio_init(at91sam9263_gpio, 5);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9263_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller (FIQ) */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C, D and E */
	0,
	0,
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	0,	/* Multimedia Card Interface 0 */
	0,	/* Multimedia Card Interface 1 */
	3,	/* CAN */
	6,	/* Two-Wire Interface */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	4,	/* Serial Synchronous Controller 0 */
	4,	/* Serial Synchronous Controller 1 */
	5,	/* AC97 Controller */
	0,	/* Timer Counter 0, 1 and 2 */
	0,	/* Pulse Width Modulation Controller */
	3,	/* Ethernet */
	0,
	0,	/* 2D Graphic Engine */
	2,	/* USB Device Port */
	0,	/* Image Sensor Interface */
	3,	/* LDC Controller */
	0,	/* DMA Controller */
	0,
	2,	/* USB Host port */
	0,	/* Advanced Interrupt Controller (IRQ0) */
	0,	/* Advanced Interrupt Controller (IRQ1) */
};

struct at91_init_soc __initdata at91sam9263_soc = {
	.map_io = at91sam9263_map_io,
	.default_irq_priority = at91sam9263_default_irq_priority,
	.ioremap_registers = at91sam9263_ioremap_registers,
	.register_clocks = at91sam9263_register_clocks,
	.init = at91sam9263_initialize,
};
