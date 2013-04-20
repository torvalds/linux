/*
 * Qualcomm Atheros AP136 reference board support
 *
 * Copyright (c) 2012 Qualcomm Atheros
 * Copyright (c) 2012-2013 Gabor Juhos <juhosg@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/pci.h>
#include <linux/ath9k_platform.h>

#include "machtypes.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-spi.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "pci.h"

#define AP136_GPIO_LED_STATUS_RED	14
#define AP136_GPIO_LED_STATUS_GREEN	19
#define AP136_GPIO_LED_USB		4
#define AP136_GPIO_LED_WLAN_2G		13
#define AP136_GPIO_LED_WLAN_5G		12
#define AP136_GPIO_LED_WPS_RED		15
#define AP136_GPIO_LED_WPS_GREEN	20

#define AP136_GPIO_BTN_WPS		16
#define AP136_GPIO_BTN_RFKILL		21

#define AP136_KEYS_POLL_INTERVAL	20	/* msecs */
#define AP136_KEYS_DEBOUNCE_INTERVAL	(3 * AP136_KEYS_POLL_INTERVAL)

#define AP136_WMAC_CALDATA_OFFSET 0x1000
#define AP136_PCIE_CALDATA_OFFSET 0x5000

static struct gpio_led ap136_leds_gpio[] __initdata = {
	{
		.name		= "qca:green:status",
		.gpio		= AP136_GPIO_LED_STATUS_GREEN,
		.active_low	= 1,
	},
	{
		.name		= "qca:red:status",
		.gpio		= AP136_GPIO_LED_STATUS_RED,
		.active_low	= 1,
	},
	{
		.name		= "qca:green:wps",
		.gpio		= AP136_GPIO_LED_WPS_GREEN,
		.active_low	= 1,
	},
	{
		.name		= "qca:red:wps",
		.gpio		= AP136_GPIO_LED_WPS_RED,
		.active_low	= 1,
	},
	{
		.name		= "qca:red:wlan-2g",
		.gpio		= AP136_GPIO_LED_WLAN_2G,
		.active_low	= 1,
	},
	{
		.name		= "qca:red:usb",
		.gpio		= AP136_GPIO_LED_USB,
		.active_low	= 1,
	}
};

static struct gpio_keys_button ap136_gpio_keys[] __initdata = {
	{
		.desc		= "WPS button",
		.type		= EV_KEY,
		.code		= KEY_WPS_BUTTON,
		.debounce_interval = AP136_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP136_GPIO_BTN_WPS,
		.active_low	= 1,
	},
	{
		.desc		= "RFKILL button",
		.type		= EV_KEY,
		.code		= KEY_RFKILL,
		.debounce_interval = AP136_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP136_GPIO_BTN_RFKILL,
		.active_low	= 1,
	},
};

static struct spi_board_info ap136_spi_info[] = {
	{
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 25000000,
		.modalias	= "mx25l6405d",
	}
};

static struct ath79_spi_platform_data ap136_spi_data = {
	.bus_num	= 0,
	.num_chipselect	= 1,
};

#ifdef CONFIG_PCI
static struct ath9k_platform_data ap136_ath9k_data;

static int ap136_pci_plat_dev_init(struct pci_dev *dev)
{
	if (dev->bus->number == 1 && (PCI_SLOT(dev->devfn)) == 0)
		dev->dev.platform_data = &ap136_ath9k_data;

	return 0;
}

static void __init ap136_pci_init(u8 *eeprom)
{
	memcpy(ap136_ath9k_data.eeprom_data, eeprom,
	       sizeof(ap136_ath9k_data.eeprom_data));

	ath79_pci_set_plat_dev_init(ap136_pci_plat_dev_init);
	ath79_register_pci();
}
#else
static inline void ap136_pci_init(void) {}
#endif /* CONFIG_PCI */

static void __init ap136_setup(void)
{
	u8 *art = (u8 *) KSEG1ADDR(0x1fff0000);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap136_leds_gpio),
				 ap136_leds_gpio);
	ath79_register_gpio_keys_polled(-1, AP136_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap136_gpio_keys),
					ap136_gpio_keys);
	ath79_register_spi(&ap136_spi_data, ap136_spi_info,
			   ARRAY_SIZE(ap136_spi_info));
	ath79_register_usb();
	ath79_register_wmac(art + AP136_WMAC_CALDATA_OFFSET);
	ap136_pci_init(art + AP136_PCIE_CALDATA_OFFSET);
}

MIPS_MACHINE(ATH79_MACH_AP136_010, "AP136-010",
	     "Atheros AP136-010 reference board",
	     ap136_setup);
