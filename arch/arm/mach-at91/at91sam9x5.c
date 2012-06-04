/*
 *  Chip-specific setup code for the AT91SAM9x5 family
 *
 *  Copyright (C) 2010-2012 Atmel Corporation.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/at91sam9x5.h>
#include <mach/at91_pmc.h>
#include <mach/cpu.h>
#include <mach/board.h>

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
	.pmc_mask	= 1 << AT91SAM9X5_ID_PIOAB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioCD_clk = {
	.name		= "pioCD_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_PIOCD,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk smd_clk = {
	.name		= "smd_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_SMD,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_USART0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_USART1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_USART2,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* USART3 clock - Only for sam9g25/sam9x25 */
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_USART3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi0_clk = {
	.name		= "twi0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_TWI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi1_clk = {
	.name		= "twi1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_TWI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi2_clk = {
	.name		= "twi2_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_TWI2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc0_clk = {
	.name		= "mci0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_MCI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi0_clk = {
	.name		= "spi0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_SPI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi1_clk = {
	.name		= "spi1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_SPI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uart0_clk = {
	.name		= "uart0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_UART0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uart1_clk = {
	.name		= "uart1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_UART1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tcb0_clk = {
	.name		= "tcb0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_TCB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pwm_clk = {
	.name		= "pwm_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_PWM,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk adc_clk = {
	.name		= "adc_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_ADC,
	.type	= CLK_TYPE_PERIPHERAL,
};
static struct clk adc_op_clk = {
	.name		= "adc_op_clk",
	.type		= CLK_TYPE_PERIPHERAL,
	.rate_hz	= 5000000,
};
static struct clk dma0_clk = {
	.name		= "dma0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_DMA0,
	.type	= CLK_TYPE_PERIPHERAL,
};
static struct clk dma1_clk = {
	.name		= "dma1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_DMA1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk uhphs_clk = {
	.name		= "uhphs",
	.pmc_mask	= 1 << AT91SAM9X5_ID_UHPHS,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udphs_clk = {
	.name		= "udphs_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_UDPHS,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* emac0 clock - Only for sam9g25/sam9x25/sam9g35/sam9x35 */
static struct clk macb0_clk = {
	.name		= "pclk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_EMAC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* lcd clock - Only for sam9g15/sam9g35/sam9x35 */
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* isi clock - Only for sam9g25 */
static struct clk isi_clk = {
	.name		= "isi_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_ISI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc1_clk = {
	.name		= "mci1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_MCI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* emac1 clock - Only for sam9x25 */
static struct clk macb1_clk = {
	.name		= "pclk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_EMAC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc_clk = {
	.name		= "ssc_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_SSC,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* can0 clock - Only for sam9x35 */
static struct clk can0_clk = {
	.name		= "can0_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_CAN0,
	.type		= CLK_TYPE_PERIPHERAL,
};
/* can1 clock - Only for sam9x35 */
static struct clk can1_clk = {
	.name		= "can1_clk",
	.pmc_mask	= 1 << AT91SAM9X5_ID_CAN1,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioAB_clk,
	&pioCD_clk,
	&smd_clk,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&twi0_clk,
	&twi1_clk,
	&twi2_clk,
	&mmc0_clk,
	&spi0_clk,
	&spi1_clk,
	&uart0_clk,
	&uart1_clk,
	&tcb0_clk,
	&pwm_clk,
	&adc_clk,
	&adc_op_clk,
	&dma0_clk,
	&dma1_clk,
	&uhphs_clk,
	&udphs_clk,
	&mmc1_clk,
	&ssc_clk,
	// irq0
};

static struct clk_lookup periph_clocks_lookups[] = {
	/* lookup table for DT entries */
	CLKDEV_CON_DEV_ID("usart", "fffff200.serial", &mck),
	CLKDEV_CON_DEV_ID("usart", "f801c000.serial", &usart0_clk),
	CLKDEV_CON_DEV_ID("usart", "f8020000.serial", &usart1_clk),
	CLKDEV_CON_DEV_ID("usart", "f8024000.serial", &usart2_clk),
	CLKDEV_CON_DEV_ID("usart", "f8028000.serial", &usart3_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "f8008000.timer", &tcb0_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "f800c000.timer", &tcb0_clk),
	CLKDEV_CON_DEV_ID("dma_clk", "ffffec00.dma-controller", &dma0_clk),
	CLKDEV_CON_DEV_ID("dma_clk", "ffffee00.dma-controller", &dma1_clk),
	CLKDEV_CON_ID("pioA", &pioAB_clk),
	CLKDEV_CON_ID("pioB", &pioAB_clk),
	CLKDEV_CON_ID("pioC", &pioCD_clk),
	CLKDEV_CON_ID("pioD", &pioCD_clk),
	/* additional fake clock for macb_hclk */
	CLKDEV_CON_DEV_ID("hclk", "f802c000.ethernet", &macb0_clk),
	CLKDEV_CON_DEV_ID("hclk", "f8030000.ethernet", &macb1_clk),
	CLKDEV_CON_DEV_ID("hclk", "600000.ohci", &uhphs_clk),
	CLKDEV_CON_DEV_ID("ohci_clk", "600000.ohci", &uhphs_clk),
	CLKDEV_CON_DEV_ID("ehci_clk", "700000.ehci", &uhphs_clk),
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

static void __init at91sam9x5_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);

	clkdev_add_table(periph_clocks_lookups,
			 ARRAY_SIZE(periph_clocks_lookups));

	if (cpu_is_at91sam9g25()
	|| cpu_is_at91sam9x25())
		clk_register(&usart3_clk);

	if (cpu_is_at91sam9g25()
	|| cpu_is_at91sam9x25()
	|| cpu_is_at91sam9g35()
	|| cpu_is_at91sam9x35())
		clk_register(&macb0_clk);

	if (cpu_is_at91sam9g15()
	|| cpu_is_at91sam9g35()
	|| cpu_is_at91sam9x35())
		clk_register(&lcdc_clk);

	if (cpu_is_at91sam9g25())
		clk_register(&isi_clk);

	if (cpu_is_at91sam9x25())
		clk_register(&macb1_clk);

	if (cpu_is_at91sam9x25()
	|| cpu_is_at91sam9x35()) {
		clk_register(&can0_clk);
		clk_register(&can1_clk);
	}

	clk_register(&pck0);
	clk_register(&pck1);
}

/* --------------------------------------------------------------------
 *  AT91SAM9x5 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9x5_map_io(void)
{
	at91_init_sram(0, AT91SAM9X5_SRAM_BASE, AT91SAM9X5_SRAM_SIZE);
}

void __init at91sam9x5_initialize(void)
{
	at91_extern_irq = (1 << AT91SAM9X5_ID_IRQ0);

	/* Register GPIO subsystem (using DT) */
	at91_gpio_init(NULL, 0);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */
/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9x5_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller (FIQ) */
	7,	/* System Peripherals */
	1,	/* Parallel IO Controller A and B */
	1,	/* Parallel IO Controller C and D */
	4,	/* Soft Modem */
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	5,	/* USART 3 */
	6,	/* Two-Wire Interface 0 */
	6,	/* Two-Wire Interface 1 */
	6,	/* Two-Wire Interface 2 */
	0,	/* Multimedia Card Interface 0 */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	5,	/* UART 0 */
	5,	/* UART 1 */
	0,	/* Timer Counter 0, 1, 2, 3, 4 and 5 */
	0,	/* Pulse Width Modulation Controller */
	0,	/* ADC Controller */
	0,	/* DMA Controller 0 */
	0,	/* DMA Controller 1 */
	2,	/* USB Host High Speed port */
	2,	/* USB Device High speed port */
	3,	/* Ethernet MAC 0 */
	3,	/* LDC Controller or Image Sensor Interface */
	0,	/* Multimedia Card Interface 1 */
	3,	/* Ethernet MAC 1 */
	4,	/* Synchronous Serial Interface */
	4,	/* CAN Controller 0 */
	4,	/* CAN Controller 1 */
	0,	/* Advanced Interrupt Controller (IRQ0) */
};

struct at91_init_soc __initdata at91sam9x5_soc = {
	.map_io = at91sam9x5_map_io,
	.default_irq_priority = at91sam9x5_default_irq_priority,
	.register_clocks = at91sam9x5_register_clocks,
	.init = at91sam9x5_initialize,
};
