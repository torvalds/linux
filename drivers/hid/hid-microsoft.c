// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for some microsoft "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 */

/*
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define MS_HIDINPUT		BIT(0)
#define MS_ERGONOMY		BIT(1)
#define MS_PRESENTER		BIT(2)
#define MS_RDESC		BIT(3)
#define MS_NOGET		BIT(4)
#define MS_DUPLICATE_USAGES	BIT(5)
#define MS_SURFACE_DIAL		BIT(6)
#define MS_QUIRK_FF		BIT(7)

struct ms_data {
	unsigned long quirks;
	struct hid_device *hdev;
	struct work_struct ff_worker;
	__u8 strong;
	__u8 weak;
	void *output_report_dmabuf;
};

#define XB1S_FF_REPORT		3
#define ENABLE_WEAK		BIT(0)
#define ENABLE_STRONG		BIT(1)

enum {
	MAGNITUDE_STRONG = 2,
	MAGNITUDE_WEAK,
	MAGNITUDE_NUM
};

struct xb1s_ff_report {
	__u8	report_id;
	__u8	enable;
	__u8	magnitude[MAGNITUDE_NUM];
	__u8	duration_10ms;
	__u8	start_delay_10ms;
	__u8	loop_count;
} __packed;

static __u8 *ms_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct ms_data *ms = hid_get_drvdata(hdev);
	unsigned long quirks = ms->quirks;

	/*
	 * Microsoft Wireless Desktop Receiver (Model 1028) has
	 * 'Usage Min/Max' where it ought to have 'Physical Min/Max'
	 */
	if ((quirks & MS_RDESC) && *rsize == 571 && rdesc[557] == 0x19 &&
			rdesc[559] == 0x29) {
		hid_info(hdev, "fixing up Microsoft Wireless Receiver Model 1028 report descriptor\n");
		rdesc[557] = 0x35;
		rdesc[559] = 0x45;
	}
	return rdesc;
}

#define ms_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int ms_ergonomy_kb_quirk(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct input_dev *input = hi->input;

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
		switch (usage->hid & HID_USAGE) {
		/*
		 * Microsoft uses these 2 reserved usage ids for 2 keys on
		 * the MS office kb labelled "Office Home" and "Task Pane".
		 */
		case 0x29d:
			ms_map_key_clear(KEY_PROG1);
			return 1;
		case 0x29e:
			ms_map_key_clear(KEY_PROG2);
			return 1;
		}
		return 0;
	}

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0xfd06: ms_map_key_clear(KEY_CHAT);	break;
	case 0xfd07: ms_map_key_clear(KEY_PHONE);	break;
	case 0xff00:
		/* Special keypad keys */
		ms_map_key_clear(KEY_KPEQUAL);
		set_bit(KEY_KPLEFTPAREN, input->keybit);
		set_bit(KEY_KPRIGHTPAREN, input->keybit);
		break;
	case 0xff01:
		/* Scroll wheel */
		hid_map_usage_clear(hi, usage, bit, max, EV_REL, REL_WHEEL);
		break;
	case 0xff02:
		/*
		 * This byte contains a copy of the modifier keys byte of a
		 * standard hid keyboard report, as send by interface 0
		 * (this usage is found on interface 1).
		 *
		 * This byte only gets send when another key in the same report
		 * changes state, and as such is useless, ignore it.
		 */
		return -1;
	case 0xff05:
		set_bit(EV_REP, input->evbit);
		ms_map_key_clear(KEY_F13);
		set_bit(KEY_F14, input->keybit);
		set_bit(KEY_F15, input->keybit);
		set_bit(KEY_F16, input->keybit);
		set_bit(KEY_F17, input->keybit);
		set_bit(KEY_F18, input->keybit);
		break;
	default:
		return 0;
	}
	return 1;
}

static int ms_presenter_8k_quirk(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	case 0xfd08: ms_map_key_clear(KEY_FORWARD);	break;
	case 0xfd09: ms_map_key_clear(KEY_BACK);	break;
	case 0xfd0b: ms_map_key_clear(KEY_PLAYPAUSE);	break;
	case 0xfd0e: ms_map_key_clear(KEY_CLOSE);	break;
	case 0xfd0f: ms_map_key_clear(KEY_PLAY);	break;
	default:
		return 0;
	}
	return 1;
}

static int ms_surface_dial_quirk(struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	switch (usage->hid & HID_USAGE_PAGE) {
	case 0xff070000:
		/* fall-through */
	case HID_UP_DIGITIZER:
		/* ignore those axis */
		return -1;
	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			/* fall-through */
		case HID_GD_Y:
			/* fall-through */
		case HID_GD_RFKILL_BTN:
			/* ignore those axis */
			return -1;
		}
	}

	return 0;
}

static int ms_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct ms_data *ms = hid_get_drvdata(hdev);
	unsigned long quirks = ms->quirks;

	if (quirks & MS_ERGONOMY) {
		int ret = ms_ergonomy_kb_quirk(hi, usage, bit, max);
		if (ret)
			return ret;
	}

	if ((quirks & MS_PRESENTER) &&
			ms_presenter_8k_quirk(hi, usage, bit, max))
		return 1;

	if (quirks & MS_SURFACE_DIAL) {
		int ret = ms_surface_dial_quirk(hi, field, usage, bit, max);

		if (ret)
			return ret;
	}

	return 0;
}

static int ms_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct ms_data *ms = hid_get_drvdata(hdev);
	unsigned long quirks = ms->quirks;

	if (quirks & MS_DUPLICATE_USAGES)
		clear_bit(usage->code, *bit);

	return 0;
}

static int ms_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct ms_data *ms = hid_get_drvdata(hdev);
	unsigned long quirks = ms->quirks;
	struct input_dev *input;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type)
		return 0;

	input = field->hidinput->input;

	/* Handling MS keyboards special buttons */
	if (quirks & MS_ERGONOMY && usage->hid == (HID_UP_MSVENDOR | 0xff00)) {
		/* Special keypad keys */
		input_report_key(input, KEY_KPEQUAL, value & 0x01);
		input_report_key(input, KEY_KPLEFTPAREN, value & 0x02);
		input_report_key(input, KEY_KPRIGHTPAREN, value & 0x04);
		return 1;
	}

	if (quirks & MS_ERGONOMY && usage->hid == (HID_UP_MSVENDOR | 0xff01)) {
		/* Scroll wheel */
		int step = ((value & 0x60) >> 5) + 1;

		switch (value & 0x1f) {
		case 0x01:
			input_report_rel(input, REL_WHEEL, step);
			break;
		case 0x1f:
			input_report_rel(input, REL_WHEEL, -step);
			break;
		}
		return 1;
	}

	if (quirks & MS_ERGONOMY && usage->hid == (HID_UP_MSVENDOR | 0xff05)) {
		static unsigned int last_key = 0;
		unsigned int key = 0;
		switch (value) {
		case 0x01: key = KEY_F14; break;
		case 0x02: key = KEY_F15; break;
		case 0x04: key = KEY_F16; break;
		case 0x08: key = KEY_F17; break;
		case 0x10: key = KEY_F18; break;
		}
		if (key) {
			input_event(input, usage->type, key, 1);
			last_key = key;
		} else
			input_event(input, usage->type, last_key, 0);

		return 1;
	}

	return 0;
}

static void ms_ff_worker(struct work_struct *work)
{
	struct ms_data *ms = container_of(work, struct ms_data, ff_worker);
	struct hid_device *hdev = ms->hdev;
	struct xb1s_ff_report *r = ms->output_report_dmabuf;
	int ret;

	memset(r, 0, sizeof(*r));

	r->report_id = XB1S_FF_REPORT;
	r->enable = ENABLE_WEAK | ENABLE_STRONG;
	/*
	 * Specifying maximum duration and maximum loop count should
	 * cover maximum duration of a single effect, which is 65536
	 * ms
	 */
	r->duration_10ms = U8_MAX;
	r->loop_count = U8_MAX;
	r->magnitude[MAGNITUDE_STRONG] = ms->strong; /* left actuator */
	r->magnitude[MAGNITUDE_WEAK] = ms->weak;     /* right actuator */

	ret = hid_hw_output_report(hdev, (__u8 *)r, sizeof(*r));
	if (ret < 0)
		hid_warn(hdev, "failed to send FF report\n");
}

static int ms_play_effect(struct input_dev *dev, void *data,
			  struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct ms_data *ms = hid_get_drvdata(hid);

	if (effect->type != FF_RUMBLE)
		return 0;

	/*
	 * Magnitude is 0..100 so scale the 16-bit input here
	 */
	ms->strong = ((u32) effect->u.rumble.strong_magnitude * 100) / U16_MAX;
	ms->weak = ((u32) effect->u.rumble.weak_magnitude * 100) / U16_MAX;

	schedule_work(&ms->ff_worker);
	return 0;
}

static int ms_init_ff(struct hid_device *hdev)
{
	struct hid_input *hidinput;
	struct input_dev *input_dev;
	struct ms_data *ms = hid_get_drvdata(hdev);

	if (list_empty(&hdev->inputs)) {
		hid_err(hdev, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hdev->inputs.next, struct hid_input, list);
	input_dev = hidinput->input;

	if (!(ms->quirks & MS_QUIRK_FF))
		return 0;

	ms->hdev = hdev;
	INIT_WORK(&ms->ff_worker, ms_ff_worker);

	ms->output_report_dmabuf = devm_kzalloc(&hdev->dev,
						sizeof(struct xb1s_ff_report),
						GFP_KERNEL);
	if (ms->output_report_dmabuf == NULL)
		return -ENOMEM;

	input_set_capability(input_dev, EV_FF, FF_RUMBLE);
	return input_ff_create_memless(input_dev, NULL, ms_play_effect);
}

static void ms_remove_ff(struct hid_device *hdev)
{
	struct ms_data *ms = hid_get_drvdata(hdev);

	if (!(ms->quirks & MS_QUIRK_FF))
		return;

	cancel_work_sync(&ms->ff_worker);
}

static int ms_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	struct ms_data *ms;
	int ret;

	ms = devm_kzalloc(&hdev->dev, sizeof(*ms), GFP_KERNEL);
	if (ms == NULL)
		return -ENOMEM;

	ms->quirks = quirks;

	hid_set_drvdata(hdev, ms);

	if (quirks & MS_NOGET)
		hdev->quirks |= HID_QUIRK_NOGET;

	if (quirks & MS_SURFACE_DIAL)
		hdev->quirks |= HID_QUIRK_INPUT_PER_APP;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT | ((quirks & MS_HIDINPUT) ?
				HID_CONNECT_HIDINPUT_FORCE : 0));
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	ret = ms_init_ff(hdev);
	if (ret)
		hid_err(hdev, "could not initialize ff, continuing anyway");

	return 0;
err_free:
	return ret;
}

static void ms_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	ms_remove_ff(hdev);
}

static const struct hid_device_id ms_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_SIDEWINDER_GV),
		.driver_data = MS_HIDINPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_OFFICE_KB),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_NE4K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_NE4K_JP),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_NE7K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_LK6K),
		.driver_data = MS_ERGONOMY | MS_RDESC },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_PRESENTER_8K_USB),
		.driver_data = MS_PRESENTER },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_DIGITAL_MEDIA_3K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_DIGITAL_MEDIA_7K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_DIGITAL_MEDIA_600),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_DIGITAL_MEDIA_3KV1),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_WIRELESS_OPTICAL_DESKTOP_3_0),
		.driver_data = MS_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_COMFORT_MOUSE_4500),
		.driver_data = MS_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_POWER_COVER),
		.driver_data = MS_HIDINPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_COMFORT_KEYBOARD),
		.driver_data = MS_ERGONOMY},

	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_PRESENTER_8K_BT),
		.driver_data = MS_PRESENTER },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_MICROSOFT, 0x091B),
		.driver_data = MS_SURFACE_DIAL },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_XBOX_ONE_S_CONTROLLER),
		.driver_data = MS_QUIRK_FF },
	{ }
};
MODULE_DEVICE_TABLE(hid, ms_devices);

static struct hid_driver ms_driver = {
	.name = "microsoft",
	.id_table = ms_devices,
	.report_fixup = ms_report_fixup,
	.input_mapping = ms_input_mapping,
	.input_mapped = ms_input_mapped,
	.event = ms_event,
	.probe = ms_probe,
	.remove = ms_remove,
};
module_hid_driver(ms_driver);

MODULE_LICENSE("GPL");
