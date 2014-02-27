/*
 * Atheros CARL9170 driver
 *
 * LED handling
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparer <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "carl9170.h"
#include "cmd.h"

int carl9170_led_set_state(struct ar9170 *ar, const u32 led_state)
{
	return carl9170_write_reg(ar, AR9170_GPIO_REG_PORT_DATA, led_state);
}

int carl9170_led_init(struct ar9170 *ar)
{
	int err;

	/* disable LEDs */
	/* GPIO [0/1 mode: output, 2/3: input] */
	err = carl9170_write_reg(ar, AR9170_GPIO_REG_PORT_TYPE, 3);
	if (err)
		goto out;

	/* GPIO 0/1 value: off */
	err = carl9170_led_set_state(ar, 0);

out:
	return err;
}

#ifdef CONFIG_CARL9170_LEDS
static void carl9170_led_update(struct work_struct *work)
{
	struct ar9170 *ar = container_of(work, struct ar9170, led_work.work);
	int i, tmp = 300, blink_delay = 1000;
	u32 led_val = 0;
	bool rerun = false;

	if (!IS_ACCEPTING_CMD(ar))
		return;

	mutex_lock(&ar->mutex);
	for (i = 0; i < AR9170_NUM_LEDS; i++) {
		if (ar->leds[i].registered) {
			if (ar->leds[i].last_state ||
			    ar->leds[i].toggled) {

				if (ar->leds[i].toggled)
					tmp = 70 + 200 / (ar->leds[i].toggled);

				if (tmp < blink_delay)
					blink_delay = tmp;

				led_val |= 1 << i;
				ar->leds[i].toggled = 0;
				rerun = true;
			}
		}
	}

	carl9170_led_set_state(ar, led_val);
	mutex_unlock(&ar->mutex);

	if (!rerun)
		return;

	ieee80211_queue_delayed_work(ar->hw,
				     &ar->led_work,
				     msecs_to_jiffies(blink_delay));
}

static void carl9170_led_set_brightness(struct led_classdev *led,
					enum led_brightness brightness)
{
	struct carl9170_led *arl = container_of(led, struct carl9170_led, l);
	struct ar9170 *ar = arl->ar;

	if (!arl->registered)
		return;

	if (arl->last_state != !!brightness) {
		arl->toggled++;
		arl->last_state = !!brightness;
	}

	if (likely(IS_ACCEPTING_CMD(ar) && arl->toggled))
		ieee80211_queue_delayed_work(ar->hw, &ar->led_work, HZ / 10);
}

static int carl9170_led_register_led(struct ar9170 *ar, int i, char *name,
				     char *trigger)
{
	int err;

	snprintf(ar->leds[i].name, sizeof(ar->leds[i].name),
		 "carl9170-%s::%s", wiphy_name(ar->hw->wiphy), name);

	ar->leds[i].ar = ar;
	ar->leds[i].l.name = ar->leds[i].name;
	ar->leds[i].l.brightness_set = carl9170_led_set_brightness;
	ar->leds[i].l.brightness = 0;
	ar->leds[i].l.default_trigger = trigger;

	err = led_classdev_register(wiphy_dev(ar->hw->wiphy),
				    &ar->leds[i].l);
	if (err) {
		wiphy_err(ar->hw->wiphy, "failed to register %s LED (%d).\n",
			ar->leds[i].name, err);
	} else {
		ar->leds[i].registered = true;
	}

	return err;
}

void carl9170_led_unregister(struct ar9170 *ar)
{
	int i;

	for (i = 0; i < AR9170_NUM_LEDS; i++)
		if (ar->leds[i].registered) {
			led_classdev_unregister(&ar->leds[i].l);
			ar->leds[i].registered = false;
			ar->leds[i].toggled = 0;
		}

	cancel_delayed_work_sync(&ar->led_work);
}

int carl9170_led_register(struct ar9170 *ar)
{
	int err;

	INIT_DELAYED_WORK(&ar->led_work, carl9170_led_update);

	err = carl9170_led_register_led(ar, 0, "tx",
					ieee80211_get_tx_led_name(ar->hw));
	if (err)
		goto fail;

	if (ar->features & CARL9170_ONE_LED)
		return 0;

	err = carl9170_led_register_led(ar, 1, "assoc",
					ieee80211_get_assoc_led_name(ar->hw));
	if (err)
		goto fail;

	return 0;

fail:
	carl9170_led_unregister(ar);
	return err;
}

#endif /* CONFIG_CARL9170_LEDS */
