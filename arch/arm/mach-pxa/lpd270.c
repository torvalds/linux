/*
 * linux/arch/arm/mach-pxa/lpd270.c
 *
 * Support for the LogicPD PXA270 Card Engine.
 * Derived from the mainstone code, which carries these notices:
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 05, 2002
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/pwm_backlight.h>
#include <linux/smc91x.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/lpd270.h>
#include <mach/audio.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <mach/smemc.h>

#include "generic.h"
#include "devices.h"

static unsigned long lpd270_pin_config[] __initdata = {
	/* Chip Selects */
	GPIO15_nCS_1,	/* Mainboard Flash */
	GPIO78_nCS_2,	/* CPLD + Ethernet */

	/* LCD - 16bpp Active TFT */
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
	GPIO16_PWM0_OUT,	/* Backlight */

	/* USB Host */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO45_AC97_SYSCLK,

	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,
};

static unsigned int lpd270_irq_enabled;

static void lpd270_mask_irq(struct irq_data *d)
{
	int lpd270_irq = d->irq - LPD270_IRQ(0);

	__raw_writew(~(1 << lpd270_irq), LPD270_INT_STATUS);

	lpd270_irq_enabled &= ~(1 << lpd270_irq);
	__raw_writew(lpd270_irq_enabled, LPD270_INT_MASK);
}

static void lpd270_unmask_irq(struct irq_data *d)
{
	int lpd270_irq = d->irq - LPD270_IRQ(0);

	lpd270_irq_enabled |= 1 << lpd270_irq;
	__raw_writew(lpd270_irq_enabled, LPD270_INT_MASK);
}

static struct irq_chip lpd270_irq_chip = {
	.name		= "CPLD",
	.irq_ack	= lpd270_mask_irq,
	.irq_mask	= lpd270_mask_irq,
	.irq_unmask	= lpd270_unmask_irq,
};

static void lpd270_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending;

	pending = __raw_readw(LPD270_INT_STATUS) & lpd270_irq_enabled;
	do {
		/* clear useless edge notification */
		desc->irq_data.chip->irq_ack(&desc->irq_data);
		if (likely(pending)) {
			irq = LPD270_IRQ(0) + __ffs(pending);
			generic_handle_irq(irq);

			pending = __raw_readw(LPD270_INT_STATUS) &
						lpd270_irq_enabled;
		}
	} while (pending);
}

static void __init lpd270_init_irq(void)
{
	int irq;

	pxa27x_init_irq();

	__raw_writew(0, LPD270_INT_MASK);
	__raw_writew(0, LPD270_INT_STATUS);

	/* setup extra LogicPD PXA270 irqs */
	for (irq = LPD270_IRQ(2); irq <= LPD270_IRQ(4); irq++) {
		irq_set_chip_and_handler(irq, &lpd270_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	irq_set_chained_handler(PXA_GPIO_TO_IRQ(0), lpd270_irq_handler);
	irq_set_irq_type(PXA_GPIO_TO_IRQ(0), IRQ_TYPE_EDGE_FALLING);
}


#ifdef CONFIG_PM
static void lpd270_irq_resume(void)
{
	__raw_writew(lpd270_irq_enabled, LPD270_INT_MASK);
}

static struct syscore_ops lpd270_irq_syscore_ops = {
	.resume = lpd270_irq_resume,
};

static int __init lpd270_irq_device_init(void)
{
	if (machine_is_logicpd_pxa270()) {
		register_syscore_ops(&lpd270_irq_syscore_ops);
		return 0;
	}
	return -ENODEV;
}

device_initcall(lpd270_irq_device_init);
#endif


static struct resource smc91x_resources[] = {
	[0] = {
		.start	= LPD270_ETH_PHYS,
		.end	= (LPD270_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LPD270_ETHERNET_IRQ,
		.end	= LPD270_ETHERNET_IRQ,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

struct smc91x_platdata smc91x_platdata = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT;
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev.platform_data = &smc91x_platdata,
};

static struct resource lpd270_flash_resources[] = {
	[0] = {
		.start	= PXA_CS0_PHYS,
		.end	= PXA_CS0_PHYS + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_CS1_PHYS,
		.end	= PXA_CS1_PHYS + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mtd_partition lpd270_flash0_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	}, {
		.name =		"Kernel",
		.size =		0x00400000,
		.offset =	0x00040000,
	}, {
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00440000
	},
};

static struct flash_platform_data lpd270_flash_data[2] = {
	{
		.name		= "processor-flash",
		.map_name	= "cfi_probe",
		.parts		= lpd270_flash0_partitions,
		.nr_parts	= ARRAY_SIZE(lpd270_flash0_partitions),
	}, {
		.name		= "mainboard-flash",
		.map_name	= "cfi_probe",
		.parts		= NULL,
		.nr_parts	= 0,
	}
};

static struct platform_device lpd270_flash_device[2] = {
	{
		.name		= "pxa2xx-flash",
		.id		= 0,
		.dev = {
			.platform_data	= &lpd270_flash_data[0],
		},
		.resource	= &lpd270_flash_resources[0],
		.num_resources	= 1,
	}, {
		.name		= "pxa2xx-flash",
		.id		= 1,
		.dev = {
			.platform_data	= &lpd270_flash_data[1],
		},
		.resource	= &lpd270_flash_resources[1],
		.num_resources	= 1,
	},
};

static struct platform_pwm_backlight_data lpd270_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 1,
	.dft_brightness	= 1,
	.pwm_period_ns	= 78770,
	.enable_gpio	= -1,
};

static struct platform_device lpd270_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent	= &pxa27x_device_pwm0.dev,
		.platform_data = &lpd270_backlight_data,
	},
};

/* 5.7" TFT QVGA (LoLo display number 1) */
static struct pxafb_mode_info sharp_lq057q3dc02_mode = {
	.pixclock		= 150000,
	.xres			= 320,
	.yres			= 240,
	.bpp			= 16,
	.hsync_len		= 0x14,
	.left_margin		= 0x28,
	.right_margin		= 0x0a,
	.vsync_len		= 0x02,
	.upper_margin		= 0x08,
	.lower_margin		= 0x14,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq057q3dc02 = {
	.modes			= &sharp_lq057q3dc02_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

/* 12.1" TFT SVGA (LoLo display number 2) */
static struct pxafb_mode_info sharp_lq121s1dg31_mode = {
	.pixclock		= 50000,
	.xres			= 800,
	.yres			= 600,
	.bpp			= 16,
	.hsync_len		= 0x05,
	.left_margin		= 0x52,
	.right_margin		= 0x05,
	.vsync_len		= 0x04,
	.upper_margin		= 0x14,
	.lower_margin		= 0x0a,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq121s1dg31 = {
	.modes			= &sharp_lq121s1dg31_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

/* 3.6" TFT QVGA (LoLo display number 3) */
static struct pxafb_mode_info sharp_lq036q1da01_mode = {
	.pixclock		= 150000,
	.xres			= 320,
	.yres			= 240,
	.bpp			= 16,
	.hsync_len		= 0x0e,
	.left_margin		= 0x04,
	.right_margin		= 0x0a,
	.vsync_len		= 0x03,
	.upper_margin		= 0x03,
	.lower_margin		= 0x03,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq036q1da01 = {
	.modes			= &sharp_lq036q1da01_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

/* 6.4" TFT VGA (LoLo display number 5) */
static struct pxafb_mode_info sharp_lq64d343_mode = {
	.pixclock		= 25000,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 0x31,
	.left_margin		= 0x89,
	.right_margin		= 0x19,
	.vsync_len		= 0x12,
	.upper_margin		= 0x22,
	.lower_margin		= 0x00,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq64d343 = {
	.modes			= &sharp_lq64d343_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

/* 10.4" TFT VGA (LoLo display number 7) */
static struct pxafb_mode_info sharp_lq10d368_mode = {
	.pixclock		= 25000,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 0x31,
	.left_margin		= 0x89,
	.right_margin		= 0x19,
	.vsync_len		= 0x12,
	.upper_margin		= 0x22,
	.lower_margin		= 0x00,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq10d368 = {
	.modes			= &sharp_lq10d368_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

/* 3.5" TFT QVGA (LoLo display number 8) */
static struct pxafb_mode_info sharp_lq035q7db02_20_mode = {
	.pixclock		= 150000,
	.xres			= 240,
	.yres			= 320,
	.bpp			= 16,
	.hsync_len		= 0x0e,
	.left_margin		= 0x0a,
	.right_margin		= 0x0a,
	.vsync_len		= 0x03,
	.upper_margin		= 0x05,
	.lower_margin		= 0x14,
	.sync			= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info sharp_lq035q7db02_20 = {
	.modes			= &sharp_lq035q7db02_20_mode,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL |
				  LCD_ALTERNATE_MAPPING,
};

static struct pxafb_mach_info *lpd270_lcd_to_use;

static int __init lpd270_set_lcd(char *str)
{
	if (!strncasecmp(str, "lq057q3dc02", 11)) {
		lpd270_lcd_to_use = &sharp_lq057q3dc02;
	} else if (!strncasecmp(str, "lq121s1dg31", 11)) {
		lpd270_lcd_to_use = &sharp_lq121s1dg31;
	} else if (!strncasecmp(str, "lq036q1da01", 11)) {
		lpd270_lcd_to_use = &sharp_lq036q1da01;
	} else if (!strncasecmp(str, "lq64d343", 8)) {
		lpd270_lcd_to_use = &sharp_lq64d343;
	} else if (!strncasecmp(str, "lq10d368", 8)) {
		lpd270_lcd_to_use = &sharp_lq10d368;
	} else if (!strncasecmp(str, "lq035q7db02-20", 14)) {
		lpd270_lcd_to_use = &sharp_lq035q7db02_20;
	} else {
		printk(KERN_INFO "lpd270: unknown lcd panel [%s]\n", str);
	}

	return 1;
}

__setup("lcd=", lpd270_set_lcd);

static struct platform_device *platform_devices[] __initdata = {
	&smc91x_device,
	&lpd270_backlight_device,
	&lpd270_flash_device[0],
	&lpd270_flash_device[1],
};

static struct pxaohci_platform_data lpd270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static void __init lpd270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(lpd270_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	lpd270_flash_data[0].width = (__raw_readl(BOOT_DEF) & 1) ? 2 : 4;
	lpd270_flash_data[1].width = 4;

	/*
	 * System bus arbiter setting:
	 * - Core_Park
	 * - LCD_wt:DMA_wt:CORE_Wt = 2:3:4
	 */
	ARB_CNTRL = ARB_CORE_PARK | 0x234;

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	pxa_set_ac97_info(NULL);

	if (lpd270_lcd_to_use != NULL)
		pxa_set_fb_info(NULL, lpd270_lcd_to_use);

	pxa_set_ohci_info(&lpd270_ohci_platform_data);
}


static struct map_desc lpd270_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)LPD270_CPLD_VIRT,
		.pfn		= __phys_to_pfn(LPD270_CPLD_PHYS),
		.length		= LPD270_CPLD_SIZE,
		.type		= MT_DEVICE,
	},
};

static void __init lpd270_map_io(void)
{
	pxa27x_map_io();
	iotable_init(lpd270_io_desc, ARRAY_SIZE(lpd270_io_desc));

	/* for use I SRAM as framebuffer.  */
	PSLR |= 0x00000F04;
	PCFR  = 0x00000066;
}

MACHINE_START(LOGICPD_PXA270, "LogicPD PXA270 Card Engine")
	/* Maintainer: Peter Barada */
	.atag_offset	= 0x100,
	.map_io		= lpd270_map_io,
	.nr_irqs	= LPD270_NR_IRQS,
	.init_irq	= lpd270_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= lpd270_init,
	.restart	= pxa_restart,
MACHINE_END
