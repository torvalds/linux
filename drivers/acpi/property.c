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

static int acpi_data_get_property_array(struct acpi_device_data *data,
					const char *name,
					acpi_object_type type,
					const union acpi_object **obj);

/* ACPI _DSD device properties UUID: daffd814-6eba-4d8c-8a91-bc9bbf4aa301 */
static const u8 prp_uuid[16] = {
	0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
	0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01
};
/* ACPI _DSD data subnodes UUID: dbb8e3e6-5886-4ba6-8795-1319f52a966b */
static const u8 ads_uuid[16] = {
	0xe6, 0xe3, 0xb8, 0xdb, 0x86, 0x58, 0xa6, 0x4b,
	0x87, 0x95, 0x13, 0x19, 0xf5, 0x2a, 0x96, 0x6b
};

static bool acpi_enumerate_nondev_subnodes(acpi_handle scope,
					   const union acpi_object *desc,
					   struct acpi_device_data *data);
static bool acpi_extract_properties(const union acpi_object *desc,
				    struct acpi_device_data *data);

static bool acpi_nondev_subnode_ok(acpi_handle scope,
				   const union acpi_object *link,
				   struct list_head *list)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	struct acpi_data_node *dn;
	acpi_handle handle;
	acpi_status status;

	dn = kzalloc(sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return false;

	dn->name = link->package.elements[0].string.pointer;
	dn->fwnode.type = FWNODE_ACPI_DATA;
	INIT_LIST_HEAD(&dn->data.subnodes);

	status = acpi_get_handle(scope, link->package.elements[1].string.pointer,
				 &handle);
	if (ACPI_FAILURE(status))
		goto fail;

	status = acpi_evaluate_object_typed(handle, NULL, NULL, &buf,
					    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		goto fail;

	if (acpi_extract_properties(buf.pointer, &dn->data))
		dn->handle = handle;

	/*
	 * The scope for the subnode object lookup is the one of the namespace
	 * node (device) containing the object that has returned the package.
	 * That is, it's the scope of that object's parent.
	 */
	status = acpi_get_parent(handle, &scope);
	if (ACPI_SUCCESS(status)
	    && acpi_enumerate_nondev_subnodes(scope, buf.pointer, &dn->data))
		dn->handle = handle;

	if (dn->handle) {
		dn->data.pointer = buf.pointer;
		list_add_tail(&dn->sibling, list);
		return true;
	}

	acpi_handle_debug(handle, "Invalid properties/subnodes data, skipping\n");

 fail:
	ACPI_FREE(buf.pointer);
	kfree(dn);
	return false;
}

static int acpi_add_nondev_subnodes(acpi_handle scope,
				    const union acpi_object *links,
				    struct list_head *list)
{
	bool ret = false;
	int i;

	for (i = 0; i < links->package.count; i++) {
		const union acpi_object *link;

		link = &links->package.elements[i];
		/* Only two elements allowed, both must be strings. */
		if (link->package.count == 2
		    && link->package.elements[0].type == ACPI_TYPE_STRING
		    && link->package.elements[1].type == ACPI_TYPE_STRING
		    && acpi_nondev_subnode_ok(scope, link, list))
			ret = true;
	}

	return ret;
}

static bool acpi_enumerate_nondev_subnodes(acpi_handle scope,
					   const union acpi_object *desc,
					   struct acpi_device_data *data)
{
	int i;

	/* Look for the ACPI data subnodes UUID. */
	for (i = 0; i < desc->package.count; i += 2) {
		const union acpi_object *uuid, *links;

		uuid = &desc->package.elements[i];
		links = &desc->package.elements[i + 1];

		/*
		 * The first element must be a UUID and the second one must be
		 * a package.
		 */
		if (uuid->type != ACPI_TYPE_BUFFER || uuid->buffer.length != 16
		    || links->type != ACPI_TYPE_PACKAGE)
			break;

		if (memcmp(uuid->buffer.pointer, ads_uuid, sizeof(ads_uuid)))
			continue;

		return acpi_add_nondev_subnodes(scope, links, &data->subnodes);
	}

	return false;
}

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

static void acpi_init_of_compatible(struct acpi_device *adev)
{
	const union acpi_object *of_compatible;
	int ret;

	ret = acpi_data_get_property_array(&adev->data, "compatible",
					   ACPI_TYPE_STRING, &of_compatible);
	if (ret) {
		ret = acpi_dev_get_property(adev, "compatible",
					    ACPI_TYPE_STRING, &of_compatible);
		if (ret) {
			if (adev->parent
			    && adev->parent->flags.of_compatible_ok)
				goto out;

			return;
		}
	}
	adev->data.of_compatible = of_compatible;

 out:
	adev->flags.of_compatible_ok = 1;
}

static bool acpi_extract_properties(const union acpi_object *desc,
				    struct acpi_device_data *data)
{
	int i;

	if (desc->package.count % 2)
		return false;

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

		data->properties = properties;
		return true;
	}

	return false;
}

void acpi_init_properties(struct acpi_device *adev)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	struct acpi_hardware_id *hwid;
	acpi_status status;
	bool acpi_of = false;

	INIT_LIST_HEAD(&adev->data.subnodes);

	/*
	 * Check if ACPI_DT_NAMESPACE_HID is present and inthat case we fill in
	 * Device Tree compatible properties for this device.
	 */
	list_for_each_entry(hwid, &adev->pnp.ids, list) {
		if (!strcmp(hwid->id, ACPI_DT_NAMESPACE_HID)) {
			acpi_of = true;
			break;
		}
	}

	status = acpi_evaluate_object_typed(adev->handle, "_DSD", NULL, &buf,
					    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		goto out;

	if (acpi_extract_properties(buf.pointer, &adev->data)) {
		adev->data.pointer = buf.pointer;
		if (acpi_of)
			acpi_init_of_compatible(adev);
	}
	if (acpi_enumerate_nondev_subnodes(adev->handle, buf.pointer, &adev->data))
		adev->data.pointer = buf.pointer;

	if (!adev->data.pointer) {
		acpi_handle_debug(adev->handle, "Invalid _DSD data, skipping\n");
		ACPI_FREE(buf.pointer);
	}

 out:
	if (acpi_of && !adev->flags.of_compatible_ok)
		acpi_handle_info(adev->handle,
			 ACPI_DT_NAMESPACE_HID " requires 'compatible' property\n");
}

static void acpi_destroy_nondev_subnodes(struct list_head *list)
{
	struct acpi_data_node *dn, *next;

	if (list_empty(list))
		return;

	list_for_each_entry_safe_reverse(dn, next, list, sibling) {
		acpi_destroy_nondev_subnodes(&dn->data.subnodes);
		wait_for_completion(&dn->kobj_done);
		list_del(&dn->sibling);
		ACPI_FREE((void *)dn->data.pointer);
		kfree(dn);
	}
}

void acpi_free_properties(struct acpi_device *adev)
{
	acpi_destroy_nondev_subnodes(&adev->data.subnodes);
	ACPI_FREE((void *)adev->data.pointer);
	adev->data.of_compatible = NULL;
	adev->data.pointer = NULL;
	adev->data.properties = NULL;
}

/**
 * acpi_data_get_property - return an ACPI property with given name
 * @data: ACPI device deta object to get the property from
 * @name: Name of the property
 * @type: Expected property type
 * @obj: Location to store the property value (if not %NULL)
 *
 * Look up a property with @name and store a pointer to the resulting ACPI
 * object at the location pointed to by @obj if found.
 *
 * Callers must not attempt to free the returned objects.  These objects will be
 * freed by the ACPI core automatically during the removal of @data.
 *
 * Return: %0 if property with @name has been found (success),
 *         %-EINVAL if the arguments are invalid,
 *         %-EINVAL if the property doesn't exist,
 *         %-EPROTO if the property value type doesn't match @type.
 */
static int acpi_data_get_property(struct acpi_device_data *data,
				  const char *name, acpi_object_type type,
				  const union acpi_object **obj)
{
	const union acpi_object *properties;
	int i;

	if (!data || !name)
		return -EINVAL;

	if (!data->pointer || !data->properties)
		return -EINVAL;

	properties = data->properties;
	for (i = 0; i < properties->package.count; i++) {
		const union acpi_object *propname, *propvalue;
		const union acpi_object *property;

		property = &properties->package.elements[i];

		propname = &property->package.elements[0];
		propvalue = &property->package.elements[1];

		if (!strcmp(name, propname->string.pointer)) {
			if (type != ACPI_TYPE_ANY && propvalue->type != type)
				return -EPROTO;
			if (obj)
				*obj = propvalue;

			return 0;
		}
	}
	return -EINVAL;
}

/**
 * acpi_dev_get_property - return an ACPI property with given name.
 * @adev: ACPI device to get the property from.
 * @name: Name of the property.
 * @type: Expected property type.
 * @obj: Location to store the property value (if not %NULL).
 */
int acpi_dev_get_property(struct acpi_device *adev, const char *name,
			  acpi_object_type type, const union acpi_object **obj)
{
	return adev ? acpi_data_get_property(&adev->data, name, type, obj) : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property);

static struct acpi_device_data *acpi_device_data_of_node(struct fwnode_handle *fwnode)
{
	if (fwnode->type == FWNODE_ACPI) {
		struct acpi_device *adev = to_acpi_device_node(fwnode);
		return &adev->data;
	} else if (fwnode->type == FWNODE_ACPI_DATA) {
		struct acpi_data_node *dn = to_acpi_data_node(fwnode);
		return &dn->data;
	}
	return NULL;
}

/**
 * acpi_node_prop_get - return an ACPI property with given name.
 * @fwnode: Firmware node to get the property from.
 * @propname: Name of the property.
 * @valptr: Location to store a pointer to the property value (if not %NULL).
 */
int acpi_node_prop_get(struct fwnode_handle *fwnode, const char *propname,
		       void **valptr)
{
	return acpi_data_get_property(acpi_device_data_of_node(fwnode),
				      propname, ACPI_TYPE_ANY,
				      (const union acpi_object **)valptr);
}

/**
 * acpi_data_get_property_array - return an ACPI array property with given name
 * @adev: ACPI data object to get the property from
 * @name: Name of the property
 * @type: Expected type of array elements
 * @obj: Location to store a pointer to the property value (if not NULL)
 *
 * Look up an array property with @name and store a pointer to the resulting
 * ACPI object at the location pointed to by @obj if found.
 *
 * Callers must not attempt to free the returned objects.  Those objects will be
 * freed by the ACPI core automatically during the removal of @data.
 *
 * Return: %0 if array property (package) with @name has been found (success),
 *         %-EINVAL if the arguments are invalid,
 *         %-EINVAL if the property doesn't exist,
 *         %-EPROTO if the property is not a package or the type of its elements
 *           doesn't match @type.
 */
static int acpi_data_get_property_array(struct acpi_device_data *data,
					const char *name,
					acpi_object_type type,
					const union acpi_object **obj)
{
	const union acpi_object *prop;
	int ret, i;

	ret = acpi_data_get_property(data, name, ACPI_TYPE_PACKAGE, &prop);
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

/**
 * acpi_data_get_property_reference - returns handle to the referenced object
 * @data: ACPI device data object containing the property
 * @propname: Name of the property
 * @index: Index of the reference to return
 * @args: Location to store the returned reference with optional arguments
 *
 * Find property with @name, verifify that it is a package containing at least
 * one object reference and if so, store the ACPI device object pointer to the
 * target object in @args->adev.  If the reference includes arguments, store
 * them in the @args->args[] array.
 *
 * If there's more than one reference in the property value package, @index is
 * used to select the one to return.
 *
 * Return: %0 on success, negative error code on failure.
 */
static int acpi_data_get_property_reference(struct acpi_device_data *data,
					    const char *propname, size_t index,
					    struct acpi_reference_args *args)
{
	const union acpi_object *element, *end;
	const union acpi_object *obj;
	struct acpi_device *device;
	int ret, idx = 0;

	ret = acpi_data_get_property(data, propname, ACPI_TYPE_ANY, &obj);
	if (ret)
		return ret;

	/*
	 * The simplest case is when the value is a single reference.  Just
	 * return that reference then.
	 */
	if (obj->type == ACPI_TYPE_LOCAL_REFERENCE) {
		if (index)
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

/**
 * acpi_node_get_property_reference - get a handle to the referenced object.
 * @fwnode: Firmware node to get the property from.
 * @propname: Name of the property.
 * @index: Index of the reference to return.
 * @args: Location to store the returned reference with optional arguments.
 */
int acpi_node_get_property_reference(struct fwnode_handle *fwnode,
				     const char *name, size_t index,
				     struct acpi_reference_args *args)
{
	struct acpi_device_data *data = acpi_device_data_of_node(fwnode);

	return data ? acpi_data_get_property_reference(data, name, index, args) : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_node_get_property_reference);

static int acpi_data_prop_read_single(struct acpi_device_data *data,
				      const char *propname,
				      enum dev_prop_type proptype, void *val)
{
	const union acpi_object *obj;
	int ret;

	if (!val)
		return -EINVAL;

	if (proptype >= DEV_PROP_U8 && proptype <= DEV_PROP_U64) {
		ret = acpi_data_get_property(data, propname, ACPI_TYPE_INTEGER, &obj);
		if (ret)
			return ret;

		switch (proptype) {
		case DEV_PROP_U8:
			if (obj->integer.value > U8_MAX)
				return -EOVERFLOW;
			*(u8 *)val = obj->integer.value;
			break;
		case DEV_PROP_U16:
			if (obj->integer.value > U16_MAX)
				return -EOVERFLOW;
			*(u16 *)val = obj->integer.value;
			break;
		case DEV_PROP_U32:
			if (obj->integer.value > U32_MAX)
				return -EOVERFLOW;
			*(u32 *)val = obj->integer.value;
			break;
		default:
			*(u64 *)val = obj->integer.value;
			break;
		}
	} else if (proptype == DEV_PROP_STRING) {
		ret = acpi_data_get_property(data, propname, ACPI_TYPE_STRING, &obj);
		if (ret)
			return ret;

		*(char **)val = obj->string.pointer;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

int acpi_dev_prop_read_single(struct acpi_device *adev, const char *propname,
			      enum dev_prop_type proptype, void *val)
{
	return adev ? acpi_data_prop_read_single(&adev->data, propname, proptype, val) : -EINVAL;
}

static int acpi_copy_property_array_u8(const union acpi_object *items, u8 *val,
				       size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		if (items[i].type != ACPI_TYPE_INTEGER)
			return -EPROTO;
		if (items[i].integer.value > U8_MAX)
			return -EOVERFLOW;

		val[i] = items[i].integer.value;
	}
	return 0;
}

static int acpi_copy_property_array_u16(const union acpi_object *items,
					u16 *val, size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		if (items[i].type != ACPI_TYPE_INTEGER)
			return -EPROTO;
		if (items[i].integer.value > U16_MAX)
			return -EOVERFLOW;

		val[i] = items[i].integer.value;
	}
	return 0;
}

static int acpi_copy_property_array_u32(const union acpi_object *items,
					u32 *val, size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		if (items[i].type != ACPI_TYPE_INTEGER)
			return -EPROTO;
		if (items[i].integer.value > U32_MAX)
			return -EOVERFLOW;

		val[i] = items[i].integer.value;
	}
	return 0;
}

static int acpi_copy_property_array_u64(const union acpi_object *items,
					u64 *val, size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		if (items[i].type != ACPI_TYPE_INTEGER)
			return -EPROTO;

		val[i] = items[i].integer.value;
	}
	return 0;
}

static int acpi_copy_property_array_string(const union acpi_object *items,
					   char **val, size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		if (items[i].type != ACPI_TYPE_STRING)
			return -EPROTO;

		val[i] = items[i].string.pointer;
	}
	return 0;
}

static int acpi_data_prop_read(struct acpi_device_data *data,
			       const char *propname,
			       enum dev_prop_type proptype,
			       void *val, size_t nval)
{
	const union acpi_object *obj;
	const union acpi_object *items;
	int ret;

	if (val && nval == 1) {
		ret = acpi_data_prop_read_single(data, propname, proptype, val);
		if (!ret)
			return ret;
	}

	ret = acpi_data_get_property_array(data, propname, ACPI_TYPE_ANY, &obj);
	if (ret)
		return ret;

	if (!val)
		return obj->package.count;

	if (nval > obj->package.count)
		return -EOVERFLOW;
	else if (nval <= 0)
		return -EINVAL;

	items = obj->package.elements;

	switch (proptype) {
	case DEV_PROP_U8:
		ret = acpi_copy_property_array_u8(items, (u8 *)val, nval);
		break;
	case DEV_PROP_U16:
		ret = acpi_copy_property_array_u16(items, (u16 *)val, nval);
		break;
	case DEV_PROP_U32:
		ret = acpi_copy_property_array_u32(items, (u32 *)val, nval);
		break;
	case DEV_PROP_U64:
		ret = acpi_copy_property_array_u64(items, (u64 *)val, nval);
		break;
	case DEV_PROP_STRING:
		ret = acpi_copy_property_array_string(items, (char **)val, nval);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int acpi_dev_prop_read(struct acpi_device *adev, const char *propname,
		       enum dev_prop_type proptype, void *val, size_t nval)
{
	return adev ? acpi_data_prop_read(&adev->data, propname, proptype, val, nval) : -EINVAL;
}

/**
 * acpi_node_prop_read - retrieve the value of an ACPI property with given name.
 * @fwnode: Firmware node to get the property from.
 * @propname: Name of the property.
 * @proptype: Expected property type.
 * @val: Location to store the property value (if not %NULL).
 * @nval: Size of the array pointed to by @val.
 *
 * If @val is %NULL, return the number of array elements comprising the value
 * of the property.  Otherwise, read at most @nval values to the array at the
 * location pointed to by @val.
 */
int acpi_node_prop_read(struct fwnode_handle *fwnode,  const char *propname,
		        enum dev_prop_type proptype, void *val, size_t nval)
{
	return acpi_data_prop_read(acpi_device_data_of_node(fwnode),
				   propname, proptype, val, nval);
}

/**
 * acpi_get_next_subnode - Return the next child node handle for a device.
 * @dev: Device to find the next child node for.
 * @child: Handle to one of the device's child nodes or a null handle.
 */
struct fwnode_handle *acpi_get_next_subnode(struct device *dev,
					    struct fwnode_handle *child)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct list_head *head, *next;

	if (!adev)
		return NULL;

	if (!child || child->type == FWNODE_ACPI) {
		head = &adev->children;
		if (list_empty(head))
			goto nondev;

		if (child) {
			adev = to_acpi_device_node(child);
			next = adev->node.next;
			if (next == head) {
				child = NULL;
				adev = ACPI_COMPANION(dev);
				goto nondev;
			}
			adev = list_entry(next, struct acpi_device, node);
		} else {
			adev = list_first_entry(head, struct acpi_device, node);
		}
		return acpi_fwnode_handle(adev);
	}

 nondev:
	if (!child || child->type == FWNODE_ACPI_DATA) {
		struct acpi_data_node *dn;

		head = &adev->data.subnodes;
		if (list_empty(head))
			return NULL;

		if (child) {
			dn = to_acpi_data_node(child);
			next = dn->sibling.next;
			if (next == head)
				return NULL;

			dn = list_entry(next, struct acpi_data_node, sibling);
		} else {
			dn = list_first_entry(head, struct acpi_data_node, sibling);
		}
		return &dn->fwnode;
	}
	return NULL;
}
