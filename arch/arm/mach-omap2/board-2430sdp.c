/*
 * linux/arch/arm/mach-omap2/board-2430sdp.c
 *
 * Copyright (C) 2006 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial Code : Based on a patch from Komal Shah and Richard Woodruff
 * Updated the Code for 2430 SDP : Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/i2c/twl4030.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>
#include <mach/usb.h>
#include <mach/gpmc-smc91x.h>

#include "mmc-twl4030.h"

#define SDP2430_CS0_BASE	0x04000000
#define SECONDARY_LCD_GPIO		147

static struct mtd_partition sdp2430_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
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

static struct flash_platform_data sdp2430_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= sdp2430_partitions,
	.nr_parts	= ARRAY_SIZE(sdp2430_partitions),
};

static struct resource sdp2430_flash_resource = {
	.start		= SDP2430_CS0_BASE,
	.end		= SDP2430_CS0_BASE + SZ_64M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device sdp2430_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev = {
		.platform_data	= &sdp2430_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sdp2430_flash_resource,
};

static struct platform_device sdp2430_lcd_device = {
	.name		= "sdp2430_lcd",
	.id		= -1,
};

static struct platform_device *sdp2430_devices[] __initdata = {
	&sdp2430_flash_device,
	&sdp2430_lcd_device,
};

static struct omap_lcd_config sdp2430_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91x_MODULE)

static struct omap_smc91x_platform_data board_smc91x_data = {
	.cs		= 5,
	.gpio_irq	= 149,
	.flags		= GPMC_MUX_ADD_DATA | GPMC_TIMINGS_SMC91C96 |
				IORESOURCE_IRQ_LOWLEVEL,

};

static void __init board_smc91x_init(void)
{
	if (omap_rev() > OMAP3430_REV_ES1_0)
		board_smc91x_data.gpio_irq = 6;
	else
		board_smc91x_data.gpio_irq = 29;

	gpmc_smc91x_init(&board_smc91x_data);
}

#else

static inline void board_smc91x_init(void)
{
}

#endif

static void __init omap_2430sdp_init_irq(void)
{
	omap2_init_common_hw(NULL);
	omap_init_irq();
	omap_gpio_init();
}

static struct omap_uart_config sdp2430_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel sdp2430_config[] = {
	{OMAP_TAG_UART, &sdp2430_uart_config},
	{OMAP_TAG_LCD, &sdp2430_lcd_config},
};


static struct twl4030_gpio_platform_data sdp2430_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_platform_data sdp2430_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.gpio		= &sdp2430_gpio_data,
};

static struct i2c_board_info __initdata sdp2430_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_24XX_SYS_NIRQ,
		.platform_data = &sdp2430_twldata,
	},
};

static int __init omap2430_i2c_init(void)
{
	omap_register_i2c_bus(1, 400, NULL, 0);
	omap_register_i2c_bus(2, 2600, sdp2430_i2c_boardinfo,
			ARRAY_SIZE(sdp2430_i2c_boardinfo));
	return 0;
}

static struct twl4030_hsmmc_info mmc[] __initdata = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.ext_clock	= 1,
	},
	{}	/* Terminator */
};

static void __init omap_2430sdp_init(void)
{
	int ret;

	omap2430_i2c_init();

	platform_add_devices(sdp2430_devices, ARRAY_SIZE(sdp2430_devices));
	omap_board_config = sdp2430_config;
	omap_board_config_size = ARRAY_SIZE(sdp2430_config);
	omap_serial_init();
	twl4030_mmc_init(mmc);
	usb_musb_init();
	board_smc91x_init();

	/* Turn off secondary LCD backlight */
	ret = gpio_request(SECONDARY_LCD_GPIO, "Secondary LCD backlight");
	if (ret == 0)
		gpio_direction_output(SECONDARY_LCD_GPIO, 0);
}

static void __init omap_2430sdp_map_io(void)
{
	omap2_set_globals_243x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_2430SDP, "OMAP2430 sdp2430 board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_2430sdp_map_io,
	.init_irq	= omap_2430sdp_init_irq,
	.init_machine	= omap_2430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
