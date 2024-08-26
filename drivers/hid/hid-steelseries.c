// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Steelseries devices
 *
 *  Copyright (c) 2013 Simon Wood
 *  Copyright (c) 2023 Bastien Nocera
 */

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/leds.h>

#include "hid-ids.h"

#define STEELSERIES_SRWS1		BIT(0)
#define STEELSERIES_ARCTIS_1		BIT(1)

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
};

#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
#define SRWS1_NUMBER_LEDS 15
struct steelseries_srws1_data {
	__u16 led_state;
	/* the last element is used for setting all leds simultaneously */
	struct led_classdev *led[SRWS1_NUMBER_LEDS + 1];
};
#endif

/* Fixed report descriptor for Steelseries SRW-S1 wheel controller
 *
 * The original descriptor hides the sensitivity and assists dials
 * a custom vendor usage page. This inserts a patch to make them
 * appear in the 'Generic Desktop' usage.
 */

static __u8 steelseries_srws1_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop)                */
0x09, 0x08,         /*  Usage (MultiAxis), Changed          */
0xA1, 0x01,         /*  Collection (Application),           */
0xA1, 0x02,         /*      Collection (Logical),           */
0x95, 0x01,         /*          Report Count (1),           */
0x05, 0x01,         /* Changed  Usage Page (Desktop),       */
0x09, 0x30,         /* Changed  Usage (X),                  */
0x16, 0xF8, 0xF8,   /*          Logical Minimum (-1800),    */
0x26, 0x08, 0x07,   /*          Logical Maximum (1800),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x55, 0x0F,         /*          Unit Exponent (15),         */
0x75, 0x10,         /*          Report Size (16),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x31,         /* Changed  Usage (Y),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x32,         /* Changed  Usage (Z),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x39,         /*          Usage (Hat Switch),         */
0x25, 0x07,         /*          Logical Maximum (7),        */
0x35, 0x00,         /*          Physical Minimum (0),       */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x81, 0x02,         /*          Input (Variable),           */
0x25, 0x01,         /*          Logical Maximum (1),        */
0x45, 0x01,         /*          Physical Maximum (1),       */
0x65, 0x00,         /*          Unit,                       */
0x75, 0x01,         /*          Report Size (1),            */
0x95, 0x03,         /*          Report Count (3),           */
0x81, 0x01,         /*          Input (Constant),           */
0x05, 0x09,         /*          Usage Page (Button),        */
0x19, 0x01,         /*          Usage Minimum (01h),        */
0x29, 0x11,         /*          Usage Maximum (11h),        */
0x95, 0x11,         /*          Report Count (17),          */
0x81, 0x02,         /*          Input (Variable),           */
                    /*   ---- Dial patch starts here ----   */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x33,         /*          Usage (RX),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x02,         /*          Report Count (2),           */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x25, 0x0b,         /*          Logical Maximum (b),        */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x35,         /*          Usage (RZ),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x25, 0x03,         /*          Logical Maximum (3),        */
0x81, 0x02,         /*          Input (Variable),           */
                    /*    ---- Dial patch ends here ----    */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x01,         /*          Usage (01h),                */
0x75, 0x04,         /* Changed  Report Size (4),            */
0x95, 0x0D,         /* Changed  Report Count (13),          */
0x81, 0x02,         /*          Input (Variable),           */
0xC0,               /*      End Collection,                 */
0xA1, 0x02,         /*      Collection (Logical),           */
0x09, 0x02,         /*          Usage (02h),                */
0x75, 0x08,         /*          Report Size (8),            */
0x95, 0x10,         /*          Report Count (16),          */
0x91, 0x02,         /*          Output (Variable),          */
0xC0,               /*      End Collection,                 */
0xC0                /*  End Collection                      */
};

#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
static void steelseries_srws1_set_leds(struct hid_device *hdev, __u16 leds)
{
	struct list_head *report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0x40;
	value[1] = leds & 0xFF;
	value[2] = leds >> 8;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	value[7] = 0x00;
	value[8] = 0x00;
	value[9] = 0x00;
	value[10] = 0x00;
	value[11] = 0x00;
	value[12] = 0x00;
	value[13] = 0x00;
	value[14] = 0x00;
	value[15] = 0x00;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	/* Note: LED change does not show on device until the device is read/polled */
}

static void steelseries_srws1_led_all_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return;
	}

	if (value == LED_OFF)
		drv_data->led_state = 0;
	else
		drv_data->led_state = (1 << (SRWS1_NUMBER_LEDS + 1)) - 1;

	steelseries_srws1_set_leds(hid, drv_data->led_state);
}

static enum led_brightness steelseries_srws1_led_all_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data;

	drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return LED_OFF;
	}

	return (drv_data->led_state >> SRWS1_NUMBER_LEDS) ? LED_FULL : LED_OFF;
}

static void steelseries_srws1_led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data = hid_get_drvdata(hid);
	int i, state = 0;

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return;
	}

	for (i = 0; i < SRWS1_NUMBER_LEDS; i++) {
		if (led_cdev != drv_data->led[i])
			continue;

		state = (drv_data->led_state >> i) & 1;
		if (value == LED_OFF && state) {
			drv_data->led_state &= ~(1 << i);
			steelseries_srws1_set_leds(hid, drv_data->led_state);
		} else if (value != LED_OFF && !state) {
			drv_data->led_state |= 1 << i;
			steelseries_srws1_set_leds(hid, drv_data->led_state);
		}
		break;
	}
}

static enum led_brightness steelseries_srws1_led_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data;
	int i, value = 0;

	drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return LED_OFF;
	}

	for (i = 0; i < SRWS1_NUMBER_LEDS; i++)
		if (led_cdev == drv_data->led[i]) {
			value = (drv_data->led_state >> i) & 1;
			break;
		}

	return value ? LED_FULL : LED_OFF;
}

static int steelseries_srws1_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret, i;
	struct led_classdev *led;
	size_t name_sz;
	char *name;

	struct steelseries_srws1_data *drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);

	if (drv_data == NULL) {
		hid_err(hdev, "can't alloc SRW-S1 memory\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, drv_data);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 0, 0, 16)) {
		ret = -ENODEV;
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	/* register led subsystem */
	drv_data->led_state = 0;
	for (i = 0; i < SRWS1_NUMBER_LEDS + 1; i++)
		drv_data->led[i] = NULL;

	steelseries_srws1_set_leds(hdev, 0);

	name_sz = strlen(hdev->uniq) + 16;

	/* 'ALL', for setting all LEDs simultaneously */
	led = kzalloc(sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
	if (!led) {
		hid_err(hdev, "can't allocate memory for LED ALL\n");
		goto err_led;
	}

	name = (void *)(&led[1]);
	snprintf(name, name_sz, "SRWS1::%s::RPMALL", hdev->uniq);
	led->name = name;
	led->brightness = 0;
	led->max_brightness = 1;
	led->brightness_get = steelseries_srws1_led_all_get_brightness;
	led->brightness_set = steelseries_srws1_led_all_set_brightness;

	drv_data->led[SRWS1_NUMBER_LEDS] = led;
	ret = led_classdev_register(&hdev->dev, led);
	if (ret)
		goto err_led;

	/* Each individual LED */
	for (i = 0; i < SRWS1_NUMBER_LEDS; i++) {
		led = kzalloc(sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
		if (!led) {
			hid_err(hdev, "can't allocate memory for LED %d\n", i);
			goto err_led;
		}

		name = (void *)(&led[1]);
		snprintf(name, name_sz, "SRWS1::%s::RPM%d", hdev->uniq, i+1);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = steelseries_srws1_led_get_brightness;
		led->brightness_set = steelseries_srws1_led_set_brightness;

		drv_data->led[i] = led;
		ret = led_classdev_register(&hdev->dev, led);

		if (ret) {
			hid_err(hdev, "failed to register LED %d. Aborting.\n", i);
err_led:
			/* Deregister all LEDs (if any) */
			for (i = 0; i < SRWS1_NUMBER_LEDS + 1; i++) {
				led = drv_data->led[i];
				drv_data->led[i] = NULL;
				if (!led)
					continue;
				led_classdev_unregister(led);
				kfree(led);
			}
			goto out;	/* but let the driver continue without LEDs */
		}
	}
out:
	return 0;
err_free:
	kfree(drv_data);
	return ret;
}

static void steelseries_srws1_remove(struct hid_device *hdev)
{
	int i;
	struct led_classdev *led;

	struct steelseries_srws1_data *drv_data = hid_get_drvdata(hdev);

	if (drv_data) {
		/* Deregister LEDs (if any) */
		for (i = 0; i < SRWS1_NUMBER_LEDS + 1; i++) {
			led = drv_data->led[i];
			drv_data->led[i] = NULL;
			if (!led)
				continue;
			led_classdev_unregister(led);
			kfree(led);
		}

	}

	hid_hw_stop(hdev);
	kfree(drv_data);
	return;
}
#endif

#define STEELSERIES_HEADSET_BATTERY_TIMEOUT_MS	3000

#define ARCTIS_1_BATTERY_RESPONSE_LEN		8
static const char arctis_1_battery_request[] = { 0x06, 0x12 };

static int steelseries_headset_arctis_1_fetch_battery(struct hid_device *hdev)
{
	u8 *write_buf;
	int ret;

	/* Request battery information */
	write_buf = kmemdup(arctis_1_battery_request, sizeof(arctis_1_battery_request), GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, arctis_1_battery_request[0],
				 write_buf, sizeof(arctis_1_battery_request),
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < (int)sizeof(arctis_1_battery_request)) {
		hid_err(hdev, "hid_hw_raw_request() failed with %d\n", ret);
		ret = -ENODATA;
	}
	kfree(write_buf);
	return ret;
}

static void steelseries_headset_fetch_battery(struct hid_device *hdev)
{
	struct steelseries_device *sd = hid_get_drvdata(hdev);
	int ret = 0;

	if (sd->quirks & STEELSERIES_ARCTIS_1)
		ret = steelseries_headset_arctis_1_fetch_battery(hdev);

	if (ret < 0)
		hid_dbg(hdev,
			"Battery query failed (err: %d)\n", ret);
}

static void steelseries_headset_battery_timer_tick(struct work_struct *work)
{
	struct steelseries_device *sd = container_of(work,
		struct steelseries_device, battery_work.work);
	struct hid_device *hdev = sd->hdev;

	steelseries_headset_fetch_battery(hdev);
}

static int steelseries_headset_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct steelseries_device *sd = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sd->headset_connected ?
			POWER_SUPPLY_STATUS_DISCHARGING :
			POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = sd->battery_capacity;
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
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CAPACITY,
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
	steelseries_headset_fetch_battery(sd->hdev);

	return 0;
}

static int steelseries_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct steelseries_device *sd;
	int ret;

	sd = devm_kzalloc(&hdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;
	hid_set_drvdata(hdev, sd);
	sd->hdev = hdev;
	sd->quirks = id->driver_data;

	if (sd->quirks & STEELSERIES_SRWS1) {
#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
		return steelseries_srws1_probe(hdev, id);
#else
		return -ENODEV;
#endif
	}

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	spin_lock_init(&sd->lock);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	if (steelseries_headset_battery_register(sd) < 0)
		hid_err(sd->hdev,
			"Failed to register battery for headset\n");

	return ret;
}

static void steelseries_remove(struct hid_device *hdev)
{
	struct steelseries_device *sd = hid_get_drvdata(hdev);
	unsigned long flags;

	if (sd->quirks & STEELSERIES_SRWS1) {
#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
		steelseries_srws1_remove(hdev);
#endif
		return;
	}

	spin_lock_irqsave(&sd->lock, flags);
	sd->removed = true;
	spin_unlock_irqrestore(&sd->lock, flags);

	cancel_delayed_work_sync(&sd->battery_work);

	hid_hw_stop(hdev);
}

static __u8 *steelseries_srws1_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (hdev->vendor != USB_VENDOR_ID_STEELSERIES ||
	    hdev->product != USB_DEVICE_ID_STEELSERIES_SRWS1)
		return rdesc;

	if (*rsize >= 115 && rdesc[11] == 0x02 && rdesc[13] == 0xc8
			&& rdesc[29] == 0xbb && rdesc[40] == 0xc5) {
		hid_info(hdev, "Fixing up Steelseries SRW-S1 report descriptor\n");
		rdesc = steelseries_srws1_rdesc_fixed;
		*rsize = sizeof(steelseries_srws1_rdesc_fixed);
	}
	return rdesc;
}

static int steelseries_headset_raw_event(struct hid_device *hdev,
					struct hid_report *report, u8 *read_buf,
					int size)
{
	struct steelseries_device *sd = hid_get_drvdata(hdev);
	int capacity = sd->battery_capacity;
	bool connected = sd->headset_connected;
	unsigned long flags;

	/* Not a headset */
	if (sd->quirks & STEELSERIES_SRWS1)
		return 0;

	if (sd->quirks & STEELSERIES_ARCTIS_1) {
		hid_dbg(sd->hdev,
			"Parsing raw event for Arctis 1 headset (%*ph)\n", size, read_buf);
		if (size < ARCTIS_1_BATTERY_RESPONSE_LEN ||
		    memcmp (read_buf, arctis_1_battery_request, sizeof(arctis_1_battery_request)))
			return 0;
		if (read_buf[2] == 0x01) {
			connected = false;
			capacity = 100;
		} else {
			connected = true;
			capacity = read_buf[3];
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

	spin_lock_irqsave(&sd->lock, flags);
	if (!sd->removed)
		schedule_delayed_work(&sd->battery_work,
				msecs_to_jiffies(STEELSERIES_HEADSET_BATTERY_TIMEOUT_MS));
	spin_unlock_irqrestore(&sd->lock, flags);

	return 0;
}

static const struct hid_device_id steelseries_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_SRWS1),
	  .driver_data = STEELSERIES_SRWS1 },

	{ /* SteelSeries Arctis 1 Wireless for XBox */
	  HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, 0x12b6),
	.driver_data = STEELSERIES_ARCTIS_1 },

	{ }
};
MODULE_DEVICE_TABLE(hid, steelseries_devices);

static struct hid_driver steelseries_driver = {
	.name = "steelseries",
	.id_table = steelseries_devices,
	.probe = steelseries_probe,
	.remove = steelseries_remove,
	.report_fixup = steelseries_srws1_report_fixup,
	.raw_event = steelseries_headset_raw_event,
};

module_hid_driver(steelseries_driver);
MODULE_DESCRIPTION("HID driver for Steelseries devices");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bastien Nocera <hadess@hadess.net>");
MODULE_AUTHOR("Simon Wood <simon@mungewell.org>");
