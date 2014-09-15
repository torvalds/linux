/*
 * arch/arm/mach-at91/at91sam9261.c
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
#include <linux/platform_device.h>
#include <linux/clk/at91_pmc.h>

#include <asm/proc-fns.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/cpu.h>
#include <mach/at91sam9261.h>
#include <mach/hardware.h>

#include "at91_aic.h"
#include "soc.h"
#include "generic.h"
#include "sam9_smc.h"
#include "pm.h"

#if defined(CONFIG_OLD_CLK_AT91)
#include "clock.h"

/* --------------------------------------------------------------------
 *  Clocks
 * -------------------------------------------------------------------- */

/*
 * The peripheral clocks.
 */
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udc_clk = {
	.name		= "udc_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi_clk = {
	.name		= "twi_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_TWI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc2_clk = {
	.name		= "ssc2_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_SSC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc0_clk = {
	.name		= "tc0_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_TC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc1_clk = {
	.name		= "tc1_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_TC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc2_clk = {
	.name		= "tc2_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_TC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9261_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
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

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&mmc_clk,
	&udc_clk,
	&twi_clk,
	&spi0_clk,
	&spi1_clk,
	&ssc0_clk,
	&ssc1_clk,
	&ssc2_clk,
	&tc0_clk,
	&tc1_clk,
	&tc2_clk,
	&ohci_clk,
	&lcdc_clk,
	// irq0 .. irq2
};

static struct clk_lookup periph_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("hclk", "at91sam9261-lcdfb.0", &hck1),
	CLKDEV_CON_DEV_ID("hclk", "at91sam9g10-lcdfb.0", &hck1),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.0", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "atmel_spi.1", &spi1_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.0", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.0", &tc2_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.0", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.1", &ssc1_clk),
	CLKDEV_CON_DEV_ID("pclk", "at91rm9200_ssc.2", &ssc2_clk),
	CLKDEV_CON_DEV_ID("pclk", "fffbc000.ssc", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "fffc0000.ssc", &ssc1_clk),
	CLKDEV_CON_DEV_ID("pclk", "fffc4000.ssc", &ssc2_clk),
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &hck0),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9261.0", &twi_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9g10.0", &twi_clk),
	CLKDEV_CON_ID("pioA", &pioA_clk),
	CLKDEV_CON_ID("pioB", &pioB_clk),
	CLKDEV_CON_ID("pioC", &pioC_clk),
	/* more lookup table for DT entries */
	CLKDEV_CON_DEV_ID("usart", "fffff200.serial", &mck),
	CLKDEV_CON_DEV_ID("usart", "fffb0000.serial", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "ffffb400.serial", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "fff94000.serial", &usart2_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "fffa0000.timer", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "fffa0000.timer", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "fffa0000.timer", &tc2_clk),
	CLKDEV_CON_DEV_ID("hclk", "500000.ohci", &hck0),
	CLKDEV_CON_DEV_ID("hclk", "600000.fb", &hck1),
	CLKDEV_CON_DEV_ID("spi_clk", "fffc8000.spi", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "fffcc000.spi", &spi1_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "fffa8000.mmc", &mmc_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffac000.i2c", &twi_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff400.gpio", &pioA_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff600.gpio", &pioB_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff800.gpio", &pioC_clk),
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

static void __init at91sam9261_register_clocks(void)
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

	clk_register(&hck0);
	clk_register(&hck1);
}
#else
#define at91sam9261_register_clocks NULL
#endif
/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank at91sam9261_gpio[] __initdata = {
	{
		.id		= AT91SAM9261_ID_PIOA,
		.regbase	= AT91SAM9261_BASE_PIOA,
	}, {
		.id		= AT91SAM9261_ID_PIOB,
		.regbase	= AT91SAM9261_BASE_PIOB,
	}, {
		.id		= AT91SAM9261_ID_PIOC,
		.regbase	= AT91SAM9261_BASE_PIOC,
	}
};

/* --------------------------------------------------------------------
 *  AT91SAM9261 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9261_map_io(void)
{
	if (cpu_is_at91sam9g10())
		at91_init_sram(0, AT91SAM9G10_SRAM_BASE, AT91SAM9G10_SRAM_SIZE);
	else
		at91_init_sram(0, AT91SAM9261_SRAM_BASE, AT91SAM9261_SRAM_SIZE);
}

static void __init at91sam9261_ioremap_registers(void)
{
	at91_ioremap_ramc(0, AT91SAM9261_BASE_SDRAMC, 512);
	at91sam926x_ioremap_pit(AT91SAM9261_BASE_PIT);
	at91sam9_ioremap_smc(0, AT91SAM9261_BASE_SMC);
	at91_ioremap_matrix(AT91SAM9261_BASE_MATRIX);
	at91_pm_set_standby(at91sam9_sdram_standby);
}

static void __init at91sam9261_initialize(void)
{
	arm_pm_idle = at91sam9_idle;

	at91_sysirq_mask_rtt(AT91SAM9261_BASE_RTT);

	/* Register GPIO subsystem */
	at91_gpio_init(at91sam9261_gpio, 3);
}

static struct resource rstc_resources[] = {
	[0] = {
		.start  = AT91SAM9261_BASE_RSTC,
		.end    = AT91SAM9261_BASE_RSTC + SZ_16 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = AT91SAM9261_BASE_SDRAMC,
		.end    = AT91SAM9261_BASE_SDRAMC + SZ_512 - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device rstc_device = {
	.name           = "at91-sam9260-reset",
	.resource       = rstc_resources,
	.num_resources  = ARRAY_SIZE(rstc_resources),
};

static struct resource shdwc_resources[] = {
	[0] = {
		.start  = AT91SAM9261_BASE_SHDWC,
		.end    = AT91SAM9261_BASE_SHDWC + SZ_16 - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device shdwc_device = {
	.name           = "at91-poweroff",
	.resource       = shdwc_resources,
	.num_resources  = ARRAY_SIZE(shdwc_resources),
};

static void __init at91sam9261_register_devices(void)
{
	platform_device_register(&rstc_device);
	platform_device_register(&shdwc_device);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9261_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C */
	0,
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	0,	/* Multimedia Card Interface */
	2,	/* USB Device Port */
	6,	/* Two-Wire Interface */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	4,	/* Serial Synchronous Controller 0 */
	4,	/* Serial Synchronous Controller 1 */
	4,	/* Serial Synchronous Controller 2 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	2,	/* USB Host port */
	3,	/* LCD Controller */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
};

static void __init at91sam9261_init_time(void)
{
	at91sam926x_pit_init(NR_IRQS_LEGACY + AT91_ID_SYS);
}

AT91_SOC_START(at91sam9261)
	.map_io = at91sam9261_map_io,
	.default_irq_priority = at91sam9261_default_irq_priority,
	.extern_irq = (1 << AT91SAM9261_ID_IRQ0) | (1 << AT91SAM9261_ID_IRQ1)
		    | (1 << AT91SAM9261_ID_IRQ2),
	.ioremap_registers = at91sam9261_ioremap_registers,
	.register_clocks = at91sam9261_register_clocks,
	.register_devices = at91sam9261_register_devices,
	.init = at91sam9261_initialize,
	.init_time = at91sam9261_init_time,
AT91_SOC_END
