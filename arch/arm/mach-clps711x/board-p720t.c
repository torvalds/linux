/*
 *  linux/arch/arm/mach-clps711x/p720t.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/sizes.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand-gpio.h>

#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/syspld.h>

#include <video/platform_lcd.h>

#include "common.h"

#define P720T_USERLED		CLPS711X_GPIO(3, 0)
#define P720T_NAND_CLE		CLPS711X_GPIO(4, 0)
#define P720T_NAND_ALE		CLPS711X_GPIO(4, 1)
#define P720T_NAND_NCE		CLPS711X_GPIO(4, 2)

#define P720T_NAND_BASE		(CLPS711X_SDRAM1_BASE)

static struct resource p720t_nand_resource[] __initdata = {
	DEFINE_RES_MEM(P720T_NAND_BASE, SZ_4),
};

static struct mtd_partition p720t_nand_parts[] __initdata = {
	{
		.name	= "Flash partition 1",
		.offset	= 0,
		.size	= SZ_2M,
	},
	{
		.name	= "Flash partition 2",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct gpio_nand_platdata p720t_nand_pdata __initdata = {
	.gpio_rdy	= -1,
	.gpio_nce	= P720T_NAND_NCE,
	.gpio_ale	= P720T_NAND_ALE,
	.gpio_cle	= P720T_NAND_CLE,
	.gpio_nwp	= -1,
	.chip_delay	= 15,
	.parts		= p720t_nand_parts,
	.num_parts	= ARRAY_SIZE(p720t_nand_parts),
};

static struct platform_device p720t_nand_pdev __initdata = {
	.name		= "gpio-nand",
	.id		= -1,
	.resource	= p720t_nand_resource,
	.num_resources	= ARRAY_SIZE(p720t_nand_resource),
	.dev		= {
		.platform_data = &p720t_nand_pdata,
	},
};

static void p720t_lcd_power_set(struct plat_lcd_data *pd, unsigned int power)
{
	if (power) {
		PLD_LCDEN = PLD_LCDEN_EN;
		PLD_PWR |= PLD_S4_ON | PLD_S2_ON | PLD_S1_ON;
	} else {
		PLD_PWR &= ~(PLD_S4_ON | PLD_S2_ON | PLD_S1_ON);
		PLD_LCDEN = 0;
	}
}

static struct plat_lcd_data p720t_lcd_power_pdata = {
	.set_power	= p720t_lcd_power_set,
};

static void p720t_lcd_backlight_set_intensity(int intensity)
{
	if (intensity)
		PLD_PWR |= PLD_S3_ON;
	else
		PLD_PWR = 0;
}

static struct generic_bl_info p720t_lcd_backlight_pdata = {
	.name			= "lcd-backlight.0",
	.default_intensity	= 0x01,
	.max_intensity		= 0x01,
	.set_bl_intensity	= p720t_lcd_backlight_set_intensity,
};

/*
 * Map the P720T system PLD. It occupies two address spaces:
 * 0x10000000 and 0x10400000. We map both regions as one.
 */
static struct map_desc p720t_io_desc[] __initdata = {
	{
		.virtual	= SYSPLD_VIRT_BASE,
		.pfn		= __phys_to_pfn(SYSPLD_PHYS_BASE),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	},
};

static void __init
fixup_p720t(struct tag *tag, char **cmdline, struct meminfo *mi)
{
	/*
	 * Our bootloader doesn't setup any tags (yet).
	 */
	if (tag->hdr.tag != ATAG_CORE) {
		tag->hdr.tag = ATAG_CORE;
		tag->hdr.size = tag_size(tag_core);
		tag->u.core.flags = 0;
		tag->u.core.pagesize = PAGE_SIZE;
		tag->u.core.rootdev = 0x0100;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_MEM;
		tag->hdr.size = tag_size(tag_mem32);
		tag->u.mem.size = 4096;
		tag->u.mem.start = PHYS_OFFSET;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_NONE;
		tag->hdr.size = 0;
	}
}

static void __init p720t_map_io(void)
{
	clps711x_map_io();
	iotable_init(p720t_io_desc, ARRAY_SIZE(p720t_io_desc));
}

static void __init p720t_init_early(void)
{
	/*
	 * Power down as much as possible in case we don't
	 * have the drivers loaded.
	 */
	PLD_LCDEN = 0;
	PLD_PWR  &= ~(PLD_S4_ON|PLD_S3_ON|PLD_S2_ON|PLD_S1_ON);

	PLD_KBD   = 0;
	PLD_IO    = 0;
	PLD_IRDA  = 0;
	PLD_CODEC = 0;
	PLD_TCH   = 0;
	PLD_SPI   = 0;
	if (!IS_ENABLED(CONFIG_DEBUG_LL)) {
		PLD_COM2 = 0;
		PLD_COM1 = 0;
	}
}

static struct gpio_led p720t_gpio_leds[] = {
	{
		.name			= "User LED",
		.default_trigger	= "heartbeat",
		.gpio			= P720T_USERLED,
	},
};

static struct gpio_led_platform_data p720t_gpio_led_pdata __initdata = {
	.leds		= p720t_gpio_leds,
	.num_leds	= ARRAY_SIZE(p720t_gpio_leds),
};

static void __init p720t_init(void)
{
	platform_device_register(&p720t_nand_pdev);
	platform_device_register_data(&platform_bus, "platform-lcd", 0,
				      &p720t_lcd_power_pdata,
				      sizeof(p720t_lcd_power_pdata));
	platform_device_register_data(&platform_bus, "generic-bl", 0,
				      &p720t_lcd_backlight_pdata,
				      sizeof(p720t_lcd_backlight_pdata));
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
}

static void __init p720t_init_late(void)
{
	platform_device_register_data(&platform_bus, "leds-gpio", 0,
				      &p720t_gpio_led_pdata,
				      sizeof(p720t_gpio_led_pdata));
}

MACHINE_START(P720T, "ARM-Prospector720T")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.atag_offset	= 0x100,
	.nr_irqs	= CLPS711X_NR_IRQS,
	.fixup		= fixup_p720t,
	.map_io		= p720t_map_io,
	.init_early	= p720t_init_early,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.init_machine	= p720t_init,
	.init_late	= p720t_init_late,
	.handle_irq	= clps711x_handle_irq,
	.restart	= clps711x_restart,
MACHINE_END
