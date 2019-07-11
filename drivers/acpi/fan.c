// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_fan.c - ACPI Fan Driver ($Revision: 29 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Fan Driver");
MODULE_LICENSE("GPL");

static int acpi_fan_probe(struct platform_device *pdev);
static int acpi_fan_remove(struct platform_device *pdev);

static const struct acpi_device_id fan_device_ids[] = {
	{"PNP0C0B", 0},
	{"INT3404", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fan_device_ids);

#ifdef CONFIG_PM_SLEEP
static int acpi_fan_suspend(struct device *dev);
static int acpi_fan_resume(struct device *dev);
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

struct acpi_fan_fps {
	u64 control;
	u64 trip_point;
	u64 speed;
	u64 noise_level;
	u64 power;
};

struct acpi_fan_fif {
	u64 revision;
	u64 fine_grain_ctrl;
	u64 step_size;
	u64 low_speed_notification;
};

struct acpi_fan {
	bool acpi4;
	struct acpi_fan_fif fif;
	struct acpi_fan_fps *fps;
	int fps_count;
	struct thermal_cooling_device *cdev;
};

static struct platform_driver acpi_fan_driver = {
	.probe = acpi_fan_probe,
	.remove = acpi_fan_remove,
	.driver = {
		.name = "acpi-fan",
		.acpi_match_table = fan_device_ids,
		.pm = FAN_PM_OPS_PTR,
	},
};

/* thermal cooling device callbacks */
static int fan_get_max_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4)
		*state = fan->fps_count - 1;
	else
		*state = 1;
	return 0;
}

static int fan_get_state_acpi4(struct acpi_device *device, unsigned long *state)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_fan *fan = acpi_driver_data(device);
	union acpi_object *obj;
	acpi_status status;
	int control, i;

	status = acpi_evaluate_object(device->handle, "_FST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Get fan state failed\n");
		return status;
	}

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE ||
	    obj->package.count != 3 ||
	    obj->package.elements[1].type != ACPI_TYPE_INTEGER) {
		dev_err(&device->dev, "Invalid _FST data\n");
		status = -EINVAL;
		goto err;
	}

	control = obj->package.elements[1].integer.value;
	for (i = 0; i < fan->fps_count; i++) {
		/*
		 * When Fine Grain Control is set, return the state
		 * corresponding to maximum fan->fps[i].control
		 * value compared to the current speed. Here the
		 * fan->fps[] is sorted array with increasing speed.
		 */
		if (fan->fif.fine_grain_ctrl && control < fan->fps[i].control) {
			i = (i > 0) ? i - 1 : 0;
			break;
		} else if (control == fan->fps[i].control) {
			break;
		}
	}
	if (i == fan->fps_count) {
		dev_dbg(&device->dev, "Invalid control value returned\n");
		status = -EINVAL;
		goto err;
	}

	*state = i;

err:
	kfree(obj);
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

	if (state >= fan->fps_count)
		return -EINVAL;

	status = acpi_execute_simple_method(device->handle, "_FSL",
					    fan->fps[state].control);
	if (ACPI_FAILURE(status)) {
		dev_dbg(&device->dev, "Failed to set state by _FSL\n");
		return status;
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

static bool acpi_fan_is_acpi4(struct acpi_device *device)
{
	return acpi_has_method(device->handle, "_FIF") &&
	       acpi_has_method(device->handle, "_FPS") &&
	       acpi_has_method(device->handle, "_FSL") &&
	       acpi_has_method(device->handle, "_FST");
}

static int acpi_fan_get_fif(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_buffer format = { sizeof("NNNN"), "NNNN" };
	struct acpi_buffer fif = { sizeof(fan->fif), &fan->fif };
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
	}

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
		struct acpi_buffer fps = { sizeof(fan->fps[i]), &fan->fps[i] };
		status = acpi_extract_package(&obj->package.elements[i + 1],
					      &format, &fps);
		if (ACPI_FAILURE(status)) {
			dev_err(&device->dev, "Invalid _FPS element\n");
			break;
		}
	}

	/* sort the state array according to fan speed in increase order */
	sort(fan->fps, fan->fps_count, sizeof(*fan->fps),
	     acpi_fan_speed_cmp, NULL);

err:
	kfree(obj);
	return status;
}

static int acpi_fan_probe(struct platform_device *pdev)
{
	int result = 0;
	struct thermal_cooling_device *cdev;
	struct acpi_fan *fan;
	struct acpi_device *device = ACPI_COMPANION(&pdev->dev);
	char *name;

	fan = devm_kzalloc(&pdev->dev, sizeof(*fan), GFP_KERNEL);
	if (!fan) {
		dev_err(&device->dev, "No memory for fan\n");
		return -ENOMEM;
	}
	device->driver_data = fan;
	platform_set_drvdata(pdev, fan);

	if (acpi_fan_is_acpi4(device)) {
		if (acpi_fan_get_fif(device) || acpi_fan_get_fps(device))
			goto end;
		fan->acpi4 = true;
	} else {
		result = acpi_device_update_power(device, NULL);
		if (result) {
			dev_err(&device->dev, "Failed to set initial power state\n");
			goto end;
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
		goto end;
	}

	dev_dbg(&pdev->dev, "registered as cooling_device%d\n", cdev->id);

	fan->cdev = cdev;
	result = sysfs_create_link(&pdev->dev.kobj,
				   &cdev->device.kobj,
				   "thermal_cooling");
	if (result)
		dev_err(&pdev->dev, "Failed to create sysfs link 'thermal_cooling'\n");

	result = sysfs_create_link(&cdev->device.kobj,
				   &pdev->dev.kobj,
				   "device");
	if (result)
		dev_err(&pdev->dev, "Failed to create sysfs link 'device'\n");

end:
	return result;
}

static int acpi_fan_remove(struct platform_device *pdev)
{
	struct acpi_fan *fan = platform_get_drvdata(pdev);

	sysfs_remove_link(&pdev->dev.kobj, "thermal_cooling");
	sysfs_remove_link(&fan->cdev->device.kobj, "device");
	thermal_cooling_device_unregister(fan->cdev);

	return 0;
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
	int result;
	struct acpi_fan *fan = dev_get_drvdata(dev);

	if (fan->acpi4)
		return 0;

	result = acpi_device_update_power(ACPI_COMPANION(dev), NULL);
	if (result)
		dev_err(dev, "Error updating fan power state\n");

	return result;
}
#endif

module_platform_driver(acpi_fan_driver);
