// SPDX-License-Identifier: GPL-2.0-only
/*
 * power/home/volume button support for
 * Microsoft Surface Pro 3/4 tablet.
 *
 * Copyright (c) 2015 Intel Corporation.
 * All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/acpi.h>
#include <acpi/button.h>

#define SURFACE_PRO3_BUTTON_HID		"MSHW0028"
#define SURFACE_PRO4_BUTTON_HID		"MSHW0040"
#define SURFACE_BUTTON_OBJ_NAME		"VGBI"
#define SURFACE_BUTTON_DEVICE_NAME	"Surface Pro 3/4 Buttons"

#define MSHW0040_DSM_REVISION		0x01
#define MSHW0040_DSM_GET_OMPR		0x02	// get OEM Platform Revision
static const guid_t MSHW0040_DSM_UUID =
	GUID_INIT(0x6fd05c69, 0xcde3, 0x49f4, 0x95, 0xed, 0xab, 0x16, 0x65,
		  0x49, 0x80, 0x35);

#define SURFACE_BUTTON_NOTIFY_TABLET_MODE	0xc8

#define SURFACE_BUTTON_NOTIFY_PRESS_POWER	0xc6
#define SURFACE_BUTTON_NOTIFY_RELEASE_POWER	0xc7

#define SURFACE_BUTTON_NOTIFY_PRESS_HOME	0xc4
#define SURFACE_BUTTON_NOTIFY_RELEASE_HOME	0xc5

#define SURFACE_BUTTON_NOTIFY_PRESS_VOLUME_UP	0xc0
#define SURFACE_BUTTON_NOTIFY_RELEASE_VOLUME_UP	0xc1

#define SURFACE_BUTTON_NOTIFY_PRESS_VOLUME_DOWN		0xc2
#define SURFACE_BUTTON_NOTIFY_RELEASE_VOLUME_DOWN	0xc3

ACPI_MODULE_NAME("surface pro 3 button");

MODULE_AUTHOR("Chen Yu");
MODULE_DESCRIPTION("Surface Pro3 Button Driver");
MODULE_LICENSE("GPL v2");

/*
 * Power button, Home button, Volume buttons support is supposed to
 * be covered by drivers/input/misc/soc_button_array.c, which is implemented
 * according to "Windows ACPI Design Guide for SoC Platforms".
 * However surface pro3 seems not to obey the specs, instead it uses
 * device VGBI(MSHW0028) for dispatching the events.
 * We choose acpi_driver rather than platform_driver/i2c_driver because
 * although VGBI has an i2c resource connected to i2c controller, it
 * is not embedded in any i2c controller's scope, thus neither platform_device
 * will be created, nor i2c_client will be enumerated, we have to use
 * acpi_driver.
 */
static const struct acpi_device_id surface_button_device_ids[] = {
	{SURFACE_PRO3_BUTTON_HID,    0},
	{SURFACE_PRO4_BUTTON_HID,    0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, surface_button_device_ids);

struct surface_button {
	unsigned int type;
	struct input_dev *input;
	char phys[32];			/* for input device */
	unsigned long pushed;
	bool suspended;
};

static void surface_button_notify(struct acpi_device *device, u32 event)
{
	struct surface_button *button = acpi_driver_data(device);
	struct input_dev *input;
	int key_code = KEY_RESERVED;
	bool pressed = false;

	switch (event) {
	/* Power button press,release handle */
	case SURFACE_BUTTON_NOTIFY_PRESS_POWER:
		pressed = true;
		/*fall through*/
	case SURFACE_BUTTON_NOTIFY_RELEASE_POWER:
		key_code = KEY_POWER;
		break;
	/* Home button press,release handle */
	case SURFACE_BUTTON_NOTIFY_PRESS_HOME:
		pressed = true;
		/*fall through*/
	case SURFACE_BUTTON_NOTIFY_RELEASE_HOME:
		key_code = KEY_LEFTMETA;
		break;
	/* Volume up button press,release handle */
	case SURFACE_BUTTON_NOTIFY_PRESS_VOLUME_UP:
		pressed = true;
		/*fall through*/
	case SURFACE_BUTTON_NOTIFY_RELEASE_VOLUME_UP:
		key_code = KEY_VOLUMEUP;
		break;
	/* Volume down button press,release handle */
	case SURFACE_BUTTON_NOTIFY_PRESS_VOLUME_DOWN:
		pressed = true;
		/*fall through*/
	case SURFACE_BUTTON_NOTIFY_RELEASE_VOLUME_DOWN:
		key_code = KEY_VOLUMEDOWN;
		break;
	case SURFACE_BUTTON_NOTIFY_TABLET_MODE:
		dev_warn_once(&device->dev, "Tablet mode is not supported\n");
		break;
	default:
		dev_info_ratelimited(&device->dev,
				     "Unsupported event [0x%x]\n", event);
		break;
	}
	input = button->input;
	if (key_code == KEY_RESERVED)
		return;
	if (pressed)
		pm_wakeup_dev_event(&device->dev, 0, button->suspended);
	if (button->suspended)
		return;
	input_report_key(input, key_code, pressed?1:0);
	input_sync(input);
}

#ifdef CONFIG_PM_SLEEP
static int surface_button_suspend(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct surface_button *button = acpi_driver_data(device);

	button->suspended = true;
	return 0;
}

static int surface_button_resume(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct surface_button *button = acpi_driver_data(device);

	button->suspended = false;
	return 0;
}
#endif

/*
 * Surface Pro 4 and Surface Book 2 / Surface Pro 2017 use the same device
 * ID (MSHW0040) for the power/volume buttons. Make sure this is the right
 * device by checking for the _DSM method and OEM Platform Revision.
 *
 * Returns true if the driver should bind to this device, i.e. the device is
 * either MSWH0028 (Pro 3) or MSHW0040 on a Pro 4 or Book 1.
 */
static bool surface_button_check_MSHW0040(struct acpi_device *dev)
{
	acpi_handle handle = dev->handle;
	union acpi_object *result;
	u64 oem_platform_rev = 0;	// valid revisions are nonzero

	// get OEM platform revision
	result = acpi_evaluate_dsm_typed(handle, &MSHW0040_DSM_UUID,
					 MSHW0040_DSM_REVISION,
					 MSHW0040_DSM_GET_OMPR,
					 NULL, ACPI_TYPE_INTEGER);

	/*
	 * If evaluating the _DSM fails, the method is not present. This means
	 * that we have either MSHW0028 or MSHW0040 on Pro 4 or Book 1, so we
	 * should use this driver. We use revision 0 indicating it is
	 * unavailable.
	 */

	if (result) {
		oem_platform_rev = result->integer.value;
		ACPI_FREE(result);
	}

	dev_dbg(&dev->dev, "OEM Platform Revision %llu\n", oem_platform_rev);

	return oem_platform_rev == 0;
}


static int surface_button_add(struct acpi_device *device)
{
	struct surface_button *button;
	struct input_dev *input;
	const char *hid = acpi_device_hid(device);
	char *name;
	int error;

	if (strncmp(acpi_device_bid(device), SURFACE_BUTTON_OBJ_NAME,
	    strlen(SURFACE_BUTTON_OBJ_NAME)))
		return -ENODEV;

	if (!surface_button_check_MSHW0040(device))
		return -ENODEV;

	button = kzalloc(sizeof(struct surface_button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	device->driver_data = button;
	button->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_button;
	}

	name = acpi_device_name(device);
	strcpy(name, SURFACE_BUTTON_DEVICE_NAME);
	snprintf(button->phys, sizeof(button->phys), "%s/buttons", hid);

	input->name = name;
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->dev.parent = &device->dev;
	input_set_capability(input, EV_KEY, KEY_POWER);
	input_set_capability(input, EV_KEY, KEY_LEFTMETA);
	input_set_capability(input, EV_KEY, KEY_VOLUMEUP);
	input_set_capability(input, EV_KEY, KEY_VOLUMEDOWN);

	error = input_register_device(input);
	if (error)
		goto err_free_input;

	device_init_wakeup(&device->dev, true);
	dev_info(&device->dev,
			"%s [%s]\n", name, acpi_device_bid(device));
	return 0;

 err_free_input:
	input_free_device(input);
 err_free_button:
	kfree(button);
	return error;
}

static int surface_button_remove(struct acpi_device *device)
{
	struct surface_button *button = acpi_driver_data(device);

	input_unregister_device(button->input);
	kfree(button);
	return 0;
}

static SIMPLE_DEV_PM_OPS(surface_button_pm,
		surface_button_suspend, surface_button_resume);

static struct acpi_driver surface_button_driver = {
	.name = "surface_pro3_button",
	.class = "SurfacePro3",
	.ids = surface_button_device_ids,
	.ops = {
		.add = surface_button_add,
		.remove = surface_button_remove,
		.notify = surface_button_notify,
	},
	.drv.pm = &surface_button_pm,
};

module_acpi_driver(surface_button_driver);
