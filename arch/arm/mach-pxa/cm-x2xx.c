/*
 * linux/arch/arm/mach-pxa/cm-x2xx.c
 *
 * Copyright (C) 2008 CompuLab, Ltd.
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
#include <linux/leds.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/pxa2xx-regs.h>
#include <mach/audio.h>
#include <mach/pxafb.h>
#include <mach/smemc.h>

#include <asm/hardware/it8152.h>

#include "generic.h"
#include "cm-x2xx-pci.h"

extern void cmx255_init(void);
extern void cmx270_init(void);

/* reserve IRQs for IT8152 */
#define CMX2XX_NR_IRQS		(IRQ_BOARD_START + 40)

/* virtual addresses for statically mapped regions */
#define CMX2XX_VIRT_BASE	(0xe8000000)
#define CMX2XX_IT8152_VIRT	(CMX2XX_VIRT_BASE)

/* physical address if local-bus attached devices */
#define CMX255_DM9000_PHYS_BASE (PXA_CS1_PHYS + (8 << 22))
#define CMX270_DM9000_PHYS_BASE	(PXA_CS1_PHYS + (6 << 22))

/* leds */
#define CMX255_GPIO_RED		(27)
#define CMX255_GPIO_GREEN	(32)
#define CMX270_GPIO_RED		(93)
#define CMX270_GPIO_GREEN	(94)

/* GPIO IRQ usage */
#define GPIO22_ETHIRQ		(22)
#define GPIO10_ETHIRQ		(10)
#define CMX255_GPIO_IT8152_IRQ	(0)
#define CMX270_GPIO_IT8152_IRQ	(22)

#define CMX255_ETHIRQ		IRQ_GPIO(GPIO22_ETHIRQ)
#define CMX270_ETHIRQ		IRQ_GPIO(GPIO10_ETHIRQ)

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource cmx255_dm9000_resource[] = {
	[0] = {
		.start = CMX255_DM9000_PHYS_BASE,
		.end   = CMX255_DM9000_PHYS_BASE + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CMX255_DM9000_PHYS_BASE + 4,
		.end   = CMX255_DM9000_PHYS_BASE + 4 + 500,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = CMX255_ETHIRQ,
		.end   = CMX255_ETHIRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct resource cmx270_dm9000_resource[] = {
	[0] = {
		.start = CMX270_DM9000_PHYS_BASE,
		.end   = CMX270_DM9000_PHYS_BASE + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CMX270_DM9000_PHYS_BASE + 8,
		.end   = CMX270_DM9000_PHYS_BASE + 8 + 500,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = CMX270_ETHIRQ,
		.end   = CMX270_ETHIRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data cmx270_dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY | DM9000_PLATF_NO_EEPROM,
};

static struct platform_device cmx2xx_dm9000_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cmx270_dm9000_resource),
	.dev		= {
		.platform_data = &cmx270_dm9000_platdata,
	}
};

static void __init cmx2xx_init_dm9000(void)
{
	if (cpu_is_pxa25x())
		cmx2xx_dm9000_device.resource = cmx255_dm9000_resource;
	else
		cmx2xx_dm9000_device.resource = cmx270_dm9000_resource;
	platform_device_register(&cmx2xx_dm9000_device);
}
#else
static inline void cmx2xx_init_dm9000(void) {}
#endif

/* UCB1400 touchscreen controller */
#if defined(CONFIG_TOUCHSCREEN_UCB1400) || defined(CONFIG_TOUCHSCREEN_UCB1400_MODULE)
static struct platform_device cmx2xx_ts_device = {
	.name		= "ucb1400_core",
	.id		= -1,
};

static void __init cmx2xx_init_touchscreen(void)
{
	platform_device_register(&cmx2xx_ts_device);
}
#else
static inline void cmx2xx_init_touchscreen(void) {}
#endif

/* CM-X270 LEDs */
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led cmx2xx_leds[] = {
	[0] = {
		.name = "cm-x2xx:red",
		.default_trigger = "nand-disk",
		.active_low = 1,
	},
	[1] = {
		.name = "cm-x2xx:green",
		.default_trigger = "heartbeat",
		.active_low = 1,
	},
};

static struct gpio_led_platform_data cmx2xx_gpio_led_pdata = {
	.num_leds = ARRAY_SIZE(cmx2xx_leds),
	.leds = cmx2xx_leds,
};

static struct platform_device cmx2xx_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &cmx2xx_gpio_led_pdata,
	},
};

static void __init cmx2xx_init_leds(void)
{
	if (cpu_is_pxa25x()) {
		cmx2xx_leds[0].gpio = CMX255_GPIO_RED;
		cmx2xx_leds[1].gpio = CMX255_GPIO_GREEN;
	} else {
		cmx2xx_leds[0].gpio = CMX270_GPIO_RED;
		cmx2xx_leds[1].gpio = CMX270_GPIO_GREEN;
	}
	platform_device_register(&cmx2xx_led_device);
}
#else
static inline void cmx2xx_init_leds(void) {}
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
	.lcd_conn	= LCD_COLOR_STN_8BPP | LCD_PCLK_EDGE_FALL |\
			  LCD_AC_BIAS_FREQ(0xff),
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
	.lcd_conn	= LCD_COLOR_TFT_8BPP | LCD_PCLK_EDGE_FALL |\
			  LCD_AC_BIAS_FREQ(0xff),
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
	.lcd_conn	= LCD_COLOR_TFT_8BPP | LCD_AC_BIAS_FREQ(0xff),
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
	.lcd_conn	= LCD_COLOR_TFT_8BPP | LCD_AC_BIAS_FREQ(0xff),
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
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_AC_BIAS_FREQ(0xff),
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
	.lcd_conn	= LCD_COLOR_STN_8BPP | LCD_AC_BIAS_FREQ(0xff),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
};

static struct pxafb_mach_info *cmx2xx_display = &generic_crt_640x480;

static int __init cmx2xx_set_display(char *str)
{
	int disp_type = simple_strtol(str, NULL, 0);
	switch (disp_type) {
	case MTYPE_STN320x240:
		cmx2xx_display = &generic_stn_320x240;
		break;
	case MTYPE_TFT640x480:
		cmx2xx_display = &generic_tft_640x480;
		break;
	case MTYPE_CRT640x480:
		cmx2xx_display = &generic_crt_640x480;
		break;
	case MTYPE_CRT800x600:
		cmx2xx_display = &generic_crt_800x600;
		break;
	case MTYPE_TFT320x240:
		cmx2xx_display = &generic_tft_320x240;
		break;
	case MTYPE_STN640x480:
		cmx2xx_display = &generic_stn_640x480;
		break;
	default: /* fallback to CRT 640x480 */
		cmx2xx_display = &generic_crt_640x480;
		break;
	}
	return 1;
}

/*
   This should be done really early to get proper configuration for
   frame buffer.
   Indeed, pxafb parameters can be used istead, but CM-X2XX bootloader
   has limitied line length for kernel command line, and also it will
   break compatibitlty with proprietary releases already in field.
*/
__setup("monitor=", cmx2xx_set_display);

static void __init cmx2xx_init_display(void)
{
	pxa_set_fb_info(NULL, cmx2xx_display);
}
#else
static inline void cmx2xx_init_display(void) {}
#endif

#ifdef CONFIG_PM
static unsigned long sleep_save_msc[10];

static int cmx2xx_suspend(struct sys_device *dev, pm_message_t state)
{
	cmx2xx_pci_suspend();

	/* save MSC registers */
	sleep_save_msc[0] = __raw_readl(MSC0);
	sleep_save_msc[1] = __raw_readl(MSC1);
	sleep_save_msc[2] = __raw_readl(MSC2);

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

static int cmx2xx_resume(struct sys_device *dev)
{
	cmx2xx_pci_resume();

	/* restore MSC registers */
	__raw_writel(sleep_save_msc[0], MSC0);
	__raw_writel(sleep_save_msc[1], MSC1);
	__raw_writel(sleep_save_msc[2], MSC2);

	return 0;
}

static struct sysdev_class cmx2xx_pm_sysclass = {
	.name = "pm",
	.resume = cmx2xx_resume,
	.suspend = cmx2xx_suspend,
};

static struct sys_device cmx2xx_pm_device = {
	.cls = &cmx2xx_pm_sysclass,
};

static int __init cmx2xx_pm_init(void)
{
	int error;
	error = sysdev_class_register(&cmx2xx_pm_sysclass);
	if (error == 0)
		error = sysdev_register(&cmx2xx_pm_device);
	return error;
}
#else
static int __init cmx2xx_pm_init(void) { return 0; }
#endif

#if defined(CONFIG_SND_PXA2XX_AC97) || defined(CONFIG_SND_PXA2XX_AC97_MODULE)
static void __init cmx2xx_init_ac97(void)
{
	pxa_set_ac97_info(NULL);
}
#else
static inline void cmx2xx_init_ac97(void) {}
#endif

static void __init cmx2xx_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	cmx2xx_pm_init();

	if (cpu_is_pxa25x())
		cmx255_init();
	else
		cmx270_init();

	cmx2xx_init_dm9000();
	cmx2xx_init_display();
	cmx2xx_init_ac97();
	cmx2xx_init_touchscreen();
	cmx2xx_init_leds();
}

static void __init cmx2xx_init_irq(void)
{
	if (cpu_is_pxa25x()) {
		pxa25x_init_irq();
		cmx2xx_pci_init_irq(CMX255_GPIO_IT8152_IRQ);
	} else {
		pxa27x_init_irq();
		cmx2xx_pci_init_irq(CMX270_GPIO_IT8152_IRQ);
	}
}

#ifdef CONFIG_PCI
/* Map PCI companion statically */
static struct map_desc cmx2xx_io_desc[] __initdata = {
	[0] = { /* PCI bridge */
		.virtual	= CMX2XX_IT8152_VIRT,
		.pfn		= __phys_to_pfn(PXA_CS4_PHYS),
		.length		= SZ_64M,
		.type		= MT_DEVICE
	},
};

static void __init cmx2xx_map_io(void)
{
	if (cpu_is_pxa25x())
		pxa25x_map_io();

	if (cpu_is_pxa27x())
		pxa27x_map_io();

	iotable_init(cmx2xx_io_desc, ARRAY_SIZE(cmx2xx_io_desc));

	it8152_base_address = CMX2XX_IT8152_VIRT;
}
#else
static void __init cmx2xx_map_io(void)
{
	if (cpu_is_pxa25x())
		pxa25x_map_io();

	if (cpu_is_pxa27x())
		pxa27x_map_io();
}
#endif

MACHINE_START(ARMCORE, "Compulab CM-X2XX")
	.boot_params	= 0xa0000100,
	.map_io		= cmx2xx_map_io,
	.nr_irqs	= CMX2XX_NR_IRQS,
	.init_irq	= cmx2xx_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= cmx2xx_init,
MACHINE_END
