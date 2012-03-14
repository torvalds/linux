/*
 *  Ubiquiti Networks XM (rev 1.0) board support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *
 *  Derived from: mach-pb44.c
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>

#ifdef CONFIG_PCI
#include <linux/ath9k_platform.h>
#endif /* CONFIG_PCI */

#include <asm/mach-ath79/irq.h>

#include "machtypes.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-spi.h"
#include "pci.h"

#define UBNT_XM_GPIO_LED_L1		0
#define UBNT_XM_GPIO_LED_L2		1
#define UBNT_XM_GPIO_LED_L3		11
#define UBNT_XM_GPIO_LED_L4		7

#define UBNT_XM_GPIO_BTN_RESET		12

#define UBNT_XM_KEYS_POLL_INTERVAL	20
#define UBNT_XM_KEYS_DEBOUNCE_INTERVAL	(3 * UBNT_XM_KEYS_POLL_INTERVAL)

#define UBNT_XM_EEPROM_ADDR		(u8 *) KSEG1ADDR(0x1fff1000)

static struct gpio_led ubnt_xm_leds_gpio[] __initdata = {
	{
		.name		= "ubnt-xm:red:link1",
		.gpio		= UBNT_XM_GPIO_LED_L1,
		.active_low	= 0,
	}, {
		.name		= "ubnt-xm:orange:link2",
		.gpio		= UBNT_XM_GPIO_LED_L2,
		.active_low	= 0,
	}, {
		.name		= "ubnt-xm:green:link3",
		.gpio		= UBNT_XM_GPIO_LED_L3,
		.active_low	= 0,
	}, {
		.name		= "ubnt-xm:green:link4",
		.gpio		= UBNT_XM_GPIO_LED_L4,
		.active_low	= 0,
	},
};

static struct gpio_keys_button ubnt_xm_gpio_keys[] __initdata = {
	{
		.desc			= "reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.debounce_interval	= UBNT_XM_KEYS_DEBOUNCE_INTERVAL,
		.gpio			= UBNT_XM_GPIO_BTN_RESET,
		.active_low		= 1,
	}
};

static struct spi_board_info ubnt_xm_spi_info[] = {
	{
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 25000000,
		.modalias	= "mx25l6405d",
	}
};

static struct ath79_spi_platform_data ubnt_xm_spi_data = {
	.bus_num		= 0,
	.num_chipselect		= 1,
};

#ifdef CONFIG_PCI
static struct ath9k_platform_data ubnt_xm_eeprom_data;

static struct ar724x_pci_data ubnt_xm_pci_data[] = {
	{
		.irq	= ATH79_PCI_IRQ(0),
		.pdata	= &ubnt_xm_eeprom_data,
	},
};
#endif /* CONFIG_PCI */

static void __init ubnt_xm_init(void)
{
	ath79_register_leds_gpio(-1, ARRAY_SIZE(ubnt_xm_leds_gpio),
				 ubnt_xm_leds_gpio);

	ath79_register_gpio_keys_polled(-1, UBNT_XM_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ubnt_xm_gpio_keys),
					ubnt_xm_gpio_keys);

	ath79_register_spi(&ubnt_xm_spi_data, ubnt_xm_spi_info,
			   ARRAY_SIZE(ubnt_xm_spi_info));

#ifdef CONFIG_PCI
	memcpy(ubnt_xm_eeprom_data.eeprom_data, UBNT_XM_EEPROM_ADDR,
	       sizeof(ubnt_xm_eeprom_data.eeprom_data));

	ar724x_pci_add_data(ubnt_xm_pci_data, ARRAY_SIZE(ubnt_xm_pci_data));
#endif /* CONFIG_PCI */

	ath79_register_pci();
}

MIPS_MACHINE(ATH79_MACH_UBNT_XM,
	     "UBNT-XM",
	     "Ubiquiti Networks XM (rev 1.0) board",
	     ubnt_xm_init);
