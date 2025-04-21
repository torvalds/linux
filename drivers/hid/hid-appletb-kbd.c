// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Touch Bar Keyboard Mode Driver
 *
 * Copyright (c) 2017-2018 Ronald Tschalär
 * Copyright (c) 2022-2023 Kerem Karabay <kekrby@gmail.com>
 * Copyright (c) 2024-2025 Aditya Garg <gargaditya08@live.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/sysfs.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/input/sparse-keymap.h>

#include "hid-ids.h"

#define APPLETB_KBD_MODE_ESC	0
#define APPLETB_KBD_MODE_FN	1
#define APPLETB_KBD_MODE_SPCL	2
#define APPLETB_KBD_MODE_OFF	3
#define APPLETB_KBD_MODE_MAX	APPLETB_KBD_MODE_OFF

#define APPLETB_DEVID_KEYBOARD	1
#define APPLETB_DEVID_TRACKPAD	2

#define HID_USAGE_MODE		0x00ff0004

static int appletb_tb_def_mode = APPLETB_KBD_MODE_SPCL;
module_param_named(mode, appletb_tb_def_mode, int, 0444);
MODULE_PARM_DESC(mode, "Default touchbar mode:\n"
			 "    0 - escape key only\n"
			 "    1 - function-keys\n"
			 "    [2] - special keys");

static bool appletb_tb_fn_toggle = true;
module_param_named(fntoggle, appletb_tb_fn_toggle, bool, 0644);
MODULE_PARM_DESC(fntoggle, "Switch between Fn and media controls on pressing Fn key");

static bool appletb_tb_autodim = true;
module_param_named(autodim, appletb_tb_autodim, bool, 0644);
MODULE_PARM_DESC(autodim, "Automatically dim and turn off the Touch Bar after some time");

static int appletb_tb_dim_timeout = 60;
module_param_named(dim_timeout, appletb_tb_dim_timeout, int, 0644);
MODULE_PARM_DESC(dim_timeout, "Dim timeout in sec");

static int appletb_tb_idle_timeout = 15;
module_param_named(idle_timeout, appletb_tb_idle_timeout, int, 0644);
MODULE_PARM_DESC(idle_timeout, "Idle timeout in sec");

struct appletb_kbd {
	struct hid_field *mode_field;
	struct input_handler inp_handler;
	struct input_handle kbd_handle;
	struct input_handle tpd_handle;
	struct backlight_device *backlight_dev;
	struct timer_list inactivity_timer;
	bool has_dimmed;
	bool has_turned_off;
	u8 saved_mode;
	u8 current_mode;
};

static const struct key_entry appletb_kbd_keymap[] = {
	{ KE_KEY, KEY_ESC, { KEY_ESC } },
	{ KE_KEY, KEY_F1,  { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, KEY_F2,  { KEY_BRIGHTNESSUP } },
	{ KE_KEY, KEY_F3,  { KEY_RESERVED } },
	{ KE_KEY, KEY_F4,  { KEY_RESERVED } },
	{ KE_KEY, KEY_F5,  { KEY_KBDILLUMDOWN } },
	{ KE_KEY, KEY_F6,  { KEY_KBDILLUMUP } },
	{ KE_KEY, KEY_F7,  { KEY_PREVIOUSSONG } },
	{ KE_KEY, KEY_F8,  { KEY_PLAYPAUSE } },
	{ KE_KEY, KEY_F9,  { KEY_NEXTSONG } },
	{ KE_KEY, KEY_F10, { KEY_MUTE } },
	{ KE_KEY, KEY_F11, { KEY_VOLUMEDOWN } },
	{ KE_KEY, KEY_F12, { KEY_VOLUMEUP } },
	{ KE_END, 0 }
};

static int appletb_kbd_set_mode(struct appletb_kbd *kbd, u8 mode)
{
	struct hid_report *report = kbd->mode_field->report;
	struct hid_device *hdev = report->device;
	int ret;

	ret = hid_hw_power(hdev, PM_HINT_FULLON);
	if (ret) {
		hid_err(hdev, "Device didn't resume (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	ret = hid_set_field(kbd->mode_field, 0, mode);
	if (ret) {
		hid_err(hdev, "Failed to set mode field to %u (%pe)\n", mode, ERR_PTR(ret));
		goto power_normal;
	}

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	kbd->current_mode = mode;

power_normal:
	hid_hw_power(hdev, PM_HINT_NORMAL);

	return ret;
}

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct appletb_kbd *kbd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", kbd->current_mode);
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t size)
{
	struct appletb_kbd *kbd = dev_get_drvdata(dev);
	u8 mode;
	int ret;

	ret = kstrtou8(buf, 0, &mode);
	if (ret)
		return ret;

	if (mode > APPLETB_KBD_MODE_MAX)
		return -EINVAL;

	ret = appletb_kbd_set_mode(kbd, mode);

	return ret < 0 ? ret : size;
}
static DEVICE_ATTR_RW(mode);

static struct attribute *appletb_kbd_attrs[] = {
	&dev_attr_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(appletb_kbd);

static int appletb_tb_key_to_slot(unsigned int code)
{
	switch (code) {
	case KEY_ESC:
		return 0;
	case KEY_F1 ... KEY_F10:
		return code - KEY_F1 + 1;
	case KEY_F11 ... KEY_F12:
		return code - KEY_F11 + 11;

	default:
		return -EINVAL;
	}
}

static void appletb_inactivity_timer(struct timer_list *t)
{
	struct appletb_kbd *kbd = from_timer(kbd, t, inactivity_timer);

	if (kbd->backlight_dev && appletb_tb_autodim) {
		if (!kbd->has_dimmed) {
			backlight_device_set_brightness(kbd->backlight_dev, 1);
			kbd->has_dimmed = true;
			mod_timer(&kbd->inactivity_timer, jiffies + msecs_to_jiffies(appletb_tb_idle_timeout * 1000));
		} else if (!kbd->has_turned_off) {
			backlight_device_set_brightness(kbd->backlight_dev, 0);
			kbd->has_turned_off = true;
		}
	}
}

static void reset_inactivity_timer(struct appletb_kbd *kbd)
{
	if (kbd->backlight_dev && appletb_tb_autodim) {
		if (kbd->has_dimmed || kbd->has_turned_off) {
			backlight_device_set_brightness(kbd->backlight_dev, 2);
			kbd->has_dimmed = false;
			kbd->has_turned_off = false;
		}
		mod_timer(&kbd->inactivity_timer, jiffies + msecs_to_jiffies(appletb_tb_dim_timeout * 1000));
	}
}

static int appletb_kbd_hid_event(struct hid_device *hdev, struct hid_field *field,
				      struct hid_usage *usage, __s32 value)
{
	struct appletb_kbd *kbd = hid_get_drvdata(hdev);
	struct key_entry *translation;
	struct input_dev *input;
	int slot;

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_KEYBOARD || usage->type != EV_KEY)
		return 0;

	input = field->hidinput->input;

	/*
	 * Skip non-touch-bar keys.
	 *
	 * Either the touch bar itself or usbhid generate a slew of key-down
	 * events for all the meta keys. None of which we're at all interested
	 * in.
	 */
	slot = appletb_tb_key_to_slot(usage->code);
	if (slot < 0)
		return 0;

	reset_inactivity_timer(kbd);

	translation = sparse_keymap_entry_from_scancode(input, usage->code);

	if (translation && kbd->current_mode == APPLETB_KBD_MODE_SPCL) {
		input_event(input, usage->type, translation->keycode, value);

		return 1;
	}

	return kbd->current_mode == APPLETB_KBD_MODE_OFF;
}

static void appletb_kbd_inp_event(struct input_handle *handle, unsigned int type,
			      unsigned int code, int value)
{
	struct appletb_kbd *kbd = handle->private;

	reset_inactivity_timer(kbd);

	if (type == EV_KEY && code == KEY_FN && appletb_tb_fn_toggle &&
		(kbd->current_mode == APPLETB_KBD_MODE_SPCL ||
		 kbd->current_mode == APPLETB_KBD_MODE_FN)) {
		if (value == 1) {
			kbd->saved_mode = kbd->current_mode;
			appletb_kbd_set_mode(kbd, kbd->current_mode == APPLETB_KBD_MODE_SPCL
						? APPLETB_KBD_MODE_FN : APPLETB_KBD_MODE_SPCL);
		} else if (value == 0) {
			if (kbd->saved_mode != kbd->current_mode)
				appletb_kbd_set_mode(kbd, kbd->saved_mode);
		}
	}
}

static int appletb_kbd_inp_connect(struct input_handler *handler,
			       struct input_dev *dev,
			       const struct input_device_id *id)
{
	struct appletb_kbd *kbd = handler->private;
	struct input_handle *handle;
	int rc;

	if (id->driver_info == APPLETB_DEVID_KEYBOARD) {
		handle = &kbd->kbd_handle;
		handle->name = "tbkbd";
	} else if (id->driver_info == APPLETB_DEVID_TRACKPAD) {
		handle = &kbd->tpd_handle;
		handle->name = "tbtpd";
	} else {
		return -ENOENT;
	}

	if (handle->dev)
		return -EEXIST;

	handle->open = 0;
	handle->dev = input_get_device(dev);
	handle->handler = handler;
	handle->private = kbd;

	rc = input_register_handle(handle);
	if (rc)
		goto err_free_dev;

	rc = input_open_device(handle);
	if (rc)
		goto err_unregister_handle;

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_dev:
	input_put_device(handle->dev);
	handle->dev = NULL;
	return rc;
}

static void appletb_kbd_inp_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);

	input_put_device(handle->dev);
	handle->dev = NULL;
}

static int appletb_kbd_input_configured(struct hid_device *hdev, struct hid_input *hidinput)
{
	int idx;
	struct input_dev *input = hidinput->input;

	/*
	 * Clear various input capabilities that are blindly set by the hid
	 * driver (usbkbd.c)
	 */
	memset(input->evbit, 0, sizeof(input->evbit));
	memset(input->keybit, 0, sizeof(input->keybit));
	memset(input->ledbit, 0, sizeof(input->ledbit));

	__set_bit(EV_REP, input->evbit);

	sparse_keymap_setup(input, appletb_kbd_keymap, NULL);

	for (idx = 0; appletb_kbd_keymap[idx].type != KE_END; idx++)
		input_set_capability(input, EV_KEY, appletb_kbd_keymap[idx].code);

	return 0;
}

static const struct input_device_id appletb_kbd_input_devices[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			INPUT_DEVICE_ID_MATCH_VENDOR |
			INPUT_DEVICE_ID_MATCH_KEYBIT,
		.bustype = BUS_USB,
		.vendor = USB_VENDOR_ID_APPLE,
		.keybit = { [BIT_WORD(KEY_FN)] = BIT_MASK(KEY_FN) },
		.driver_info = APPLETB_DEVID_KEYBOARD,
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			INPUT_DEVICE_ID_MATCH_VENDOR |
			INPUT_DEVICE_ID_MATCH_KEYBIT,
		.bustype = BUS_USB,
		.vendor = USB_VENDOR_ID_APPLE,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.driver_info = APPLETB_DEVID_TRACKPAD,
	},
	{ }
};

static bool appletb_kbd_match_internal_device(struct input_handler *handler,
					  struct input_dev *inp_dev)
{
	struct device *dev = &inp_dev->dev;

	/* in kernel: dev && !is_usb_device(dev) */
	while (dev && !(dev->type && dev->type->name &&
			!strcmp(dev->type->name, "usb_device")))
		dev = dev->parent;

	/*
	 * Apple labels all their internal keyboards and trackpads as such,
	 * instead of maintaining an ever expanding list of product-id's we
	 * just look at the device's product name.
	 */
	if (dev)
		return !!strstr(to_usb_device(dev)->product, "Internal Keyboard");

	return false;
}

static int appletb_kbd_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct appletb_kbd *kbd;
	struct device *dev = &hdev->dev;
	struct hid_field *mode_field;
	int ret;

	ret = hid_parse(hdev);
	if (ret)
		return dev_err_probe(dev, ret, "HID parse failed\n");

	mode_field = hid_find_field(hdev, HID_OUTPUT_REPORT,
				    HID_GD_KEYBOARD, HID_USAGE_MODE);
	if (!mode_field)
		return -ENODEV;

	kbd = devm_kzalloc(dev, sizeof(*kbd), GFP_KERNEL);
	if (!kbd)
		return -ENOMEM;

	kbd->mode_field = mode_field;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDINPUT);
	if (ret)
		return dev_err_probe(dev, ret, "HID hw start failed\n");

	ret = hid_hw_open(hdev);
	if (ret) {
		dev_err_probe(dev, ret, "HID hw open failed\n");
		goto stop_hw;
	}

	kbd->backlight_dev = backlight_device_get_by_name("appletb_backlight");
	if (!kbd->backlight_dev) {
		dev_err_probe(dev, -ENODEV, "Failed to get backlight device\n");
	} else {
		backlight_device_set_brightness(kbd->backlight_dev, 2);
		timer_setup(&kbd->inactivity_timer, appletb_inactivity_timer, 0);
		mod_timer(&kbd->inactivity_timer, jiffies + msecs_to_jiffies(appletb_tb_dim_timeout * 1000));
	}

	kbd->inp_handler.event = appletb_kbd_inp_event;
	kbd->inp_handler.connect = appletb_kbd_inp_connect;
	kbd->inp_handler.disconnect = appletb_kbd_inp_disconnect;
	kbd->inp_handler.name = "appletb";
	kbd->inp_handler.id_table = appletb_kbd_input_devices;
	kbd->inp_handler.match = appletb_kbd_match_internal_device;
	kbd->inp_handler.private = kbd;

	ret = input_register_handler(&kbd->inp_handler);
	if (ret) {
		dev_err_probe(dev, ret, "Unable to register keyboard handler\n");
		goto close_hw;
	}

	ret = appletb_kbd_set_mode(kbd, appletb_tb_def_mode);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to set touchbar mode\n");
		goto close_hw;
	}

	hid_set_drvdata(hdev, kbd);

	return 0;

close_hw:
	hid_hw_close(hdev);
stop_hw:
	hid_hw_stop(hdev);
	return ret;
}

static void appletb_kbd_remove(struct hid_device *hdev)
{
	struct appletb_kbd *kbd = hid_get_drvdata(hdev);

	appletb_kbd_set_mode(kbd, APPLETB_KBD_MODE_OFF);

	input_unregister_handler(&kbd->inp_handler);
	timer_delete_sync(&kbd->inactivity_timer);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

#ifdef CONFIG_PM
static int appletb_kbd_suspend(struct hid_device *hdev, pm_message_t msg)
{
	struct appletb_kbd *kbd = hid_get_drvdata(hdev);

	kbd->saved_mode = kbd->current_mode;
	appletb_kbd_set_mode(kbd, APPLETB_KBD_MODE_OFF);

	return 0;
}

static int appletb_kbd_reset_resume(struct hid_device *hdev)
{
	struct appletb_kbd *kbd = hid_get_drvdata(hdev);

	appletb_kbd_set_mode(kbd, kbd->saved_mode);

	return 0;
}
#endif

static const struct hid_device_id appletb_kbd_hid_ids[] = {
	/* MacBook Pro's 2018, 2019, with T2 chip: iBridge Display */
	{ HID_USB_DEVICE(USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_TOUCHBAR_DISPLAY) },
	{ }
};
MODULE_DEVICE_TABLE(hid, appletb_kbd_hid_ids);

static struct hid_driver appletb_kbd_hid_driver = {
	.name = "hid-appletb-kbd",
	.id_table = appletb_kbd_hid_ids,
	.probe = appletb_kbd_probe,
	.remove = appletb_kbd_remove,
	.event = appletb_kbd_hid_event,
	.input_configured = appletb_kbd_input_configured,
#ifdef CONFIG_PM
	.suspend = appletb_kbd_suspend,
	.reset_resume = appletb_kbd_reset_resume,
#endif
	.driver.dev_groups = appletb_kbd_groups,
};
module_hid_driver(appletb_kbd_hid_driver);

/* The backlight driver should be loaded before the keyboard driver is initialised */
MODULE_SOFTDEP("pre: hid_appletb_bl");

MODULE_AUTHOR("Ronald Tschalär");
MODULE_AUTHOR("Kerem Karabay <kekrby@gmail.com>");
MODULE_AUTHOR("Aditya Garg <gargaditya08@live.com>");
MODULE_DESCRIPTION("MacBook Pro Touch Bar Keyboard Mode driver");
MODULE_LICENSE("GPL");
