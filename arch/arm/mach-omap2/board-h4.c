/*
 * linux/arch/arm/mach-omap2/board-h4.c
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Modified from mach-omap/omap1/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/platform_data/at24.h>
#include <linux/input.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/input/matrix_keypad.h>
#include <linux/mfd/menelaus.h>
#include <linux/omap-dma.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "common.h"
#include "mux.h"
#include "control.h"
#include "gpmc.h"
#include "gpmc-smc91x.h"

#define H4_FLASH_CS	0

#if defined(CONFIG_KEYBOARD_MATRIX) || defined(CONFIG_KEYBOARD_MATRIX_MODULE)
static const uint32_t board_matrix_keys[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(1, 0, KEY_RIGHT),
	KEY(2, 0, KEY_A),
	KEY(3, 0, KEY_B),
	KEY(4, 0, KEY_C),
	KEY(0, 1, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(2, 1, KEY_E),
	KEY(3, 1, KEY_F),
	KEY(4, 1, KEY_G),
	KEY(0, 2, KEY_ENTER),
	KEY(1, 2, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(3, 2, KEY_K),
	KEY(4, 2, KEY_3),
	KEY(0, 3, KEY_M),
	KEY(1, 3, KEY_N),
	KEY(2, 3, KEY_O),
	KEY(3, 3, KEY_P),
	KEY(4, 3, KEY_Q),
	KEY(0, 4, KEY_R),
	KEY(1, 4, KEY_4),
	KEY(2, 4, KEY_T),
	KEY(3, 4, KEY_U),
	KEY(4, 4, KEY_ENTER),
	KEY(0, 5, KEY_V),
	KEY(1, 5, KEY_W),
	KEY(2, 5, KEY_L),
	KEY(3, 5, KEY_S),
	KEY(4, 5, KEY_ENTER),
};

static const struct matrix_keymap_data board_keymap_data = {
	.keymap			= board_matrix_keys,
	.keymap_size		= ARRAY_SIZE(board_matrix_keys),
};

static unsigned int board_keypad_row_gpios[] = {
	88, 89, 124, 11, 6, 96
};

static unsigned int board_keypad_col_gpios[] = {
	90, 91, 100, 36, 12, 97, 98
};

static struct matrix_keypad_platform_data board_keypad_platform_data = {
	.keymap_data	= &board_keymap_data,
	.row_gpios	= board_keypad_row_gpios,
	.num_row_gpios	= ARRAY_SIZE(board_keypad_row_gpios),
	.col_gpios	= board_keypad_col_gpios,
	.num_col_gpios	= ARRAY_SIZE(board_keypad_col_gpios),
	.active_low	= 1,

	.debounce_ms		= 20,
	.col_scan_delay_us	= 5,
};

static struct platform_device board_keyboard = {
	.name	= "matrix-keypad",
	.id	= -1,
	.dev	= {
		.platform_data = &board_keypad_platform_data,
	},
};
static void __init board_mkp_init(void)
{
	omap_mux_init_gpio(88, OMAP_PULL_ENA | OMAP_PULL_UP);
	omap_mux_init_gpio(89, OMAP_PULL_ENA | OMAP_PULL_UP);
	omap_mux_init_gpio(124, OMAP_PULL_ENA | OMAP_PULL_UP);
	omap_mux_init_signal("mcbsp2_dr.gpio_11", OMAP_PULL_ENA | OMAP_PULL_UP);
	if (omap_has_menelaus()) {
		omap_mux_init_signal("sdrc_a14.gpio0",
			OMAP_PULL_ENA | OMAP_PULL_UP);
		omap_mux_init_signal("vlynq_rx0.gpio_15", 0);
		omap_mux_init_signal("gpio_98", 0);
		board_keypad_row_gpios[5] = 0;
		board_keypad_col_gpios[2] = 15;
		board_keypad_col_gpios[6] = 18;
	} else {
		omap_mux_init_signal("gpio_96", OMAP_PULL_ENA | OMAP_PULL_UP);
		omap_mux_init_signal("gpio_100", 0);
		omap_mux_init_signal("gpio_98", 0);
	}
	omap_mux_init_signal("gpio_90", 0);
	omap_mux_init_signal("gpio_91", 0);
	omap_mux_init_signal("gpio_36", 0);
	omap_mux_init_signal("mcbsp2_clkx.gpio_12", 0);
	omap_mux_init_signal("gpio_97", 0);

	platform_device_register(&board_keyboard);
}
#else
static inline void board_mkp_init(void)
{
}
#endif

static struct mtd_partition h4_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	},
	/* kernel */
	{
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	},
	/* file system */
	{
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct physmap_flash_data h4_flash_data = {
	.width		= 2,
	.parts		= h4_partitions,
	.nr_parts	= ARRAY_SIZE(h4_partitions),
};

static struct resource h4_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h4_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &h4_flash_data,
	},
	.num_resources	= 1,
	.resource	= &h4_flash_resource,
};

static const struct display_timing cm_t35_lcd_videomode = {
	.pixelclock	= { 0, 6250000, 0 },

	.hactive = { 0, 240, 0 },
	.hfront_porch = { 0, 15, 0 },
	.hback_porch = { 0, 60, 0 },
	.hsync_len = { 0, 15, 0 },

	.vactive = { 0, 320, 0 },
	.vfront_porch = { 0, 1, 0 },
	.vback_porch = { 0, 1, 0 },
	.vsync_len = { 0, 1, 0 },

	.flags = DISPLAY_FLAGS_HSYNC_HIGH | DISPLAY_FLAGS_VSYNC_HIGH |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE,
};

static struct panel_dpi_platform_data cm_t35_lcd_pdata = {
	.name                   = "lcd",
	.source                 = "dpi.0",

	.data_lines		= 16,

	.display_timing		= &cm_t35_lcd_videomode,

	.enable_gpio		= -1,
	.backlight_gpio		= -1,
};

static struct platform_device cm_t35_lcd_device = {
	.name                   = "panel-dpi",
	.id                     = 0,
	.dev.platform_data      = &cm_t35_lcd_pdata,
};

static struct platform_device *h4_devices[] __initdata = {
	&h4_flash_device,
	&cm_t35_lcd_device,
};

static struct omap_dss_board_info h4_dss_data = {
	.default_display_name = "lcd",
};

/* 2420 Sysboot setup (2430 is different) */
static u32 get_sysboot_value(void)
{
	return (omap_ctrl_readl(OMAP24XX_CONTROL_STATUS) &
		(OMAP2_SYSBOOT_5_MASK | OMAP2_SYSBOOT_4_MASK |
		 OMAP2_SYSBOOT_3_MASK | OMAP2_SYSBOOT_2_MASK |
		 OMAP2_SYSBOOT_1_MASK | OMAP2_SYSBOOT_0_MASK));
}

/* H4-2420's always used muxed mode, H4-2422's always use non-muxed
 *
 * Note: OMAP-GIT doesn't correctly do is_cpu_omap2422 and is_cpu_omap2423
 *  correctly.  The macro needs to look at production_id not just hawkeye.
 */
static u32 is_gpmc_muxed(void)
{
	u32 mux;
	mux = get_sysboot_value();
	if ((mux & 0xF) == 0xd)
		return 1;	/* NAND config (could be either) */
	if (mux & 0x2)		/* if mux'ed */
		return 1;
	else
		return 0;
}

#if IS_ENABLED(CONFIG_SMC91X)

static struct omap_smc91x_platform_data board_smc91x_data = {
	.cs		= 1,
	.gpio_irq	= 92,
	.flags		= GPMC_TIMINGS_SMC91C96 | IORESOURCE_IRQ_LOWLEVEL,
};

static void __init board_smc91x_init(void)
{
	if (is_gpmc_muxed())
		board_smc91x_data.flags |= GPMC_MUX_ADD_DATA;

	omap_mux_init_gpio(board_smc91x_data.gpio_irq, OMAP_PIN_INPUT);
	gpmc_smc91x_init(&board_smc91x_data);
}

#else

static inline void board_smc91x_init(void)
{
}

#endif

static void __init h4_init_flash(void)
{
	unsigned long base;

	if (gpmc_cs_request(H4_FLASH_CS, SZ_64M, &base) < 0) {
		printk("Can't request GPMC CS for flash\n");
		return;
	}
	h4_flash_resource.start	= base;
	h4_flash_resource.end	= base + SZ_64M - 1;
}

static struct at24_platform_data m24c01 = {
	.byte_len	= SZ_1K / 8,
	.page_size	= 16,
};

static struct i2c_board_info __initdata h4_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("isp1301_omap", 0x2d),
	},
	{	/* EEPROM on mainboard */
		I2C_BOARD_INFO("24c01", 0x52),
		.platform_data	= &m24c01,
	},
	{	/* EEPROM on cpu card */
		I2C_BOARD_INFO("24c01", 0x57),
		.platform_data	= &m24c01,
	},
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init omap_h4_init(void)
{
	omap2420_mux_init(board_mux, OMAP_PACKAGE_ZAF);

	/*
	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on
	 * if not needed.
	 */

	board_mkp_init();
	h4_i2c_board_info[0].irq = gpio_to_irq(125);
	i2c_register_board_info(1, h4_i2c_board_info,
			ARRAY_SIZE(h4_i2c_board_info));

	platform_add_devices(h4_devices, ARRAY_SIZE(h4_devices));
	omap_serial_init();
	omap_sdrc_init(NULL, NULL);
	h4_init_flash();
	board_smc91x_init();

	omap_display_init(&h4_dss_data);
}

MACHINE_START(OMAP_H4, "OMAP2420 H4 board")
	/* Maintainer: Paul Mundt <paul.mundt@nokia.com> */
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap242x_map_io,
	.init_early	= omap2420_init_early,
	.init_irq	= omap2_init_irq,
	.handle_irq	= omap2_intc_handle_irq,
	.init_machine	= omap_h4_init,
	.init_late	= omap2420_init_late,
	.init_time	= omap2_sync32k_timer_init,
	.restart	= omap2xxx_restart,
MACHINE_END
