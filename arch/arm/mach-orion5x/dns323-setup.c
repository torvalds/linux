/*
 * arch/arm/mach-orion5x/dns323-setup.c
 *
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Support for HW Rev C1:
 *
 * Copyright (C) 2010 Benjamin Herrenschmidt <benh@kernel.crashing.org>
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
#include <linux/phy.h>
#include <linux/marvell_phy.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"

/* Rev A1 and B1 */
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

/* Rev C1 */
#define DNS323C_GPIO_KEY_POWER		1
#define DNS323C_GPIO_POWER_OFF		2
#define DNS323C_GPIO_LED_RIGHT_AMBER	8
#define DNS323C_GPIO_LED_LEFT_AMBER	9
#define DNS323C_GPIO_LED_POWER		17
#define DNS323C_GPIO_FAN_BIT1		18
#define DNS323C_GPIO_FAN_BIT0		19

/* Exposed to userspace, do not change */
enum {
	DNS323_REV_A1,	/* 0 */
	DNS323_REV_B1,	/* 1 */
	DNS323_REV_C1,	/* 2 */
};


/****************************************************************************
 * PCI setup
 */

static int __init dns323_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
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

static int __init dns323_pci_init(void)
{
	/* Rev B1 and C1 doesn't really use its PCI bus, and initialising PCI
	 * gets in the way of initialising the SATA controller.
	 */
	if (machine_is_dns323() && system_rev == DNS323_REV_A1)
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
	printk("DNS-323: Found ethernet MAC address: ");
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

static int dns323_gpio_blink_set(unsigned gpio, int state,
	unsigned long *delay_on, unsigned long *delay_off)
{

	if (delay_on && delay_off && !*delay_on && !*delay_off)
		*delay_on = *delay_off = ORION_BLINK_HALF_PERIOD;

	switch(state) {
	case GPIO_LED_NO_BLINK_LOW:
	case GPIO_LED_NO_BLINK_HIGH:
		orion_gpio_set_blink(gpio, 0);
		gpio_set_value(gpio, state);
		break;
	case GPIO_LED_BLINK:
		orion_gpio_set_blink(gpio, 1);
	}
	return 0;
}

static struct gpio_led dns323ab_leds[] = {
	{
		.name = "power:blue",
		.gpio = DNS323_GPIO_LED_POWER2,
		.default_trigger = "default-on",
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


static struct gpio_led dns323c_leds[] = {
	{
		.name = "power:blue",
		.gpio = DNS323C_GPIO_LED_POWER,
		.default_trigger = "timer",
		.active_low = 1,
	}, {
		.name = "right:amber",
		.gpio = DNS323C_GPIO_LED_RIGHT_AMBER,
		.active_low = 1,
	}, {
		.name = "left:amber",
		.gpio = DNS323C_GPIO_LED_LEFT_AMBER,
		.active_low = 1,
	},
};


static struct gpio_led_platform_data dns323ab_led_data = {
	.num_leds	= ARRAY_SIZE(dns323ab_leds),
	.leds		= dns323ab_leds,
	.gpio_blink_set = dns323_gpio_blink_set,
};

static struct gpio_led_platform_data dns323c_led_data = {
	.num_leds	= ARRAY_SIZE(dns323c_leds),
	.leds		= dns323c_leds,
	.gpio_blink_set = dns323_gpio_blink_set,
};

static struct platform_device dns323_gpio_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &dns323ab_led_data,
	},
};

/****************************************************************************
 * GPIO Attached Keys
 */

static struct gpio_keys_button dns323ab_buttons[] = {
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

static struct gpio_keys_platform_data dns323ab_button_data = {
	.buttons	= dns323ab_buttons,
	.nbuttons	= ARRAY_SIZE(dns323ab_buttons),
};

static struct gpio_keys_button dns323c_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= DNS323C_GPIO_KEY_POWER,
		.desc		= "Power Button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data dns323c_button_data = {
	.buttons	= dns323c_buttons,
	.nbuttons	= ARRAY_SIZE(dns323c_buttons),
};

static struct platform_device dns323_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &dns323ab_button_data,
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
static unsigned int dns323a_mpp_modes[] __initdata = {
	MPP0_PCIE_RST_OUTn,
	MPP1_GPIO,		/* right amber LED (sata ch0) */
	MPP2_GPIO,		/* left amber LED (sata ch1) */
	MPP3_UNUSED,
	MPP4_GPIO,		/* power button LED */
	MPP5_GPIO,		/* power button LED */
	MPP6_GPIO,		/* GMT G751-2f overtemp */
	MPP7_GPIO,		/* M41T80 nIRQ/OUT/SQW */
	MPP8_GPIO,		/* triggers power off */
	MPP9_GPIO,		/* power button switch */
	MPP10_GPIO,		/* reset button switch */
	MPP11_UNUSED,
	MPP12_UNUSED,
	MPP13_UNUSED,
	MPP14_UNUSED,
	MPP15_UNUSED,
	MPP16_UNUSED,
	MPP17_UNUSED,
	MPP18_UNUSED,
	MPP19_UNUSED,
	0,
};

static unsigned int dns323b_mpp_modes[] __initdata = {
	MPP0_UNUSED,
	MPP1_GPIO,		/* right amber LED (sata ch0) */
	MPP2_GPIO,		/* left amber LED (sata ch1) */
	MPP3_GPIO,		/* system up flag */
	MPP4_GPIO,		/* power button LED */
	MPP5_GPIO,		/* power button LED */
	MPP6_GPIO,		/* GMT G751-2f overtemp */
	MPP7_GPIO,		/* M41T80 nIRQ/OUT/SQW */
	MPP8_GPIO,		/* triggers power off */
	MPP9_GPIO,		/* power button switch */
	MPP10_GPIO,		/* reset button switch */
	MPP11_UNUSED,
	MPP12_SATA_LED,
	MPP13_SATA_LED,
	MPP14_SATA_LED,
	MPP15_SATA_LED,
	MPP16_UNUSED,
	MPP17_UNUSED,
	MPP18_UNUSED,
	MPP19_UNUSED,
	0,
};

static unsigned int dns323c_mpp_modes[] __initdata = {
	MPP0_GPIO,		/* ? input */
	MPP1_GPIO,		/* input power switch (0 = pressed) */
	MPP2_GPIO,		/* output power off */
	MPP3_UNUSED,		/* ? output */
	MPP4_UNUSED,		/* ? output */
	MPP5_UNUSED,		/* ? output */
	MPP6_UNUSED,		/* ? output */
	MPP7_UNUSED,		/* ? output */
	MPP8_GPIO,		/* i/o right amber LED */
	MPP9_GPIO,		/* i/o left amber LED */
	MPP10_GPIO,		/* input */
	MPP11_UNUSED,
	MPP12_SATA_LED,
	MPP13_SATA_LED,
	MPP14_SATA_LED,
	MPP15_SATA_LED,
	MPP16_UNUSED,
	MPP17_GPIO,		/* power button LED */
	MPP18_GPIO,		/* fan speed bit 0 */
	MPP19_GPIO,		/* fan speed bit 1 */
	0,
};

/* Rev C1 Fan speed notes:
 *
 * The fan is controlled by 2 GPIOs on this board. The settings
 * of the bits is as follow:
 *
 *  GPIO 18    GPIO 19    Fan
 *
 *    0          0        stopped
 *    0          1        low speed
 *    1          0        high speed
 *    1          1        don't do that (*)
 *
 * (*) I think the two bits control two feed-in resistors into a fixed
 *     PWN circuit, setting both bits will basically go a 'bit' faster
 *     than high speed, but d-link doesn't do it and you may get out of
 *     HW spec so don't do it.
 */

/*
 * On the DNS-323 A1 and B1 the following devices are attached via I2C:
 *
 *  i2c addr | chip        | description
 *  0x3e     | GMT G760Af  | fan speed PWM controller
 *  0x48     | GMT G751-2f | temp. sensor and therm. watchdog (LM75 compatible)
 *  0x68     | ST M41T80   | RTC w/ alarm
 */
static struct i2c_board_info __initdata dns323ab_i2c_devices[] = {
	{
		I2C_BOARD_INFO("g760a", 0x3e),
	}, {
		I2C_BOARD_INFO("lm75", 0x48),
	}, {
		I2C_BOARD_INFO("m41t80", 0x68),
	},
};

/*
 * On the DNS-323 C1 the following devices are attached via I2C:
 *
 *  i2c addr | chip        | description
 *  0x48     | GMT G751-2f | temp. sensor and therm. watchdog (LM75 compatible)
 *  0x68     | ST M41T80   | RTC w/ alarm
 */
static struct i2c_board_info __initdata dns323c_i2c_devices[] = {
	{
		I2C_BOARD_INFO("lm75", 0x48),
	}, {
		I2C_BOARD_INFO("m41t80", 0x68),
	},
};

/* DNS-323 rev. A specific power off method */
static void dns323a_power_off(void)
{
	pr_info("DNS-323: Triggering power-off...\n");
	gpio_set_value(DNS323_GPIO_POWER_OFF, 1);
}

/* DNS-323 rev B specific power off method */
static void dns323b_power_off(void)
{
	pr_info("DNS-323: Triggering power-off...\n");
	/* Pin has to be changed to 1 and back to 0 to do actual power off. */
	gpio_set_value(DNS323_GPIO_POWER_OFF, 1);
	mdelay(100);
	gpio_set_value(DNS323_GPIO_POWER_OFF, 0);
}

/* DNS-323 rev. C specific power off method */
static void dns323c_power_off(void)
{
	pr_info("DNS-323: Triggering power-off...\n");
	gpio_set_value(DNS323C_GPIO_POWER_OFF, 1);
}

static int dns323c_phy_fixup(struct phy_device *phy)
{
	phy->dev_flags |= MARVELL_PHY_M1118_DNS323_LEDS;

	return 0;
}

static int __init dns323_identify_rev(void)
{
	u32 dev, rev, i, reg;

	pr_debug("DNS-323: Identifying board ... \n");

	/* Rev A1 has a 5181 */
	orion5x_pcie_id(&dev, &rev);
	if (dev == MV88F5181_DEV_ID) {
		pr_debug("DNS-323: 5181 found, board is A1\n");
		return DNS323_REV_A1;
	}
	pr_debug("DNS-323: 5182 found, board is B1 or C1, checking PHY...\n");

	/* Rev B1 and C1 both have 5182, let's poke at the eth PHY. This is
	 * a bit gross but we want to do that without links into the eth
	 * driver so let's poke at it directly. We default to rev B1 in
	 * case the accesses fail
	 */

#define ETH_SMI_REG		(ORION5X_ETH_VIRT_BASE + 0x2000 + 0x004)
#define  SMI_BUSY		0x10000000
#define  SMI_READ_VALID		0x08000000
#define  SMI_OPCODE_READ	0x04000000
#define  SMI_OPCODE_WRITE	0x00000000

	for (i = 0; i < 1000; i++) {
		reg = readl(ETH_SMI_REG);
		if (!(reg & SMI_BUSY))
			break;
	}
	if (i >= 1000) {
		pr_warning("DNS-323: Timeout accessing PHY, assuming rev B1\n");
		return DNS323_REV_B1;
	}
	writel((3 << 21)	/* phy ID reg */ |
	       (8 << 16)	/* phy addr */ |
	       SMI_OPCODE_READ, ETH_SMI_REG);
	for (i = 0; i < 1000; i++) {
		reg = readl(ETH_SMI_REG);
		if (reg & SMI_READ_VALID)
			break;
	}
	if (i >= 1000) {
		pr_warning("DNS-323: Timeout reading PHY, assuming rev B1\n");
		return DNS323_REV_B1;
	}
	pr_debug("DNS-323: Ethernet PHY ID 0x%x\n", reg & 0xffff);

	/* Note: the Marvell tools mask the ID with 0x3f0 before comparison
	 * but I don't see that making a difference here, at least with
	 * any known Marvell PHY ID
	 */
	switch(reg & 0xfff0) {
	case 0x0cc0: /* MV88E1111 */
		return DNS323_REV_B1;
	case 0x0e10: /* MV88E1118 */
		return DNS323_REV_C1;
	default:
		pr_warning("DNS-323: Unknown PHY ID 0x%04x, assuming rev B1\n",
			   reg & 0xffff);
	}
	return DNS323_REV_B1;
}

static void __init dns323_init(void)
{
	/* Setup basic Orion functions. Need to be called early. */
	orion5x_init();

	/* Identify revision */
	system_rev = dns323_identify_rev();
	pr_info("DNS-323: Identified HW revision %c1\n", 'A' + system_rev);

	/* Just to be tricky, the 5182 has a completely different
	 * set of MPP modes to the 5181.
	 */
	switch(system_rev) {
	case DNS323_REV_A1:
		orion5x_mpp_conf(dns323a_mpp_modes);
		writel(0, MPP_DEV_CTRL);		/* DEV_D[31:16] */
		break;
	case DNS323_REV_B1:
		orion5x_mpp_conf(dns323b_mpp_modes);
		break;
	case DNS323_REV_C1:
		orion5x_mpp_conf(dns323c_mpp_modes);
		break;
	}

	/* setup flash mapping
	 * CS3 holds a 8 MB Spansion S29GL064M90TFIR4
	 */
	orion5x_setup_dev_boot_win(DNS323_NOR_BOOT_BASE, DNS323_NOR_BOOT_SIZE);
	platform_device_register(&dns323_nor_flash);

	/* Sort out LEDs, Buttons and i2c devices */
	switch(system_rev) {
	case DNS323_REV_A1:
		/* The 5181 power LED is active low and requires
		 * DNS323_GPIO_LED_POWER1 to also be low.
		 */
		 dns323ab_leds[0].active_low = 1;
		 gpio_request(DNS323_GPIO_LED_POWER1, "Power Led Enable");
		 gpio_direction_output(DNS323_GPIO_LED_POWER1, 0);
		/* Fall through */
	case DNS323_REV_B1:
		i2c_register_board_info(0, dns323ab_i2c_devices,
				ARRAY_SIZE(dns323ab_i2c_devices));
		break;
	case DNS323_REV_C1:
		/* Hookup LEDs & Buttons */
		dns323_gpio_leds.dev.platform_data = &dns323c_led_data;
		dns323_button_device.dev.platform_data = &dns323c_button_data;

		/* Hookup i2c devices and fan driver */
		i2c_register_board_info(0, dns323c_i2c_devices,
				ARRAY_SIZE(dns323c_i2c_devices));
		platform_device_register_simple("dns323c-fan", 0, NULL, 0);

		/* Register fixup for the PHY LEDs */
		phy_register_fixup_for_uid(MARVELL_PHY_ID_88E1118,
					   MARVELL_PHY_ID_MASK,
					   dns323c_phy_fixup);
	}

	platform_device_register(&dns323_gpio_leds);
	platform_device_register(&dns323_button_device);

	/*
	 * Configure peripherals.
	 */
	if (dns323_read_mac_addr() < 0)
		printk("DNS-323: Failed to read MAC address\n");
	orion5x_ehci0_init();
	orion5x_eth_init(&dns323_eth_data);
	orion5x_i2c_init();
	orion5x_uart0_init();

	/* Remaining GPIOs */
	switch(system_rev) {
	case DNS323_REV_A1:
		/* Poweroff GPIO */
		if (gpio_request(DNS323_GPIO_POWER_OFF, "POWEROFF") != 0 ||
		    gpio_direction_output(DNS323_GPIO_POWER_OFF, 0) != 0)
			pr_err("DNS-323: failed to setup power-off GPIO\n");
		pm_power_off = dns323a_power_off;
		break;
	case DNS323_REV_B1:
		/* 5182 built-in SATA init */
		orion5x_sata_init(&dns323_sata_data);

		/* The DNS323 rev B1 has flag to indicate the system is up.
		 * Without this flag set, power LED will flash and cannot be
		 * controlled via leds-gpio.
		 */
		if (gpio_request(DNS323_GPIO_SYSTEM_UP, "SYS_READY") == 0)
			gpio_direction_output(DNS323_GPIO_SYSTEM_UP, 1);

		/* Poweroff GPIO */
		if (gpio_request(DNS323_GPIO_POWER_OFF, "POWEROFF") != 0 ||
		    gpio_direction_output(DNS323_GPIO_POWER_OFF, 0) != 0)
			pr_err("DNS-323: failed to setup power-off GPIO\n");
		pm_power_off = dns323b_power_off;
		break;
	case DNS323_REV_C1:
		/* 5182 built-in SATA init */
		orion5x_sata_init(&dns323_sata_data);

		/* Poweroff GPIO */
		if (gpio_request(DNS323C_GPIO_POWER_OFF, "POWEROFF") != 0 ||
		    gpio_direction_output(DNS323C_GPIO_POWER_OFF, 0) != 0)
			pr_err("DNS-323: failed to setup power-off GPIO\n");
		pm_power_off = dns323c_power_off;

		/* Now, -this- should theorically be done by the sata_mv driver
		 * once I figure out what's going on there. Maybe the behaviour
		 * of the LEDs should be somewhat passed via the platform_data.
		 * for now, just whack the register and make the LEDs happy
		 *
		 * Note: AFAIK, rev B1 needs the same treatement but I'll let
		 * somebody else test it.
		 */
		writel(0x5, ORION5X_SATA_VIRT_BASE | 0x2c);
		break;
	}
}

/* Warning: D-Link uses a wrong mach-type (=526) in their bootloader */
MACHINE_START(DNS323, "D-Link DNS-323")
	/* Maintainer: Herbert Valerio Riedel <hvr@gnu.org> */
	.boot_params	= 0x00000100,
	.init_machine	= dns323_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
