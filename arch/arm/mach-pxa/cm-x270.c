/*
 * linux/arch/arm/mach-pxa/cm-x270.c
 *
 * Copyright (C) 2007, 2008 CompuLab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <linux/rtc-v3020.h>
#include <video/mbxfb.h>

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spi/libertas_spi.h>

#include <mach/pxa27x.h>
#include <mach/ohci.h>
#include <mach/mmc.h>

#include "generic.h"

/* physical address if local-bus attached devices */
#define RTC_PHYS_BASE		(PXA_CS1_PHYS + (5 << 22))

/* GPIO IRQ usage */
#define GPIO83_MMC_IRQ		(83)

#define CMX270_MMC_IRQ		IRQ_GPIO(GPIO83_MMC_IRQ)

/* MMC power enable */
#define GPIO105_MMC_POWER	(105)

/* WLAN GPIOS */
#define GPIO19_WLAN_STRAP	(19)
#define GPIO102_WLAN_RST	(102)

static unsigned long cmx270_pin_config[] = {
	/* AC'97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO98_AC97_SYSCLK,
	GPIO113_AC97_nRESET,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* MCI controller */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* SSP1 */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* SSP2 */
	GPIO19_GPIO,	/* SSP2 clock is used as GPIO for Libertas pin-strap */
	GPIO14_GPIO,
	GPIO87_SSP2_TXD,
	GPIO88_SSP2_RXD,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* SDRAM and local bus */
	GPIO15_nCS_1,
	GPIO78_nCS_2,
	GPIO79_nCS_3,
	GPIO80_nCS_4,
	GPIO33_nCS_5,
	GPIO49_nPWE,
	GPIO18_RDY,

	/* GPIO */
	GPIO0_GPIO	| WAKEUP_ON_EDGE_BOTH,
	GPIO105_GPIO	| MFP_LPM_DRIVE_HIGH,	/* MMC/SD power */
	GPIO53_GPIO,				/* PC card reset */
	GPIO102_GPIO,				/* WLAN reset */

	/* NAND controls */
	GPIO11_GPIO	| MFP_LPM_DRIVE_HIGH,	/* NAND CE# */
	GPIO89_GPIO,				/* NAND Ready/Busy */

	/* interrupts */
	GPIO10_GPIO,	/* DM9000 interrupt */
	GPIO83_GPIO,	/* MMC card detect */
	GPIO95_GPIO,	/* WLAN interrupt */
};

/* V3020 RTC */
#if defined(CONFIG_RTC_DRV_V3020) || defined(CONFIG_RTC_DRV_V3020_MODULE)
static struct resource cmx270_v3020_resource[] = {
	[0] = {
		.start = RTC_PHYS_BASE,
		.end   = RTC_PHYS_BASE + 4,
		.flags = IORESOURCE_MEM,
	},
};

struct v3020_platform_data cmx270_v3020_pdata = {
	.leftshift = 16,
};

static struct platform_device cmx270_rtc_device = {
	.name		= "v3020",
	.num_resources	= ARRAY_SIZE(cmx270_v3020_resource),
	.resource	= cmx270_v3020_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &cmx270_v3020_pdata,
	}
};

static void __init cmx270_init_rtc(void)
{
	platform_device_register(&cmx270_rtc_device);
}
#else
static inline void cmx270_init_rtc(void) {}
#endif

/* 2700G graphics */
#if defined(CONFIG_FB_MBX) || defined(CONFIG_FB_MBX_MODULE)
static u64 fb_dma_mask = ~(u64)0;

static struct resource cmx270_2700G_resource[] = {
	/* frame buffer memory including ODFB and External SDRAM */
	[0] = {
		.start = PXA_CS2_PHYS,
		.end   = PXA_CS2_PHYS + 0x01ffffff,
		.flags = IORESOURCE_MEM,
	},
	/* Marathon registers */
	[1] = {
		.start = PXA_CS2_PHYS + 0x03fe0000,
		.end   = PXA_CS2_PHYS + 0x03ffffff,
		.flags = IORESOURCE_MEM,
	},
};

static unsigned long cmx270_marathon_on[] = {
	GPIO58_GPIO,
	GPIO59_GPIO,
	GPIO60_GPIO,
	GPIO61_GPIO,
	GPIO62_GPIO,
	GPIO63_GPIO,
	GPIO64_GPIO,
	GPIO65_GPIO,
	GPIO66_GPIO,
	GPIO67_GPIO,
	GPIO68_GPIO,
	GPIO69_GPIO,
	GPIO70_GPIO,
	GPIO71_GPIO,
	GPIO72_GPIO,
	GPIO73_GPIO,
	GPIO74_GPIO,
	GPIO75_GPIO,
	GPIO76_GPIO,
	GPIO77_GPIO,
};

static unsigned long cmx270_marathon_off[] = {
	GPIOxx_LCD_TFT_16BPP,
};

static int cmx270_marathon_probe(struct fb_info *fb)
{
	int gpio, err;

	for (gpio = 58; gpio <= 77; gpio++) {
		err = gpio_request(gpio, "LCD");
		if (err)
			return err;
		gpio_direction_input(gpio);
	}

	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx270_marathon_on));
	return 0;
}

static int cmx270_marathon_remove(struct fb_info *fb)
{
	int gpio;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx270_marathon_off));

	for (gpio = 58; gpio <= 77; gpio++)
		gpio_free(gpio);

	return 0;
}

static struct mbxfb_platform_data cmx270_2700G_data = {
	.xres = {
		.min = 240,
		.max = 1200,
		.defval = 640,
	},
	.yres = {
		.min = 240,
		.max = 1200,
		.defval = 480,
	},
	.bpp = {
		.min = 16,
		.max = 32,
		.defval = 16,
	},
	.memsize = 8*1024*1024,
	.probe = cmx270_marathon_probe,
	.remove = cmx270_marathon_remove,
};

static struct platform_device cmx270_2700G = {
	.name		= "mbx-fb",
	.dev		= {
		.platform_data	= &cmx270_2700G_data,
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(cmx270_2700G_resource),
	.resource	= cmx270_2700G_resource,
	.id		= -1,
};

static void __init cmx270_init_2700G(void)
{
	platform_device_register(&cmx270_2700G);
}
#else
static inline void cmx270_init_2700G(void) {}
#endif

/* PXA27x OHCI controller setup */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data cmx270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | POWER_CONTROL_LOW,
};

static void __init cmx270_init_ohci(void)
{
	pxa_set_ohci_info(&cmx270_ohci_platform_data);
}
#else
static inline void cmx270_init_ohci(void) {}
#endif

#if defined(CONFIG_MMC) || defined(CONFIG_MMC_MODULE)
static struct pxamci_platform_data cmx270_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.gpio_card_detect	= GPIO83_MMC_IRQ,
	.gpio_card_ro		= -1,
	.gpio_power		= GPIO105_MMC_POWER,
	.gpio_power_invert	= 1,
};

static void __init cmx270_init_mmc(void)
{
	pxa_set_mci_info(&cmx270_mci_platform_data);
}
#else
static inline void cmx270_init_mmc(void) {}
#endif

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
static struct pxa2xx_spi_master cm_x270_spi_info = {
	.num_chipselect	= 1,
	.enable_dma	= 1,
};

static struct pxa2xx_spi_chip cm_x270_libertas_chip = {
	.rx_threshold	= 1,
	.tx_threshold	= 1,
	.timeout	= 1000,
	.gpio_cs	= 14,
};

static unsigned long cm_x270_libertas_pin_config[] = {
	/* SSP2 */
	GPIO19_SSP2_SCLK,
	GPIO14_GPIO,
	GPIO87_SSP2_TXD,
	GPIO88_SSP2_RXD,

};

static int cm_x270_libertas_setup(struct spi_device *spi)
{
	int err = gpio_request(GPIO19_WLAN_STRAP, "WLAN STRAP");
	if (err)
		return err;

	err = gpio_request(GPIO102_WLAN_RST, "WLAN RST");
	if (err)
		goto err_free_strap;

	err = gpio_direction_output(GPIO102_WLAN_RST, 0);
	if (err)
		goto err_free_strap;
	msleep(100);

	err = gpio_direction_output(GPIO19_WLAN_STRAP, 1);
	if (err)
		goto err_free_strap;
	msleep(100);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(cm_x270_libertas_pin_config));

	gpio_set_value(GPIO102_WLAN_RST, 1);
	msleep(100);

	spi->bits_per_word = 16;
	spi_setup(spi);

	return 0;

err_free_strap:
	gpio_free(GPIO19_WLAN_STRAP);

	return err;
}

static int cm_x270_libertas_teardown(struct spi_device *spi)
{
	gpio_set_value(GPIO102_WLAN_RST, 0);
	gpio_free(GPIO102_WLAN_RST);
	gpio_free(GPIO19_WLAN_STRAP);

	return 0;
}

struct libertas_spi_platform_data cm_x270_libertas_pdata = {
	.use_dummy_writes	= 1,
	.setup			= cm_x270_libertas_setup,
	.teardown		= cm_x270_libertas_teardown,
};

static struct spi_board_info cm_x270_spi_devices[] __initdata = {
	{
		.modalias		= "libertas_spi",
		.max_speed_hz		= 13000000,
		.bus_num		= 2,
		.irq			= gpio_to_irq(95),
		.chip_select		= 0,
		.controller_data	= &cm_x270_libertas_chip,
		.platform_data		= &cm_x270_libertas_pdata,
	},
};

static void __init cmx270_init_spi(void)
{
	pxa2xx_set_spi_info(2, &cm_x270_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(cm_x270_spi_devices));
}
#else
static inline void cmx270_init_spi(void) {}
#endif

void __init cmx270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx270_pin_config));

#ifdef CONFIG_PM
	pxa27x_set_pwrmode(PWRMODE_DEEPSLEEP);
#endif

	cmx270_init_rtc();
	cmx270_init_mmc();
	cmx270_init_ohci();
	cmx270_init_2700G();
	cmx270_init_spi();
}
