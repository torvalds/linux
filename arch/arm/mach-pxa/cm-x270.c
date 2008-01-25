/*
 * linux/arch/arm/mach-pxa/cm-x270.c
 *
 * Copyright (C) 2007 CompuLab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/pm.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/sysdev.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <linux/dm9000.h>
#include <linux/rtc-v3020.h>
#include <linux/serial_8250.h>

#include <video/mbxfb.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/ohci.h>
#include <asm/arch/mmc.h>
#include <asm/arch/bitfield.h>
#include <asm/arch/cm-x270.h>

#include <asm/hardware/it8152.h>

#include "generic.h"
#include "cm-x270-pci.h"

#define RTC_PHYS_BASE		(PXA_CS1_PHYS + (5 << 22))
#define DM9000_PHYS_BASE	(PXA_CS1_PHYS + (6 << 22))

static struct resource cmx270_dm9k_resource[] = {
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
		.flags = IORESOURCE_IRQ,
	}
};

/* for the moment we limit ourselves to 32bit IO until some
 * better IO routines can be written and tested
 */
static struct dm9000_plat_data cmx270_dm9k_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

/* Ethernet device */
static struct platform_device cmx270_device_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cmx270_dm9k_resource),
	.resource	= cmx270_dm9k_resource,
	.dev		= {
		.platform_data = &cmx270_dm9k_platdata,
	}
};

/* audio device */
static struct platform_device cmx270_audio_device = {
	.name		= "pxa2xx-ac97",
	.id		= -1,
};

/* touchscreen controller */
static struct platform_device cmx270_ts_device = {
	.name		= "ucb1400_ts",
	.id		= -1,
};

/* RTC */
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

/*
 * CM-X270 LEDs
 */
static struct platform_device cmx270_led_device = {
	.name		= "cm-x270-led",
	.id		= -1,
};

/* 2700G graphics */
static u64 fb_dma_mask = ~(u64)0;

static struct resource cmx270_2700G_resource[] = {
	/* frame buffer memory including ODFB and External SDRAM */
	[0] = {
		.start = MARATHON_PHYS,
		.end   = MARATHON_PHYS + 0x02000000,
		.flags = IORESOURCE_MEM,
	},
	/* Marathon registers */
	[1] = {
		.start = MARATHON_PHYS + 0x03fe0000,
		.end   = MARATHON_PHYS + 0x03ffffff,
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

static u64 ata_dma_mask = ~(u64)0;

static struct platform_device cmx270_ata = {
	.name = "pata_cm_x270",
	.id = -1,
	.dev		= {
		.dma_mask	= &ata_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
};

/* platform devices */
static struct platform_device *platform_devices[] __initdata = {
	&cmx270_device_dm9k,
	&cmx270_audio_device,
	&cmx270_rtc_device,
	&cmx270_2700G,
	&cmx270_led_device,
	&cmx270_ts_device,
	&cmx270_ata,
};

/* Map PCI companion and IDE/General Purpose CS statically */
static struct map_desc cmx270_io_desc[] __initdata = {
	[0] = { /* IDE/general purpose space */
		.virtual	= CMX270_IDE104_VIRT,
		.pfn		= __phys_to_pfn(CMX270_IDE104_PHYS),
		.length		= SZ_64M - SZ_8M,
		.type		= MT_DEVICE
	},
	[1] = { /* PCI bridge */
		.virtual	= CMX270_IT8152_VIRT,
		.pfn		= __phys_to_pfn(CMX270_IT8152_PHYS),
		.length		= SZ_64M,
		.type		= MT_DEVICE
	},
};

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

/* PXA27x OHCI controller setup */
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


static int cmx270_mci_init(struct device *dev,
			   irq_handler_t cmx270_detect_int,
			   void *data)
{
	int err;

	/*
	 * setup GPIO for PXA27x MMC controller
	 */
	pxa_gpio_mode(GPIO32_MMCCLK_MD);
	pxa_gpio_mode(GPIO112_MMCCMD_MD);
	pxa_gpio_mode(GPIO92_MMCDAT0_MD);
	pxa_gpio_mode(GPIO109_MMCDAT1_MD);
	pxa_gpio_mode(GPIO110_MMCDAT2_MD);
	pxa_gpio_mode(GPIO111_MMCDAT3_MD);

	/* SB-X270 uses GPIO105 as SD power enable */
	pxa_gpio_mode(105 | GPIO_OUT);

	/* card detect IRQ on GPIO 83 */
	pxa_gpio_mode(IRQ_TO_GPIO(CMX270_MMC_IRQ));
	set_irq_type(CMX270_MMC_IRQ, IRQT_FALLING);

	err = request_irq(CMX270_MMC_IRQ, cmx270_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "cmx270_mci_init: MMC/SD: can't"
		       " request MMC card detect IRQ\n");
		return -1;
	}

	return 0;
}

static void cmx270_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;

	if ((1 << vdd) & p_d->ocr_mask) {
		printk(KERN_DEBUG "%s: on\n", __FUNCTION__);
		GPCR(105) = GPIO_bit(105);
	} else {
		GPSR(105) = GPIO_bit(105);
		printk(KERN_DEBUG "%s: off\n", __FUNCTION__);
	}
}

static void cmx270_mci_exit(struct device *dev, void *data)
{
	free_irq(CMX270_MMC_IRQ, data);
}

static struct pxamci_platform_data cmx270_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= cmx270_mci_init,
	.setpower 	= cmx270_mci_setpower,
	.exit		= cmx270_mci_exit,
};

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

static void __init cmx270_init(void)
{
	cmx270_pm_init();

	set_pxa_fb_info(cmx270_display);

	/* register CM-X270 platform devices */
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	/* set MCI and OHCI platform parameters */
	pxa_set_mci_info(&cmx270_mci_platform_data);
	pxa_set_ohci_info(&cmx270_ohci_platform_data);

	/* This enables the STUART */
	pxa_gpio_mode(GPIO46_STRXD_MD);
	pxa_gpio_mode(GPIO47_STTXD_MD);

	/* This enables the BTUART  */
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);
}

static void __init cmx270_init_irq(void)
{
	pxa27x_init_irq();


	cmx270_pci_init_irq();

	/* Setup interrupt for dm9000 */
	pxa_gpio_mode(IRQ_TO_GPIO(CMX270_ETHIRQ));
	set_irq_type(CMX270_ETHIRQ, IRQT_RISING);

	/* Setup interrupt for 2700G */
	pxa_gpio_mode(IRQ_TO_GPIO(CMX270_GFXIRQ));
	set_irq_type(CMX270_GFXIRQ, IRQT_FALLING);
}

static void __init cmx270_map_io(void)
{
	pxa_map_io();
	iotable_init(cmx270_io_desc, ARRAY_SIZE(cmx270_io_desc));
}


MACHINE_START(ARMCORE, "Compulab CM-x270")
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= cmx270_map_io,
	.init_irq	= cmx270_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= cmx270_init,
MACHINE_END
