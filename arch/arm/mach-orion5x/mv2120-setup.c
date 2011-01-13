/*
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 * Copyright (C) 2008 Martin Michlmayr <tbm@cyrius.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx_eth.h>
#include <linux/leds.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

#define MV2120_NOR_BOOT_BASE	0xf4000000
#define MV2120_NOR_BOOT_SIZE	SZ_512K

#define MV2120_GPIO_RTC_IRQ	3
#define MV2120_GPIO_KEY_RESET	17
#define MV2120_GPIO_KEY_POWER	18
#define MV2120_GPIO_POWER_OFF	19


/*****************************************************************************
 * Ethernet
 ****************************************************************************/
static struct mv643xx_eth_platform_data mv2120_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data mv2120_sata_data = {
	.n_ports	= 2,
};

static struct mtd_partition mv2120_partitions[] = {
	{
		.name	= "firmware",
		.size	= 0x00080000,
		.offset	= 0,
	},
};

static struct physmap_flash_data mv2120_nor_flash_data = {
	.width		= 1,
	.parts		= mv2120_partitions,
	.nr_parts	= ARRAY_SIZE(mv2120_partitions)
};

static struct resource mv2120_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= MV2120_NOR_BOOT_BASE,
	.end		= MV2120_NOR_BOOT_BASE + MV2120_NOR_BOOT_SIZE - 1,
};

static struct platform_device mv2120_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &mv2120_nor_flash_data,
	},
	.resource	= &mv2120_nor_flash_resource,
	.num_resources	= 1,
};

static struct gpio_keys_button mv2120_buttons[] = {
	{
		.code		= KEY_RESTART,
		.gpio		= MV2120_GPIO_KEY_RESET,
		.desc		= "reset",
		.active_low	= 1,
	}, {
		.code		= KEY_POWER,
		.gpio		= MV2120_GPIO_KEY_POWER,
		.desc		= "power",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data mv2120_button_data = {
	.buttons	= mv2120_buttons,
	.nbuttons	= ARRAY_SIZE(mv2120_buttons),
};

static struct platform_device mv2120_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &mv2120_button_data,
	},
};


/****************************************************************************
 * General Setup
 ****************************************************************************/
static struct orion5x_mpp_mode mv2120_mpp_modes[] __initdata = {
	{  0, MPP_GPIO },		/* Sys status LED */
	{  1, MPP_GPIO },		/* Sys error LED */
	{  2, MPP_GPIO },		/* OverTemp interrupt */
	{  3, MPP_GPIO },		/* RTC interrupt */
	{  4, MPP_GPIO },		/* V_LED 5V */
	{  5, MPP_GPIO },		/* V_LED 3.3V */
	{  6, MPP_UNUSED },
	{  7, MPP_UNUSED },
	{  8, MPP_GPIO },		/* SATA 0 fail LED */
	{  9, MPP_GPIO },		/* SATA 1 fail LED */
	{ 10, MPP_UNUSED },
	{ 11, MPP_UNUSED },
	{ 12, MPP_SATA_LED },		/* SATA 0 presence */
	{ 13, MPP_SATA_LED },		/* SATA 1 presence */
	{ 14, MPP_SATA_LED },		/* SATA 0 active */
	{ 15, MPP_SATA_LED },		/* SATA 1 active */
	{ 16, MPP_UNUSED },
	{ 17, MPP_GPIO },		/* Reset button */
	{ 18, MPP_GPIO },		/* Power button */
	{ 19, MPP_GPIO },		/* Power off */
	{ -1 },
};

static struct i2c_board_info __initdata mv2120_i2c_rtc = {
	I2C_BOARD_INFO("pcf8563", 0x51),
	.irq	= 0,
};

static struct gpio_led mv2120_led_pins[] = {
	{
		.name			= "mv2120:blue:health",
		.gpio			= 0,
	},
	{
		.name			= "mv2120:red:health",
		.gpio			= 1,
	},
	{
		.name			= "mv2120:led:bright",
		.gpio			= 4,
		.default_trigger	= "default-on",
	},
	{
		.name			= "mv2120:led:dimmed",
		.gpio			= 5,
	},
	{
		.name			= "mv2120:red:sata0",
		.gpio			= 8,
		.active_low		= 1,
	},
	{
		.name			= "mv2120:red:sata1",
		.gpio			= 9,
		.active_low		= 1,
	},

};

static struct gpio_led_platform_data mv2120_led_data = {
	.leds		= mv2120_led_pins,
	.num_leds	= ARRAY_SIZE(mv2120_led_pins),
};

static struct platform_device mv2120_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &mv2120_led_data,
	}
};

static void mv2120_power_off(void)
{
	pr_info("%s: triggering power-off...\n", __func__);
	gpio_set_value(MV2120_GPIO_POWER_OFF, 0);
}

static void __init mv2120_init(void)
{
	/* Setup basic Orion functions. Need to be called early. */
	orion5x_init();

	orion5x_mpp_conf(mv2120_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	orion5x_ehci0_init();
	orion5x_ehci1_init();
	orion5x_eth_init(&mv2120_eth_data);
	orion5x_i2c_init();
	orion5x_sata_init(&mv2120_sata_data);
	orion5x_uart0_init();
	orion5x_xor_init();

	orion5x_setup_dev_boot_win(MV2120_NOR_BOOT_BASE, MV2120_NOR_BOOT_SIZE);
	platform_device_register(&mv2120_nor_flash);

	platform_device_register(&mv2120_button_device);

	if (gpio_request(MV2120_GPIO_RTC_IRQ, "rtc") == 0) {
		if (gpio_direction_input(MV2120_GPIO_RTC_IRQ) == 0)
			mv2120_i2c_rtc.irq = gpio_to_irq(MV2120_GPIO_RTC_IRQ);
		else
			gpio_free(MV2120_GPIO_RTC_IRQ);
	}
	i2c_register_board_info(0, &mv2120_i2c_rtc, 1);
	platform_device_register(&mv2120_leds);

	/* register mv2120 specific power-off method */
	if (gpio_request(MV2120_GPIO_POWER_OFF, "POWEROFF") != 0 ||
	    gpio_direction_output(MV2120_GPIO_POWER_OFF, 1) != 0)
		pr_err("mv2120: failed to setup power-off GPIO\n");
	pm_power_off = mv2120_power_off;
}

/* Warning: HP uses a wrong mach-type (=526) in their bootloader */
MACHINE_START(MV2120, "HP Media Vault mv2120")
	/* Maintainer: Martin Michlmayr <tbm@cyrius.com> */
	.boot_params	= 0x00000100,
	.init_machine	= mv2120_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32
MACHINE_END
