/*
 * QNAP TS-409 Board Setup
 *
 * Maintainer: Sylver Bruneau <sylver.bruneau@gmail.com>
 *
 * Copyright (C) 2008  Sylver Bruneau <sylver.bruneau@gmail.com>
 * Copyright (C) 2008  Martin Michlmayr <tbm@cyrius.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/gpio.h>
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
#include <linux/serial_reg.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/orion5x.h>
#include "common.h"
#include "mpp.h"
#include "tsx09-common.h"

/*****************************************************************************
 * QNAP TS-409 Info
 ****************************************************************************/

/*
 * QNAP TS-409 hardware :
 * - Marvell 88F5281-D0
 * - Marvell 88SX7042 SATA controller (PCIe)
 * - Marvell 88E1118 Gigabit Ethernet PHY
 * - RTC S35390A (@0x30) on I2C bus
 * - 8MB NOR flash
 * - 256MB of DDR-2 RAM
 */

/*
 * 8MB NOR flash Device bus boot chip select
 */

#define QNAP_TS409_NOR_BOOT_BASE 0xff800000
#define QNAP_TS409_NOR_BOOT_SIZE SZ_8M

/****************************************************************************
 * 8MiB NOR flash. The struct mtd_partition is not in the same order as the
 *     partitions on the device because we want to keep compatibility with
 *     existing QNAP firmware.
 *
 * Layout as used by QNAP:
 *  [2] 0x00000000-0x00200000 : "Kernel"
 *  [3] 0x00200000-0x00600000 : "RootFS1"
 *  [4] 0x00600000-0x00700000 : "RootFS2"
 *  [6] 0x00700000-0x00760000 : "NAS Config" (read-only)
 *  [5] 0x00760000-0x00780000 : "U-Boot Config"
 *  [1] 0x00780000-0x00800000 : "U-Boot" (read-only)
 ***************************************************************************/
static struct mtd_partition qnap_ts409_partitions[] = {
	{
		.name		= "U-Boot",
		.size		= 0x00080000,
		.offset		= 0x00780000,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "Kernel",
		.size		= 0x00200000,
		.offset		= 0,
	}, {
		.name		= "RootFS1",
		.size		= 0x00400000,
		.offset		= 0x00200000,
	}, {
		.name		= "RootFS2",
		.size		= 0x00100000,
		.offset		= 0x00600000,
	}, {
		.name		= "U-Boot Config",
		.size		= 0x00020000,
		.offset		= 0x00760000,
	}, {
		.name		= "NAS Config",
		.size		= 0x00060000,
		.offset		= 0x00700000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data qnap_ts409_nor_flash_data = {
	.width		= 1,
	.parts		= qnap_ts409_partitions,
	.nr_parts	= ARRAY_SIZE(qnap_ts409_partitions)
};

static struct resource qnap_ts409_nor_flash_resource = {
	.flags	= IORESOURCE_MEM,
	.start	= QNAP_TS409_NOR_BOOT_BASE,
	.end	= QNAP_TS409_NOR_BOOT_BASE + QNAP_TS409_NOR_BOOT_SIZE - 1,
};

static struct platform_device qnap_ts409_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= { .platform_data = &qnap_ts409_nor_flash_data, },
	.num_resources	= 1,
	.resource	= &qnap_ts409_nor_flash_resource,
};

/*****************************************************************************
 * PCI
 ****************************************************************************/

static int __init qnap_ts409_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * PCI isn't used on the TS-409
	 */
	return -1;
}

static struct hw_pci qnap_ts409_pci __initdata = {
	.nr_controllers	= 2,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= qnap_ts409_pci_map_irq,
};

static int __init qnap_ts409_pci_init(void)
{
	if (machine_is_ts409())
		pci_common_init(&qnap_ts409_pci);

	return 0;
}

subsys_initcall(qnap_ts409_pci_init);

/*****************************************************************************
 * RTC S35390A on I2C bus
 ****************************************************************************/

#define TS409_RTC_GPIO	10

static struct i2c_board_info __initdata qnap_ts409_i2c_rtc = {
	I2C_BOARD_INFO("s35390a", 0x30),
};

/*****************************************************************************
 * LEDs attached to GPIO
 ****************************************************************************/

static struct gpio_led ts409_led_pins[] = {
	{
		.name		= "ts409:red:sata1",
		.gpio		= 4,
		.active_low	= 1,
	}, {
		.name		= "ts409:red:sata2",
		.gpio		= 5,
		.active_low	= 1,
	}, {
		.name		= "ts409:red:sata3",
		.gpio		= 6,
		.active_low	= 1,
	}, {
		.name		= "ts409:red:sata4",
		.gpio		= 7,
		.active_low	= 1,
	},
};

static struct gpio_led_platform_data ts409_led_data = {
	.leds		= ts409_led_pins,
	.num_leds	= ARRAY_SIZE(ts409_led_pins),
};

static struct platform_device ts409_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &ts409_led_data,
	},
};

/****************************************************************************
 * GPIO Attached Keys
 *     Power button is attached to the PIC microcontroller
 ****************************************************************************/

#define QNAP_TS409_GPIO_KEY_RESET	14
#define QNAP_TS409_GPIO_KEY_MEDIA	15

static struct gpio_keys_button qnap_ts409_buttons[] = {
	{
		.code		= KEY_RESTART,
		.gpio		= QNAP_TS409_GPIO_KEY_RESET,
		.desc		= "Reset Button",
		.active_low	= 1,
	}, {
		.code		= KEY_COPY,
		.gpio		= QNAP_TS409_GPIO_KEY_MEDIA,
		.desc		= "USB Copy Button",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data qnap_ts409_button_data = {
	.buttons	= qnap_ts409_buttons,
	.nbuttons	= ARRAY_SIZE(qnap_ts409_buttons),
};

static struct platform_device qnap_ts409_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &qnap_ts409_button_data,
	},
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/
static unsigned int ts409_mpp_modes[] __initdata = {
	MPP0_UNUSED,
	MPP1_UNUSED,
	MPP2_UNUSED,
	MPP3_UNUSED,
	MPP4_GPIO,		/* HDD 1 status */
	MPP5_GPIO,		/* HDD 2 status */
	MPP6_GPIO,		/* HDD 3 status */
	MPP7_GPIO,		/* HDD 4 status */
	MPP8_UNUSED,
	MPP9_UNUSED,
	MPP10_GPIO,		/* RTC int */
	MPP11_UNUSED,
	MPP12_UNUSED,
	MPP13_UNUSED,
	MPP14_GPIO,		/* SW_RST */
	MPP15_GPIO,		/* USB copy button */
	MPP16_UART,		/* UART1 RXD */
	MPP17_UART,		/* UART1 TXD */
	MPP18_UNUSED,
	MPP19_UNUSED,
	0,
};

static void __init qnap_ts409_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	orion5x_mpp_conf(ts409_mpp_modes);

	/*
	 * Configure peripherals.
	 */
	mvebu_mbus_add_window_by_id(ORION_MBUS_DEVBUS_BOOT_TARGET,
				    ORION_MBUS_DEVBUS_BOOT_ATTR,
				    QNAP_TS409_NOR_BOOT_BASE,
				    QNAP_TS409_NOR_BOOT_SIZE);
	platform_device_register(&qnap_ts409_nor_flash);

	orion5x_ehci0_init();
	qnap_tsx09_find_mac_addr(QNAP_TS409_NOR_BOOT_BASE +
				 qnap_ts409_partitions[5].offset,
				 qnap_ts409_partitions[5].size);
	orion5x_eth_init(&qnap_tsx09_eth_data);
	orion5x_i2c_init();
	orion5x_uart0_init();
	orion5x_uart1_init();

	platform_device_register(&qnap_ts409_button_device);

	/* Get RTC IRQ and register the chip */
	if (gpio_request(TS409_RTC_GPIO, "rtc") == 0) {
		if (gpio_direction_input(TS409_RTC_GPIO) == 0)
			qnap_ts409_i2c_rtc.irq = gpio_to_irq(TS409_RTC_GPIO);
		else
			gpio_free(TS409_RTC_GPIO);
	}
	if (qnap_ts409_i2c_rtc.irq == 0)
		pr_warn("qnap_ts409_init: failed to get RTC IRQ\n");
	i2c_register_board_info(0, &qnap_ts409_i2c_rtc, 1);
	platform_device_register(&ts409_leds);

	/* register tsx09 specific power-off method */
	pm_power_off = qnap_tsx09_power_off;
}

MACHINE_START(TS409, "QNAP TS-409")
	/* Maintainer:  Sylver Bruneau <sylver.bruneau@gmail.com> */
	.atag_offset	= 0x100,
	.init_machine	= qnap_ts409_init,
	.map_io		= orion5x_map_io,
	.init_early	= orion5x_init_early,
	.init_irq	= orion5x_init_irq,
	.init_time	= orion5x_timer_init,
	.fixup		= tag_fixup_mem32,
	.restart	= orion5x_restart,
MACHINE_END
