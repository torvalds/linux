// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for Google Hammer device.
 *
 *  Copyright (c) 2017 Google Inc.
 *  Author: Wei-Ning Huang <wnhuang@google.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/acpi.h>
#include <linux/hid.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <asm/unaligned.h>

#include "hid-ids.h"

/*
 * C(hrome)B(ase)A(ttached)S(witch) - switch exported by Chrome EC and reporting
 * state of the "Whiskers" base - attached or detached. Whiskers USB device also
 * reports position of the keyboard - folded or not. Combining base state and
 * position allows us to generate proper "Tablet mode" events.
 */
struct cbas_ec {
	struct device *dev;	/* The platform device (EC) */
	struct input_dev *input;
	bool base_present;
	bool base_folded;
	struct notifier_block notifier;
};

static struct cbas_ec cbas_ec;
static DEFINE_SPINLOCK(cbas_ec_lock);
static DEFINE_MUTEX(cbas_ec_reglock);

static bool cbas_parse_base_state(const void *data)
{
	u32 switches = get_unaligned_le32(data);

	return !!(switches & BIT(EC_MKBP_BASE_ATTACHED));
}

static int cbas_ec_query_base(struct cros_ec_device *ec_dev, bool get_state,
				  bool *state)
{
	struct ec_params_mkbp_info *params;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + max(sizeof(u32), sizeof(*params)),
		      GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_MKBP_INFO;
	msg->version = 1;
	msg->outsize = sizeof(*params);
	msg->insize = sizeof(u32);
	params = (struct ec_params_mkbp_info *)msg->data;
	params->info_type = get_state ?
		EC_MKBP_INFO_CURRENT : EC_MKBP_INFO_SUPPORTED;
	params->event_type = EC_MKBP_EVENT_SWITCH;

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret >= 0) {
		if (ret != sizeof(u32)) {
			dev_warn(ec_dev->dev, "wrong result size: %d != %zu\n",
				 ret, sizeof(u32));
			ret = -EPROTO;
		} else {
			*state = cbas_parse_base_state(msg->data);
			ret = 0;
		}
	}

	kfree(msg);

	return ret;
}

static int cbas_ec_notify(struct notifier_block *nb,
			      unsigned long queued_during_suspend,
			      void *_notify)
{
	struct cros_ec_device *ec = _notify;
	unsigned long flags;
	bool base_present;

	if (ec->event_data.event_type == EC_MKBP_EVENT_SWITCH) {
		base_present = cbas_parse_base_state(
					&ec->event_data.data.switches);
		dev_dbg(cbas_ec.dev,
			"%s: base: %d\n", __func__, base_present);

		if (device_may_wakeup(cbas_ec.dev) ||
		    !queued_during_suspend) {

			pm_wakeup_event(cbas_ec.dev, 0);

			spin_lock_irqsave(&cbas_ec_lock, flags);

			/*
			 * While input layer dedupes the events, we do not want
			 * to disrupt the state reported by the base by
			 * overriding it with state reported by the LID. Only
			 * report changes, as we assume that on attach the base
			 * is not folded.
			 */
			if (base_present != cbas_ec.base_present) {
				input_report_switch(cbas_ec.input,
						    SW_TABLET_MODE,
						    !base_present);
				input_sync(cbas_ec.input);
				cbas_ec.base_present = base_present;
			}

			spin_unlock_irqrestore(&cbas_ec_lock, flags);
		}
	}

	return NOTIFY_OK;
}

static __maybe_unused int cbas_ec_resume(struct device *dev)
{
	struct cros_ec_device *ec = dev_get_drvdata(dev->parent);
	bool base_present;
	int error;

	error = cbas_ec_query_base(ec, true, &base_present);
	if (error) {
		dev_warn(dev, "failed to fetch base state on resume: %d\n",
			 error);
	} else {
		spin_lock_irq(&cbas_ec_lock);

		cbas_ec.base_present = base_present;

		/*
		 * Only report if base is disconnected. If base is connected,
		 * it will resend its state on resume, and we'll update it
		 * in hammer_event().
		 */
		if (!cbas_ec.base_present) {
			input_report_switch(cbas_ec.input, SW_TABLET_MODE, 1);
			input_sync(cbas_ec.input);
		}

		spin_unlock_irq(&cbas_ec_lock);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cbas_ec_pm_ops, NULL, cbas_ec_resume);

static void cbas_ec_set_input(struct input_dev *input)
{
	/* Take the lock so hammer_event() does not race with us here */
	spin_lock_irq(&cbas_ec_lock);
	cbas_ec.input = input;
	spin_unlock_irq(&cbas_ec_lock);
}

static int __cbas_ec_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct input_dev *input;
	bool base_supported;
	int error;

	error = cbas_ec_query_base(ec, false, &base_supported);
	if (error)
		return error;

	if (!base_supported)
		return -ENXIO;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "Whiskers Tablet Mode Switch";
	input->id.bustype = BUS_HOST;

	input_set_capability(input, EV_SW, SW_TABLET_MODE);

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "cannot register input device: %d\n",
			error);
		return error;
	}

	/* Seed the state */
	error = cbas_ec_query_base(ec, true, &cbas_ec.base_present);
	if (error) {
		dev_err(&pdev->dev, "cannot query base state: %d\n", error);
		return error;
	}

	if (!cbas_ec.base_present)
		cbas_ec.base_folded = false;

	dev_dbg(&pdev->dev, "%s: base: %d, folded: %d\n", __func__,
		cbas_ec.base_present, cbas_ec.base_folded);

	input_report_switch(input, SW_TABLET_MODE,
			    !cbas_ec.base_present || cbas_ec.base_folded);

	cbas_ec_set_input(input);

	cbas_ec.dev = &pdev->dev;
	cbas_ec.notifier.notifier_call = cbas_ec_notify;
	error = blocking_notifier_chain_register(&ec->event_notifier,
						 &cbas_ec.notifier);
	if (error) {
		dev_err(&pdev->dev, "cannot register notifier: %d\n", error);
		cbas_ec_set_input(NULL);
		return error;
	}

	device_init_wakeup(&pdev->dev, true);
	return 0;
}

static int cbas_ec_probe(struct platform_device *pdev)
{
	int retval;

	mutex_lock(&cbas_ec_reglock);

	if (cbas_ec.input) {
		retval = -EBUSY;
		goto out;
	}

	retval = __cbas_ec_probe(pdev);

out:
	mutex_unlock(&cbas_ec_reglock);
	return retval;
}

static int cbas_ec_remove(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);

	mutex_lock(&cbas_ec_reglock);

	blocking_notifier_chain_unregister(&ec->event_notifier,
					   &cbas_ec.notifier);
	cbas_ec_set_input(NULL);

	mutex_unlock(&cbas_ec_reglock);
	return 0;
}

static const struct acpi_device_id cbas_ec_acpi_ids[] = {
	{ "GOOG000B", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cbas_ec_acpi_ids);

static struct platform_driver cbas_ec_driver = {
	.probe = cbas_ec_probe,
	.remove = cbas_ec_remove,
	.driver = {
		.name = "cbas_ec",
		.acpi_match_table = ACPI_PTR(cbas_ec_acpi_ids),
		.pm = &cbas_ec_pm_ops,
	},
};

#define MAX_BRIGHTNESS 100

struct hammer_kbd_leds {
	struct led_classdev cdev;
	struct hid_device *hdev;
	u8 buf[2] ____cacheline_aligned;
};

static int hammer_kbd_brightness_set_blocking(struct led_classdev *cdev,
		enum led_brightness br)
{
	struct hammer_kbd_leds *led = container_of(cdev,
						   struct hammer_kbd_leds,
						   cdev);
	int ret;

	led->buf[0] = 0;
	led->buf[1] = br;

	/*
	 * Request USB HID device to be in Full On mode, so that sending
	 * hardware output report and hardware raw request won't fail.
	 */
	ret = hid_hw_power(led->hdev, PM_HINT_FULLON);
	if (ret < 0) {
		hid_err(led->hdev, "failed: device not resumed %d\n", ret);
		return ret;
	}

	ret = hid_hw_output_report(led->hdev, led->buf, sizeof(led->buf));
	if (ret == -ENOSYS)
		ret = hid_hw_raw_request(led->hdev, 0, led->buf,
					 sizeof(led->buf),
					 HID_OUTPUT_REPORT,
					 HID_REQ_SET_REPORT);
	if (ret < 0)
		hid_err(led->hdev, "failed to set keyboard backlight: %d\n",
			ret);

	/* Request USB HID device back to Normal Mode. */
	hid_hw_power(led->hdev, PM_HINT_NORMAL);

	return ret;
}

static int hammer_register_leds(struct hid_device *hdev)
{
	struct hammer_kbd_leds *kbd_backlight;
	int error;

	kbd_backlight = kzalloc(sizeof(*kbd_backlight), GFP_KERNEL);
	if (!kbd_backlight)
		return -ENOMEM;

	kbd_backlight->hdev = hdev;
	kbd_backlight->cdev.name = "hammer::kbd_backlight";
	kbd_backlight->cdev.max_brightness = MAX_BRIGHTNESS;
	kbd_backlight->cdev.brightness_set_blocking =
		hammer_kbd_brightness_set_blocking;
	kbd_backlight->cdev.flags = LED_HW_PLUGGABLE;

	/* Set backlight to 0% initially. */
	hammer_kbd_brightness_set_blocking(&kbd_backlight->cdev, 0);

	error = led_classdev_register(&hdev->dev, &kbd_backlight->cdev);
	if (error)
		goto err_free_mem;

	hid_set_drvdata(hdev, kbd_backlight);
	return 0;

err_free_mem:
	kfree(kbd_backlight);
	return error;
}

static void hammer_unregister_leds(struct hid_device *hdev)
{
	struct hammer_kbd_leds *kbd_backlight = hid_get_drvdata(hdev);

	if (kbd_backlight) {
		led_classdev_unregister(&kbd_backlight->cdev);
		kfree(kbd_backlight);
	}
}

#define HID_UP_GOOGLEVENDOR	0xffd10000
#define HID_VD_KBD_FOLDED	0x00000019
#define HID_USAGE_KBD_FOLDED	(HID_UP_GOOGLEVENDOR | HID_VD_KBD_FOLDED)

/* HID usage for keyboard backlight (Alphanumeric display brightness) */
#define HID_AD_BRIGHTNESS	0x00140046

static int hammer_input_mapping(struct hid_device *hdev, struct hid_input *hi,
				struct hid_field *field,
				struct hid_usage *usage,
				unsigned long **bit, int *max)
{
	if (usage->hid == HID_USAGE_KBD_FOLDED) {
		/*
		 * We do not want to have this usage mapped as it will get
		 * mixed in with "base attached" signal and delivered over
		 * separate input device for tablet switch mode.
		 */
		return -1;
	}

	return 0;
}

static void hammer_folded_event(struct hid_device *hdev, bool folded)
{
	unsigned long flags;

	spin_lock_irqsave(&cbas_ec_lock, flags);

	/*
	 * If we are getting events from Whiskers that means that it
	 * is attached to the lid.
	 */
	cbas_ec.base_present = true;
	cbas_ec.base_folded = folded;
	hid_dbg(hdev, "%s: base: %d, folded: %d\n", __func__,
		cbas_ec.base_present, cbas_ec.base_folded);

	if (cbas_ec.input) {
		input_report_switch(cbas_ec.input, SW_TABLET_MODE, folded);
		input_sync(cbas_ec.input);
	}

	spin_unlock_irqrestore(&cbas_ec_lock, flags);
}

static int hammer_event(struct hid_device *hid, struct hid_field *field,
			struct hid_usage *usage, __s32 value)
{
	if (usage->hid == HID_USAGE_KBD_FOLDED) {
		hammer_folded_event(hid, value);
		return 1; /* We handled this event */
	}

	return 0;
}

static bool hammer_has_usage(struct hid_device *hdev, unsigned int report_type,
			unsigned application, unsigned usage)
{
	struct hid_report_enum *re = &hdev->report_enum[report_type];
	struct hid_report *report;
	int i, j;

	list_for_each_entry(report, &re->report_list, list) {
		if (report->application != application)
			continue;

		for (i = 0; i < report->maxfield; i++) {
			struct hid_field *field = report->field[i];

			for (j = 0; j < field->maxusage; j++)
				if (field->usage[j].hid == usage)
					return true;
		}
	}

	return false;
}

static bool hammer_has_folded_event(struct hid_device *hdev)
{
	return hammer_has_usage(hdev, HID_INPUT_REPORT,
				HID_GD_KEYBOARD, HID_USAGE_KBD_FOLDED);
}

static bool hammer_has_backlight_control(struct hid_device *hdev)
{
	return hammer_has_usage(hdev, HID_OUTPUT_REPORT,
				HID_GD_KEYBOARD, HID_AD_BRIGHTNESS);
}

static void hammer_get_folded_state(struct hid_device *hdev)
{
	struct hid_report *report;
	char *buf;
	int len, rlen;
	int a;

	report = hdev->report_enum[HID_INPUT_REPORT].report_id_hash[0x0];

	if (!report || report->maxfield < 1)
		return;

	len = hid_report_len(report) + 1;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	rlen = hid_hw_raw_request(hdev, report->id, buf, len, report->type, HID_REQ_GET_REPORT);

	if (rlen != len) {
		hid_warn(hdev, "Unable to read base folded state: %d (expected %d)\n", rlen, len);
		goto out;
	}

	for (a = 0; a < report->maxfield; a++) {
		struct hid_field *field = report->field[a];

		if (field->usage->hid == HID_USAGE_KBD_FOLDED) {
			u32 value = hid_field_extract(hdev, buf+1,
					field->report_offset, field->report_size);

			hammer_folded_event(hdev, value);
			break;
		}
	}

out:
	kfree(buf);
}

static int hammer_probe(struct hid_device *hdev,
			const struct hid_device_id *id)
{
	int error;

	error = hid_parse(hdev);
	if (error)
		return error;

	error = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (error)
		return error;

	/*
	 * We always want to poll for, and handle tablet mode events from
	 * devices that have folded usage, even when nobody has opened the input
	 * device. This also prevents the hid core from dropping early tablet
	 * mode events from the device.
	 */
	if (hammer_has_folded_event(hdev)) {
		hdev->quirks |= HID_QUIRK_ALWAYS_POLL;
		error = hid_hw_open(hdev);
		if (error)
			return error;

		hammer_get_folded_state(hdev);
	}

	if (hammer_has_backlight_control(hdev)) {
		error = hammer_register_leds(hdev);
		if (error)
			hid_warn(hdev,
				"Failed to register keyboard backlight: %d\n",
				error);
	}

	return 0;
}

static void hammer_remove(struct hid_device *hdev)
{
	unsigned long flags;

	if (hammer_has_folded_event(hdev)) {
		hid_hw_close(hdev);

		/*
		 * If we are disconnecting then most likely Whiskers is
		 * being removed. Even if it is not removed, without proper
		 * keyboard we should not stay in clamshell mode.
		 *
		 * The reason for doing it here and not waiting for signal
		 * from EC, is that on some devices there are high leakage
		 * on Whiskers pins and we do not detect disconnect reliably,
		 * resulting in devices being stuck in clamshell mode.
		 */
		spin_lock_irqsave(&cbas_ec_lock, flags);
		if (cbas_ec.input && cbas_ec.base_present) {
			input_report_switch(cbas_ec.input, SW_TABLET_MODE, 1);
			input_sync(cbas_ec.input);
		}
		cbas_ec.base_present = false;
		spin_unlock_irqrestore(&cbas_ec_lock, flags);
	}

	hammer_unregister_leds(hdev);

	hid_hw_stop(hdev);
}

static const struct hid_device_id hammer_devices[] = {
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_DON) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_HAMMER) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_MAGNEMITE) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_MASTERBALL) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_MOONBALL) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_STAFF) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_WAND) },
	{ HID_DEVICE(BUS_USB, HID_GROUP_GENERIC,
		     USB_VENDOR_ID_GOOGLE, USB_DEVICE_ID_GOOGLE_WHISKERS) },
	{ }
};
MODULE_DEVICE_TABLE(hid, hammer_devices);

static struct hid_driver hammer_driver = {
	.name = "hammer",
	.id_table = hammer_devices,
	.probe = hammer_probe,
	.remove = hammer_remove,
	.input_mapping = hammer_input_mapping,
	.event = hammer_event,
};

static int __init hammer_init(void)
{
	int error;

	error = platform_driver_register(&cbas_ec_driver);
	if (error)
		return error;

	error = hid_register_driver(&hammer_driver);
	if (error) {
		platform_driver_unregister(&cbas_ec_driver);
		return error;
	}

	return 0;
}
module_init(hammer_init);

static void __exit hammer_exit(void)
{
	hid_unregister_driver(&hammer_driver);
	platform_driver_unregister(&cbas_ec_driver);
}
module_exit(hammer_exit);

MODULE_LICENSE("GPL");
