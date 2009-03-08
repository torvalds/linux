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

void ath5k_led_enable(struct ath5k_softc *sc)
{
	if (test_bit(ATH_STAT_LEDSOFT, sc->status)) {
		ath5k_hw_set_gpio_output(sc->ah, sc->led_pin);
		ath5k_led_off(sc);
	}
}

void ath5k_led_on(struct ath5k_softc *sc)
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

	/*
	 * Auto-enable soft led processing for IBM cards and for
	 * 5211 minipci cards.
	 */
	if (pdev->device == PCI_DEVICE_ID_ATHEROS_AR5212_IBM ||
	    pdev->device == PCI_DEVICE_ID_ATHEROS_AR5211) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = 0;
		sc->led_on = 0;  /* active low */
	}
	/* Enable softled on PIN1 on HP Compaq nc6xx, nc4000 & nx5000 laptops */
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_COMPAQ) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = 1;
		sc->led_on = 1;  /* active high */
	}
	/*
	 * Pin 3 on Foxconn chips used in Acer Aspire One (0x105b:e008) and
	 * in emachines notebooks with AMBIT subsystem.
	 */
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_FOXCONN ||
	    pdev->subsystem_vendor == PCI_VENDOR_ID_AMBIT) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = 3;
		sc->led_on = 0;  /* active low */
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

