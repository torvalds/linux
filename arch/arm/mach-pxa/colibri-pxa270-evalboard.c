/*
 *  linux/arch/arm/mach-pxa/colibri-pxa270-evalboard.c
 *
 *  Support for Toradex PXA270 based Colibri Evaluation Carrier Board
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
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>

#include <mach/pxa27x.h>
#include <mach/colibri.h>
#include <mach/mmc.h>
#include <mach/ohci.h>
#include <mach/pxa27x-udc.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data colibri_pxa270_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_power		= -1,
	.gpio_card_ro		= -1,
	.detect_delay_ms	= 200,
};

static void __init colibri_pxa270_mmc_init(void)
{
	if (machine_is_colibri())	/* PXA270 Colibri */
		colibri_pxa270_mci_platform_data.gpio_card_detect =
			GPIO0_COLIBRI_PXA270_SD_DETECT;
	if (machine_is_colibri300())	/* PXA300 Colibri */
		colibri_pxa270_mci_platform_data.gpio_card_detect =
			GPIO39_COLIBRI_PXA300_SD_DETECT;
	else				/* PXA320 Colibri */
		colibri_pxa270_mci_platform_data.gpio_card_detect =
			GPIO28_COLIBRI_PXA320_SD_DETECT;

	pxa_set_mci_info(&colibri_pxa270_mci_platform_data);
}
#else
static inline void colibri_pxa270_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int colibri_pxa270_ohci_init(struct device *dev)
{
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;
	return 0;
}

static struct pxaohci_platform_data colibri_pxa270_ohci_info = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 |
			  POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.init		= colibri_pxa270_ohci_init,
};

static void __init colibri_pxa270_uhc_init(void)
{
	/* Colibri PXA270 has two usb ports, TBA for 320 */
	if (machine_is_colibri())
		colibri_pxa270_ohci_info.flags	|= ENABLE_PORT2;

	pxa_set_ohci_info(&colibri_pxa270_ohci_info);
}
#else
static inline void colibri_pxa270_uhc_init(void) {}
#endif

void __init colibri_pxa270_evalboard_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	colibri_pxa270_mmc_init();
	colibri_pxa270_uhc_init();
}
