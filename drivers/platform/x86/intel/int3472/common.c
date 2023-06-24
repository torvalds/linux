// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/slab.h>

#include "common.h"

union acpi_object *skl_int3472_get_acpi_buffer(struct acpi_device *adev, char *id)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle handle = adev->handle;
	union acpi_object *obj;
	acpi_status status;

	status = acpi_evaluate_object(handle, id, NULL, &buffer);
	if (ACPI_FAILURE(status))
		return ERR_PTR(-ENODEV);

	obj = buffer.pointer;
	if (!obj)
		return ERR_PTR(-ENODEV);

	if (obj->type != ACPI_TYPE_BUFFER) {
		acpi_handle_err(handle, "%s object is not an ACPI buffer\n", id);
		kfree(obj);
		return ERR_PTR(-EINVAL);
	}

	return obj;
}

int skl_int3472_fill_cldb(struct acpi_device *adev, struct int3472_cldb *cldb)
{
	union acpi_object *obj;
	int ret;

	obj = skl_int3472_get_acpi_buffer(adev, "CLDB");
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (obj->buffer.length > sizeof(*cldb)) {
		acpi_handle_err(adev->handle, "The CLDB buffer is too large\n");
		ret = -EINVAL;
		goto out_free_obj;
	}

	memcpy(cldb, obj->buffer.pointer, obj->buffer.length);
	ret = 0;

out_free_obj:
	kfree(obj);
	return ret;
}

/* sensor_adev_ret may be NULL, name_ret must not be NULL */
int skl_int3472_get_sensor_adev_and_name(struct device *dev,
					 struct acpi_device **sensor_adev_ret,
					 const char **name_ret)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpi_device *sensor;
	int ret = 0;

	sensor = acpi_dev_get_next_consumer_dev(adev, NULL);
	if (!sensor) {
		dev_err(dev, "INT3472 seems to have no dependents.\n");
		return -ENODEV;
	}

	*name_ret = devm_kasprintf(dev, GFP_KERNEL, I2C_DEV_NAME_FORMAT,
				   acpi_dev_name(sensor));
	if (!*name_ret)
		ret = -ENOMEM;

	if (ret == 0 && sensor_adev_ret)
		*sensor_adev_ret = sensor;
	else
		acpi_dev_put(sensor);

	return ret;
}
