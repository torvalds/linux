/*
 * ACPI device specific properties support.
 *
 * Copyright (C) 2014, Intel Corporation
 * All rights reserved.
 *
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Darren Hart <dvhart@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>

#include "internal.h"

/* ACPI _DSD device properties UUID: daffd814-6eba-4d8c-8a91-bc9bbf4aa301 */
static const u8 prp_uuid[16] = {
	0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
	0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01
};

static bool acpi_property_value_ok(const union acpi_object *value)
{
	int j;

	/*
	 * The value must be an integer, a string, a reference, or a package
	 * whose every element must be an integer, a string, or a reference.
	 */
	switch (value->type) {
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_LOCAL_REFERENCE:
		return true;

	case ACPI_TYPE_PACKAGE:
		for (j = 0; j < value->package.count; j++)
			switch (value->package.elements[j].type) {
			case ACPI_TYPE_INTEGER:
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_LOCAL_REFERENCE:
				continue;

			default:
				return false;
			}

		return true;
	}
	return false;
}

static bool acpi_properties_format_valid(const union acpi_object *properties)
{
	int i;

	for (i = 0; i < properties->package.count; i++) {
		const union acpi_object *property;

		property = &properties->package.elements[i];
		/*
		 * Only two elements allowed, the first one must be a string and
		 * the second one has to satisfy certain conditions.
		 */
		if (property->package.count != 2
		    || property->package.elements[0].type != ACPI_TYPE_STRING
		    || !acpi_property_value_ok(&property->package.elements[1]))
			return false;
	}
	return true;
}

void acpi_init_properties(struct acpi_device *adev)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	const union acpi_object *desc;
	acpi_status status;
	int i;

	status = acpi_evaluate_object_typed(adev->handle, "_DSD", NULL, &buf,
					    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return;

	desc = buf.pointer;
	if (desc->package.count % 2)
		goto fail;

	/* Look for the device properties UUID. */
	for (i = 0; i < desc->package.count; i += 2) {
		const union acpi_object *uuid, *properties;

		uuid = &desc->package.elements[i];
		properties = &desc->package.elements[i + 1];

		/*
		 * The first element must be a UUID and the second one must be
		 * a package.
		 */
		if (uuid->type != ACPI_TYPE_BUFFER || uuid->buffer.length != 16
		    || properties->type != ACPI_TYPE_PACKAGE)
			break;

		if (memcmp(uuid->buffer.pointer, prp_uuid, sizeof(prp_uuid)))
			continue;

		/*
		 * We found the matching UUID. Now validate the format of the
		 * package immediately following it.
		 */
		if (!acpi_properties_format_valid(properties))
			break;

		adev->data.pointer = buf.pointer;
		adev->data.properties = properties;
		return;
	}

 fail:
	dev_warn(&adev->dev, "Returned _DSD data is not valid, skipping\n");
	ACPI_FREE(buf.pointer);
}

void acpi_free_properties(struct acpi_device *adev)
{
	ACPI_FREE((void *)adev->data.pointer);
	adev->data.pointer = NULL;
	adev->data.properties = NULL;
}

/**
 * acpi_dev_get_property - return an ACPI property with given name
 * @adev: ACPI device to get property
 * @name: Name of the property
 * @type: Expected property type
 * @obj: Location to store the property value (if not %NULL)
 *
 * Look up a property with @name and store a pointer to the resulting ACPI
 * object at the location pointed to by @obj if found.
 *
 * Callers must not attempt to free the returned objects.  These objects will be
 * freed by the ACPI core automatically during the removal of @adev.
 *
 * Return: %0 if property with @name has been found (success),
 *         %-EINVAL if the arguments are invalid,
 *         %-ENODATA if the property doesn't exist,
 *         %-EPROTO if the property value type doesn't match @type.
 */
int acpi_dev_get_property(struct acpi_device *adev, const char *name,
			  acpi_object_type type, const union acpi_object **obj)
{
	const union acpi_object *properties;
	int i;

	if (!adev || !name)
		return -EINVAL;

	if (!adev->data.pointer || !adev->data.properties)
		return -ENODATA;

	properties = adev->data.properties;
	for (i = 0; i < properties->package.count; i++) {
		const union acpi_object *propname, *propvalue;
		const union acpi_object *property;

		property = &properties->package.elements[i];

		propname = &property->package.elements[0];
		propvalue = &property->package.elements[1];

		if (!strcmp(name, propname->string.pointer)) {
			if (type != ACPI_TYPE_ANY && propvalue->type != type)
				return -EPROTO;
			else if (obj)
				*obj = propvalue;

			return 0;
		}
	}
	return -ENODATA;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property);

/**
 * acpi_dev_get_property_array - return an ACPI array property with given name
 * @adev: ACPI device to get property
 * @name: Name of the property
 * @type: Expected type of array elements
 * @obj: Location to store a pointer to the property value (if not NULL)
 *
 * Look up an array property with @name and store a pointer to the resulting
 * ACPI object at the location pointed to by @obj if found.
 *
 * Callers must not attempt to free the returned objects.  Those objects will be
 * freed by the ACPI core automatically during the removal of @adev.
 *
 * Return: %0 if array property (package) with @name has been found (success),
 *         %-EINVAL if the arguments are invalid,
 *         %-ENODATA if the property doesn't exist,
 *         %-EPROTO if the property is not a package or the type of its elements
 *           doesn't match @type.
 */
int acpi_dev_get_property_array(struct acpi_device *adev, const char *name,
				acpi_object_type type,
				const union acpi_object **obj)
{
	const union acpi_object *prop;
	int ret, i;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_PACKAGE, &prop);
	if (ret)
		return ret;

	if (type != ACPI_TYPE_ANY) {
		/* Check that all elements are of correct type. */
		for (i = 0; i < prop->package.count; i++)
			if (prop->package.elements[i].type != type)
				return -EPROTO;
	}
	if (obj)
		*obj = prop;

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_array);

/**
 * acpi_dev_get_property_reference - returns handle to the referenced object
 * @adev: ACPI device to get property
 * @name: Name of the property
 * @size_prop: Name of the "size" property in referenced object
 * @index: Index of the reference to return
 * @args: Location to store the returned reference with optional arguments
 *
 * Find property with @name, verifify that it is a package containing at least
 * one object reference and if so, store the ACPI device object pointer to the
 * target object in @args->adev.
 *
 * If the reference includes arguments (@size_prop is not %NULL) follow the
 * reference and check whether or not there is an integer property @size_prop
 * under the target object and if so, whether or not its value matches the
 * number of arguments that follow the reference.  If there's more than one
 * reference in the property value package, @index is used to select the one to
 * return.
 *
 * Return: %0 on success, negative error code on failure.
 */
int acpi_dev_get_property_reference(struct acpi_device *adev, const char *name,
				    const char *size_prop, size_t index,
				    struct acpi_reference_args *args)
{
	const union acpi_object *element, *end;
	const union acpi_object *obj;
	struct acpi_device *device;
	int ret, idx = 0;

	ret = acpi_dev_get_property(adev, name, ACPI_TYPE_ANY, &obj);
	if (ret)
		return ret;

	/*
	 * The simplest case is when the value is a single reference.  Just
	 * return that reference then.
	 */
	if (obj->type == ACPI_TYPE_LOCAL_REFERENCE) {
		if (size_prop || index)
			return -EINVAL;

		ret = acpi_bus_get_device(obj->reference.handle, &device);
		if (ret)
			return ret;

		args->adev = device;
		args->nargs = 0;
		return 0;
	}

	/*
	 * If it is not a single reference, then it is a package of
	 * references followed by number of ints as follows:
	 *
	 *  Package () { REF, INT, REF, INT, INT }
	 *
	 * The index argument is then used to determine which reference
	 * the caller wants (along with the arguments).
	 */
	if (obj->type != ACPI_TYPE_PACKAGE || index >= obj->package.count)
		return -EPROTO;

	element = obj->package.elements;
	end = element + obj->package.count;

	while (element < end) {
		u32 nargs, i;

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			return -EPROTO;

		ret = acpi_bus_get_device(element->reference.handle, &device);
		if (ret)
			return -ENODEV;

		element++;
		nargs = 0;

		if (size_prop) {
			const union acpi_object *prop;

			/*
			 * Find out how many arguments the refenced object
			 * expects by reading its size_prop property.
			 */
			ret = acpi_dev_get_property(device, size_prop,
						    ACPI_TYPE_INTEGER, &prop);
			if (ret)
				return ret;

			nargs = prop->integer.value;
			if (nargs > MAX_ACPI_REFERENCE_ARGS
			    || element + nargs > end)
				return -EPROTO;

			/*
			 * Skip to the start of the arguments and verify
			 * that they all are in fact integers.
			 */
			for (i = 0; i < nargs; i++)
				if (element[i].type != ACPI_TYPE_INTEGER)
					return -EPROTO;
		} else {
			/* assume following integer elements are all args */
			for (i = 0; element + i < end; i++) {
				int type = element[i].type;

				if (type == ACPI_TYPE_INTEGER)
					nargs++;
				else if (type == ACPI_TYPE_LOCAL_REFERENCE)
					break;
				else
					return -EPROTO;
			}
		}

		if (idx++ == index) {
			args->adev = device;
			args->nargs = nargs;
			for (i = 0; i < nargs; i++)
				args->args[i] = element[i].integer.value;

			return 0;
		}

		element += nargs;
	}

	return -EPROTO;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property_reference);
