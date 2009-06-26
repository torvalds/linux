/*
 *  Chip-specific setup code for the AT91SAM9G45 family
 *
 *  Copyright (C) 2009 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/pm.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/at91sam9g45.h>
#include <mach/at91_pmc.h>
#include <mach/at91_rstc.h>
#include <mach/at91_shdwc.h>

#include "generic.h"
#include "clock.h"

static struct map_desc at91sam9g45_io_desc[] __initdata = {
	{
		.virtual	= AT91_VA_BASE_SYS,
		.pfn		= __phys_to_pfn(AT91_BASE_SYS),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_IO_VIRT_BASE - AT91SAM9G45_SRAM_SIZE,
		.pfn		= __phys_to_pfn(AT91SAM9G45_SRAM_BASE),
		.length		= AT91SAM9G45_SRAM_SIZE,
		.type		= MT_DEVICE,
	}
};

/* --------------------------------------------------------------------
 *  Clocks
 * -------------------------------------------------------------------- */

/*
 * The peripheral clocks.
 */
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioDE_clk = {
	.name		= "pioDE_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_PIODE,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_US3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc0_clk = {
	.name		= "mci0_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_MCI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi0_clk = {
	.name		= "twi0_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_TWI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi1_clk = {
	.name		= "twi1_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_TWI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tcb_clk = {
	.name		= "tcb_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_TCB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pwm_clk = {
	.name		= "pwm_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_PWMC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tsc_clk = {
	.name		= "tsc_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_TSC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk dma_clk = {
	.name		= "dma_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_DMA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uhphs_clk = {
	.name		= "uhphs_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_UHPHS,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ac97_clk = {
	.name		= "ac97_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_AC97C,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk macb_clk = {
	.name		= "macb_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_EMAC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk isi_clk = {
	.name		= "isi_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_ISI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udphs_clk = {
	.name		= "udphs_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_UDPHS,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc1_clk = {
	.name		= "mci1_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_MCI1,
	.type		= CLK_TYPE_PERIPHERAL,
};

/* One additional fake clock for ohci */
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 0,
	.type		= CLK_TYPE_PERIPHERAL,
	.parent		= &uhphs_clk,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&pioDE_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&usart3_clk,
	&mmc0_clk,
	&twi0_clk,
	&twi1_clk,
	&spi0_clk,
	&spi1_clk,
	&ssc0_clk,
	&ssc1_clk,
	&tcb_clk,
	&pwm_clk,
	&tsc_clk,
	&dma_clk,
	&uhphs_clk,
	&lcdc_clk,
	&ac97_clk,
	&macb_clk,
	&isi_clk,
	&udphs_clk,
	&mmc1_clk,
	// irq0
	&ohci_clk,
};

/*
 * The two programmable clocks.
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

static void __init at91sam9g45_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);

	clk_register(&pck0);
	clk_register(&pck1);
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at91sam9g45_gpio[] = {
	{
		.id		= AT91SAM9G45_ID_PIOA,
		.offset		= AT91_PIOA,
		.clock		= &pioA_clk,
	}, {
		.id		= AT91SAM9G45_ID_PIOB,
		.offset		= AT91_PIOB,
		.clock		= &pioB_clk,
	}, {
		.id		= AT91SAM9G45_ID_PIOC,
		.offset		= AT91_PIOC,
		.clock		= &pioC_clk,
	}, {
		.id		= AT91SAM9G45_ID_PIODE,
		.offset		= AT91_PIOD,
		.clock		= &pioDE_clk,
	}, {
		.id		= AT91SAM9G45_ID_PIODE,
		.offset		= AT91_PIOE,
		.clock		= &pioDE_clk,
	}
};

static void at91sam9g45_reset(void)
{
	at91_sys_write(AT91_RSTC_CR, AT91_RSTC_KEY | AT91_RSTC_PROCRST | AT91_RSTC_PERRST);
}

static void at91sam9g45_poweroff(void)
{
	at91_sys_write(AT91_SHDW_CR, AT91_SHDW_KEY | AT91_SHDW_SHDW);
}


/* --------------------------------------------------------------------
 *  AT91SAM9G45 processor initialization
 * -------------------------------------------------------------------- */

void __init at91sam9g45_initialize(unsigned long main_clock)
{
	/* Map peripherals */
	iotable_init(at91sam9g45_io_desc, ARRAY_SIZE(at91sam9g45_io_desc));

	at91_arch_reset = at91sam9g45_reset;
	pm_power_off = at91sam9g45_poweroff;
	at91_extern_irq = (1 << AT91SAM9G45_ID_IRQ0);

	/* Init clock subsystem */
	at91_clock_init(main_clock);

	/* Register the processor-specific clocks */
	at91sam9g45_register_clocks();

	/* Register GPIO subsystem */
	at91_gpio_init(at91sam9g45_gpio, 5);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9g45_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller (FIQ) */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C */
	1,	/* Parallel IO Controller D and E */
	0,
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	5,	/* USART 3 */
	0,	/* Multimedia Card Interface 0 */
	6,	/* Two-Wire Interface 0 */
	6,	/* Two-Wire Interface 1 */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	4,	/* Serial Synchronous Controller 0 */
	4,	/* Serial Synchronous Controller 1 */
	0,	/* Timer Counter 0, 1, 2, 3, 4 and 5 */
	0,	/* Pulse Width Modulation Controller */
	0,	/* Touch Screen Controller */
	0,	/* DMA Controller */
	2,	/* USB Host High Speed port */
	3,	/* LDC Controller */
	5,	/* AC97 Controller */
	3,	/* Ethernet */
	0,	/* Image Sensor Interface */
	2,	/* USB Device High speed port */
	0,
	0,	/* Multimedia Card Interface 1 */
	0,
	0,	/* Advanced Interrupt Controller (IRQ0) */
};

void __init at91sam9g45_init_interrupts(unsigned int priority[NR_AIC_IRQS])
{
	if (!priority)
		priority = at91sam9g45_default_irq_priority;

	/* Initialize the AIC interrupt controller */
	at91_aic_init(priority);

	/* Enable GPIO interrupts */
	at91_gpio_irq_setup();
}
