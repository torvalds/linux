/*
 * arch/arm/mach-at91/at91sam9260_devices.c
 *
 *  Copyright (C) 2006 Atmel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>

#include <mach/board.h>
#include <mach/cpu.h>
#include <mach/at91sam9260.h>
#include <mach/at91sam9260_matrix.h>
#include <mach/at91sam9_smc.h>

#include "generic.h"


/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_data;

static struct resource usbh_resources[] = {
	[0] = {
		.start	= AT91SAM9260_UHP_BASE,
		.end	= AT91SAM9260_UHP_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_UHP,
		.end	= AT91SAM9260_ID_UHP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_device = {
	.name		= "at91_ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
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

	/* Enable overcurrent notification */
	for (i = 0; i < data->ports; i++) {
		if (data->overcurrent_pin[i])
			at91_set_gpio_input(data->overcurrent_pin[i], 1);
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

#ifdef CONFIG_USB_AT91
static struct at91_udc_data udc_data;

static struct resource udc_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_UDP,
		.end	= AT91SAM9260_BASE_UDP + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_UDP,
		.end	= AT91SAM9260_ID_UDP,
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
static u64 eth_dmamask = DMA_BIT_MASK(32);
static struct at91_eth_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_EMAC,
		.end	= AT91SAM9260_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_EMAC,
		.end	= AT91SAM9260_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_eth_device = {
	.name		= "macb",
	.id		= -1,
	.dev		= {
				.dma_mask		= &eth_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
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
	at91_set_A_periph(AT91_PIN_PA19, 0);	/* ETXCK_EREFCK */
	at91_set_A_periph(AT91_PIN_PA17, 0);	/* ERXDV */
	at91_set_A_periph(AT91_PIN_PA14, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PA15, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PA18, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PA16, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PA12, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PA13, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PA21, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PA20, 0);	/* EMDC */

	if (!data->is_rmii) {
		at91_set_B_periph(AT91_PIN_PA28, 0);	/* ECRS */
		at91_set_B_periph(AT91_PIN_PA29, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PA25, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PA26, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PA27, 0);	/* ERXCK */
		at91_set_B_periph(AT91_PIN_PA23, 0);	/* ETX2 */
		at91_set_B_periph(AT91_PIN_PA24, 0);	/* ETX3 */
		at91_set_B_periph(AT91_PIN_PA22, 0);	/* ETXER */
	}

	eth_data = *data;
	platform_device_register(&at91sam9260_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91) || defined(CONFIG_MMC_AT91_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct at91_mmc_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_MCI,
		.end	= AT91SAM9260_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_MCI,
		.end	= AT91SAM9260_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_mmc_device = {
	.name		= "at91_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
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
	at91_set_A_periph(AT91_PIN_PA8, 0);

	if (data->slot_b) {
		/* CMD */
		at91_set_B_periph(AT91_PIN_PA1, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_B_periph(AT91_PIN_PA0, 1);
		if (data->wire4) {
			at91_set_B_periph(AT91_PIN_PA5, 1);
			at91_set_B_periph(AT91_PIN_PA4, 1);
			at91_set_B_periph(AT91_PIN_PA3, 1);
		}
	} else {
		/* CMD */
		at91_set_A_periph(AT91_PIN_PA7, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA6, 1);
		if (data->wire4) {
			at91_set_A_periph(AT91_PIN_PA9, 1);
			at91_set_A_periph(AT91_PIN_PA10, 1);
			at91_set_A_periph(AT91_PIN_PA11, 1);
		}
	}

	mmc_data = *data;
	platform_device_register(&at91sam9260_mmc_device);
}
#else
void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  MMC / SD Slot for Atmel MCI Driver
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_MCI,
		.end	= AT91SAM9260_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_MCI,
		.end	= AT91SAM9260_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_mmc_device = {
	.name		= "atmel_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc_data,
	},
	.resource	= mmc_resources,
	.num_resources	= ARRAY_SIZE(mmc_resources),
};

void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data)
{
	unsigned int i;
	unsigned int slot_count = 0;

	if (!data)
		return;

	for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
		if (data->slot[i].bus_width) {
			/* input/irq */
			if (data->slot[i].detect_pin) {
				at91_set_gpio_input(data->slot[i].detect_pin, 1);
				at91_set_deglitch(data->slot[i].detect_pin, 1);
			}
			if (data->slot[i].wp_pin)
				at91_set_gpio_input(data->slot[i].wp_pin, 1);

			switch (i) {
			case 0:
				/* CMD */
				at91_set_A_periph(AT91_PIN_PA7, 1);
				/* DAT0, maybe DAT1..DAT3 */
				at91_set_A_periph(AT91_PIN_PA6, 1);
				if (data->slot[i].bus_width == 4) {
					at91_set_A_periph(AT91_PIN_PA9, 1);
					at91_set_A_periph(AT91_PIN_PA10, 1);
					at91_set_A_periph(AT91_PIN_PA11, 1);
				}
				slot_count++;
				break;
			case 1:
				/* CMD */
				at91_set_B_periph(AT91_PIN_PA1, 1);
				/* DAT0, maybe DAT1..DAT3 */
				at91_set_B_periph(AT91_PIN_PA0, 1);
				if (data->slot[i].bus_width == 4) {
					at91_set_B_periph(AT91_PIN_PA5, 1);
					at91_set_B_periph(AT91_PIN_PA4, 1);
					at91_set_B_periph(AT91_PIN_PA3, 1);
				}
				slot_count++;
				break;
			default:
				printk(KERN_ERR
					"AT91: SD/MMC slot %d not available\n", i);
				break;
			}
		}
	}

	if (slot_count) {
		/* CLK */
		at91_set_A_periph(AT91_PIN_PA8, 0);

		mmc_data = *data;
		platform_device_register(&at91sam9260_mmc_device);
	}
}
#else
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  NAND / SmartMedia
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MTD_NAND_ATMEL) || defined(CONFIG_MTD_NAND_ATMEL_MODULE)
static struct atmel_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	[0] = {
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_BASE_SYS + AT91_ECC,
		.end	= AT91_BASE_SYS + AT91_ECC + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9260_nand_device = {
	.name		= "atmel_nand",
	.id		= -1,
	.dev		= {
				.platform_data	= &nand_data,
	},
	.resource	= nand_resources,
	.num_resources	= ARRAY_SIZE(nand_resources),
};

void __init at91_add_device_nand(struct atmel_nand_data *data)
{
	unsigned long csa;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_CS3A_SMC_SMARTMEDIA);

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
	platform_device_register(&at91sam9260_nand_device);
}
#else
void __init at91_add_device_nand(struct atmel_nand_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  TWI (i2c)
 * -------------------------------------------------------------------- */

/*
 * Prefer the GPIO code since the TWI controller isn't robust
 * (gets overruns and underruns under load) and can only issue
 * repeated STARTs in one scenario (the driver doesn't yet handle them).
 */

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)

static struct i2c_gpio_platform_data pdata = {
	.sda_pin		= AT91_PIN_PA23,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PA24,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
};

static struct platform_device at91sam9260_twi_device = {
	.name			= "i2c-gpio",
	.id			= -1,
	.dev.platform_data	= &pdata,
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	at91_set_GPIO_periph(AT91_PIN_PA23, 1);		/* TWD (SDA) */
	at91_set_multi_drive(AT91_PIN_PA23, 1);

	at91_set_GPIO_periph(AT91_PIN_PA24, 1);		/* TWCK (SCL) */
	at91_set_multi_drive(AT91_PIN_PA24, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91sam9260_twi_device);
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)

static struct resource twi_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_TWI,
		.end	= AT91SAM9260_BASE_TWI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_TWI,
		.end	= AT91SAM9260_ID_TWI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_twi_device = {
	.name		= "at91_i2c",
	.id		= -1,
	.resource	= twi_resources,
	.num_resources	= ARRAY_SIZE(twi_resources),
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	/* pins used for TWI interface */
	at91_set_A_periph(AT91_PIN_PA23, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PA23, 1);

	at91_set_A_periph(AT91_PIN_PA24, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PA24, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91sam9260_twi_device);
}
#else
void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct resource spi0_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_SPI0,
		.end	= AT91SAM9260_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_SPI0,
		.end	= AT91SAM9260_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PA3, AT91_PIN_PC11, AT91_PIN_PC16, AT91_PIN_PC17 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_SPI1,
		.end	= AT91SAM9260_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_SPI1,
		.end	= AT91SAM9260_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi1_resources,
	.num_resources	= ARRAY_SIZE(spi1_resources),
};

static const unsigned spi1_standard_cs[4] = { AT91_PIN_PB3, AT91_PIN_PC5, AT91_PIN_PC4, AT91_PIN_PC3 };

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
		at91_set_A_periph(AT91_PIN_PA0, 0);	/* SPI0_MISO */
		at91_set_A_periph(AT91_PIN_PA1, 0);	/* SPI0_MOSI */
		at91_set_A_periph(AT91_PIN_PA2, 0);	/* SPI1_SPCK */

		platform_device_register(&at91sam9260_spi0_device);
	}
	if (enable_spi1) {
		at91_set_A_periph(AT91_PIN_PB0, 0);	/* SPI1_MISO */
		at91_set_A_periph(AT91_PIN_PB1, 0);	/* SPI1_MOSI */
		at91_set_A_periph(AT91_PIN_PB2, 0);	/* SPI1_SPCK */

		platform_device_register(&at91sam9260_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  Timer/Counter blocks
 * -------------------------------------------------------------------- */

#ifdef CONFIG_ATMEL_TCLIB

static struct resource tcb0_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_TCB0,
		.end	= AT91SAM9260_BASE_TCB0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_TC0,
		.end	= AT91SAM9260_ID_TC0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AT91SAM9260_ID_TC1,
		.end	= AT91SAM9260_ID_TC1,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= AT91SAM9260_ID_TC2,
		.end	= AT91SAM9260_ID_TC2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_tcb0_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb0_resources,
	.num_resources	= ARRAY_SIZE(tcb0_resources),
};

static struct resource tcb1_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_TCB1,
		.end	= AT91SAM9260_BASE_TCB1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_TC3,
		.end	= AT91SAM9260_ID_TC3,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AT91SAM9260_ID_TC4,
		.end	= AT91SAM9260_ID_TC4,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= AT91SAM9260_ID_TC5,
		.end	= AT91SAM9260_ID_TC5,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_tcb1_device = {
	.name		= "atmel_tcb",
	.id		= 1,
	.resource	= tcb1_resources,
	.num_resources	= ARRAY_SIZE(tcb1_resources),
};

static void __init at91_add_device_tc(void)
{
	platform_device_register(&at91sam9260_tcb0_device);
	platform_device_register(&at91sam9260_tcb1_device);
}
#else
static void __init at91_add_device_tc(void) { }
#endif


/* --------------------------------------------------------------------
 *  RTT
 * -------------------------------------------------------------------- */

static struct resource rtt_resources[] = {
	{
		.start	= AT91_BASE_SYS + AT91_RTT,
		.end	= AT91_BASE_SYS + AT91_RTT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9260_rtt_device = {
	.name		= "at91_rtt",
	.id		= 0,
	.resource	= rtt_resources,
	.num_resources	= ARRAY_SIZE(rtt_resources),
};

static void __init at91_add_device_rtt(void)
{
	platform_device_register(&at91sam9260_rtt_device);
}


/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91SAM9X_WATCHDOG) || defined(CONFIG_AT91SAM9X_WATCHDOG_MODULE)
static struct platform_device at91sam9260_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91sam9260_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  SSC -- Synchronous Serial Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ATMEL_SSC) || defined(CONFIG_ATMEL_SSC_MODULE)
static u64 ssc_dmamask = DMA_BIT_MASK(32);

static struct resource ssc_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_SSC,
		.end	= AT91SAM9260_BASE_SSC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_SSC,
		.end	= AT91SAM9260_ID_SSC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9260_ssc_device = {
	.name	= "ssc",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ssc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc_resources,
	.num_resources	= ARRAY_SIZE(ssc_resources),
};

static inline void configure_ssc_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PB17, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PB16, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PB18, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PB19, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_A_periph(AT91_PIN_PB20, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_A_periph(AT91_PIN_PB21, 1);
}

/*
 * SSC controllers are accessed through library code, instead of any
 * kind of all-singing/all-dancing driver.  For example one could be
 * used by a particular I2S audio codec's driver, while another one
 * on the same system might be used by a custom data capture driver.
 */
void __init at91_add_device_ssc(unsigned id, unsigned pins)
{
	struct platform_device *pdev;

	/*
	 * NOTE: caller is responsible for passing information matching
	 * "pins" to whatever will be using each particular controller.
	 */
	switch (id) {
	case AT91SAM9260_ID_SSC:
		pdev = &at91sam9260_ssc_device;
		configure_ssc_pins(pins);
		break;
	default:
		return;
	}

	platform_device_register(pdev);
}

#else
void __init at91_add_device_ssc(unsigned id, unsigned pins) {}
#endif


/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */
#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91_BASE_SYS + AT91_DBGU,
		.end	= AT91_BASE_SYS + AT91_DBGU + SZ_512 - 1,
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
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_dbgu_device = {
	.name		= "atmel_usart",
	.id		= 0,
	.dev		= {
				.dma_mask		= &dbgu_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &dbgu_data,
	},
	.resource	= dbgu_resources,
	.num_resources	= ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PB14, 0);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PB15, 1);		/* DTXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US0,
		.end	= AT91SAM9260_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US0,
		.end	= AT91SAM9260_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart0_device = {
	.name		= "atmel_usart",
	.id		= 1,
	.dev		= {
				.dma_mask		= &uart0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart0_data,
	},
	.resource	= uart0_resources,
	.num_resources	= ARRAY_SIZE(uart0_resources),
};

static inline void configure_usart0_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB4, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PB5, 0);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PB26, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PB27, 0);	/* CTS0 */
	if (pins & ATMEL_UART_DTR)
		at91_set_A_periph(AT91_PIN_PB24, 0);	/* DTR0 */
	if (pins & ATMEL_UART_DSR)
		at91_set_A_periph(AT91_PIN_PB22, 0);	/* DSR0 */
	if (pins & ATMEL_UART_DCD)
		at91_set_A_periph(AT91_PIN_PB23, 0);	/* DCD0 */
	if (pins & ATMEL_UART_RI)
		at91_set_A_periph(AT91_PIN_PB25, 0);	/* RI0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US1,
		.end	= AT91SAM9260_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US1,
		.end	= AT91SAM9260_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart1_device = {
	.name		= "atmel_usart",
	.id		= 2,
	.dev		= {
				.dma_mask		= &uart1_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart1_data,
	},
	.resource	= uart1_resources,
	.num_resources	= ARRAY_SIZE(uart1_resources),
};

static inline void configure_usart1_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB6, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PB7, 0);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PB28, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PB29, 0);	/* CTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US2,
		.end	= AT91SAM9260_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US2,
		.end	= AT91SAM9260_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart2_device = {
	.name		= "atmel_usart",
	.id		= 3,
	.dev		= {
				.dma_mask		= &uart2_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart2_data,
	},
	.resource	= uart2_resources,
	.num_resources	= ARRAY_SIZE(uart2_resources),
};

static inline void configure_usart2_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB8, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PB9, 0);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA4, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA5, 0);	/* CTS2 */
}

static struct resource uart3_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US3,
		.end	= AT91SAM9260_BASE_US3 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US3,
		.end	= AT91SAM9260_ID_US3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart3_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart3_device = {
	.name		= "atmel_usart",
	.id		= 4,
	.dev		= {
				.dma_mask		= &uart3_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart3_data,
	},
	.resource	= uart3_resources,
	.num_resources	= ARRAY_SIZE(uart3_resources),
};

static inline void configure_usart3_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB10, 1);		/* TXD3 */
	at91_set_A_periph(AT91_PIN_PB11, 0);		/* RXD3 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PC8, 0);	/* RTS3 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PC10, 0);	/* CTS3 */
}

static struct resource uart4_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US4,
		.end	= AT91SAM9260_BASE_US4 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US4,
		.end	= AT91SAM9260_ID_US4,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart4_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart4_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart4_device = {
	.name		= "atmel_usart",
	.id		= 5,
	.dev		= {
				.dma_mask		= &uart4_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart4_data,
	},
	.resource	= uart4_resources,
	.num_resources	= ARRAY_SIZE(uart4_resources),
};

static inline void configure_usart4_pins(void)
{
	at91_set_B_periph(AT91_PIN_PA31, 1);		/* TXD4 */
	at91_set_B_periph(AT91_PIN_PA30, 0);		/* RXD4 */
}

static struct resource uart5_resources[] = {
	[0] = {
		.start	= AT91SAM9260_BASE_US5,
		.end	= AT91SAM9260_BASE_US5 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9260_ID_US5,
		.end	= AT91SAM9260_ID_US5,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart5_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart5_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9260_uart5_device = {
	.name		= "atmel_usart",
	.id		= 6,
	.dev		= {
				.dma_mask		= &uart5_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart5_data,
	},
	.resource	= uart5_resources,
	.num_resources	= ARRAY_SIZE(uart5_resources),
};

static inline void configure_usart5_pins(void)
{
	at91_set_A_periph(AT91_PIN_PB12, 1);		/* TXD5 */
	at91_set_A_periph(AT91_PIN_PB13, 0);		/* RXD5 */
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];	/* the UARTs to use */
struct platform_device *atmel_default_console_device;	/* the serial console device */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;
	struct atmel_uart_data *pdata;

	switch (id) {
		case 0:		/* DBGU */
			pdev = &at91sam9260_dbgu_device;
			configure_dbgu_pins();
			break;
		case AT91SAM9260_ID_US0:
			pdev = &at91sam9260_uart0_device;
			configure_usart0_pins(pins);
			break;
		case AT91SAM9260_ID_US1:
			pdev = &at91sam9260_uart1_device;
			configure_usart1_pins(pins);
			break;
		case AT91SAM9260_ID_US2:
			pdev = &at91sam9260_uart2_device;
			configure_usart2_pins(pins);
			break;
		case AT91SAM9260_ID_US3:
			pdev = &at91sam9260_uart3_device;
			configure_usart3_pins(pins);
			break;
		case AT91SAM9260_ID_US4:
			pdev = &at91sam9260_uart4_device;
			configure_usart4_pins();
			break;
		case AT91SAM9260_ID_US5:
			pdev = &at91sam9260_uart5_device;
			configure_usart5_pins();
			break;
		default:
			return;
	}
	pdata = pdev->dev.platform_data;
	pdata->num = portnr;		/* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART) {
		atmel_default_console_device = at91_uarts[portnr];
		at91sam9260_set_console_clock(at91_uarts[portnr]->id);
	}
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}

	if (!atmel_default_console_device)
		printk(KERN_INFO "AT91: No default serial console defined.\n");
}
#else
void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins) {}
void __init at91_set_serial_console(unsigned portnr) {}
void __init at91_add_device_serial(void) {}
#endif

/* --------------------------------------------------------------------
 *  CF/IDE
 * -------------------------------------------------------------------- */

#if defined(CONFIG_BLK_DEV_IDE_AT91) || defined(CONFIG_BLK_DEV_IDE_AT91_MODULE) || \
	defined(CONFIG_PATA_AT91) || defined(CONFIG_PATA_AT91_MODULE) || \
	defined(CONFIG_AT91_CF) || defined(CONFIG_AT91_CF_MODULE)

static struct at91_cf_data cf0_data;

static struct resource cf0_resources[] = {
	[0] = {
		.start	= AT91_CHIPSELECT_4,
		.end	= AT91_CHIPSELECT_4 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device cf0_device = {
	.id		= 0,
	.dev		= {
				.platform_data	= &cf0_data,
	},
	.resource	= cf0_resources,
	.num_resources	= ARRAY_SIZE(cf0_resources),
};

static struct at91_cf_data cf1_data;

static struct resource cf1_resources[] = {
	[0] = {
		.start	= AT91_CHIPSELECT_5,
		.end	= AT91_CHIPSELECT_5 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device cf1_device = {
	.id		= 1,
	.dev		= {
				.platform_data	= &cf1_data,
	},
	.resource	= cf1_resources,
	.num_resources	= ARRAY_SIZE(cf1_resources),
};

void __init at91_add_device_cf(struct at91_cf_data *data)
{
	struct platform_device *pdev;
	unsigned long csa;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);

	switch (data->chipselect) {
	case 4:
		at91_set_multi_drive(AT91_PIN_PC8, 0);
		at91_set_A_periph(AT91_PIN_PC8, 0);
		csa |= AT91_MATRIX_CS4A_SMC_CF1;
		cf0_data = *data;
		pdev = &cf0_device;
		break;
	case 5:
		at91_set_multi_drive(AT91_PIN_PC9, 0);
		at91_set_A_periph(AT91_PIN_PC9, 0);
		csa |= AT91_MATRIX_CS5A_SMC_CF2;
		cf1_data = *data;
		pdev = &cf1_device;
		break;
	default:
		printk(KERN_ERR "AT91 CF: bad chip-select requested (%u)\n",
		       data->chipselect);
		return;
	}

	at91_sys_write(AT91_MATRIX_EBICSA, csa);

	if (data->rst_pin) {
		at91_set_multi_drive(data->rst_pin, 0);
		at91_set_gpio_output(data->rst_pin, 1);
	}

	if (data->irq_pin) {
		at91_set_gpio_input(data->irq_pin, 0);
		at91_set_deglitch(data->irq_pin, 1);
	}

	if (data->det_pin) {
		at91_set_gpio_input(data->det_pin, 0);
		at91_set_deglitch(data->det_pin, 1);
	}

	at91_set_B_periph(AT91_PIN_PC6, 0);     /* CFCE1 */
	at91_set_B_periph(AT91_PIN_PC7, 0);     /* CFCE2 */
	at91_set_A_periph(AT91_PIN_PC10, 0);    /* CFRNW */
	at91_set_A_periph(AT91_PIN_PC15, 1);    /* NWAIT */

	if (data->flags & AT91_CF_TRUE_IDE)
#if defined(CONFIG_PATA_AT91) || defined(CONFIG_PATA_AT91_MODULE)
		pdev->name = "pata_at91";
#elif defined(CONFIG_BLK_DEV_IDE_AT91) || defined(CONFIG_BLK_DEV_IDE_AT91_MODULE)
		pdev->name = "at91_ide";
#else
#warning "board requires AT91_CF_TRUE_IDE: enable either at91_ide or pata_at91"
#endif
	else
		pdev->name = "at91_cf";

	platform_device_register(pdev);
}

#else
void __init at91_add_device_cf(struct at91_cf_data * data) {}
#endif

/* -------------------------------------------------------------------- */
/*
 * These devices are always present and don't need any board-specific
 * setup.
 */
static int __init at91_add_standard_devices(void)
{
	at91_add_device_rtt();
	at91_add_device_watchdog();
	at91_add_device_tc();
	return 0;
}

arch_initcall(at91_add_standard_devices);
