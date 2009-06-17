/*
 * Linux LED driver for RTL8187
 *
 * Copyright 2009 Larry Finger <Larry.Finger@lwfinger.net>
 *
 * Based on the LED handling in the r8187 driver, which is:
 * Copyright (c) Realtek Semiconductor Corp. All rights reserved.
 *
 * Thanks to Realtek for their support!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_RTL8187_LEDS

#include <net/mac80211.h>
#include <linux/usb.h>
#include <linux/eeprom_93cx6.h>

#include "rtl8187.h"
#include "rtl8187_leds.h"

static void led_turn_on(struct work_struct *work)
{
	/* As this routine does read/write operations on the hardware, it must
	 * be run from a work queue.
	 */
	u8 reg;
	struct rtl8187_priv *priv = container_of(work, struct rtl8187_priv,
				    led_on.work);
	struct rtl8187_led *led = &priv->led_tx;

	/* Don't change the LED, when the device is down. */
	if (priv->mode == NL80211_IFTYPE_UNSPECIFIED)
		return ;

	/* Skip if the LED is not registered. */
	if (!led->dev)
		return;
	mutex_lock(&priv->conf_mutex);
	switch (led->ledpin) {
	case LED_PIN_GPIO0:
		rtl818x_iowrite8(priv, &priv->map->GPIO, 0x01);
		rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0x00);
		break;
	case LED_PIN_LED0:
		reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) & ~(1 << 4);
		rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
		break;
	case LED_PIN_LED1:
		reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) & ~(1 << 5);
		rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
		break;
	case LED_PIN_HW:
	default:
		break;
	}
	mutex_unlock(&priv->conf_mutex);
}

static void led_turn_off(struct work_struct *work)
{
	/* As this routine does read/write operations on the hardware, it must
	 * be run from a work queue.
	 */
	u8 reg;
	struct rtl8187_priv *priv = container_of(work, struct rtl8187_priv,
				    led_off.work);
	struct rtl8187_led *led = &priv->led_tx;

	/* Don't change the LED, when the device is down. */
	if (priv->mode == NL80211_IFTYPE_UNSPECIFIED)
		return ;

	/* Skip if the LED is not registered. */
	if (!led->dev)
		return;
	mutex_lock(&priv->conf_mutex);
	switch (led->ledpin) {
	case LED_PIN_GPIO0:
		rtl818x_iowrite8(priv, &priv->map->GPIO, 0x01);
		rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0x01);
		break;
	case LED_PIN_LED0:
		reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) | (1 << 4);
		rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
		break;
	case LED_PIN_LED1:
		reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) | (1 << 5);
		rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
		break;
	case LED_PIN_HW:
	default:
		break;
	}
	mutex_unlock(&priv->conf_mutex);
}

/* Callback from the LED subsystem. */
static void rtl8187_led_brightness_set(struct led_classdev *led_dev,
				   enum led_brightness brightness)
{
	struct rtl8187_led *led = container_of(led_dev, struct rtl8187_led,
					       led_dev);
	struct ieee80211_hw *hw = led->dev;
	struct rtl8187_priv *priv = hw->priv;

	if (brightness == LED_OFF) {
		queue_delayed_work(hw->workqueue, &priv->led_off, 0);
		/* The LED is off for 1/20 sec so that it just blinks. */
		queue_delayed_work(hw->workqueue, &priv->led_on, HZ / 20);
	} else
		queue_delayed_work(hw->workqueue, &priv->led_on, 0);
}

static int rtl8187_register_led(struct ieee80211_hw *dev,
				struct rtl8187_led *led, const char *name,
				const char *default_trigger, u8 ledpin)
{
	int err;
	struct rtl8187_priv *priv = dev->priv;

	if (led->dev)
		return -EEXIST;
	if (!default_trigger)
		return -EINVAL;
	led->dev = dev;
	led->ledpin = ledpin;
	strncpy(led->name, name, sizeof(led->name));

	led->led_dev.name = led->name;
	led->led_dev.default_trigger = default_trigger;
	led->led_dev.brightness_set = rtl8187_led_brightness_set;

	err = led_classdev_register(&priv->udev->dev, &led->led_dev);
	if (err) {
		printk(KERN_INFO "LEDs: Failed to register %s\n", name);
		led->dev = NULL;
		return err;
	}
	return 0;
}

static void rtl8187_unregister_led(struct rtl8187_led *led)
{
	led_classdev_unregister(&led->led_dev);
	led->dev = NULL;
}

void rtl8187_leds_init(struct ieee80211_hw *dev, u16 custid)
{
	struct rtl8187_priv *priv = dev->priv;
	char name[RTL8187_LED_MAX_NAME_LEN + 1];
	u8 ledpin;
	int err;

	/* According to the vendor driver, the LED operation depends on the
	 * customer ID encoded in the EEPROM
	 */
	printk(KERN_INFO "rtl8187: Customer ID is 0x%02X\n", custid);
	switch (custid) {
	case EEPROM_CID_RSVD0:
	case EEPROM_CID_RSVD1:
	case EEPROM_CID_SERCOMM_PS:
	case EEPROM_CID_QMI:
	case EEPROM_CID_DELL:
	case EEPROM_CID_TOSHIBA:
		ledpin = LED_PIN_GPIO0;
		break;
	case EEPROM_CID_ALPHA0:
		ledpin = LED_PIN_LED0;
		break;
	case EEPROM_CID_HW:
		ledpin = LED_PIN_HW;
		break;
	default:
		ledpin = LED_PIN_GPIO0;
	}

	INIT_DELAYED_WORK(&priv->led_on, led_turn_on);
	INIT_DELAYED_WORK(&priv->led_off, led_turn_off);

	snprintf(name, sizeof(name),
		 "rtl8187-%s::tx", wiphy_name(dev->wiphy));
	err = rtl8187_register_led(dev, &priv->led_tx, name,
			 ieee80211_get_tx_led_name(dev), ledpin);
	if (err)
		goto error;
	snprintf(name, sizeof(name),
		 "rtl8187-%s::rx", wiphy_name(dev->wiphy));
	err = rtl8187_register_led(dev, &priv->led_rx, name,
			 ieee80211_get_rx_led_name(dev), ledpin);
	if (!err) {
		queue_delayed_work(dev->workqueue, &priv->led_on, 0);
		return;
	}
	/* registration of RX LED failed - unregister TX */
	rtl8187_unregister_led(&priv->led_tx);
error:
	/* If registration of either failed, cancel delayed work */
	cancel_delayed_work_sync(&priv->led_off);
	cancel_delayed_work_sync(&priv->led_on);
}

void rtl8187_leds_exit(struct ieee80211_hw *dev)
{
	struct rtl8187_priv *priv = dev->priv;

	rtl8187_unregister_led(&priv->led_tx);
	/* turn the LED off before exiting */
	queue_delayed_work(dev->workqueue, &priv->led_off, 0);
	cancel_delayed_work_sync(&priv->led_off);
	rtl8187_unregister_led(&priv->led_rx);
}
#endif /* def CONFIG_RTL8187_LED */

