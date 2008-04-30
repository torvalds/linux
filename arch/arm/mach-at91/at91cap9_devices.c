/*
 * arch/arm/mach-at91/at91cap9_devices.c
 *
 *  Copyright (C) 2007 Stelian Pop <stelian.pop@leadtechdesign.com>
 *  Copyright (C) 2007 Lead Tech Design <www.leadtechdesign.com>
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

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>

#include <video/atmel_lcdc.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/at91cap9.h>
#include <asm/arch/at91cap9_matrix.h>
#include <asm/arch/at91sam9_smc.h>

#include "generic.h"


/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_data;

static struct resource usbh_resources[] = {
	[0] = {
		.start	= AT91CAP9_UHP_BASE,
		.end	= AT91CAP9_UHP_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_UHP,
		.end	= AT91CAP9_ID_UHP,
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
 *  Ethernet
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MACB) || defined(CONFIG_MACB_MODULE)
static u64 eth_dmamask = DMA_BIT_MASK(32);
static struct at91_eth_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_EMAC,
		.end	= AT91CAP9_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_EMAC,
		.end	= AT91CAP9_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_eth_device = {
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
	at91_set_A_periph(AT91_PIN_PB21, 0);	/* ETXCK_EREFCK */
	at91_set_A_periph(AT91_PIN_PB22, 0);	/* ERXDV */
	at91_set_A_periph(AT91_PIN_PB25, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PB26, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PB27, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PB28, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PB23, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PB24, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PB30, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PB29, 0);	/* EMDC */

	if (!data->is_rmii) {
		at91_set_B_periph(AT91_PIN_PC25, 0);	/* ECRS */
		at91_set_B_periph(AT91_PIN_PC26, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PC22, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PC23, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PC27, 0);	/* ERXCK */
		at91_set_B_periph(AT91_PIN_PC20, 0);	/* ETX2 */
		at91_set_B_periph(AT91_PIN_PC21, 0);	/* ETX3 */
		at91_set_B_periph(AT91_PIN_PC24, 0);	/* ETXER */
	}

	eth_data = *data;
	platform_device_register(&at91cap9_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91) || defined(CONFIG_MMC_AT91_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct at91_mmc_data mmc0_data, mmc1_data;

static struct resource mmc0_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_MCI0,
		.end	= AT91CAP9_BASE_MCI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_MCI0,
		.end	= AT91CAP9_ID_MCI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_mmc0_device = {
	.name		= "at91_mci",
	.id		= 0,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc0_data,
	},
	.resource	= mmc0_resources,
	.num_resources	= ARRAY_SIZE(mmc0_resources),
};

static struct resource mmc1_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_MCI1,
		.end	= AT91CAP9_BASE_MCI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_MCI1,
		.end	= AT91CAP9_ID_MCI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_mmc1_device = {
	.name		= "at91_mci",
	.id		= 1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
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
		at91_set_A_periph(AT91_PIN_PA2, 0);

		/* CMD */
		at91_set_A_periph(AT91_PIN_PA1, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA0, 1);
		if (data->wire4) {
			at91_set_A_periph(AT91_PIN_PA3, 1);
			at91_set_A_periph(AT91_PIN_PA4, 1);
			at91_set_A_periph(AT91_PIN_PA5, 1);
		}

		mmc0_data = *data;
		at91_clock_associate("mci0_clk", &at91cap9_mmc1_device.dev, "mci_clk");
		platform_device_register(&at91cap9_mmc0_device);
	} else {			/* MCI1 */
		/* CLK */
		at91_set_A_periph(AT91_PIN_PA16, 0);

		/* CMD */
		at91_set_A_periph(AT91_PIN_PA17, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA18, 1);
		if (data->wire4) {
			at91_set_A_periph(AT91_PIN_PA19, 1);
			at91_set_A_periph(AT91_PIN_PA20, 1);
			at91_set_A_periph(AT91_PIN_PA21, 1);
		}

		mmc1_data = *data;
		at91_clock_associate("mci1_clk", &at91cap9_mmc1_device.dev, "mci_clk");
		platform_device_register(&at91cap9_mmc1_device);
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

static struct platform_device at91cap9_nand_device = {
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

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_EBI_CS3A_SMC_SMARTMEDIA | AT91_MATRIX_EBI_VDDIOMSEL_3_3V);

	/* set the bus interface characteristics */
	at91_sys_write(AT91_SMC_SETUP(3), AT91_SMC_NWESETUP_(2) | AT91_SMC_NCS_WRSETUP_(1)
			| AT91_SMC_NRDSETUP_(2) | AT91_SMC_NCS_RDSETUP_(1));

	at91_sys_write(AT91_SMC_PULSE(3), AT91_SMC_NWEPULSE_(4) | AT91_SMC_NCS_WRPULSE_(6)
			| AT91_SMC_NRDPULSE_(4) | AT91_SMC_NCS_RDPULSE_(6));

	at91_sys_write(AT91_SMC_CYCLE(3), AT91_SMC_NWECYCLE_(8) | AT91_SMC_NRDCYCLE_(8));

	if (data->bus_width_16)
		mode = AT91_SMC_DBW_16;
	else
		mode = AT91_SMC_DBW_8;
	at91_sys_write(AT91_SMC_MODE(3), mode | AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_TDF_(1));

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
	platform_device_register(&at91cap9_nand_device);
}
#else
void __init at91_add_device_nand(struct at91_nand_data *data) {}
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
	.sda_pin		= AT91_PIN_PB4,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PB5,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
};

static struct platform_device at91cap9_twi_device = {
	.name			= "i2c-gpio",
	.id			= -1,
	.dev.platform_data	= &pdata,
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	at91_set_GPIO_periph(AT91_PIN_PB4, 1);		/* TWD (SDA) */
	at91_set_multi_drive(AT91_PIN_PB4, 1);

	at91_set_GPIO_periph(AT91_PIN_PB5, 1);		/* TWCK (SCL) */
	at91_set_multi_drive(AT91_PIN_PB5, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91cap9_twi_device);
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)

static struct resource twi_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_TWI,
		.end	= AT91CAP9_BASE_TWI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_TWI,
		.end	= AT91CAP9_ID_TWI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_twi_device = {
	.name		= "at91_i2c",
	.id		= -1,
	.resource	= twi_resources,
	.num_resources	= ARRAY_SIZE(twi_resources),
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	/* pins used for TWI interface */
	at91_set_B_periph(AT91_PIN_PB4, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PB4, 1);

	at91_set_B_periph(AT91_PIN_PB5, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PB5, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91cap9_twi_device);
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
		.start	= AT91CAP9_BASE_SPI0,
		.end	= AT91CAP9_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_SPI0,
		.end	= AT91CAP9_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PA5, AT91_PIN_PA3, AT91_PIN_PD0, AT91_PIN_PD1 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_SPI1,
		.end	= AT91CAP9_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_SPI1,
		.end	= AT91CAP9_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
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

		at91_clock_associate("spi0_clk", &at91cap9_spi0_device.dev, "spi_clk");
		platform_device_register(&at91cap9_spi0_device);
	}
	if (enable_spi1) {
		at91_set_A_periph(AT91_PIN_PB12, 0);	/* SPI1_MISO */
		at91_set_A_periph(AT91_PIN_PB13, 0);	/* SPI1_MOSI */
		at91_set_A_periph(AT91_PIN_PB14, 0);	/* SPI1_SPCK */

		at91_clock_associate("spi1_clk", &at91cap9_spi1_device.dev, "spi_clk");
		platform_device_register(&at91cap9_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  Timer/Counter block
 * -------------------------------------------------------------------- */

#ifdef CONFIG_ATMEL_TCLIB

static struct resource tcb_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_TCB0,
		.end	= AT91CAP9_BASE_TCB0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_TCB,
		.end	= AT91CAP9_ID_TCB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_tcb_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb_resources,
	.num_resources	= ARRAY_SIZE(tcb_resources),
};

static void __init at91_add_device_tc(void)
{
	/* this chip has one clock and irq for all three TC channels */
	at91_clock_associate("tcb_clk", &at91cap9_tcb_device.dev, "t0_clk");
	platform_device_register(&at91cap9_tcb_device);
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

static struct platform_device at91cap9_rtt_device = {
	.name		= "at91_rtt",
	.id		= 0,
	.resource	= rtt_resources,
	.num_resources	= ARRAY_SIZE(rtt_resources),
};

static void __init at91_add_device_rtt(void)
{
	platform_device_register(&at91cap9_rtt_device);
}


/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91SAM9_WATCHDOG) || defined(CONFIG_AT91SAM9_WATCHDOG_MODULE)
static struct platform_device at91cap9_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91cap9_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  AC97
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SND_AT91_AC97) || defined(CONFIG_SND_AT91_AC97_MODULE)
static u64 ac97_dmamask = DMA_BIT_MASK(32);
static struct atmel_ac97_data ac97_data;

static struct resource ac97_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_AC97C,
		.end	= AT91CAP9_BASE_AC97C + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_AC97C,
		.end	= AT91CAP9_ID_AC97C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_ac97_device = {
	.name		= "ac97c",
	.id		= 1,
	.dev		= {
				.dma_mask		= &ac97_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &ac97_data,
	},
	.resource	= ac97_resources,
	.num_resources	= ARRAY_SIZE(ac97_resources),
};

void __init at91_add_device_ac97(struct atmel_ac97_data *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PA6, 0);	/* AC97FS */
	at91_set_A_periph(AT91_PIN_PA7, 0);	/* AC97CK */
	at91_set_A_periph(AT91_PIN_PA8, 0);	/* AC97TX */
	at91_set_A_periph(AT91_PIN_PA9, 0);	/* AC97RX */

	/* reset */
	if (data->reset_pin)
		at91_set_gpio_output(data->reset_pin, 0);

	ac97_data = *data;
	platform_device_register(&at91cap9_ac97_device);
}
#else
void __init at91_add_device_ac97(struct atmel_ac97_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  LCD Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static u64 lcdc_dmamask = DMA_BIT_MASK(32);
static struct atmel_lcdfb_info lcdc_data;

static struct resource lcdc_resources[] = {
	[0] = {
		.start	= AT91CAP9_LCDC_BASE,
		.end	= AT91CAP9_LCDC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_LCDC,
		.end	= AT91CAP9_ID_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_lcdc_device = {
	.name		= "atmel_lcdfb",
	.id		= 0,
	.dev		= {
				.dma_mask		= &lcdc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &lcdc_data,
	},
	.resource	= lcdc_resources,
	.num_resources	= ARRAY_SIZE(lcdc_resources),
};

void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PC1, 0);	/* LCDHSYNC */
	at91_set_A_periph(AT91_PIN_PC2, 0);	/* LCDDOTCK */
	at91_set_A_periph(AT91_PIN_PC3, 0);	/* LCDDEN */
	at91_set_B_periph(AT91_PIN_PB9, 0);	/* LCDCC */
	at91_set_A_periph(AT91_PIN_PC6, 0);	/* LCDD2 */
	at91_set_A_periph(AT91_PIN_PC7, 0);	/* LCDD3 */
	at91_set_A_periph(AT91_PIN_PC8, 0);	/* LCDD4 */
	at91_set_A_periph(AT91_PIN_PC9, 0);	/* LCDD5 */
	at91_set_A_periph(AT91_PIN_PC10, 0);	/* LCDD6 */
	at91_set_A_periph(AT91_PIN_PC11, 0);	/* LCDD7 */
	at91_set_A_periph(AT91_PIN_PC14, 0);	/* LCDD10 */
	at91_set_A_periph(AT91_PIN_PC15, 0);	/* LCDD11 */
	at91_set_A_periph(AT91_PIN_PC16, 0);	/* LCDD12 */
	at91_set_A_periph(AT91_PIN_PC17, 0);	/* LCDD13 */
	at91_set_A_periph(AT91_PIN_PC18, 0);	/* LCDD14 */
	at91_set_A_periph(AT91_PIN_PC19, 0);	/* LCDD15 */
	at91_set_A_periph(AT91_PIN_PC22, 0);	/* LCDD18 */
	at91_set_A_periph(AT91_PIN_PC23, 0);	/* LCDD19 */
	at91_set_A_periph(AT91_PIN_PC24, 0);	/* LCDD20 */
	at91_set_A_periph(AT91_PIN_PC25, 0);	/* LCDD21 */
	at91_set_A_periph(AT91_PIN_PC26, 0);	/* LCDD22 */
	at91_set_A_periph(AT91_PIN_PC27, 0);	/* LCDD23 */

	lcdc_data = *data;
	platform_device_register(&at91_lcdc_device);
}
#else
void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data) {}
#endif


/* --------------------------------------------------------------------
 *  SSC -- Synchronous Serial Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ATMEL_SSC) || defined(CONFIG_ATMEL_SSC_MODULE)
static u64 ssc0_dmamask = DMA_BIT_MASK(32);

static struct resource ssc0_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_SSC0,
		.end	= AT91CAP9_BASE_SSC0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_SSC0,
		.end	= AT91CAP9_ID_SSC0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_ssc0_device = {
	.name	= "ssc",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ssc0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc0_resources,
	.num_resources	= ARRAY_SIZE(ssc0_resources),
};

static inline void configure_ssc0_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PB0, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PB1, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PB2, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PB3, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_A_periph(AT91_PIN_PB4, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_A_periph(AT91_PIN_PB5, 1);
}

static u64 ssc1_dmamask = DMA_BIT_MASK(32);

static struct resource ssc1_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_SSC1,
		.end	= AT91CAP9_BASE_SSC1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_SSC1,
		.end	= AT91CAP9_ID_SSC1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91cap9_ssc1_device = {
	.name	= "ssc",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ssc1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc1_resources,
	.num_resources	= ARRAY_SIZE(ssc1_resources),
};

static inline void configure_ssc1_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PB6, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PB7, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PB8, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PB9, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_A_periph(AT91_PIN_PB10, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_A_periph(AT91_PIN_PB11, 1);
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
	case AT91CAP9_ID_SSC0:
		pdev = &at91cap9_ssc0_device;
		configure_ssc0_pins(pins);
		at91_clock_associate("ssc0_clk", &pdev->dev, "ssc");
		break;
	case AT91CAP9_ID_SSC1:
		pdev = &at91cap9_ssc1_device;
		configure_ssc1_pins(pins);
		at91_clock_associate("ssc1_clk", &pdev->dev, "ssc");
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

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91cap9_dbgu_device = {
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
	at91_set_A_periph(AT91_PIN_PC30, 0);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PC31, 1);		/* DTXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_US0,
		.end	= AT91CAP9_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_US0,
		.end	= AT91CAP9_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91cap9_uart0_device = {
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
	at91_set_A_periph(AT91_PIN_PA22, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA23, 0);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA24, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA25, 0);	/* CTS0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_US1,
		.end	= AT91CAP9_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_US1,
		.end	= AT91CAP9_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91cap9_uart1_device = {
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
	at91_set_A_periph(AT91_PIN_PD0, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PD1, 0);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PD7, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PD8, 0);	/* CTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91CAP9_BASE_US2,
		.end	= AT91CAP9_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91CAP9_ID_US2,
		.end	= AT91CAP9_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91cap9_uart2_device = {
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
	at91_set_A_periph(AT91_PIN_PD2, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PD3, 0);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PD5, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PD6, 0);	/* CTS2 */
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];	/* the UARTs to use */
struct platform_device *atmel_default_console_device;	/* the serial console device */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;

	switch (id) {
		case 0:		/* DBGU */
			pdev = &at91cap9_dbgu_device;
			configure_dbgu_pins();
			at91_clock_associate("mck", &pdev->dev, "usart");
			break;
		case AT91CAP9_ID_US0:
			pdev = &at91cap9_uart0_device;
			configure_usart0_pins(pins);
			at91_clock_associate("usart0_clk", &pdev->dev, "usart");
			break;
		case AT91CAP9_ID_US1:
			pdev = &at91cap9_uart1_device;
			configure_usart1_pins(pins);
			at91_clock_associate("usart1_clk", &pdev->dev, "usart");
			break;
		case AT91CAP9_ID_US2:
			pdev = &at91cap9_uart2_device;
			configure_usart2_pins(pins);
			at91_clock_associate("usart2_clk", &pdev->dev, "usart");
			break;
		default:
			return;
	}
	pdev->id = portnr;		/* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART)
		atmel_default_console_device = at91_uarts[portnr];
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
