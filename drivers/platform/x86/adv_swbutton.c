// SPDX-License-Identifier: GPL-2.0
/*
 *  adv_swbutton.c - Software Button Interface Driver.
 *
 *  (C) Copyright 2020 Advantech Corporation, Inc
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

#define ACPI_BUTTON_HID_SWBTN               "AHC0310"

#define ACPI_BUTTON_NOTIFY_SWBTN_RELEASE    0x86
#define ACPI_BUTTON_NOTIFY_SWBTN_PRESSED    0x85

struct adv_swbutton {
	struct input_dev *input;
	char phys[32];
};

/*-------------------------------------------------------------------------
 *                               Driver Interface
 *--------------------------------------------------------------------------
 */
static void adv_swbutton_notify(acpi_handle handle, u32 event, void *context)
{
	struct platform_device *device = context;
	struct adv_swbutton *button = dev_get_drvdata(&device->dev);

	switch (event) {
	case ACPI_BUTTON_NOTIFY_SWBTN_RELEASE:
		input_report_key(button->input, KEY_PROG1, 0);
		input_sync(button->input);
		break;
	case ACPI_BUTTON_NOTIFY_SWBTN_PRESSED:
		input_report_key(button->input, KEY_PROG1, 1);
		input_sync(button->input);
		break;
	default:
		dev_dbg(&device->dev, "Unsupported event [0x%x]\n", event);
	}
}

static int adv_swbutton_probe(struct platform_device *device)
{
	struct adv_swbutton *button;
	struct input_dev *input;
	acpi_handle handle = ACPI_HANDLE(&device->dev);
	acpi_status status;
	int error;

	button = devm_kzalloc(&device->dev, sizeof(*button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	dev_set_drvdata(&device->dev, button);

	input = devm_input_allocate_device(&device->dev);
	if (!input)
		return -ENOMEM;

	button->input = input;
	snprintf(button->phys, sizeof(button->phys), "%s/button/input0", ACPI_BUTTON_HID_SWBTN);

	input->name = "Advantech Software Button";
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->dev.parent = &device->dev;
	set_bit(EV_REP, input->evbit);
	input_set_capability(input, EV_KEY, KEY_PROG1);

	error = input_register_device(input);
	if (error)
		return error;

	device_init_wakeup(&device->dev, true);

	status = acpi_install_notify_handler(handle,
					     ACPI_DEVICE_NOTIFY,
					     adv_swbutton_notify,
					     device);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Error installing notify handler\n");
		return -EIO;
	}

	return 0;
}

static void adv_swbutton_remove(struct platform_device *device)
{
	acpi_handle handle = ACPI_HANDLE(&device->dev);

	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY,
				   adv_swbutton_notify);
}

static const struct acpi_device_id button_device_ids[] = {
	{ACPI_BUTTON_HID_SWBTN, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, button_device_ids);

static struct platform_driver adv_swbutton_driver = {
	.driver = {
		.name = "adv_swbutton",
		.acpi_match_table = button_device_ids,
	},
	.probe = adv_swbutton_probe,
	.remove = adv_swbutton_remove,
};
module_platform_driver(adv_swbutton_driver);

MODULE_AUTHOR("Andrea Ho");
MODULE_DESCRIPTION("Advantech ACPI SW Button Driver");
MODULE_LICENSE("GPL v2");
