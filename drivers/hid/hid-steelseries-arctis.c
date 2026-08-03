// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Steelseries arctis headsets
 *
 *  Copyright (c) 2023 Bastien Nocera
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define STEELSERIES_ARCTIS_1_X		BIT(0)
#define STEELSERIES_ARCTIS_9		BIT(1)

struct steelseries_device {
	struct hid_device *hdev;
	unsigned long quirks;

	struct delayed_work battery_work;
	spinlock_t lock;
	bool removed;

	struct power_supply_desc battery_desc;
	struct power_supply *battery;
	uint8_t battery_capacity;
	bool headset_connected;
	bool battery_charging;
	bool battery_registered;
};

#define STEELSERIES_HEADSET_BATTERY_TIMEOUT_MS	3000

#define ARCTIS_1_BATTERY_RESPONSE_LEN		8
#define ARCTIS_9_BATTERY_RESPONSE_LEN		64
static const char arctis_1_battery_request[] = { 0x06, 0x12 };
static const char arctis_9_battery_request[] = { 0x00, 0x20 };

static int steelseries_headset_request_battery(struct hid_device *hdev,
	const char *request, size_t len)
{
	u8 *write_buf;
	int ret;

	/* Request battery information */
	write_buf = kmemdup(request, len, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	hid_dbg(hdev, "Sending battery request report");
	ret = hid_hw_raw_request(hdev, request[0], write_buf, len,
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < (int)len) {
		hid_err(hdev, "hid_hw_raw_request() failed with %d\n", ret);
		ret = -ENODATA;
	}

	kfree(write_buf);
	return ret;
}

static void steelseries_headset_fetch_battery(struct hid_device *hdev)
{
	int ret = 0;

	if (hdev->product == USB_DEVICE_ID_STEELSERIES_ARCTIS_1_X)
		ret = steelseries_headset_request_battery(hdev,
			arctis_1_battery_request, sizeof(arctis_1_battery_request));
	else if (hdev->product == USB_DEVICE_ID_STEELSERIES_ARCTIS_9)
		ret = steelseries_headset_request_battery(hdev,
			arctis_9_battery_request, sizeof(arctis_9_battery_request));

	if (ret < 0)
		hid_dbg(hdev,
			"Battery query failed (err: %d)\n", ret);
}

static int battery_capacity_to_level(int capacity)
{
	if (capacity >= 50)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	if (capacity >= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
}

static void steelseries_headset_battery_timer_tick(struct work_struct *work)
{
	struct steelseries_device *sd = container_of(work,
		struct steelseries_device, battery_work.work);
	struct hid_device *hdev = sd->hdev;

	steelseries_headset_fetch_battery(hdev);
}

#define STEELSERIES_PREFIX "SteelSeries "
#define STEELSERIES_PREFIX_LEN strlen(STEELSERIES_PREFIX)

static int steelseries_headset_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct steelseries_device *sd = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = sd->hdev->name;
		while (!strncmp(val->strval, STEELSERIES_PREFIX, STEELSERIES_PREFIX_LEN))
			val->strval += STEELSERIES_PREFIX_LEN;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "SteelSeries";
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (sd->headset_connected) {
			val->intval = sd->battery_charging ?
				POWER_SUPPLY_STATUS_CHARGING :
				POWER_SUPPLY_STATUS_DISCHARGING;
		} else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
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

static enum power_supply_property steelseries_headset_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static int steelseries_headset_battery_register(struct steelseries_device *sd)
{
	static atomic_t battery_no = ATOMIC_INIT(0);
	struct power_supply_config battery_cfg = { .drv_data = sd, };
	unsigned long n;
	int ret;

	sd->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	sd->battery_desc.properties = steelseries_headset_battery_props;
	sd->battery_desc.num_properties = ARRAY_SIZE(steelseries_headset_battery_props);
	sd->battery_desc.get_property = steelseries_headset_battery_get_property;
	sd->battery_desc.use_for_apm = 0;
	n = atomic_inc_return(&battery_no) - 1;
	sd->battery_desc.name = devm_kasprintf(&sd->hdev->dev, GFP_KERNEL,
							"steelseries_headset_battery_%ld", n);
	if (!sd->battery_desc.name)
		return -ENOMEM;

	/* avoid the warning of 0% battery while waiting for the first info */
	steelseries_headset_set_wireless_status(sd->hdev, false);
	sd->battery_capacity = 100;
	sd->battery_charging = false;

	sd->battery = devm_power_supply_register(&sd->hdev->dev,
			&sd->battery_desc, &battery_cfg);
	if (IS_ERR(sd->battery)) {
		ret = PTR_ERR(sd->battery);
		hid_err(sd->hdev,
				"%s:power_supply_register failed with error %d\n",
				__func__, ret);
		return ret;
	}
	power_supply_powers(sd->battery, &sd->hdev->dev);

	INIT_DELAYED_WORK(&sd->battery_work, steelseries_headset_battery_timer_tick);
	/* Pairs with smp_load_acquire() in raw_event and remove paths */
	smp_store_release(&sd->battery_registered, true);
	steelseries_headset_fetch_battery(sd->hdev);

	if (sd->quirks & STEELSERIES_ARCTIS_9) {
		/* The first fetch_battery request can remain unanswered in some cases */
		schedule_delayed_work(&sd->battery_work,
				msecs_to_jiffies(STEELSERIES_HEADSET_BATTERY_TIMEOUT_MS));
	}

	return 0;
}

static bool steelseries_is_vendor_usage_page(struct hid_device *hdev, uint8_t usage_page)
{
	if (hdev->rsize < 3)
		return false;

	return hdev->rdesc[0] == 0x06 &&
		hdev->rdesc[1] == usage_page &&
		hdev->rdesc[2] == 0xff;
}

static int steelseries_arctis_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct steelseries_device *sd;
	int ret;

	sd = devm_kzalloc(&hdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;
	hid_set_drvdata(hdev, sd);
	sd->hdev = hdev;
	sd->quirks = id->driver_data;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	if (sd->quirks & STEELSERIES_ARCTIS_9 &&
			!steelseries_is_vendor_usage_page(hdev, 0xc0))
		return -ENODEV;

	spin_lock_init(&sd->lock);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto err_stop;

	if (steelseries_headset_battery_register(sd) < 0)
		hid_err(sd->hdev,
			"Failed to register battery for headset\n");

	return 0;

err_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void steelseries_arctis_remove(struct hid_device *hdev)
{
	struct steelseries_device *sd;
	unsigned long flags;

	sd = hid_get_drvdata(hdev);
	if (!sd)
		return;

	spin_lock_irqsave(&sd->lock, flags);
	sd->removed = true;
	spin_unlock_irqrestore(&sd->lock, flags);

	/* Pairs with smp_store_release() in steelseries_headset_battery_register() */
	if (smp_load_acquire(&sd->battery_registered))
		cancel_delayed_work_sync(&sd->battery_work);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static uint8_t steelseries_headset_map_capacity(uint8_t capacity, uint8_t min_in, uint8_t max_in)
{
	if (capacity >= max_in)
		return 100;
	if (capacity <= min_in)
		return 0;
	return (capacity - min_in) * 100 / (max_in - min_in);
}

static int steelseries_arctis_raw_event(struct hid_device *hdev,
					struct hid_report *report, u8 *read_buf,
					int size)
{
	struct steelseries_device *sd = hid_get_drvdata(hdev);
	int capacity;
	bool connected;
	bool charging;
	unsigned long flags;

	/* Pairs with smp_store_release() in steelseries_headset_battery_register() */
	if (!sd || !smp_load_acquire(&sd->battery_registered))
		return 0;

	capacity = sd->battery_capacity;
	connected = sd->headset_connected;
	charging = sd->battery_charging;

	if (hdev->product == USB_DEVICE_ID_STEELSERIES_ARCTIS_1_X) {
		hid_dbg(sd->hdev,
			"Parsing raw event for Arctis 1 headset (%*ph)\n", size, read_buf);
		if (size < ARCTIS_1_BATTERY_RESPONSE_LEN ||
			memcmp(read_buf, arctis_1_battery_request, sizeof(arctis_1_battery_request))) {
			if (!delayed_work_pending(&sd->battery_work))
				goto request_battery;
			return 0;
		}
		if (read_buf[2] == 0x01) {
			connected = false;
			capacity = 100;
		} else {
			connected = true;
			capacity = read_buf[3];
		}
	}

	if (hdev->product == USB_DEVICE_ID_STEELSERIES_ARCTIS_9) {
		hid_dbg(sd->hdev,
			"Parsing raw event for Arctis 9 headset (%*ph)\n", size, read_buf);
		if (size < ARCTIS_9_BATTERY_RESPONSE_LEN) {
			if (!delayed_work_pending(&sd->battery_work))
				goto request_battery;
			return 0;
		}

		if (read_buf[0] == 0xaa && read_buf[1] == 0x01) {
			connected = true;
			charging = read_buf[4] == 0x01;

			/*
			 * Found no official documentation about min and max.
			 * Values defined by testing.
			 */
			capacity = steelseries_headset_map_capacity(read_buf[3], 0x68, 0x9d);
		} else {
			/*
			 * Device is off and sends the last known status read_buf[1] == 0x03 or
			 * there is no known status of the device read_buf[0] == 0x55
			 */
			connected = false;
			charging = false;
		}
	}

	if (connected != sd->headset_connected) {
		hid_dbg(sd->hdev,
			"Connected status changed from %sconnected to %sconnected\n",
			sd->headset_connected ? "" : "not ",
			connected ? "" : "not ");
		sd->headset_connected = connected;
		steelseries_headset_set_wireless_status(hdev, connected);
	}

	if (capacity != sd->battery_capacity) {
		hid_dbg(sd->hdev,
			"Battery capacity changed from %d%% to %d%%\n",
			sd->battery_capacity, capacity);
		sd->battery_capacity = capacity;
		power_supply_changed(sd->battery);
	}

	if (charging != sd->battery_charging) {
		hid_dbg(sd->hdev,
			"Battery charging status changed from %scharging to %scharging\n",
			sd->battery_charging ? "" : "not ",
			charging ? "" : "not ");
		sd->battery_charging = charging;
		power_supply_changed(sd->battery);
	}

request_battery:
	spin_lock_irqsave(&sd->lock, flags);
	if (!sd->removed)
		schedule_delayed_work(&sd->battery_work,
				msecs_to_jiffies(STEELSERIES_HEADSET_BATTERY_TIMEOUT_MS));
	spin_unlock_irqrestore(&sd->lock, flags);

	return 0;
}

static const struct hid_device_id steelseries_arctis_devices[] = {
	{ /* SteelSeries Arctis 1 Wireless for XBox */
		HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_ARCTIS_1_X),
		.driver_data = STEELSERIES_ARCTIS_1_X },

	{ /* SteelSeries Arctis 9 Wireless for XBox */
		HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_ARCTIS_9),
		.driver_data = STEELSERIES_ARCTIS_9 },

	{ }
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
