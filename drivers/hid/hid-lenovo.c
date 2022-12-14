// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Lenovo:
 *  - ThinkPad USB Keyboard with TrackPoint (tpkbd)
 *  - ThinkPad Compact Bluetooth Keyboard with TrackPoint (cptkbd)
 *  - ThinkPad Compact USB Keyboard with TrackPoint (cptkbd)
 *  - ThinkPad TrackPoint Keyboard II USB/Bluetooth (cptkbd/tpIIkbd)
 *
 *  Copyright (c) 2012 Bernhard Seibold
 *  Copyright (c) 2014 Jamie Lentin <jm@lentin.co.uk>
 *
 * Linux IBM/Lenovo Scrollpoint mouse driver:
 * - IBM Scrollpoint III
 * - IBM Scrollpoint Pro
 * - IBM Scrollpoint Optical
 * - IBM Scrollpoint Optical 800dpi
 * - IBM Scrollpoint Optical 800dpi Pro
 * - Lenovo Scrollpoint Optical
 *
 *  Copyright (c) 2012 Peter De Wachter <pdewacht@gmail.com>
 *  Copyright (c) 2018 Peter Ganzhorn <peter.ganzhorn@gmail.com>
 */

/*
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

/* Userspace expects F20 for mic-mute KEY_MICMUTE does not work */
#define LENOVO_KEY_MICMUTE KEY_F20

struct lenovo_drvdata {
	u8 led_report[3]; /* Must be first for proper alignment */
	int led_state;
	struct mutex led_report_mutex;
	struct led_classdev led_mute;
	struct led_classdev led_micmute;
	struct work_struct fn_lock_sync_work;
	struct hid_device *hdev;
	int press_to_select;
	int dragging;
	int release_to_select;
	int select_right;
	int sensitivity;
	int press_speed;
	u8 middlebutton_state; /* 0:Up, 1:Down (undecided), 2:Scrolling */
	bool fn_lock;
};

#define map_key_clear(c) hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

#define TP10UBKBD_LED_OUTPUT_REPORT	9

#define TP10UBKBD_FN_LOCK_LED		0x54
#define TP10UBKBD_MUTE_LED		0x64
#define TP10UBKBD_MICMUTE_LED		0x74

#define TP10UBKBD_LED_OFF		1
#define TP10UBKBD_LED_ON		2

static int lenovo_led_set_tp10ubkbd(struct hid_device *hdev, u8 led_code,
				    enum led_brightness value)
{
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);
	int ret;

	mutex_lock(&data->led_report_mutex);

	data->led_report[0] = TP10UBKBD_LED_OUTPUT_REPORT;
	data->led_report[1] = led_code;
	data->led_report[2] = value ? TP10UBKBD_LED_ON : TP10UBKBD_LED_OFF;
	ret = hid_hw_raw_request(hdev, data->led_report[0], data->led_report, 3,
				 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret != 3) {
		if (ret != -ENODEV)
			hid_err(hdev, "Set LED output report error: %d\n", ret);

		ret = ret < 0 ? ret : -EIO;
	} else {
		ret = 0;
	}

	mutex_unlock(&data->led_report_mutex);

	return ret;
}

static void lenovo_tp10ubkbd_sync_fn_lock(struct work_struct *work)
{
	struct lenovo_drvdata *data =
		container_of(work, struct lenovo_drvdata, fn_lock_sync_work);

	lenovo_led_set_tp10ubkbd(data->hdev, TP10UBKBD_FN_LOCK_LED,
				 data->fn_lock);
}

static const __u8 lenovo_pro_dock_need_fixup_collection[] = {
	0x05, 0x88,		/* Usage Page (Vendor Usage Page 0x88)	*/
	0x09, 0x01,		/* Usage (Vendor Usage 0x01)		*/
	0xa1, 0x01,		/* Collection (Application)		*/
	0x85, 0x04,		/*  Report ID (4)			*/
	0x19, 0x00,		/*  Usage Minimum (0)			*/
	0x2a, 0xff, 0xff,	/*  Usage Maximum (65535)		*/
};

/* Broken ThinkPad TrackPoint II collection (Bluetooth mode) */
static const __u8 lenovo_tpIIbtkbd_need_fixup_collection[] = {
	0x06, 0x00, 0xFF,	/* Usage Page (Vendor Defined 0xFF00) */
	0x09, 0x01,		/* Usage (0x01) */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, 0x05,		/*   Report ID (5) */
	0x1A, 0xF1, 0x00,	/*   Usage Minimum (0xF1) */
	0x2A, 0xFC, 0x00,	/*   Usage Maximum (0xFC) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x25, 0x01,		/*   Logical Maximum (1) */
	0x75, 0x01,		/*   Report Size (1) */
	0x95, 0x0D,		/*   Report Count (13) */
	0x81, 0x02,		/*   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position) */
	0x95, 0x03,		/*   Report Count (3) */
	0x81, 0x01,		/*   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position) */
};

static __u8 *lenovo_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPPRODOCK:
		/* the fixups that need to be done:
		 *   - get a reasonable usage max for the vendor collection
		 *     0x8801 from the report ID 4
		 */
		if (*rsize >= 153 &&
		    memcmp(&rdesc[140], lenovo_pro_dock_need_fixup_collection,
			  sizeof(lenovo_pro_dock_need_fixup_collection)) == 0) {
			rdesc[151] = 0x01;
			rdesc[152] = 0x00;
		}
		break;
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		if (*rsize >= 263 &&
		    memcmp(&rdesc[234], lenovo_tpIIbtkbd_need_fixup_collection,
			  sizeof(lenovo_tpIIbtkbd_need_fixup_collection)) == 0) {
			rdesc[244] = 0x00; /* usage minimum = 0x00 */
			rdesc[247] = 0xff; /* usage maximum = 0xff */
			rdesc[252] = 0xff; /* logical maximum = 0xff */
			rdesc[254] = 0x08; /* report size = 0x08 */
			rdesc[256] = 0x01; /* report count = 0x01 */
			rdesc[258] = 0x00; /* input = 0x00 */
			rdesc[260] = 0x01; /* report count (2) = 0x01 */
		}
		break;
	}
	return rdesc;
}

static int lenovo_input_mapping_tpkbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (usage->hid == (HID_UP_BUTTON | 0x0010)) {
		/* This sub-device contains trackpoint, mark it */
		hid_set_drvdata(hdev, (void *)1);
		map_key_clear(LENOVO_KEY_MICMUTE);
		return 1;
	}
	return 0;
}

static int lenovo_input_mapping_cptkbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/* HID_UP_LNVENDOR = USB, HID_UP_MSVENDOR = BT */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR ||
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_LNVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0x00f1: /* Fn-F4: Mic mute */
			map_key_clear(LENOVO_KEY_MICMUTE);
			return 1;
		case 0x00f2: /* Fn-F5: Brightness down */
			map_key_clear(KEY_BRIGHTNESSDOWN);
			return 1;
		case 0x00f3: /* Fn-F6: Brightness up */
			map_key_clear(KEY_BRIGHTNESSUP);
			return 1;
		case 0x00f4: /* Fn-F7: External display (projector) */
			map_key_clear(KEY_SWITCHVIDEOMODE);
			return 1;
		case 0x00f5: /* Fn-F8: Wireless */
			map_key_clear(KEY_WLAN);
			return 1;
		case 0x00f6: /* Fn-F9: Control panel */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 0x00f8: /* Fn-F11: View open applications (3 boxes) */
			map_key_clear(KEY_SCALE);
			return 1;
		case 0x00f9: /* Fn-F12: Open My computer (6 boxes) USB-only */
			/* NB: This mapping is invented in raw_event below */
			map_key_clear(KEY_FILE);
			return 1;
		case 0x00fa: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		case 0x00fb: /* Middle mouse button (in native mode) */
			map_key_clear(BTN_MIDDLE);
			return 1;
		}
	}

	/* Compatibility middle/wheel mappings should be ignored */
	if (usage->hid == HID_GD_WHEEL)
		return -1;
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON &&
			(usage->hid & HID_USAGE) == 0x003)
		return -1;
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER &&
			(usage->hid & HID_USAGE) == 0x238)
		return -1;

	/* Map wheel emulation reports: 0xffa1 = USB, 0xff10 = BT */
	if ((usage->hid & HID_USAGE_PAGE) == 0xff100000 ||
	    (usage->hid & HID_USAGE_PAGE) == 0xffa10000) {
		field->flags |= HID_MAIN_ITEM_RELATIVE | HID_MAIN_ITEM_VARIABLE;
		field->logical_minimum = -127;
		field->logical_maximum = 127;

		switch (usage->hid & HID_USAGE) {
		case 0x0000:
			hid_map_usage(hi, usage, bit, max, EV_REL, REL_HWHEEL);
			return 1;
		case 0x0001:
			hid_map_usage(hi, usage, bit, max, EV_REL, REL_WHEEL);
			return 1;
		default:
			return -1;
		}
	}

	return 0;
}

static int lenovo_input_mapping_tpIIkbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/*
	 * 0xff0a0000 = USB, HID_UP_MSVENDOR = BT.
	 *
	 * In BT mode, there are two HID_UP_MSVENDOR pages.
	 * Use only the page that contains report ID == 5.
	 */
	if (((usage->hid & HID_USAGE_PAGE) == 0xff0a0000 ||
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR) &&
	    field->report->id == 5) {
		switch (usage->hid & HID_USAGE) {
		case 0x00bb: /* Fn-F4: Mic mute */
			map_key_clear(LENOVO_KEY_MICMUTE);
			return 1;
		case 0x00c3: /* Fn-F5: Brightness down */
			map_key_clear(KEY_BRIGHTNESSDOWN);
			return 1;
		case 0x00c4: /* Fn-F6: Brightness up */
			map_key_clear(KEY_BRIGHTNESSUP);
			return 1;
		case 0x00c1: /* Fn-F8: Notification center */
			map_key_clear(KEY_NOTIFICATION_CENTER);
			return 1;
		case 0x00bc: /* Fn-F9: Control panel */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 0x00b6: /* Fn-F10: Bluetooth */
			map_key_clear(KEY_BLUETOOTH);
			return 1;
		case 0x00b7: /* Fn-F11: Keyboard config */
			map_key_clear(KEY_KEYBOARD);
			return 1;
		case 0x00b8: /* Fn-F12: User function */
			map_key_clear(KEY_PROG1);
			return 1;
		case 0x00b9: /* Fn-PrtSc: Snipping tool */
			map_key_clear(KEY_SELECTIVE_SCREENSHOT);
			return 1;
		case 0x00b5: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		}
	}

	if ((usage->hid & HID_USAGE_PAGE) == 0xffa00000) {
		switch (usage->hid & HID_USAGE) {
		case 0x00fb: /* Middle mouse (in native USB mode) */
			map_key_clear(BTN_MIDDLE);
			return 1;
		}
	}

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR &&
	    field->report->id == 21) {
		switch (usage->hid & HID_USAGE) {
		case 0x0004: /* Middle mouse (in native Bluetooth mode) */
			map_key_clear(BTN_MIDDLE);
			return 1;
		}
	}

	/* Compatibility middle/wheel mappings should be ignored */
	if (usage->hid == HID_GD_WHEEL)
		return -1;
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON &&
			(usage->hid & HID_USAGE) == 0x003)
		return -1;
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER &&
			(usage->hid & HID_USAGE) == 0x238)
		return -1;

	/* Map wheel emulation reports: 0xff10 */
	if ((usage->hid & HID_USAGE_PAGE) == 0xff100000) {
		field->flags |= HID_MAIN_ITEM_RELATIVE | HID_MAIN_ITEM_VARIABLE;
		field->logical_minimum = -127;
		field->logical_maximum = 127;

		switch (usage->hid & HID_USAGE) {
		case 0x0000:
			hid_map_usage(hi, usage, bit, max, EV_REL, REL_HWHEEL);
			return 1;
		case 0x0001:
			hid_map_usage(hi, usage, bit, max, EV_REL, REL_WHEEL);
			return 1;
		default:
			return -1;
		}
	}

	return 0;
}

static int lenovo_input_mapping_scrollpoint(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (usage->hid == HID_GD_Z) {
		hid_map_usage(hi, usage, bit, max, EV_REL, REL_HWHEEL);
		return 1;
	}
	return 0;
}

static int lenovo_input_mapping_tp10_ultrabook_kbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/*
	 * The ThinkPad 10 Ultrabook Keyboard uses 0x000c0001 usage for
	 * a bunch of keys which have no standard consumer page code.
	 */
	if (usage->hid == 0x000c0001) {
		switch (usage->usage_index) {
		case 8: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		case 9: /* Fn-F4: Mic mute */
			map_key_clear(LENOVO_KEY_MICMUTE);
			return 1;
		case 10: /* Fn-F7: Control panel */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 11: /* Fn-F8: Search (magnifier glass) */
			map_key_clear(KEY_SEARCH);
			return 1;
		case 12: /* Fn-F10: Open My computer (6 boxes) */
			map_key_clear(KEY_FILE);
			return 1;
		}
	}

	/*
	 * The Ultrabook Keyboard sends a spurious F23 key-press when resuming
	 * from suspend and it does not actually have a F23 key, ignore it.
	 */
	if (usage->hid == 0x00070072)
		return -1;

	return 0;
}

static int lenovo_input_mapping_x1_tab_kbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/*
	 * The ThinkPad X1 Tablet Thin Keyboard uses 0x000c0001 usage for
	 * a bunch of keys which have no standard consumer page code.
	 */
	if (usage->hid == 0x000c0001) {
		switch (usage->usage_index) {
		case 0: /* Fn-F10: Enable/disable bluetooth */
			map_key_clear(KEY_BLUETOOTH);
			return 1;
		case 1: /* Fn-F11: Keyboard settings */
			map_key_clear(KEY_KEYBOARD);
			return 1;
		case 2: /* Fn-F12: User function / Cortana */
			map_key_clear(KEY_MACRO1);
			return 1;
		case 3: /* Fn-PrtSc: Snipping tool */
			map_key_clear(KEY_SELECTIVE_SCREENSHOT);
			return 1;
		case 8: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		case 9: /* Fn-F4: Mute/unmute microphone */
			map_key_clear(KEY_MICMUTE);
			return 1;
		case 10: /* Fn-F9: Settings */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 13: /* Fn-F7: Manage external displays */
			map_key_clear(KEY_SWITCHVIDEOMODE);
			return 1;
		case 14: /* Fn-F8: Enable/disable wifi */
			map_key_clear(KEY_WLAN);
			return 1;
		}
	}

	if (usage->hid == (HID_UP_KEYBOARD | 0x009a)) {
		map_key_clear(KEY_SYSRQ);
		return 1;
	}

	return 0;
}

static int lenovo_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		return lenovo_input_mapping_tpkbd(hdev, hi, field,
							usage, bit, max);
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
		return lenovo_input_mapping_cptkbd(hdev, hi, field,
							usage, bit, max);
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		return lenovo_input_mapping_tpIIkbd(hdev, hi, field,
							usage, bit, max);
	case USB_DEVICE_ID_IBM_SCROLLPOINT_III:
	case USB_DEVICE_ID_IBM_SCROLLPOINT_PRO:
	case USB_DEVICE_ID_IBM_SCROLLPOINT_OPTICAL:
	case USB_DEVICE_ID_IBM_SCROLLPOINT_800DPI_OPTICAL:
	case USB_DEVICE_ID_IBM_SCROLLPOINT_800DPI_OPTICAL_PRO:
	case USB_DEVICE_ID_LENOVO_SCROLLPOINT_OPTICAL:
		return lenovo_input_mapping_scrollpoint(hdev, hi, field,
							usage, bit, max);
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
		return lenovo_input_mapping_tp10_ultrabook_kbd(hdev, hi, field,
							       usage, bit, max);
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		return lenovo_input_mapping_x1_tab_kbd(hdev, hi, field, usage, bit, max);
	default:
		return 0;
	}
}

#undef map_key_clear

/* Send a config command to the keyboard */
static int lenovo_send_cmd_cptkbd(struct hid_device *hdev,
			unsigned char byte2, unsigned char byte3)
{
	int ret;
	unsigned char *buf;

	buf = kzalloc(3, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * Feature report 0x13 is used for USB,
	 * output report 0x18 is used for Bluetooth.
	 * buf[0] is ignored by hid_hw_raw_request.
	 */
	buf[0] = 0x18;
	buf[1] = byte2;
	buf[2] = byte3;

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
		ret = hid_hw_raw_request(hdev, 0x13, buf, 3,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
		break;
	case USB_DEVICE_ID_LENOVO_CBTKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		ret = hid_hw_output_report(hdev, buf, 3);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	kfree(buf);

	return ret < 0 ? ret : 0; /* BT returns 0, USB returns sizeof(buf) */
}

static void lenovo_features_set_cptkbd(struct hid_device *hdev)
{
	int ret;
	struct lenovo_drvdata *cptkbd_data = hid_get_drvdata(hdev);

	ret = lenovo_send_cmd_cptkbd(hdev, 0x05, cptkbd_data->fn_lock);
	if (ret)
		hid_err(hdev, "Fn-lock setting failed: %d\n", ret);

	ret = lenovo_send_cmd_cptkbd(hdev, 0x02, cptkbd_data->sensitivity);
	if (ret)
		hid_err(hdev, "Sensitivity setting failed: %d\n", ret);
}

static ssize_t attr_fn_lock_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data->fn_lock);
}

static ssize_t attr_fn_lock_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);
	int value, ret;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data->fn_lock = !!value;

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		lenovo_features_set_cptkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		ret = lenovo_led_set_tp10ubkbd(hdev, TP10UBKBD_FN_LOCK_LED, value);
		if (ret)
			return ret;
		break;
	}

	return count;
}

static ssize_t attr_sensitivity_show_cptkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *cptkbd_data = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		cptkbd_data->sensitivity);
}

static ssize_t attr_sensitivity_store_cptkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *cptkbd_data = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	cptkbd_data->sensitivity = value;
	lenovo_features_set_cptkbd(hdev);

	return count;
}


static struct device_attribute dev_attr_fn_lock =
	__ATTR(fn_lock, S_IWUSR | S_IRUGO,
			attr_fn_lock_show,
			attr_fn_lock_store);

static struct device_attribute dev_attr_sensitivity_cptkbd =
	__ATTR(sensitivity, S_IWUSR | S_IRUGO,
			attr_sensitivity_show_cptkbd,
			attr_sensitivity_store_cptkbd);


static struct attribute *lenovo_attributes_cptkbd[] = {
	&dev_attr_fn_lock.attr,
	&dev_attr_sensitivity_cptkbd.attr,
	NULL
};

static const struct attribute_group lenovo_attr_group_cptkbd = {
	.attrs = lenovo_attributes_cptkbd,
};

static int lenovo_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data, int size)
{
	/*
	 * Compact USB keyboard's Fn-F12 report holds down many other keys, and
	 * its own key is outside the usage page range. Remove extra
	 * keypresses and remap to inside usage page.
	 */
	if (unlikely(hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& size == 3
			&& data[0] == 0x15
			&& data[1] == 0x94
			&& data[2] == 0x01)) {
		data[1] = 0x00;
		data[2] = 0x01;
	}

	return 0;
}

static int lenovo_event_tp10ubkbd(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);

	if (usage->type == EV_KEY && usage->code == KEY_FN_ESC && value == 1) {
		/*
		 * The user has toggled the Fn-lock state. Toggle our own
		 * cached value of it and sync our value to the keyboard to
		 * ensure things are in sync (the sycning should be a no-op).
		 */
		data->fn_lock = !data->fn_lock;
		schedule_work(&data->fn_lock_sync_work);
	}

	return 0;
}

static int lenovo_event_cptkbd(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct lenovo_drvdata *cptkbd_data = hid_get_drvdata(hdev);

	/* "wheel" scroll events */
	if (usage->type == EV_REL && (usage->code == REL_WHEEL ||
			usage->code == REL_HWHEEL)) {
		/* Scroll events disable middle-click event */
		cptkbd_data->middlebutton_state = 2;
		return 0;
	}

	/* Middle click events */
	if (usage->type == EV_KEY && usage->code == BTN_MIDDLE) {
		if (value == 1) {
			cptkbd_data->middlebutton_state = 1;
		} else if (value == 0) {
			if (cptkbd_data->middlebutton_state == 1) {
				/* No scrolling inbetween, send middle-click */
				input_event(field->hidinput->input,
					EV_KEY, BTN_MIDDLE, 1);
				input_sync(field->hidinput->input);
				input_event(field->hidinput->input,
					EV_KEY, BTN_MIDDLE, 0);
				input_sync(field->hidinput->input);
			}
			cptkbd_data->middlebutton_state = 0;
		}
		return 1;
	}

	if (usage->type == EV_KEY && usage->code == KEY_FN_ESC && value == 1) {
		/*
		 * The user has toggled the Fn-lock state. Toggle our own
		 * cached value of it and sync our value to the keyboard to
		 * ensure things are in sync (the syncing should be a no-op).
		 */
		cptkbd_data->fn_lock = !cptkbd_data->fn_lock;
	}

	return 0;
}

static int lenovo_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	if (!hid_get_drvdata(hdev))
		return 0;

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		return lenovo_event_cptkbd(hdev, field, usage, value);
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		return lenovo_event_tp10ubkbd(hdev, field, usage, value);
	default:
		return 0;
	}
}

static int lenovo_features_set_tpkbd(struct hid_device *hdev)
{
	struct hid_report *report;
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[4];

	report->field[0]->value[0]  = data_pointer->press_to_select   ? 0x01 : 0x02;
	report->field[0]->value[0] |= data_pointer->dragging          ? 0x04 : 0x08;
	report->field[0]->value[0] |= data_pointer->release_to_select ? 0x10 : 0x20;
	report->field[0]->value[0] |= data_pointer->select_right      ? 0x80 : 0x40;
	report->field[1]->value[0] = 0x03; // unknown setting, imitate windows driver
	report->field[2]->value[0] = data_pointer->sensitivity;
	report->field[3]->value[0] = data_pointer->press_speed;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
	return 0;
}

static ssize_t attr_press_to_select_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->press_to_select);
}

static ssize_t attr_press_to_select_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->press_to_select = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_dragging_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->dragging);
}

static ssize_t attr_dragging_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->dragging = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_release_to_select_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->release_to_select);
}

static ssize_t attr_release_to_select_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->release_to_select = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_select_right_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->select_right);
}

static ssize_t attr_select_right_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->select_right = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_sensitivity_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->sensitivity);
}

static ssize_t attr_sensitivity_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->sensitivity = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_press_speed_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->press_speed);
}

static ssize_t attr_press_speed_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->press_speed = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static struct device_attribute dev_attr_press_to_select_tpkbd =
	__ATTR(press_to_select, S_IWUSR | S_IRUGO,
			attr_press_to_select_show_tpkbd,
			attr_press_to_select_store_tpkbd);

static struct device_attribute dev_attr_dragging_tpkbd =
	__ATTR(dragging, S_IWUSR | S_IRUGO,
			attr_dragging_show_tpkbd,
			attr_dragging_store_tpkbd);

static struct device_attribute dev_attr_release_to_select_tpkbd =
	__ATTR(release_to_select, S_IWUSR | S_IRUGO,
			attr_release_to_select_show_tpkbd,
			attr_release_to_select_store_tpkbd);

static struct device_attribute dev_attr_select_right_tpkbd =
	__ATTR(select_right, S_IWUSR | S_IRUGO,
			attr_select_right_show_tpkbd,
			attr_select_right_store_tpkbd);

static struct device_attribute dev_attr_sensitivity_tpkbd =
	__ATTR(sensitivity, S_IWUSR | S_IRUGO,
			attr_sensitivity_show_tpkbd,
			attr_sensitivity_store_tpkbd);

static struct device_attribute dev_attr_press_speed_tpkbd =
	__ATTR(press_speed, S_IWUSR | S_IRUGO,
			attr_press_speed_show_tpkbd,
			attr_press_speed_store_tpkbd);

static struct attribute *lenovo_attributes_tpkbd[] = {
	&dev_attr_press_to_select_tpkbd.attr,
	&dev_attr_dragging_tpkbd.attr,
	&dev_attr_release_to_select_tpkbd.attr,
	&dev_attr_select_right_tpkbd.attr,
	&dev_attr_sensitivity_tpkbd.attr,
	&dev_attr_press_speed_tpkbd.attr,
	NULL
};

static const struct attribute_group lenovo_attr_group_tpkbd = {
	.attrs = lenovo_attributes_tpkbd,
};

static void lenovo_led_set_tpkbd(struct hid_device *hdev)
{
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	struct hid_report *report;

	report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[3];
	report->field[0]->value[0] = (data_pointer->led_state >> 0) & 1;
	report->field[0]->value[1] = (data_pointer->led_state >> 1) & 1;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static int lenovo_led_brightness_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = to_hid_device(dev);
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);
	static const u8 tp10ubkbd_led[] = { TP10UBKBD_MUTE_LED, TP10UBKBD_MICMUTE_LED };
	int led_nr = 0;
	int ret = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	if (value == LED_OFF)
		data_pointer->led_state &= ~(1 << led_nr);
	else
		data_pointer->led_state |= 1 << led_nr;

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		lenovo_led_set_tpkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		ret = lenovo_led_set_tp10ubkbd(hdev, tp10ubkbd_led[led_nr], value);
		break;
	}

	return ret;
}

static int lenovo_register_leds(struct hid_device *hdev)
{
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);
	size_t name_sz = strlen(dev_name(&hdev->dev)) + 16;
	char *name_mute, *name_micm;
	int ret;

	name_mute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	name_micm = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	if (name_mute == NULL || name_micm == NULL) {
		hid_err(hdev, "Could not allocate memory for led data\n");
		return -ENOMEM;
	}
	snprintf(name_mute, name_sz, "%s:amber:mute", dev_name(&hdev->dev));
	snprintf(name_micm, name_sz, "%s:amber:micmute", dev_name(&hdev->dev));

	data->led_mute.name = name_mute;
	data->led_mute.default_trigger = "audio-mute";
	data->led_mute.brightness_set_blocking = lenovo_led_brightness_set;
	data->led_mute.max_brightness = 1;
	data->led_mute.flags = LED_HW_PLUGGABLE;
	data->led_mute.dev = &hdev->dev;
	ret = led_classdev_register(&hdev->dev, &data->led_mute);
	if (ret < 0)
		return ret;

	data->led_micmute.name = name_micm;
	data->led_micmute.default_trigger = "audio-micmute";
	data->led_micmute.brightness_set_blocking = lenovo_led_brightness_set;
	data->led_micmute.max_brightness = 1;
	data->led_micmute.flags = LED_HW_PLUGGABLE;
	data->led_micmute.dev = &hdev->dev;
	ret = led_classdev_register(&hdev->dev, &data->led_micmute);
	if (ret < 0) {
		led_classdev_unregister(&data->led_mute);
		return ret;
	}

	return 0;
}

static int lenovo_probe_tpkbd(struct hid_device *hdev)
{
	struct lenovo_drvdata *data_pointer;
	int i, ret;

	/*
	 * Only register extra settings against subdevice where input_mapping
	 * set drvdata to 1, i.e. the trackpoint.
	 */
	if (!hid_get_drvdata(hdev))
		return 0;

	hid_set_drvdata(hdev, NULL);

	/* Validate required reports. */
	for (i = 0; i < 4; i++) {
		if (!hid_validate_values(hdev, HID_FEATURE_REPORT, 4, i, 1))
			return -ENODEV;
	}
	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 3, 0, 2))
		return -ENODEV;

	ret = sysfs_create_group(&hdev->dev.kobj, &lenovo_attr_group_tpkbd);
	if (ret)
		hid_warn(hdev, "Could not create sysfs group: %d\n", ret);

	data_pointer = devm_kzalloc(&hdev->dev,
				    sizeof(struct lenovo_drvdata),
				    GFP_KERNEL);
	if (data_pointer == NULL) {
		hid_err(hdev, "Could not allocate memory for driver data\n");
		ret = -ENOMEM;
		goto err;
	}

	// set same default values as windows driver
	data_pointer->sensitivity = 0xa0;
	data_pointer->press_speed = 0x38;

	hid_set_drvdata(hdev, data_pointer);

	ret = lenovo_register_leds(hdev);
	if (ret)
		goto err;

	lenovo_features_set_tpkbd(hdev);

	return 0;
err:
	sysfs_remove_group(&hdev->dev.kobj, &lenovo_attr_group_tpkbd);
	return ret;
}

static int lenovo_probe_cptkbd(struct hid_device *hdev)
{
	int ret;
	struct lenovo_drvdata *cptkbd_data;

	/* All the custom action happens on the USBMOUSE device for USB */
	if (((hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD) ||
	    (hdev->product == USB_DEVICE_ID_LENOVO_TPIIUSBKBD)) &&
	    hdev->type != HID_TYPE_USBMOUSE) {
		hid_dbg(hdev, "Ignoring keyboard half of device\n");
		return 0;
	}

	cptkbd_data = devm_kzalloc(&hdev->dev,
					sizeof(*cptkbd_data),
					GFP_KERNEL);
	if (cptkbd_data == NULL) {
		hid_err(hdev, "can't alloc keyboard descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, cptkbd_data);

	/*
	 * Tell the keyboard a driver understands it, and turn F7, F9, F11 into
	 * regular keys (Compact only)
	 */
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD ||
	    hdev->product == USB_DEVICE_ID_LENOVO_CBTKBD) {
		ret = lenovo_send_cmd_cptkbd(hdev, 0x01, 0x03);
		if (ret)
			hid_warn(hdev, "Failed to switch F7/9/11 mode: %d\n", ret);
	}

	/* Switch middle button to native mode */
	ret = lenovo_send_cmd_cptkbd(hdev, 0x09, 0x01);
	if (ret)
		hid_warn(hdev, "Failed to switch middle button: %d\n", ret);

	/* Set keyboard settings to known state */
	cptkbd_data->middlebutton_state = 0;
	cptkbd_data->fn_lock = true;
	cptkbd_data->sensitivity = 0x05;
	lenovo_features_set_cptkbd(hdev);

	ret = sysfs_create_group(&hdev->dev.kobj, &lenovo_attr_group_cptkbd);
	if (ret)
		hid_warn(hdev, "Could not create sysfs group: %d\n", ret);

	return 0;
}

static struct attribute *lenovo_attributes_tp10ubkbd[] = {
	&dev_attr_fn_lock.attr,
	NULL
};

static const struct attribute_group lenovo_attr_group_tp10ubkbd = {
	.attrs = lenovo_attributes_tp10ubkbd,
};

static int lenovo_probe_tp10ubkbd(struct hid_device *hdev)
{
	struct hid_report_enum *rep_enum;
	struct lenovo_drvdata *data;
	struct hid_report *rep;
	bool found;
	int ret;

	/*
	 * The LEDs and the Fn-lock functionality use output report 9,
	 * with an application of 0xffa0001, add the LEDs on the interface
	 * with this output report.
	 */
	found = false;
	rep_enum = &hdev->report_enum[HID_OUTPUT_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		if (rep->application == 0xffa00001)
			found = true;
	}
	if (!found)
		return 0;

	data = devm_kzalloc(&hdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->led_report_mutex);
	INIT_WORK(&data->fn_lock_sync_work, lenovo_tp10ubkbd_sync_fn_lock);
	data->hdev = hdev;

	hid_set_drvdata(hdev, data);

	/*
	 * The Thinkpad 10 ultrabook USB kbd dock's Fn-lock defaults to on.
	 * We cannot read the state, only set it, so we force it to on here
	 * (which should be a no-op) to make sure that our state matches the
	 * keyboard's FN-lock state. This is the same as what Windows does.
	 */
	data->fn_lock = true;
	lenovo_led_set_tp10ubkbd(hdev, TP10UBKBD_FN_LOCK_LED, data->fn_lock);

	ret = sysfs_create_group(&hdev->dev.kobj, &lenovo_attr_group_tp10ubkbd);
	if (ret)
		return ret;

	ret = lenovo_register_leds(hdev);
	if (ret)
		goto err;

	return 0;
err:
	sysfs_remove_group(&hdev->dev.kobj, &lenovo_attr_group_tp10ubkbd);
	return ret;
}

static int lenovo_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid_parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hid_hw_start failed\n");
		goto err;
	}

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		ret = lenovo_probe_tpkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		ret = lenovo_probe_cptkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		ret = lenovo_probe_tp10ubkbd(hdev);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret)
		goto err_hid;

	return 0;
err_hid:
	hid_hw_stop(hdev);
err:
	return ret;
}

static void lenovo_remove_tpkbd(struct hid_device *hdev)
{
	struct lenovo_drvdata *data_pointer = hid_get_drvdata(hdev);

	/*
	 * Only the trackpoint half of the keyboard has drvdata and stuff that
	 * needs unregistering.
	 */
	if (data_pointer == NULL)
		return;

	sysfs_remove_group(&hdev->dev.kobj,
			&lenovo_attr_group_tpkbd);

	led_classdev_unregister(&data_pointer->led_micmute);
	led_classdev_unregister(&data_pointer->led_mute);
}

static void lenovo_remove_cptkbd(struct hid_device *hdev)
{
	sysfs_remove_group(&hdev->dev.kobj,
			&lenovo_attr_group_cptkbd);
}

static void lenovo_remove_tp10ubkbd(struct hid_device *hdev)
{
	struct lenovo_drvdata *data = hid_get_drvdata(hdev);

	if (data == NULL)
		return;

	led_classdev_unregister(&data->led_micmute);
	led_classdev_unregister(&data->led_mute);

	sysfs_remove_group(&hdev->dev.kobj, &lenovo_attr_group_tp10ubkbd);
	cancel_work_sync(&data->fn_lock_sync_work);
}

static void lenovo_remove(struct hid_device *hdev)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		lenovo_remove_tpkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
	case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
	case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
		lenovo_remove_cptkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_TP10UBKBD:
	case USB_DEVICE_ID_LENOVO_X1_TAB:
		lenovo_remove_tp10ubkbd(hdev);
		break;
	}

	hid_hw_stop(hdev);
}

static int lenovo_input_configured(struct hid_device *hdev,
		struct hid_input *hi)
{
	switch (hdev->product) {
		case USB_DEVICE_ID_LENOVO_TPKBD:
		case USB_DEVICE_ID_LENOVO_CUSBKBD:
		case USB_DEVICE_ID_LENOVO_CBTKBD:
		case USB_DEVICE_ID_LENOVO_TPIIUSBKBD:
		case USB_DEVICE_ID_LENOVO_TPIIBTKBD:
			if (test_bit(EV_REL, hi->input->evbit)) {
				/* set only for trackpoint device */
				__set_bit(INPUT_PROP_POINTER, hi->input->propbit);
				__set_bit(INPUT_PROP_POINTING_STICK,
						hi->input->propbit);
			}
			break;
	}

	return 0;
}


static const struct hid_device_id lenovo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPKBD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CUSBKBD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPIIUSBKBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CBTKBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPIIBTKBD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPPRODOCK) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_IBM, USB_DEVICE_ID_IBM_SCROLLPOINT_III) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_IBM, USB_DEVICE_ID_IBM_SCROLLPOINT_PRO) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_IBM, USB_DEVICE_ID_IBM_SCROLLPOINT_OPTICAL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_IBM, USB_DEVICE_ID_IBM_SCROLLPOINT_800DPI_OPTICAL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_IBM, USB_DEVICE_ID_IBM_SCROLLPOINT_800DPI_OPTICAL_PRO) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_SCROLLPOINT_OPTICAL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TP10UBKBD) },
	/*
	 * Note bind to the HID_GROUP_GENERIC group, so that we only bind to the keyboard
	 * part, while letting hid-multitouch.c handle the touchpad and trackpoint.
	 */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_X1_TAB) },
	{ }
};

MODULE_DEVICE_TABLE(hid, lenovo_devices);

static struct hid_driver lenovo_driver = {
	.name = "lenovo",
	.id_table = lenovo_devices,
	.input_configured = lenovo_input_configured,
	.input_mapping = lenovo_input_mapping,
	.probe = lenovo_probe,
	.remove = lenovo_remove,
	.raw_event = lenovo_raw_event,
	.event = lenovo_event,
	.report_fixup = lenovo_report_fixup,
};
module_hid_driver(lenovo_driver);

MODULE_LICENSE("GPL");
