/*
 * arch/arm/mach-at91/at91sam9263_devices.c
 *
 *  Copyright (C) 2007 Atmel Corporation.
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
#include <asm/arch/at91sam9263.h>
#include <asm/arch/at91sam926x_mc.h>
#include <asm/arch/at91sam9263_matrix.h>

#include "generic.h"

#define SZ_512	0x00000200
#define SZ_256	0x00000100
#define SZ_16	0x00000010

/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = 0xffffffffUL;
static struct at91_usbh_data usbh_data;

static struct resource usbh_resources[] = {
	[0] = {
		.start	= AT91SAM9263_UHP_BASE,
		.end	= AT91SAM9263_UHP_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_UHP,
		.end	= AT91SAM9263_ID_UHP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_device = {
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
	int i;

	if (!data)
		return;

	/* Enable VBus control for UHP ports */
	for (i = 0; i < data->ports; i++) {
		if (data->vbus_pin[i])
			at91_set_gpio_output(data->vbus_pin[i], 0);
	}

	usbh_data = *data;
	platform_device_register(&at91_usbh_device);
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
		.start	= AT91SAM9263_BASE_UDP,
		.end	= AT91SAM9263_BASE_UDP + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_UDP,
		.end	= AT91SAM9263_ID_UDP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_udc_device = {
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

	/* Pullup pin is handled internally by USB device peripheral */

	udc_data = *data;
	platform_device_register(&at91_udc_device);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MACB) || defined(CONFIG_MACB_MODULE)
static u64 eth_dmamask = 0xffffffffUL;
static struct at91_eth_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_EMAC,
		.end	= AT91SAM9263_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_EMAC,
		.end	= AT91SAM9263_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_eth_device = {
	.name		= "macb",
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
	at91_set_A_periph(AT91_PIN_PE21, 0);	/* ETXCK_EREFCK */
	at91_set_B_periph(AT91_PIN_PC25, 0);	/* ERXDV */
	at91_set_A_periph(AT91_PIN_PE25, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PE26, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PE27, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PE28, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PE23, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PE24, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PE30, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PE29, 0);	/* EMDC */

	if (!data->is_rmii) {
		at91_set_A_periph(AT91_PIN_PE22, 0);	/* ECRS */
		at91_set_B_periph(AT91_PIN_PC26, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PC22, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PC23, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PC27, 0);	/* ERXCK */
		at91_set_B_periph(AT91_PIN_PC20, 0);	/* ETX2 */
		at91_set_B_periph(AT91_PIN_PC21, 0);	/* ETX3 */
		at91_set_B_periph(AT91_PIN_PC24, 0);	/* ETXER */
	}

	eth_data = *data;
	platform_device_register(&at91sam9263_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91) || defined(CONFIG_MMC_AT91_MODULE)
static u64 mmc_dmamask = 0xffffffffUL;
static struct at91_mmc_data mmc0_data, mmc1_data;

static struct resource mmc0_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_MCI0,
		.end	= AT91SAM9263_BASE_MCI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_MCI0,
		.end	= AT91SAM9263_ID_MCI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_mmc0_device = {
	.name		= "at91_mci",
	.id		= 0,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &mmc0_data,
	},
	.resource	= mmc0_resources,
	.num_resources	= ARRAY_SIZE(mmc0_resources),
};

static struct resource mmc1_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_MCI1,
		.end	= AT91SAM9263_BASE_MCI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_MCI1,
		.end	= AT91SAM9263_ID_MCI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_mmc1_device = {
	.name		= "at91_mci",
	.id		= 1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &mmc1_data,
	},
	.resource	= mmc1_resources,
	.num_resources	= ARRAY_SIZE(mmc1_resources),
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

	if (mmc_id == 0) {		/* MCI0 */
		/* CLK */
		at91_set_A_periph(AT91_PIN_PA12, 0);

		if (data->slot_b) {
			/* CMD */
			at91_set_A_periph(AT91_PIN_PA16, 1);

			/* DAT0, maybe DAT1..DAT3 */
			at91_set_A_periph(AT91_PIN_PA17, 1);
			if (data->wire4) {
				at91_set_A_periph(AT91_PIN_PA18, 1);
				at91_set_A_periph(AT91_PIN_PA19, 1);
				at91_set_A_periph(AT91_PIN_PA20, 1);
			}
		} else {
			/* CMD */
			at91_set_A_periph(AT91_PIN_PA1, 1);

			/* DAT0, maybe DAT1..DAT3 */
			at91_set_A_periph(AT91_PIN_PA0, 1);
			if (data->wire4) {
				at91_set_A_periph(AT91_PIN_PA3, 1);
				at91_set_A_periph(AT91_PIN_PA4, 1);
				at91_set_A_periph(AT91_PIN_PA5, 1);
			}
		}

		mmc0_data = *data;
		at91_clock_associate("mci0_clk", &at91sam9263_mmc1_device.dev, "mci_clk");
		platform_device_register(&at91sam9263_mmc0_device);
	} else {			/* MCI1 */
		/* CLK */
		at91_set_A_periph(AT91_PIN_PA6, 0);

		if (data->slot_b) {
			/* CMD */
			at91_set_A_periph(AT91_PIN_PA21, 1);

			/* DAT0, maybe DAT1..DAT3 */
			at91_set_A_periph(AT91_PIN_PA22, 1);
			if (data->wire4) {
				at91_set_A_periph(AT91_PIN_PA23, 1);
				at91_set_A_periph(AT91_PIN_PA24, 1);
				at91_set_A_periph(AT91_PIN_PA25, 1);
			}
		} else {
			/* CMD */
			at91_set_A_periph(AT91_PIN_PA7, 1);

			/* DAT0, maybe DAT1..DAT3 */
			at91_set_A_periph(AT91_PIN_PA8, 1);
			if (data->wire4) {
				at91_set_A_periph(AT91_PIN_PA9, 1);
				at91_set_A_periph(AT91_PIN_PA10, 1);
				at91_set_A_periph(AT91_PIN_PA11, 1);
			}
		}

		mmc1_data = *data;
		at91_clock_associate("mci1_clk", &at91sam9263_mmc1_device.dev, "mci_clk");
		platform_device_register(&at91sam9263_mmc1_device);
	}
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
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9263_nand_device = {
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
	unsigned long csa, mode;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBI0CSA);
	at91_sys_write(AT91_MATRIX_EBI0CSA, csa | AT91_MATRIX_EBI0_CS3A_SMC);

	/* set the bus interface characteristics */
	at91_sys_write(AT91_SMC_SETUP(3), AT91_SMC_NWESETUP_(0) | AT91_SMC_NCS_WRSETUP_(0)
			| AT91_SMC_NRDSETUP_(0) | AT91_SMC_NCS_RDSETUP_(0));

	at91_sys_write(AT91_SMC_PULSE(3), AT91_SMC_NWEPULSE_(3) | AT91_SMC_NCS_WRPULSE_(3)
			| AT91_SMC_NRDPULSE_(3) | AT91_SMC_NCS_RDPULSE_(3));

	at91_sys_write(AT91_SMC_CYCLE(3), AT91_SMC_NWECYCLE_(5) | AT91_SMC_NRDCYCLE_(5));

	if (data->bus_width_16)
		mode = AT91_SMC_DBW_16;
	else
		mode = AT91_SMC_DBW_8;
	at91_sys_write(AT91_SMC_MODE(3), mode | AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_TDF_(2));

	/* enable pin */
	if (data->enable_pin)
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (data->rdy_pin)
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (data->det_pin)
		at91_set_gpio_input(data->det_pin, 1);

	nand_data = *data;
	platform_device_register(&at91sam9263_nand_device);
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
		.start	= AT91SAM9263_BASE_TWI,
		.end	= AT91SAM9263_BASE_TWI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_TWI,
		.end	= AT91SAM9263_ID_TWI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_twi_device = {
	.name		= "at91_i2c",
	.id		= -1,
	.resource	= twi_resources,
	.num_resources	= ARRAY_SIZE(twi_resources),
};

void __init at91_add_device_i2c(void)
{
	/* pins used for TWI interface */
	at91_set_A_periph(AT91_PIN_PB4, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PB4, 1);

	at91_set_A_periph(AT91_PIN_PB5, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PB5, 1);

	platform_device_register(&at91sam9263_twi_device);
}
#else
void __init at91_add_device_i2c(void) {}
#endif


/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = 0xffffffffUL;

static struct resource spi0_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_SPI0,
		.end	= AT91SAM9263_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_SPI0,
		.end	= AT91SAM9263_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= 0xffffffff,
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PA5, AT91_PIN_PA3, AT91_PIN_PA4, AT91_PIN_PB11 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_SPI1,
		.end	= AT91SAM9263_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_SPI1,
		.end	= AT91SAM9263_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9263_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= 0xffffffff,
	},
	.resource	= spi1_resources,
	.num_resources	= ARRAY_SIZE(spi1_resources),
};

static const unsigned spi1_standard_cs[4] = { AT91_PIN_PB15, AT91_PIN_PB16, AT91_PIN_PB17, AT91_PIN_PB18 };

void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;
	short enable_spi0 = 0;
	short enable_spi1 = 0;

	/* Choose SPI chip-selects */
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else if (devices[i].bus_num == 0)
			cs_pin = spi0_standard_cs[devices[i].chip_select];
		else
			cs_pin = spi1_standard_cs[devices[i].chip_select];

		if (devices[i].bus_num == 0)
			enable_spi0 = 1;
		else
			enable_spi1 = 1;

		/* enable chip-select pin */
		at91_set_gpio_output(cs_pin, 1);

		/* pass chip-select pin to driver */
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);

	/* Configure SPI bus(es) */
	if (enable_spi0) {
		at91_set_B_periph(AT91_PIN_PA0, 0);	/* SPI0_MISO */
		at91_set_B_periph(AT91_PIN_PA1, 0);	/* SPI0_MOSI */
		at91_set_B_periph(AT91_PIN_PA2, 0);	/* SPI0_SPCK */

		at91_clock_associate("spi0_clk", &at91sam9263_spi0_device.dev, "spi_clk");
		platform_device_register(&at91sam9263_spi0_device);
	}
	if (enable_spi1) {
		at91_set_A_periph(AT91_PIN_PB12, 0);	/* SPI1_MISO */
		at91_set_A_periph(AT91_PIN_PB13, 0);	/* SPI1_MOSI */
		at91_set_A_periph(AT91_PIN_PB14, 0);	/* SPI1_SPCK */

		at91_clock_associate("spi1_clk", &at91sam9263_spi1_device.dev, "spi_clk");
		platform_device_register(&at91sam9263_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
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

static struct platform_device at91sam9263_dbgu_device = {
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
	at91_set_A_periph(AT91_PIN_PC30, 0);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PC31, 1);		/* DTXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_US0,
		.end	= AT91SAM9263_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_US0,
		.end	= AT91SAM9263_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91sam9263_uart0_device = {
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
	at91_set_A_periph(AT91_PIN_PA26, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA27, 0);		/* RXD0 */
	at91_set_A_periph(AT91_PIN_PA28, 0);		/* RTS0 */
	at91_set_A_periph(AT91_PIN_PA29, 0);		/* CTS0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_US1,
		.end	= AT91SAM9263_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_US1,
		.end	= AT91SAM9263_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91sam9263_uart1_device = {
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
	at91_set_A_periph(AT91_PIN_PD0, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PD1, 0);		/* RXD1 */
	at91_set_B_periph(AT91_PIN_PD7, 0);		/* RTS1 */
	at91_set_B_periph(AT91_PIN_PD8, 0);		/* CTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91SAM9263_BASE_US2,
		.end	= AT91SAM9263_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9263_ID_US2,
		.end	= AT91SAM9263_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static struct platform_device at91sam9263_uart2_device = {
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
	at91_set_A_periph(AT91_PIN_PD2, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PD3, 0);		/* RXD2 */
	at91_set_B_periph(AT91_PIN_PD5, 0);		/* RTS2 */
	at91_set_B_periph(AT91_PIN_PD6, 0);		/* CTS2 */
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
				at91_uarts[i] = &at91sam9263_uart0_device;
				at91_clock_associate("usart0_clk", &at91sam9263_uart0_device.dev, "usart");
				break;
			case 1:
				configure_usart1_pins();
				at91_uarts[i] = &at91sam9263_uart1_device;
				at91_clock_associate("usart1_clk", &at91sam9263_uart1_device.dev, "usart");
				break;
			case 2:
				configure_usart2_pins();
				at91_uarts[i] = &at91sam9263_uart2_device;
				at91_clock_associate("usart2_clk", &at91sam9263_uart2_device.dev, "usart");
				break;
			case 3:
				configure_dbgu_pins();
				at91_uarts[i] = &at91sam9263_dbgu_device;
				at91_clock_associate("mck", &at91sam9263_dbgu_device.dev, "usart");
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
	return 0;
}

arch_initcall(at91_add_standard_devices);
