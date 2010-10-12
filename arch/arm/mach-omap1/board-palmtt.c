/*
 * linux/arch/arm/mach-omap1/board-palmtt.c
 *
 * Modified from board-palmtt2.c
 *
 * Modified and amended for Palm Tungsten|T
 * by Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
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

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/led.h>
#include <mach/gpio.h>
#include <plat/flash.h>
#include <plat/mux.h>
#include <plat/usb.h>
#include <plat/dma.h>
#include <plat/tc.h>
#include <plat/board.h>
#include <plat/irda.h>
#include <plat/keypad.h>
#include <plat/common.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#define PALMTT_USBDETECT_GPIO	0
#define PALMTT_CABLE_GPIO	1
#define PALMTT_LED_GPIO		3
#define PALMTT_PENIRQ_GPIO	6
#define PALMTT_MMC_WP_GPIO	8
#define PALMTT_HDQ_GPIO		11

static int palmtt_keymap[] = {
	KEY(0, 0, KEY_ESC),
	KEY(0, 1, KEY_SPACE),
	KEY(0, 2, KEY_LEFTCTRL),
	KEY(0, 3, KEY_TAB),
	KEY(0, 4, KEY_ENTER),
	KEY(1, 0, KEY_LEFT),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_UP),
	KEY(1, 3, KEY_RIGHT),
	KEY(2, 0, KEY_SLEEP),
	KEY(2, 4, KEY_Y),
	0
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

static struct omap_kp_platform_data palmtt_kp_data = {
	.rows	= 6,
	.cols	= 3,
	.keymap = palmtt_keymap,
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
static struct omap_irda_config palmtt_irda_config = {
	.transceiver_cap	= IR_SIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource palmtt_irda_resources[] = {
	[0]	= {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmtt_irda_device = {
	.name		= "omapirda",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmtt_irda_config,
	},
	.num_resources	= ARRAY_SIZE(palmtt_irda_resources),
	.resource	= palmtt_irda_resources,
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
	&palmtt_irda_device,
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
		.irq		= OMAP_GPIO_IRQ(6),
		.max_speed_hz	= 120000	/* max sample rate at 3V */
					* 26	/* command + data + overhead */,
		.bus_num	= 2,
		.chip_select	= 0,
	}
};

static void __init omap_palmtt_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
}

static struct omap_usb_config palmtt_usb_config __initdata = {
	.register_dev	= 1,
	.hmc_mode	= 0,
	.pins[0]	= 2,
};

static struct omap_lcd_config palmtt_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel palmtt_config[] __initdata = {
	{ OMAP_TAG_LCD,		&palmtt_lcd_config	},
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

	omap_board_config = palmtt_config;
	omap_board_config_size = ARRAY_SIZE(palmtt_config);

	platform_add_devices(palmtt_devices, ARRAY_SIZE(palmtt_devices));

	spi_register_board_info(palmtt_boardinfo,ARRAY_SIZE(palmtt_boardinfo));
	omap_serial_init();
	omap1_usb_init(&palmtt_usb_config);
	omap_register_i2c_bus(1, 100, NULL, 0);
}

static void __init omap_palmtt_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(OMAP_PALMTT, "OMAP1510 based Palm Tungsten|T")
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_palmtt_map_io,
	.reserve	= omap_reserve,
	.init_irq	= omap_palmtt_init_irq,
	.init_machine	= omap_palmtt_init,
	.timer		= &omap_timer,
MACHINE_END
