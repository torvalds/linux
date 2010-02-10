/*
 * arch/arm/mach-orion5x/dns323-setup.c
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
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
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
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

#define DNS323_GPIO_LED_RIGHT_AMBER	1
#define DNS323_GPIO_LED_LEFT_AMBER	2
#define DNS323_GPIO_SYSTEM_UP		3
#define DNS323_GPIO_LED_POWER1		4
#define DNS323_GPIO_LED_POWER2		5
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
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	return -1;
}

static struct hw_pci dns323_pci __initdata = {
	.nr_controllers = 2,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= dns323_pci_map_irq,
};

static int __init dns323_dev_id(void)
{
	u32 dev, rev;

	orion5x_pcie_id(&dev, &rev);

	return dev;
}

static int __init dns323_pci_init(void)
{
	/* The 5182 doesn't really use its PCI bus, and initialising PCI
	 * gets in the way of initialising the SATA controller.
	 */
	if (machine_is_dns323() && dns323_dev_id() != MV88F5182_DEV_ID)
		pci_common_init(&dns323_pci);

	return 0;
}

subsys_initcall(dns323_pci_init);

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
	},
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
	.dev		= {
		.platform_data	= &dns323_nor_flash_data,
	},
	.resource	= &dns323_nor_flash_resource,
	.num_resources	= 1,
};

/****************************************************************************
 * Ethernet
 */

static struct mv643xx_eth_platform_data dns323_eth_data = {
	.phy_addr = MV643XX_ETH_PHY_ADDR(8),
};

/* dns323_parse_hex_*() taken from tsx09-common.c; should a common copy of these
 * functions be kept somewhere?
 */
static int __init dns323_parse_hex_nibble(char n)
{
	if (n >= '0' && n <= '9')
		return n - '0';

	if (n >= 'A' && n <= 'F')
		return n - 'A' + 10;

	if (n >= 'a' && n <= 'f')
		return n - 'a' + 10;

	return -1;
}

static int __init dns323_parse_hex_byte(const char *b)
{
	int hi;
	int lo;

	hi = dns323_parse_hex_nibble(b[0]);
	lo = dns323_parse_hex_nibble(b[1]);

	if (hi < 0 || lo < 0)
		return -1;

	return (hi << 4) | lo;
}

static int __init dns323_read_mac_addr(void)
{
	u_int8_t addr[6];
	int i;
	char *mac_page;

	/* MAC address is stored as a regular ol' string in /dev/mtdblock4
	 * (0x007d0000-0x00800000) starting at offset 196480 (0x2ff80).
	 */
	mac_page = ioremap(DNS323_NOR_BOOT_BASE + 0x7d0000 + 196480, 1024);
	if (!mac_page)
		return -ENOMEM;

	/* Sanity check the string we're looking at */
	for (i = 0; i < 5; i++) {
		if (*(mac_page + (i * 3) + 2) != ':') {
			goto error_fail;
		}
	}

	for (i = 0; i < 6; i++)	{
		int byte;

		byte = dns323_parse_hex_byte(mac_page + (i * 3));
		if (byte < 0) {
			goto error_fail;
		}

		addr[i] = byte;
	}

	iounmap(mac_page);
	printk("DNS323: Found ethernet MAC address: ");
	for (i = 0; i < 6; i++)
		printk("%.2x%s", addr[i], (i < 5) ? ":" : ".\n");

	memcpy(dns323_eth_data.mac_addr, addr, 6);

	return 0;

error_fail:
	iounmap(mac_page);
	return -EINVAL;
}

/****************************************************************************
 * GPIO LEDs (simple - doesn't use hardware blinking support)
 */

#define ORION_BLINK_HALF_PERIOD 100 /* ms */

static int dns323_gpio_blink_set(unsigned gpio,
	unsigned long *delay_on, unsigned long *delay_off)
{
	static int value = 0;

	if (!*delay_on && !*delay_off)
		*delay_on = *delay_off = ORION_BLINK_HALF_PERIOD;

	if (ORION_BLINK_HALF_PERIOD == *delay_on
	    && ORION_BLINK_HALF_PERIOD == *delay_off) {
		value = !value;
		orion_gpio_set_blink(gpio, value);
		return 0;
	}

	return -EINVAL;
}

static struct gpio_led dns323_leds[] = {
	{
		.name = "power:blue",
		.gpio = DNS323_GPIO_LED_POWER2,
		.default_trigger = "timer",
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
	.gpio_blink_set = dns323_gpio_blink_set,
};

static struct platform_device dns323_gpio_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &dns323_led_data,
	},
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
	}, {
		.code		= KEY_POWER,
		.gpio		= DNS323_GPIO_KEY_POWER,
		.desc		= "Power Button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data dns323_button_data = {
	.buttons	= dns323_buttons,
	.nbuttons	= ARRAY_SIZE(dns323_buttons),
};

static struct platform_device dns323_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &dns323_button_data,
	},
};

/*****************************************************************************
 * SATA
 */
static struct mv_sata_platform_data dns323_sata_data = {
       .n_ports        = 2,
};

/****************************************************************************
 * General Setup
 */
static struct orion5x_mpp_mode dns323_mv88f5181_mpp_modes[] __initdata = {
	{  0, MPP_PCIE_RST_OUTn },
	{  1, MPP_GPIO },		/* right amber LED (sata ch0) */
	{  2, MPP_GPIO },		/* left amber LED (sata ch1) */
	{  3, MPP_UNUSED },
	{  4, MPP_GPIO },		/* power button LED */
	{  5, MPP_GPIO },		/* power button LED */
	{  6, MPP_GPIO },		/* GMT G751-2f overtemp */
	{  7, MPP_GPIO },		/* M41T80 nIRQ/OUT/SQW */
	{  8, MPP_GPIO },		/* triggers power off */
	{  9, MPP_GPIO },		/* power button switch */
	{ 10, MPP_GPIO },		/* reset button switch */
	{ 11, MPP_UNUSED },
	{ 12, MPP_UNUSED },
	{ 13, MPP_UNUSED },
	{ 14, MPP_UNUSED },
	{ 15, MPP_UNUSED },
	{ 16, MPP_UNUSED },
	{ 17, MPP_UNUSED },
	{ 18, MPP_UNUSED },
	{ 19, MPP_UNUSED },
	{ -1 },
};

static struct orion5x_mpp_mode dns323_mv88f5182_mpp_modes[] __initdata = {
	{  0, MPP_UNUSED },
	{  1, MPP_GPIO },		/* right amber LED (sata ch0) */
	{  2, MPP_GPIO },		/* left amber LED (sata ch1) */
	{  3, MPP_GPIO },		/* system up flag */
	{  4, MPP_GPIO },		/* power button LED */
	{  5, MPP_GPIO },		/* power button LED */
	{  6, MPP_GPIO },		/* GMT G751-2f overtemp */
	{  7, MPP_GPIO },		/* M41T80 nIRQ/OUT/SQW */
	{  8, MPP_GPIO },		/* triggers power off */
	{  9, MPP_GPIO },		/* power button switch */
	{ 10, MPP_GPIO },		/* reset button switch */
	{ 11, MPP_UNUSED },
	{ 12, MPP_SATA_LED },
	{ 13, MPP_SATA_LED },
	{ 14, MPP_SATA_LED },
	{ 15, MPP_SATA_LED },
	{ 16, MPP_UNUSED },
	{ 17, MPP_UNUSED },
	{ 18, MPP_UNUSED },
	{ 19, MPP_UNUSED },
	{ -1 },
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
	}, {
		I2C_BOARD_INFO("lm75", 0x48),
	}, {
		I2C_BOARD_INFO("m41t80", 0x68),
	},
};

/* DNS-323 rev. A specific power off method */
static void dns323a_power_off(void)
{
	pr_info("%s: triggering power-off...\n", __func__);
	gpio_set_value(DNS323_GPIO_POWER_OFF, 1);
}

/* DNS-323 rev B specific power off method */
static void dns323b_power_off(void)
{
	pr_info("%s: triggering power-off...\n", __func__);
	/* Pin has to be changed to 1 and back to 0 to do actual power off. */
	gpio_set_value(DNS323_GPIO_POWER_OFF, 1);
	mdelay(100);
	gpio_set_value(DNS323_GPIO_POWER_OFF, 0);
}

static void __init dns323_init(void)
{
	/* Setup basic Orion functions. Need to be called early. */
	orion5x_init();

	/* Just to be tricky, the 5182 has a completely different
	 * set of MPP modes to the 5181.
	 */
	if (dns323_dev_id() == MV88F5182_DEV_ID)
		orion5x_mpp_conf(dns323_mv88f5182_mpp_modes);
	else {
		orion5x_mpp_conf(dns323_mv88f5181_mpp_modes);
		writel(0, MPP_DEV_CTRL);		/* DEV_D[31:16] */
	}

	/* setup flash mapping
	 * CS3 holds a 8 MB Spansion S29GL064M90TFIR4
	 */
	orion5x_setup_dev_boot_win(DNS323_NOR_BOOT_BASE, DNS323_NOR_BOOT_SIZE);
	platform_device_register(&dns323_nor_flash);

	/* The 5181 power LED is active low and requires
	 * DNS323_GPIO_LED_POWER1 to also be low.
	 */
	if (dns323_dev_id() == MV88F5181_DEV_ID) {
		dns323_leds[0].active_low = 1;
		gpio_direction_output(DNS323_GPIO_LED_POWER1, 0);
	}

	platform_device_register(&dns323_gpio_leds);

	platform_device_register(&dns323_button_device);

	i2c_register_board_info(0, dns323_i2c_devices,
				ARRAY_SIZE(dns323_i2c_devices));

	/*
	 * Configure peripherals.
	 */
	if (dns323_read_mac_addr() < 0)
		printk("DNS323: Failed to read MAC address\n");

	orion5x_ehci0_init();
	orion5x_eth_init(&dns323_eth_data);
	orion5x_i2c_init();
	orion5x_uart0_init();

	/* The 5182 has its SATA controller on-chip, and needs its own little
	 * init routine.
	 */
	if (dns323_dev_id() == MV88F5182_DEV_ID)
		orion5x_sata_init(&dns323_sata_data);

	/* The 5182 has flag to indicate the system is up. Without this flag
	 * set, power LED will flash and cannot be controlled via leds-gpio.
	 */
	if (dns323_dev_id() == MV88F5182_DEV_ID)
		gpio_set_value(DNS323_GPIO_SYSTEM_UP, 1);

	/* Register dns323 specific power-off method */
	if (gpio_request(DNS323_GPIO_POWER_OFF, "POWEROFF") != 0 ||
	    gpio_direction_output(DNS323_GPIO_POWER_OFF, 0) != 0)
		pr_err("DNS323: failed to setup power-off GPIO\n");
	if (dns323_dev_id() == MV88F5182_DEV_ID)
		pm_power_off = dns323b_power_off;
	else
		pm_power_off = dns323a_power_off;
}

/* Warning: D-Link uses a wrong mach-type (=526) in their bootloader */
MACHINE_START(DNS323, "D-Link DNS-323")
	/* Maintainer: Herbert Valerio Riedel <hvr@gnu.org> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= dns323_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
