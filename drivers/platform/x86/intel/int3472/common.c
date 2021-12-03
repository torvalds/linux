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
