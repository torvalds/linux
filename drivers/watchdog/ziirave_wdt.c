/*
 * Copyright (C) 2015 Zodiac Inflight Innovations
 *
 * Author: Martyn Welch <martyn.welch@collabora.co.uk>
 *
 * Based on twl4030_wdt.c by Timo Kokkonen <timo.t.kokkonen at nokia.com>:
 *
 * Copyright (C) Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/watchdog.h>

#define ZIIRAVE_TIMEOUT_MIN	3
#define ZIIRAVE_TIMEOUT_MAX	255

#define ZIIRAVE_PING_VALUE	0x0

#define ZIIRAVE_STATE_INITIAL	0x0
#define ZIIRAVE_STATE_OFF	0x1
#define ZIIRAVE_STATE_ON	0x2

static char *ziirave_reasons[] = {"power cycle", "triggered", NULL, NULL,
				  "host request", NULL, "illegal configuration",
				  "illegal instruction", "illegal trap",
				  "unknown"};

#define ZIIRAVE_WDT_FIRM_VER_MAJOR	0x1
#define ZIIRAVE_WDT_BOOT_VER_MAJOR	0x3
#define ZIIRAVE_WDT_RESET_REASON	0x5
#define ZIIRAVE_WDT_STATE		0x6
#define ZIIRAVE_WDT_TIMEOUT		0x7
#define ZIIRAVE_WDT_TIME_LEFT		0x8
#define ZIIRAVE_WDT_PING		0x9
#define ZIIRAVE_WDT_RESET_DURATION	0xa

struct ziirave_wdt_rev {
	unsigned char major;
	unsigned char minor;
};

struct ziirave_wdt_data {
	struct watchdog_device wdd;
	struct ziirave_wdt_rev bootloader_rev;
	struct ziirave_wdt_rev firmware_rev;
	int reset_reason;
};

static int wdt_timeout;
module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout, "Watchdog timeout in seconds");

static int reset_duration;
module_param(reset_duration, int, 0);
MODULE_PARM_DESC(reset_duration,
		 "Watchdog reset pulse duration in milliseconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int ziirave_wdt_revision(struct i2c_client *client,
				struct ziirave_wdt_rev *rev, u8 command)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, command);
	if (ret < 0)
		return ret;

	rev->major = ret;

	ret = i2c_smbus_read_byte_data(client, command + 1);
	if (ret < 0)
		return ret;

	rev->minor = ret;

	return 0;
}

static int ziirave_wdt_set_state(struct watchdog_device *wdd, int state)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_STATE, state);
}

static int ziirave_wdt_start(struct watchdog_device *wdd)
{
	return ziirave_wdt_set_state(wdd, ZIIRAVE_STATE_ON);
}

static int ziirave_wdt_stop(struct watchdog_device *wdd)
{
	return ziirave_wdt_set_state(wdd, ZIIRAVE_STATE_OFF);
}

static int ziirave_wdt_ping(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_PING,
					 ZIIRAVE_PING_VALUE);
}

static int ziirave_wdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int timeout)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_TIMEOUT, timeout);
	if (!ret)
		wdd->timeout = timeout;

	return ret;
}

static unsigned int ziirave_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct i2c_client *client = to_i2c_client(wdd->parent);
	int ret;

	ret = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_TIME_LEFT);
	if (ret < 0)
		ret = 0;

	return ret;
}

static const struct watchdog_info ziirave_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "Zodiac RAVE Watchdog",
};

static const struct watchdog_ops ziirave_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= ziirave_wdt_start,
	.stop		= ziirave_wdt_stop,
	.ping		= ziirave_wdt_ping,
	.set_timeout	= ziirave_wdt_set_timeout,
	.get_timeleft	= ziirave_wdt_get_timeleft,
};

static ssize_t ziirave_wdt_sysfs_show_firm(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);

	return sprintf(buf, "02.%02u.%02u", w_priv->firmware_rev.major,
		       w_priv->firmware_rev.minor);
}

static DEVICE_ATTR(firmware_version, S_IRUGO, ziirave_wdt_sysfs_show_firm,
		   NULL);

static ssize_t ziirave_wdt_sysfs_show_boot(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);

	return sprintf(buf, "01.%02u.%02u", w_priv->bootloader_rev.major,
		       w_priv->bootloader_rev.minor);
}

static DEVICE_ATTR(bootloader_version, S_IRUGO, ziirave_wdt_sysfs_show_boot,
		   NULL);

static ssize_t ziirave_wdt_sysfs_show_reason(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);

	return sprintf(buf, "%s", ziirave_reasons[w_priv->reset_reason]);
}

static DEVICE_ATTR(reset_reason, S_IRUGO, ziirave_wdt_sysfs_show_reason,
		   NULL);

static struct attribute *ziirave_wdt_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_bootloader_version.attr,
	&dev_attr_reset_reason.attr,
	NULL
};
ATTRIBUTE_GROUPS(ziirave_wdt);

static int ziirave_wdt_init_duration(struct i2c_client *client)
{
	int ret;

	if (!reset_duration) {
		/* See if the reset pulse duration is provided in an of_node */
		if (!client->dev.of_node)
			ret = -ENODEV;
		else
			ret = of_property_read_u32(client->dev.of_node,
						   "reset-duration-ms",
						   &reset_duration);
		if (ret) {
			dev_info(&client->dev,
				 "Unable to set reset pulse duration, using default\n");
			return 0;
		}
	}

	if (reset_duration < 1 || reset_duration > 255)
		return -EINVAL;

	dev_info(&client->dev, "Setting reset duration to %dms",
		 reset_duration);

	return i2c_smbus_write_byte_data(client, ZIIRAVE_WDT_RESET_DURATION,
					 reset_duration);
}

static int ziirave_wdt_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret;
	struct ziirave_wdt_data *w_priv;
	int val;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	w_priv = devm_kzalloc(&client->dev, sizeof(*w_priv), GFP_KERNEL);
	if (!w_priv)
		return -ENOMEM;

	w_priv->wdd.info = &ziirave_wdt_info;
	w_priv->wdd.ops = &ziirave_wdt_ops;
	w_priv->wdd.min_timeout = ZIIRAVE_TIMEOUT_MIN;
	w_priv->wdd.max_timeout = ZIIRAVE_TIMEOUT_MAX;
	w_priv->wdd.parent = &client->dev;
	w_priv->wdd.groups = ziirave_wdt_groups;

	ret = watchdog_init_timeout(&w_priv->wdd, wdt_timeout, &client->dev);
	if (ret) {
		dev_info(&client->dev,
			 "Unable to select timeout value, using default\n");
	}

	/*
	 * The default value set in the watchdog should be perfectly valid, so
	 * pass that in if we haven't provided one via the module parameter or
	 * of property.
	 */
	if (w_priv->wdd.timeout == 0) {
		val = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_TIMEOUT);
		if (val < 0)
			return val;

		if (val < ZIIRAVE_TIMEOUT_MIN)
			return -ENODEV;

		w_priv->wdd.timeout = val;
	} else {
		ret = ziirave_wdt_set_timeout(&w_priv->wdd,
					      w_priv->wdd.timeout);
		if (ret)
			return ret;

		dev_info(&client->dev, "Timeout set to %ds.",
			 w_priv->wdd.timeout);
	}

	watchdog_set_nowayout(&w_priv->wdd, nowayout);

	i2c_set_clientdata(client, w_priv);

	/* If in unconfigured state, set to stopped */
	val = i2c_smbus_read_byte_data(client, ZIIRAVE_WDT_STATE);
	if (val < 0)
		return val;

	if (val == ZIIRAVE_STATE_INITIAL)
		ziirave_wdt_stop(&w_priv->wdd);

	ret = ziirave_wdt_init_duration(client);
	if (ret)
		return ret;

	ret = ziirave_wdt_revision(client, &w_priv->firmware_rev,
				   ZIIRAVE_WDT_FIRM_VER_MAJOR);
	if (ret)
		return ret;

	ret = ziirave_wdt_revision(client, &w_priv->bootloader_rev,
				   ZIIRAVE_WDT_BOOT_VER_MAJOR);
	if (ret)
		return ret;

	w_priv->reset_reason = i2c_smbus_read_byte_data(client,
						ZIIRAVE_WDT_RESET_REASON);
	if (w_priv->reset_reason < 0)
		return w_priv->reset_reason;

	if (w_priv->reset_reason >= ARRAY_SIZE(ziirave_reasons) ||
	    !ziirave_reasons[w_priv->reset_reason])
		return -ENODEV;

	ret = watchdog_register_device(&w_priv->wdd);

	return ret;
}

static int ziirave_wdt_remove(struct i2c_client *client)
{
	struct ziirave_wdt_data *w_priv = i2c_get_clientdata(client);

	watchdog_unregister_device(&w_priv->wdd);

	return 0;
}

static struct i2c_device_id ziirave_wdt_id[] = {
	{ "ziirave-wdt", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ziirave_wdt_id);

static const struct of_device_id zrv_wdt_of_match[] = {
	{ .compatible = "zii,rave-wdt", },
	{ },
};
MODULE_DEVICE_TABLE(of, zrv_wdt_of_match);

static struct i2c_driver ziirave_wdt_driver = {
	.driver = {
		.name = "ziirave_wdt",
		.of_match_table = zrv_wdt_of_match,
	},
	.probe = ziirave_wdt_probe,
	.remove = ziirave_wdt_remove,
	.id_table = ziirave_wdt_id,
};

module_i2c_driver(ziirave_wdt_driver);

MODULE_AUTHOR("Martyn Welch <martyn.welch@collabora.co.uk");
MODULE_DESCRIPTION("Zodiac Aerospace RAVE Switch Watchdog Processor Driver");
MODULE_LICENSE("GPL");
