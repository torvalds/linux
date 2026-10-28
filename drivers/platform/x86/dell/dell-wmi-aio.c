// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  WMI hotkeys support for Dell All-In-One series
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/string.h>
#include <linux/wmi.h>

MODULE_DESCRIPTION("WMI hotkeys driver for Dell All-In-One series");
MODULE_LICENSE("GPL");

#define EVENT_GUID1 "284A0E6B-380E-472A-921F-E52786257FB4"
#define EVENT_GUID2 "02314822-307C-4F66-BF0E-48AEAEB26CC8"

struct dell_wmi_aio_data {
	struct input_dev *input_device;
	/* Protects the input sequence */
	struct mutex input_lock;
};

/* A hot key was pressed or an event occurred */
#define DELL_AIO_EVENT_TYPE_SINGLE	0x0000
/* A sequence of hot keys was pressed */
#define DELL_AIO_EVENT_TYPE_SEQUENCE	0x000F

struct dell_wmi_event {
	__le16	length;
	__le16	type;
	__le16	event[];
} __packed;

static const struct key_entry dell_wmi_aio_keymap[] = {
	{ KE_KEY, 0xc0, { KEY_VOLUMEUP } },
	{ KE_KEY, 0xc1, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0xe030, { KEY_VOLUMEUP } },
	{ KE_KEY, 0xe02e, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0xe020, { KEY_MUTE } },
	{ KE_KEY, 0xe027, { KEY_DISPLAYTOGGLE } },
	{ KE_KEY, 0xe006, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xe005, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0xe00b, { KEY_SWITCHVIDEOMODE } },
	{ KE_END, 0 }
};

/*
 * The new WMI event data format will follow the dell_wmi_event structure
 * So, we will check if the buffer matches the format
 */
static bool dell_wmi_aio_event_check(const struct wmi_buffer *buffer)
{
	struct dell_wmi_event *event;
	u16 length, type;

	if (buffer->length < struct_size(event, event, 1))
		return false;

	event = buffer->data;
	length = le16_to_cpu(event->length);
	type = le16_to_cpu(event->type);

	if (type != DELL_AIO_EVENT_TYPE_SINGLE && type != DELL_AIO_EVENT_TYPE_SEQUENCE)
		return false;

	if (length < 2)
		return false;

	return true;
}

static void dell_wmi_aio_notify(struct wmi_device *wdev, const struct wmi_buffer *data)
{
	struct dell_wmi_aio_data *drvdata = dev_get_drvdata(&wdev->dev);
	const struct dell_wmi_event *new_event;
	unsigned int scancode;
	const u8 *old_event;

	if (dell_wmi_aio_event_check(data)) {
		new_event = data->data;
		scancode = le16_to_cpu(new_event->event[0]);
	} else {
		old_event = data->data;
		scancode = old_event[0];
	}

	guard(mutex)(&drvdata->input_lock);

	sparse_keymap_report_event(drvdata->input_device, scancode, 1, true);
}

static int dell_wmi_aio_probe(struct wmi_device *wdev, const void *context)
{
	struct dell_wmi_aio_data *data;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);
	ret = devm_mutex_init(&wdev->dev, &data->input_lock);
	if (ret < 0)
		return ret;

	data->input_device = devm_input_allocate_device(&wdev->dev);
	if (!data->input_device)
		return -ENOMEM;

	data->input_device->name = "Dell AIO WMI hotkeys";
	data->input_device->phys = "wmi/input0";
	data->input_device->id.bustype = BUS_HOST;

	ret = sparse_keymap_setup(data->input_device, dell_wmi_aio_keymap, NULL);
	if (ret < 0)
		return ret;

	return input_register_device(data->input_device);
}

static const struct wmi_device_id dell_wmi_aio_id_table[] = {
	{ EVENT_GUID1, NULL },
	{ EVENT_GUID2, NULL },
	{ }
};
MODULE_DEVICE_TABLE(wmi, dell_wmi_aio_id_table);

static struct wmi_driver dell_wmi_aio_driver = {
	.driver = {
		.name = "dell-wmi-aio",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = dell_wmi_aio_id_table,
	.probe = dell_wmi_aio_probe,
	.notify_new = dell_wmi_aio_notify,
	.min_event_size = sizeof(u8),
	.no_singleton = true,
};
module_wmi_driver(dell_wmi_aio_driver);
