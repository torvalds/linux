// SPDX-License-Identifier: GPL-2.0
/*
 * Lenovo WMI Camera Button Driver
 *
 * Author: Ai Chao <aichao@kylinos.cn>
 * Copyright (C) 2024 KylinSoft Corporation.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/wmi.h>
#include <linux/cleanup.h>

#define WMI_LENOVO_CAMERABUTTON_EVENT_GUID "50C76F1F-D8E4-D895-0A3D-62F4EA400013"

struct lenovo_wmi_priv {
	struct input_dev *idev;
	struct mutex notify_lock;	/* lenovo WMI camera button notify lock */
};

enum {
	SW_CAMERA_OFF	= 0,
	SW_CAMERA_ON	= 1,
};

static int camera_shutter_input_setup(struct wmi_device *wdev, u8 camera_mode)
{
	struct lenovo_wmi_priv *priv = dev_get_drvdata(&wdev->dev);
	int err;

	priv->idev = input_allocate_device();
	if (!priv->idev)
		return -ENOMEM;

	priv->idev->name = "Lenovo WMI Camera Button";
	priv->idev->phys = "wmi/input0";
	priv->idev->id.bustype = BUS_HOST;
	priv->idev->dev.parent = &wdev->dev;

	input_set_capability(priv->idev, EV_SW, SW_CAMERA_LENS_COVER);

	input_report_switch(priv->idev, SW_CAMERA_LENS_COVER,
			    camera_mode == SW_CAMERA_ON ? 0 : 1);
	input_sync(priv->idev);

	err = input_register_device(priv->idev);
	if (err) {
		input_free_device(priv->idev);
		priv->idev = NULL;
	}

	return err;
}

static void lenovo_wmi_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct lenovo_wmi_priv *priv = dev_get_drvdata(&wdev->dev);
	u8 camera_mode;

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Bad response type %u\n", obj->type);
		return;
	}

	if (obj->buffer.length != 1) {
		dev_err(&wdev->dev, "Invalid buffer length %u\n", obj->buffer.length);
		return;
	}

	/*
	 * obj->buffer.pointer[0] is camera mode:
	 *      0 camera close
	 *      1 camera open
	 */
	camera_mode = obj->buffer.pointer[0];
	if (camera_mode > SW_CAMERA_ON) {
		dev_err(&wdev->dev, "Unknown camera mode %u\n", camera_mode);
		return;
	}

	guard(mutex)(&priv->notify_lock);

	if (!priv->idev) {
		if (camera_shutter_input_setup(wdev, camera_mode))
			dev_warn(&wdev->dev, "Failed to register input device\n");
		return;
	}

	if (camera_mode == SW_CAMERA_ON)
		input_report_switch(priv->idev, SW_CAMERA_LENS_COVER, 0);
	else
		input_report_switch(priv->idev, SW_CAMERA_LENS_COVER, 1);
	input_sync(priv->idev);
}

static int lenovo_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct lenovo_wmi_priv *priv;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, priv);

	mutex_init(&priv->notify_lock);

	return 0;
}

static void lenovo_wmi_remove(struct wmi_device *wdev)
{
	struct lenovo_wmi_priv *priv = dev_get_drvdata(&wdev->dev);

	if (priv->idev)
		input_unregister_device(priv->idev);

	mutex_destroy(&priv->notify_lock);
}

static const struct wmi_device_id lenovo_wmi_id_table[] = {
	{ .guid_string = WMI_LENOVO_CAMERABUTTON_EVENT_GUID },
	{  }
};
MODULE_DEVICE_TABLE(wmi, lenovo_wmi_id_table);

static struct wmi_driver lenovo_wmi_driver = {
	.driver = {
		.name = "lenovo-wmi-camera",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = lenovo_wmi_id_table,
	.no_singleton = true,
	.probe = lenovo_wmi_probe,
	.notify = lenovo_wmi_notify,
	.remove = lenovo_wmi_remove,
};
module_wmi_driver(lenovo_wmi_driver);

MODULE_AUTHOR("Ai Chao <aichao@kylinos.cn>");
MODULE_DESCRIPTION("Lenovo WMI Camera Button Driver");
MODULE_LICENSE("GPL");
