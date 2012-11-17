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
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <mach/hardware.h>

#include "common.h"

#define VIDEORAM_SIZE		SZ_128K

#define EDB7211_LCD_DC_DC_EN	CLPS711X_GPIO(3, 1)
#define EDB7211_LCDEN		CLPS711X_GPIO(3, 2)

#define EDB7211_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define EDB7211_CS8900_IRQ	(IRQ_EINT3)

static struct resource edb7211_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(EDB7211_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(EDB7211_CS8900_IRQ),
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

static struct gpio edb7211_gpios[] __initconst = {
	{ EDB7211_LCD_DC_DC_EN,	GPIOF_OUT_INIT_LOW,	"LCD DC-DC" },
	{ EDB7211_LCDEN,	GPIOF_OUT_INIT_LOW,	"LCD POWER" },
};

static struct map_desc edb7211_io_desc[] __initdata = {
	{	/* Memory-mapped extra keyboard row */
		.virtual	= IO_ADDRESS(EP7211_PHYS_EXTKBD),
		.pfn		= __phys_to_pfn(EP7211_PHYS_EXTKBD),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {	/* Flash bank 0 */
		.virtual	= IO_ADDRESS(EP7211_PHYS_FLASH1),
		.pfn		= __phys_to_pfn(EP7211_PHYS_FLASH1),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	}, {	/* Flash bank 1 */
		.virtual	= IO_ADDRESS(EP7211_PHYS_FLASH2),
		.pfn		= __phys_to_pfn(EP7211_PHYS_FLASH2),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	},
};

void __init edb7211_map_io(void)
{
	clps711x_map_io();
	iotable_init(edb7211_io_desc, ARRAY_SIZE(edb7211_io_desc));
}

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
	gpio_request_array(edb7211_gpios, ARRAY_SIZE(edb7211_gpios));

	platform_device_register_data(&platform_bus, "platform-lcd", 0,
				      &edb7211_lcd_power_pdata,
				      sizeof(edb7211_lcd_power_pdata));
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_simple("cs89x0", 0, edb7211_cs8900_resource,
					ARRAY_SIZE(edb7211_cs8900_resource));
}

MACHINE_START(EDB7211, "CL-EDB7211 (EP7211 eval board)")
	/* Maintainer: Jon McClintock */
	.atag_offset	= VIDEORAM_SIZE + 0x100,
	.nr_irqs	= CLPS711X_NR_IRQS,
	.fixup		= fixup_edb7211,
	.reserve	= edb7211_reserve,
	.map_io		= edb7211_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
	.init_machine	= edb7211_init,
	.handle_irq	= clps711x_handle_irq,
	.restart	= clps711x_restart,
MACHINE_END
