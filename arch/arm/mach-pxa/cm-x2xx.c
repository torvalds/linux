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

#include <linux/dm9000.h>
#include <linux/rtc-v3020.h>
#include <video/mbxfb.h>
#include <linux/leds.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa27x.h>
#include <mach/pxa-regs.h>
#include <mach/audio.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/mmc.h>
#include <mach/bitfield.h>

#include <asm/hardware/it8152.h>

#include "generic.h"
#include "cm-x2xx-pci.h"

/* virtual addresses for statically mapped regions */
#define CMX270_VIRT_BASE	(0xe8000000)
#define CMX270_IT8152_VIRT	(CMX270_VIRT_BASE)

#define RTC_PHYS_BASE		(PXA_CS1_PHYS + (5 << 22))
#define DM9000_PHYS_BASE	(PXA_CS1_PHYS + (6 << 22))

/* GPIO IRQ usage */
#define GPIO10_ETHIRQ		(10)
#define GPIO22_IT8152_IRQ	(22)
#define GPIO83_MMC_IRQ		(83)
#define GPIO95_GFXIRQ		(95)

#define CMX270_ETHIRQ		IRQ_GPIO(GPIO10_ETHIRQ)
#define CMX270_IT8152_IRQ	IRQ_GPIO(GPIO22_IT8152_IRQ)
#define CMX270_MMC_IRQ		IRQ_GPIO(GPIO83_MMC_IRQ)
#define CMX270_GFXIRQ		IRQ_GPIO(GPIO95_GFXIRQ)

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

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource cmx270_dm9000_resource[] = {
	[0] = {
		.start = DM9000_PHYS_BASE,
		.end   = DM9000_PHYS_BASE + 4,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DM9000_PHYS_BASE + 8,
		.end   = DM9000_PHYS_BASE + 8 + 500,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = CMX270_ETHIRQ,
		.end   = CMX270_ETHIRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data cmx270_dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

static struct platform_device cmx270_dm9000_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cmx270_dm9000_resource),
	.resource	= cmx270_dm9000_resource,
	.dev		= {
		.platform_data = &cmx270_dm9000_platdata,
	}
};

static void __init cmx270_init_dm9000(void)
{
	platform_device_register(&cmx270_dm9000_device);
}
#else
static inline void cmx270_init_dm9000(void) {}
#endif

/* UCB1400 touchscreen controller */
#if defined(CONFIG_TOUCHSCREEN_UCB1400) || defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static struct platform_device cmx270_ts_device = {
	.name		= "ucb1400_ts",
	.id		= -1,
};

static void __init cmx270_init_touchscreen(void)
{
	platform_device_register(&cmx270_ts_device);
}
#else
static inline void cmx270_init_touchscreen(void) {}
#endif

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

/* CM-X270 LEDs */
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led cmx270_leds[] = {
	[0] = {
		.name = "cm-x270:red",
		.default_trigger = "nand-disk",
		.gpio = 93,
		.active_low = 1,
	},
	[1] = {
		.name = "cm-x270:green",
		.default_trigger = "heartbeat",
		.gpio = 94,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data cmx270_gpio_led_pdata = {
	.num_leds = ARRAY_SIZE(cmx270_leds),
	.leds = cmx270_leds,
};

static struct platform_device cmx270_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &cmx270_gpio_led_pdata,
	},
};

static void __init cmx270_init_leds(void)
{
	platform_device_register(&cmx270_led_device);
}
#else
static inline void cmx270_init_leds(void) {}
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

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
/*
  Display definitions
  keep these for backwards compatibility, although symbolic names (as
  e.g. in lpd270.c) looks better
*/
#define MTYPE_STN320x240	0
#define MTYPE_TFT640x480	1
#define MTYPE_CRT640x480	2
#define MTYPE_CRT800x600	3
#define MTYPE_TFT320x240	6
#define MTYPE_STN640x480	7

static struct pxafb_mode_info generic_stn_320x240_mode = {
	.pixclock	= 76923,
	.bpp		= 8,
	.xres		= 320,
	.yres		= 240,
	.hsync_len	= 3,
	.vsync_len	= 2,
	.left_margin	= 3,
	.upper_margin	= 0,
	.right_margin	= 3,
	.lower_margin	= 0,
	.sync		= (FB_SYNC_HOR_HIGH_ACT |
			   FB_SYNC_VERT_HIGH_ACT),
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_stn_320x240 = {
	.modes		= &generic_stn_320x240_mode,
	.num_modes	= 1,
	.lccr0		= 0,
	.lccr3		= (LCCR3_PixClkDiv(0x03) |
			   LCCR3_Acb(0xff) |
			   LCCR3_PCP),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mode_info generic_tft_640x480_mode = {
	.pixclock	= 38461,
	.bpp		= 8,
	.xres		= 640,
	.yres		= 480,
	.hsync_len	= 60,
	.vsync_len	= 2,
	.left_margin	= 70,
	.upper_margin	= 10,
	.right_margin	= 70,
	.lower_margin	= 5,
	.sync		= 0,
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_tft_640x480 = {
	.modes		= &generic_tft_640x480_mode,
	.num_modes	= 1,
	.lccr0		= (LCCR0_PAS),
	.lccr3		= (LCCR3_PixClkDiv(0x01) |
			   LCCR3_Acb(0xff) |
			   LCCR3_PCP),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mode_info generic_crt_640x480_mode = {
	.pixclock	= 38461,
	.bpp		= 8,
	.xres		= 640,
	.yres		= 480,
	.hsync_len	= 63,
	.vsync_len	= 2,
	.left_margin	= 81,
	.upper_margin	= 33,
	.right_margin	= 16,
	.lower_margin	= 10,
	.sync		= (FB_SYNC_HOR_HIGH_ACT |
			   FB_SYNC_VERT_HIGH_ACT),
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_crt_640x480 = {
	.modes		= &generic_crt_640x480_mode,
	.num_modes	= 1,
	.lccr0		= (LCCR0_PAS),
	.lccr3		= (LCCR3_PixClkDiv(0x01) |
			   LCCR3_Acb(0xff)),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mode_info generic_crt_800x600_mode = {
	.pixclock	= 28846,
	.bpp		= 8,
	.xres		= 800,
	.yres	  	= 600,
	.hsync_len	= 63,
	.vsync_len	= 2,
	.left_margin	= 26,
	.upper_margin	= 21,
	.right_margin	= 26,
	.lower_margin	= 11,
	.sync		= (FB_SYNC_HOR_HIGH_ACT |
			   FB_SYNC_VERT_HIGH_ACT),
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_crt_800x600 = {
	.modes		= &generic_crt_800x600_mode,
	.num_modes	= 1,
	.lccr0		= (LCCR0_PAS),
	.lccr3		= (LCCR3_PixClkDiv(0x02) |
			   LCCR3_Acb(0xff)),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mode_info generic_tft_320x240_mode = {
	.pixclock	= 134615,
	.bpp		= 16,
	.xres		= 320,
	.yres		= 240,
	.hsync_len	= 63,
	.vsync_len	= 7,
	.left_margin	= 75,
	.upper_margin	= 0,
	.right_margin	= 15,
	.lower_margin	= 15,
	.sync		= 0,
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_tft_320x240 = {
	.modes		= &generic_tft_320x240_mode,
	.num_modes	= 1,
	.lccr0		= (LCCR0_PAS),
	.lccr3		= (LCCR3_PixClkDiv(0x06) |
			   LCCR3_Acb(0xff) |
			   LCCR3_PCP),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mode_info generic_stn_640x480_mode = {
	.pixclock	= 57692,
	.bpp		= 8,
	.xres		= 640,
	.yres		= 480,
	.hsync_len	= 4,
	.vsync_len	= 2,
	.left_margin	= 10,
	.upper_margin	= 5,
	.right_margin	= 10,
	.lower_margin	= 5,
	.sync		= (FB_SYNC_HOR_HIGH_ACT |
			   FB_SYNC_VERT_HIGH_ACT),
	.cmap_greyscale = 0,
};

static struct pxafb_mach_info generic_stn_640x480 = {
	.modes		= &generic_stn_640x480_mode,
	.num_modes	= 1,
	.lccr0		= 0,
	.lccr3		= (LCCR3_PixClkDiv(0x02) |
			   LCCR3_Acb(0xff)),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mach_info *cmx270_display = &generic_crt_640x480;

static int __init cmx270_set_display(char *str)
{
	int disp_type = simple_strtol(str, NULL, 0);
	switch (disp_type) {
	case MTYPE_STN320x240:
		cmx270_display = &generic_stn_320x240;
		break;
	case MTYPE_TFT640x480:
		cmx270_display = &generic_tft_640x480;
		break;
	case MTYPE_CRT640x480:
		cmx270_display = &generic_crt_640x480;
		break;
	case MTYPE_CRT800x600:
		cmx270_display = &generic_crt_800x600;
		break;
	case MTYPE_TFT320x240:
		cmx270_display = &generic_tft_320x240;
		break;
	case MTYPE_STN640x480:
		cmx270_display = &generic_stn_640x480;
		break;
	default: /* fallback to CRT 640x480 */
		cmx270_display = &generic_crt_640x480;
		break;
	}
	return 1;
}

/*
   This should be done really early to get proper configuration for
   frame buffer.
   Indeed, pxafb parameters can be used istead, but CM-X270 bootloader
   has limitied line length for kernel command line, and also it will
   break compatibitlty with proprietary releases already in field.
*/
__setup("monitor=", cmx270_set_display);

static void __init cmx270_init_display(void)
{
	set_pxa_fb_info(cmx270_display);
}
#else
static inline void cmx270_init_display(void) {}
#endif

/* PXA27x OHCI controller setup */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int cmx270_ohci_init(struct device *dev)
{
	/* Set the Power Control Polarity Low */
	UHCHR = (UHCHR | UHCHR_PCPL) &
		~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSE);

	return 0;
}

static struct pxaohci_platform_data cmx270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.init		= cmx270_ohci_init,
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

#ifdef CONFIG_PM
static unsigned long sleep_save_msc[10];

static int cmx270_suspend(struct sys_device *dev, pm_message_t state)
{
	cmx270_pci_suspend();

	/* save MSC registers */
	sleep_save_msc[0] = MSC0;
	sleep_save_msc[1] = MSC1;
	sleep_save_msc[2] = MSC2;

	/* setup power saving mode registers */
	PCFR = 0x0;
	PSLR = 0xff400000;
	PMCR  = 0x00000005;
	PWER  = 0x80000000;
	PFER  = 0x00000000;
	PRER  = 0x00000000;
	PGSR0 = 0xC0018800;
	PGSR1 = 0x004F0002;
	PGSR2 = 0x6021C000;
	PGSR3 = 0x00020000;

	return 0;
}

static int cmx270_resume(struct sys_device *dev)
{
	cmx270_pci_resume();

	/* restore MSC registers */
	MSC0 = sleep_save_msc[0];
	MSC1 = sleep_save_msc[1];
	MSC2 = sleep_save_msc[2];

	return 0;
}

static struct sysdev_class cmx270_pm_sysclass = {
	.name = "pm",
	.resume = cmx270_resume,
	.suspend = cmx270_suspend,
};

static struct sys_device cmx270_pm_device = {
	.cls = &cmx270_pm_sysclass,
};

static int __init cmx270_pm_init(void)
{
	int error;
	error = sysdev_class_register(&cmx270_pm_sysclass);
	if (error == 0)
		error = sysdev_register(&cmx270_pm_device);
	return error;
}
#else
static int __init cmx270_pm_init(void) { return 0; }
#endif

#if defined(CONFIG_SND_PXA2XX_AC97) || defined(CONFIG_SND_PXA2XX_AC97_MODULE)
static void __init cmx270_init_ac97(void)
{
	pxa_set_ac97_info(NULL);
}
#else
static inline void cmx270_init_ac97(void) {}
#endif

static void __init cmx270_init(void)
{
	cmx270_pm_init();

	pxa2xx_mfp_config(ARRAY_AND_SIZE(cmx270_pin_config));

	cmx270_init_dm9000();
	cmx270_init_rtc();
	cmx270_init_display();
	cmx270_init_mmc();
	cmx270_init_ohci();
	cmx270_init_ac97();
	cmx270_init_touchscreen();
	cmx270_init_leds();
	cmx270_init_2700G();
}

static void __init cmx270_init_irq(void)
{
	pxa27x_init_irq();

	cmx270_pci_init_irq(GPIO22_IT8152_IRQ);
}

#ifdef CONFIG_PCI
/* Map PCI companion statically */
static struct map_desc cmx270_io_desc[] __initdata = {
	[0] = { /* PCI bridge */
		.virtual	= CMX270_IT8152_VIRT,
		.pfn		= __phys_to_pfn(PXA_CS4_PHYS),
		.length		= SZ_64M,
		.type		= MT_DEVICE
	},
};

static void __init cmx270_map_io(void)
{
	pxa_map_io();
	iotable_init(cmx270_io_desc, ARRAY_SIZE(cmx270_io_desc));

	it8152_base_address = CMX270_IT8152_VIRT;
}
#else
static void __init cmx270_map_io(void)
{
	pxa_map_io();
}
#endif

MACHINE_START(ARMCORE, "Compulab CM-x270")
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= cmx270_map_io,
	.init_irq	= cmx270_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= cmx270_init,
MACHINE_END
