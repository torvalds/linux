/*
 *  linux/arch/arm/mach-pxa/trizeps4.c
 *
 *  Support for the Keith und Koep Trizeps4 Module Platform.
 *
 *  Author:	Jürgen Schindele
 *  Created:	20 02, 2006
 *  Copyright:	Jürgen Schindele
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
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/dm9000.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/pxa2xx_spi.h>
#include <mach/trizeps4.h>
#include <mach/audio.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <mach/irda.h>
#include <mach/ohci.h>
#include <mach/i2c.h>

#include "generic.h"
#include "devices.h"

/*	comment out the following line if you want to use the
 *	Standard UART from PXA for serial / irda transmission
 *	and acivate it if you have status leds connected */
#define STATUS_LEDS_ON_STUART_PINS 1

/*****************************************************************************
 * MultiFunctionPins of CPU
 *****************************************************************************/
static unsigned long trizeps4_pin_config[] __initdata = {
	/* Chip Selects */
	GPIO15_nCS_1,		/* DiskOnChip CS */
	GPIO93_GPIO,		/* TRIZEPS4_DOC_IRQ */
	GPIO94_GPIO,		/* DOC lock */

	GPIO78_nCS_2,		/* DM9000 CS */
	GPIO101_GPIO,		/* TRIZEPS4_ETH_IRQ */

	GPIO79_nCS_3,		/* Logic CS */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,	/* Logic irq */

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

	/* UART */
	GPIO9_FFUART_CTS,
	GPIO10_FFUART_DCD,
	GPIO16_FFUART_TXD,
	GPIO33_FFUART_DSR,
	GPIO38_FFUART_RI,
	GPIO82_FFUART_DTR,
	GPIO83_FFUART_RTS,
	GPIO96_FFUART_RXD,

	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,
#ifdef STATUS_LEDS_ON_STUART_PINS
	GPIO46_GPIO,
	GPIO47_GPIO,
#else
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,
#endif
	/* PCMCIA */
	GPIO11_GPIO,			/* TRIZEPS4_CD_IRQ */
	GPIO13_GPIO,			/* TRIZEPS4_READY_NINT */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO102_nPCE_1,
	GPIO104_PSKTSEL,

	/* MultiMediaCard */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO12_GPIO,			/* TRIZEPS4_MMC_IRQ */

	/* USB OHCI */
	GPIO88_USBH1_PWR,		/* USBHPWR1 */
	GPIO89_USBH1_PEN,		/* USBHPEN1 */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
};

static unsigned long trizeps4wl_pin_config[] __initdata = {
	/* SSP 2 */
	GPIO14_SSP2_SFRM,
	GPIO19_SSP2_SCLK,
	GPIO53_GPIO,			/* TRIZEPS4_SPI_IRQ */
	GPIO86_SSP2_RXD,
	GPIO87_SSP2_TXD,
};

/****************************************************************************
 * ONBOARD FLASH
 ****************************************************************************/
static struct mtd_partition trizeps4_partitions[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	}, {
		.name =		"Backup",
		.offset =	0x00040000,
		.size =		0x00040000,
	}, {
		.name =		"Image",
		.offset =	0x00080000,
		.size =		0x01080000,
	}, {
		.name =		"IPSM",
		.offset =	0x01100000,
		.size =		0x00e00000,
	}, {
		.name =		"Registry",
		.offset =	0x01f00000,
		.size =		MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data trizeps4_flash_data[] = {
	{
		.width		= 4,			/* bankwidth in bytes */
		.parts		= trizeps4_partitions,
		.nr_parts	= ARRAY_SIZE(trizeps4_partitions)
	}
};

static struct resource flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
		.platform_data = trizeps4_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

/****************************************************************************
 * DAVICOM DM9000 Ethernet
 ****************************************************************************/
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= TRIZEPS4_ETH_PHYS+0x300,
		.end	= TRIZEPS4_ETH_PHYS+0x400-1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= TRIZEPS4_ETH_PHYS+0x8300,
		.end	= TRIZEPS4_ETH_PHYS+0x8400-1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= TRIZEPS4_ETH_IRQ,
		.end	= TRIZEPS4_ETH_IRQ,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct dm9000_plat_data tri_dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
	.dev		= {
		.platform_data = &tri_dm9000_platdata,
	}
};

/****************************************************************************
 * LED's on GPIO pins of PXA
 ****************************************************************************/
static struct gpio_led trizeps4_led[] = {
#ifdef STATUS_LEDS_ON_STUART_PINS
	{
		.name = "led0:orange:heartbeat",	/* */
		.default_trigger = "heartbeat",
		.gpio = GPIO_HEARTBEAT_LED,
		.active_low = 1,
	},
	{
		.name = "led1:yellow:cpubusy",		/* */
		.default_trigger = "cpu-busy",
		.gpio = GPIO_SYS_BUSY_LED,
		.active_low = 1,
	},
#endif
};

static struct gpio_led_platform_data trizeps4_led_data = {
	.leds		= trizeps4_led,
	.num_leds	= ARRAY_SIZE(trizeps4_led),
};

static struct platform_device leds_devices = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &trizeps4_led_data,
	},
};

static struct platform_device *trizeps4_devices[] __initdata = {
	&flash_device,
	&dm9000_device,
	&leds_devices,
};

static struct platform_device *trizeps4wl_devices[] __initdata = {
	&flash_device,
	&leds_devices,
};

static short trizeps_conxs_bcr;

/* PCCARD power switching supports only 3,3V */
void board_pcmcia_power(int power)
{
	if (power) {
		/* switch power on, put in reset and enable buffers */
		trizeps_conxs_bcr |= power;
		trizeps_conxs_bcr |= ConXS_BCR_CF_RESET;
		trizeps_conxs_bcr &= ~ConXS_BCR_CF_BUF_EN;
		BCR_writew(trizeps_conxs_bcr);
		/* wait a little */
		udelay(2000);
		/* take reset away */
		trizeps_conxs_bcr &= ~ConXS_BCR_CF_RESET;
		BCR_writew(trizeps_conxs_bcr);
		udelay(2000);
	} else {
		/* put in reset */
		trizeps_conxs_bcr |= ConXS_BCR_CF_RESET;
		BCR_writew(trizeps_conxs_bcr);
		udelay(1000);
		/* switch power off */
		trizeps_conxs_bcr &= ~0xf;
		BCR_writew(trizeps_conxs_bcr);
	}
	pr_debug("%s: o%s 0x%x\n", __func__, power ? "n" : "ff",
			trizeps_conxs_bcr);
}
EXPORT_SYMBOL(board_pcmcia_power);

/* backlight power switching for LCD panel */
static void board_backlight_power(int on)
{
	if (on)
		trizeps_conxs_bcr |= ConXS_BCR_L_DISP;
	else
		trizeps_conxs_bcr &= ~ConXS_BCR_L_DISP;

	pr_debug("%s: o%s 0x%x\n", __func__, on ? "n" : "ff",
			trizeps_conxs_bcr);
	BCR_writew(trizeps_conxs_bcr);
}

/* a I2C based RTC is known on CONXS board */
static struct i2c_board_info trizeps4_i2c_devices[] __initdata = {
	{ I2C_BOARD_INFO("rtc-pcf8593", 0x51) }
};

/****************************************************************************
 * MMC card slot external to module
 ****************************************************************************/
static int trizeps4_mci_init(struct device *dev, irq_handler_t mci_detect_int,
		void *data)
{
	int err;

	err = request_irq(TRIZEPS4_MMC_IRQ, mci_detect_int,
		IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_SAMPLE_RANDOM,
		"MMC card detect", data);
	if (err) {
		printk(KERN_ERR "trizeps4_mci_init: MMC/SD: can't request"
						"MMC card detect IRQ\n");
		return -1;
	}
	return 0;
}

static void trizeps4_mci_exit(struct device *dev, void *data)
{
	free_irq(TRIZEPS4_MMC_IRQ, data);
}

static struct pxamci_platform_data trizeps4_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.detect_delay	= 1,
	.init 		= trizeps4_mci_init,
	.exit		= trizeps4_mci_exit,
	.get_ro		= NULL,	/* write-protection not supported */
	.setpower 	= NULL,	/* power-switching not supported */
};

/****************************************************************************
 * IRDA mode switching on stuart
 ****************************************************************************/
#ifndef STATUS_LEDS_ON_STUART_PINS
static short trizeps_conxs_ircr;

static int trizeps4_irda_startup(struct device *dev)
{
	trizeps_conxs_ircr &= ~ConXS_IRCR_SD;
	IRCR_writew(trizeps_conxs_ircr);
	return 0;
}

static void trizeps4_irda_shutdown(struct device *dev)
{
	trizeps_conxs_ircr |= ConXS_IRCR_SD;
	IRCR_writew(trizeps_conxs_ircr);
}

static void trizeps4_irda_transceiver_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);
	/* Switch mode */
	if (mode & IR_SIRMODE)
		trizeps_conxs_ircr &= ~ConXS_IRCR_MODE;	/* Slow mode */
	else if (mode & IR_FIRMODE)
		trizeps_conxs_ircr |= ConXS_IRCR_MODE;	/* Fast mode */

	/* Switch power */
	if (mode & IR_OFF)
		trizeps_conxs_ircr |= ConXS_IRCR_SD;
	else
		trizeps_conxs_ircr &= ~ConXS_IRCR_SD;

	IRCR_writew(trizeps_conxs_ircr);
	local_irq_restore(flags);

	pxa2xx_transceiver_mode(dev, mode);
}

static struct pxaficp_platform_data trizeps4_ficp_platform_data = {
	.transceiver_cap	= IR_SIRMODE | IR_FIRMODE | IR_OFF,
	.transceiver_mode	= trizeps4_irda_transceiver_mode,
	.startup		= trizeps4_irda_startup,
	.shutdown		= trizeps4_irda_shutdown,
};
#endif

/****************************************************************************
 * OHCI USB port
 ****************************************************************************/
static struct pxaohci_platform_data trizeps4_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static struct map_desc trizeps4_io_desc[] __initdata = {
	{ 	/* ConXS CFSR */
		.virtual	= TRIZEPS4_CFSR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_CFSR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS BCR */
		.virtual	= TRIZEPS4_BOCR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_BOCR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{ 	/* ConXS IRCR */
		.virtual	= TRIZEPS4_IRCR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_IRCR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS DCR */
		.virtual	= TRIZEPS4_DICR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_DICR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS UPSR */
		.virtual	= TRIZEPS4_UPSR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_UPSR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	}
};

static struct pxafb_mode_info sharp_lcd_mode = {
	.pixclock	= 78000,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 8,
	.hsync_len	= 4,
	.left_margin	= 4,
	.right_margin	= 4,
	.vsync_len	= 2,
	.upper_margin	= 0,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info sharp_lcd = {
	.modes		= &sharp_lcd_mode,
	.num_modes	= 1,
	.lcd_conn	= LCD_COLOR_DSTN_16BPP | LCD_PCLK_EDGE_FALL,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.pxafb_backlight_power = board_backlight_power,
};

static struct pxafb_mode_info toshiba_lcd_mode = {
	.pixclock	= 39720,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 8,
	.hsync_len	= 63,
	.left_margin	= 12,
	.right_margin	= 12,
	.vsync_len	= 4,
	.upper_margin	= 32,
	.lower_margin	= 10,
	.sync		= 0,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info toshiba_lcd = {
	.modes		= &toshiba_lcd_mode,
	.num_modes	= 1,
	.lcd_conn	= (LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL),
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.pxafb_backlight_power = board_backlight_power,
};

static void __init trizeps4_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(trizeps4_pin_config));
	if (machine_is_trizeps4wl()) {
		pxa2xx_mfp_config(ARRAY_AND_SIZE(trizeps4wl_pin_config));
		platform_add_devices(trizeps4wl_devices,
					ARRAY_SIZE(trizeps4wl_devices));
	} else {
		platform_add_devices(trizeps4_devices,
					ARRAY_SIZE(trizeps4_devices));
	}

	if (0)	/* dont know how to determine LCD */
		set_pxa_fb_info(&sharp_lcd);
	else
		set_pxa_fb_info(&toshiba_lcd);

	pxa_set_mci_info(&trizeps4_mci_platform_data);
#ifndef STATUS_LEDS_ON_STUART_PINS
	pxa_set_ficp_info(&trizeps4_ficp_platform_data);
#endif
	pxa_set_ohci_info(&trizeps4_ohci_platform_data);
	pxa_set_ac97_info(NULL);
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, trizeps4_i2c_devices,
					ARRAY_SIZE(trizeps4_i2c_devices));

#ifdef CONFIG_IDE_PXA_CF
	/* if boot direct from compact flash dont disable power */
	trizeps_conxs_bcr = 0x0009;
#else
	/* this is the reset value */
	trizeps_conxs_bcr = 0x00A0;
#endif
	BCR_writew(trizeps_conxs_bcr);
	board_backlight_power(1);
}

static void __init trizeps4_map_io(void)
{
	pxa_map_io();
	iotable_init(trizeps4_io_desc, ARRAY_SIZE(trizeps4_io_desc));

	if ((MSC0 & 0x8) && (BOOT_DEF & 0x1)) {
		/* if flash is 16 bit wide its a Trizeps4 WL */
		__machine_arch_type = MACH_TYPE_TRIZEPS4WL;
		trizeps4_flash_data[0].width = 2;
	} else {
		/* if flash is 32 bit wide its a Trizeps4 */
		__machine_arch_type = MACH_TYPE_TRIZEPS4;
		trizeps4_flash_data[0].width = 4;
	}
}

MACHINE_START(TRIZEPS4, "Keith und Koep Trizeps IV module")
	/* MAINTAINER("Jürgen Schindele") */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= TRIZEPS4_SDRAM_BASE + 0x100,
	.init_machine	= trizeps4_init,
	.map_io		= trizeps4_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

MACHINE_START(TRIZEPS4WL, "Keith und Koep Trizeps IV-WL module")
	/* MAINTAINER("Jürgen Schindele") */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= TRIZEPS4_SDRAM_BASE + 0x100,
	.init_machine	= trizeps4_init,
	.map_io		= trizeps4_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END
