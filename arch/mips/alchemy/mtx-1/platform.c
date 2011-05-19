/*
 * MTX-1 platform devices registration
 *
 * Copyright (C) 2007-2009, Florian Fainelli <florian@openwrt.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <mtd/mtd-abi.h>

#include <asm/mach-au1x00/au1xxx_eth.h>

static struct gpio_keys_button mtx1_gpio_button[] = {
	{
		.gpio = 207,
		.code = BTN_0,
		.desc = "System button",
	}
};

static struct gpio_keys_platform_data mtx1_buttons_data = {
	.buttons = mtx1_gpio_button,
	.nbuttons = ARRAY_SIZE(mtx1_gpio_button),
};

static struct platform_device mtx1_button = {
	.name = "gpio-keys",
	.id = -1,
	.dev = {
		.platform_data = &mtx1_buttons_data,
	}
};

static struct resource mtx1_wdt_res[] = {
	[0] = {
		.start	= 215,
		.end	= 215,
		.name	= "mtx1-wdt-gpio",
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device mtx1_wdt = {
	.name = "mtx1-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mtx1_wdt_res),
	.resource = mtx1_wdt_res,
};

static struct gpio_led default_leds[] = {
	{
		.name	= "mtx1:green",
		.gpio = 211,
	}, {
		.name = "mtx1:red",
		.gpio = 212,
	},
};

static struct gpio_led_platform_data mtx1_led_data = {
	.num_leds = ARRAY_SIZE(default_leds),
	.leds = default_leds,
};

static struct platform_device mtx1_gpio_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &mtx1_led_data,
	}
};

static struct mtd_partition mtx1_mtd_partitions[] = {
	{
		.name	= "filesystem",
		.size	= 0x01C00000,
		.offset	= 0,
	},
	{
		.name	= "yamon",
		.size	= 0x00100000,
		.offset	= MTDPART_OFS_APPEND,
		.mask_flags = MTD_WRITEABLE,
	},
	{
		.name	= "kernel",
		.size	= 0x002c0000,
		.offset	= MTDPART_OFS_APPEND,
	},
	{
		.name	= "yamon env",
		.size	= 0x00040000,
		.offset	= MTDPART_OFS_APPEND,
	},
};

static struct physmap_flash_data mtx1_flash_data = {
	.width		= 4,
	.nr_parts	= 4,
	.parts		= mtx1_mtd_partitions,
};

static struct resource mtx1_mtd_resource = {
	.start	= 0x1e000000,
	.end	= 0x1fffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mtx1_mtd = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &mtx1_flash_data,
	},
	.num_resources	= 1,
	.resource	= &mtx1_mtd_resource,
};

static struct __initdata platform_device * mtx1_devs[] = {
	&mtx1_gpio_leds,
	&mtx1_wdt,
	&mtx1_button,
	&mtx1_mtd,
};

static struct au1000_eth_platform_data mtx1_au1000_eth0_pdata = {
	.phy_search_highest_addr	= 1,
	.phy1_search_mac0 		= 1,
};

static int __init mtx1_register_devices(void)
{
	int rc;

	au1xxx_override_eth_cfg(0, &mtx1_au1000_eth0_pdata);

	rc = gpio_request(mtx1_gpio_button[0].gpio,
					mtx1_gpio_button[0].desc);
	if (rc < 0) {
		printk(KERN_INFO "mtx1: failed to request %d\n",
					mtx1_gpio_button[0].gpio);
		goto out;
	}
	gpio_direction_input(mtx1_gpio_button[0].gpio);
out:
	return platform_add_devices(mtx1_devs, ARRAY_SIZE(mtx1_devs));
}

arch_initcall(mtx1_register_devices);
