/*
 * linux/arch/arm/mach-omap1/board-palmz71.c
 *
 * Modified from board-generic.c
 *
 * Support for the Palm Zire71 PDA.
 *
 * Original version : Laurent Gonzalez
 *
 * Modified for zire71 : Marek Vasut
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/omapfb.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/platform_data/omap1_bl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/flash.h>
#include <plat/mux.h>
#include <plat/dma.h>
#include <plat/tc.h>
#include <plat/irda.h>
#include <linux/platform_data/keypad-omap.h>

#include <mach/hardware.h>
#include <mach/usb.h>

#include "common.h"

#define PALMZ71_USBDETECT_GPIO	0
#define PALMZ71_PENIRQ_GPIO	6
#define PALMZ71_MMC_WP_GPIO	8
#define PALMZ71_HDQ_GPIO 	11

#define PALMZ71_HOTSYNC_GPIO	OMAP_MPUIO(1)
#define PALMZ71_CABLE_GPIO	OMAP_MPUIO(2)
#define PALMZ71_SLIDER_GPIO	OMAP_MPUIO(3)
#define PALMZ71_MMC_IN_GPIO	OMAP_MPUIO(4)

static const unsigned int palmz71_keymap[] = {
	KEY(0, 0, KEY_F1),
	KEY(1, 0, KEY_F2),
	KEY(2, 0, KEY_F3),
	KEY(3, 0, KEY_F4),
	KEY(4, 0, KEY_POWER),
	KEY(0, 1, KEY_LEFT),
	KEY(1, 1, KEY_DOWN),
	KEY(2, 1, KEY_UP),
	KEY(3, 1, KEY_RIGHT),
	KEY(4, 1, KEY_ENTER),
	KEY(0, 2, KEY_CAMERA),
};

static const struct matrix_keymap_data palmz71_keymap_data = {
	.keymap		= palmz71_keymap,
	.keymap_size	= ARRAY_SIZE(palmz71_keymap),
};

static struct omap_kp_platform_data palmz71_kp_data = {
	.rows	= 8,
	.cols	= 8,
	.keymap_data	= &palmz71_keymap_data,
	.rep	= true,
	.delay	= 80,
};

static struct resource palmz71_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmz71_kp_device = {
	.name	= "omap-keypad",
	.id	= -1,
	.dev	= {
		.platform_data = &palmz71_kp_data,
	},
	.num_resources	= ARRAY_SIZE(palmz71_kp_resources),
	.resource	= palmz71_kp_resources,
};

static struct mtd_partition palmz71_rom_partitions[] = {
	/* PalmOS "Small ROM", contains the bootloader and the debugger */
	{
		.name		= "smallrom",
		.offset		= 0,
		.size		= 0xa000,
		.mask_flags	= MTD_WRITEABLE,
	},
	/* PalmOS "Big ROM", a filesystem with all the OS code and data */
	{
		.name	= "bigrom",
		.offset	= SZ_128K,
		/*
		 * 0x5f0000 bytes big in the multi-language ("EFIGS") version,
		 * 0x7b0000 bytes in the English-only ("enUS") version.
		 */
		.size		= 0x7b0000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data palmz71_rom_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= palmz71_rom_partitions,
	.nr_parts	= ARRAY_SIZE(palmz71_rom_partitions),
};

static struct resource palmz71_rom_resource = {
	.start	= OMAP_CS0_PHYS,
	.end	= OMAP_CS0_PHYS + SZ_8M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device palmz71_rom_device = {
	.name	= "physmap-flash",
	.id	= -1,
	.dev = {
		.platform_data = &palmz71_rom_data,
	},
	.num_resources	= 1,
	.resource	= &palmz71_rom_resource,
};

static struct platform_device palmz71_lcd_device = {
	.name	= "lcd_palmz71",
	.id	= -1,
};

static struct omap_irda_config palmz71_irda_config = {
	.transceiver_cap	= IR_SIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource palmz71_irda_resources[] = {
	[0] = {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmz71_irda_device = {
	.name	= "omapirda",
	.id	= -1,
	.dev = {
		.platform_data = &palmz71_irda_config,
	},
	.num_resources	= ARRAY_SIZE(palmz71_irda_resources),
	.resource	= palmz71_irda_resources,
};

static struct platform_device palmz71_spi_device = {
	.name	= "spi_palmz71",
	.id	= -1,
};

static struct omap_backlight_config palmz71_backlight_config = {
	.default_intensity	= 0xa0,
};

static struct platform_device palmz71_backlight_device = {
	.name	= "omap-bl",
	.id	= -1,
	.dev	= {
		.platform_data = &palmz71_backlight_config,
	},
};

static struct platform_device *devices[] __initdata = {
	&palmz71_rom_device,
	&palmz71_kp_device,
	&palmz71_lcd_device,
	&palmz71_irda_device,
	&palmz71_spi_device,
	&palmz71_backlight_device,
};

static int
palmz71_get_pendown_state(void)
{
	return !gpio_get_value(PALMZ71_PENIRQ_GPIO);
}

static const struct ads7846_platform_data palmz71_ts_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.get_pendown_state	= palmz71_get_pendown_state,
};

static struct spi_board_info __initdata palmz71_boardinfo[] = { {
	/* MicroWire (bus 2) CS0 has an ads7846e */
	.modalias	= "ads7846",
	.platform_data	= &palmz71_ts_info,
	.max_speed_hz	= 120000	/* max sample rate at 3V */
				* 26	/* command + data + overhead */,
	.bus_num	= 2,
	.chip_select	= 0,
} };

static struct omap_usb_config palmz71_usb_config __initdata = {
	.register_dev	= 1,	/* Mini-B only receptacle */
	.hmc_mode	= 0,
	.pins[0]	= 2,
};

static struct omap_lcd_config palmz71_lcd_config __initdata = {
	.ctrl_name = "internal",
};

static irqreturn_t
palmz71_powercable(int irq, void *dev_id)
{
	if (gpio_get_value(PALMZ71_USBDETECT_GPIO)) {
		printk(KERN_INFO "PM: Power cable connected\n");
		irq_set_irq_type(gpio_to_irq(PALMZ71_USBDETECT_GPIO),
				 IRQ_TYPE_EDGE_FALLING);
	} else {
		printk(KERN_INFO "PM: Power cable disconnected\n");
		irq_set_irq_type(gpio_to_irq(PALMZ71_USBDETECT_GPIO),
				 IRQ_TYPE_EDGE_RISING);
	}
	return IRQ_HANDLED;
}

static void __init
omap_mpu_wdt_mode(int mode)
{
	if (mode)
		omap_writew(0x8000, OMAP_WDT_TIMER_MODE);
	else {
		omap_writew(0x00f5, OMAP_WDT_TIMER_MODE);
		omap_writew(0x00a0, OMAP_WDT_TIMER_MODE);
	}
}

static void __init
palmz71_gpio_setup(int early)
{
	if (early) {
		/* Only set GPIO1 so we have a working serial */
		gpio_direction_output(1, 1);
	} else {
		/* Set MMC/SD host WP pin as input */
		if (gpio_request(PALMZ71_MMC_WP_GPIO, "MMC WP") < 0) {
			printk(KERN_ERR "Could not reserve WP GPIO!\n");
			return;
		}
		gpio_direction_input(PALMZ71_MMC_WP_GPIO);

		/* Monitor the Power-cable-connected signal */
		if (gpio_request(PALMZ71_USBDETECT_GPIO, "USB detect") < 0) {
			printk(KERN_ERR
				"Could not reserve cable signal GPIO!\n");
			return;
		}
		gpio_direction_input(PALMZ71_USBDETECT_GPIO);
		if (request_irq(gpio_to_irq(PALMZ71_USBDETECT_GPIO),
				palmz71_powercable, 0, "palmz71-cable", NULL))
			printk(KERN_ERR
					"IRQ request for power cable failed!\n");
		palmz71_powercable(gpio_to_irq(PALMZ71_USBDETECT_GPIO), NULL);
	}
}

static void __init
omap_palmz71_init(void)
{
	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);
	omap_cfg_reg(UART2_TX);
	omap_cfg_reg(UART2_RTS);
	omap_cfg_reg(UART3_TX);
	omap_cfg_reg(UART3_RX);

	palmz71_gpio_setup(1);
	omap_mpu_wdt_mode(0);

	platform_add_devices(devices, ARRAY_SIZE(devices));

	palmz71_boardinfo[0].irq = gpio_to_irq(PALMZ71_PENIRQ_GPIO);
	spi_register_board_info(palmz71_boardinfo,
				ARRAY_SIZE(palmz71_boardinfo));
	omap1_usb_init(&palmz71_usb_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	palmz71_gpio_setup(0);

	omapfb_set_lcd_config(&palmz71_lcd_config);
}

MACHINE_START(OMAP_PALMZ71, "OMAP310 based Palm Zire71")
	.atag_offset	= 0x100,
	.map_io		= omap15xx_map_io,
	.init_early     = omap1_init_early,
	.reserve	= omap_reserve,
	.init_irq	= omap1_init_irq,
	.init_machine	= omap_palmz71_init,
	.init_late	= omap1_init_late,
	.timer		= &omap1_timer,
	.restart	= omap1_restart,
MACHINE_END
