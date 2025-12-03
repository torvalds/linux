// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fan_core.c - ACPI Fan core Driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2022 Intel Corporation. All rights reserved.
 */

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uuid.h>
#include <linux/thermal.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#include "fan.h"

#define ACPI_FAN_NOTIFY_STATE_CHANGED	0x80

/*
 * Defined inside the "Fan Noise Signal" section at
 * https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/design-guide.
 */
static const guid_t acpi_fan_microsoft_guid = GUID_INIT(0xA7611840, 0x99FE, 0x41AE, 0xA4, 0x88,
							0x35, 0xC7, 0x59, 0x26, 0xC8, 0xEB);
#define ACPI_FAN_DSM_GET_TRIP_POINT_GRANULARITY 1
#define ACPI_FAN_DSM_SET_TRIP_POINTS		2
#define ACPI_FAN_DSM_GET_OPERATING_RANGES	3

/*
 * Ensures that fans with a very low trip point granularity
 * do not send too many notifications.
 */
static uint min_trip_distance = 100;
module_param(min_trip_distance, uint, 0);
MODULE_PARM_DESC(min_trip_distance, "Minimum distance between fan speed trip points in RPM");

static const struct acpi_device_id fan_device_ids[] = {
	ACPI_FAN_DEVICE_IDS,
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fan_device_ids);

/* thermal cooling device callbacks */
static int fan_get_max_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4) {
		if (fan->fif.fine_grain_ctrl)
			*state = 100 / fan->fif.step_size;
		else
			*state = fan->fps_count - 1;
	} else {
		*state = 1;
	}

	return 0;
}

int acpi_fan_get_fst(acpi_handle handle, struct acpi_fan_fst *fst)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	status = acpi_evaluate_object(handle, "_FST", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = buffer.pointer;
	if (!obj)
		return -ENODATA;

	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 3) {
		ret = -EPROTO;
		goto err;
	}

	if (obj->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    obj->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    obj->package.elements[2].type != ACPI_TYPE_INTEGER) {
		ret = -EPROTO;
		goto err;
	}

	fst->revision = obj->package.elements[0].integer.value;
	fst->control = obj->package.elements[1].integer.value;
	fst->speed = obj->package.elements[2].integer.value;

err:
	kfree(obj);
	return ret;
}

static int fan_get_state_acpi4(struct acpi_device *device, unsigned long *state)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_fan_fst fst;
	int status, i;

	status = acpi_fan_get_fst(device->handle, &fst);
	if (status)
		return status;

	if (fan->fif.fine_grain_ctrl) {
		/* This control should be same what we set using _FSL by spec */
		if (fst.control > 100) {
			dev_dbg(&device->dev, "Invalid control value returned\n");
			goto match_fps;
		}

		*state = (int) fst.control / fan->fif.step_size;
		return 0;
	}

match_fps:
	for (i = 0; i < fan->fps_count; i++) {
		if (fst.control == fan->fps[i].control)
			break;
	}
	if (i == fan->fps_count) {
		dev_dbg(&device->dev, "No matching fps control value\n");
		return -EINVAL;
	}

	*state = i;

	return status;
}

static int fan_get_state(struct acpi_device *device, unsigned long *state)
{
	int result;
	int acpi_state = ACPI_STATE_D0;

	result = acpi_device_update_power(device, &acpi_state);
	if (result)
		return result;

	*state = acpi_state == ACPI_STATE_D3_COLD
			|| acpi_state == ACPI_STATE_D3_HOT ?
		0 : (acpi_state == ACPI_STATE_D0 ? 1 : -1);
	return 0;
}

static int fan_get_cur_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4)
		return fan_get_state_acpi4(device, state);
	else
		return fan_get_state(device, state);
}

static int fan_set_state(struct acpi_device *device, unsigned long state)
{
	if (state != 0 && state != 1)
		return -EINVAL;

	return acpi_device_set_power(device,
				     state ? ACPI_STATE_D0 : ACPI_STATE_D3_COLD);
}

static int fan_set_state_acpi4(struct acpi_device *device, unsigned long state)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	acpi_status status;
	u64 value = state;
	int max_state;

	if (fan->fif.fine_grain_ctrl)
		max_state = 100 / fan->fif.step_size;
	else
		max_state = fan->fps_count - 1;

	if (state > max_state)
		return -EINVAL;

	if (fan->fif.fine_grain_ctrl) {
		value *= fan->fif.step_size;
		/* Spec allows compensate the last step only */
		if (value + fan->fif.step_size > 100)
			value = 100;
	} else {
		value = fan->fps[state].control;
	}

	status = acpi_execute_simple_method(device->handle, "_FSL", value);
	if (ACPI_FAILURE(status)) {
		dev_dbg(&device->dev, "Failed to set state by _FSL\n");
		return -ENODEV;
	}

	return 0;
}

static int
fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4)
		return fan_set_state_acpi4(device, state);
	else
		return fan_set_state(device, state);
}

static const struct thermal_cooling_device_ops fan_cooling_ops = {
	.get_max_state = fan_get_max_state,
	.get_cur_state = fan_get_cur_state,
	.set_cur_state = fan_set_cur_state,
};

/* --------------------------------------------------------------------------
 *                               Driver Interface
 * --------------------------------------------------------------------------
*/

static int acpi_fan_get_fif(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_buffer format = { sizeof("NNNN"), "NNNN" };
	u64 fields[4];
	struct acpi_buffer fif = { sizeof(fields), fields };
	union acpi_object *obj;
	acpi_status status;

	status = acpi_evaluate_object(device->handle, "_FIF", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE) {
		dev_err(&device->dev, "Invalid _FIF data\n");
		status = -EINVAL;
		goto err;
	}

	status = acpi_extract_package(obj, &format, &fif);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Invalid _FIF element\n");
		status = -EINVAL;
		goto err;
	}

	fan->fif.revision = fields[0];
	fan->fif.fine_grain_ctrl = fields[1];
	fan->fif.step_size = fields[2];
	fan->fif.low_speed_notification = fields[3];

	/* If there is a bug in step size and set as 0, change to 1 */
	if (!fan->fif.step_size)
		fan->fif.step_size = 1;
	/* If step size > 9, change to 9 (by spec valid values 1-9) */
	else if (fan->fif.step_size > 9)
		fan->fif.step_size = 9;
err:
	kfree(obj);
	return status;
}

static int acpi_fan_speed_cmp(const void *a, const void *b)
{
	const struct acpi_fan_fps *fps1 = a;
	const struct acpi_fan_fps *fps2 = b;
	return fps1->speed - fps2->speed;
}

static int acpi_fan_get_fps(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int i;

	status = acpi_evaluate_object(device->handle, "_FPS", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE || obj->package.count < 2) {
		dev_err(&device->dev, "Invalid _FPS data\n");
		status = -EINVAL;
		goto err;
	}

	fan->fps_count = obj->package.count - 1; /* minus revision field */
	fan->fps = devm_kcalloc(&device->dev,
				fan->fps_count, sizeof(struct acpi_fan_fps),
				GFP_KERNEL);
	if (!fan->fps) {
		dev_err(&device->dev, "Not enough memory\n");
		status = -ENOMEM;
		goto err;
	}
	for (i = 0; i < fan->fps_count; i++) {
		struct acpi_buffer format = { sizeof("NNNNN"), "NNNNN" };
		struct acpi_buffer fps = { offsetof(struct acpi_fan_fps, name),
						&fan->fps[i] };
		status = acpi_extract_package(&obj->package.elements[i + 1],
					      &format, &fps);
		if (ACPI_FAILURE(status)) {
			dev_err(&device->dev, "Invalid _FPS element\n");
			goto err;
		}
	}

	/* sort the state array according to fan speed in increase order */
	sort(fan->fps, fan->fps_count, sizeof(*fan->fps),
	     acpi_fan_speed_cmp, NULL);

err:
	kfree(obj);
	return status;
}

static int acpi_fan_dsm_init(struct device *dev)
{
	union acpi_object dummy = {
		.package = {
			.type = ACPI_TYPE_PACKAGE,
			.count = 0,
			.elements = NULL,
		},
	};
	struct acpi_fan *fan = dev_get_drvdata(dev);
	union acpi_object *obj;
	int ret = 0;

	if (!acpi_check_dsm(fan->handle, &acpi_fan_microsoft_guid, 0,
			    BIT(ACPI_FAN_DSM_GET_TRIP_POINT_GRANULARITY) |
			    BIT(ACPI_FAN_DSM_SET_TRIP_POINTS)))
		return 0;

	dev_info(dev, "Using Microsoft fan extensions\n");

	obj = acpi_evaluate_dsm_typed(fan->handle, &acpi_fan_microsoft_guid, 0,
				      ACPI_FAN_DSM_GET_TRIP_POINT_GRANULARITY, &dummy,
				      ACPI_TYPE_INTEGER);
	if (!obj)
		return -EIO;

	if (obj->integer.value > U32_MAX)
		ret = -EOVERFLOW;
	else
		fan->fan_trip_granularity = obj->integer.value;

	kfree(obj);

	return ret;
}

static int acpi_fan_dsm_set_trip_points(struct device *dev, u64 upper, u64 lower)
{
	union acpi_object args[2] = {
		{
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = lower,
			},
		},
		{
			.integer = {
				.type = ACPI_TYPE_INTEGER,
				.value = upper,
			},
		},
	};
	struct acpi_fan *fan = dev_get_drvdata(dev);
	union acpi_object in = {
		.package = {
			.type = ACPI_TYPE_PACKAGE,
			.count = ARRAY_SIZE(args),
			.elements = args,
		},
	};
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(fan->handle, &acpi_fan_microsoft_guid, 0,
				ACPI_FAN_DSM_SET_TRIP_POINTS, &in);
	kfree(obj);

	return 0;
}

static int acpi_fan_dsm_start(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	int ret;

	if (!fan->fan_trip_granularity)
		return 0;

	/*
	 * Some firmware implementations only update the values returned by the
	 * _FST control method when a notification is received. This usually
	 * works with Microsoft Windows as setting up trip points will keep
	 * triggering said notifications, but will cause issues when using _FST
	 * without the Microsoft-specific trip point extension.
	 *
	 * Because of this, an initial notification needs to be triggered to
	 * start the cycle of trip points updates. This is achieved by setting
	 * the trip points sequencially to two separate ranges. As by the
	 * Microsoft specification the firmware should trigger a notification
	 * immediately if the fan speed is outside the trip point range. This
	 * _should_ result in at least one notification as both ranges do not
	 * overlap, meaning that the current fan speed needs to be outside at
	 * least one range.
	 */
	ret = acpi_fan_dsm_set_trip_points(dev, fan->fan_trip_granularity, 0);
	if (ret < 0)
		return ret;

	return acpi_fan_dsm_set_trip_points(dev, fan->fan_trip_granularity * 3,
					    fan->fan_trip_granularity * 2);
}

static int acpi_fan_dsm_update_trips_points(struct device *dev, struct acpi_fan_fst *fst)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	u64 upper, lower;

	if (!fan->fan_trip_granularity)
		return 0;

	if (!acpi_fan_speed_valid(fst->speed))
		return -EINVAL;

	upper = roundup_u64(fst->speed + min_trip_distance, fan->fan_trip_granularity);
	if (fst->speed <= min_trip_distance) {
		lower = 0;
	} else {
		/*
		 * Valid fan speed values cannot be larger than 32 bit, so
		 * we can safely assume that no overflow will happen here.
		 */
		lower = rounddown((u32)fst->speed - min_trip_distance, fan->fan_trip_granularity);
	}

	return acpi_fan_dsm_set_trip_points(dev, upper, lower);
}

static void acpi_fan_notify_handler(acpi_handle handle, u32 event, void *context)
{
	struct device *dev = context;
	struct acpi_fan_fst fst;
	int ret;

	switch (event) {
	case ACPI_FAN_NOTIFY_STATE_CHANGED:
		/*
		 * The ACPI specification says that we must evaluate _FST when we
		 * receive an ACPI event indicating that the fan state has changed.
		 */
		ret = acpi_fan_get_fst(handle, &fst);
		if (ret < 0) {
			dev_err(dev, "Error retrieving current fan status: %d\n", ret);
		} else {
			ret = acpi_fan_dsm_update_trips_points(dev, &fst);
			if (ret < 0)
				dev_err(dev, "Failed to update trip points: %d\n", ret);
		}

		acpi_fan_notify_hwmon(dev);
		acpi_bus_generate_netlink_event("fan", dev_name(dev), event, 0);
		break;
	default:
		dev_dbg(dev, "Unsupported ACPI notification 0x%x\n", event);
		break;
	}
}

static void acpi_fan_notify_remove(void *data)
{
	struct acpi_fan *fan = data;

	acpi_remove_notify_handler(fan->handle, ACPI_DEVICE_NOTIFY, acpi_fan_notify_handler);
}

static int devm_acpi_fan_notify_init(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	acpi_status status;

	status = acpi_install_notify_handler(fan->handle, ACPI_DEVICE_NOTIFY,
					     acpi_fan_notify_handler, dev);
	if (ACPI_FAILURE(status))
		return -EIO;

	return devm_add_action_or_reset(dev, acpi_fan_notify_remove, fan);
}

static int acpi_fan_probe(struct platform_device *pdev)
{
	int result = 0;
	struct thermal_cooling_device *cdev;
	struct acpi_fan *fan;
	struct acpi_device *device = ACPI_COMPANION(&pdev->dev);
	char *name;

	if (!device)
		return -ENODEV;

	fan = devm_kzalloc(&pdev->dev, sizeof(*fan), GFP_KERNEL);
	if (!fan) {
		dev_err(&device->dev, "No memory for fan\n");
		return -ENOMEM;
	}

	fan->handle = device->handle;
	device->driver_data = fan;
	platform_set_drvdata(pdev, fan);

	if (acpi_has_method(device->handle, "_FST")) {
		fan->has_fst = true;
		fan->acpi4 = acpi_has_method(device->handle, "_FIF") &&
				acpi_has_method(device->handle, "_FPS") &&
				acpi_has_method(device->handle, "_FSL");
	}

	if (fan->acpi4) {
		result = acpi_fan_get_fif(device);
		if (result)
			return result;

		result = acpi_fan_get_fps(device);
		if (result)
			return result;
	}

	if (fan->has_fst) {
		result = acpi_fan_dsm_init(&pdev->dev);
		if (result)
			return result;

		result = devm_acpi_fan_create_hwmon(&pdev->dev);
		if (result)
			return result;

		result = devm_acpi_fan_notify_init(&pdev->dev);
		if (result)
			return result;

		result = acpi_fan_dsm_start(&pdev->dev);
		if (result) {
			dev_err(&pdev->dev, "Failed to start Microsoft fan extensions\n");
			return result;
		}

		result = acpi_fan_create_attributes(device);
		if (result)
			return result;
	}

	if (!fan->acpi4) {
		result = acpi_device_update_power(device, NULL);
		if (result) {
			dev_err(&device->dev, "Failed to set initial power state\n");
			goto err_end;
		}
	}

	if (!strncmp(pdev->name, "PNP0C0B", strlen("PNP0C0B")))
		name = "Fan";
	else
		name = acpi_device_bid(device);

	cdev = thermal_cooling_device_register(name, device,
						&fan_cooling_ops);
	if (IS_ERR(cdev)) {
		result = PTR_ERR(cdev);
		goto err_end;
	}

	dev_dbg(&pdev->dev, "registered as cooling_device%d\n", cdev->id);

	fan->cdev = cdev;
	result = sysfs_create_link(&pdev->dev.kobj,
				   &cdev->device.kobj,
				   "thermal_cooling");
	if (result) {
		dev_err(&pdev->dev, "Failed to create sysfs link 'thermal_cooling'\n");
		goto err_unregister;
	}

	result = sysfs_create_link(&cdev->device.kobj,
				   &pdev->dev.kobj,
				   "device");
	if (result) {
		dev_err(&pdev->dev, "Failed to create sysfs link 'device'\n");
		goto err_remove_link;
	}

	return 0;

err_remove_link:
	sysfs_remove_link(&pdev->dev.kobj, "thermal_cooling");
err_unregister:
	thermal_cooling_device_unregister(cdev);
err_end:
	if (fan->has_fst)
		acpi_fan_delete_attributes(device);

	return result;
}

static void acpi_fan_remove(struct platform_device *pdev)
{
	struct acpi_fan *fan = platform_get_drvdata(pdev);

	if (fan->has_fst) {
		struct acpi_device *device = ACPI_COMPANION(&pdev->dev);

		acpi_fan_delete_attributes(device);
	}
	sysfs_remove_link(&pdev->dev.kobj, "thermal_cooling");
	sysfs_remove_link(&fan->cdev->device.kobj, "device");
	thermal_cooling_device_unregister(fan->cdev);
}

#ifdef CONFIG_PM_SLEEP
static int acpi_fan_suspend(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	if (fan->acpi4)
		return 0;

	acpi_device_set_power(ACPI_COMPANION(dev), ACPI_STATE_D0);

	return AE_OK;
}

static int acpi_fan_resume(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	int result;

	if (fan->has_fst) {
		result = acpi_fan_dsm_start(dev);
		if (result)
			dev_err(dev, "Failed to start Microsoft fan extensions: %d\n", result);
	}

	if (fan->acpi4)
		return 0;

	result = acpi_device_update_power(ACPI_COMPANION(dev), NULL);
	if (result)
		dev_err(dev, "Error updating fan power state\n");

	return result;
}

static const struct dev_pm_ops acpi_fan_pm = {
	.resume = acpi_fan_resume,
	.freeze = acpi_fan_suspend,
	.thaw = acpi_fan_resume,
	.restore = acpi_fan_resume,
};
#define FAN_PM_OPS_PTR (&acpi_fan_pm)

#else

#define FAN_PM_OPS_PTR NULL

#endif

static struct platform_driver acpi_fan_driver = {
	.probe = acpi_fan_probe,
	.remove = acpi_fan_remove,
	.driver = {
		.name = "acpi-fan",
		.acpi_match_table = fan_device_ids,
		.pm = FAN_PM_OPS_PTR,
	},
};

module_platform_driver(acpi_fan_driver);

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Fan Driver");
MODULE_LICENSE("GPL");
