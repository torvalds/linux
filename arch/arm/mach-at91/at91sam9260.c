/*
 * arch/arm/mach-at91/at91sam9260.c
 *
 *  Copyright (C) 2006 SAN People
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
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/at91sam9260.h>
#include <mach/at91_pmc.h>

#include "at91_aic.h"
#include "at91_rstc.h"
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
	.pmc_mask	= 1 << AT91SAM9260_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk adc_clk = {
	.name		= "adc_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_ADC,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk adc_op_clk = {
	.name		= "adc_op_clk",
	.type		= CLK_TYPE_PERIPHERAL,
	.rate_hz	= 5000000,
};

static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udc_clk = {
	.name		= "udc_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi_clk = {
	.name		= "twi_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TWI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc_clk = {
	.name		= "ssc_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_SSC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc0_clk = {
	.name		= "tc0_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc1_clk = {
	.name		= "tc1_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc2_clk = {
	.name		= "tc2_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk macb_clk = {
	.name		= "pclk",
	.pmc_mask	= 1 << AT91SAM9260_ID_EMAC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk isi_clk = {
	.name		= "isi_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_ISI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart4_clk = {
	.name		= "usart4_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US4,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart5_clk = {
	.name		= "usart5_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_US5,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc3_clk = {
	.name		= "tc3_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc4_clk = {
	.name		= "tc4_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC4,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc5_clk = {
	.name		= "tc5_clk",
	.pmc_mask	= 1 << AT91SAM9260_ID_TC5,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&adc_clk,
	&adc_op_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&mmc_clk,
	&udc_clk,
	&twi_clk,
	&spi0_clk,
	&spi1_clk,
	&ssc_clk,
	&tc0_clk,
	&tc1_clk,
	&tc2_clk,
	&ohci_clk,
	&macb_clk,
	&isi_clk,
	&usart3_clk,
	&usart4_clk,
	&usart5_clk,
	&tc3_clk,
	&tc4_clk,
	&tc5_clk,
	// irq0 .. irq2
};

static struct clk_lookup periph_clocks_lookups[] = {
	/* One additional fake clock for macb_hclk */
	CLKDEV_CON_ID("hclk", &macb_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.0", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.1", &spi1_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.0", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.0", &tc2_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.1", &tc3_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.1", &tc4_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.1", &tc5_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.0", &ssc_clk),
	CLKDEV_CON_DEV_ID("pclk", "fffbc000.ssc", &ssc_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9260.0", &twi_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9g20.0", &twi_clk),
	/* more usart lookup table for DT entries */
	CLKDEV_CON_DEV_ID("usart", "fffff200.serial", &mck),
	CLKDEV_CON_DEV_ID("usart", "fffb0000.serial", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "fffb4000.serial", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "fffb8000.serial", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "fffd0000.serial", &usart3_clk),
	CLKDEV_CON_DEV_ID("usart", "fffd4000.serial", &usart4_clk),
	CLKDEV_CON_DEV_ID("usart", "fffd8000.serial", &usart5_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffac000.i2c", &twi_clk),
	/* more tc lookup table for DT entries */
	CLKDEV_CON_DEV_ID("t0_clk", "fffa0000.timer", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "fffa0000.timer", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "fffa0000.timer", &tc2_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "fffdc000.timer", &tc3_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "fffdc000.timer", &tc4_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "fffdc000.timer", &tc5_clk),
	CLKDEV_CON_DEV_ID("hclk", "500000.ohci", &ohci_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "fffa8000.mmc", &mmc_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "fffc8000.spi", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "fffcc000.spi", &spi1_clk),
	/* fake hclk clock */
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &ohci_clk),
	CLKDEV_CON_ID("pioA", &pioA_clk),
	CLKDEV_CON_ID("pioB", &pioB_clk),
	CLKDEV_CON_ID("pioC", &pioC_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff400.gpio", &pioA_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff600.gpio", &pioB_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff800.gpio", &pioC_clk),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.1", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.2", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.3", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.4", &usart3_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.5", &usart4_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.6", &usart5_clk),
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

static void __init at91sam9260_register_clocks(void)
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
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at91sam9260_gpio[] __initdata = {
	{
		.id		= AT91SAM9260_ID_PIOA,
		.regbase	= AT91SAM9260_BASE_PIOA,
	}, {
		.id		= AT91SAM9260_ID_PIOB,
		.regbase	= AT91SAM9260_BASE_PIOB,
	}, {
		.id		= AT91SAM9260_ID_PIOC,
		.regbase	= AT91SAM9260_BASE_PIOC,
	}
};

/* --------------------------------------------------------------------
 *  AT91SAM9260 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9xe_map_io(void)
{
	unsigned long sram_size;

	switch (at91_soc_initdata.cidr & AT91_CIDR_SRAMSIZ) {
		case AT91_CIDR_SRAMSIZ_32K:
			sram_size = 2 * SZ_16K;
			break;
		case AT91_CIDR_SRAMSIZ_16K:
		default:
			sram_size = SZ_16K;
	}

	at91_init_sram(0, AT91SAM9XE_SRAM_BASE, sram_size);
}

static void __init at91sam9260_map_io(void)
{
	if (cpu_is_at91sam9xe())
		at91sam9xe_map_io();
	else if (cpu_is_at91sam9g20())
		at91_init_sram(0, AT91SAM9G20_SRAM_BASE, AT91SAM9G20_SRAM_SIZE);
	else
		at91_init_sram(0, AT91SAM9260_SRAM_BASE, AT91SAM9260_SRAM_SIZE);
}

static void __init at91sam9260_ioremap_registers(void)
{
	at91_ioremap_shdwc(AT91SAM9260_BASE_SHDWC);
	at91_ioremap_rstc(AT91SAM9260_BASE_RSTC);
	at91_ioremap_ramc(0, AT91SAM9260_BASE_SDRAMC, 512);
	at91sam926x_ioremap_pit(AT91SAM9260_BASE_PIT);
	at91sam9_ioremap_smc(0, AT91SAM9260_BASE_SMC);
	at91_ioremap_matrix(AT91SAM9260_BASE_MATRIX);
}

static void __init at91sam9260_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
	arm_pm_restart = at91sam9_alt_restart;
	at91_extern_irq = (1 << AT91SAM9260_ID_IRQ0) | (1 << AT91SAM9260_ID_IRQ1)
			| (1 << AT91SAM9260_ID_IRQ2);

	at91_sysirq_mask_rtt(AT91SAM9260_BASE_RTT);

	/* Register GPIO subsystem */
	at91_gpio_init(at91sam9260_gpio, 3);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9260_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C */
	0,	/* Analog-to-Digital Converter */
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	0,	/* Multimedia Card Interface */
	2,	/* USB Device Port */
	6,	/* Two-Wire Interface */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	5,	/* Serial Synchronous Controller */
	0,
	0,
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	2,	/* USB Host port */
	3,	/* Ethernet */
	0,	/* Image Sensor Interface */
	5,	/* USART 3 */
	5,	/* USART 4 */
	5,	/* USART 5 */
	0,	/* Timer Counter 3 */
	0,	/* Timer Counter 4 */
	0,	/* Timer Counter 5 */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
};

AT91_SOC_START(at91sam9260)
	.map_io = at91sam9260_map_io,
	.default_irq_priority = at91sam9260_default_irq_priority,
	.ioremap_registers = at91sam9260_ioremap_registers,
	.register_clocks = at91sam9260_register_clocks,
	.init = at91sam9260_initialize,
AT91_SOC_END
