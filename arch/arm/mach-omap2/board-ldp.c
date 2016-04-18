/*
 * linux/arch/arm/mach-omap2/board-ldp.c
 *
 * Copyright (C) 2008 Texas Instruments Inc.
 * Nishant Kamat <nskamat@ti.com>
 *
 * Modified from mach-omap2/board-3430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/mmc/host.h>
#include <linux/usb/phy.h>
#include <linux/platform_data/spi-omap2-mcspi.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "gpmc.h"
#include "gpmc-smsc911x.h"

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "board-flash.h"
#include "mux.h"
#include "hsmmc.h"
#include "control.h"
#include "common-board-devices.h"

#define LDP_SMSC911X_CS		1
#define LDP_SMSC911X_GPIO	152
#define DEBUG_BASE		0x08000000
#define LDP_ETHR_START		DEBUG_BASE

static uint32_t board_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(1, 0, KEY_2),
	KEY(2, 0, KEY_3),
	KEY(0, 1, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(2, 1, KEY_6),
	KEY(3, 1, KEY_F5),
	KEY(0, 2, KEY_7),
	KEY(1, 2, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(3, 2, KEY_F6),
	KEY(0, 3, KEY_F7),
	KEY(1, 3, KEY_0),
	KEY(2, 3, KEY_F8),
	PERSISTENT_KEY(4, 5),
	KEY(4, 4, KEY_VOLUMEUP),
	KEY(5, 5, KEY_VOLUMEDOWN),
	0
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data ldp_kp_twl4030_data = {
	.keymap_data	= &board_map_data,
	.rows		= 6,
	.cols		= 6,
	.rep		= 1,
};

static struct gpio_keys_button ldp_gpio_keys_buttons[] = {
	[0] = {
		.code			= KEY_ENTER,
		.gpio			= 101,
		.desc			= "enter sw",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[1] = {
		.code			= KEY_F1,
		.gpio			= 102,
		.desc			= "func 1",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[2] = {
		.code			= KEY_F2,
		.gpio			= 103,
		.desc			= "func 2",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[3] = {
		.code			= KEY_F3,
		.gpio			= 104,
		.desc			= "func 3",
		.active_low		= 1,
		.debounce_interval 	= 30,
	},
	[4] = {
		.code			= KEY_F4,
		.gpio			= 105,
		.desc			= "func 4",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[5] = {
		.code			= KEY_LEFT,
		.gpio			= 106,
		.desc			= "left sw",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[6] = {
		.code			= KEY_RIGHT,
		.gpio			= 107,
		.desc			= "right sw",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[7] = {
		.code			= KEY_UP,
		.gpio			= 108,
		.desc			= "up sw",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
	[8] = {
		.code			= KEY_DOWN,
		.gpio			= 109,
		.desc			= "down sw",
		.active_low		= 1,
		.debounce_interval	= 30,
	},
};

static struct gpio_keys_platform_data ldp_gpio_keys = {
	.buttons		= ldp_gpio_keys_buttons,
	.nbuttons		= ARRAY_SIZE(ldp_gpio_keys_buttons),
	.rep			= 1,
};

static struct platform_device ldp_gpio_keys_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &ldp_gpio_keys,
	},
};

static struct omap_smsc911x_platform_data smsc911x_cfg = {
	.cs             = LDP_SMSC911X_CS,
	.gpio_irq       = LDP_SMSC911X_GPIO,
	.gpio_reset     = -EINVAL,
	.flags		= SMSC911X_USE_32BIT,
};

static inline void __init ldp_init_smsc911x(void)
{
	gpmc_smsc911x_init(&smsc911x_cfg);
}

/* LCD */

#define LCD_PANEL_RESET_GPIO		55
#define LCD_PANEL_QVGA_GPIO		56

static const struct display_timing ldp_lcd_videomode = {
	.pixelclock	= { 0, 5400000, 0 },

	.hactive = { 0, 240, 0 },
	.hfront_porch = { 0, 3, 0 },
	.hback_porch = { 0, 39, 0 },
	.hsync_len = { 0, 3, 0 },

	.vactive = { 0, 320, 0 },
	.vfront_porch = { 0, 2, 0 },
	.vback_porch = { 0, 7, 0 },
	.vsync_len = { 0, 1, 0 },

	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE,
};

static struct panel_dpi_platform_data ldp_lcd_pdata = {
	.name                   = "lcd",
	.source                 = "dpi.0",

	.data_lines		= 18,

	.display_timing		= &ldp_lcd_videomode,

	.enable_gpio		= -1,	/* filled in code */
	.backlight_gpio		= -1,	/* filled in code */
};

static struct platform_device ldp_lcd_device = {
	.name                   = "panel-dpi",
	.id                     = 0,
	.dev.platform_data      = &ldp_lcd_pdata,
};

static struct omap_dss_board_info ldp_dss_data = {
	.default_display_name = "lcd",
};

static void __init ldp_display_init(void)
{
	int r;

	static struct gpio gpios[] __initdata = {
		{LCD_PANEL_RESET_GPIO, GPIOF_OUT_INIT_HIGH, "LCD RESET"},
		{LCD_PANEL_QVGA_GPIO, GPIOF_OUT_INIT_HIGH, "LCD QVGA"},
	};

	r = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if (r) {
		pr_err("Cannot request LCD GPIOs, error %d\n", r);
		return;
	}

	omap_display_init(&ldp_dss_data);
}

static int ldp_twl_gpio_setup(struct device *dev, unsigned gpio, unsigned ngpio)
{
	int res;

	/* LCD enable GPIO */
	ldp_lcd_pdata.enable_gpio = gpio + 7;

	/* Backlight enable GPIO */
	ldp_lcd_pdata.backlight_gpio = gpio + 15;

	res = platform_device_register(&ldp_lcd_device);
	if (res)
		pr_err("Unable to register LCD: %d\n", res);

	return 0;
}

static struct twl4030_gpio_platform_data ldp_gpio_data = {
	.setup		= ldp_twl_gpio_setup,
};

static struct regulator_consumer_supply ldp_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data ldp_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(ldp_vmmc1_supply),
	.consumer_supplies	= ldp_vmmc1_supply,
};

/* ads7846 on SPI */
static struct regulator_consumer_supply ldp_vaux1_supplies[] = {
	REGULATOR_SUPPLY("vcc", "spi1.0"),
};

/* VAUX1 */
static struct regulator_init_data ldp_vaux1 = {
	.constraints = {
		.min_uV			= 3000000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies		= ARRAY_SIZE(ldp_vaux1_supplies),
	.consumer_supplies		= ldp_vaux1_supplies,
};

static struct regulator_consumer_supply ldp_vpll2_supplies[] = {
	REGULATOR_SUPPLY("vdds_dsi", "omapdss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dpi.0"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.0"),
};

static struct regulator_init_data ldp_vpll2 = {
	.constraints = {
		.name			= "VDVI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(ldp_vpll2_supplies),
	.consumer_supplies	= ldp_vpll2_supplies,
};

static struct twl4030_platform_data ldp_twldata = {
	/* platform_data for children goes here */
	.vmmc1		= &ldp_vmmc1,
	.vaux1		= &ldp_vaux1,
	.vpll2		= &ldp_vpll2,
	.gpio		= &ldp_gpio_data,
	.keypad		= &ldp_kp_twl4030_data,
};

static int __init omap_i2c_init(void)
{
	omap3_pmic_get_config(&ldp_twldata,
			  TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_MADC, 0);
	omap3_pmic_init("twl4030", &ldp_twldata);
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct omap2_hsmmc_info mmc[] __initdata = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static struct platform_device *ldp_devices[] __initdata = {
	&ldp_gpio_keys_device,
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct mtd_partition ldp_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader-NAND",
		.offset		= 0,
		.size		= 4 * (64 * 2048),	/* 512KB, 0x80000 */
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot-NAND",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 10 * (64 * 2048),	/* 1.25MB, 0x140000 */
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "Boot Env-NAND",
		.offset		= MTDPART_OFS_APPEND,   /* Offset = 0x1c0000 */
		.size		= 2 * (64 * 2048),	/* 256KB, 0x40000 */
	},
	{
		.name		= "Kernel-NAND",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x0200000*/
		.size		= 240 * (64 * 2048),	/* 30M, 0x1E00000 */
	},
	{
		.name		= "File System - NAND",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x2000000 */
		.size		= MTDPART_SIZ_FULL,	/* 96MB, 0x6000000 */
	},

};

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

static void __init omap_ldp_init(void)
{
	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	ldp_init_smsc911x();
	omap_i2c_init();
	platform_add_devices(ldp_devices, ARRAY_SIZE(ldp_devices));
	omap_ads7846_init(1, 54, 310, NULL);
	omap_serial_init();
	omap_sdrc_init(NULL, NULL);
	usb_bind_phy("musb-hdrc.0.auto", 0, "twl4030_usb");
	usb_musb_init(NULL);
	board_nand_init(ldp_nand_partitions, ARRAY_SIZE(ldp_nand_partitions),
			0, 0, nand_default_timings);

	omap_hsmmc_init(mmc);
	ldp_display_init();
}

MACHINE_START(OMAP_LDP, "OMAP LDP board")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_irq	= omap3_init_irq,
	.init_machine	= omap_ldp_init,
	.init_late	= omap3430_init_late,
	.init_time	= omap_init_time,
	.restart	= omap3xxx_restart,
MACHINE_END
