/*
 * arch/arm/mach-at91/at91rm9200_devices.c
 *
 *  Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
 *  Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/platform_device.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/at91rm9200.h>
#include <asm/arch/at91rm9200_mc.h>

#include "generic.h"


/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = 0xffffffffUL;
static struct at91_usbh_data usbh_data;

static struct resource usbh_resources[] = {
	[0] = {
		.start	= AT91RM9200_UHP_BASE,
		.end	= AT91RM9200_UHP_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_UHP,
		.end	= AT91RM9200_ID_UHP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_usbh_device = {
	.name		= "at91_ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &usbh_data,
	},
	.resource	= usbh_resources,
	.num_resources	= ARRAY_SIZE(usbh_resources),
};

void __init at91_add_device_usbh(struct at91_usbh_data *data)
{
	if (!data)
		return;

	usbh_data = *data;
	platform_device_register(&at91rm9200_usbh_device);
}
#else
void __init at91_add_device_usbh(struct at91_usbh_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  USB Device (Gadget)
 * -------------------------------------------------------------------- */

#ifdef CONFIG_USB_GADGET_AT91
static struct at91_udc_data udc_data;

static struct resource udc_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_UDP,
		.end	= AT91RM9200_BASE_UDP + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_UDP,
		.end	= AT91RM9200_ID_UDP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_udc_device = {
	.name		= "at91_udc",
	.id		= -1,
	.dev		= {
				.platform_data		= &udc_data,
	},
	.resource	= udc_resources,
	.num_resources	= ARRAY_SIZE(udc_resources),
};

void __init at91_add_device_udc(struct at91_udc_data *data)
{
	if (!data)
		return;

	if (data->vbus_pin) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
	}
	if (data->pullup_pin)
		at91_set_gpio_output(data->pullup_pin, 0);

	udc_data = *data;
	platform_device_register(&at91rm9200_udc_device);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ARM_AT91_ETHER) || defined(CONFIG_ARM_AT91_ETHER_MODULE)
static u64 eth_dmamask = 0xffffffffUL;
static struct at91_eth_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT91_VA_BASE_EMAC,
		.end	= AT91_VA_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_EMAC,
		.end	= AT91RM9200_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_eth_device = {
	.name		= "at91_ether",
	.id		= -1,
	.dev		= {
				.dma_mask		= &eth_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &eth_data,
	},
	.resource	= eth_resources,
	.num_resources	= ARRAY_SIZE(eth_resources),
};

void __init at91_add_device_eth(struct at91_eth_data *data)
{
	if (!data)
		return;

	if (data->phy_irq_pin) {
		at91_set_gpio_input(data->phy_irq_pin, 0);
		at91_set_deglitch(data->phy_irq_pin, 1);
	}

	/* Pins used for MII and RMII */
	at91_set_A_periph(AT91_PIN_PA16, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PA15, 0);	/* EMDC */
	at91_set_A_periph(AT91_PIN_PA14, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PA13, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PA12, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PA11, 0);	/* ECRS_ECRSDV */
	at91_set_A_periph(AT91_PIN_PA10, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PA9, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PA8, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PA7, 0);	/* ETXCK_EREFCK */

	if (!data->is_rmii) {
		at91_set_B_periph(AT91_PIN_PB19, 0);	/* ERXCK */
		at91_set_B_periph(AT91_PIN_PB18, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PB17, 0);	/* ERXDV */
		at91_set_B_periph(AT91_PIN_PB16, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PB15, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PB14, 0);	/* ETXER */
		at91_set_B_periph(AT91_PIN_PB13, 0);	/* ETX3 */
		at91_set_B_periph(AT91_PIN_PB12, 0);	/* ETX2 */
	}

	eth_data = *data;
	platform_device_register(&at91rm9200_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Compact Flash / PCMCIA
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91_CF) || defined(CONFIG_AT91_CF_MODULE)
static struct at91_cf_data cf_data;

#define CF_BASE		AT91_CHIPSELECT_4

static struct resource cf_resources[] = {
	[0] = {
		.start	= CF_BASE,
		/* ties up CS4, CS5 and CS6 */
		.end	= CF_BASE + (0x30000000 - 1),
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8AND16BIT,
	},
};

static struct platform_device at91rm9200_cf_device = {
	.name		= "at91_cf",
	.id		= -1,
	.dev		= {
				.platform_data		= &cf_data,
	},
	.resource	= cf_resources,
	.num_resources	= ARRAY_SIZE(cf_resources),
};

void __init at91_add_device_cf(struct at91_cf_data *data)
{
	unsigned int csa;

	if (!data)
		return;

	data->chipselect = 4;		/* can only use EBI ChipSelect 4 */

	/* CF takes over CS4, CS5, CS6 */
	csa = at91_sys_read(AT91_EBI_CSA);
	at91_sys_write(AT91_EBI_CSA, csa | AT91_EBI_CS4A_SMC_COMPACTFLASH);

	/*
	 * Static memory controller timing adjustments.
	 * REVISIT:  these timings are in terms of MCK cycles, so
	 * when MCK changes (cpufreq etc) so must these values...
	 */
	at91_sys_write(AT91_SMC_CSR(4),
				  AT91_SMC_ACSS_STD
				| AT91_SMC_DBW_16
				| AT91_SMC_BAT
				| AT91_SMC_WSEN
				| AT91_SMC_NWS_(32)	/* wait states */
				| AT91_SMC_RWSETUP_(6)	/* setup time */
				| AT91_SMC_RWHOLD_(4)	/* hold time */
	);

	/* input/irq */
	if (data->irq_pin) {
		at91_set_gpio_input(data->irq_pin, 1);
		at91_set_deglitch(data->irq_pin, 1);
	}
	at91_set_gpio_input(data->det_pin, 1);
	at91_set_deglitch(data->det_pin, 1);

	/* outputs, initially off */
	if (data->vcc_pin)
		at91_set_gpio_output(data->vcc_pin, 0);
	at91_set_gpio_output(data->rst_pin, 0);

	/* force poweron defaults for these pins ... */
	at91_set_A_periph(AT91_PIN_PC9, 0);	/* A25/CFRNW */
	at91_set_A_periph(AT91_PIN_PC10, 0);	/* NCS4/CFCS */
	at91_set_A_periph(AT91_PIN_PC11, 0);	/* NCS5/CFCE1 */
	at91_set_A_periph(AT91_PIN_PC12, 0);	/* NCS6/CFCE2 */

	/* nWAIT is _not_ a default setting */
	at91_set_A_periph(AT91_PIN_PC6, 1);	/* nWAIT */

	cf_data = *data;
	platform_device_register(&at91rm9200_cf_device);
}
#else
void __init at91_add_device_cf(struct at91_cf_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91) || defined(CONFIG_MMC_AT91_MODULE)
static u64 mmc_dmamask = 0xffffffffUL;
static struct at91_mmc_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_MCI,
		.end	= AT91RM9200_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_MCI,
		.end	= AT91RM9200_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_mmc_device = {
	.name		= "at91_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &mmc_data,
	},
	.resource	= mmc_resources,
	.num_resources	= ARRAY_SIZE(mmc_resources),
};

void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data)
{
	if (!data)
		return;

	/* input/irq */
	if (data->det_pin) {
		at91_set_gpio_input(data->det_pin, 1);
		at91_set_deglitch(data->det_pin, 1);
	}
	if (data->wp_pin)
		at91_set_gpio_input(data->wp_pin, 1);
	if (data->vcc_pin)
		at91_set_gpio_output(data->vcc_pin, 0);

	/* CLK */
	at91_set_A_periph(AT91_PIN_PA27, 0);

	if (data->slot_b) {
		/* CMD */
		at91_set_B_periph(AT91_PIN_PA8, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_B_periph(AT91_PIN_PA9, 1);
		if (data->wire4) {
			at91_set_B_periph(AT91_PIN_PA10, 1);
			at91_set_B_periph(AT91_PIN_PA11, 1);
			at91_set_B_periph(AT91_PIN_PA12, 1);
		}
	} else {
		/* CMD */
		at91_set_A_periph(AT91_PIN_PA28, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA29, 1);
		if (data->wire4) {
			at91_set_B_periph(AT91_PIN_PB3, 1);
			at91_set_B_periph(AT91_PIN_PB4, 1);
			at91_set_B_periph(AT91_PIN_PB5, 1);
		}
	}

	mmc_data = *data;
	platform_device_register(&at91rm9200_mmc_device);
}
#else
void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  NAND / SmartMedia
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MTD_NAND_AT91) || defined(CONFIG_MTD_NAND_AT91_MODULE)
static struct at91_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	{
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_8M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91rm9200_nand_device = {
	.name		= "at91_nand",
	.id		= -1,
	.dev		= {
				.platform_data	= &nand_data,
	},
	.resource	= nand_resources,
	.num_resources	= ARRAY_SIZE(nand_resources),
};

void __init at91_add_device_nand(struct at91_nand_data *data)
{
	unsigned int csa;

	if (!data)
		return;

	/* enable the address range of CS3 */
	csa = at91_sys_read(AT91_EBI_CSA);
	at91_sys_write(AT91_EBI_CSA, csa | AT91_EBI_CS3A_SMC_SMARTMEDIA);

	/* set the bus interface characteristics */
	at91_sys_write(AT91_SMC_CSR(3), AT91_SMC_ACSS_STD | AT91_SMC_DBW_8 | AT91_SMC_WSEN
		| AT91_SMC_NWS_(5)
		| AT91_SMC_TDF_(1)
		| AT91_SMC_RWSETUP_(0)	/* tDS Data Set up Time 30 - ns */
		| AT91_SMC_RWHOLD_(1)	/* tDH Data Hold Time 20 - ns */
	);

	/* enable pin */
	if (data->enable_pin)
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (data->rdy_pin)
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (data->det_pin)
		at91_set_gpio_input(data->det_pin, 1);

	at91_set_A_periph(AT91_PIN_PC1, 0);		/* SMOE */
	at91_set_A_periph(AT91_PIN_PC3, 0);		/* SMWE */

	nand_data = *data;
	platform_device_register(&at91rm9200_nand_device);
}
#else
void __init at91_add_device_nand(struct at91_nand_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  TWI (i2c)
 * -------------------------------------------------------------------- */

#if defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)

static struct resource twi_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_TWI,
		.end	= AT91RM9200_BASE_TWI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_TWI,
		.end	= AT91RM9200_ID_TWI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_twi_device = {
	.name		= "at91_i2c",
	.id		= -1,
	.resource	= twi_resources,
	.num_resources	= ARRAY_SIZE(twi_resources),
};

void __init at91_add_device_i2c(void)
{
	/* pins used for TWI interface */
	at91_set_A_periph(AT91_PIN_PA25, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PA25, 1);

	at91_set_A_periph(AT91_PIN_PA26, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PA26, 1);

	platform_device_register(&at91rm9200_twi_device);
}
#else
void __init at91_add_device_i2c(void) {}
#endif


/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SPI_AT91) || defined(CONFIG_SPI_AT91_MODULE) || defined(CONFIG_AT91_SPI) || defined(CONFIG_AT91_SPI_MODULE)
static u64 spi_dmamask = 0xffffffffUL;

static struct resource spi_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_SPI,
		.end	= AT91RM9200_BASE_SPI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_SPI,
		.end	= AT91RM9200_ID_SPI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_spi_device = {
	.name		= "at91_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= 0xffffffff,
	},
	.resource	= spi_resources,
	.num_resources	= ARRAY_SIZE(spi_resources),
};

static const unsigned spi_standard_cs[4] = { AT91_PIN_PA3, AT91_PIN_PA4, AT91_PIN_PA5, AT91_PIN_PA6 };

void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;

	at91_set_A_periph(AT91_PIN_PA0, 0);	/* MISO */
	at91_set_A_periph(AT91_PIN_PA1, 0);	/* MOSI */
	at91_set_A_periph(AT91_PIN_PA2, 0);	/* SPCK */

	/* Enable SPI chip-selects */
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else
			cs_pin = spi_standard_cs[devices[i].chip_select];

#ifdef CONFIG_SPI_AT91_MANUAL_CS
		at91_set_gpio_output(cs_pin, 1);
#else
		at91_set_A_periph(cs_pin, 0);
#endif

		/* pass chip-select pin to driver */
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);
	at91_clock_associate("spi_clk", &at91rm9200_spi_device.dev, "spi");
	platform_device_register(&at91rm9200_spi_device);
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  RTC
 * -------------------------------------------------------------------- */

#if defined(CONFIG_RTC_DRV_AT91RM9200) || defined(CONFIG_RTC_DRV_AT91RM9200_MODULE)
static struct platform_device at91rm9200_rtc_device = {
	.name		= "at91_rtc",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_rtc(void)
{
	platform_device_register(&at91rm9200_rtc_device);
}
#else
static void __init at91_add_device_rtc(void) {}
#endif


/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91RM9200_WATCHDOG) || defined(CONFIG_AT91RM9200_WATCHDOG_MODULE)
static struct platform_device at91rm9200_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91rm9200_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  LEDs
 * -------------------------------------------------------------------- */

#if defined(CONFIG_LEDS)
u8 at91_leds_cpu;
u8 at91_leds_timer;

void __init at91_init_leds(u8 cpu_led, u8 timer_led)
{
	/* Enable GPIO to access the LEDs */
	at91_set_gpio_output(cpu_led, 1);
	at91_set_gpio_output(timer_led, 1);

	at91_leds_cpu	= cpu_led;
	at91_leds_timer	= timer_led;
}
#else
void __init at91_init_leds(u8 cpu_led, u8 timer_led) {}
#endif


/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91_VA_BASE_SYS + AT91_DBGU,
		.end	= AT91_VA_BASE_SYS + AT91_DBGU + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_SYS,
		.end	= AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx	= 0,
	.use_dma_rx	= 0,		/* DBGU not capable of receive DMA */
	.regs		= (void __iomem *)(AT91_VA_BASE_SYS + AT91_DBGU),
};

static struct platform_device at91rm9200_dbgu_device = {
	.name		= "atmel_usart",
	.id		= 0,
	.dev		= {
				.platform_data	= &dbgu_data,
				.coherent_dma_mask = 0xffffffff,
	},
	.resource	= dbgu_resources,
	.num_resources	= ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PA30, 0);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PA31, 1);		/* DTXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_US0,
		.end	= AT91RM9200_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_US0,
		.end	= AT91RM9200_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91rm9200_uart0_device = {
	.name		= "atmel_usart",
	.id		= 1,
	.dev		= {
				.platform_data	= &uart0_data,
				.coherent_dma_mask = 0xffffffff,
	},
	.resource	= uart0_resources,
	.num_resources	= ARRAY_SIZE(uart0_resources),
};

static inline void configure_usart0_pins(void)
{
	at91_set_A_periph(AT91_PIN_PA17, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA18, 0);		/* RXD0 */
	at91_set_A_periph(AT91_PIN_PA20, 0);		/* CTS0 */

	/*
	 * AT91RM9200 Errata #39 - RTS0 is not internally connected to PA21.
	 *  We need to drive the pin manually.  Default is off (RTS is active low).
	 */
	at91_set_gpio_output(AT91_PIN_PA21, 1);
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_US1,
		.end	= AT91RM9200_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_US1,
		.end	= AT91RM9200_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91rm9200_uart1_device = {
	.name		= "atmel_usart",
	.id		= 2,
	.dev		= {
				.platform_data	= &uart1_data,
				.coherent_dma_mask = 0xffffffff,
	},
	.resource	= uart1_resources,
	.num_resources	= ARRAY_SIZE(uart1_resources),
};

static inline void configure_usart1_pins(void)
{
	at91_set_A_periph(AT91_PIN_PB18, 0);		/* RI1 */
	at91_set_A_periph(AT91_PIN_PB19, 0);		/* DTR1 */
	at91_set_A_periph(AT91_PIN_PB20, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PB21, 0);		/* RXD1 */
	at91_set_A_periph(AT91_PIN_PB23, 0);		/* DCD1 */
	at91_set_A_periph(AT91_PIN_PB24, 0);		/* CTS1 */
	at91_set_A_periph(AT91_PIN_PB25, 0);		/* DSR1 */
	at91_set_A_periph(AT91_PIN_PB26, 0);		/* RTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_US2,
		.end	= AT91RM9200_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_US2,
		.end	= AT91RM9200_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91rm9200_uart2_device = {
	.name		= "atmel_usart",
	.id		= 3,
	.dev		= {
				.platform_data	= &uart2_data,
				.coherent_dma_mask = 0xffffffff,
	},
	.resource	= uart2_resources,
	.num_resources	= ARRAY_SIZE(uart2_resources),
};

static inline void configure_usart2_pins(void)
{
	at91_set_A_periph(AT91_PIN_PA22, 0);		/* RXD2 */
	at91_set_A_periph(AT91_PIN_PA23, 1);		/* TXD2 */
}

static struct resource uart3_resources[] = {
	[0] = {
		.start	= AT91RM9200_BASE_US3,
		.end	= AT91RM9200_BASE_US3 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91RM9200_ID_US3,
		.end	= AT91RM9200_ID_US3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91rm9200_uart3_device = {
	.name		= "atmel_usart",
	.id		= 4,
	.dev		= {
				.platform_data	= &uart3_data,
				.coherent_dma_mask = 0xffffffff,
	},
	.resource	= uart3_resources,
	.num_resources	= ARRAY_SIZE(uart3_resources),
};

static inline void configure_usart3_pins(void)
{
	at91_set_B_periph(AT91_PIN_PA5, 1);		/* TXD3 */
	at91_set_B_periph(AT91_PIN_PA6, 0);		/* RXD3 */
}

struct platform_device *at91_uarts[ATMEL_MAX_UART];	/* the UARTs to use */
struct platform_device *atmel_default_console_device;	/* the serial console device */

void __init at91_init_serial(struct at91_uart_config *config)
{
	int i;

	/* Fill in list of supported UARTs */
	for (i = 0; i < config->nr_tty; i++) {
		switch (config->tty_map[i]) {
			case 0:
				configure_usart0_pins();
				at91_uarts[i] = &at91rm9200_uart0_device;
				at91_clock_associate("usart0_clk", &at91rm9200_uart0_device.dev, "usart");
				break;
			case 1:
				configure_usart1_pins();
				at91_uarts[i] = &at91rm9200_uart1_device;
				at91_clock_associate("usart1_clk", &at91rm9200_uart1_device.dev, "usart");
				break;
			case 2:
				configure_usart2_pins();
				at91_uarts[i] = &at91rm9200_uart2_device;
				at91_clock_associate("usart2_clk", &at91rm9200_uart2_device.dev, "usart");
				break;
			case 3:
				configure_usart3_pins();
				at91_uarts[i] = &at91rm9200_uart3_device;
				at91_clock_associate("usart3_clk", &at91rm9200_uart3_device.dev, "usart");
				break;
			case 4:
				configure_dbgu_pins();
				at91_uarts[i] = &at91rm9200_dbgu_device;
				at91_clock_associate("mck", &at91rm9200_dbgu_device.dev, "usart");
				break;
			default:
				continue;
		}
		at91_uarts[i]->id = i;		/* update ID number to mapped ID */
	}

	/* Set serial console device */
	if (config->console_tty < ATMEL_MAX_UART)
		atmel_default_console_device = at91_uarts[config->console_tty];
	if (!atmel_default_console_device)
		printk(KERN_INFO "AT91: No default serial console defined.\n");
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}
}
#else
void __init at91_init_serial(struct at91_uart_config *config) {}
void __init at91_add_device_serial(void) {}
#endif


/* -------------------------------------------------------------------- */

/*
 * These devices are always present and don't need any board-specific
 * setup.
 */
static int __init at91_add_standard_devices(void)
{
	at91_add_device_rtc();
	at91_add_device_watchdog();
	return 0;
}

arch_initcall(at91_add_standard_devices);
