/*
 * arch/arm/mach-at91/at91rm9200.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/at91rm9200.h>
#include <mach/at91_aic.h>
#include <mach/at91_pmc.h>
#include <mach/at91_st.h>
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
static struct clk udc_clk = {
	.name		= "udc_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ether_clk = {
	.name		= "ether_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_EMAC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi_clk = {
	.name		= "twi_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TWI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_US3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi_clk = {
	.name		= "spi_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_SPI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioD_clk = {
	.name		= "pioD_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_PIOD,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc2_clk = {
	.name		= "ssc2_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_SSC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc0_clk = {
	.name		= "tc0_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc1_clk = {
	.name		= "tc1_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc2_clk = {
	.name		= "tc2_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc3_clk = {
	.name		= "tc3_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc4_clk = {
	.name		= "tc4_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC4,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc5_clk = {
	.name		= "tc5_clk",
	.pmc_mask	= 1 << AT91RM9200_ID_TC5,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&pioD_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&usart3_clk,
	&mmc_clk,
	&udc_clk,
	&twi_clk,
	&spi_clk,
	&ssc0_clk,
	&ssc1_clk,
	&ssc2_clk,
	&tc0_clk,
	&tc1_clk,
	&tc2_clk,
	&tc3_clk,
	&tc4_clk,
	&tc5_clk,
	&ohci_clk,
	&ether_clk,
	// irq0 .. irq6
};

static struct clk_lookup periph_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.0", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.0", &tc2_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.1", &tc3_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.1", &tc4_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.1", &tc5_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.0", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.1", &ssc1_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.2", &ssc2_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91rm9200.0", &twi_clk),
	/* fake hclk clock */
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &ohci_clk),
	CLKDEV_CON_ID("pioA", &pioA_clk),
	CLKDEV_CON_ID("pioB", &pioB_clk),
	CLKDEV_CON_ID("pioC", &pioC_clk),
	CLKDEV_CON_ID("pioD", &pioD_clk),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.1", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.2", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.3", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.4", &usart3_clk),
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

static void __init at91rm9200_register_clocks(void)
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

static struct at91_gpio_bank at91rm9200_gpio[] __initdata = {
	{
		.id		= AT91RM9200_ID_PIOA,
		.regbase	= AT91RM9200_BASE_PIOA,
	}, {
		.id		= AT91RM9200_ID_PIOB,
		.regbase	= AT91RM9200_BASE_PIOB,
	}, {
		.id		= AT91RM9200_ID_PIOC,
		.regbase	= AT91RM9200_BASE_PIOC,
	}, {
		.id		= AT91RM9200_ID_PIOD,
		.regbase	= AT91RM9200_BASE_PIOD,
	}
};

static void at91rm9200_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
	at91_pmc_write(AT91_PMC_SCDR, AT91_PMC_PCK);
}

static void at91rm9200_restart(char mode, const char *cmd)
{
	/*
	 * Perform a hardware reset with the use of the Watchdog timer.
	 */
	at91_st_write(AT91_ST_WDMR, AT91_ST_RSTEN | AT91_ST_EXTEN | 1);
	at91_st_write(AT91_ST_CR, AT91_ST_WDRST);
}

/* --------------------------------------------------------------------
 *  AT91RM9200 processor initialization
 * -------------------------------------------------------------------- */
static void __init at91rm9200_map_io(void)
{
	/* Map peripherals */
	at91_init_sram(0, AT91RM9200_SRAM_BASE, AT91RM9200_SRAM_SIZE);
}

static void __init at91rm9200_ioremap_registers(void)
{
	at91rm9200_ioremap_st(AT91RM9200_BASE_ST);
	at91_ioremap_ramc(0, AT91RM9200_BASE_MC, 256);
}

static void __init at91rm9200_initialize(void)
{
	arm_pm_idle = at91rm9200_idle;
	arm_pm_restart = at91rm9200_restart;
	at91_extern_irq = (1 << AT91RM9200_ID_IRQ0) | (1 << AT91RM9200_ID_IRQ1)
			| (1 << AT91RM9200_ID_IRQ2) | (1 << AT91RM9200_ID_IRQ3)
			| (1 << AT91RM9200_ID_IRQ4) | (1 << AT91RM9200_ID_IRQ5)
			| (1 << AT91RM9200_ID_IRQ6);

	/* Initialize GPIO subsystem */
	at91_gpio_init(at91rm9200_gpio,
		cpu_is_at91rm9200_bga() ? AT91RM9200_BGA : AT91RM9200_PQFP);
}


/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91rm9200_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller (FIQ) */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C */
	1,	/* Parallel IO Controller D */
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	5,	/* USART 3 */
	0,	/* Multimedia Card Interface */
	2,	/* USB Device Port */
	6,	/* Two-Wire Interface */
	5,	/* Serial Peripheral Interface */
	4,	/* Serial Synchronous Controller 0 */
	4,	/* Serial Synchronous Controller 1 */
	4,	/* Serial Synchronous Controller 2 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	0,	/* Timer Counter 3 */
	0,	/* Timer Counter 4 */
	0,	/* Timer Counter 5 */
	2,	/* USB Host port */
	3,	/* Ethernet MAC */
	0,	/* Advanced Interrupt Controller (IRQ0) */
	0,	/* Advanced Interrupt Controller (IRQ1) */
	0,	/* Advanced Interrupt Controller (IRQ2) */
	0,	/* Advanced Interrupt Controller (IRQ3) */
	0,	/* Advanced Interrupt Controller (IRQ4) */
	0,	/* Advanced Interrupt Controller (IRQ5) */
	0	/* Advanced Interrupt Controller (IRQ6) */
};

struct at91_init_soc __initdata at91rm9200_soc = {
	.map_io = at91rm9200_map_io,
	.default_irq_priority = at91rm9200_default_irq_priority,
	.ioremap_registers = at91rm9200_ioremap_registers,
	.register_clocks = at91rm9200_register_clocks,
	.init = at91rm9200_initialize,
};
