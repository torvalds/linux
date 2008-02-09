/*
 * arch/arm/mach-orion/dns323-setup.c
 *
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <asm/arch/orion.h>
#include <asm/arch/platform.h>
#include "common.h"

#define DNS323_GPIO_LED_RIGHT_AMBER	1
#define DNS323_GPIO_LED_LEFT_AMBER	2
#define DNS323_GPIO_LED_POWER		5
#define DNS323_GPIO_OVERTEMP		6
#define DNS323_GPIO_RTC			7
#define DNS323_GPIO_POWER_OFF		8
#define DNS323_GPIO_KEY_POWER		9
#define DNS323_GPIO_KEY_RESET		10

/****************************************************************************
 * PCI setup
 */

static int __init dns323_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* PCI-E */
	if (dev->bus->number == orion_pcie_local_bus_nr())
		return IRQ_ORION_PCIE0_INT;

	pr_err("%s: requested mapping for unknown bus\n", __func__);

	return -1;
}

static struct hw_pci dns323_pci __initdata = {
	.nr_controllers = 1,
	.swizzle	= pci_std_swizzle,
	.setup		= orion_pci_sys_setup,
	.scan		= orion_pci_sys_scan_bus,
	.map_irq	= dns323_pci_map_irq,
};

static int __init dns323_pci_init(void)
{
	if (machine_is_dns323())
		pci_common_init(&dns323_pci);

	return 0;
}

subsys_initcall(dns323_pci_init);

/****************************************************************************
 * Ethernet
 */

static struct mv643xx_eth_platform_data dns323_eth_data = {
	.phy_addr = 8,
	.force_phy_addr = 1,
};

/****************************************************************************
 * 8MiB NOR flash (Spansion S29GL064M90TFIR4)
 *
 * Layout as used by D-Link:
 *  0x00000000-0x00010000 : "MTD1"
 *  0x00010000-0x00020000 : "MTD2"
 *  0x00020000-0x001a0000 : "Linux Kernel"
 *  0x001a0000-0x007d0000 : "File System"
 *  0x007d0000-0x00800000 : "u-boot"
 */

#define DNS323_NOR_BOOT_BASE 0xf4000000
#define DNS323_NOR_BOOT_SIZE SZ_8M

static struct mtd_partition dns323_partitions[] = {
	{
		.name	= "MTD1",
		.size	= 0x00010000,
		.offset	= 0,
	}, {
		.name	= "MTD2",
		.size	= 0x00010000,
		.offset = 0x00010000,
	}, {
		.name	= "Linux Kernel",
		.size	= 0x00180000,
		.offset	= 0x00020000,
	}, {
		.name	= "File System",
		.size	= 0x00630000,
		.offset	= 0x001A0000,
	}, {
		.name	= "u-boot",
		.size	= 0x00030000,
		.offset	= 0x007d0000,
	}
};

static struct physmap_flash_data dns323_nor_flash_data = {
	.width		= 1,
	.parts		= dns323_partitions,
	.nr_parts	= ARRAY_SIZE(dns323_partitions)
};

static struct resource dns323_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= DNS323_NOR_BOOT_BASE,
	.end		= DNS323_NOR_BOOT_BASE + DNS323_NOR_BOOT_SIZE - 1,
};

static struct platform_device dns323_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= { .platform_data = &dns323_nor_flash_data, },
	.resource	= &dns323_nor_flash_resource,
	.num_resources	= 1,
};

/****************************************************************************
 * GPIO LEDs (simple - doesn't use hardware blinking support)
 */

static struct gpio_led dns323_leds[] = {
	{
		.name = "power:blue",
		.gpio = DNS323_GPIO_LED_POWER,
		.active_low = 1,
	}, {
		.name = "right:amber",
		.gpio = DNS323_GPIO_LED_RIGHT_AMBER,
		.active_low = 1,
	}, {
		.name = "left:amber",
		.gpio = DNS323_GPIO_LED_LEFT_AMBER,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data dns323_led_data = {
	.num_leds	= ARRAY_SIZE(dns323_leds),
	.leds		= dns323_leds,
};

static struct platform_device dns323_gpio_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= { .platform_data = &dns323_led_data, },
};

/****************************************************************************
 * GPIO Attached Keys
 */

static struct gpio_keys_button dns323_buttons[] = {
	{
		.code		= KEY_RESTART,
		.gpio		= DNS323_GPIO_KEY_RESET,
		.desc		= "Reset Button",
		.active_low	= 1,
	},
	{
		.code		= KEY_POWER,
		.gpio		= DNS323_GPIO_KEY_POWER,
		.desc		= "Power Button",
		.active_low	= 1,
	}
};

static struct gpio_keys_platform_data dns323_button_data = {
	.buttons	= dns323_buttons,
	.nbuttons       = ARRAY_SIZE(dns323_buttons),
};

static struct platform_device dns323_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= { .platform_data  = &dns323_button_data, },
};

/****************************************************************************
 * General Setup
 */

static struct platform_device *dns323_plat_devices[] __initdata = {
	&dns323_nor_flash,
	&dns323_gpio_leds,
	&dns323_button_device,
};

/*
 * On the DNS-323 the following devices are attached via I2C:
 *
 *  i2c addr | chip        | description
 *  0x3e     | GMT G760Af  | fan speed PWM controller
 *  0x48     | GMT G751-2f | temp. sensor and therm. watchdog (LM75 compatible)
 *  0x68     | ST M41T80   | RTC w/ alarm
 */
static struct i2c_board_info __initdata dns323_i2c_devices[] = {
	{
		I2C_BOARD_INFO("g760a", 0x3e),
		.type = "g760a",
	},
#if 0
	/* this entry requires the new-style driver model lm75 driver,
	 * for the meantime "insmod lm75.ko force_lm75=0,0x48" is needed */
	{
		I2C_BOARD_INFO("lm75", 0x48),
		.type = "g751",
	},
#endif
	{
		I2C_BOARD_INFO("rtc-m41t80", 0x68),
		.type = "m41t80",
	}
};

/* DNS-323 specific power off method */
static void dns323_power_off(void)
{
	pr_info("%s: triggering power-off...\n", __func__);
	gpio_set_value(DNS323_GPIO_POWER_OFF, 1);
}

static void __init dns323_init(void)
{
	/* Setup basic Orion functions. Need to be called early. */
	orion_init();

	/* setup flash mapping
	 * CS3 holds a 8 MB Spansion S29GL064M90TFIR4
	 */
	orion_setup_cpu_win(ORION_DEV_BOOT, DNS323_NOR_BOOT_BASE,
			    DNS323_NOR_BOOT_SIZE, -1);

	/* DNS-323 has a Marvell 88X7042 SATA controller attached via PCIE
	 *
	 * Open a special address decode windows for the PCIE WA.
	 */
	orion_write(ORION_REGS_VIRT_BASE | 0x20074, ORION_PCIE_WA_PHYS_BASE);
	orion_write(ORION_REGS_VIRT_BASE | 0x20070,
		    (0x7941 | (((ORION_PCIE_WA_SIZE >> 16) - 1)) << 16));

	/* set MPP to 0 as D-Link's 2.6.12.6 kernel did */
	orion_write(MPP_0_7_CTRL, 0);
	orion_write(MPP_8_15_CTRL, 0);
	orion_write(MPP_16_19_CTRL, 0);
	orion_write(MPP_DEV_CTRL, 0);

	/* Define used GPIO pins

	  GPIO Map:

	  |  0 |     | PEX_RST_OUT (not controlled by GPIO)
	  |  1 | Out | right amber LED (= sata ch0 LED)  (low-active)
	  |  2 | Out | left  amber LED (= sata ch1 LED)  (low-active)
	  |  3 | Out | //unknown//
	  |  4 | Out | power button LED (low-active, together with pin #5)
	  |  5 | Out | power button LED (low-active, together with pin #4)
	  |  6 | In  | GMT G751-2f overtemp. shutdown signal (low-active)
	  |  7 | In  | M41T80 nIRQ/OUT/SQW signal
	  |  8 | Out | triggers power off (high-active)
	  |  9 | In  | power button switch (low-active)
	  | 10 | In  | reset button switch (low-active)
	  | 11 | Out | //unknown//
	  | 12 | Out | //unknown//
	  | 13 | Out | //unknown//
	  | 14 | Out | //unknown//
	  | 15 | Out | //unknown//
	*/
	orion_gpio_set_valid_pins(0x07f6);

	/* register dns323 specific power-off method */
	if ((gpio_request(DNS323_GPIO_POWER_OFF, "POWEROFF") != 0)
	    || (gpio_direction_output(DNS323_GPIO_POWER_OFF, 0) != 0))
		pr_err("DNS323: failed to setup power-off GPIO\n");

	pm_power_off = dns323_power_off;

	/* register flash and other platform devices */
	platform_add_devices(dns323_plat_devices,
			     ARRAY_SIZE(dns323_plat_devices));

	i2c_register_board_info(0, dns323_i2c_devices,
				ARRAY_SIZE(dns323_i2c_devices));

	orion_eth_init(&dns323_eth_data);
}

/* Warning: D-Link uses a wrong mach-type (=526) in their bootloader */
MACHINE_START(DNS323, "D-Link DNS-323")
	/* Maintainer: Herbert Valerio Riedel <hvr@gnu.org> */
	.phys_io	= ORION_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= dns323_init,
	.map_io		= orion_map_io,
	.init_irq	= orion_init_irq,
	.timer		= &orion_timer,
MACHINE_END
