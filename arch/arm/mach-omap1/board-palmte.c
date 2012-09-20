/*
 * linux/arch/arm/mach-omap1/board-palmte.c
 *
 * Modified from board-generic.c
 *
 * Support for the Palm Tungsten E PDA.
 *
 * Original version : Laurent Gonzalez
 *
 * Maintainers : http://palmtelinux.sf.net
 *                palmtelinux-developpers@lists.sf.net
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/apm-emulation.h>
#include <linux/omapfb.h>
#include <linux/platform_data/omap1_bl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/flash.h>
#include <mach/mux.h>
#include <plat/tc.h>
#include <plat/dma.h>
#include <plat/irda.h>
#include <linux/platform_data/keypad-omap.h>

#include <mach/hardware.h>
#include <mach/usb.h>

#include "common.h"

#define PALMTE_USBDETECT_GPIO	0
#define PALMTE_USB_OR_DC_GPIO	1
#define PALMTE_TSC_GPIO		4
#define PALMTE_PINTDAV_GPIO	6
#define PALMTE_MMC_WP_GPIO	8
#define PALMTE_MMC_POWER_GPIO	9
#define PALMTE_HDQ_GPIO		11
#define PALMTE_HEADPHONES_GPIO	14
#define PALMTE_SPEAKER_GPIO	15
#define PALMTE_DC_GPIO		OMAP_MPUIO(2)
#define PALMTE_MMC_SWITCH_GPIO	OMAP_MPUIO(4)
#define PALMTE_MMC1_GPIO	OMAP_MPUIO(6)
#define PALMTE_MMC2_GPIO	OMAP_MPUIO(7)
#define PALMTE_MMC3_GPIO	OMAP_MPUIO(11)

static const unsigned int palmte_keymap[] = {
	KEY(0, 0, KEY_F1),		/* Calendar */
	KEY(1, 0, KEY_F2),		/* Contacts */
	KEY(2, 0, KEY_F3),		/* Tasks List */
	KEY(3, 0, KEY_F4),		/* Note Pad */
	KEY(4, 0, KEY_POWER),
	KEY(0, 1, KEY_LEFT),
	KEY(1, 1, KEY_DOWN),
	KEY(2, 1, KEY_UP),
	KEY(3, 1, KEY_RIGHT),
	KEY(4, 1, KEY_ENTER),
};

static const struct matrix_keymap_data palmte_keymap_data = {
	.keymap		= palmte_keymap,
	.keymap_size	= ARRAY_SIZE(palmte_keymap),
};

static struct omap_kp_platform_data palmte_kp_data = {
	.rows	= 8,
	.cols	= 8,
	.keymap_data = &palmte_keymap_data,
	.rep	= true,
	.delay	= 12,
};

static struct resource palmte_kp_resources[] = {
	[0]	= {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmte_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_kp_data,
	},
	.num_resources	= ARRAY_SIZE(palmte_kp_resources),
	.resource	= palmte_kp_resources,
};

static struct mtd_partition palmte_rom_partitions[] = {
	/* PalmOS "Small ROM", contains the bootloader and the debugger */
	{
		.name		= "smallrom",
		.offset		= 0,
		.size		= 0xa000,
		.mask_flags	= MTD_WRITEABLE,
	},
	/* PalmOS "Big ROM", a filesystem with all the OS code and data */
	{
		.name		= "bigrom",
		.offset		= SZ_128K,
		/*
		 * 0x5f0000 bytes big in the multi-language ("EFIGS") version,
		 * 0x7b0000 bytes in the English-only ("enUS") version.
		 */
		.size		= 0x7b0000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data palmte_rom_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= palmte_rom_partitions,
	.nr_parts	= ARRAY_SIZE(palmte_rom_partitions),
};

static struct resource palmte_rom_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_8M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device palmte_rom_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_rom_data,
	},
	.num_resources	= 1,
	.resource	= &palmte_rom_resource,
};

static struct platform_device palmte_lcd_device = {
	.name		= "lcd_palmte",
	.id		= -1,
};

static struct omap_backlight_config palmte_backlight_config = {
	.default_intensity	= 0xa0,
};

static struct platform_device palmte_backlight_device = {
	.name		= "omap-bl",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_backlight_config,
	},
};

static struct omap_irda_config palmte_irda_config = {
	.transceiver_cap	= IR_SIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource palmte_irda_resources[] = {
	[0]	= {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmte_irda_device = {
	.name		= "omapirda",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_irda_config,
	},
	.num_resources	= ARRAY_SIZE(palmte_irda_resources),
	.resource	= palmte_irda_resources,
};

static struct platform_device *palmte_devices[] __initdata = {
	&palmte_rom_device,
	&palmte_kp_device,
	&palmte_lcd_device,
	&palmte_backlight_device,
	&palmte_irda_device,
};

static struct omap_usb_config palmte_usb_config __initdata = {
	.register_dev	= 1,	/* Mini-B only receptacle */
	.hmc_mode	= 0,
	.pins[0]	= 2,
};

static struct omap_lcd_config palmte_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct spi_board_info palmte_spi_info[] __initdata = {
	{
		.modalias	= "tsc2102",
		.bus_num	= 2,	/* uWire (officially) */
		.chip_select	= 0,	/* As opposed to 3 */
		.max_speed_hz	= 8000000,
	},
};

static void __init palmte_misc_gpio_setup(void)
{
	/* Set TSC2102 PINTDAV pin as input (used by TSC2102 driver) */
	if (gpio_request(PALMTE_PINTDAV_GPIO, "TSC2102 PINTDAV") < 0) {
		printk(KERN_ERR "Could not reserve PINTDAV GPIO!\n");
		return;
	}
	gpio_direction_input(PALMTE_PINTDAV_GPIO);

	/* Set USB-or-DC-IN pin as input (unused) */
	if (gpio_request(PALMTE_USB_OR_DC_GPIO, "USB/DC-IN") < 0) {
		printk(KERN_ERR "Could not reserve cable signal GPIO!\n");
		return;
	}
	gpio_direction_input(PALMTE_USB_OR_DC_GPIO);
}

static void __init omap_palmte_init(void)
{
	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);
	omap_cfg_reg(UART2_TX);
	omap_cfg_reg(UART2_RTS);
	omap_cfg_reg(UART3_TX);
	omap_cfg_reg(UART3_RX);

	platform_add_devices(palmte_devices, ARRAY_SIZE(palmte_devices));

	palmte_spi_info[0].irq = gpio_to_irq(PALMTE_PINTDAV_GPIO);
	spi_register_board_info(palmte_spi_info, ARRAY_SIZE(palmte_spi_info));
	palmte_misc_gpio_setup();
	omap_serial_init();
	omap1_usb_init(&palmte_usb_config);
	omap_register_i2c_bus(1, 100, NULL, 0);

	omapfb_set_lcd_config(&palmte_lcd_config);
}

MACHINE_START(OMAP_PALMTE, "OMAP310 based Palm Tungsten E")
	.atag_offset	= 0x100,
	.map_io		= omap15xx_map_io,
	.init_early     = omap1_init_early,
	.reserve	= omap_reserve,
	.init_irq	= omap1_init_irq,
	.init_machine	= omap_palmte_init,
	.init_late	= omap1_init_late,
	.timer		= &omap1_timer,
	.restart	= omap1_restart,
MACHINE_END
