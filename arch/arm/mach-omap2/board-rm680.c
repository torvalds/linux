/*
 * Board support file for Nokia RM-680.
 *
 * Copyright (C) 2010 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/i2c/twl.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/i2c.h>
#include <plat/mmc.h>
#include <plat/usb.h>
#include <plat/gpmc.h>
#include <plat/common.h>
#include <plat/onenand.h>

#include "mux.h"
#include "hsmmc.h"
#include "sdram-nokia.h"

static struct regulator_consumer_supply rm680_vemmc_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "mmci-omap-hs.1"),
};

/* Fixed regulator for internal eMMC */
static struct regulator_init_data rm680_vemmc = {
	.constraints =	{
		.name			= "rm680_vemmc",
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS
					| REGULATOR_CHANGE_MODE,
	},
	.num_consumer_supplies		= ARRAY_SIZE(rm680_vemmc_consumers),
	.consumer_supplies		= rm680_vemmc_consumers,
};

static struct fixed_voltage_config rm680_vemmc_config = {
	.supply_name		= "VEMMC",
	.microvolts		= 2900000,
	.gpio			= 157,
	.startup_delay		= 150,
	.enable_high		= 1,
	.init_data		= &rm680_vemmc,
};

static struct platform_device rm680_vemmc_device = {
	.name			= "reg-fixed-voltage",
	.dev			= {
		.platform_data	= &rm680_vemmc_config,
	},
};

static struct platform_device *rm680_peripherals_devices[] __initdata = {
	&rm680_vemmc_device,
};

/* TWL */
static struct twl4030_gpio_platform_data rm680_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
	.pullups		= BIT(0),
	.pulldowns		= BIT(1) | BIT(2) | BIT(8) | BIT(15),
};

static struct twl4030_usb_data rm680_usb_data = {
	.usb_mode		= T2_USB_MODE_ULPI,
};

static struct twl4030_platform_data rm680_twl_data = {
	.irq_base		= TWL4030_IRQ_BASE,
	.irq_end		= TWL4030_IRQ_END,
	.gpio			= &rm680_gpio_data,
	.usb			= &rm680_usb_data,
	/* add rest of the children here */
};

static struct i2c_board_info __initdata rm680_twl_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("twl5031", 0x48),
		.flags		= I2C_CLIENT_WAKE,
		.irq		= INT_34XX_SYS_NIRQ,
		.platform_data	= &rm680_twl_data,
	},
};

static void __init rm680_i2c_init(void)
{
	omap_register_i2c_bus(1, 2900, rm680_twl_i2c_board_info,
				ARRAY_SIZE(rm680_twl_i2c_board_info));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
}

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)
static struct omap_onenand_platform_data board_onenand_data[] = {
	{
		.gpio_irq	= 65,
		.flags		= ONENAND_SYNC_READWRITE,
	}
};
#endif

/* eMMC */
static struct omap2_hsmmc_info mmc[] __initdata = {
	{
		.name		= "internal",
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{ /* Terminator */ }
};

static void __init rm680_peripherals_init(void)
{
	platform_add_devices(rm680_peripherals_devices,
				ARRAY_SIZE(rm680_peripherals_devices));
	rm680_i2c_init();
	gpmc_onenand_init(board_onenand_data);
	omap2_hsmmc_init(mmc);
}

static void __init rm680_init_irq(void)
{
	struct omap_sdrc_params *sdrc_params;

	omap2_init_common_infrastructure();
	sdrc_params = nokia_get_sdram_timings();
	omap2_init_common_devices(sdrc_params, sdrc_params);
	omap_init_irq();
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct omap_musb_board_data rm680_musb_data = {
	.interface_type	= MUSB_INTERFACE_ULPI,
	.mode		= MUSB_PERIPHERAL,
	.power		= 100,
};

static void __init rm680_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_serial_init();
	usb_musb_init(&rm680_musb_data);
	rm680_peripherals_init();
}

static void __init rm680_map_io(void)
{
	omap2_set_globals_3xxx();
	omap34xx_map_common_io();
}

MACHINE_START(NOKIA_RM680, "Nokia RM-680 board")
	.boot_params	= 0x80000100,
	.map_io		= rm680_map_io,
	.reserve	= omap_reserve,
	.init_irq	= rm680_init_irq,
	.init_machine	= rm680_init,
	.timer		= &omap_timer,
MACHINE_END
