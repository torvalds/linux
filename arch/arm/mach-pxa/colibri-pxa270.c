/*
 *  linux/arch/arm/mach-pxa/colibri-pxa270.c
 *
 *  Support for Toradex PXA270 based Colibri module
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
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/ucb1400.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/audio.h>
#include <mach/pxa27x.h>
#include <mach/colibri.h>
#include <mach/mmc.h>
#include <mach/ohci.h>
#include <mach/pxa27x-udc.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static mfp_cfg_t colibri_pxa270_pin_config[] __initdata = {
	/* Ethernet */
	GPIO78_nCS_2,	/* Ethernet CS */
	GPIO114_GPIO,	/* Ethernet IRQ */

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO0_GPIO,	/* SD detect */

	/* FFUART */
	GPIO39_FFUART_TXD,
	GPIO34_FFUART_RXD,

	/* UHC */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
	GPIO119_USBH2_PWR,
	GPIO120_USBH2_PEN,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO95_AC97_nRESET,
	GPIO98_AC97_SYSCLK,
	GPIO113_GPIO,	/* Touchscreen IRQ */
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition colibri_partitions[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE	/* force read-only */
	}, {
		.name =		"Kernel",
		.offset =	0x00040000,
		.size =		0x00400000,
		.mask_flags =	0
	}, {
		.name =		"Rootfs",
		.offset =	0x00440000,
		.size =		MTDPART_SIZ_FULL,
		.mask_flags =	0
	}
};

static struct physmap_flash_data colibri_flash_data[] = {
	{
		.width		= 4,			/* bankwidth in bytes */
		.parts		= colibri_partitions,
		.nr_parts	= ARRAY_SIZE(colibri_partitions)
	}
};

static struct resource colibri_pxa270_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device colibri_pxa270_flash_device = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev 	= {
		.platform_data = colibri_flash_data,
	},
	.resource = &colibri_pxa270_flash_resource,
	.num_resources = 1,
};

static void __init colibri_pxa270_nor_init(void)
{
	platform_device_register(&colibri_pxa270_flash_device);
}
#else
static inline void colibri_pxa270_nor_init(void) {}
#endif

/******************************************************************************
 * Ethernet
 ******************************************************************************/
#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource colibri_pxa270_dm9000_resources[] = {
	[0] = {
		.start	= PXA_CS2_PHYS,
		.end	= PXA_CS2_PHYS + 3,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_CS2_PHYS + 4,
		.end	= PXA_CS2_PHYS + 4 + 500,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= gpio_to_irq(GPIO114_COLIBRI_PXA270_ETH_IRQ),
		.end	= gpio_to_irq(GPIO114_COLIBRI_PXA270_ETH_IRQ),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	},
};

static struct platform_device colibri_pxa270_dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(colibri_pxa270_dm9000_resources),
	.resource	= colibri_pxa270_dm9000_resources,
};

static void __init colibri_pxa270_eth_init(void)
{
	platform_device_register(&colibri_pxa270_dm9000_device);
}
#else
static inline void colibri_pxa270_eth_init(void) {}
#endif

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data colibri_pxa270_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_power		= -1,
	.gpio_card_detect	= GPIO0_COLIBRI_PXA270_SD_DETECT,
	.gpio_card_ro		= -1,
	.detect_delay_ms	= 200,
};

static void __init colibri_pxa270_mmc_init(void)
{
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
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 |
			POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.init		= colibri_pxa270_ohci_init,
};

static void __init colibri_pxa270_uhc_init(void)
{
	pxa_set_ohci_info(&colibri_pxa270_ohci_info);
}
#else
static inline void colibri_pxa270_uhc_init(void) {}
#endif

/******************************************************************************
 * Audio and Touchscreen
 ******************************************************************************/
#if	defined(CONFIG_TOUCHSCREEN_UCB1400) || \
	defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static pxa2xx_audio_ops_t colibri_pxa270_ac97_pdata = {
	.reset_gpio	= 95,
};

static struct ucb1400_pdata colibri_pxa270_ucb1400_pdata = {
	.irq		= gpio_to_irq(GPIO113_COLIBRI_PXA270_TS_IRQ),
};

static struct platform_device colibri_pxa270_ucb1400_device = {
	.name		= "ucb1400_core",
	.id		= -1,
	.dev		= {
		.platform_data = &colibri_pxa270_ucb1400_pdata,
	},
};

static void __init colibri_pxa270_tsc_init(void)
{
	pxa_set_ac97_info(&colibri_pxa270_ac97_pdata);
	platform_device_register(&colibri_pxa270_ucb1400_device);
}
#else
static inline void colibri_pxa270_tsc_init(void) {}
#endif

static void __init colibri_pxa270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa270_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	colibri_pxa270_nor_init();
	colibri_pxa270_eth_init();
	colibri_pxa270_mmc_init();
	colibri_pxa270_uhc_init();
	colibri_pxa270_tsc_init();
}

MACHINE_START(COLIBRI, "Toradex Colibri PXA270")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa270_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

