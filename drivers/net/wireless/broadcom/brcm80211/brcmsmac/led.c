// SPDX-License-Identifier: GPL-2.0
#include <net/mac80211.h>
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>

#include "mac80211_if.h"
#include "pub.h"
#include "main.h"
#include "led.h"

	/* number of leds */
#define  BRCMS_LED_NO		4
	/* behavior mask */
#define  BRCMS_LED_BEH_MASK	0x7f
	/* activelow (polarity) bit */
#define  BRCMS_LED_AL_MASK	0x80
	/* radio enabled */
#define  BRCMS_LED_RADIO	3

static void brcms_radio_led_ctrl(struct brcms_info *wl, bool state)
{
	if (!wl->radio_led.gpiod)
		return;

	if (state)
		gpiod_set_value(wl->radio_led.gpiod, 1);
	else
		gpiod_set_value(wl->radio_led.gpiod, 0);
}


/* Callback from the LED subsystem. */
static void brcms_led_brightness_set(struct led_classdev *led_dev,
				   enum led_brightness brightness)
{
	struct brcms_info *wl = container_of(led_dev,
		struct brcms_info, led_dev);
	brcms_radio_led_ctrl(wl, brightness);
}

void brcms_led_unregister(struct brcms_info *wl)
{
	if (wl->led_dev.dev)
		led_classdev_unregister(&wl->led_dev);
	if (wl->radio_led.gpiod)
		gpiochip_free_own_desc(wl->radio_led.gpiod);
}

int brcms_led_register(struct brcms_info *wl)
{
	int i, err;
	struct brcms_led *radio_led = &wl->radio_led;
	/* get CC core */
	struct bcma_drv_cc *cc_drv  = &wl->wlc->hw->d11core->bus->drv_cc;
	struct gpio_chip *bcma_gpio = &cc_drv->gpio;
	struct ssb_sprom *sprom = &wl->wlc->hw->d11core->bus->sprom;
	u8 *leds[] = { &sprom->gpio0,
		&sprom->gpio1,
		&sprom->gpio2,
		&sprom->gpio3 };
	int hwnum = -1;
	enum gpio_lookup_flags lflags = GPIO_ACTIVE_HIGH;

	/* find radio enabled LED */
	for (i = 0; i < BRCMS_LED_NO; i++) {
		u8 led = *leds[i];
		if ((led & BRCMS_LED_BEH_MASK) == BRCMS_LED_RADIO) {
			hwnum = i;
			if (led & BRCMS_LED_AL_MASK)
				lflags = GPIO_ACTIVE_LOW;
			break;
		}
	}

	/* No LED, bail out */
	if (hwnum == -1)
		return -ENODEV;

	/* Try to obtain this LED GPIO line */
	radio_led->gpiod = gpiochip_request_own_desc(bcma_gpio, hwnum,
						     "radio on", lflags,
						     GPIOD_OUT_LOW);

	if (IS_ERR(radio_led->gpiod)) {
		err = PTR_ERR(radio_led->gpiod);
		wiphy_err(wl->wiphy, "requesting led GPIO failed (err: %d)\n",
			  err);
		return err;
	}

	snprintf(wl->radio_led.name, sizeof(wl->radio_led.name),
		 "brcmsmac-%s:radio", wiphy_name(wl->wiphy));

	wl->led_dev.name = wl->radio_led.name;
	wl->led_dev.default_trigger =
		ieee80211_get_radio_led_name(wl->pub->ieee_hw);
	wl->led_dev.brightness_set = brcms_led_brightness_set;
	err = led_classdev_register(wiphy_dev(wl->wiphy), &wl->led_dev);

	if (err) {
		wiphy_err(wl->wiphy, "cannot register led device: %s (err: %d)\n",
			  wl->radio_led.name, err);
		return err;
	}

	wiphy_info(wl->wiphy, "registered radio enabled led device: %s\n",
		   wl->radio_led.name);

	return 0;
}
