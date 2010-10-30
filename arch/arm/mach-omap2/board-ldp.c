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
#include <linux/spi/ads7846.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>
#include <linux/io.h>
#include <linux/smsc911x.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mcspi.h>
#include <mach/gpio.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>
#include <mach/board-zoom.h>

#include <asm/delay.h>
#include <plat/control.h>
#include <plat/usb.h>

#include "mux.h"
#include "hsmmc.h"

#define LDP_SMSC911X_CS		1
#define LDP_SMSC911X_GPIO	152
#define DEBUG_BASE		0x08000000
#define LDP_ETHR_START		DEBUG_BASE

static struct resource ldp_smsc911x_resources[] = {
	[0] = {
		.start	= LDP_ETHR_START,
		.end	= LDP_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config ldp_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device ldp_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ldp_smsc911x_resources),
	.resource	= ldp_smsc911x_resources,
	.dev		= {
		.platform_data = &ldp_smsc911x_config,
	},
};

static int board_keymap[] = {
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

static int ts_gpio;

/**
 * @brief ads7846_dev_init : Requests & sets GPIO line for pen-irq
 *
 * @return - void. If request gpio fails then Flag KERN_ERR.
 */
static void ads7846_dev_init(void)
{
	if (gpio_request(ts_gpio, "ads7846 irq") < 0) {
		printk(KERN_ERR "can't get ads746 pen down GPIO\n");
		return;
	}

	gpio_direction_input(ts_gpio);
	gpio_set_debounce(ts_gpio, 310);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(ts_gpio);
}

static struct ads7846_platform_data tsc2046_config __initdata = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct omap2_mcspi_device_config tsc2046_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct spi_board_info ldp_spi_board_info[] __initdata = {
	[0] = {
		/*
		 * TSC2046 operates at a max freqency of 2MHz, so
		 * operate slightly below at 1.5MHz
		 */
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &tsc2046_mcspi_config,
		.irq			= 0,
		.platform_data		= &tsc2046_config,
	},
};

static inline void __init ldp_init_smsc911x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	int eth_gpio = 0;

	eth_cs = LDP_SMSC911X_CS;

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smsc911x\n");
		return;
	}

	ldp_smsc911x_resources[0].start = cs_mem_base + 0x0;
	ldp_smsc911x_resources[0].end   = cs_mem_base + 0xff;
	udelay(100);

	eth_gpio = LDP_SMSC911X_GPIO;

	ldp_smsc911x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (gpio_request(eth_gpio, "smsc911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smsc911x IRQ\n",
				eth_gpio);
		return;
	}
	gpio_direction_input(eth_gpio);
}

static struct platform_device ldp_lcd_device = {
	.name		= "ldp_lcd",
	.id		= -1,
};

static struct omap_lcd_config ldp_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel ldp_config[] __initdata = {
	{ OMAP_TAG_LCD,		&ldp_lcd_config },
};

static void __init omap_ldp_init_irq(void)
{
	omap_board_config = ldp_config;
	omap_board_config_size = ARRAY_SIZE(ldp_config);
	omap2_init_common_hw(NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
	ldp_init_smsc911x();
}

static struct twl4030_usb_data ldp_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_gpio_platform_data ldp_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_madc_platform_data ldp_madc_data = {
	.irq_line	= 1,
};

static struct regulator_consumer_supply ldp_vmmc1_supply = {
	.supply			= "vmmc",
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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &ldp_vmmc1_supply,
};

static struct twl4030_platform_data ldp_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.madc		= &ldp_madc_data,
	.usb		= &ldp_usb_data,
	.vmmc1		= &ldp_vmmc1,
	.gpio		= &ldp_gpio_data,
	.keypad		= &ldp_kp_twl4030_data,
};

static struct i2c_board_info __initdata ldp_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &ldp_twldata,
	},
};

static int __init omap_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, ldp_i2c_boardinfo,
			ARRAY_SIZE(ldp_i2c_boardinfo));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct omap2_hsmmc_info mmc[] __initdata = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static struct platform_device *ldp_devices[] __initdata = {
	&ldp_smsc911x_device,
	&ldp_lcd_device,
	&ldp_gpio_keys_device,
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define board_mux	NULL
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

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

static void __init omap_ldp_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_i2c_init();
	platform_add_devices(ldp_devices, ARRAY_SIZE(ldp_devices));
	ts_gpio = 54;
	ldp_spi_board_info[0].irq = gpio_to_irq(ts_gpio);
	spi_register_board_info(ldp_spi_board_info,
				ARRAY_SIZE(ldp_spi_board_info));
	ads7846_dev_init();
	omap_serial_init();
	usb_musb_init(&musb_board_data);
	board_nand_init(ldp_nand_partitions,
		ARRAY_SIZE(ldp_nand_partitions), ZOOM_NAND_CS);

	omap2_hsmmc_init(mmc);
	/* link regulators to MMC adapters */
	ldp_vmmc1_supply.dev = mmc[0].dev;
}

MACHINE_START(OMAP_LDP, "OMAP LDP board")
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve	= omap_reserve,
	.init_irq	= omap_ldp_init_irq,
	.init_machine	= omap_ldp_init,
	.timer		= &omap_timer,
MACHINE_END
