// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI INT3403 thermal driver
 * Copyright (c) 2013, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include "int340x_thermal_zone.h"

#define INT3403_TYPE_SENSOR		0x03
#define INT3403_TYPE_CHARGER		0x0B
#define INT3403_TYPE_BATTERY		0x0C
#define INT3403_PERF_CHANGED_EVENT	0x80
#define INT3403_PERF_TRIP_POINT_CHANGED	0x81
#define INT3403_THERMAL_EVENT		0x90

/* Preserved structure for future expandbility */
struct int3403_sensor {
	struct int34x_thermal_zone *int340x_zone;
};

struct int3403_performance_state {
	u64 performance;
	u64 power;
	u64 latency;
	u64 linear;
	u64 control;
	u64 raw_performace;
	char *raw_unit;
	int reserved;
};

struct int3403_cdev {
	struct thermal_cooling_device *cdev;
	unsigned long max_state;
};

struct int3403_priv {
	struct platform_device *pdev;
	struct acpi_device *adev;
	unsigned long long type;
	void *priv;
};

static void int3403_notify(acpi_handle handle,
		u32 event, void *data)
{
	struct int3403_priv *priv = data;
	struct int3403_sensor *obj;

	if (!priv)
		return;

	obj = priv->priv;
	if (priv->type != INT3403_TYPE_SENSOR || !obj)
		return;

	switch (event) {
	case INT3403_PERF_CHANGED_EVENT:
		break;
	case INT3403_THERMAL_EVENT:
		int340x_thermal_zone_device_update(obj->int340x_zone,
						   THERMAL_TRIP_VIOLATED);
		break;
	case INT3403_PERF_TRIP_POINT_CHANGED:
		int340x_thermal_update_trips(obj->int340x_zone);
		int340x_thermal_zone_device_update(obj->int340x_zone,
						   THERMAL_TRIP_CHANGED);
		break;
	default:
		dev_dbg(&priv->pdev->dev, "Unsupported event [0x%x]\n", event);
		break;
	}
}

static int int3403_sensor_add(struct int3403_priv *priv)
{
	int result = 0;
	struct int3403_sensor *obj;

	obj = devm_kzalloc(&priv->pdev->dev, sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	priv->priv = obj;

	obj->int340x_zone = int340x_thermal_zone_add(priv->adev, NULL);
	if (IS_ERR(obj->int340x_zone))
		return PTR_ERR(obj->int340x_zone);

	result = acpi_install_notify_handler(priv->adev->handle,
			ACPI_DEVICE_NOTIFY, int3403_notify,
			(void *)priv);
	if (result)
		goto err_free_obj;

	return 0;

 err_free_obj:
	int340x_thermal_zone_remove(obj->int340x_zone);
	return result;
}

static int int3403_sensor_remove(struct int3403_priv *priv)
{
	struct int3403_sensor *obj = priv->priv;

	acpi_remove_notify_handler(priv->adev->handle,
				   ACPI_DEVICE_NOTIFY, int3403_notify);
	int340x_thermal_zone_remove(obj->int340x_zone);

	return 0;
}

/* INT3403 Cooling devices */
static int int3403_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct int3403_priv *priv = cdev->devdata;
	struct int3403_cdev *obj = priv->priv;

	*state = obj->max_state;
	return 0;
}

static int int3403_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct int3403_priv *priv = cdev->devdata;
	unsigned long long level;
	acpi_status status;

	status = acpi_evaluate_integer(priv->adev->handle, "PPPC", NULL, &level);
	if (ACPI_SUCCESS(status)) {
		*state = level;
		return 0;
	} else
		return -EINVAL;
}

static int
int3403_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct int3403_priv *priv = cdev->devdata;
	acpi_status status;

	status = acpi_execute_simple_method(priv->adev->handle, "SPPC", state);
	if (ACPI_SUCCESS(status))
		return 0;
	else
		return -EINVAL;
}

static const struct thermal_cooling_device_ops int3403_cooling_ops = {
	.get_max_state = int3403_get_max_state,
	.get_cur_state = int3403_get_cur_state,
	.set_cur_state = int3403_set_cur_state,
};

static int int3403_cdev_add(struct int3403_priv *priv)
{
	int result = 0;
	acpi_status status;
	struct int3403_cdev *obj;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *p;

	obj = devm_kzalloc(&priv->pdev->dev, sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	status = acpi_evaluate_object(priv->adev->handle, "PPSS", NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buf.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		pr_warn("Invalid PPSS data\n");
		kfree(buf.pointer);
		return -EFAULT;
	}

	priv->priv = obj;
	obj->max_state = p->package.count - 1;
	obj->cdev =
		thermal_cooling_device_register(acpi_device_bid(priv->adev),
				priv, &int3403_cooling_ops);
	if (IS_ERR(obj->cdev))
		result = PTR_ERR(obj->cdev);

	kfree(buf.pointer);
	/* TODO: add ACPI notification support */

	return result;
}

static int int3403_cdev_remove(struct int3403_priv *priv)
{
	struct int3403_cdev *obj = priv->priv;

	thermal_cooling_device_unregister(obj->cdev);
	return 0;
}

static int int3403_add(struct platform_device *pdev)
{
	struct int3403_priv *priv;
	int result = 0;
	unsigned long long tmp;
	acpi_status status;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct int3403_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->adev = ACPI_COMPANION(&(pdev->dev));
	if (!priv->adev) {
		result = -EINVAL;
		goto err;
	}


	status = acpi_evaluate_integer(priv->adev->handle, "_TMP",
				       NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		status = acpi_evaluate_integer(priv->adev->handle, "PTYP",
				       NULL, &priv->type);
		if (ACPI_FAILURE(status)) {
			result = -EINVAL;
			goto err;
		}
	} else {
		priv->type = INT3403_TYPE_SENSOR;
	}

	platform_set_drvdata(pdev, priv);
	switch (priv->type) {
	case INT3403_TYPE_SENSOR:
		result = int3403_sensor_add(priv);
		break;
	case INT3403_TYPE_CHARGER:
	case INT3403_TYPE_BATTERY:
		result = int3403_cdev_add(priv);
		break;
	default:
		result = -EINVAL;
	}

	if (result)
		goto err;
	return result;

err:
	return result;
}

static void int3403_remove(struct platform_device *pdev)
{
	struct int3403_priv *priv = platform_get_drvdata(pdev);

	switch (priv->type) {
	case INT3403_TYPE_SENSOR:
		int3403_sensor_remove(priv);
		break;
	case INT3403_TYPE_CHARGER:
	case INT3403_TYPE_BATTERY:
		int3403_cdev_remove(priv);
		break;
	default:
		break;
	}
}

static const struct acpi_device_id int3403_device_ids[] = {
	{"INT3403", 0},
	{"INTC1043", 0},
	{"INTC1046", 0},
	{"INTC1062", 0},
	{"INTC1069", 0},
	{"INTC10A1", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, int3403_device_ids);

static struct platform_driver int3403_driver = {
	.probe = int3403_add,
	.remove_new = int3403_remove,
	.driver = {
		.name = "int3403 thermal",
		.acpi_match_table = int3403_device_ids,
	},
};

module_platform_driver(int3403_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ACPI INT3403 thermal driver");
