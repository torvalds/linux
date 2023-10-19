// SPDX-License-Identifier: GPL-2.0-only
/***************************************************************************
 *   Copyright (C) 2010-2012 by Bruno Prémont <bonbons@linux-vserver.org>  *
 *                                                                         *
 *   Based on Logitech G13 driver (v0.4)                                   *
 *     Copyright (C) 2009 by Rick L. Vinyard, Jr. <rvinyard@cs.nmsu.edu>   *
 *                                                                         *
 ***************************************************************************/

#include <linux/hid.h>
#include <linux/hid-debug.h>
#include <linux/input.h>
#include "hid-ids.h"

#include <linux/fb.h>
#include <linux/vmalloc.h>

#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/string.h>

#include "hid-picolcd.h"


/* Input device
 *
 * The PicoLCD has an IR receiver header, a built-in keypad with 5 keys
 * and header for 4x4 key matrix. The built-in keys are part of the matrix.
 */
static const unsigned short def_keymap[PICOLCD_KEYS] = {
	KEY_RESERVED,	/* none */
	KEY_BACK,	/* col 4 + row 1 */
	KEY_HOMEPAGE,	/* col 3 + row 1 */
	KEY_RESERVED,	/* col 2 + row 1 */
	KEY_RESERVED,	/* col 1 + row 1 */
	KEY_SCROLLUP,	/* col 4 + row 2 */
	KEY_OK,		/* col 3 + row 2 */
	KEY_SCROLLDOWN,	/* col 2 + row 2 */
	KEY_RESERVED,	/* col 1 + row 2 */
	KEY_RESERVED,	/* col 4 + row 3 */
	KEY_RESERVED,	/* col 3 + row 3 */
	KEY_RESERVED,	/* col 2 + row 3 */
	KEY_RESERVED,	/* col 1 + row 3 */
	KEY_RESERVED,	/* col 4 + row 4 */
	KEY_RESERVED,	/* col 3 + row 4 */
	KEY_RESERVED,	/* col 2 + row 4 */
	KEY_RESERVED,	/* col 1 + row 4 */
};


/* Find a given report */
struct hid_report *picolcd_report(int id, struct hid_device *hdev, int dir)
{
	struct list_head *feature_report_list = &hdev->report_enum[dir].report_list;
	struct hid_report *report = NULL;

	list_for_each_entry(report, feature_report_list, list) {
		if (report->id == id)
			return report;
	}
	hid_warn(hdev, "No report with id 0x%x found\n", id);
	return NULL;
}

/* Submit a report and wait for a reply from device - if device fades away
 * or does not respond in time, return NULL */
struct picolcd_pending *picolcd_send_and_wait(struct hid_device *hdev,
		int report_id, const u8 *raw_data, int size)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct picolcd_pending *work;
	struct hid_report *report = picolcd_out_report(report_id, hdev);
	unsigned long flags;
	int i, j, k;

	if (!report || !data)
		return NULL;
	if (data->status & PICOLCD_FAILED)
		return NULL;
	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return NULL;

	init_completion(&work->ready);
	work->out_report = report;
	work->in_report  = NULL;
	work->raw_size   = 0;

	mutex_lock(&data->mutex);
	spin_lock_irqsave(&data->lock, flags);
	for (i = k = 0; i < report->maxfield; i++)
		for (j = 0; j < report->field[i]->report_count; j++) {
			hid_set_field(report->field[i], j, k < size ? raw_data[k] : 0);
			k++;
		}
	if (data->status & PICOLCD_FAILED) {
		kfree(work);
		work = NULL;
	} else {
		data->pending = work;
		hid_hw_request(data->hdev, report, HID_REQ_SET_REPORT);
		spin_unlock_irqrestore(&data->lock, flags);
		wait_for_completion_interruptible_timeout(&work->ready, HZ*2);
		spin_lock_irqsave(&data->lock, flags);
		data->pending = NULL;
	}
	spin_unlock_irqrestore(&data->lock, flags);
	mutex_unlock(&data->mutex);
	return work;
}

/*
 * input class device
 */
static int picolcd_raw_keypad(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size)
{
	/*
	 * Keypad event
	 * First and second data bytes list currently pressed keys,
	 * 0x00 means no key and at most 2 keys may be pressed at same time
	 */
	int i, j;

	/* determine newly pressed keys */
	for (i = 0; i < size; i++) {
		unsigned int key_code;
		if (raw_data[i] == 0)
			continue;
		for (j = 0; j < sizeof(data->pressed_keys); j++)
			if (data->pressed_keys[j] == raw_data[i])
				goto key_already_down;
		for (j = 0; j < sizeof(data->pressed_keys); j++)
			if (data->pressed_keys[j] == 0) {
				data->pressed_keys[j] = raw_data[i];
				break;
			}
		input_event(data->input_keys, EV_MSC, MSC_SCAN, raw_data[i]);
		if (raw_data[i] < PICOLCD_KEYS)
			key_code = data->keycode[raw_data[i]];
		else
			key_code = KEY_UNKNOWN;
		if (key_code != KEY_UNKNOWN) {
			dbg_hid(PICOLCD_NAME " got key press for %u:%d",
					raw_data[i], key_code);
			input_report_key(data->input_keys, key_code, 1);
		}
		input_sync(data->input_keys);
key_already_down:
		continue;
	}

	/* determine newly released keys */
	for (j = 0; j < sizeof(data->pressed_keys); j++) {
		unsigned int key_code;
		if (data->pressed_keys[j] == 0)
			continue;
		for (i = 0; i < size; i++)
			if (data->pressed_keys[j] == raw_data[i])
				goto key_still_down;
		input_event(data->input_keys, EV_MSC, MSC_SCAN, data->pressed_keys[j]);
		if (data->pressed_keys[j] < PICOLCD_KEYS)
			key_code = data->keycode[data->pressed_keys[j]];
		else
			key_code = KEY_UNKNOWN;
		if (key_code != KEY_UNKNOWN) {
			dbg_hid(PICOLCD_NAME " got key release for %u:%d",
					data->pressed_keys[j], key_code);
			input_report_key(data->input_keys, key_code, 0);
		}
		input_sync(data->input_keys);
		data->pressed_keys[j] = 0;
key_still_down:
		continue;
	}
	return 1;
}

static int picolcd_check_version(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct picolcd_pending *verinfo;
	int ret = 0;

	if (!data)
		return -ENODEV;

	verinfo = picolcd_send_and_wait(hdev, REPORT_VERSION, NULL, 0);
	if (!verinfo) {
		hid_err(hdev, "no version response from PicoLCD\n");
		return -ENODEV;
	}

	if (verinfo->raw_size == 2) {
		data->version[0] = verinfo->raw_data[1];
		data->version[1] = verinfo->raw_data[0];
		if (data->status & PICOLCD_BOOTLOADER) {
			hid_info(hdev, "PicoLCD, bootloader version %d.%d\n",
				 verinfo->raw_data[1], verinfo->raw_data[0]);
		} else {
			hid_info(hdev, "PicoLCD, firmware version %d.%d\n",
				 verinfo->raw_data[1], verinfo->raw_data[0]);
		}
	} else {
		hid_err(hdev, "confused, got unexpected version response from PicoLCD\n");
		ret = -EINVAL;
	}
	kfree(verinfo);
	return ret;
}

/*
 * Reset our device and wait for answer to VERSION request
 */
int picolcd_reset(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct hid_report *report = picolcd_out_report(REPORT_RESET, hdev);
	unsigned long flags;
	int error;

	if (!data || !report || report->maxfield != 1)
		return -ENODEV;

	spin_lock_irqsave(&data->lock, flags);
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;

	/* perform the reset */
	hid_set_field(report->field[0], 0, 1);
	if (data->status & PICOLCD_FAILED) {
		spin_unlock_irqrestore(&data->lock, flags);
		return -ENODEV;
	}
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
	spin_unlock_irqrestore(&data->lock, flags);

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	picolcd_resume_lcd(data);
	picolcd_resume_backlight(data);
	picolcd_fb_refresh(data);
	picolcd_leds_set(data);
	return 0;
}

/*
 * The "operation_mode" sysfs attribute
 */
static ssize_t picolcd_operation_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct picolcd_data *data = dev_get_drvdata(dev);

	if (data->status & PICOLCD_BOOTLOADER)
		return snprintf(buf, PAGE_SIZE, "[bootloader] lcd\n");
	else
		return snprintf(buf, PAGE_SIZE, "bootloader [lcd]\n");
}

static ssize_t picolcd_operation_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	struct hid_report *report = NULL;
	int timeout = data->opmode_delay;
	unsigned long flags;

	if (sysfs_streq(buf, "lcd")) {
		if (data->status & PICOLCD_BOOTLOADER)
			report = picolcd_out_report(REPORT_EXIT_FLASHER, data->hdev);
	} else if (sysfs_streq(buf, "bootloader")) {
		if (!(data->status & PICOLCD_BOOTLOADER))
			report = picolcd_out_report(REPORT_EXIT_KEYBOARD, data->hdev);
	} else {
		return -EINVAL;
	}

	if (!report || report->maxfield != 1)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, timeout & 0xff);
	hid_set_field(report->field[0], 1, (timeout >> 8) & 0xff);
	hid_hw_request(data->hdev, report, HID_REQ_SET_REPORT);
	spin_unlock_irqrestore(&data->lock, flags);
	return count;
}

static DEVICE_ATTR(operation_mode, 0644, picolcd_operation_mode_show,
		picolcd_operation_mode_store);

/*
 * The "operation_mode_delay" sysfs attribute
 */
static ssize_t picolcd_operation_mode_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct picolcd_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%hu\n", data->opmode_delay);
}

static ssize_t picolcd_operation_mode_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	unsigned u;
	if (sscanf(buf, "%u", &u) != 1)
		return -EINVAL;
	if (u > 30000)
		return -EINVAL;
	else
		data->opmode_delay = u;
	return count;
}

static DEVICE_ATTR(operation_mode_delay, 0644, picolcd_operation_mode_delay_show,
		picolcd_operation_mode_delay_store);

/*
 * Handle raw report as sent by device
 */
static int picolcd_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	unsigned long flags;

	if (!data)
		return 1;

	if (size > 64) {
		hid_warn(hdev, "invalid size value (%d) for picolcd raw event (%d)\n",
				size, report->id);
		return 0;
	}

	if (report->id == REPORT_KEY_STATE) {
		if (data->input_keys)
			picolcd_raw_keypad(data, report, raw_data+1, size-1);
	} else if (report->id == REPORT_IR_DATA) {
		picolcd_raw_cir(data, report, raw_data+1, size-1);
	} else {
		spin_lock_irqsave(&data->lock, flags);
		/*
		 * We let the caller of picolcd_send_and_wait() check if the
		 * report we got is one of the expected ones or not.
		 */
		if (data->pending) {
			memcpy(data->pending->raw_data, raw_data+1, size-1);
			data->pending->raw_size  = size-1;
			data->pending->in_report = report;
			complete(&data->pending->ready);
		}
		spin_unlock_irqrestore(&data->lock, flags);
	}

	picolcd_debug_raw_event(data, hdev, report, raw_data, size);
	return 1;
}

#ifdef CONFIG_PM
static int picolcd_suspend(struct hid_device *hdev, pm_message_t message)
{
	if (PMSG_IS_AUTO(message))
		return 0;

	picolcd_suspend_backlight(hid_get_drvdata(hdev));
	dbg_hid(PICOLCD_NAME " device ready for suspend\n");
	return 0;
}

static int picolcd_resume(struct hid_device *hdev)
{
	int ret;
	ret = picolcd_resume_backlight(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring backlight failed: %d\n", ret);
	return 0;
}

static int picolcd_reset_resume(struct hid_device *hdev)
{
	int ret;
	ret = picolcd_reset(hdev);
	if (ret)
		dbg_hid(PICOLCD_NAME " resetting our device failed: %d\n", ret);
	ret = picolcd_fb_reset(hid_get_drvdata(hdev), 0);
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring framebuffer content failed: %d\n", ret);
	ret = picolcd_resume_lcd(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring lcd failed: %d\n", ret);
	ret = picolcd_resume_backlight(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring backlight failed: %d\n", ret);
	picolcd_leds_set(hid_get_drvdata(hdev));
	return 0;
}
#endif

/* initialize keypad input device */
static int picolcd_init_keys(struct picolcd_data *data,
		struct hid_report *report)
{
	struct hid_device *hdev = data->hdev;
	struct input_dev *idev;
	int error, i;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 2 ||
			report->field[0]->report_size != 8) {
		hid_err(hdev, "unsupported KEY_STATE report\n");
		return -EINVAL;
	}

	idev = input_allocate_device();
	if (idev == NULL) {
		hid_err(hdev, "failed to allocate input device\n");
		return -ENOMEM;
	}
	input_set_drvdata(idev, hdev);
	memcpy(data->keycode, def_keymap, sizeof(def_keymap));
	idev->name = hdev->name;
	idev->phys = hdev->phys;
	idev->uniq = hdev->uniq;
	idev->id.bustype = hdev->bus;
	idev->id.vendor  = hdev->vendor;
	idev->id.product = hdev->product;
	idev->id.version = hdev->version;
	idev->dev.parent = &hdev->dev;
	idev->keycode     = &data->keycode;
	idev->keycodemax  = PICOLCD_KEYS;
	idev->keycodesize = sizeof(data->keycode[0]);
	input_set_capability(idev, EV_MSC, MSC_SCAN);
	set_bit(EV_REP, idev->evbit);
	for (i = 0; i < PICOLCD_KEYS; i++)
		input_set_capability(idev, EV_KEY, data->keycode[i]);
	error = input_register_device(idev);
	if (error) {
		hid_err(hdev, "error registering the input device\n");
		input_free_device(idev);
		return error;
	}
	data->input_keys = idev;
	return 0;
}

static void picolcd_exit_keys(struct picolcd_data *data)
{
	struct input_dev *idev = data->input_keys;

	data->input_keys = NULL;
	if (idev)
		input_unregister_device(idev);
}

static int picolcd_probe_lcd(struct hid_device *hdev, struct picolcd_data *data)
{
	int error;

	/* Setup keypad input device */
	error = picolcd_init_keys(data, picolcd_in_report(REPORT_KEY_STATE, hdev));
	if (error)
		goto err;

	/* Setup CIR input device */
	error = picolcd_init_cir(data, picolcd_in_report(REPORT_IR_DATA, hdev));
	if (error)
		goto err;

	/* Set up the framebuffer device */
	error = picolcd_init_framebuffer(data);
	if (error)
		goto err;

	/* Setup lcd class device */
	error = picolcd_init_lcd(data, picolcd_out_report(REPORT_CONTRAST, hdev));
	if (error)
		goto err;

	/* Setup backlight class device */
	error = picolcd_init_backlight(data, picolcd_out_report(REPORT_BRIGHTNESS, hdev));
	if (error)
		goto err;

	/* Setup the LED class devices */
	error = picolcd_init_leds(data, picolcd_out_report(REPORT_LED_STATE, hdev));
	if (error)
		goto err;

	picolcd_init_devfs(data, picolcd_out_report(REPORT_EE_READ, hdev),
			picolcd_out_report(REPORT_EE_WRITE, hdev),
			picolcd_out_report(REPORT_READ_MEMORY, hdev),
			picolcd_out_report(REPORT_WRITE_MEMORY, hdev),
			picolcd_out_report(REPORT_RESET, hdev));
	return 0;
err:
	picolcd_exit_leds(data);
	picolcd_exit_backlight(data);
	picolcd_exit_lcd(data);
	picolcd_exit_framebuffer(data);
	picolcd_exit_cir(data);
	picolcd_exit_keys(data);
	return error;
}

static int picolcd_probe_bootloader(struct hid_device *hdev, struct picolcd_data *data)
{
	picolcd_init_devfs(data, NULL, NULL,
			picolcd_out_report(REPORT_BL_READ_MEMORY, hdev),
			picolcd_out_report(REPORT_BL_WRITE_MEMORY, hdev), NULL);
	return 0;
}

static int picolcd_probe(struct hid_device *hdev,
		     const struct hid_device_id *id)
{
	struct picolcd_data *data;
	int error = -ENOMEM;

	dbg_hid(PICOLCD_NAME " hardware probe...\n");

	/*
	 * Let's allocate the picolcd data structure, set some reasonable
	 * defaults, and associate it with the device
	 */
	data = kzalloc(sizeof(struct picolcd_data), GFP_KERNEL);
	if (data == NULL) {
		hid_err(hdev, "can't allocate space for Minibox PicoLCD device data\n");
		return -ENOMEM;
	}

	spin_lock_init(&data->lock);
	mutex_init(&data->mutex);
	data->hdev = hdev;
	data->opmode_delay = 5000;
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;
	hid_set_drvdata(hdev, data);

	/* Parse the device reports and start it up */
	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "device report parse failed\n");
		goto err_cleanup_data;
	}

	error = hid_hw_start(hdev, 0);
	if (error) {
		hid_err(hdev, "hardware start failed\n");
		goto err_cleanup_data;
	}

	error = hid_hw_open(hdev);
	if (error) {
		hid_err(hdev, "failed to open input interrupt pipe for key and IR events\n");
		goto err_cleanup_hid_hw;
	}

	error = device_create_file(&hdev->dev, &dev_attr_operation_mode_delay);
	if (error) {
		hid_err(hdev, "failed to create sysfs attributes\n");
		goto err_cleanup_hid_ll;
	}

	error = device_create_file(&hdev->dev, &dev_attr_operation_mode);
	if (error) {
		hid_err(hdev, "failed to create sysfs attributes\n");
		goto err_cleanup_sysfs1;
	}

	if (data->status & PICOLCD_BOOTLOADER)
		error = picolcd_probe_bootloader(hdev, data);
	else
		error = picolcd_probe_lcd(hdev, data);
	if (error)
		goto err_cleanup_sysfs2;

	dbg_hid(PICOLCD_NAME " activated and initialized\n");
	return 0;

err_cleanup_sysfs2:
	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
err_cleanup_sysfs1:
	device_remove_file(&hdev->dev, &dev_attr_operation_mode_delay);
err_cleanup_hid_ll:
	hid_hw_close(hdev);
err_cleanup_hid_hw:
	hid_hw_stop(hdev);
err_cleanup_data:
	kfree(data);
	return error;
}

static void picolcd_remove(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	unsigned long flags;

	dbg_hid(PICOLCD_NAME " hardware remove...\n");
	spin_lock_irqsave(&data->lock, flags);
	data->status |= PICOLCD_FAILED;
	spin_unlock_irqrestore(&data->lock, flags);

	picolcd_exit_devfs(data);
	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
	device_remove_file(&hdev->dev, &dev_attr_operation_mode_delay);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);

	/* Shortcut potential pending reply that will never arrive */
	spin_lock_irqsave(&data->lock, flags);
	if (data->pending)
		complete(&data->pending->ready);
	spin_unlock_irqrestore(&data->lock, flags);

	/* Cleanup LED */
	picolcd_exit_leds(data);
	/* Clean up the framebuffer */
	picolcd_exit_backlight(data);
	picolcd_exit_lcd(data);
	picolcd_exit_framebuffer(data);
	/* Cleanup input */
	picolcd_exit_cir(data);
	picolcd_exit_keys(data);

	mutex_destroy(&data->mutex);
	/* Finally, clean up the picolcd data itself */
	kfree(data);
}

static const struct hid_device_id picolcd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_PICOLCD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_PICOLCD_BOOTLOADER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, picolcd_devices);

static struct hid_driver picolcd_driver = {
	.name =          "hid-picolcd",
	.id_table =      picolcd_devices,
	.probe =         picolcd_probe,
	.remove =        picolcd_remove,
	.raw_event =     picolcd_raw_event,
#ifdef CONFIG_PM
	.suspend =       picolcd_suspend,
	.resume =        picolcd_resume,
	.reset_resume =  picolcd_reset_resume,
#endif
};
module_hid_driver(picolcd_driver);

MODULE_DESCRIPTION("Minibox graphics PicoLCD Driver");
MODULE_LICENSE("GPL v2");
