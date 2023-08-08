// SPDX-License-Identifier: GPL-2.0
// Copyright 2017 Ben Whitten <ben.whitten@gmail.com>
// Copyright 2007 Oliver Jowett <oliver@opencloud.com>
//
// LED Kernel Netdev Trigger
//
// Toggles the LED to reflect the link and traffic state of a named net device
//
// Derived from ledtrig-timer.c which is:
//  Copyright 2005-2006 Openedhand Ltd.
//  Author: Richard Purdie <rpurdie@openedhand.com>

#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/rtnetlink.h>
#include <linux/timer.h>
#include "../leds.h"

#define NETDEV_LED_DEFAULT_INTERVAL	50

/*
 * Configurable sysfs attributes:
 *
 * device_name - network device name to monitor
 * interval - duration of LED blink, in milliseconds
 * link -  LED's normal state reflects whether the link is up
 *         (has carrier) or not
 * tx -  LED blinks on transmitted data
 * rx -  LED blinks on receive data
 *
 */

struct led_netdev_data {
	struct mutex lock;

	struct delayed_work work;
	struct notifier_block notifier;

	struct led_classdev *led_cdev;
	struct net_device *net_dev;

	char device_name[IFNAMSIZ];
	atomic_t interval;
	unsigned int last_activity;

	unsigned long mode;
	int link_speed;
	u8 duplex;

	bool carrier_link_up;
	bool hw_control;
};

static void set_baseline_state(struct led_netdev_data *trigger_data)
{
	int current_brightness;
	struct led_classdev *led_cdev = trigger_data->led_cdev;

	/* Already validated, hw control is possible with the requested mode */
	if (trigger_data->hw_control) {
		led_cdev->hw_control_set(led_cdev, trigger_data->mode);

		return;
	}

	current_brightness = led_cdev->brightness;
	if (current_brightness)
		led_cdev->blink_brightness = current_brightness;
	if (!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;

	if (!trigger_data->carrier_link_up) {
		led_set_brightness(led_cdev, LED_OFF);
	} else {
		bool blink_on = false;

		if (test_bit(TRIGGER_NETDEV_LINK, &trigger_data->mode))
			blink_on = true;

		if (test_bit(TRIGGER_NETDEV_LINK_10, &trigger_data->mode) &&
		    trigger_data->link_speed == SPEED_10)
			blink_on = true;

		if (test_bit(TRIGGER_NETDEV_LINK_100, &trigger_data->mode) &&
		    trigger_data->link_speed == SPEED_100)
			blink_on = true;

		if (test_bit(TRIGGER_NETDEV_LINK_1000, &trigger_data->mode) &&
		    trigger_data->link_speed == SPEED_1000)
			blink_on = true;

		if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &trigger_data->mode) &&
		    trigger_data->duplex == DUPLEX_HALF)
			blink_on = true;

		if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &trigger_data->mode) &&
		    trigger_data->duplex == DUPLEX_FULL)
			blink_on = true;

		if (blink_on)
			led_set_brightness(led_cdev,
					   led_cdev->blink_brightness);
		else
			led_set_brightness(led_cdev, LED_OFF);

		/* If we are looking for RX/TX start periodically
		 * checking stats
		 */
		if (test_bit(TRIGGER_NETDEV_TX, &trigger_data->mode) ||
		    test_bit(TRIGGER_NETDEV_RX, &trigger_data->mode))
			schedule_delayed_work(&trigger_data->work, 0);
	}
}

static bool supports_hw_control(struct led_classdev *led_cdev)
{
	if (!led_cdev->hw_control_get || !led_cdev->hw_control_set ||
	    !led_cdev->hw_control_is_supported)
		return false;

	return !strcmp(led_cdev->hw_control_trigger, led_cdev->trigger->name);
}

/*
 * Validate the configured netdev is the same as the one associated with
 * the LED driver in hw control.
 */
static bool validate_net_dev(struct led_classdev *led_cdev,
			     struct net_device *net_dev)
{
	struct device *dev = led_cdev->hw_control_get_device(led_cdev);
	struct net_device *ndev;

	if (!dev)
		return false;

	ndev = to_net_dev(dev);

	return ndev == net_dev;
}

static bool can_hw_control(struct led_netdev_data *trigger_data)
{
	unsigned long default_interval = msecs_to_jiffies(NETDEV_LED_DEFAULT_INTERVAL);
	unsigned int interval = atomic_read(&trigger_data->interval);
	struct led_classdev *led_cdev = trigger_data->led_cdev;
	int ret;

	if (!supports_hw_control(led_cdev))
		return false;

	/*
	 * Interval must be set to the default
	 * value. Any different value is rejected if in hw
	 * control.
	 */
	if (interval != default_interval)
		return false;

	/*
	 * net_dev must be set with hw control, otherwise no
	 * blinking can be happening and there is nothing to
	 * offloaded. Additionally, for hw control to be
	 * valid, the configured netdev must be the same as
	 * netdev associated to the LED.
	 */
	if (!validate_net_dev(led_cdev, trigger_data->net_dev))
		return false;

	/* Check if the requested mode is supported */
	ret = led_cdev->hw_control_is_supported(led_cdev, trigger_data->mode);
	/* Fall back to software blinking if not supported */
	if (ret == -EOPNOTSUPP)
		return false;
	if (ret) {
		dev_warn(led_cdev->dev,
			 "Current mode check failed with error %d\n", ret);
		return false;
	}

	return true;
}

static void get_device_state(struct led_netdev_data *trigger_data)
{
	struct ethtool_link_ksettings cmd;

	trigger_data->carrier_link_up = netif_carrier_ok(trigger_data->net_dev);
	if (!trigger_data->carrier_link_up)
		return;

	if (!__ethtool_get_link_ksettings(trigger_data->net_dev, &cmd)) {
		trigger_data->link_speed = cmd.base.speed;
		trigger_data->duplex = cmd.base.duplex;
	}
}

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);
	ssize_t len;

	mutex_lock(&trigger_data->lock);
	len = sprintf(buf, "%s\n", trigger_data->device_name);
	mutex_unlock(&trigger_data->lock);

	return len;
}

static int set_device_name(struct led_netdev_data *trigger_data,
			   const char *name, size_t size)
{
	cancel_delayed_work_sync(&trigger_data->work);

	mutex_lock(&trigger_data->lock);

	if (trigger_data->net_dev) {
		dev_put(trigger_data->net_dev);
		trigger_data->net_dev = NULL;
	}

	memcpy(trigger_data->device_name, name, size);
	trigger_data->device_name[size] = 0;
	if (size > 0 && trigger_data->device_name[size - 1] == '\n')
		trigger_data->device_name[size - 1] = 0;

	if (trigger_data->device_name[0] != 0)
		trigger_data->net_dev =
		    dev_get_by_name(&init_net, trigger_data->device_name);

	trigger_data->carrier_link_up = false;
	trigger_data->link_speed = SPEED_UNKNOWN;
	trigger_data->duplex = DUPLEX_UNKNOWN;
	if (trigger_data->net_dev != NULL) {
		rtnl_lock();
		get_device_state(trigger_data);
		rtnl_unlock();
	}

	trigger_data->last_activity = 0;

	set_baseline_state(trigger_data);
	mutex_unlock(&trigger_data->lock);

	return 0;
}

static ssize_t device_name_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);
	int ret;

	if (size >= IFNAMSIZ)
		return -EINVAL;

	ret = set_device_name(trigger_data, buf, size);

	if (ret < 0)
		return ret;
	return size;
}

static DEVICE_ATTR_RW(device_name);

static ssize_t netdev_led_attr_show(struct device *dev, char *buf,
				    enum led_trigger_netdev_modes attr)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);
	int bit;

	switch (attr) {
	case TRIGGER_NETDEV_LINK:
	case TRIGGER_NETDEV_LINK_10:
	case TRIGGER_NETDEV_LINK_100:
	case TRIGGER_NETDEV_LINK_1000:
	case TRIGGER_NETDEV_HALF_DUPLEX:
	case TRIGGER_NETDEV_FULL_DUPLEX:
	case TRIGGER_NETDEV_TX:
	case TRIGGER_NETDEV_RX:
		bit = attr;
		break;
	default:
		return -EINVAL;
	}

	return sprintf(buf, "%u\n", test_bit(bit, &trigger_data->mode));
}

static ssize_t netdev_led_attr_store(struct device *dev, const char *buf,
				     size_t size, enum led_trigger_netdev_modes attr)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);
	unsigned long state, mode = trigger_data->mode;
	int ret;
	int bit;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	switch (attr) {
	case TRIGGER_NETDEV_LINK:
	case TRIGGER_NETDEV_LINK_10:
	case TRIGGER_NETDEV_LINK_100:
	case TRIGGER_NETDEV_LINK_1000:
	case TRIGGER_NETDEV_HALF_DUPLEX:
	case TRIGGER_NETDEV_FULL_DUPLEX:
	case TRIGGER_NETDEV_TX:
	case TRIGGER_NETDEV_RX:
		bit = attr;
		break;
	default:
		return -EINVAL;
	}

	if (state)
		set_bit(bit, &mode);
	else
		clear_bit(bit, &mode);

	if (test_bit(TRIGGER_NETDEV_LINK, &mode) &&
	    (test_bit(TRIGGER_NETDEV_LINK_10, &mode) ||
	     test_bit(TRIGGER_NETDEV_LINK_100, &mode) ||
	     test_bit(TRIGGER_NETDEV_LINK_1000, &mode)))
		return -EINVAL;

	cancel_delayed_work_sync(&trigger_data->work);

	trigger_data->mode = mode;
	trigger_data->hw_control = can_hw_control(trigger_data);

	set_baseline_state(trigger_data);

	return size;
}

#define DEFINE_NETDEV_TRIGGER(trigger_name, trigger) \
	static ssize_t trigger_name##_show(struct device *dev, \
		struct device_attribute *attr, char *buf) \
	{ \
		return netdev_led_attr_show(dev, buf, trigger); \
	} \
	static ssize_t trigger_name##_store(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t size) \
	{ \
		return netdev_led_attr_store(dev, buf, size, trigger); \
	} \
	static DEVICE_ATTR_RW(trigger_name)

DEFINE_NETDEV_TRIGGER(link, TRIGGER_NETDEV_LINK);
DEFINE_NETDEV_TRIGGER(link_10, TRIGGER_NETDEV_LINK_10);
DEFINE_NETDEV_TRIGGER(link_100, TRIGGER_NETDEV_LINK_100);
DEFINE_NETDEV_TRIGGER(link_1000, TRIGGER_NETDEV_LINK_1000);
DEFINE_NETDEV_TRIGGER(half_duplex, TRIGGER_NETDEV_HALF_DUPLEX);
DEFINE_NETDEV_TRIGGER(full_duplex, TRIGGER_NETDEV_FULL_DUPLEX);
DEFINE_NETDEV_TRIGGER(tx, TRIGGER_NETDEV_TX);
DEFINE_NETDEV_TRIGGER(rx, TRIGGER_NETDEV_RX);

static ssize_t interval_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n",
		       jiffies_to_msecs(atomic_read(&trigger_data->interval)));
}

static ssize_t interval_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t size)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);
	unsigned long value;
	int ret;

	if (trigger_data->hw_control)
		return -EINVAL;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	/* impose some basic bounds on the timer interval */
	if (value >= 5 && value <= 10000) {
		cancel_delayed_work_sync(&trigger_data->work);

		atomic_set(&trigger_data->interval, msecs_to_jiffies(value));
		set_baseline_state(trigger_data);	/* resets timer */
	}

	return size;
}

static DEVICE_ATTR_RW(interval);

static ssize_t hw_control_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_netdev_data *trigger_data = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%d\n", trigger_data->hw_control);
}

static DEVICE_ATTR_RO(hw_control);

static struct attribute *netdev_trig_attrs[] = {
	&dev_attr_device_name.attr,
	&dev_attr_link.attr,
	&dev_attr_link_10.attr,
	&dev_attr_link_100.attr,
	&dev_attr_link_1000.attr,
	&dev_attr_full_duplex.attr,
	&dev_attr_half_duplex.attr,
	&dev_attr_rx.attr,
	&dev_attr_tx.attr,
	&dev_attr_interval.attr,
	&dev_attr_hw_control.attr,
	NULL
};
ATTRIBUTE_GROUPS(netdev_trig);

static int netdev_trig_notify(struct notifier_block *nb,
			      unsigned long evt, void *dv)
{
	struct net_device *dev =
		netdev_notifier_info_to_dev((struct netdev_notifier_info *)dv);
	struct led_netdev_data *trigger_data =
		container_of(nb, struct led_netdev_data, notifier);

	if (evt != NETDEV_UP && evt != NETDEV_DOWN && evt != NETDEV_CHANGE
	    && evt != NETDEV_REGISTER && evt != NETDEV_UNREGISTER
	    && evt != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	if (!(dev == trigger_data->net_dev ||
	      (evt == NETDEV_CHANGENAME && !strcmp(dev->name, trigger_data->device_name)) ||
	      (evt == NETDEV_REGISTER && !strcmp(dev->name, trigger_data->device_name))))
		return NOTIFY_DONE;

	cancel_delayed_work_sync(&trigger_data->work);

	mutex_lock(&trigger_data->lock);

	trigger_data->carrier_link_up = false;
	trigger_data->link_speed = SPEED_UNKNOWN;
	trigger_data->duplex = DUPLEX_UNKNOWN;
	switch (evt) {
	case NETDEV_CHANGENAME:
		get_device_state(trigger_data);
		fallthrough;
	case NETDEV_REGISTER:
		dev_put(trigger_data->net_dev);
		dev_hold(dev);
		trigger_data->net_dev = dev;
		break;
	case NETDEV_UNREGISTER:
		dev_put(trigger_data->net_dev);
		trigger_data->net_dev = NULL;
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		get_device_state(trigger_data);
		break;
	}

	set_baseline_state(trigger_data);

	mutex_unlock(&trigger_data->lock);

	return NOTIFY_DONE;
}

/* here's the real work! */
static void netdev_trig_work(struct work_struct *work)
{
	struct led_netdev_data *trigger_data =
		container_of(work, struct led_netdev_data, work.work);
	struct rtnl_link_stats64 *dev_stats;
	unsigned int new_activity;
	struct rtnl_link_stats64 temp;
	unsigned long interval;
	int invert;

	/* If we dont have a device, insure we are off */
	if (!trigger_data->net_dev) {
		led_set_brightness(trigger_data->led_cdev, LED_OFF);
		return;
	}

	/* If we are not looking for RX/TX then return  */
	if (!test_bit(TRIGGER_NETDEV_TX, &trigger_data->mode) &&
	    !test_bit(TRIGGER_NETDEV_RX, &trigger_data->mode))
		return;

	dev_stats = dev_get_stats(trigger_data->net_dev, &temp);
	new_activity =
	    (test_bit(TRIGGER_NETDEV_TX, &trigger_data->mode) ?
		dev_stats->tx_packets : 0) +
	    (test_bit(TRIGGER_NETDEV_RX, &trigger_data->mode) ?
		dev_stats->rx_packets : 0);

	if (trigger_data->last_activity != new_activity) {
		led_stop_software_blink(trigger_data->led_cdev);

		invert = test_bit(TRIGGER_NETDEV_LINK, &trigger_data->mode) ||
			 test_bit(TRIGGER_NETDEV_LINK_10, &trigger_data->mode) ||
			 test_bit(TRIGGER_NETDEV_LINK_100, &trigger_data->mode) ||
			 test_bit(TRIGGER_NETDEV_LINK_1000, &trigger_data->mode) ||
			 test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &trigger_data->mode) ||
			 test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &trigger_data->mode);
		interval = jiffies_to_msecs(
				atomic_read(&trigger_data->interval));
		/* base state is ON (link present) */
		led_blink_set_oneshot(trigger_data->led_cdev,
				      &interval,
				      &interval,
				      invert);
		trigger_data->last_activity = new_activity;
	}

	schedule_delayed_work(&trigger_data->work,
			(atomic_read(&trigger_data->interval)*2));
}

static int netdev_trig_activate(struct led_classdev *led_cdev)
{
	struct led_netdev_data *trigger_data;
	unsigned long mode = 0;
	struct device *dev;
	int rc;

	trigger_data = kzalloc(sizeof(struct led_netdev_data), GFP_KERNEL);
	if (!trigger_data)
		return -ENOMEM;

	mutex_init(&trigger_data->lock);

	trigger_data->notifier.notifier_call = netdev_trig_notify;
	trigger_data->notifier.priority = 10;

	INIT_DELAYED_WORK(&trigger_data->work, netdev_trig_work);

	trigger_data->led_cdev = led_cdev;
	trigger_data->net_dev = NULL;
	trigger_data->device_name[0] = 0;

	trigger_data->mode = 0;
	atomic_set(&trigger_data->interval, msecs_to_jiffies(NETDEV_LED_DEFAULT_INTERVAL));
	trigger_data->last_activity = 0;

	/* Check if hw control is active by default on the LED.
	 * Init already enabled mode in hw control.
	 */
	if (supports_hw_control(led_cdev)) {
		dev = led_cdev->hw_control_get_device(led_cdev);
		if (dev) {
			const char *name = dev_name(dev);

			set_device_name(trigger_data, name, strlen(name));
			trigger_data->hw_control = true;

			rc = led_cdev->hw_control_get(led_cdev, &mode);
			if (!rc)
				trigger_data->mode = mode;
		}
	}

	led_set_trigger_data(led_cdev, trigger_data);

	rc = register_netdevice_notifier(&trigger_data->notifier);
	if (rc)
		kfree(trigger_data);

	return rc;
}

static void netdev_trig_deactivate(struct led_classdev *led_cdev)
{
	struct led_netdev_data *trigger_data = led_get_trigger_data(led_cdev);

	unregister_netdevice_notifier(&trigger_data->notifier);

	cancel_delayed_work_sync(&trigger_data->work);

	dev_put(trigger_data->net_dev);

	kfree(trigger_data);
}

static struct led_trigger netdev_led_trigger = {
	.name = "netdev",
	.activate = netdev_trig_activate,
	.deactivate = netdev_trig_deactivate,
	.groups = netdev_trig_groups,
};

static int __init netdev_trig_init(void)
{
	return led_trigger_register(&netdev_led_trigger);
}

static void __exit netdev_trig_exit(void)
{
	led_trigger_unregister(&netdev_led_trigger);
}

module_init(netdev_trig_init);
module_exit(netdev_trig_exit);

MODULE_AUTHOR("Ben Whitten <ben.whitten@gmail.com>");
MODULE_AUTHOR("Oliver Jowett <oliver@opencloud.com>");
MODULE_DESCRIPTION("Netdev LED trigger");
MODULE_LICENSE("GPL v2");
