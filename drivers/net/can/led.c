/*
 * Copyright 2012, Fabio Baltieri <fabio.baltieri@gmail.com>
 * Copyright 2012, Kurt Van Dijck <kurt.van.dijck@eia.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

#include <linux/can/led.h>

static unsigned long led_delay = 50;
module_param(led_delay, ulong, 0644);
MODULE_PARM_DESC(led_delay,
		"blink delay time for activity leds (msecs, default: 50).");

/* Trigger a LED event in response to a CAN device event */
void can_led_event(struct net_device *netdev, enum can_led_event event)
{
	struct can_priv *priv = netdev_priv(netdev);

	switch (event) {
	case CAN_LED_EVENT_OPEN:
		led_trigger_event(priv->tx_led_trig, LED_FULL);
		led_trigger_event(priv->rx_led_trig, LED_FULL);
		break;
	case CAN_LED_EVENT_STOP:
		led_trigger_event(priv->tx_led_trig, LED_OFF);
		led_trigger_event(priv->rx_led_trig, LED_OFF);
		break;
	case CAN_LED_EVENT_TX:
		if (led_delay)
			led_trigger_blink_oneshot(priv->tx_led_trig,
						  &led_delay, &led_delay, 1);
		break;
	case CAN_LED_EVENT_RX:
		if (led_delay)
			led_trigger_blink_oneshot(priv->rx_led_trig,
						  &led_delay, &led_delay, 1);
		break;
	}
}
EXPORT_SYMBOL_GPL(can_led_event);

static void can_led_release(struct device *gendev, void *res)
{
	struct can_priv *priv = netdev_priv(to_net_dev(gendev));

	led_trigger_unregister_simple(priv->tx_led_trig);
	led_trigger_unregister_simple(priv->rx_led_trig);
}

/* Register CAN LED triggers for a CAN device
 *
 * This is normally called from a driver's probe function
 */
void devm_can_led_init(struct net_device *netdev)
{
	struct can_priv *priv = netdev_priv(netdev);
	void *res;

	res = devres_alloc(can_led_release, 0, GFP_KERNEL);
	if (!res) {
		netdev_err(netdev, "cannot register LED triggers\n");
		return;
	}

	snprintf(priv->tx_led_trig_name, sizeof(priv->tx_led_trig_name),
		 "%s-tx", netdev->name);
	snprintf(priv->rx_led_trig_name, sizeof(priv->rx_led_trig_name),
		 "%s-rx", netdev->name);

	led_trigger_register_simple(priv->tx_led_trig_name,
				    &priv->tx_led_trig);
	led_trigger_register_simple(priv->rx_led_trig_name,
				    &priv->rx_led_trig);

	devres_add(&netdev->dev, res);
}
EXPORT_SYMBOL_GPL(devm_can_led_init);

/* NETDEV rename notifier to rename the associated led triggers too */
static int can_led_notifier(struct notifier_block *nb, unsigned long msg,
			void *data)
{
	struct net_device *netdev = data;
	struct can_priv *priv = safe_candev_priv(netdev);
	char name[CAN_LED_NAME_SZ];

	if (!priv)
		return NOTIFY_DONE;

	if (msg == NETDEV_CHANGENAME) {
		snprintf(name, sizeof(name), "%s-tx", netdev->name);
		led_trigger_rename_static(name, priv->tx_led_trig);

		snprintf(name, sizeof(name), "%s-rx", netdev->name);
		led_trigger_rename_static(name, priv->rx_led_trig);
	}

	return NOTIFY_DONE;
}

/* notifier block for netdevice event */
static struct notifier_block can_netdev_notifier __read_mostly = {
	.notifier_call = can_led_notifier,
};

int __init can_led_notifier_init(void)
{
	return register_netdevice_notifier(&can_netdev_notifier);
}

void __exit can_led_notifier_exit(void)
{
	unregister_netdevice_notifier(&can_netdev_notifier);
}
