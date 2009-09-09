/*
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2009 Bob Copeland <me@bobcopeland.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <linux/pci.h>
#include "ath5k.h"
#include "base.h"

#define ATH_SDEVICE(subv,subd) \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID, \
	.subvendor = (subv), .subdevice = (subd)

#define ATH_LED(pin,polarity) .driver_data = (((pin) << 8) | (polarity))
#define ATH_PIN(data) ((data) >> 8)
#define ATH_POLARITY(data) ((data) & 0xff)

/* Devices we match on for LED config info (typically laptops) */
static const struct pci_device_id ath5k_led_devices[] = {
	/* AR5211 */
	{ PCI_VDEVICE(ATHEROS, PCI_DEVICE_ID_ATHEROS_AR5211), ATH_LED(0, 0) },
	/* HP Compaq nc6xx, nc4000, nx6000 */
	{ ATH_SDEVICE(PCI_VENDOR_ID_COMPAQ, PCI_ANY_ID), ATH_LED(1, 1) },
	/* Acer Aspire One A150 (maximlevitsky@gmail.com) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_FOXCONN, 0xe008), ATH_LED(3, 0) },
	/* Acer Ferrari 5000 (russ.dill@gmail.com) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_AMBIT, 0x0422), ATH_LED(1, 1) },
	/* E-machines E510 (tuliom@gmail.com) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_AMBIT, 0x0428), ATH_LED(3, 0) },
	/* Acer Extensa 5620z (nekoreeve@gmail.com) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_QMI, 0x0105), ATH_LED(3, 0) },
	/* Fukato Datacask Jupiter 1014a (mrb74@gmx.at) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_AZWAVE, 0x1026), ATH_LED(3, 0) },
	/* IBM ThinkPad AR5BXB6 (legovini@spiro.fisica.unipd.it) */
	{ ATH_SDEVICE(PCI_VENDOR_ID_IBM, 0x058a), ATH_LED(1, 0) },
	/* IBM-specific AR5212 (all others) */
	{ PCI_VDEVICE(ATHEROS, PCI_DEVICE_ID_ATHEROS_AR5212_IBM), ATH_LED(0, 0) },
	{ }
};

void ath5k_led_enable(struct ath5k_softc *sc)
{
	if (test_bit(ATH_STAT_LEDSOFT, sc->status)) {
		ath5k_hw_set_gpio_output(sc->ah, sc->led_pin);
		ath5k_led_off(sc);
	}
}

static void ath5k_led_on(struct ath5k_softc *sc)
{
	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		return;
	ath5k_hw_set_gpio(sc->ah, sc->led_pin, sc->led_on);
}

void ath5k_led_off(struct ath5k_softc *sc)
{
	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		return;
	ath5k_hw_set_gpio(sc->ah, sc->led_pin, !sc->led_on);
}

static void
ath5k_led_brightness_set(struct led_classdev *led_dev,
	enum led_brightness brightness)
{
	struct ath5k_led *led = container_of(led_dev, struct ath5k_led,
		led_dev);

	if (brightness == LED_OFF)
		ath5k_led_off(led->sc);
	else
		ath5k_led_on(led->sc);
}

static int
ath5k_register_led(struct ath5k_softc *sc, struct ath5k_led *led,
		   const char *name, char *trigger)
{
	int err;

	led->sc = sc;
	strncpy(led->name, name, sizeof(led->name));
	led->led_dev.name = led->name;
	led->led_dev.default_trigger = trigger;
	led->led_dev.brightness_set = ath5k_led_brightness_set;

	err = led_classdev_register(&sc->pdev->dev, &led->led_dev);
	if (err) {
		ATH5K_WARN(sc, "could not register LED %s\n", name);
		led->sc = NULL;
	}
	return err;
}

static void
ath5k_unregister_led(struct ath5k_led *led)
{
	if (!led->sc)
		return;
	led_classdev_unregister(&led->led_dev);
	ath5k_led_off(led->sc);
	led->sc = NULL;
}

void ath5k_unregister_leds(struct ath5k_softc *sc)
{
	ath5k_unregister_led(&sc->rx_led);
	ath5k_unregister_led(&sc->tx_led);
}

int ath5k_init_leds(struct ath5k_softc *sc)
{
	int ret = 0;
	struct ieee80211_hw *hw = sc->hw;
	struct pci_dev *pdev = sc->pdev;
	char name[ATH5K_LED_MAX_NAME_LEN + 1];
	const struct pci_device_id *match;

	match = pci_match_id(&ath5k_led_devices[0], pdev);
	if (match) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = ATH_PIN(match->driver_data);
		sc->led_on = ATH_POLARITY(match->driver_data);
	}

	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		goto out;

	ath5k_led_enable(sc);

	snprintf(name, sizeof(name), "ath5k-%s::rx", wiphy_name(hw->wiphy));
	ret = ath5k_register_led(sc, &sc->rx_led, name,
		ieee80211_get_rx_led_name(hw));
	if (ret)
		goto out;

	snprintf(name, sizeof(name), "ath5k-%s::tx", wiphy_name(hw->wiphy));
	ret = ath5k_register_led(sc, &sc->tx_led, name,
		ieee80211_get_tx_led_name(hw));
out:
	return ret;
}

