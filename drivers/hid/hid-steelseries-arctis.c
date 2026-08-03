// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Steelseries arctis headsets
 *
 *  Copyright (c) 2023 Bastien Nocera
 *  Copyright (c) 2026 Sriman Achanta
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define SS_CAP_BATTERY			BIT(0)

struct steelseries_device;

struct steelseries_device_info {
	unsigned long capabilities;

	u8 sync_interface;

	int (*request_status)(struct hid_device *hdev);
	void (*parse_status)(struct steelseries_device *sd, u8 *data, int size);
};

struct steelseries_device {
	struct hid_device *hdev;
	const struct steelseries_device_info *info;

	struct delayed_work status_work;

	struct power_supply_desc battery_desc;
	struct power_supply *battery;
	bool headset_connected;
	u8 battery_capacity;
	bool battery_charging;

	spinlock_t lock;
	bool removed;
};

/*
 * Headset report helpers
 */

static int steelseries_send_report(struct hid_device *hdev, const u8 *data,
				    int len, enum hid_report_type type)
{
	u8 *buf;
	int ret;

	buf = kmemdup(data, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, data[0], buf, len, type,
				 HID_REQ_SET_REPORT);
	kfree(buf);

	if (ret < 0)
		return ret;
	if (ret < len)
		return -EIO;

	return 0;
}

static inline int steelseries_send_output_report(struct hid_device *hdev,
						  const u8 *data, int len)
{
	return steelseries_send_report(hdev, data, len, HID_OUTPUT_REPORT);
}

/*
 * Headset status request functions
 */

static int steelseries_arctis_1_request_status(struct hid_device *hdev)
{
	const u8 data[] = { 0x06, 0x12 };

	return steelseries_send_output_report(hdev, data, sizeof(data));
}

static int steelseries_arctis_9_request_status(struct hid_device *hdev)
{
	const u8 data[] = { 0x00, 0x20 };

	return steelseries_send_output_report(hdev, data, sizeof(data));
}

/*
 * Headset battery helpers
 */

static int battery_capacity_to_level(int capacity)
{
	if (capacity >= 50)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	if (capacity >= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
}

static u8 steelseries_map_capacity(u8 capacity, u8 min_in, u8 max_in)
{
	if (capacity >= max_in)
		return 100;
	if (capacity <= min_in)
		return 0;
	return (capacity - min_in) * 100 / (max_in - min_in);
}

/*
 * Headset status parse functions
 */

static void steelseries_arctis_1_parse_status(struct steelseries_device *sd,
					      u8 *data, int size)
{
	/* Only the battery status report echoes the request header. */
	if (size < 8 || data[0] != 0x06 || data[1] != 0x12)
		return;

	sd->headset_connected = (data[2] != 0x01);
	sd->battery_capacity = data[3];
}

static void steelseries_arctis_9_parse_status(struct steelseries_device *sd,
					      u8 *data, int size)
{
	if (size < 5)
		return;

	if (data[0] == 0xaa && data[1] == 0x01) {
		sd->headset_connected = true;
		sd->battery_charging = (data[4] == 0x01);
		sd->battery_capacity = steelseries_map_capacity(data[3], 0x68, 0x9d);
	} else {
		/* Device off: 0x55 (no status) or 0x03 (stale status). */
		sd->headset_connected = false;
		sd->battery_charging = false;
	}
}

/*
 * Device info definitions
 */

static const struct steelseries_device_info arctis_1_info = {
	.sync_interface = 3,
	.capabilities = SS_CAP_BATTERY,
	.request_status = steelseries_arctis_1_request_status,
	.parse_status = steelseries_arctis_1_parse_status,
};

static const struct steelseries_device_info arctis_9_info = {
	.sync_interface = 0,
	.capabilities = SS_CAP_BATTERY,
	.request_status = steelseries_arctis_9_request_status,
	.parse_status = steelseries_arctis_9_parse_status,
};

/*
 * Headset wireless status and battery infrastructure
 */

#define STEELSERIES_HEADSET_STATUS_TIMEOUT_MS	3000

static void
steelseries_headset_set_wireless_status(struct hid_device *hdev,
					bool connected)
{
	struct usb_interface *intf;

	if (!hid_is_usb(hdev))
		return;

	intf = to_usb_interface(hdev->dev.parent);
	usb_set_wireless_status(intf, connected ?
				USB_WIRELESS_STATUS_CONNECTED :
				USB_WIRELESS_STATUS_DISCONNECTED);
}

#define STEELSERIES_PREFIX "SteelSeries "

static int steelseries_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct steelseries_device *sd = power_supply_get_drvdata(psy);
	size_t prefix_len;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = sd->hdev->name;
		while ((prefix_len = str_has_prefix(val->strval, STEELSERIES_PREFIX)))
			val->strval += prefix_len;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "SteelSeries";
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!sd->headset_connected)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		else if (sd->battery_charging)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = sd->battery_capacity;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = battery_capacity_to_level(sd->battery_capacity);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum power_supply_property steelseries_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

/*
 * Delayed work handlers for status polling
 */

static void steelseries_status_timer_work_handler(struct work_struct *work)
{
	struct steelseries_device *sd = container_of(
		work, struct steelseries_device, status_work.work);
	unsigned long flags;

	sd->info->request_status(sd->hdev);

	spin_lock_irqsave(&sd->lock, flags);
	if (!sd->removed)
		schedule_delayed_work(&sd->status_work,
				msecs_to_jiffies(STEELSERIES_HEADSET_STATUS_TIMEOUT_MS));
	spin_unlock_irqrestore(&sd->lock, flags);
}

static int steelseries_battery_register(struct steelseries_device *sd)
{
	static atomic_t battery_no = ATOMIC_INIT(0);
	struct power_supply_config battery_cfg = { .drv_data = sd, };
	unsigned long n;
	int ret;

	sd->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	sd->battery_desc.properties = steelseries_battery_props;
	sd->battery_desc.num_properties = ARRAY_SIZE(steelseries_battery_props);
	sd->battery_desc.get_property = steelseries_battery_get_property;
	sd->battery_desc.use_for_apm = 0;
	n = atomic_inc_return(&battery_no) - 1;
	sd->battery_desc.name = devm_kasprintf(&sd->hdev->dev, GFP_KERNEL,
						"steelseries_headset_battery_%ld", n);
	if (!sd->battery_desc.name)
		return -ENOMEM;

	/* avoid the warning of 0% battery while waiting for the first info */
	sd->battery_capacity = 100;
	sd->battery_charging = false;
	sd->headset_connected = false;
	steelseries_headset_set_wireless_status(sd->hdev, false);

	sd->battery = devm_power_supply_register(&sd->hdev->dev,
			&sd->battery_desc, &battery_cfg);
	if (IS_ERR(sd->battery)) {
		ret = PTR_ERR(sd->battery);
		sd->battery = NULL;
		hid_err(sd->hdev,
				"%s:power_supply_register failed with error %d\n",
				__func__, ret);
		return ret;
	}
	power_supply_powers(sd->battery, &sd->hdev->dev);

	return 0;
}

static int steelseries_arctis_probe(struct hid_device *hdev,
				    const struct hid_device_id *id)
{
	const struct steelseries_device_info *info =
		(const struct steelseries_device_info *)id->driver_data;
	struct steelseries_device *sd;
	struct usb_interface *intf;
	u8 interface_num;
	int ret;

	if (hid_is_usb(hdev)) {
		intf = to_usb_interface(hdev->dev.parent);
		interface_num = intf->cur_altsetting->desc.bInterfaceNumber;
	} else {
		return -ENODEV;
	}

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	/* Let hid-generic handle non-sync interfaces */
	if (interface_num != info->sync_interface)
		return hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	sd = devm_kzalloc(&hdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	sd->hdev = hdev;
	sd->info = info;
	spin_lock_init(&sd->lock);

	hid_set_drvdata(hdev, sd);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto err_stop;

	if (info->capabilities & SS_CAP_BATTERY) {
		ret = steelseries_battery_register(sd);
		if (ret < 0)
			hid_warn(hdev, "Failed to register battery: %d\n", ret);
	}

	INIT_DELAYED_WORK(&sd->status_work, steelseries_status_timer_work_handler);
	schedule_delayed_work(&sd->status_work, msecs_to_jiffies(100));

	return 0;

err_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void steelseries_arctis_remove(struct hid_device *hdev)
{
	struct steelseries_device *sd;
	unsigned long flags;
	struct usb_interface *intf;
	u8 interface_num;

	if (hid_is_usb(hdev)) {
		intf = to_usb_interface(hdev->dev.parent);
		interface_num = intf->cur_altsetting->desc.bInterfaceNumber;
	} else {
		return;
	}

	sd = hid_get_drvdata(hdev);

	if (!sd) {
		hid_hw_stop(hdev);
		return;
	}

	if (interface_num == sd->info->sync_interface) {
		spin_lock_irqsave(&sd->lock, flags);
		sd->removed = true;
		spin_unlock_irqrestore(&sd->lock, flags);

		cancel_delayed_work_sync(&sd->status_work);
	}

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int steelseries_arctis_raw_event(struct hid_device *hdev,
				 struct hid_report *report, u8 *data, int size)
{
	struct steelseries_device *sd = hid_get_drvdata(hdev);
	u8 old_capacity;
	bool old_connected;
	bool old_charging;

	if (!sd)
		return 0;

	old_capacity = sd->battery_capacity;
	old_connected = sd->headset_connected;
	old_charging = sd->battery_charging;

	sd->info->parse_status(sd, data, size);

	if (sd->headset_connected != old_connected) {
		hid_dbg(hdev,
			"Connected status changed from %sconnected to %sconnected\n",
			old_connected ? "" : "not ",
			sd->headset_connected ? "" : "not ");

		if (sd->battery) {
			steelseries_headset_set_wireless_status(sd->hdev,
							       sd->headset_connected);
			power_supply_changed(sd->battery);
		}
	}

	if (sd->battery_capacity != old_capacity) {
		hid_dbg(hdev, "Battery capacity changed from %d%% to %d%%\n",
			old_capacity, sd->battery_capacity);
		if (sd->battery)
			power_supply_changed(sd->battery);
	}

	if (sd->battery_charging != old_charging) {
		hid_dbg(hdev,
			"Battery charging status changed from %scharging to %scharging\n",
			old_charging ? "" : "not ",
			sd->battery_charging ? "" : "not ");
		if (sd->battery)
			power_supply_changed(sd->battery);
	}

	return 0;
}

static const struct hid_device_id steelseries_arctis_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES,
			 USB_DEVICE_ID_STEELSERIES_ARCTIS_1_X),
	  .driver_data = (unsigned long)&arctis_1_info },
	{ HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES,
			 USB_DEVICE_ID_STEELSERIES_ARCTIS_9),
	  .driver_data = (unsigned long)&arctis_9_info },
	{}
};
MODULE_DEVICE_TABLE(hid, steelseries_arctis_devices);

static struct hid_driver steelseries_arctis_driver = {
	.name = "hid-steelseries-arctis",
	.id_table = steelseries_arctis_devices,
	.probe = steelseries_arctis_probe,
	.remove = steelseries_arctis_remove,
	.raw_event = steelseries_arctis_raw_event,
};

module_hid_driver(steelseries_arctis_driver);
MODULE_DESCRIPTION("HID driver for Steelseries arctis headsets");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Mayer <git@mayer-bgk.de>");
MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_AUTHOR("Sriman Achanta <srimanachanta@gmail.com>");
