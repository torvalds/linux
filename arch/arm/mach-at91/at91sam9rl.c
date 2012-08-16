/*
 * arch/arm/mach-at91/at91sam9rl.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>

#include <asm/proc-fns.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/at91sam9rl.h>
#include <mach/at91_aic.h>
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
	.pmc_mask	= 1 << AT91SAM9RL_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pioD_clk = {
	.name		= "pioD_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_PIOD,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_US0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_US1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_US2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_US3,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_MCI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi0_clk = {
	.name		= "twi0_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TWI0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk twi1_clk = {
	.name		= "twi1_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TWI1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk spi_clk = {
	.name		= "spi_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_SPI,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc0_clk = {
	.name		= "ssc0_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_SSC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ssc1_clk = {
	.name		= "ssc1_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_SSC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc0_clk = {
	.name		= "tc0_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TC0,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc1_clk = {
	.name		= "tc1_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TC1,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tc2_clk = {
	.name		= "tc2_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TC2,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk pwm_clk = {
	.name		= "pwm_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_PWMC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk tsc_clk = {
	.name		= "tsc_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_TSC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk dma_clk = {
	.name		= "dma_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_DMA,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk udphs_clk = {
	.name		= "udphs_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_UDPHS,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk lcdc_clk = {
	.name		= "lcdc_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_LCDC,
	.type		= CLK_TYPE_PERIPHERAL,
};
static struct clk ac97_clk = {
	.name		= "ac97_clk",
	.pmc_mask	= 1 << AT91SAM9RL_ID_AC97C,
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
	&twi0_clk,
	&twi1_clk,
	&spi_clk,
	&ssc0_clk,
	&ssc1_clk,
	&tc0_clk,
	&tc1_clk,
	&tc2_clk,
	&pwm_clk,
	&tsc_clk,
	&dma_clk,
	&udphs_clk,
	&lcdc_clk,
	&ac97_clk,
	// irq0
};

static struct clk_lookup periph_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("hclk", "atmel_usba_udc", &utmi_clk),
	CLKDEV_CON_DEV_ID("pclk", "atmel_usba_udc", &udphs_clk),
	CLKDEV_CON_DEV_ID("t0_clk", "atmel_tcb.0", &tc0_clk),
	CLKDEV_CON_DEV_ID("t1_clk", "atmel_tcb.0", &tc1_clk),
	CLKDEV_CON_DEV_ID("t2_clk", "atmel_tcb.0", &tc2_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.0", &ssc0_clk),
	CLKDEV_CON_DEV_ID("pclk", "ssc.1", &ssc1_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9g20.0", &twi0_clk),
	CLKDEV_CON_DEV_ID(NULL, "i2c-at91sam9g20.1", &twi1_clk),
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

static void __init at91sam9rl_register_clocks(void)
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

static struct at91_gpio_bank at91sam9rl_gpio[] __initdata = {
	{
		.id		= AT91SAM9RL_ID_PIOA,
		.regbase	= AT91SAM9RL_BASE_PIOA,
	}, {
		.id		= AT91SAM9RL_ID_PIOB,
		.regbase	= AT91SAM9RL_BASE_PIOB,
	}, {
		.id		= AT91SAM9RL_ID_PIOC,
		.regbase	= AT91SAM9RL_BASE_PIOC,
	}, {
		.id		= AT91SAM9RL_ID_PIOD,
		.regbase	= AT91SAM9RL_BASE_PIOD,
	}
};

/* --------------------------------------------------------------------
 *  AT91SAM9RL processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9rl_map_io(void)
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

	/* Map SRAM */
	at91_init_sram(0, AT91SAM9RL_SRAM_BASE, sram_size);
}

static void __init at91sam9rl_ioremap_registers(void)
{
	at91_ioremap_shdwc(AT91SAM9RL_BASE_SHDWC);
	at91_ioremap_rstc(AT91SAM9RL_BASE_RSTC);
	at91_ioremap_ramc(0, AT91SAM9RL_BASE_SDRAMC, 512);
	at91sam926x_ioremap_pit(AT91SAM9RL_BASE_PIT);
	at91sam9_ioremap_smc(0, AT91SAM9RL_BASE_SMC);
	at91_ioremap_matrix(AT91SAM9RL_BASE_MATRIX);
}

static void __init at91sam9rl_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
	arm_pm_restart = at91sam9_alt_restart;
	at91_extern_irq = (1 << AT91SAM9RL_ID_IRQ0);

	/* Register GPIO subsystem */
	at91_gpio_init(at91sam9rl_gpio, 4);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91sam9rl_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller */
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
	6,	/* Two-Wire Interface 0 */
	6,	/* Two-Wire Interface 1 */
	5,	/* Serial Peripheral Interface */
	4,	/* Serial Synchronous Controller 0 */
	4,	/* Serial Synchronous Controller 1 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	0,
	0,	/* Touch Screen Controller */
	0,	/* DMA Controller */
	2,	/* USB Device High speed port */
	2,	/* LCD Controller */
	6,	/* AC97 Controller */
	0,
	0,
	0,
	0,
	0,
	0,
	0,	/* Advanced Interrupt Controller */
};

AT91_SOC_START(sam9rl)
	.map_io = at91sam9rl_map_io,
	.default_irq_priority = at91sam9rl_default_irq_priority,
	.ioremap_registers = at91sam9rl_ioremap_registers,
	.register_clocks = at91sam9rl_register_clocks,
	.init = at91sam9rl_initialize,
AT91_SOC_END
