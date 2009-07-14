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

#include <linux/rtc-v3020.h>
#include <video/mbxfb.h>

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
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* SSP1 */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* SSP2 */
	GPIO19_SSP2_SCLK,
	GPIO14_SSP2_SFRM,
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

	/* NAND controls */
	GPIO11_GPIO	| MFP_LPM_DRIVE_HIGH,	/* NAND CE# */
	GPIO89_GPIO,				/* NAND Ready/Busy */

	/* interrupts */
	GPIO10_GPIO,	/* DM9000 interrupt */
	GPIO83_GPIO,	/* MMC card detect */
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

static unsigned long save_lcd_regs[10];

static int cmx270_marathon_probe(struct fb_info *fb)
{
	/* save PXA-270 pin settings before enabling 2700G */
	save_lcd_regs[0] = GPDR1;
	save_lcd_regs[1] = GPDR2;
	save_lcd_regs[2] = GAFR1_U;
	save_lcd_regs[3] = GAFR2_L;
	save_lcd_regs[4] = GAFR2_U;

	/* Disable PXA-270 on-chip controller driving pins */
	GPDR1 &= ~(0xfc000000);
	GPDR2 &= ~(0x00c03fff);
	GAFR1_U &= ~(0xfff00000);
	GAFR2_L &= ~(0x0fffffff);
	GAFR2_U &= ~(0x0000f000);
	return 0;
}

static int cmx270_marathon_remove(struct fb_info *fb)
{
	GPDR1 =   save_lcd_regs[0];
	GPDR2 =   save_lcd_regs[1];
	GAFR1_U = save_lcd_regs[2];
	GAFR2_L = save_lcd_regs[3];
	GAFR2_U = save_lcd_regs[4];
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
static int cmx270_mci_init(struct device *dev,
			   irq_handler_t cmx270_detect_int,
			   void *data)
{
	int err;

	err = gpio_request(GPIO105_MMC_POWER, "MMC/SD power");
	if (err) {
		dev_warn(dev, "power gpio unavailable\n");
		return err;
	}

	gpio_direction_output(GPIO105_MMC_POWER, 0);

	err = request_irq(CMX270_MMC_IRQ, cmx270_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err) {
		gpio_free(GPIO105_MMC_POWER);
		dev_err(dev, "cmx270_mci_init: MMC/SD: can't"
			" request MMC card detect IRQ\n");
	}

	return err;
}

static void cmx270_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;

	if ((1 << vdd) & p_d->ocr_mask) {
		dev_dbg(dev, "power on\n");
		gpio_set_value(GPIO105_MMC_POWER, 0);
	} else {
		gpio_set_value(GPIO105_MMC_POWER, 1);
		dev_dbg(dev, "power off\n");
	}
}

static void cmx270_mci_exit(struct device *dev, void *data)
{
	free_irq(CMX270_MMC_IRQ, data);
	gpio_free(GPIO105_MMC_POWER);
}

static struct pxamci_platform_data cmx270_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= cmx270_mci_init,
	.setpower 	= cmx270_mci_setpower,
	.exit		= cmx270_mci_exit,
};

static void __init cmx270_init_mmc(void)
{
	pxa_set_mci_info(&cmx270_mci_platform_data);
}
#else
static inline void cmx270_init_mmc(void) {}
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
}
