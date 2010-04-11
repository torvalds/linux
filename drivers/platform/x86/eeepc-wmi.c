/*
 * Eee PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define	EEEPC_WMI_FILE	"eeepc-wmi"

MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Eee PC WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"
#define EEEPC_WMI_MGMT_GUID	"97845ED0-4E6D-11DE-8A39-0800200C9A66"

MODULE_ALIAS("wmi:"EEEPC_WMI_EVENT_GUID);
MODULE_ALIAS("wmi:"EEEPC_WMI_MGMT_GUID);

#define NOTIFY_BRNUP_MIN	0x11
#define NOTIFY_BRNUP_MAX	0x1f
#define NOTIFY_BRNDOWN_MIN	0x20
#define NOTIFY_BRNDOWN_MAX	0x2e

#define EEEPC_WMI_METHODID_DEVS	0x53564544
#define EEEPC_WMI_METHODID_DSTS	0x53544344

#define EEEPC_WMI_DEVID_BACKLIGHT	0x00050012

static const struct key_entry eeepc_wmi_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{ KE_KEY, 0x5d, { KEY_WLAN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_IGNORE, NOTIFY_BRNDOWN_MIN, { KEY_BRIGHTNESSDOWN } },
	{ KE_IGNORE, NOTIFY_BRNUP_MIN, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xcc, { KEY_SWITCHVIDEOMODE } },
	{ KE_END, 0},
};

struct bios_args {
	u32	dev_id;
	u32	ctrl_param;
};

struct eeepc_wmi {
	struct input_dev *inputdev;
	struct backlight_device *backlight_device;
};

static struct platform_device *platform_device;

static int eeepc_wmi_input_init(struct eeepc_wmi *eeepc)
{
	int err;

	eeepc->inputdev = input_allocate_device();
	if (!eeepc->inputdev)
		return -ENOMEM;

	eeepc->inputdev->name = "Eee PC WMI hotkeys";
	eeepc->inputdev->phys = EEEPC_WMI_FILE "/input0";
	eeepc->inputdev->id.bustype = BUS_HOST;
	eeepc->inputdev->dev.parent = &platform_device->dev;

	err = sparse_keymap_setup(eeepc->inputdev, eeepc_wmi_keymap, NULL);
	if (err)
		goto err_free_dev;

	err = input_register_device(eeepc->inputdev);
	if (err)
		goto err_free_keymap;

	return 0;

err_free_keymap:
	sparse_keymap_free(eeepc->inputdev);
err_free_dev:
	input_free_device(eeepc->inputdev);
	return err;
}

static void eeepc_wmi_input_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->inputdev) {
		sparse_keymap_free(eeepc->inputdev);
		input_unregister_device(eeepc->inputdev);
	}

	eeepc->inputdev = NULL;
}

static acpi_status eeepc_wmi_get_devstate(u32 dev_id, u32 *ctrl_param)
{
	struct acpi_buffer input = { (acpi_size)sizeof(u32), &dev_id };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u32 tmp;

	status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID,
			1, EEEPC_WMI_METHODID_DSTS, &input, &output);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *)output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		tmp = (u32)obj->integer.value;
	else
		tmp = 0;

	if (ctrl_param)
		*ctrl_param = tmp;

	kfree(obj);

	return status;

}

static acpi_status eeepc_wmi_set_devstate(u32 dev_id, u32 ctrl_param)
{
	struct bios_args args = {
		.dev_id = dev_id,
		.ctrl_param = ctrl_param,
	};
	struct acpi_buffer input = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID,
			1, EEEPC_WMI_METHODID_DEVS, &input, NULL);

	return status;
}

static int read_brightness(struct backlight_device *bd)
{
	static u32 ctrl_param;
	acpi_status status;

	status = eeepc_wmi_get_devstate(EEEPC_WMI_DEVID_BACKLIGHT, &ctrl_param);

	if (ACPI_FAILURE(status))
		return -1;
	else
		return ctrl_param & 0xFF;
}

static int update_bl_status(struct backlight_device *bd)
{

	static u32 ctrl_param;
	acpi_status status;

	ctrl_param = bd->props.brightness;

	status = eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_BACKLIGHT, ctrl_param);

	if (ACPI_FAILURE(status))
		return -1;
	else
		return 0;
}

static const struct backlight_ops eeepc_wmi_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int eeepc_wmi_backlight_notify(struct eeepc_wmi *eeepc, int code)
{
	struct backlight_device *bd = eeepc->backlight_device;
	int old = bd->props.brightness;
	int new;

	if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
		new = code - NOTIFY_BRNUP_MIN + 1;
	else if (code >= NOTIFY_BRNDOWN_MIN && code <= NOTIFY_BRNDOWN_MAX)
		new = code - NOTIFY_BRNDOWN_MIN;

	bd->props.brightness = new;
	backlight_update_status(bd);
	backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);

	return old;
}

static int eeepc_wmi_backlight_init(struct eeepc_wmi *eeepc)
{
	struct backlight_device *bd;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 15;
	bd = backlight_device_register(EEEPC_WMI_FILE,
				       &platform_device->dev, eeepc,
				       &eeepc_wmi_bl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register backlight device\n");
		return PTR_ERR(bd);
	}

	eeepc->backlight_device = bd;

	bd->props.brightness = read_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	return 0;
}

static void eeepc_wmi_backlight_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->backlight_device)
		backlight_device_unregister(eeepc->backlight_device);

	eeepc->backlight_device = NULL;
}

static void eeepc_wmi_notify(u32 value, void *context)
{
	struct eeepc_wmi *eeepc = context;
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int code;
	int orig_code;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_err("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		code = obj->integer.value;
		orig_code = code;

		if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
			code = NOTIFY_BRNUP_MIN;
		else if (code >= NOTIFY_BRNDOWN_MIN &&
			 code <= NOTIFY_BRNDOWN_MAX)
			code = NOTIFY_BRNDOWN_MIN;

		if (code == NOTIFY_BRNUP_MIN || code == NOTIFY_BRNDOWN_MIN) {
			if (!acpi_video_backlight_support())
				eeepc_wmi_backlight_notify(eeepc, orig_code);
		}

		if (!sparse_keymap_report_event(eeepc->inputdev,
						code, 1, true))
			pr_info("Unknown key %x pressed\n", code);
	}

	kfree(obj);
}

static int __devinit eeepc_wmi_platform_probe(struct platform_device *device)
{
	struct eeepc_wmi *eeepc;
	int err;
	acpi_status status;

	eeepc = platform_get_drvdata(device);

	err = eeepc_wmi_input_init(eeepc);
	if (err)
		goto error_input;

	if (!acpi_video_backlight_support()) {
		err = eeepc_wmi_backlight_init(eeepc);
		if (err)
			goto error_backlight;
	} else
		pr_info("Backlight controlled by ACPI video driver\n");

	status = wmi_install_notify_handler(EEEPC_WMI_EVENT_GUID,
					eeepc_wmi_notify, eeepc);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to register notify handler - %d\n",
			status);
		err = -ENODEV;
		goto error_wmi;
	}

	return 0;

error_wmi:
	eeepc_wmi_backlight_exit(eeepc);
error_backlight:
	eeepc_wmi_input_exit(eeepc);
error_input:
	return err;
}

static int __devexit eeepc_wmi_platform_remove(struct platform_device *device)
{
	struct eeepc_wmi *eeepc;

	eeepc = platform_get_drvdata(device);
	wmi_remove_notify_handler(EEEPC_WMI_EVENT_GUID);
	eeepc_wmi_backlight_exit(eeepc);
	eeepc_wmi_input_exit(eeepc);

	return 0;
}

static struct platform_driver platform_driver = {
	.driver = {
		.name = EEEPC_WMI_FILE,
		.owner = THIS_MODULE,
	},
	.probe = eeepc_wmi_platform_probe,
	.remove = __devexit_p(eeepc_wmi_platform_remove),
};

static int __init eeepc_wmi_init(void)
{
	struct eeepc_wmi *eeepc;
	int err;

	if (!wmi_has_guid(EEEPC_WMI_EVENT_GUID) ||
	    !wmi_has_guid(EEEPC_WMI_MGMT_GUID)) {
		pr_warning("No known WMI GUID found\n");
		return -ENODEV;
	}

	eeepc = kzalloc(sizeof(struct eeepc_wmi), GFP_KERNEL);
	if (!eeepc)
		return -ENOMEM;

	platform_device = platform_device_alloc(EEEPC_WMI_FILE, -1);
	if (!platform_device) {
		pr_warning("Unable to allocate platform device\n");
		err = -ENOMEM;
		goto fail_platform;
	}

	err = platform_device_add(platform_device);
	if (err) {
		pr_warning("Unable to add platform device\n");
		goto put_dev;
	}

	platform_set_drvdata(platform_device, eeepc);

	err = platform_driver_register(&platform_driver);
	if (err) {
		pr_warning("Unable to register platform driver\n");
		goto del_dev;
	}

	return 0;

del_dev:
	platform_device_del(platform_device);
put_dev:
	platform_device_put(platform_device);
fail_platform:
	kfree(eeepc);

	return err;
}

static void __exit eeepc_wmi_exit(void)
{
	struct eeepc_wmi *eeepc;

	eeepc = platform_get_drvdata(platform_device);
	platform_driver_unregister(&platform_driver);
	platform_device_unregister(platform_device);
	kfree(eeepc);
}

module_init(eeepc_wmi_init);
module_exit(eeepc_wmi_exit);
