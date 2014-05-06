/*
 *  Copyright (C) 2000, 2001 Blue Mug, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include <linux/types.h>
#include <linux/i2c-gpio.h>
#include <linux/interrupt.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>

#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <mach/hardware.h>

#include "common.h"
#include "devices.h"

#define VIDEORAM_SIZE		SZ_128K

#define EDB7211_LCD_DC_DC_EN	CLPS711X_GPIO(3, 1)
#define EDB7211_LCDEN		CLPS711X_GPIO(3, 2)
#define EDB7211_LCDBL		CLPS711X_GPIO(3, 3)

#define EDB7211_I2C_SDA		CLPS711X_GPIO(3, 4)
#define EDB7211_I2C_SCL		CLPS711X_GPIO(3, 5)

#define EDB7211_FLASH0_BASE	(CS0_PHYS_BASE)
#define EDB7211_FLASH1_BASE	(CS1_PHYS_BASE)

#define EDB7211_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define EDB7211_CS8900_IRQ	(IRQ_EINT3)

/* The extra 8 lines of the keyboard matrix */
#define EDB7211_EXTKBD_BASE	(CS3_PHYS_BASE)

static struct i2c_gpio_platform_data edb7211_i2c_pdata __initdata = {
	.sda_pin	= EDB7211_I2C_SDA,
	.scl_pin	= EDB7211_I2C_SCL,
	.scl_is_output_only = 1,
};

static struct resource edb7211_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(EDB7211_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(EDB7211_CS8900_IRQ),
};

static struct mtd_partition edb7211_flash_partitions[] __initdata = {
	{
		.name	= "Flash",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data edb7211_flash_pdata __initdata = {
	.width		= 4,
	.parts		= edb7211_flash_partitions,
	.nr_parts	= ARRAY_SIZE(edb7211_flash_partitions),
};

static struct resource edb7211_flash_resources[] __initdata = {
	DEFINE_RES_MEM(EDB7211_FLASH0_BASE, SZ_8M),
	DEFINE_RES_MEM(EDB7211_FLASH1_BASE, SZ_8M),
};

static struct platform_device edb7211_flash_pdev __initdata = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= edb7211_flash_resources,
	.num_resources	= ARRAY_SIZE(edb7211_flash_resources),
	.dev	= {
		.platform_data	= &edb7211_flash_pdata,
	},
};

static void edb7211_lcd_power_set(struct plat_lcd_data *pd, unsigned int power)
{
	if (power) {
		gpio_set_value(EDB7211_LCDEN, 1);
		udelay(100);
		gpio_set_value(EDB7211_LCD_DC_DC_EN, 1);
	} else {
		gpio_set_value(EDB7211_LCD_DC_DC_EN, 0);
		udelay(100);
		gpio_set_value(EDB7211_LCDEN, 0);
	}
}

static struct plat_lcd_data edb7211_lcd_power_pdata = {
	.set_power	= edb7211_lcd_power_set,
};

static void edb7211_lcd_backlight_set_intensity(int intensity)
{
	gpio_set_value(EDB7211_LCDBL, !!intensity);
	clps_writel((clps_readl(PMPCON) & 0xf0ff) | (intensity << 8), PMPCON);
}

static struct generic_bl_info edb7211_lcd_backlight_pdata = {
	.name			= "lcd-backlight.0",
	.default_intensity	= 0x01,
	.max_intensity		= 0x0f,
	.set_bl_intensity	= edb7211_lcd_backlight_set_intensity,
};

static struct gpio edb7211_gpios[] __initconst = {
	{ EDB7211_LCD_DC_DC_EN,	GPIOF_OUT_INIT_LOW,	"LCD DC-DC" },
	{ EDB7211_LCDEN,	GPIOF_OUT_INIT_LOW,	"LCD POWER" },
	{ EDB7211_LCDBL,	GPIOF_OUT_INIT_LOW,	"LCD BACKLIGHT" },
};

/* Reserve screen memory region at the start of main system memory. */
static void __init edb7211_reserve(void)
{
	memblock_reserve(PHYS_OFFSET, VIDEORAM_SIZE);
}

static void __init
fixup_edb7211(struct tag *tags, char **cmdline, struct meminfo *mi)
{
	/*
	 * Bank start addresses are not present in the information
	 * passed in from the boot loader.  We could potentially
	 * detect them, but instead we hard-code them.
	 *
	 * Banks sizes _are_ present in the param block, but we're
	 * not using that information yet.
	 */
	mi->bank[0].start = 0xc0000000;
	mi->bank[0].size = SZ_8M;
	mi->bank[1].start = 0xc1000000;
	mi->bank[1].size = SZ_8M;
	mi->nr_banks = 2;
}

static void __init edb7211_init(void)
{
	clps711x_devices_init();
}

static void __init edb7211_init_late(void)
{
	gpio_request_array(edb7211_gpios, ARRAY_SIZE(edb7211_gpios));

	platform_device_register(&edb7211_flash_pdev);
	platform_device_register_data(&platform_bus, "platform-lcd", 0,
				      &edb7211_lcd_power_pdata,
				      sizeof(edb7211_lcd_power_pdata));
	platform_device_register_data(&platform_bus, "generic-bl", 0,
				      &edb7211_lcd_backlight_pdata,
				      sizeof(edb7211_lcd_backlight_pdata));
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_simple("cs89x0", 0, edb7211_cs8900_resource,
					ARRAY_SIZE(edb7211_cs8900_resource));
	platform_device_register_data(&platform_bus, "i2c-gpio", 0,
				      &edb7211_i2c_pdata,
				      sizeof(edb7211_i2c_pdata));
}

MACHINE_START(EDB7211, "CL-EDB7211 (EP7211 eval board)")
	/* Maintainer: Jon McClintock */
	.atag_offset	= VIDEORAM_SIZE + 0x100,
	.fixup		= fixup_edb7211,
	.reserve	= edb7211_reserve,
	.map_io		= clps711x_map_io,
	.init_early	= clps711x_init_early,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.init_machine	= edb7211_init,
	.init_late	= edb7211_init_late,
	.restart	= clps711x_restart,
MACHINE_END
