#include <net/mac80211.h>
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/gpio.h>

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
	if (wl->radio_led.gpio == -1)
		return;

	if (wl->radio_led.active_low)
		state = !state;

	if (state)
		gpio_set_value(wl->radio_led.gpio, 1);
	else
		gpio_set_value(wl->radio_led.gpio, 0);
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
	if (wl->radio_led.gpio != -1)
		gpio_free(wl->radio_led.gpio);
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
	unsigned gpio = -1;
	bool active_low = false;

	/* none by default */
	radio_led->gpio = -1;
	radio_led->active_low = false;

	if (!bcma_gpio || !gpio_is_valid(bcma_gpio->base))
		return -ENODEV;

	/* find radio enabled LED */
	for (i = 0; i < BRCMS_LED_NO; i++) {
		u8 led = *leds[i];
		if ((led & BRCMS_LED_BEH_MASK) == BRCMS_LED_RADIO) {
			gpio = bcma_gpio->base + i;
			if (led & BRCMS_LED_AL_MASK)
				active_low = true;
			break;
		}
	}

	if (gpio == -1 || !gpio_is_valid(gpio))
		return -ENODEV;

	/* request and configure LED gpio */
	err = gpio_request_one(gpio,
				active_low ? GPIOF_OUT_INIT_HIGH
					: GPIOF_OUT_INIT_LOW,
				"radio on");
	if (err) {
		wiphy_err(wl->wiphy, "requesting led gpio %d failed (err: %d)\n",
			  gpio, err);
		return err;
	}
	err = gpio_direction_output(gpio, 1);
	if (err) {
		wiphy_err(wl->wiphy, "cannot set led gpio %d to output (err: %d)\n",
			  gpio, err);
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

	wiphy_info(wl->wiphy, "registered radio enabled led device: %s gpio: %d\n",
		   wl->radio_led.name,
		   gpio);
	radio_led->gpio = gpio;
	radio_led->active_low = active_low;

	return 0;
}
