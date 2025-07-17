// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  USB HID driver for Kysona
 *  Kysona M600 mice.
 *
 *  Copyright (c) 2024 Lode Willems <me@lodewillems.com>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/usb.h>

#include "hid-ids.h"

#define BATTERY_TIMEOUT_MS 5000

#define ONLINE_REPORT_ID 3
#define BATTERY_REPORT_ID 4

struct kysona_drvdata {
	struct hid_device *hdev;
	bool online;

	struct power_supply_desc battery_desc;
	struct power_supply *battery;
	u8 battery_capacity;
	bool battery_charging;
	u16 battery_voltage;
	struct delayed_work battery_work;
};

static enum power_supply_property kysona_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_ONLINE
};

static int kysona_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct kysona_drvdata *drv_data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = drv_data->online;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (drv_data->online)
			val->intval = drv_data->battery_charging ?
					POWER_SUPPLY_STATUS_CHARGING :
					POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = drv_data->battery_capacity;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* hardware reports voltage in mV. sysfs expects uV */
		val->intval = drv_data->battery_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = drv_data->hdev->name;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const char kysona_online_request[] = {
	0x08, ONLINE_REPORT_ID, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a
};

static const char kysona_battery_request[] = {
	0x08, BATTERY_REPORT_ID, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49
};

static int kysona_m600_fetch_online(struct hid_device *hdev)
{
	u8 *write_buf;
	int ret;

	/* Request online information */
	write_buf = kmemdup(kysona_online_request, sizeof(kysona_online_request), GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, kysona_online_request[0],
				 write_buf, sizeof(kysona_online_request),
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < (int)sizeof(kysona_online_request)) {
		hid_err(hdev, "hid_hw_raw_request() failed with %d\n", ret);
		ret = -ENODATA;
	}
	kfree(write_buf);
	return ret;
}

static void kysona_fetch_online(struct hid_device *hdev)
{
	int ret = kysona_m600_fetch_online(hdev);

	if (ret < 0)
		hid_dbg(hdev,
			"Online query failed (err: %d)\n", ret);
}

static int kysona_m600_fetch_battery(struct hid_device *hdev)
{
	u8 *write_buf;
	int ret;

	/* Request battery information */
	write_buf = kmemdup(kysona_battery_request, sizeof(kysona_battery_request), GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, kysona_battery_request[0],
				 write_buf, sizeof(kysona_battery_request),
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < (int)sizeof(kysona_battery_request)) {
		hid_err(hdev, "hid_hw_raw_request() failed with %d\n", ret);
		ret = -ENODATA;
	}
	kfree(write_buf);
	return ret;
}

static void kysona_fetch_battery(struct hid_device *hdev)
{
	int ret = kysona_m600_fetch_battery(hdev);

	if (ret < 0)
		hid_dbg(hdev,
			"Battery query failed (err: %d)\n", ret);
}

static void kysona_battery_timer_tick(struct work_struct *work)
{
	struct kysona_drvdata *drv_data = container_of(work,
		struct kysona_drvdata, battery_work.work);
	struct hid_device *hdev = drv_data->hdev;

	kysona_fetch_online(hdev);
	kysona_fetch_battery(hdev);
	schedule_delayed_work(&drv_data->battery_work,
			      msecs_to_jiffies(BATTERY_TIMEOUT_MS));
}

static int kysona_battery_probe(struct hid_device *hdev)
{
	struct kysona_drvdata *drv_data = hid_get_drvdata(hdev);
	struct power_supply_config pscfg = { .drv_data = drv_data };
	int ret = 0;

	drv_data->online = false;
	drv_data->battery_capacity = 100;
	drv_data->battery_voltage = 4200;

	drv_data->battery_desc.properties = kysona_battery_props;
	drv_data->battery_desc.num_properties = ARRAY_SIZE(kysona_battery_props);
	drv_data->battery_desc.get_property = kysona_battery_get_property;
	drv_data->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	drv_data->battery_desc.use_for_apm = 0;
	drv_data->battery_desc.name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
						     "kysona-%s-battery",
						     strlen(hdev->uniq) ?
						     hdev->uniq : dev_name(&hdev->dev));
	if (!drv_data->battery_desc.name)
		return -ENOMEM;

	drv_data->battery = devm_power_supply_register(&hdev->dev,
						       &drv_data->battery_desc, &pscfg);
	if (IS_ERR(drv_data->battery)) {
		ret = PTR_ERR(drv_data->battery);
		drv_data->battery = NULL;
		hid_err(hdev, "Unable to register battery device\n");
		return ret;
	}

	power_supply_powers(drv_data->battery, &hdev->dev);

	INIT_DELAYED_WORK(&drv_data->battery_work, kysona_battery_timer_tick);
	kysona_fetch_online(hdev);
	kysona_fetch_battery(hdev);
	schedule_delayed_work(&drv_data->battery_work,
			      msecs_to_jiffies(BATTERY_TIMEOUT_MS));

	return ret;
}

static int kysona_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct kysona_drvdata *drv_data;
	struct usb_interface *usbif;

	if (!hid_is_usb(hdev))
		return -EINVAL;

	usbif = to_usb_interface(hdev->dev.parent);

	drv_data = devm_kzalloc(&hdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	hid_set_drvdata(hdev, drv_data);
	drv_data->hdev = hdev;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	if (usbif->cur_altsetting->desc.bInterfaceNumber == 1) {
		if (kysona_battery_probe(hdev) < 0)
			hid_err(hdev, "Kysona hid battery_probe failed: %d\n", ret);
	}

	return 0;
}

static int kysona_raw_event(struct hid_device *hdev,
			    struct hid_report *report, u8 *data, int size)
{
	struct kysona_drvdata *drv_data = hid_get_drvdata(hdev);

	if (size == sizeof(kysona_online_request) &&
	    data[0] == 8 && data[1] == ONLINE_REPORT_ID) {
		drv_data->online = data[6];
	}

	if (size == sizeof(kysona_battery_request) &&
	    data[0] == 8 && data[1] == BATTERY_REPORT_ID) {
		drv_data->battery_capacity = data[6];
		drv_data->battery_charging = data[7];
		drv_data->battery_voltage = (data[8] << 8) | data[9];
	}

	return 0;
}

static void kysona_remove(struct hid_device *hdev)
{
	struct kysona_drvdata *drv_data = hid_get_drvdata(hdev);

	if (drv_data->battery)
		cancel_delayed_work_sync(&drv_data->battery_work);

	hid_hw_stop(hdev);
}

static const struct hid_device_id kysona_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYSONA, USB_DEVICE_ID_KYSONA_M600_DONGLE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYSONA, USB_DEVICE_ID_KYSONA_M600_WIRED) },
	{ }
};
MODULE_DEVICE_TABLE(hid, kysona_devices);

static struct hid_driver kysona_driver = {
	.name			= "kysona",
	.id_table		= kysona_devices,
	.probe			= kysona_probe,
	.raw_event		= kysona_raw_event,
	.remove			= kysona_remove
};
module_hid_driver(kysona_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HID driver for Kysona devices");
MODULE_AUTHOR("Lode Willems <me@lodewillems.com>");
