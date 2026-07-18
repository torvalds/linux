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
#include <linux/platform_device.h>

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
	bool gpe_enabled;
};

static int ebook_send_state(struct device *dev)
{
	struct ebook_switch *button = dev_get_drvdata(dev);
	unsigned long long state;
	acpi_status status;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "EBK", NULL, &state);
	if (ACPI_FAILURE(status))
		return -EIO;

	/* input layer checks if event is redundant */
	input_report_switch(button->input, SW_TABLET_MODE, !state);
	input_sync(button->input);
	return 0;
}

static void ebook_switch_notify(acpi_handle handle, u32 event, void *data)
{
	switch (event) {
	case ACPI_FIXED_HARDWARE_EVENT:
	case XO15_EBOOK_NOTIFY_STATUS:
		ebook_send_state(data);
		break;
	default:
		acpi_handle_debug(handle, "Unsupported event [0x%x]\n", event);
		break;
	}
}

#ifdef CONFIG_PM_SLEEP
static int ebook_switch_resume(struct device *dev)
{
	return ebook_send_state(dev);
}
#endif

static SIMPLE_DEV_PM_OPS(ebook_switch_pm, NULL, ebook_switch_resume);

static int ebook_switch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *device = ACPI_COMPANION(dev);
	const struct acpi_device_id *id;
	struct ebook_switch *button;
	struct input_dev *input;
	int error;

	button = devm_kzalloc(dev, sizeof(*button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	platform_set_drvdata(pdev, button);

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	button->input = input;

	id = acpi_match_acpi_device(ebook_device_ids, device);
	if (!id)
		return dev_err_probe(dev, -ENODEV, "Unsupported hid\n");

	strscpy(acpi_device_name(device), XO15_EBOOK_DEVICE_NAME);
	strscpy(acpi_device_class(device), XO15_EBOOK_CLASS "/" XO15_EBOOK_SUBCLASS);

	snprintf(button->phys, sizeof(button->phys), "%s/button/input0", id->id);

	input->name = acpi_device_name(device);
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;

	input->evbit[0] = BIT_MASK(EV_SW);
	set_bit(SW_TABLET_MODE, input->swbit);

	error = input_register_device(input);
	if (error)
		return error;

	error = acpi_dev_install_notify_handler(device, ACPI_DEVICE_NOTIFY,
						ebook_switch_notify, dev);
	if (error)
		return error;

	ebook_send_state(dev);

	if (device->wakeup.flags.valid) {
		/* Button's GPE is run-wake GPE */
		acpi_enable_gpe(device->wakeup.gpe_device,
				device->wakeup.gpe_number);
		button->gpe_enabled = true;
	}

	return 0;
}

static void ebook_switch_remove(struct platform_device *pdev)
{
	struct ebook_switch *button = platform_get_drvdata(pdev);
	struct acpi_device *device = ACPI_COMPANION(&pdev->dev);

	if (button->gpe_enabled)
		acpi_disable_gpe(device->wakeup.gpe_device,
				 device->wakeup.gpe_number);

	acpi_dev_remove_notify_handler(device, ACPI_DEVICE_NOTIFY,
				       ebook_switch_notify);
}

static struct platform_driver xo15_ebook_driver = {
	.probe = ebook_switch_probe,
	.remove = ebook_switch_remove,
	.driver = {
		.name = MODULE_NAME,
		.acpi_match_table = ebook_device_ids,
		.pm = &ebook_switch_pm,
	},
};
module_platform_driver(xo15_ebook_driver);
