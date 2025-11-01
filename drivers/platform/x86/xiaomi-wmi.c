// SPDX-License-Identifier: GPL-2.0
/* WMI driver for Xiaomi Laptops */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/wmi.h>

#include <uapi/linux/input-event-codes.h>

#define XIAOMI_KEY_FN_ESC_0	"A2095CCE-0491-44E7-BA27-F8ED8F88AA86"
#define XIAOMI_KEY_FN_ESC_1	"7BBE8E39-B486-473D-BA13-66F75C5805CD"
#define XIAOMI_KEY_FN_FN	"409B028D-F06B-4C7C-8BBB-EE133A6BD87E"
#define XIAOMI_KEY_CAPSLOCK	"83FE7607-053A-4644-822A-21532C621FC7"
#define XIAOMI_KEY_FN_F7	"76E9027C-95D0-4180-8692-DA6747DD1C2D"

#define XIAOMI_DEVICE(guid, key)		\
	.guid_string = (guid),			\
	.context = &(const unsigned int){key}

struct xiaomi_wmi {
	struct input_dev *input_dev;
	struct mutex key_lock;	/* Protects the key event sequence */
	unsigned int key_code;
};

static int xiaomi_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct xiaomi_wmi *data;
	int ret;

	if (!context)
		return -EINVAL;

	data = devm_kzalloc(&wdev->dev, sizeof(struct xiaomi_wmi), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	dev_set_drvdata(&wdev->dev, data);

	ret = devm_mutex_init(&wdev->dev, &data->key_lock);
	if (ret < 0)
		return ret;

	data->input_dev = devm_input_allocate_device(&wdev->dev);
	if (data->input_dev == NULL)
		return -ENOMEM;
	data->input_dev->name = "Xiaomi WMI keys";
	data->input_dev->phys = "wmi/input0";

	data->key_code = *((const unsigned int *)context);
	set_bit(EV_KEY, data->input_dev->evbit);
	set_bit(data->key_code, data->input_dev->keybit);

	return input_register_device(data->input_dev);
}

static void xiaomi_wmi_notify(struct wmi_device *wdev, union acpi_object *dummy)
{
	struct xiaomi_wmi *data = dev_get_drvdata(&wdev->dev);

	mutex_lock(&data->key_lock);
	input_report_key(data->input_dev, data->key_code, 1);
	input_sync(data->input_dev);
	input_report_key(data->input_dev, data->key_code, 0);
	input_sync(data->input_dev);
	mutex_unlock(&data->key_lock);
}

static const struct wmi_device_id xiaomi_wmi_id_table[] = {
	// { XIAOMI_DEVICE(XIAOMI_KEY_FN_ESC_0, KEY_FN_ESC) },
	// { XIAOMI_DEVICE(XIAOMI_KEY_FN_ESC_1, KEY_FN_ESC) },
	{ XIAOMI_DEVICE(XIAOMI_KEY_FN_FN, KEY_PROG1) },
	// { XIAOMI_DEVICE(XIAOMI_KEY_CAPSLOCK, KEY_CAPSLOCK) },
	{ XIAOMI_DEVICE(XIAOMI_KEY_FN_F7, KEY_CUT) },

	/* Terminating entry */
	{ }
};

static struct wmi_driver xiaomi_wmi_driver = {
	.driver = {
		.name = "xiaomi-wmi",
	},
	.id_table = xiaomi_wmi_id_table,
	.probe = xiaomi_wmi_probe,
	.notify = xiaomi_wmi_notify,
	.no_singleton = true,
};
module_wmi_driver(xiaomi_wmi_driver);

MODULE_DEVICE_TABLE(wmi, xiaomi_wmi_id_table);
MODULE_AUTHOR("Mattias Jacobsson");
MODULE_DESCRIPTION("Xiaomi WMI driver");
MODULE_LICENSE("GPL v2");
