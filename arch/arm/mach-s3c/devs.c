// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Base Samsung platform device definitions

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/gfp.h>
#include <linux/mmc/host.h>
#include <linux/ioport.h>
#include <linux/sizes.h>
#include <linux/platform_data/s3c-hsotg.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "irqs.h"
#include "map.h"
#include "gpio-samsung.h"
#include "gpio-cfg.h"

#include "cpu.h"
#include "devs.h"
#include "fb.h"
#include <linux/platform_data/i2c-s3c2410.h>
#include "keypad.h"
#include "pwm-core.h"
#include "sdhci.h"
#include "usb-phy.h"
#include <linux/platform_data/asoc-s3c.h>
#include <linux/platform_data/spi-s3c64xx.h>

#define samsung_device_dma_mask (*((u64[]) { DMA_BIT_MASK(32) }))

/* FB */

#ifdef CONFIG_S3C_DEV_FB
static struct resource s3c_fb_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_FB, SZ_16K),
	[1] = DEFINE_RES_IRQ(IRQ_LCD_VSYNC),
	[2] = DEFINE_RES_IRQ(IRQ_LCD_FIFO),
	[3] = DEFINE_RES_IRQ(IRQ_LCD_SYSTEM),
};

struct platform_device s3c_device_fb = {
	.name		= "s3c-fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_fb_resource),
	.resource	= s3c_fb_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s3c_fb_set_platdata(struct s3c_fb_platdata *pd)
{
	s3c_set_platdata(pd, sizeof(struct s3c_fb_platdata),
			 &s3c_device_fb);
}
#endif /* CONFIG_S3C_DEV_FB */

/* HSMMC */

#ifdef CONFIG_S3C_DEV_HSMMC
static struct resource s3c_hsmmc_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_HSMMC0, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_HSMMC0),
};

struct s3c_sdhci_platdata s3c_hsmmc0_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
};

struct platform_device s3c_device_hsmmc0 = {
	.name		= "s3c-sdhci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc_resource),
	.resource	= s3c_hsmmc_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &s3c_hsmmc0_def_platdata,
	},
};

void s3c_sdhci0_set_platdata(struct s3c_sdhci_platdata *pd)
{
	s3c_sdhci_set_platdata(pd, &s3c_hsmmc0_def_platdata);
}
#endif /* CONFIG_S3C_DEV_HSMMC */

#ifdef CONFIG_S3C_DEV_HSMMC1
static struct resource s3c_hsmmc1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_HSMMC1, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_HSMMC1),
};

struct s3c_sdhci_platdata s3c_hsmmc1_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
};

struct platform_device s3c_device_hsmmc1 = {
	.name		= "s3c-sdhci",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc1_resource),
	.resource	= s3c_hsmmc1_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &s3c_hsmmc1_def_platdata,
	},
};

void s3c_sdhci1_set_platdata(struct s3c_sdhci_platdata *pd)
{
	s3c_sdhci_set_platdata(pd, &s3c_hsmmc1_def_platdata);
}
#endif /* CONFIG_S3C_DEV_HSMMC1 */

/* HSMMC2 */

#ifdef CONFIG_S3C_DEV_HSMMC2
static struct resource s3c_hsmmc2_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_HSMMC2, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_HSMMC2),
};

struct s3c_sdhci_platdata s3c_hsmmc2_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
};

struct platform_device s3c_device_hsmmc2 = {
	.name		= "s3c-sdhci",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc2_resource),
	.resource	= s3c_hsmmc2_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &s3c_hsmmc2_def_platdata,
	},
};

void s3c_sdhci2_set_platdata(struct s3c_sdhci_platdata *pd)
{
	s3c_sdhci_set_platdata(pd, &s3c_hsmmc2_def_platdata);
}
#endif /* CONFIG_S3C_DEV_HSMMC2 */

#ifdef CONFIG_S3C_DEV_HSMMC3
static struct resource s3c_hsmmc3_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_HSMMC3, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_HSMMC3),
};

struct s3c_sdhci_platdata s3c_hsmmc3_def_platdata = {
	.max_width	= 4,
	.host_caps	= (MMC_CAP_4_BIT_DATA |
			   MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
};

struct platform_device s3c_device_hsmmc3 = {
	.name		= "s3c-sdhci",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc3_resource),
	.resource	= s3c_hsmmc3_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &s3c_hsmmc3_def_platdata,
	},
};

void s3c_sdhci3_set_platdata(struct s3c_sdhci_platdata *pd)
{
	s3c_sdhci_set_platdata(pd, &s3c_hsmmc3_def_platdata);
}
#endif /* CONFIG_S3C_DEV_HSMMC3 */

/* I2C */

static struct resource s3c_i2c0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_IIC, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_IIC),
};

struct platform_device s3c_device_i2c0 = {
	.name		= "s3c2410-i2c",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_i2c0_resource),
	.resource	= s3c_i2c0_resource,
};

struct s3c2410_platform_i2c default_i2c_data __initdata = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
};

void __init s3c_i2c0_set_platdata(struct s3c2410_platform_i2c *pd)
{
	struct s3c2410_platform_i2c *npd;

	if (!pd) {
		pd = &default_i2c_data;
		pd->bus_num = 0;
	}

	npd = s3c_set_platdata(pd, sizeof(*npd), &s3c_device_i2c0);

	if (!npd->cfg_gpio)
		npd->cfg_gpio = s3c_i2c0_cfg_gpio;
}

#ifdef CONFIG_S3C_DEV_I2C1
static struct resource s3c_i2c1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_IIC1, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_IIC1),
};

struct platform_device s3c_device_i2c1 = {
	.name		= "s3c2410-i2c",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c_i2c1_resource),
	.resource	= s3c_i2c1_resource,
};

void __init s3c_i2c1_set_platdata(struct s3c2410_platform_i2c *pd)
{
	struct s3c2410_platform_i2c *npd;

	if (!pd) {
		pd = &default_i2c_data;
		pd->bus_num = 1;
	}

	npd = s3c_set_platdata(pd, sizeof(*npd), &s3c_device_i2c1);

	if (!npd->cfg_gpio)
		npd->cfg_gpio = s3c_i2c1_cfg_gpio;
}
#endif /* CONFIG_S3C_DEV_I2C1 */

/* KEYPAD */

#ifdef CONFIG_SAMSUNG_DEV_KEYPAD
static struct resource samsung_keypad_resources[] = {
	[0] = DEFINE_RES_MEM(SAMSUNG_PA_KEYPAD, SZ_32),
	[1] = DEFINE_RES_IRQ(IRQ_KEYPAD),
};

struct platform_device samsung_device_keypad = {
	.name		= "samsung-keypad",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(samsung_keypad_resources),
	.resource	= samsung_keypad_resources,
};

void __init samsung_keypad_set_platdata(struct samsung_keypad_platdata *pd)
{
	struct samsung_keypad_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(*npd), &samsung_device_keypad);

	if (!npd->cfg_gpio)
		npd->cfg_gpio = samsung_keypad_cfg_gpio;
}
#endif /* CONFIG_SAMSUNG_DEV_KEYPAD */

/* PWM Timer */

#ifdef CONFIG_SAMSUNG_DEV_PWM
static struct resource samsung_pwm_resource[] = {
	DEFINE_RES_MEM(SAMSUNG_PA_TIMER, SZ_4K),
};

struct platform_device samsung_device_pwm = {
	.name		= "samsung-pwm",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(samsung_pwm_resource),
	.resource	= samsung_pwm_resource,
};

void __init samsung_pwm_set_platdata(struct samsung_pwm_variant *pd)
{
	samsung_device_pwm.dev.platform_data = pd;
}
#endif /* CONFIG_SAMSUNG_DEV_PWM */

/* USB */

#ifdef CONFIG_S3C_DEV_USB_HOST
static struct resource s3c_usb_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_USBHOST, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_USBH),
};

struct platform_device s3c_device_ohci = {
	.name		= "s3c2410-ohci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_usb_resource),
	.resource	= s3c_usb_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_S3C_DEV_USB_HOST */

/* USB HSOTG */

#ifdef CONFIG_S3C_DEV_USB_HSOTG
static struct resource s3c_usb_hsotg_resources[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_USB_HSOTG, SZ_128K),
	[1] = DEFINE_RES_IRQ(IRQ_OTG),
};

struct platform_device s3c_device_usb_hsotg = {
	.name		= "s3c-hsotg",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_usb_hsotg_resources),
	.resource	= s3c_usb_hsotg_resources,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init dwc2_hsotg_set_platdata(struct dwc2_hsotg_plat *pd)
{
	struct dwc2_hsotg_plat *npd;

	npd = s3c_set_platdata(pd, sizeof(*npd), &s3c_device_usb_hsotg);

	if (!npd->phy_init)
		npd->phy_init = s3c_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s3c_usb_phy_exit;
}
#endif /* CONFIG_S3C_DEV_USB_HSOTG */

#ifdef CONFIG_S3C64XX_DEV_SPI0
static struct resource s3c64xx_spi0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_SPI0, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_SPI0),
};

struct platform_device s3c64xx_device_spi0 = {
	.name		= "s3c6410-spi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c64xx_spi0_resource),
	.resource	= s3c64xx_spi0_resource,
	.dev = {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s3c64xx_spi0_set_platdata(int src_clk_nr, int num_cs)
{
	struct s3c64xx_spi_info pd;

	/* Reject invalid configuration */
	if (!num_cs || src_clk_nr < 0) {
		pr_err("%s: Invalid SPI configuration\n", __func__);
		return;
	}

	pd.num_cs = num_cs;
	pd.src_clk_nr = src_clk_nr;
	pd.cfg_gpio = s3c64xx_spi0_cfg_gpio;

	s3c_set_platdata(&pd, sizeof(pd), &s3c64xx_device_spi0);
}
#endif /* CONFIG_S3C64XX_DEV_SPI0 */
