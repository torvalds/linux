/*
 * SoC specific setup code for the AT91SAM9N12
 *
 * Copyright (C) 2012 Atmel Corporation.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/clk/at91_pmc.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/at91sam9n12.h>
#include <mach/cpu.h>

#include "board.h"
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
static struct clk pioAB_clk = {
	.name		= "pioAB_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_PIOAB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioCD_clk = {
	.name		= "pioCD_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_PIOCD,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_USART0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_USART1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_USART2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_USART3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi0_clk = {
	.name		= "twi0_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_TWI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi1_clk = {
	.name		= "twi1_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_TWI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uart0_clk = {
	.name		= "uart0_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_UART0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uart1_clk = {
	.name		= "uart1_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_UART1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tcb_clk = {
	.name		= "tcb_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_TCB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pwm_clk = {
	.name		= "pwm_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_PWM,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk adc_clk = {
	.name		= "adc_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_ADC,
	.type	= CLK_TYPE_PERIPHERAL,
};
static struct clk dma_clk = {
	.name		= "dma_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_DMA,
	.type	= CLK_TYPE_PERIPHERAL,
};
static struct clk uhp_clk = {
	.name		= "uhp",
	.pmc_mask	= 1 << AT91SAM9N12_ID_UHP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udp_clk = {
	.name		= "udp_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_UDP,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc_clk = {
	.name		= "ssc_clk",
	.pmc_mask	= 1 << AT91SAM9N12_ID_SSC,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioAB_clk,
	&pioCD_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&usart3_clk,
	&twi0_clk,
	&twi1_clk,
	&mmc_clk,
	&spi0_clk,
	&spi1_clk,
	&lcdc_clk,
	&uart0_clk,
	&uart1_clk,
	&tcb_clk,
	&pwm_clk,
	&adc_clk,
	&dma_clk,
	&uhp_clk,
	&udp_clk,
	&ssc_clk,
};

static struct clk_lookup periph_clocks_lookups[] = {
	/* lookup table for DT entries */
	CLKDEV_CON_DEV_ID("usart", "fffff200.serial", &mck),
	CLKDEV_CON_DEV_ID("usart", "f801c000.serial", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "f8020000.serial", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "f8024000.serial", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "f8028000.serial", &usart3_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "f8008000.timer", &tcb_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "f800c000.timer", &tcb_clk),
	CLKDEV_CON_DEV_ID("mci_clk", "f0008000.mmc", &mmc_clk),
	CLKDEV_CON_DEV_ID(NULL, "f0010000.ssc", &ssc_clk),
	CLKDEV_CON_DEV_ID("dma_clk", "ffffec00.dma-controller", &dma_clk),
	CLKDEV_CON_DEV_ID(NULL, "f8010000.i2c", &twi0_clk),
	CLKDEV_CON_DEV_ID(NULL, "f8014000.i2c", &twi1_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "f0000000.spi", &spi0_clk),
	CLKDEV_CON_DEV_ID("spi_clk", "f0004000.spi", &spi1_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff400.gpio", &pioAB_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff600.gpio", &pioAB_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffff800.gpio", &pioCD_clk),
	CLKDEV_CON_DEV_ID(NULL, "fffffa00.gpio", &pioCD_clk),
	/* additional fake clock for macb_hclk */
	CLKDEV_CON_DEV_ID("hclk", "500000.ohci", &uhp_clk),
	CLKDEV_CON_DEV_ID("ohci_clk", "500000.ohci", &uhp_clk),
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

static void __init at91sam9n12_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);
	clk_register(&pck0);
	clk_register(&pck1);

	clkdev_add_table(periph_clocks_lookups,
			 ARRAY_SIZE(periph_clocks_lookups));

}

/* --------------------------------------------------------------------
 *  AT91SAM9N12 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9n12_map_io(void)
{
	at91_init_sram(0, AT91SAM9N12_SRAM_BASE, AT91SAM9N12_SRAM_SIZE);
}

static void __init at91sam9n12_initialize(void)
{
	at91_sysirq_mask_rtc(AT91SAM9N12_BASE_RTC);
}

AT91_SOC_START(at91sam9n12)
	.map_io = at91sam9n12_map_io,
	.register_clocks = at91sam9n12_register_clocks,
	.init = at91sam9n12_initialize,
AT91_SOC_END
