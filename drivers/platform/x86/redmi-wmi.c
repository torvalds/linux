// SPDX-License-Identifier: GPL-2.0
/* WMI driver for Xiaomi Redmibooks */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include <uapi/linux/input-event-codes.h>

#define WMI_REDMIBOOK_KEYBOARD_EVENT_GUID "46C93E13-EE9B-4262-8488-563BCA757FEF"

#define AI_KEY_VALUE_MASK BIT(8)

static const struct key_entry redmi_wmi_keymap[] = {
	{KE_KEY, 0x00000201,	{KEY_SELECTIVE_SCREENSHOT}},
	{KE_KEY, 0x00000301,	{KEY_ALL_APPLICATIONS}},
	{KE_KEY, 0x00001b01,	{KEY_SETUP}},

	/* AI button has code for each position */
	{KE_KEY, 0x00011801,	{KEY_ASSISTANT}},
	{KE_KEY, 0x00011901,	{KEY_ASSISTANT}},

	/* Keyboard backlight */
	{KE_IGNORE, 0x00000501, {}},
	{KE_IGNORE, 0x00800501, {}},
	{KE_IGNORE, 0x00050501, {}},
	{KE_IGNORE, 0x000a0501, {}},

	{KE_END}
};

struct redmi_wmi {
	struct input_dev *input_dev;
	/* Protects the key event sequence */
	struct mutex key_lock;
};

static int redmi_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct redmi_wmi *data;
	int err;

	/* Init dev */
	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);

	err = devm_mutex_init(&wdev->dev, &data->key_lock);
	if (err)
		return err;

	data->input_dev = devm_input_allocate_device(&wdev->dev);
	if (!data->input_dev)
		return -ENOMEM;

	data->input_dev->name = "Redmibook WMI keys";
	data->input_dev->phys = "wmi/input0";

	err = sparse_keymap_setup(data->input_dev, redmi_wmi_keymap, NULL);
	if (err)
		return err;

	return input_register_device(data->input_dev);
}

static void redmi_wmi_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct key_entry *entry;
	struct redmi_wmi *data = dev_get_drvdata(&wdev->dev);
	bool autorelease = true;
	u32 payload;
	int value = 1;

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Bad response type %u\n", obj->type);
		return;
	}

	if (obj->buffer.length < 32) {
		dev_err(&wdev->dev, "Invalid buffer length %u\n", obj->buffer.length);
		return;
	}

	payload = get_unaligned_le32(obj->buffer.pointer);
	entry = sparse_keymap_entry_from_scancode(data->input_dev, payload);

	if (!entry) {
		dev_dbg(&wdev->dev, "Unknown WMI event with payload %u", payload);
		return;
	}

	/* AI key quirk */
	if (entry->keycode == KEY_ASSISTANT) {
		value = !(payload & AI_KEY_VALUE_MASK);
		autorelease = false;
	}

	guard(mutex)(&data->key_lock);
	sparse_keymap_report_entry(data->input_dev, entry, value, autorelease);
}

static const struct wmi_device_id redmi_wmi_id_table[] = {
	{ WMI_REDMIBOOK_KEYBOARD_EVENT_GUID, NULL },
	{ }
};

static struct wmi_driver redmi_wmi_driver = {
	.driver = {
		.name = "redmi-wmi",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = redmi_wmi_id_table,
	.probe = redmi_wmi_probe,
	.notify = redmi_wmi_notify,
	.no_singleton = true,
};
module_wmi_driver(redmi_wmi_driver);

MODULE_DEVICE_TABLE(wmi, redmi_wmi_id_table);
MODULE_AUTHOR("Gladyshev Ilya <foxido@foxido.dev>");
MODULE_DESCRIPTION("Redmibook WMI driver");
MODULE_LICENSE("GPL");
