// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ACPI Direct App Launch driver
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 * Copyright (C) 2022 Arvid Norlander <lkml@vorapal.se>
 * Copyright (C) 2007-2010 Angelo Arrifano <miknix@gmail.com>
 *
 * Information gathered from disassembled dsdt and from here:
 * <https://archive.org/details/microsoft-acpi-dirapplaunch>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/unaligned.h>

#define DRIVER_NAME	"quickstart"

/*
 * There will be two events:
 * 0x02 - Button was pressed while device was off/sleeping.
 * 0x80 - Button was pressed while device was up.
 */
#define QUICKSTART_EVENT_RUNTIME	0x80

struct quickstart_data {
	struct device *dev;
	struct mutex input_lock;	/* Protects input sequence during notify */
	struct input_dev *input_device;
	char input_name[32];
	char phys[32];
	u32 id;
};

/*
 * Knowing what these buttons do require system specific knowledge.
 * This could be done by matching on DMI data in a long quirk table.
 * However, it is easier to leave it up to user space to figure this out.
 *
 * Using for example udev hwdb the scancode 0x1 can be remapped suitably.
 */
static const struct key_entry quickstart_keymap[] = {
	{ KE_KEY, 0x1, { KEY_UNKNOWN } },
	{ KE_END, 0 },
};

static ssize_t button_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct quickstart_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", data->id);
}
static DEVICE_ATTR_RO(button_id);

static struct attribute *quickstart_attrs[] = {
	&dev_attr_button_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(quickstart);

static void quickstart_notify(acpi_handle handle, u32 event, void *context)
{
	struct quickstart_data *data = context;

	switch (event) {
	case QUICKSTART_EVENT_RUNTIME:
		mutex_lock(&data->input_lock);
		sparse_keymap_report_event(data->input_device, 0x1, 1, true);
		mutex_unlock(&data->input_lock);

		acpi_bus_generate_netlink_event(DRIVER_NAME, dev_name(data->dev), event, 0);
		break;
	default:
		dev_err(data->dev, FW_INFO "Unexpected ACPI notify event (%u)\n", event);
		break;
	}
}

/*
 * The GHID ACPI method is used to indicate the "role" of the button.
 * However, all the meanings of these values are vendor defined.
 *
 * We do however expose this value to user space.
 */
static int quickstart_get_ghid(struct quickstart_data *data)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle handle = ACPI_HANDLE(data->dev);
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	/*
	 * This returns a buffer telling the button usage ID,
	 * and triggers pending notify events (The ones before booting).
	 */
	status = acpi_evaluate_object_typed(handle, "GHID", NULL, &buffer, ACPI_TYPE_BUFFER);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = buffer.pointer;
	if (!obj)
		return -ENODATA;

	/*
	 * Quoting the specification:
	 * "The GHID method can return a BYTE, WORD, or DWORD.
	 *  The value must be encoded in little-endian byte
	 *  order (least significant byte first)."
	 */
	switch (obj->buffer.length) {
	case 1:
		data->id = obj->buffer.pointer[0];
		break;
	case 2:
		data->id = get_unaligned_le16(obj->buffer.pointer);
		break;
	case 4:
		data->id = get_unaligned_le32(obj->buffer.pointer);
		break;
	default:
		dev_err(data->dev,
			FW_BUG "GHID method returned buffer of unexpected length %u\n",
			obj->buffer.length);
		ret = -EIO;
		break;
	}

	kfree(obj);

	return ret;
}

static void quickstart_notify_remove(void *context)
{
	struct quickstart_data *data = context;
	acpi_handle handle;

	handle = ACPI_HANDLE(data->dev);

	acpi_remove_notify_handler(handle, ACPI_DEVICE_NOTIFY, quickstart_notify);
}

static void quickstart_mutex_destroy(void *data)
{
	struct mutex *lock = data;

	mutex_destroy(lock);
}

static int quickstart_probe(struct platform_device *pdev)
{
	struct quickstart_data *data;
	acpi_handle handle;
	acpi_status status;
	int ret;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, data);

	mutex_init(&data->input_lock);
	ret = devm_add_action_or_reset(&pdev->dev, quickstart_mutex_destroy, &data->input_lock);
	if (ret < 0)
		return ret;

	/*
	 * We have to initialize the device wakeup before evaluating GHID because
	 * doing so will notify the device if the button was used to wake the machine
	 * from S5.
	 */
	device_init_wakeup(&pdev->dev, true);

	ret = quickstart_get_ghid(data);
	if (ret < 0)
		return ret;

	data->input_device = devm_input_allocate_device(&pdev->dev);
	if (!data->input_device)
		return -ENOMEM;

	ret = sparse_keymap_setup(data->input_device, quickstart_keymap, NULL);
	if (ret < 0)
		return ret;

	snprintf(data->input_name, sizeof(data->input_name), "Quickstart Button %u", data->id);
	snprintf(data->phys, sizeof(data->phys), DRIVER_NAME "/input%u", data->id);

	data->input_device->name = data->input_name;
	data->input_device->phys = data->phys;
	data->input_device->id.bustype = BUS_HOST;

	ret = input_register_device(data->input_device);
	if (ret < 0)
		return ret;

	status = acpi_install_notify_handler(handle, ACPI_DEVICE_NOTIFY, quickstart_notify, data);
	if (ACPI_FAILURE(status))
		return -EIO;

	return devm_add_action_or_reset(&pdev->dev, quickstart_notify_remove, data);
}

static const struct acpi_device_id quickstart_device_ids[] = {
	{ "PNP0C32" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, quickstart_device_ids);

static struct platform_driver quickstart_platform_driver = {
	.driver	= {
		.name = DRIVER_NAME,
		.dev_groups = quickstart_groups,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.acpi_match_table = quickstart_device_ids,
	},
	.probe = quickstart_probe,
};
module_platform_driver(quickstart_platform_driver);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_AUTHOR("Arvid Norlander <lkml@vorpal.se>");
MODULE_AUTHOR("Angelo Arrifano");
MODULE_DESCRIPTION("ACPI Direct App Launch driver");
MODULE_LICENSE("GPL");
