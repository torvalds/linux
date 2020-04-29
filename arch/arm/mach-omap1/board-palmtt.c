// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/board-palmtt.c
 *
 * Modified from board-palmtt2.c
 *
 * Modified and amended for Palm Tungsten|T
 * by Marek Vasut <marek.vasut@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/clk.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/leds.h>
#include <linux/omapfb.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/platform_data/omap1_bl.h>
#include <linux/platform_data/leds-omap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "flash.h"
#include <mach/mux.h>
#include <linux/omap-dma.h>
#include <mach/tc.h>
#include <linux/platform_data/keypad-omap.h>

#include <mach/hardware.h>
#include <mach/usb.h>

#include "common.h"

#define PALMTT_USBDETECT_GPIO	0
#define PALMTT_CABLE_GPIO	1
#define PALMTT_LED_GPIO		3
#define PALMTT_PENIRQ_GPIO	6
#define PALMTT_MMC_WP_GPIO	8
#define PALMTT_HDQ_GPIO		11

static const unsigned int palmtt_keymap[] = {
	KEY(0, 0, KEY_ESC),
	KEY(1, 0, KEY_SPACE),
	KEY(2, 0, KEY_LEFTCTRL),
	KEY(3, 0, KEY_TAB),
	KEY(4, 0, KEY_ENTER),
	KEY(0, 1, KEY_LEFT),
	KEY(1, 1, KEY_DOWN),
	KEY(2, 1, KEY_UP),
	KEY(3, 1, KEY_RIGHT),
	KEY(0, 2, KEY_SLEEP),
	KEY(4, 2, KEY_Y),
};

static struct mtd_partition palmtt_partitions[] = {
	{
		.name		= "write8k",
		.offset		= 0,
		.size		= SZ_8K,
		.mask_flags	= 0,
	},
	{
		.name		= "PalmOS-BootLoader(ro)",
		.offset		= SZ_8K,
		.size		= 7 * SZ_8K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "u-boot",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 8 * SZ_8K,
		.mask_flags	= 0,
	},
	{
		.name		= "PalmOS-FS(ro)",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 7 * SZ_1M + 4 * SZ_64K - 16 * SZ_8K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "u-boot(rez)",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= 0
	},
	{
		.name		= "empty",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data palmtt_flash_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= palmtt_partitions,
	.nr_parts	= ARRAY_SIZE(palmtt_partitions),
};

static struct resource palmtt_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_8M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device palmtt_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &palmtt_flash_data,
	},
	.num_resources	= 1,
	.resource	= &palmtt_flash_resource,
};

static struct resource palmtt_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data palmtt_keymap_data = {
	.keymap		= palmtt_keymap,
	.keymap_size	= ARRAY_SIZE(palmtt_keymap),
};

static struct omap_kp_platform_data palmtt_kp_data = {
	.rows	= 6,
	.cols	= 3,
	.keymap_data = &palmtt_keymap_data,
};

static struct platform_device palmtt_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &palmtt_kp_data,
	},
	.num_resources	= ARRAY_SIZE(palmtt_kp_resources),
	.resource	= palmtt_kp_resources,
};

static struct platform_device palmtt_lcd_device = {
	.name		= "lcd_palmtt",
	.id		= -1,
};

static struct platform_device palmtt_spi_device = {
	.name		= "spi_palmtt",
	.id		= -1,
};

static struct omap_backlight_config palmtt_backlight_config = {
	.default_intensity	= 0xa0,
};

static struct platform_device palmtt_backlight_device = {
	.name		= "omap-bl",
	.id		= -1,
	.dev		= {
		.platform_data= &palmtt_backlight_config,
	},
};

static struct omap_led_config palmtt_led_config[] = {
	{
		.cdev	= {
			.name	= "palmtt:led0",
		},
		.gpio	= PALMTT_LED_GPIO,
	},
};

static struct omap_led_platform_data palmtt_led_data = {
	.nr_leds	= ARRAY_SIZE(palmtt_led_config),
	.leds		= palmtt_led_config,
};

static struct platform_device palmtt_led_device = {
	.name	= "omap-led",
	.id	= -1,
	.dev	= {
		.platform_data	= &palmtt_led_data,
	},
};

static struct platform_device *palmtt_devices[] __initdata = {
	&palmtt_flash_device,
	&palmtt_kp_device,
	&palmtt_lcd_device,
	&palmtt_spi_device,
	&palmtt_backlight_device,
	&palmtt_led_device,
};

static int palmtt_get_pendown_state(void)
{
	return !gpio_get_value(6);
}

static const struct ads7846_platform_data palmtt_ts_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.get_pendown_state	= palmtt_get_pendown_state,
};

static struct spi_board_info __initdata palmtt_boardinfo[] = {
	{
		/* MicroWire (bus 2) CS0 has an ads7846e */
		.modalias	= "ads7846",
		.platform_data	= &palmtt_ts_info,
		.max_speed_hz	= 120000	/* max sample rate at 3V */
					* 26	/* command + data + overhead */,
		.bus_num	= 2,
		.chip_select	= 0,
	}
};

static struct omap_usb_config palmtt_usb_config __initdata = {
	.register_dev	= 1,
	.hmc_mode	= 0,
	.pins[0]	= 2,
};

static const struct omap_lcd_config palmtt_lcd_config __initconst = {
	.ctrl_name	= "internal",
};

static void __init omap_mpu_wdt_mode(int mode) {
	if (mode)
		omap_writew(0x8000, OMAP_WDT_TIMER_MODE);
	else {
		omap_writew(0x00f5, OMAP_WDT_TIMER_MODE);
		omap_writew(0x00a0, OMAP_WDT_TIMER_MODE);
	}
}

static void __init omap_palmtt_init(void)
{
	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);
	omap_cfg_reg(UART2_TX);
	omap_cfg_reg(UART2_RTS);
	omap_cfg_reg(UART3_TX);
	omap_cfg_reg(UART3_RX);

	omap_mpu_wdt_mode(0);

	platform_add_devices(palmtt_devices, ARRAY_SIZE(palmtt_devices));

	palmtt_boardinfo[0].irq = gpio_to_irq(6);
	spi_register_board_info(palmtt_boardinfo,ARRAY_SIZE(palmtt_boardinfo));
	omap_serial_init();
	omap1_usb_init(&palmtt_usb_config);
	omap_register_i2c_bus(1, 100, NULL, 0);

	omapfb_set_lcd_config(&palmtt_lcd_config);
}

MACHINE_START(OMAP_PALMTT, "OMAP1510 based Palm Tungsten|T")
	.atag_offset	= 0x100,
	.map_io		= omap15xx_map_io,
	.init_early     = omap1_init_early,
	.init_irq	= omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine	= omap_palmtt_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
