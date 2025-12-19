// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Corsair Void headsets
 *
 *  Copyright (C) 2023-2024 Stuart Hayhurst
 */

/* -------------------------------------------------------------------------- */
/* Receiver report information: (ID 100)                                      */
/* -------------------------------------------------------------------------- */
/*
 * When queried, the receiver reponds with 5 bytes to describe the battery
 *   The power button, mute button and moving the mic also trigger this report
 * This includes power button + mic + connection + battery status and capacity
 * The information below may not be perfect, it's been gathered through guesses
 *
 * 0: REPORT ID
 *  100 for the battery packet
 *
 * 1: POWER BUTTON + (?)
 *  Largest bit is 1 when power button pressed
 *
 * 2: BATTERY CAPACITY + MIC STATUS
 *  Battery capacity:
 *    Seems to report ~54 higher than reality when charging
 *    Capped at 100, charging or not
 *  Microphone status:
 *    Largest bit is set to 1 when the mic is physically up
 *    No bits change when the mic is muted, only when physically moved
 *    This report is sent every time the mic is moved, no polling required
 *
 * 3: CONNECTION STATUS
 *  16: Wired headset
 *  38: Initialising
 *  49: Lost connection
 *  51: Disconnected, searching
 *  52: Disconnected, not searching
 *  177: Normal
 *
 * 4: BATTERY STATUS
 *  0: Disconnected
 *  1: Normal
 *  2: Low
 *  3: Critical - sent during shutdown
 *  4: Fully charged
 *  5: Charging
 */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* Receiver report information: (ID 102)                                      */
/* -------------------------------------------------------------------------- */
/*
 * When queried, the recevier responds with 4 bytes to describe the firmware
 * The first 2 bytes are for the receiver, the second 2 are the headset
 * The headset firmware version will be 0 if no headset is connected
 *
 * 0: Recevier firmware major version
 *  Major version of the receiver's firmware
 *
 * 1: Recevier firmware minor version
 *  Minor version of the receiver's firmware
 *
 * 2: Headset firmware major version
 *  Major version of the headset's firmware
 *
 * 3: Headset firmware minor version
 *  Minor version of the headset's firmware
 */
/* -------------------------------------------------------------------------- */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <asm/byteorder.h>

#include "hid-ids.h"

#define CORSAIR_VOID_DEVICE(id, type)		{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, (id)), \
						.driver_data = (type) }
#define CORSAIR_VOID_WIRELESS_DEVICE(id)	CORSAIR_VOID_DEVICE((id), CORSAIR_VOID_WIRELESS)
#define CORSAIR_VOID_WIRED_DEVICE(id)		CORSAIR_VOID_DEVICE((id), CORSAIR_VOID_WIRED)

#define CORSAIR_VOID_STATUS_REQUEST_ID		0xC9
#define CORSAIR_VOID_NOTIF_REQUEST_ID		0xCA
#define CORSAIR_VOID_SIDETONE_REQUEST_ID	0xFF
#define CORSAIR_VOID_STATUS_REPORT_ID		0x64
#define CORSAIR_VOID_FIRMWARE_REPORT_ID		0x66

#define CORSAIR_VOID_USB_SIDETONE_REQUEST	0x1
#define CORSAIR_VOID_USB_SIDETONE_REQUEST_TYPE	0x21
#define CORSAIR_VOID_USB_SIDETONE_VALUE		0x200
#define CORSAIR_VOID_USB_SIDETONE_INDEX		0xB00

#define CORSAIR_VOID_MIC_MASK			GENMASK(7, 7)
#define CORSAIR_VOID_CAPACITY_MASK		GENMASK(6, 0)

#define CORSAIR_VOID_WIRELESS_CONNECTED		177

#define CORSAIR_VOID_SIDETONE_MAX_WIRELESS	55
#define CORSAIR_VOID_SIDETONE_MAX_WIRED		4096

enum {
	CORSAIR_VOID_WIRELESS,
	CORSAIR_VOID_WIRED,
};

enum {
	CORSAIR_VOID_BATTERY_NORMAL	= 1,
	CORSAIR_VOID_BATTERY_LOW	= 2,
	CORSAIR_VOID_BATTERY_CRITICAL	= 3,
	CORSAIR_VOID_BATTERY_CHARGED	= 4,
	CORSAIR_VOID_BATTERY_CHARGING	= 5,
};

enum {
	CORSAIR_VOID_ADD_BATTERY	= 0,
	CORSAIR_VOID_REMOVE_BATTERY	= 1,
	CORSAIR_VOID_UPDATE_BATTERY	= 2,
};

static enum power_supply_property corsair_void_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

struct corsair_void_battery_data {
	int status;
	bool present;
	int capacity;
	int capacity_level;
};

struct corsair_void_drvdata {
	struct hid_device *hid_dev;
	struct device *dev;

	char *name;
	bool is_wired;
	unsigned int sidetone_max;

	struct corsair_void_battery_data battery_data;
	bool mic_up;
	bool connected;
	int fw_receiver_major;
	int fw_receiver_minor;
	int fw_headset_major;
	int fw_headset_minor;

	struct power_supply *battery;
	struct power_supply_desc battery_desc;

	struct delayed_work delayed_status_work;
	struct delayed_work delayed_firmware_work;

	unsigned long battery_work_flags;
	struct work_struct battery_work;
};

/*
 * Functions to process receiver data
*/

static void corsair_void_set_wireless_status(struct corsair_void_drvdata *drvdata)
{
	struct usb_interface *usb_if = to_usb_interface(drvdata->dev->parent);

	if (drvdata->is_wired)
		return;

	usb_set_wireless_status(usb_if, drvdata->connected ?
					USB_WIRELESS_STATUS_CONNECTED :
					USB_WIRELESS_STATUS_DISCONNECTED);
}

static void corsair_void_set_unknown_batt(struct corsair_void_drvdata *drvdata)
{
	struct corsair_void_battery_data *battery_data = &drvdata->battery_data;

	battery_data->status = POWER_SUPPLY_STATUS_UNKNOWN;
	battery_data->present = false;
	battery_data->capacity = 0;
	battery_data->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
}

/* Reset data that may change between wireless connections */
static void corsair_void_set_unknown_wireless_data(struct corsair_void_drvdata *drvdata)
{
	/* Only 0 out headset, receiver is always known if relevant */
	drvdata->fw_headset_major = 0;
	drvdata->fw_headset_minor = 0;

	drvdata->connected = false;
	drvdata->mic_up = false;

	corsair_void_set_wireless_status(drvdata);
}

static void corsair_void_process_receiver(struct corsair_void_drvdata *drvdata,
					  int raw_battery_capacity,
					  int raw_connection_status,
					  int raw_battery_status)
{
	struct corsair_void_battery_data *battery_data = &drvdata->battery_data;
	struct corsair_void_battery_data orig_battery_data;

	/* Save initial battery data, to compare later */
	orig_battery_data = *battery_data;

	/* Headset not connected, or it's wired */
	if (raw_connection_status != CORSAIR_VOID_WIRELESS_CONNECTED)
		goto unknown_battery;

	/* Battery information unavailable */
	if (raw_battery_status == 0)
		goto unknown_battery;

	/* Battery must be connected then */
	battery_data->present = true;
	battery_data->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

	/* Set battery status */
	switch (raw_battery_status) {
	case CORSAIR_VOID_BATTERY_NORMAL:
	case CORSAIR_VOID_BATTERY_LOW:
	case CORSAIR_VOID_BATTERY_CRITICAL:
		battery_data->status = POWER_SUPPLY_STATUS_DISCHARGING;
		if (raw_battery_status == CORSAIR_VOID_BATTERY_LOW)
			battery_data->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (raw_battery_status == CORSAIR_VOID_BATTERY_CRITICAL)
			battery_data->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

		break;
	case CORSAIR_VOID_BATTERY_CHARGED:
		battery_data->status = POWER_SUPPLY_STATUS_FULL;
		break;
	case CORSAIR_VOID_BATTERY_CHARGING:
		battery_data->status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		hid_warn(drvdata->hid_dev, "unknown battery status '%d'",
			 raw_battery_status);
		goto unknown_battery;
		break;
	}

	battery_data->capacity = raw_battery_capacity;
	corsair_void_set_wireless_status(drvdata);

	goto success;
unknown_battery:
	corsair_void_set_unknown_batt(drvdata);
success:

	/* Inform power supply if battery values changed */
	if (memcmp(&orig_battery_data, battery_data, sizeof(*battery_data))) {
		set_bit(CORSAIR_VOID_UPDATE_BATTERY,
			&drvdata->battery_work_flags);
		schedule_work(&drvdata->battery_work);
	}
}

/*
 * Functions to report stored data
*/

static int corsair_void_battery_get_property(struct power_supply *psy,
					     enum power_supply_property prop,
					     union power_supply_propval *val)
{
	struct corsair_void_drvdata *drvdata = power_supply_get_drvdata(psy);

	switch (prop) {
		case POWER_SUPPLY_PROP_SCOPE:
			val->intval = POWER_SUPPLY_SCOPE_DEVICE;
			break;
		case POWER_SUPPLY_PROP_MODEL_NAME:
			if (!strncmp(drvdata->hid_dev->name, "Corsair ", 8))
				val->strval = drvdata->hid_dev->name + 8;
			else
				val->strval = drvdata->hid_dev->name;
			break;
		case POWER_SUPPLY_PROP_MANUFACTURER:
			val->strval = "Corsair";
			break;
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = drvdata->battery_data.status;
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = drvdata->battery_data.present;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = drvdata->battery_data.capacity;
			break;
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = drvdata->battery_data.capacity_level;
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static ssize_t microphone_up_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);

	if (!drvdata->connected)
		return -ENODEV;

	return sysfs_emit(buf, "%d\n", drvdata->mic_up);
}

static ssize_t fw_version_receiver_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->fw_receiver_major == 0 && drvdata->fw_receiver_minor == 0)
		return -ENODATA;

	return sysfs_emit(buf, "%d.%02d\n", drvdata->fw_receiver_major,
			  drvdata->fw_receiver_minor);
}


static ssize_t fw_version_headset_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata->fw_headset_major == 0 && drvdata->fw_headset_minor == 0)
		return -ENODATA;

	return sysfs_emit(buf, "%d.%02d\n", drvdata->fw_headset_major,
			  drvdata->fw_headset_minor);
}

static ssize_t sidetone_max_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", drvdata->sidetone_max);
}

/*
 * Functions to send data to headset
*/

static ssize_t send_alert_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);
	struct hid_device *hid_dev = drvdata->hid_dev;
	unsigned char alert_id;
	unsigned char *send_buf __free(kfree) = NULL;
	int ret;

	if (!drvdata->connected || drvdata->is_wired)
		return -ENODEV;

	/* Only accept 0 or 1 for alert ID */
	if (kstrtou8(buf, 10, &alert_id) || alert_id >= 2)
		return -EINVAL;

	send_buf = kmalloc(3, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	/* Packet format to send alert with ID alert_id */
	send_buf[0] = CORSAIR_VOID_NOTIF_REQUEST_ID;
	send_buf[1] = 0x02;
	send_buf[2] = alert_id;

	ret = hid_hw_raw_request(hid_dev, CORSAIR_VOID_NOTIF_REQUEST_ID,
				 send_buf, 3, HID_OUTPUT_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret < 0)
		hid_warn(hid_dev, "failed to send alert request (reason: %d)",
			 ret);
	else
		ret = count;

	return ret;
}

static int corsair_void_set_sidetone_wired(struct device *dev, const char *buf,
					   unsigned int sidetone)
{
	struct usb_interface *usb_if = to_usb_interface(dev->parent);
	struct usb_device *usb_dev = interface_to_usbdev(usb_if);

	/* Packet format to set sidetone for wired headsets */
	__le16 sidetone_le = cpu_to_le16(sidetone);

	return usb_control_msg_send(usb_dev, 0,
				   CORSAIR_VOID_USB_SIDETONE_REQUEST,
				   CORSAIR_VOID_USB_SIDETONE_REQUEST_TYPE,
				   CORSAIR_VOID_USB_SIDETONE_VALUE,
				   CORSAIR_VOID_USB_SIDETONE_INDEX,
				   &sidetone_le, 2, USB_CTRL_SET_TIMEOUT,
				   GFP_KERNEL);
}

static int corsair_void_set_sidetone_wireless(struct device *dev,
					      const char *buf,
					      unsigned char sidetone)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);
	struct hid_device *hid_dev = drvdata->hid_dev;
	unsigned char *send_buf __free(kfree) = NULL;

	send_buf = kmalloc(12, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	/* Packet format to set sidetone for wireless headsets */
	send_buf[0] = CORSAIR_VOID_SIDETONE_REQUEST_ID;
	send_buf[1] = 0x0B;
	send_buf[2] = 0x00;
	send_buf[3] = 0xFF;
	send_buf[4] = 0x04;
	send_buf[5] = 0x0E;
	send_buf[6] = 0xFF;
	send_buf[7] = 0x05;
	send_buf[8] = 0x01;
	send_buf[9] = 0x04;
	send_buf[10] = 0x00;
	send_buf[11] = sidetone + 200;

	return hid_hw_raw_request(hid_dev, CORSAIR_VOID_SIDETONE_REQUEST_ID,
				  send_buf, 12, HID_FEATURE_REPORT,
				  HID_REQ_SET_REPORT);
}

static ssize_t set_sidetone_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct corsair_void_drvdata *drvdata = dev_get_drvdata(dev);
	struct hid_device *hid_dev = drvdata->hid_dev;
	unsigned int sidetone;
	int ret;

	if (!drvdata->connected)
		return -ENODEV;

	/* sidetone must be between 0 and drvdata->sidetone_max inclusive */
	if (kstrtouint(buf, 10, &sidetone) || sidetone > drvdata->sidetone_max)
		return -EINVAL;

	if (drvdata->is_wired)
		ret = corsair_void_set_sidetone_wired(dev, buf, sidetone);
	else
		ret = corsair_void_set_sidetone_wireless(dev, buf, sidetone);

	if (ret < 0)
		hid_warn(hid_dev, "failed to send sidetone (reason: %d)", ret);
	else
		ret = count;

	return ret;
}

static int corsair_void_request_status(struct hid_device *hid_dev, int id)
{
	unsigned char *send_buf __free(kfree) = NULL;

	send_buf = kmalloc(2, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	/* Packet format to request data item (status / firmware) refresh */
	send_buf[0] = CORSAIR_VOID_STATUS_REQUEST_ID;
	send_buf[1] = id;

	/* Send request for data refresh */
	return hid_hw_raw_request(hid_dev, CORSAIR_VOID_STATUS_REQUEST_ID,
				  send_buf, 2, HID_OUTPUT_REPORT,
				  HID_REQ_SET_REPORT);
}

/*
 * Headset connect / disconnect handlers and work handlers
*/

static void corsair_void_status_work_handler(struct work_struct *work)
{
	struct corsair_void_drvdata *drvdata;
	struct delayed_work *delayed_work;
	int battery_ret;

	delayed_work = to_delayed_work(work);
	drvdata = container_of(delayed_work, struct corsair_void_drvdata,
			       delayed_status_work);

	battery_ret = corsair_void_request_status(drvdata->hid_dev,
						  CORSAIR_VOID_STATUS_REPORT_ID);
	if (battery_ret < 0) {
		hid_warn(drvdata->hid_dev,
			 "failed to request battery (reason: %d)", battery_ret);
	}
}

static void corsair_void_firmware_work_handler(struct work_struct *work)
{
	struct corsair_void_drvdata *drvdata;
	struct delayed_work *delayed_work;
	int firmware_ret;

	delayed_work = to_delayed_work(work);
	drvdata = container_of(delayed_work, struct corsair_void_drvdata,
			       delayed_firmware_work);

	firmware_ret = corsair_void_request_status(drvdata->hid_dev,
						   CORSAIR_VOID_FIRMWARE_REPORT_ID);
	if (firmware_ret < 0) {
		hid_warn(drvdata->hid_dev,
			 "failed to request firmware (reason: %d)", firmware_ret);
	}

}

static void corsair_void_add_battery(struct corsair_void_drvdata *drvdata)
{
	struct power_supply_config psy_cfg = {};
	struct power_supply *new_supply;

	if (drvdata->battery)
		return;

	psy_cfg.drv_data = drvdata;
	new_supply = power_supply_register(drvdata->dev,
					   &drvdata->battery_desc,
					   &psy_cfg);

	if (IS_ERR(new_supply)) {
		hid_err(drvdata->hid_dev,
			"failed to register battery '%s' (reason: %pe)\n",
			drvdata->battery_desc.name, new_supply);
		return;
	}

	if (power_supply_powers(new_supply, drvdata->dev)) {
		power_supply_unregister(new_supply);
		return;
	}

	drvdata->battery = new_supply;
}

static void corsair_void_battery_work_handler(struct work_struct *work)
{
	struct corsair_void_drvdata *drvdata = container_of(work,
		struct corsair_void_drvdata, battery_work);

	bool add_battery = test_and_clear_bit(CORSAIR_VOID_ADD_BATTERY,
					      &drvdata->battery_work_flags);
	bool remove_battery = test_and_clear_bit(CORSAIR_VOID_REMOVE_BATTERY,
						 &drvdata->battery_work_flags);
	bool update_battery = test_and_clear_bit(CORSAIR_VOID_UPDATE_BATTERY,
						 &drvdata->battery_work_flags);

	if (add_battery && !remove_battery) {
		corsair_void_add_battery(drvdata);
	} else if (remove_battery && !add_battery && drvdata->battery) {
		power_supply_unregister(drvdata->battery);
		drvdata->battery = NULL;
	}

	if (update_battery && drvdata->battery)
		power_supply_changed(drvdata->battery);

}

static void corsair_void_headset_connected(struct corsair_void_drvdata *drvdata)
{
	set_bit(CORSAIR_VOID_ADD_BATTERY, &drvdata->battery_work_flags);
	schedule_work(&drvdata->battery_work);
	schedule_delayed_work(&drvdata->delayed_firmware_work,
			      msecs_to_jiffies(100));
}

static void corsair_void_headset_disconnected(struct corsair_void_drvdata *drvdata)
{
	set_bit(CORSAIR_VOID_REMOVE_BATTERY, &drvdata->battery_work_flags);
	schedule_work(&drvdata->battery_work);

	corsair_void_set_unknown_wireless_data(drvdata);
	corsair_void_set_unknown_batt(drvdata);
}

/*
 * Driver setup, probing and HID event handling
*/

static DEVICE_ATTR_RO(fw_version_receiver);
static DEVICE_ATTR_RO(fw_version_headset);
static DEVICE_ATTR_RO(microphone_up);
static DEVICE_ATTR_RO(sidetone_max);

static DEVICE_ATTR_WO(send_alert);
static DEVICE_ATTR_WO(set_sidetone);

static struct attribute *corsair_void_attrs[] = {
	&dev_attr_fw_version_receiver.attr,
	&dev_attr_fw_version_headset.attr,
	&dev_attr_microphone_up.attr,
	&dev_attr_send_alert.attr,
	&dev_attr_set_sidetone.attr,
	&dev_attr_sidetone_max.attr,
	NULL,
};

static const struct attribute_group corsair_void_attr_group = {
	.attrs = corsair_void_attrs,
};

static int corsair_void_probe(struct hid_device *hid_dev,
			      const struct hid_device_id *hid_id)
{
	int ret;
	struct corsair_void_drvdata *drvdata;
	char *name;

	if (!hid_is_usb(hid_dev))
		return -EINVAL;

	drvdata = devm_kzalloc(&hid_dev->dev, sizeof(*drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	hid_set_drvdata(hid_dev, drvdata);
	dev_set_drvdata(&hid_dev->dev, drvdata);

	drvdata->dev = &hid_dev->dev;
	drvdata->hid_dev = hid_dev;
	drvdata->is_wired = hid_id->driver_data == CORSAIR_VOID_WIRED;

	drvdata->sidetone_max = CORSAIR_VOID_SIDETONE_MAX_WIRELESS;
	if (drvdata->is_wired)
		drvdata->sidetone_max = CORSAIR_VOID_SIDETONE_MAX_WIRED;

	/* Set initial values for no wireless headset attached */
	/* If a headset is attached, it'll be prompted later */
	corsair_void_set_unknown_wireless_data(drvdata);
	corsair_void_set_unknown_batt(drvdata);

	/* Receiver version won't be reset after init */
	/* Headset version already set via set_unknown_wireless_data */
	drvdata->fw_receiver_major = 0;
	drvdata->fw_receiver_minor = 0;

	ret = hid_parse(hid_dev);
	if (ret) {
		hid_err(hid_dev, "parse failed (reason: %d)\n", ret);
		return ret;
	}

	name = devm_kasprintf(drvdata->dev, GFP_KERNEL,
			      "corsair-void-%d-battery", hid_dev->id);
	if (!name)
		return -ENOMEM;

	drvdata->battery_desc.name = name;
	drvdata->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	drvdata->battery_desc.properties = corsair_void_battery_props;
	drvdata->battery_desc.num_properties = ARRAY_SIZE(corsair_void_battery_props);
	drvdata->battery_desc.get_property = corsair_void_battery_get_property;

	drvdata->battery = NULL;
	INIT_WORK(&drvdata->battery_work, corsair_void_battery_work_handler);

	ret = sysfs_create_group(&hid_dev->dev.kobj, &corsair_void_attr_group);
	if (ret)
		return ret;

	/* Any failures after here will need to call hid_hw_stop */
	ret = hid_hw_start(hid_dev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hid_dev, "hid_hw_start failed (reason: %d)\n", ret);
		goto failed_after_sysfs;
	}

	/* Refresh battery data, in case wireless headset is already connected */
	INIT_DELAYED_WORK(&drvdata->delayed_status_work,
			  corsair_void_status_work_handler);
	schedule_delayed_work(&drvdata->delayed_status_work,
			      msecs_to_jiffies(100));

	/* Refresh firmware versions */
	INIT_DELAYED_WORK(&drvdata->delayed_firmware_work,
			  corsair_void_firmware_work_handler);
	schedule_delayed_work(&drvdata->delayed_firmware_work,
			      msecs_to_jiffies(100));

	return 0;

failed_after_sysfs:
	sysfs_remove_group(&hid_dev->dev.kobj, &corsair_void_attr_group);
	return ret;
}

static void corsair_void_remove(struct hid_device *hid_dev)
{
	struct corsair_void_drvdata *drvdata = hid_get_drvdata(hid_dev);

	hid_hw_stop(hid_dev);
	cancel_work_sync(&drvdata->battery_work);
	if (drvdata->battery)
		power_supply_unregister(drvdata->battery);

	cancel_delayed_work_sync(&drvdata->delayed_status_work);
	cancel_delayed_work_sync(&drvdata->delayed_firmware_work);
	sysfs_remove_group(&hid_dev->dev.kobj, &corsair_void_attr_group);
}

static int corsair_void_raw_event(struct hid_device *hid_dev,
				  struct hid_report *hid_report,
				  u8 *data, int size)
{
	struct corsair_void_drvdata *drvdata = hid_get_drvdata(hid_dev);
	bool was_connected = drvdata->connected;

	/* Description of packets are documented at the top of this file */
	if (hid_report->id == CORSAIR_VOID_STATUS_REPORT_ID) {
		drvdata->mic_up = FIELD_GET(CORSAIR_VOID_MIC_MASK, data[2]);
		drvdata->connected = (data[3] == CORSAIR_VOID_WIRELESS_CONNECTED) ||
				     drvdata->is_wired;

		corsair_void_process_receiver(drvdata,
					      FIELD_GET(CORSAIR_VOID_CAPACITY_MASK, data[2]),
					      data[3], data[4]);
	} else if (hid_report->id == CORSAIR_VOID_FIRMWARE_REPORT_ID) {
		drvdata->fw_receiver_major = data[1];
		drvdata->fw_receiver_minor = data[2];
		drvdata->fw_headset_major = data[3];
		drvdata->fw_headset_minor = data[4];
	}

	/* Handle wireless headset connect / disconnect */
	if ((was_connected != drvdata->connected) && !drvdata->is_wired) {
		if (drvdata->connected)
			corsair_void_headset_connected(drvdata);
		else
			corsair_void_headset_disconnected(drvdata);
	}

	return 0;
}

static const struct hid_device_id corsair_void_devices[] = {
	/* Corsair Void Wireless */
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a0c),
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a2b),
	CORSAIR_VOID_WIRELESS_DEVICE(0x1b23),
	CORSAIR_VOID_WIRELESS_DEVICE(0x1b25),
	CORSAIR_VOID_WIRELESS_DEVICE(0x1b27),

	/* Corsair Void USB */
	CORSAIR_VOID_WIRED_DEVICE(0x0a0f),
	CORSAIR_VOID_WIRED_DEVICE(0x1b1c),
	CORSAIR_VOID_WIRED_DEVICE(0x1b29),
	CORSAIR_VOID_WIRED_DEVICE(0x1b2a),

	/* Corsair Void Surround */
	CORSAIR_VOID_WIRED_DEVICE(0x0a30),
	CORSAIR_VOID_WIRED_DEVICE(0x0a31),

	/* Corsair Void Pro Wireless */
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a14),
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a16),
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a1a),

	/* Corsair Void Pro USB */
	CORSAIR_VOID_WIRED_DEVICE(0x0a17),
	CORSAIR_VOID_WIRED_DEVICE(0x0a1d),

	/* Corsair Void Pro Surround */
	CORSAIR_VOID_WIRED_DEVICE(0x0a18),
	CORSAIR_VOID_WIRED_DEVICE(0x0a1e),
	CORSAIR_VOID_WIRED_DEVICE(0x0a1f),

	/* Corsair Void Elite Wireless */
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a51),
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a55),
	CORSAIR_VOID_WIRELESS_DEVICE(0x0a75),

	/* Corsair Void Elite USB */
	CORSAIR_VOID_WIRED_DEVICE(0x0a52),
	CORSAIR_VOID_WIRED_DEVICE(0x0a56),

	/* Corsair Void Elite Surround */
	CORSAIR_VOID_WIRED_DEVICE(0x0a53),
	CORSAIR_VOID_WIRED_DEVICE(0x0a57),

	{}
};

MODULE_DEVICE_TABLE(hid, corsair_void_devices);

static struct hid_driver corsair_void_driver = {
	.name = "hid-corsair-void",
	.id_table = corsair_void_devices,
	.probe = corsair_void_probe,
	.remove = corsair_void_remove,
	.raw_event = corsair_void_raw_event,
};

module_hid_driver(corsair_void_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stuart Hayhurst <stuart.a.hayhurst@gmail.com>");
MODULE_DESCRIPTION("HID driver for Corsair Void headsets");
