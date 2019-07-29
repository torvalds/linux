/*
 *  linux/arch/arm/mach-pxa/colibri-evalboard.c
 *
 *  Support for Toradex Colibri Evaluation Carrier Board
 *  Daniel Mack <daniel@caiaq.de>
 *  Marek Vasut <marek.vasut@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/machine.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-pxa.h>
#include <asm/io.h>

#include "pxa27x.h"
#include "colibri.h"
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include "pxa27x-udc.h"

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data colibri_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.detect_delay_ms	= 200,
};

static struct gpiod_lookup_table colibri_pxa270_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO0_COLIBRI_PXA270_SD_DETECT,
			    "cd", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table colibri_pxa300_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO13_COLIBRI_PXA300_SD_DETECT,
			    "cd", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table colibri_pxa320_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO28_COLIBRI_PXA320_SD_DETECT,
			    "cd", GPIO_ACTIVE_LOW),
		{ },
	},
};

static void __init colibri_mmc_init(void)
{
	if (machine_is_colibri())	/* PXA270 Colibri */
		gpiod_add_lookup_table(&colibri_pxa270_mci_gpio_table);
	if (machine_is_colibri300())	/* PXA300 Colibri */
		gpiod_add_lookup_table(&colibri_pxa300_mci_gpio_table);
	else				/* PXA320 Colibri */
		gpiod_add_lookup_table(&colibri_pxa320_mci_gpio_table);

	pxa_set_mci_info(&colibri_mci_platform_data);
}
#else
static inline void colibri_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int colibri_ohci_init(struct device *dev)
{
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;
	return 0;
}

static struct pxaohci_platform_data colibri_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 |
			  POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.init		= colibri_ohci_init,
};

static void __init colibri_uhc_init(void)
{
	/* Colibri PXA270 has two usb ports, TBA for 320 */
	if (machine_is_colibri())
		colibri_ohci_info.flags	|= ENABLE_PORT2;

	pxa_set_ohci_info(&colibri_ohci_info);
}
#else
static inline void colibri_uhc_init(void) {}
#endif

/******************************************************************************
 * I2C RTC
 ******************************************************************************/
#if defined(CONFIG_RTC_DRV_DS1307) || defined(CONFIG_RTC_DRV_DS1307_MODULE)
static struct i2c_board_info __initdata colibri_i2c_devs[] = {
	{
		I2C_BOARD_INFO("m41t00", 0x68),
	},
};

static void __init colibri_rtc_init(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(colibri_i2c_devs));
}
#else
static inline void colibri_rtc_init(void) {}
#endif

void __init colibri_evalboard_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	colibri_mmc_init();
	colibri_uhc_init();
	colibri_rtc_init();
}
