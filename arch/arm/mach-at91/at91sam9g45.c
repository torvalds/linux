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
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/at91sam9g45.h>
#include <mach/at91_aic.h>
#include <mach/at91_pmc.h>
#include <mach/cpu.h>

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
static struct clk trng_clk = {
	.name		= "trng_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_TRNG,
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
static struct clk tcb0_clk = {
	.name		= "tcb0_clk",
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
	.name		= "pclk",
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

/* Video decoder clock - Only for sam9m10/sam9m11 */
static struct clk vdec_clk = {
	.name		= "vdec_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_VDEC,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk adc_op_clk = {
	.name		= "adc_op_clk",
	.type		= CLK_TYPE_PERIPHERAL,
	.rate_hz	= 13200000,
};

/* AES/TDES/SHA clock - Only for sam9m11/sam9g56 */
static struct clk aestdessha_clk = {
	.name		= "aestdessha_clk",
	.pmc_mask	= 1 << AT91SAM9G45_ID_AESTDESSHA,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&pioDE_clk,
	&trng_clk,
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
	&tcb0_clk,
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
	&adc_op_clk,
	&aestdessha_clk,
	// irq0
};

static struct clk_lookup periph_clocks_lookups[] = {
	/* One additional fake clock for macb_hclk */
	CLKDEV_CON_ID("hclk", &macb_clk),
	/* One additional fake clock for ohci */
	CLKDEV_CON_ID("ohci_clk", &uhphs_clk),
	CLKDEV_CON_DEV_ID("ehci_clk", "atmel-ehci", &uhphs_clk),
	CLKDEV_CON_DEV_ID("hclk", "atmel_usba_udc", &utmi_clk),
	CLKDEV_CON_DEV_ID("pclk", "atmel_usba_udc", &udphs_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "atmel_mci.0", &mmc0_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "atmel_mci.1", &mmc1_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.0", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.1", &spi1_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tcb0_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.1", &tcb0_clk),
	CLKDEV_CON_DEV_ID(NULL, "at91_i2c.0", &twi0_clk),
	CLKDEV_CON_DEV_ID(NULL, "at91_i2c.1", &twi1_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.0", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.1", &ssc1_clk),
	CLKDEV_CON_DEV_ID(NULL, "atmel-trng", &trng_clk),
	CLKDEV_CON_DEV_ID(NULL, "atmel_sha", &aestdessha_clk),
	CLKDEV_CON_DEV_ID(NULL, "atmel_tdes", &aestdessha_clk),
	CLKDEV_CON_DEV_ID(NULL, "atmel_aes", &aestdessha_clk),
	/* more usart lookup table for DT entries */
	CLKDEV_CON_DEV_ID("usart", "ffffee00.serial", &mck),
	CLKDEV_CON_DEV_ID("usart", "fff8c000.serial", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "fff90000.serial", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "fff94000.serial", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "fff98000.serial", &usart3_clk),
	/* more tc lookup table for DT entries */
	CLKDEV_CON_DEV_ID("t0_clk", "fff7c000.timer", &tcb0_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "fffd4000.timer", &tcb0_clk),
	CLKDEV_CON_DEV_ID("hclk", "700000.ohci", &uhphs_clk),
	CLKDEV_CON_DEV_ID("ehci_clk", "800000.ehci", &uhphs_clk),
	/* fake hclk clock */
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &uhphs_clk),
	CLKDEV_CON_ID("pioA", &pioA_clk),
	CLKDEV_CON_ID("pioB", &pioB_clk),
	CLKDEV_CON_ID("pioC", &pioC_clk),
	CLKDEV_CON_ID("pioD", &pioDE_clk),
	CLKDEV_CON_ID("pioE", &pioDE_clk),
	/* Fake adc clock */
	CLKDEV_CON_ID("adc_clk", &tsc_clk),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.1", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.2", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.3", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.4", &usart3_clk),
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

	clkdev_add_table(periph_clocks_lookups,
			 ARRAY_SIZE(periph_clocks_lookups));
	clkdev_add_table(usart_clocks_lookups,
			 ARRAY_SIZE(usart_clocks_lookups));

	if (cpu_is_at91sam9m10() || cpu_is_at91sam9m11())
		clk_register(&vdec_clk);

	clk_register(&pck0);
	clk_register(&pck1);
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at91sam9g45_gpio[] __initdata = {
	{
		.id		= AT91SAM9G45_ID_PIOA,
		.regbase	= AT91SAM9G45_BASE_PIOA,
	}, {
		.id		= AT91SAM9G45_ID_PIOB,
		.regbase	= AT91SAM9G45_BASE_PIOB,
	}, {
		.id		= AT91SAM9G45_ID_PIOC,
		.regbase	= AT91SAM9G45_BASE_PIOC,
	}, {
		.id		= AT91SAM9G45_ID_PIODE,
		.regbase	= AT91SAM9G45_BASE_PIOD,
	}, {
		.id		= AT91SAM9G45_ID_PIODE,
		.regbase	= AT91SAM9G45_BASE_PIOE,
	}
};

/* --------------------------------------------------------------------
 *  AT91SAM9G45 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9g45_map_io(void)
{
	at91_init_sram(0, AT91SAM9G45_SRAM_BASE, AT91SAM9G45_SRAM_SIZE);
	init_consistent_dma_size(SZ_4M);
}

static void __init at91sam9g45_ioremap_registers(void)
{
	at91_ioremap_shdwc(AT91SAM9G45_BASE_SHDWC);
	at91_ioremap_rstc(AT91SAM9G45_BASE_RSTC);
	at91_ioremap_ramc(0, AT91SAM9G45_BASE_DDRSDRC1, 512);
	at91_ioremap_ramc(1, AT91SAM9G45_BASE_DDRSDRC0, 512);
	at91sam926x_ioremap_pit(AT91SAM9G45_BASE_PIT);
	at91sam9_ioremap_smc(0, AT91SAM9G45_BASE_SMC);
	at91_ioremap_matrix(AT91SAM9G45_BASE_MATRIX);
}

static void __init at91sam9g45_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
	arm_pm_restart = at91sam9g45_restart;
	at91_extern_irq = (1 << AT91SAM9G45_ID_IRQ0);

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
	0,	/* AESTDESSHA Crypto HW Accelerators */
	0,	/* Multimedia Card Interface 1 */
	0,
	0,	/* Advanced Interrupt Controller (IRQ0) */
};

struct at91_init_soc __initdata at91sam9g45_soc = {
	.map_io = at91sam9g45_map_io,
	.default_irq_priority = at91sam9g45_default_irq_priority,
	.ioremap_registers = at91sam9g45_ioremap_registers,
	.register_clocks = at91sam9g45_register_clocks,
	.init = at91sam9g45_initialize,
};
