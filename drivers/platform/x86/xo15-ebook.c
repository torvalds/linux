// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  OLPC XO-1.5 ebook switch driver
 *  (based on generic ACPI button driver)
 *
 *  Copyright (C) 2009 Paul Fox <pgf@laptop.org>
 *  Copyright (C) 2010 One Laptop per Child
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/acpi.h>

#define MODULE_NAME "xo15-ebook"

#define XO15_EBOOK_CLASS		MODULE_NAME
#define XO15_EBOOK_TYPE_UNKNOWN	0x00
#define XO15_EBOOK_NOTIFY_STATUS	0x80

#define XO15_EBOOK_SUBCLASS		"ebook"
#define XO15_EBOOK_HID			"XO15EBK"
#define XO15_EBOOK_DEVICE_NAME		"EBook Switch"

MODULE_DESCRIPTION("OLPC XO-1.5 ebook switch driver");
MODULE_LICENSE("GPL");

static const struct acpi_device_id ebook_device_ids[] = {
	{ XO15_EBOOK_HID, 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, ebook_device_ids);

struct ebook_switch {
	struct input_dev *input;
	char phys[32];			/* for input device */
};

static int ebook_send_state(struct acpi_device *device)
{
	struct ebook_switch *button = acpi_driver_data(device);
	unsigned long long state;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "EBK", NULL, &state);
	if (ACPI_FAILURE(status))
		return -EIO;

	/* input layer checks if event is redundant */
	input_report_switch(button->input, SW_TABLET_MODE, !state);
	input_sync(button->input);
	return 0;
}

static void ebook_switch_notify(struct acpi_device *device, u32 event)
{
	switch (event) {
	case ACPI_FIXED_HARDWARE_EVENT:
	case XO15_EBOOK_NOTIFY_STATUS:
		ebook_send_state(device);
		break;
	default:
		acpi_handle_debug(device->handle,
				  "Unsupported event [0x%x]\n", event);
		break;
	}
}

#ifdef CONFIG_PM_SLEEP
static int ebook_switch_resume(struct device *dev)
{
	return ebook_send_state(to_acpi_device(dev));
}
#endif

static SIMPLE_DEV_PM_OPS(ebook_switch_pm, NULL, ebook_switch_resume);

static int ebook_switch_add(struct acpi_device *device)
{
	struct ebook_switch *button;
	struct input_dev *input;
	const char *hid = acpi_device_hid(device);
	char *name, *class;
	int error;

	button = kzalloc(sizeof(struct ebook_switch), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	device->driver_data = button;

	button->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_button;
	}

	name = acpi_device_name(device);
	class = acpi_device_class(device);

	if (strcmp(hid, XO15_EBOOK_HID)) {
		pr_err("Unsupported hid [%s]\n", hid);
		error = -ENODEV;
		goto err_free_input;
	}

	strcpy(name, XO15_EBOOK_DEVICE_NAME);
	sprintf(class, "%s/%s", XO15_EBOOK_CLASS, XO15_EBOOK_SUBCLASS);

	snprintf(button->phys, sizeof(button->phys), "%s/button/input0", hid);

	input->name = name;
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->dev.parent = &device->dev;

	input->evbit[0] = BIT_MASK(EV_SW);
	set_bit(SW_TABLET_MODE, input->swbit);

	error = input_register_device(input);
	if (error)
		goto err_free_input;

	ebook_send_state(device);

	if (device->wakeup.flags.valid) {
		/* Button's GPE is run-wake GPE */
		acpi_enable_gpe(device->wakeup.gpe_device,
				device->wakeup.gpe_number);
		device_set_wakeup_enable(&device->dev, true);
	}

	return 0;

 err_free_input:
	input_free_device(input);
 err_free_button:
	kfree(button);
	return error;
}

static void ebook_switch_remove(struct acpi_device *device)
{
	struct ebook_switch *button = acpi_driver_data(device);

	input_unregister_device(button->input);
	kfree(button);
}

static struct acpi_driver xo15_ebook_driver = {
	.name = MODULE_NAME,
	.class = XO15_EBOOK_CLASS,
	.ids = ebook_device_ids,
	.ops = {
		.add = ebook_switch_add,
		.remove = ebook_switch_remove,
		.notify = ebook_switch_notify,
	},
	.drv.pm = &ebook_switch_pm,
};
module_acpi_driver(xo15_ebook_driver);
