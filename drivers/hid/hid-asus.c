// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Asus notebook built-in keyboard.
 *  Fixes small logical maximum to match usage maximum.
 *
 *  Currently supported devices are:
 *    EeeBook X205TA
 *    VivoBook E200HA
 *
 *  Copyright (c) 2016 Yusuke Fujimaki <usk.fujimaki@gmail.com>
 *
 *  This module based on hid-ortek by
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
 *  Copyright (c) 2011 Jiri Kosina
 *
 *  This module has been updated to add support for Asus i2c touchpad.
 *
 *  Copyright (c) 2016 Brendan McGrath <redmcg@redmandi.dyndns.org>
 *  Copyright (c) 2016 Victor Vlasenko <victor.vlasenko@sysgears.com>
 *  Copyright (c) 2016 Frederik Wenigwieser <frederik.wenigwieser@gmail.com>
 */

/*
 */

#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/platform_data/x86/asus-wmi.h>
#include <linux/input/mt.h>
#include <linux/usb.h> /* For to_usb_interface for T100 touchpad intf check */
#include <linux/power_supply.h>
#include <linux/leds.h>

#include "hid-ids.h"

MODULE_AUTHOR("Yusuke Fujimaki <usk.fujimaki@gmail.com>");
MODULE_AUTHOR("Brendan McGrath <redmcg@redmandi.dyndns.org>");
MODULE_AUTHOR("Victor Vlasenko <victor.vlasenko@sysgears.com>");
MODULE_AUTHOR("Frederik Wenigwieser <frederik.wenigwieser@gmail.com>");
MODULE_DESCRIPTION("Asus HID Keyboard and TouchPad");

#define T100_TPAD_INTF 2
#define MEDION_E1239T_TPAD_INTF 1

#define E1239T_TP_TOGGLE_REPORT_ID 0x05
#define T100CHI_MOUSE_REPORT_ID 0x06
#define FEATURE_REPORT_ID 0x0d
#define INPUT_REPORT_ID 0x5d
#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 16
#define FEATURE_KBD_LED_REPORT_ID1 0x5d
#define FEATURE_KBD_LED_REPORT_ID2 0x5e

#define ROG_ALLY_REPORT_SIZE 64
#define ROG_ALLY_X_MIN_MCU 313
#define ROG_ALLY_MIN_MCU 319

#define SUPPORT_KBD_BACKLIGHT BIT(0)

#define MAX_TOUCH_MAJOR 8
#define MAX_PRESSURE 128

#define BTN_LEFT_MASK 0x01
#define CONTACT_TOOL_TYPE_MASK 0x80
#define CONTACT_X_MSB_MASK 0xf0
#define CONTACT_Y_MSB_MASK 0x0f
#define CONTACT_TOUCH_MAJOR_MASK 0x07
#define CONTACT_PRESSURE_MASK 0x7f

#define	BATTERY_REPORT_ID	(0x03)
#define	BATTERY_REPORT_SIZE	(1 + 8)
#define	BATTERY_LEVEL_MAX	((u8)255)
#define	BATTERY_STAT_DISCONNECT	(0)
#define	BATTERY_STAT_CHARGING	(1)
#define	BATTERY_STAT_FULL	(2)

#define QUIRK_FIX_NOTEBOOK_REPORT	BIT(0)
#define QUIRK_NO_INIT_REPORTS		BIT(1)
#define QUIRK_SKIP_INPUT_MAPPING	BIT(2)
#define QUIRK_IS_MULTITOUCH		BIT(3)
#define QUIRK_NO_CONSUMER_USAGES	BIT(4)
#define QUIRK_USE_KBD_BACKLIGHT		BIT(5)
#define QUIRK_T100_KEYBOARD		BIT(6)
#define QUIRK_T100CHI			BIT(7)
#define QUIRK_G752_KEYBOARD		BIT(8)
#define QUIRK_T90CHI			BIT(9)
#define QUIRK_MEDION_E1239T		BIT(10)
#define QUIRK_ROG_NKEY_KEYBOARD		BIT(11)
#define QUIRK_ROG_CLAYMORE_II_KEYBOARD BIT(12)
#define QUIRK_ROG_ALLY_XPAD		BIT(13)

#define I2C_KEYBOARD_QUIRKS			(QUIRK_FIX_NOTEBOOK_REPORT | \
						 QUIRK_NO_INIT_REPORTS | \
						 QUIRK_NO_CONSUMER_USAGES)
#define I2C_TOUCHPAD_QUIRKS			(QUIRK_NO_INIT_REPORTS | \
						 QUIRK_SKIP_INPUT_MAPPING | \
						 QUIRK_IS_MULTITOUCH)

#define TRKID_SGN       ((TRKID_MAX + 1) >> 1)

struct asus_kbd_leds {
	struct led_classdev cdev;
	struct hid_device *hdev;
	struct work_struct work;
	unsigned int brightness;
	spinlock_t lock;
	bool removed;
};

struct asus_touchpad_info {
	int max_x;
	int max_y;
	int res_x;
	int res_y;
	int contact_size;
	int max_contacts;
	int report_size;
};

struct asus_drvdata {
	unsigned long quirks;
	struct hid_device *hdev;
	struct input_dev *input;
	struct input_dev *tp_kbd_input;
	struct asus_kbd_leds *kbd_backlight;
	const struct asus_touchpad_info *tp;
	bool enable_backlight;
	struct power_supply *battery;
	struct power_supply_desc battery_desc;
	int battery_capacity;
	int battery_stat;
	bool battery_in_query;
	unsigned long battery_next_query;
};

static int asus_report_battery(struct asus_drvdata *, u8 *, int);

static const struct asus_touchpad_info asus_i2c_tp = {
	.max_x = 2794,
	.max_y = 1758,
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100ta_tp = {
	.max_x = 2240,
	.max_y = 1120,
	.res_x = 30, /* units/mm */
	.res_y = 27, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100ha_tp = {
	.max_x = 2640,
	.max_y = 1320,
	.res_x = 30, /* units/mm */
	.res_y = 29, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t200ta_tp = {
	.max_x = 3120,
	.max_y = 1716,
	.res_x = 30, /* units/mm */
	.res_y = 28, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 28 /* 2 byte header + 5 * 5 + 1 byte footer */,
};

static const struct asus_touchpad_info asus_t100chi_tp = {
	.max_x = 2640,
	.max_y = 1320,
	.res_x = 31, /* units/mm */
	.res_y = 29, /* units/mm */
	.contact_size = 3,
	.max_contacts = 4,
	.report_size = 15 /* 2 byte header + 3 * 4 + 1 byte footer */,
};

static const struct asus_touchpad_info medion_e1239t_tp = {
	.max_x = 2640,
	.max_y = 1380,
	.res_x = 29, /* units/mm */
	.res_y = 28, /* units/mm */
	.contact_size = 5,
	.max_contacts = 5,
	.report_size = 32 /* 2 byte header + 5 * 5 + 5 byte footer */,
};

static void asus_report_contact_down(struct asus_drvdata *drvdat,
		int toolType, u8 *data)
{
	struct input_dev *input = drvdat->input;
	int touch_major, pressure, x, y;

	x = (data[0] & CONTACT_X_MSB_MASK) << 4 | data[1];
	y = drvdat->tp->max_y - ((data[0] & CONTACT_Y_MSB_MASK) << 8 | data[2]);

	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);

	if (drvdat->tp->contact_size < 5)
		return;

	if (toolType == MT_TOOL_PALM) {
		touch_major = MAX_TOUCH_MAJOR;
		pressure = MAX_PRESSURE;
	} else {
		touch_major = (data[3] >> 4) & CONTACT_TOUCH_MAJOR_MASK;
		pressure = data[4] & CONTACT_PRESSURE_MASK;
	}

	input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
	input_report_abs(input, ABS_MT_PRESSURE, pressure);
}

/* Required for Synaptics Palm Detection */
static void asus_report_tool_width(struct asus_drvdata *drvdat)
{
	struct input_mt *mt = drvdat->input->mt;
	struct input_mt_slot *oldest;
	int oldid, i;

	if (drvdat->tp->contact_size < 5)
		return;

	oldest = NULL;
	oldid = mt->trkid;

	for (i = 0; i < mt->num_slots; ++i) {
		struct input_mt_slot *ps = &mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
	}

	if (oldest) {
		input_report_abs(drvdat->input, ABS_TOOL_WIDTH,
			input_mt_get_value(oldest, ABS_MT_TOUCH_MAJOR));
	}
}

static int asus_report_input(struct asus_drvdata *drvdat, u8 *data, int size)
{
	int i, toolType = MT_TOOL_FINGER;
	u8 *contactData = data + 2;

	if (size != drvdat->tp->report_size)
		return 0;

	for (i = 0; i < drvdat->tp->max_contacts; i++) {
		bool down = !!(data[1] & BIT(i+3));

		if (drvdat->tp->contact_size >= 5)
			toolType = contactData[3] & CONTACT_TOOL_TYPE_MASK ?
						MT_TOOL_PALM : MT_TOOL_FINGER;

		input_mt_slot(drvdat->input, i);
		input_mt_report_slot_state(drvdat->input, toolType, down);

		if (down) {
			asus_report_contact_down(drvdat, toolType, contactData);
			contactData += drvdat->tp->contact_size;
		}
	}

	input_report_key(drvdat->input, BTN_LEFT, data[1] & BTN_LEFT_MASK);
	asus_report_tool_width(drvdat);

	input_mt_sync_frame(drvdat->input);
	input_sync(drvdat->input);

	return 1;
}

static int asus_e1239t_event(struct asus_drvdata *drvdat, u8 *data, int size)
{
	if (size != 3)
		return 0;

	/* Handle broken mute key which only sends press events */
	if (!drvdat->tp &&
	    data[0] == 0x02 && data[1] == 0xe2 && data[2] == 0x00) {
		input_report_key(drvdat->input, KEY_MUTE, 1);
		input_sync(drvdat->input);
		input_report_key(drvdat->input, KEY_MUTE, 0);
		input_sync(drvdat->input);
		return 1;
	}

	/* Handle custom touchpad toggle key which only sends press events */
	if (drvdat->tp_kbd_input &&
	    data[0] == 0x05 && data[1] == 0x02 && data[2] == 0x28) {
		input_report_key(drvdat->tp_kbd_input, KEY_F21, 1);
		input_sync(drvdat->tp_kbd_input);
		input_report_key(drvdat->tp_kbd_input, KEY_F21, 0);
		input_sync(drvdat->tp_kbd_input);
		return 1;
	}

	return 0;
}

static int asus_event(struct hid_device *hdev, struct hid_field *field,
		      struct hid_usage *usage, __s32 value)
{
	if ((usage->hid & HID_USAGE_PAGE) == 0xff310000 &&
	    (usage->hid & HID_USAGE) != 0x00 &&
	    (usage->hid & HID_USAGE) != 0xff && !usage->type) {
		hid_warn(hdev, "Unmapped Asus vendor usagepage code 0x%02x\n",
			 usage->hid & HID_USAGE);
	}

	return 0;
}

static int asus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->battery && data[0] == BATTERY_REPORT_ID)
		return asus_report_battery(drvdata, data, size);

	if (drvdata->tp && data[0] == INPUT_REPORT_ID)
		return asus_report_input(drvdata, data, size);

	if (drvdata->quirks & QUIRK_MEDION_E1239T)
		return asus_e1239t_event(drvdata, data, size);

	/*
	 * Skip these report ID, the device emits a continuous stream associated
	 * with the AURA mode it is in which looks like an 'echo'.
	 */
	if (report->id == FEATURE_KBD_LED_REPORT_ID1 || report->id == FEATURE_KBD_LED_REPORT_ID2)
		return -1;
	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD) {
		/*
		 * G713 and G733 send these codes on some keypresses, depending on
		 * the key pressed it can trigger a shutdown event if not caught.
		*/
		if (data[0] == 0x02 && data[1] == 0x30) {
			return -1;
		}
	}

	if (drvdata->quirks & QUIRK_ROG_CLAYMORE_II_KEYBOARD) {
		/*
		 * CLAYMORE II keyboard sends this packet when it goes to sleep
		 * this causes the whole system to go into suspend.
		*/

		if(size == 2 && data[0] == 0x02 && data[1] == 0x00) {
			return -1;
		}
	}

	return 0;
}

static int asus_kbd_set_report(struct hid_device *hdev, const u8 *buf, size_t buf_size)
{
	unsigned char *dmabuf;
	int ret;

	dmabuf = kmemdup(buf, buf_size, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	/*
	 * The report ID should be set from the incoming buffer due to LED and key
	 * interfaces having different pages
	*/
	ret = hid_hw_raw_request(hdev, buf[0], dmabuf,
				 buf_size, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	kfree(dmabuf);

	return ret;
}

static int asus_kbd_init(struct hid_device *hdev, u8 report_id)
{
	const u8 buf[] = { report_id, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
		     0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0)
		hid_err(hdev, "Asus failed to send init command: %d\n", ret);

	return ret;
}

static int asus_kbd_get_functions(struct hid_device *hdev,
				  unsigned char *kbd_func,
				  u8 report_id)
{
	const u8 buf[] = { report_id, 0x05, 0x20, 0x31, 0x00, 0x08 };
	u8 *readbuf;
	int ret;

	ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
	if (ret < 0) {
		hid_err(hdev, "Asus failed to send configuration command: %d\n", ret);
		return ret;
	}

	readbuf = kzalloc(FEATURE_KBD_REPORT_SIZE, GFP_KERNEL);
	if (!readbuf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, FEATURE_KBD_REPORT_ID, readbuf,
				 FEATURE_KBD_REPORT_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		hid_err(hdev, "Asus failed to request functions: %d\n", ret);
		kfree(readbuf);
		return ret;
	}

	*kbd_func = readbuf[6];

	kfree(readbuf);
	return ret;
}

static int asus_kbd_disable_oobe(struct hid_device *hdev)
{
	const u8 init[][6] = {
		{ FEATURE_KBD_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08 },
		{ FEATURE_KBD_REPORT_ID, 0xBA, 0xC5, 0xC4 },
		{ FEATURE_KBD_REPORT_ID, 0xD0, 0x8F, 0x01 },
		{ FEATURE_KBD_REPORT_ID, 0xD0, 0x85, 0xFF }
	};
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(init); i++) {
		ret = asus_kbd_set_report(hdev, init[i], sizeof(init[i]));
		if (ret < 0)
			return ret;
	}

	hid_info(hdev, "Disabled OOBE for keyboard\n");
	return 0;
}

static void asus_schedule_work(struct asus_kbd_leds *led)
{
	unsigned long flags;

	spin_lock_irqsave(&led->lock, flags);
	if (!led->removed)
		schedule_work(&led->work);
	spin_unlock_irqrestore(&led->lock, flags);
}

static void asus_kbd_backlight_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct asus_kbd_leds *led = container_of(led_cdev, struct asus_kbd_leds,
						 cdev);
	unsigned long flags;

	spin_lock_irqsave(&led->lock, flags);
	led->brightness = brightness;
	spin_unlock_irqrestore(&led->lock, flags);

	asus_schedule_work(led);
}

static enum led_brightness asus_kbd_backlight_get(struct led_classdev *led_cdev)
{
	struct asus_kbd_leds *led = container_of(led_cdev, struct asus_kbd_leds,
						 cdev);
	enum led_brightness brightness;
	unsigned long flags;

	spin_lock_irqsave(&led->lock, flags);
	brightness = led->brightness;
	spin_unlock_irqrestore(&led->lock, flags);

	return brightness;
}

static void asus_kbd_backlight_work(struct work_struct *work)
{
	struct asus_kbd_leds *led = container_of(work, struct asus_kbd_leds, work);
	u8 buf[] = { FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4, 0x00 };
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&led->lock, flags);
	buf[4] = led->brightness;
	spin_unlock_irqrestore(&led->lock, flags);

	ret = asus_kbd_set_report(led->hdev, buf, sizeof(buf));
	if (ret < 0)
		hid_err(led->hdev, "Asus failed to set keyboard backlight: %d\n", ret);
}

/* WMI-based keyboard backlight LED control (via asus-wmi driver) takes
 * precedence. We only activate HID-based backlight control when the
 * WMI control is not available.
 */
static bool asus_kbd_wmi_led_control_present(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	u32 value;
	int ret;

	if (!IS_ENABLED(CONFIG_ASUS_WMI))
		return false;

	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD &&
			dmi_check_system(asus_use_hid_led_dmi_ids)) {
		hid_info(hdev, "using HID for asus::kbd_backlight\n");
		return false;
	}

	ret = asus_wmi_evaluate_method(ASUS_WMI_METHODID_DSTS,
				       ASUS_WMI_DEVID_KBD_BACKLIGHT, 0, &value);
	hid_dbg(hdev, "WMI backlight check: rc %d value %x", ret, value);
	if (ret)
		return false;

	return !!(value & ASUS_WMI_DSTS_PRESENCE_BIT);
}

/*
 * We don't care about any other part of the string except the version section.
 * Example strings: FGA80100.RC72LA.312_T01, FGA80100.RC71LS.318_T01
 * The bytes "5a 05 03 31 00 1a 13" and possibly more come before the version
 * string, and there may be additional bytes after the version string such as
 * "75 00 74 00 65 00" or a postfix such as "_T01"
 */
static int mcu_parse_version_string(const u8 *response, size_t response_size)
{
	const u8 *end = response + response_size;
	const u8 *p = response;
	int dots, err, version;
	char buf[4];

	dots = 0;
	while (p < end && dots < 2) {
		if (*p++ == '.')
			dots++;
	}

	if (dots != 2 || p >= end || (p + 3) >= end)
		return -EINVAL;

	memcpy(buf, p, 3);
	buf[3] = '\0';

	err = kstrtoint(buf, 10, &version);
	if (err || version < 0)
		return -EINVAL;

	return version;
}

static int mcu_request_version(struct hid_device *hdev)
{
	u8 *response __free(kfree) = kzalloc(ROG_ALLY_REPORT_SIZE, GFP_KERNEL);
	const u8 request[] = { 0x5a, 0x05, 0x03, 0x31, 0x00, 0x20 };
	int ret;

	if (!response)
		return -ENOMEM;

	ret = asus_kbd_set_report(hdev, request, sizeof(request));
	if (ret < 0)
		return ret;

	ret = hid_hw_raw_request(hdev, FEATURE_REPORT_ID, response,
				ROG_ALLY_REPORT_SIZE, HID_FEATURE_REPORT,
				HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	ret = mcu_parse_version_string(response, ROG_ALLY_REPORT_SIZE);
	if (ret < 0) {
		pr_err("Failed to parse MCU version: %d\n", ret);
		print_hex_dump(KERN_ERR, "MCU: ", DUMP_PREFIX_NONE,
			      16, 1, response, ROG_ALLY_REPORT_SIZE, false);
	}

	return ret;
}

static void validate_mcu_fw_version(struct hid_device *hdev, int idProduct)
{
	int min_version, version;

	version = mcu_request_version(hdev);
	if (version < 0)
		return;

	switch (idProduct) {
	case USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY:
		min_version = ROG_ALLY_MIN_MCU;
		break;
	case USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY_X:
		min_version = ROG_ALLY_X_MIN_MCU;
		break;
	default:
		min_version = 0;
	}

	if (version < min_version) {
		hid_warn(hdev,
			"The MCU firmware version must be %d or greater to avoid issues with suspend.\n",
			min_version);
	} else {
		set_ally_mcu_hack(ASUS_WMI_ALLY_MCU_HACK_DISABLED);
		set_ally_mcu_powersave(true);
	}
}

static int asus_kbd_register_leds(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	struct usb_interface *intf;
	struct usb_device *udev;
	unsigned char kbd_func;
	int ret;

	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD) {
		/* Initialize keyboard */
		ret = asus_kbd_init(hdev, FEATURE_KBD_REPORT_ID);
		if (ret < 0)
			return ret;

		/* The LED endpoint is initialised in two HID */
		ret = asus_kbd_init(hdev, FEATURE_KBD_LED_REPORT_ID1);
		if (ret < 0)
			return ret;

		ret = asus_kbd_init(hdev, FEATURE_KBD_LED_REPORT_ID2);
		if (ret < 0)
			return ret;

		if (dmi_match(DMI_PRODUCT_FAMILY, "ProArt P16")) {
			ret = asus_kbd_disable_oobe(hdev);
			if (ret < 0)
				return ret;
		}

		if (drvdata->quirks & QUIRK_ROG_ALLY_XPAD) {
			intf = to_usb_interface(hdev->dev.parent);
			udev = interface_to_usbdev(intf);
			validate_mcu_fw_version(hdev,
				le16_to_cpu(udev->descriptor.idProduct));
		}

	} else {
		/* Initialize keyboard */
		ret = asus_kbd_init(hdev, FEATURE_KBD_REPORT_ID);
		if (ret < 0)
			return ret;

		/* Get keyboard functions */
		ret = asus_kbd_get_functions(hdev, &kbd_func, FEATURE_KBD_REPORT_ID);
		if (ret < 0)
			return ret;

		/* Check for backlight support */
		if (!(kbd_func & SUPPORT_KBD_BACKLIGHT))
			return -ENODEV;
	}

	drvdata->kbd_backlight = devm_kzalloc(&hdev->dev,
					      sizeof(struct asus_kbd_leds),
					      GFP_KERNEL);
	if (!drvdata->kbd_backlight)
		return -ENOMEM;

	drvdata->kbd_backlight->removed = false;
	drvdata->kbd_backlight->brightness = 0;
	drvdata->kbd_backlight->hdev = hdev;
	drvdata->kbd_backlight->cdev.name = "asus::kbd_backlight";
	drvdata->kbd_backlight->cdev.max_brightness = 3;
	drvdata->kbd_backlight->cdev.brightness_set = asus_kbd_backlight_set;
	drvdata->kbd_backlight->cdev.brightness_get = asus_kbd_backlight_get;
	INIT_WORK(&drvdata->kbd_backlight->work, asus_kbd_backlight_work);
	spin_lock_init(&drvdata->kbd_backlight->lock);

	ret = devm_led_classdev_register(&hdev->dev, &drvdata->kbd_backlight->cdev);
	if (ret < 0) {
		/* No need to have this still around */
		devm_kfree(&hdev->dev, drvdata->kbd_backlight);
	}

	return ret;
}

/*
 * [0]       REPORT_ID (same value defined in report descriptor)
 * [1]	     rest battery level. range [0..255]
 * [2]..[7]  Bluetooth hardware address (MAC address)
 * [8]       charging status
 *            = 0 : AC offline / discharging
 *            = 1 : AC online  / charging
 *            = 2 : AC online  / fully charged
 */
static int asus_parse_battery(struct asus_drvdata *drvdata, u8 *data, int size)
{
	u8 sts;
	u8 lvl;
	int val;

	lvl = data[1];
	sts = data[8];

	drvdata->battery_capacity = ((int)lvl * 100) / (int)BATTERY_LEVEL_MAX;

	switch (sts) {
	case BATTERY_STAT_CHARGING:
		val = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case BATTERY_STAT_FULL:
		val = POWER_SUPPLY_STATUS_FULL;
		break;
	case BATTERY_STAT_DISCONNECT:
	default:
		val = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	}
	drvdata->battery_stat = val;

	return 0;
}

static int asus_report_battery(struct asus_drvdata *drvdata, u8 *data, int size)
{
	/* notify only the autonomous event by device */
	if ((drvdata->battery_in_query == false) &&
			 (size == BATTERY_REPORT_SIZE))
		power_supply_changed(drvdata->battery);

	return 0;
}

static int asus_battery_query(struct asus_drvdata *drvdata)
{
	u8 *buf;
	int ret = 0;

	buf = kmalloc(BATTERY_REPORT_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	drvdata->battery_in_query = true;
	ret = hid_hw_raw_request(drvdata->hdev, BATTERY_REPORT_ID,
				buf, BATTERY_REPORT_SIZE,
				HID_INPUT_REPORT, HID_REQ_GET_REPORT);
	drvdata->battery_in_query = false;
	if (ret == BATTERY_REPORT_SIZE)
		ret = asus_parse_battery(drvdata, buf, BATTERY_REPORT_SIZE);
	else
		ret = -ENODATA;

	kfree(buf);

	return ret;
}

static enum power_supply_property asus_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

#define	QUERY_MIN_INTERVAL	(60 * HZ)	/* 60[sec] */

static int asus_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct asus_drvdata *drvdata = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CAPACITY:
		if (time_before(drvdata->battery_next_query, jiffies)) {
			drvdata->battery_next_query =
					 jiffies + QUERY_MIN_INTERVAL;
			ret = asus_battery_query(drvdata);
			if (ret)
				return ret;
		}
		if (psp == POWER_SUPPLY_PROP_STATUS)
			val->intval = drvdata->battery_stat;
		else
			val->intval = drvdata->battery_capacity;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = drvdata->hdev->name;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int asus_battery_probe(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	struct power_supply_config pscfg = { .drv_data = drvdata };
	int ret = 0;

	drvdata->battery_capacity = 0;
	drvdata->battery_stat = POWER_SUPPLY_STATUS_UNKNOWN;
	drvdata->battery_in_query = false;

	drvdata->battery_desc.properties = asus_battery_props;
	drvdata->battery_desc.num_properties = ARRAY_SIZE(asus_battery_props);
	drvdata->battery_desc.get_property = asus_battery_get_property;
	drvdata->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	drvdata->battery_desc.use_for_apm = 0;
	drvdata->battery_desc.name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
					"asus-keyboard-%s-battery",
					strlen(hdev->uniq) ?
					hdev->uniq : dev_name(&hdev->dev));
	if (!drvdata->battery_desc.name)
		return -ENOMEM;

	drvdata->battery_next_query = jiffies;

	drvdata->battery = devm_power_supply_register(&hdev->dev,
				&(drvdata->battery_desc), &pscfg);
	if (IS_ERR(drvdata->battery)) {
		ret = PTR_ERR(drvdata->battery);
		drvdata->battery = NULL;
		hid_err(hdev, "Unable to register battery device\n");
		return ret;
	}

	power_supply_powers(drvdata->battery, &hdev->dev);

	return ret;
}

static int asus_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct input_dev *input = hi->input;
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	/* T100CHI uses MULTI_INPUT, bind the touchpad to the mouse hid_input */
	if (drvdata->quirks & QUIRK_T100CHI &&
	    hi->report->id != T100CHI_MOUSE_REPORT_ID)
		return 0;

	/* Handle MULTI_INPUT on E1239T mouse/touchpad USB interface */
	if (drvdata->tp && (drvdata->quirks & QUIRK_MEDION_E1239T)) {
		switch (hi->report->id) {
		case E1239T_TP_TOGGLE_REPORT_ID:
			input_set_capability(input, EV_KEY, KEY_F21);
			input->name = "Asus Touchpad Keys";
			drvdata->tp_kbd_input = input;
			return 0;
		case INPUT_REPORT_ID:
			break; /* Touchpad report, handled below */
		default:
			return 0; /* Ignore other reports */
		}
	}

	if (drvdata->tp) {
		int ret;

		input_set_abs_params(input, ABS_MT_POSITION_X, 0,
				     drvdata->tp->max_x, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
				     drvdata->tp->max_y, 0, 0);
		input_abs_set_res(input, ABS_MT_POSITION_X, drvdata->tp->res_x);
		input_abs_set_res(input, ABS_MT_POSITION_Y, drvdata->tp->res_y);

		if (drvdata->tp->contact_size >= 5) {
			input_set_abs_params(input, ABS_TOOL_WIDTH, 0,
					     MAX_TOUCH_MAJOR, 0, 0);
			input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0,
					     MAX_TOUCH_MAJOR, 0, 0);
			input_set_abs_params(input, ABS_MT_PRESSURE, 0,
					      MAX_PRESSURE, 0, 0);
		}

		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

		ret = input_mt_init_slots(input, drvdata->tp->max_contacts,
					  INPUT_MT_POINTER);

		if (ret) {
			hid_err(hdev, "Asus input mt init slots failed: %d\n", ret);
			return ret;
		}
	}

	drvdata->input = input;

	if (drvdata->enable_backlight &&
	    !asus_kbd_wmi_led_control_present(hdev) &&
	    asus_kbd_register_leds(hdev))
		hid_warn(hdev, "Failed to initialize backlight.\n");

	return 0;
}

#define asus_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, \
						    max, EV_KEY, (c))
static int asus_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit,
		int *max)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_SKIP_INPUT_MAPPING) {
		/* Don't map anything from the HID report.
		 * We do it all manually in asus_input_configured
		 */
		return -1;
	}

	/*
	 * Ignore a bunch of bogus collections in the T100CHI descriptor.
	 * This avoids a bunch of non-functional hid_input devices getting
	 * created because of the T100CHI using HID_QUIRK_MULTI_INPUT.
	 */
	if ((drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) &&
	    (field->application == (HID_UP_GENDESK | 0x0080) ||
	     field->application == HID_GD_MOUSE ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0024) ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0025) ||
	     usage->hid == (HID_UP_GENDEVCTRLS | 0x0026)))
		return -1;

	/* ASUS-specific keyboard hotkeys and led backlight */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_ASUSVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0x10: asus_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x20: asus_map_key_clear(KEY_BRIGHTNESSUP);		break;
		case 0x35: asus_map_key_clear(KEY_DISPLAY_OFF);		break;
		case 0x6c: asus_map_key_clear(KEY_SLEEP);		break;
		case 0x7c: asus_map_key_clear(KEY_MICMUTE);		break;
		case 0x82: asus_map_key_clear(KEY_CAMERA);		break;
		case 0x88: asus_map_key_clear(KEY_RFKILL);			break;
		case 0xb5: asus_map_key_clear(KEY_CALC);			break;
		case 0xc4: asus_map_key_clear(KEY_KBDILLUMUP);		break;
		case 0xc5: asus_map_key_clear(KEY_KBDILLUMDOWN);		break;
		case 0xc7: asus_map_key_clear(KEY_KBDILLUMTOGGLE);	break;
		case 0x4e: asus_map_key_clear(KEY_FN_ESC);		break;
		case 0x7e: asus_map_key_clear(KEY_EMOJI_PICKER);	break;

		case 0x8b: asus_map_key_clear(KEY_PROG1);	break; /* ProArt Creator Hub key */
		case 0x6b: asus_map_key_clear(KEY_F21);		break; /* ASUS touchpad toggle */
		case 0x38: asus_map_key_clear(KEY_PROG1);	break; /* ROG key */
		case 0xba: asus_map_key_clear(KEY_PROG2);	break; /* Fn+C ASUS Splendid */
		case 0x5c: asus_map_key_clear(KEY_PROG3);	break; /* Fn+Space Power4Gear */
		case 0x99: asus_map_key_clear(KEY_PROG4);	break; /* Fn+F5 "fan" symbol */
		case 0xae: asus_map_key_clear(KEY_PROG4);	break; /* Fn+F5 "fan" symbol */
		case 0x92: asus_map_key_clear(KEY_CALC);	break; /* Fn+Ret "Calc" symbol */
		case 0xb2: asus_map_key_clear(KEY_PROG2);	break; /* Fn+Left previous aura */
		case 0xb3: asus_map_key_clear(KEY_PROG3);	break; /* Fn+Left next aura */
		case 0x6a: asus_map_key_clear(KEY_F13);		break; /* Screenpad toggle */
		case 0x4b: asus_map_key_clear(KEY_F14);		break; /* Arrows/Pg-Up/Dn toggle */
		case 0xa5: asus_map_key_clear(KEY_F15);		break; /* ROG Ally left back */
		case 0xa6: asus_map_key_clear(KEY_F16);		break; /* ROG Ally QAM button */
		case 0xa7: asus_map_key_clear(KEY_F17);		break; /* ROG Ally ROG long-press */
		case 0xa8: asus_map_key_clear(KEY_F18);		break; /* ROG Ally ROG long-press-release */

		default:
			/* ASUS lazily declares 256 usages, ignore the rest,
			 * as some make the keyboard appear as a pointer device. */
			return -1;
		}

		/*
		 * Check and enable backlight only on devices with UsagePage ==
		 * 0xff31 to avoid initializing the keyboard firmware multiple
		 * times on devices with multiple HID descriptors but same
		 * PID/VID.
		 */
		if (drvdata->quirks & QUIRK_USE_KBD_BACKLIGHT)
			drvdata->enable_backlight = true;

		set_bit(EV_REP, hi->input->evbit);
		return 1;
	}

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR) {
		switch (usage->hid & HID_USAGE) {
		case 0xff01: asus_map_key_clear(BTN_1);	break;
		case 0xff02: asus_map_key_clear(BTN_2);	break;
		case 0xff03: asus_map_key_clear(BTN_3);	break;
		case 0xff04: asus_map_key_clear(BTN_4);	break;
		case 0xff05: asus_map_key_clear(BTN_5);	break;
		case 0xff06: asus_map_key_clear(BTN_6);	break;
		case 0xff07: asus_map_key_clear(BTN_7);	break;
		case 0xff08: asus_map_key_clear(BTN_8);	break;
		case 0xff09: asus_map_key_clear(BTN_9);	break;
		case 0xff0a: asus_map_key_clear(BTN_A);	break;
		case 0xff0b: asus_map_key_clear(BTN_B);	break;
		case 0x00f1: asus_map_key_clear(KEY_WLAN);	break;
		case 0x00f2: asus_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x00f3: asus_map_key_clear(KEY_BRIGHTNESSUP);	break;
		case 0x00f4: asus_map_key_clear(KEY_DISPLAY_OFF);	break;
		case 0x00f7: asus_map_key_clear(KEY_CAMERA);	break;
		case 0x00f8: asus_map_key_clear(KEY_PROG1);	break;
		default:
			return 0;
		}

		set_bit(EV_REP, hi->input->evbit);
		return 1;
	}

	if (drvdata->quirks & QUIRK_NO_CONSUMER_USAGES &&
		(usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
		switch (usage->hid & HID_USAGE) {
		case 0xe2: /* Mute */
		case 0xe9: /* Volume up */
		case 0xea: /* Volume down */
			return 0;
		default:
			/* Ignore dummy Consumer usages which make the
			 * keyboard incorrectly appear as a pointer device.
			 */
			return -1;
		}
	}

	/*
	 * The mute button is broken and only sends press events, we
	 * deal with this in our raw_event handler, so do not map it.
	 */
	if ((drvdata->quirks & QUIRK_MEDION_E1239T) &&
	    usage->hid == (HID_UP_CONSUMER | 0xe2)) {
		input_set_capability(hi->input, EV_KEY, KEY_MUTE);
		return -1;
	}

	return 0;
}

static int asus_start_multitouch(struct hid_device *hdev)
{
	int ret;
	static const unsigned char buf[] = {
		FEATURE_REPORT_ID, 0x00, 0x03, 0x01, 0x00
	};
	unsigned char *dmabuf = kmemdup(buf, sizeof(buf), GFP_KERNEL);

	if (!dmabuf) {
		ret = -ENOMEM;
		hid_err(hdev, "Asus failed to alloc dma buf: %d\n", ret);
		return ret;
	}

	ret = hid_hw_raw_request(hdev, dmabuf[0], dmabuf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	kfree(dmabuf);

	if (ret != sizeof(buf)) {
		hid_err(hdev, "Asus failed to start multitouch: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused asus_resume(struct hid_device *hdev) {
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	int ret = 0;

	if (drvdata->kbd_backlight) {
		const u8 buf[] = { FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4,
				drvdata->kbd_backlight->cdev.brightness };
		ret = asus_kbd_set_report(hdev, buf, sizeof(buf));
		if (ret < 0) {
			hid_err(hdev, "Asus failed to set keyboard backlight: %d\n", ret);
			goto asus_resume_err;
		}
	}

asus_resume_err:
	return ret;
}

static int __maybe_unused asus_reset_resume(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->tp)
		return asus_start_multitouch(hdev);

	return 0;
}

static int asus_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct asus_drvdata *drvdata;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		hid_err(hdev, "Can't alloc Asus descriptor\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, drvdata);

	drvdata->quirks = id->driver_data;

	/*
	 * T90CHI's keyboard dock returns same ID values as T100CHI's dock.
	 * Thus, identify T90CHI dock with product name string.
	 */
	if (strstr(hdev->name, "T90CHI")) {
		drvdata->quirks &= ~QUIRK_T100CHI;
		drvdata->quirks |= QUIRK_T90CHI;
	}

	if (drvdata->quirks & QUIRK_IS_MULTITOUCH)
		drvdata->tp = &asus_i2c_tp;

	if ((drvdata->quirks & QUIRK_T100_KEYBOARD) && hid_is_usb(hdev)) {
		struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

		if (intf->altsetting->desc.bInterfaceNumber == T100_TPAD_INTF) {
			drvdata->quirks = QUIRK_SKIP_INPUT_MAPPING;
			/*
			 * The T100HA uses the same USB-ids as the T100TAF and
			 * the T200TA uses the same USB-ids as the T100TA, while
			 * both have different max x/y values as the T100TA[F].
			 */
			if (dmi_match(DMI_PRODUCT_NAME, "T100HAN"))
				drvdata->tp = &asus_t100ha_tp;
			else if (dmi_match(DMI_PRODUCT_NAME, "T200TA"))
				drvdata->tp = &asus_t200ta_tp;
			else
				drvdata->tp = &asus_t100ta_tp;
		}
	}

	if (drvdata->quirks & QUIRK_T100CHI) {
		/*
		 * All functionality is on a single HID interface and for
		 * userspace the touchpad must be a separate input_dev.
		 */
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;
		drvdata->tp = &asus_t100chi_tp;
	}

	if ((drvdata->quirks & QUIRK_MEDION_E1239T) && hid_is_usb(hdev)) {
		struct usb_host_interface *alt =
			to_usb_interface(hdev->dev.parent)->altsetting;

		if (alt->desc.bInterfaceNumber == MEDION_E1239T_TPAD_INTF) {
			/* For separate input-devs for tp and tp toggle key */
			hdev->quirks |= HID_QUIRK_MULTI_INPUT;
			drvdata->quirks |= QUIRK_SKIP_INPUT_MAPPING;
			drvdata->tp = &medion_e1239t_tp;
		}
	}

	if (drvdata->quirks & QUIRK_NO_INIT_REPORTS)
		hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	drvdata->hdev = hdev;

	if (drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) {
		ret = asus_battery_probe(hdev);
		if (ret) {
			hid_err(hdev,
			    "Asus hid battery_probe failed: %d\n", ret);
			return ret;
		}
	}

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Asus hid parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "Asus hw start failed: %d\n", ret);
		return ret;
	}

	/*
	 * Check that input registration succeeded. Checking that
	 * HID_CLAIMED_INPUT is set prevents a UAF when all input devices
	 * were freed during registration due to no usages being mapped,
	 * leaving drvdata->input pointing to freed memory.
	 */
	if (!drvdata->input || !(hdev->claimed & HID_CLAIMED_INPUT)) {
		hid_err(hdev, "Asus input not registered\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}

	if (drvdata->tp) {
		drvdata->input->name = "Asus TouchPad";
	} else {
		drvdata->input->name = "Asus Keyboard";
	}

	if (drvdata->tp) {
		ret = asus_start_multitouch(hdev);
		if (ret)
			goto err_stop_hw;
	}

	return 0;
err_stop_hw:
	hid_hw_stop(hdev);
	return ret;
}

static void asus_remove(struct hid_device *hdev)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);
	unsigned long flags;

	if (drvdata->kbd_backlight) {
		spin_lock_irqsave(&drvdata->kbd_backlight->lock, flags);
		drvdata->kbd_backlight->removed = true;
		spin_unlock_irqrestore(&drvdata->kbd_backlight->lock, flags);

		cancel_work_sync(&drvdata->kbd_backlight->work);
	}

	hid_hw_stop(hdev);
}

static const __u8 asus_g752_fixed_rdesc[] = {
        0x19, 0x00,			/*   Usage Minimum (0x00)       */
        0x2A, 0xFF, 0x00,		/*   Usage Maximum (0xFF)       */
};

static const __u8 *asus_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct asus_drvdata *drvdata = hid_get_drvdata(hdev);

	if (drvdata->quirks & QUIRK_FIX_NOTEBOOK_REPORT &&
			*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x65) {
		hid_info(hdev, "Fixing up Asus notebook report descriptor\n");
		rdesc[55] = 0xdd;
	}
	/* For the T100TA/T200TA keyboard dock */
	if (drvdata->quirks & QUIRK_T100_KEYBOARD &&
		 (*rsize == 76 || *rsize == 101) &&
		 rdesc[73] == 0x81 && rdesc[74] == 0x01) {
		hid_info(hdev, "Fixing up Asus T100 keyb report descriptor\n");
		rdesc[74] &= ~HID_MAIN_ITEM_CONSTANT;
	}
	/* For the T100CHI/T90CHI keyboard dock */
	if (drvdata->quirks & (QUIRK_T100CHI | QUIRK_T90CHI)) {
		int rsize_orig;
		int offs;

		if (drvdata->quirks & QUIRK_T100CHI) {
			rsize_orig = 403;
			offs = 388;
		} else {
			rsize_orig = 306;
			offs = 291;
		}

		/*
		 * Change Usage (76h) to Usage Minimum (00h), Usage Maximum
		 * (FFh) and clear the flags in the Input() byte.
		 * Note the descriptor has a bogus 0 byte at the end so we
		 * only need 1 extra byte.
		 */
		if (*rsize == rsize_orig &&
			rdesc[offs] == 0x09 && rdesc[offs + 1] == 0x76) {
			*rsize = rsize_orig + 1;
			rdesc = kmemdup(rdesc, *rsize, GFP_KERNEL);
			if (!rdesc)
				return NULL;

			hid_info(hdev, "Fixing up %s keyb report descriptor\n",
				drvdata->quirks & QUIRK_T100CHI ?
				"T100CHI" : "T90CHI");
			memmove(rdesc + offs + 4, rdesc + offs + 2, 12);
			rdesc[offs] = 0x19;
			rdesc[offs + 1] = 0x00;
			rdesc[offs + 2] = 0x29;
			rdesc[offs + 3] = 0xff;
			rdesc[offs + 14] = 0x00;
		}
	}

	if (drvdata->quirks & QUIRK_G752_KEYBOARD &&
		 *rsize == 75 && rdesc[61] == 0x15 && rdesc[62] == 0x00) {
		/* report is missing usage minimum and maximum */
		__u8 *new_rdesc;
		size_t new_size = *rsize + sizeof(asus_g752_fixed_rdesc);

		new_rdesc = devm_kzalloc(&hdev->dev, new_size, GFP_KERNEL);
		if (new_rdesc == NULL)
			return rdesc;

		hid_info(hdev, "Fixing up Asus G752 keyb report descriptor\n");
		/* copy the valid part */
		memcpy(new_rdesc, rdesc, 61);
		/* insert missing part */
		memcpy(new_rdesc + 61, asus_g752_fixed_rdesc, sizeof(asus_g752_fixed_rdesc));
		/* copy remaining data */
		memcpy(new_rdesc + 61 + sizeof(asus_g752_fixed_rdesc), rdesc + 61, *rsize - 61);

		*rsize = new_size;
		rdesc = new_rdesc;
	}

	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD &&
			*rsize == 331 && rdesc[190] == 0x85 && rdesc[191] == 0x5a &&
			rdesc[204] == 0x95 && rdesc[205] == 0x05) {
		hid_info(hdev, "Fixing up Asus N-KEY keyb report descriptor\n");
		rdesc[205] = 0x01;
	}

	/* match many more n-key devices */
	if (drvdata->quirks & QUIRK_ROG_NKEY_KEYBOARD && *rsize > 15) {
		for (int i = 0; i < *rsize - 15; i++) {
			/* offset to the count from 0x5a report part always 14 */
			if (rdesc[i] == 0x85 && rdesc[i + 1] == 0x5a &&
			    rdesc[i + 14] == 0x95 && rdesc[i + 15] == 0x05) {
				hid_info(hdev, "Fixing up Asus N-Key report descriptor\n");
				rdesc[i + 15] = 0x01;
				break;
			}
		}
	}

	return rdesc;
}

static const struct hid_device_id asus_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_I2C_KEYBOARD), I2C_KEYBOARD_QUIRKS},
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_I2C_TOUCHPAD), I2C_TOUCHPAD_QUIRKS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD1), QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD2), QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_ROG_KEYBOARD3), QUIRK_G752_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_FX503VD_KEYBOARD),
	  QUIRK_USE_KBD_BACKLIGHT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_KEYBOARD2),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_Z13_LIGHTBAR),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_ROG_ALLY_XPAD},
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_NKEY_ALLY_X),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD | QUIRK_ROG_ALLY_XPAD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
	    USB_DEVICE_ID_ASUSTEK_ROG_CLAYMORE_II_KEYBOARD),
	  QUIRK_ROG_CLAYMORE_II_KEYBOARD },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100TA_KEYBOARD),
	  QUIRK_T100_KEYBOARD | QUIRK_NO_CONSUMER_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100TAF_KEYBOARD),
	  QUIRK_T100_KEYBOARD | QUIRK_NO_CONSUMER_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_CHICONY, USB_DEVICE_ID_ASUS_AK1D) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_TURBOX, USB_DEVICE_ID_ASUS_MD_5110) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_JESS, USB_DEVICE_ID_ASUS_MD_5112) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_ASUSTEK,
		USB_DEVICE_ID_ASUSTEK_T100CHI_KEYBOARD), QUIRK_T100CHI },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ITE, USB_DEVICE_ID_ITE_MEDION_E1239T),
		QUIRK_MEDION_E1239T },
	/*
	 * Note bind to the HID_GROUP_GENERIC group, so that we only bind to the keyboard
	 * part, while letting hid-multitouch.c handle the touchpad.
	 */
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_ROG_Z13_FOLIO),
	  QUIRK_USE_KBD_BACKLIGHT | QUIRK_ROG_NKEY_KEYBOARD },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_T101HA_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, asus_devices);

static struct hid_driver asus_driver = {
	.name			= "asus",
	.id_table		= asus_devices,
	.report_fixup		= asus_report_fixup,
	.probe                  = asus_probe,
	.remove			= asus_remove,
	.input_mapping          = asus_input_mapping,
	.input_configured       = asus_input_configured,
#ifdef CONFIG_PM
	.reset_resume           = asus_reset_resume,
	.resume					= asus_resume,
#endif
	.event			= asus_event,
	.raw_event		= asus_raw_event
};
module_hid_driver(asus_driver);

MODULE_IMPORT_NS("ASUS_WMI");
MODULE_LICENSE("GPL");
