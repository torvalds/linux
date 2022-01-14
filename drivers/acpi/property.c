// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI device specific properties support.
 *
 * Copyright (C) 2014, Intel Corporation
 * All rights reserved.
 *
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Darren Hart <dvhart@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>

#include "internal.h"

static int acpi_data_get_property_array(const struct acpi_device_data *data,
					const char *name,
					acpi_object_type type,
					const union acpi_object **obj);

/*
 * The GUIDs here are made equivalent to each other in order to avoid extra
 * complexity in the properties handling code, with the caveat that the
 * kernel will accept certain combinations of GUID and properties that are
 * not defined without a warning. For instance if any of the properties
 * from different GUID appear in a property list of another, it will be
 * accepted by the kernel. Firmware validation tools should catch these.
 */
static const guid_t prp_guids[] = {
	/* ACPI _DSD device properties GUID: daffd814-6eba-4d8c-8a91-bc9bbf4aa301 */
	GUID_INIT(0xdaffd814, 0x6eba, 0x4d8c,
		  0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01),
	/* Hotplug in D3 GUID: 6211e2c0-58a3-4af3-90e1-927a4e0c55a4 */
	GUID_INIT(0x6211e2c0, 0x58a3, 0x4af3,
		  0x90, 0xe1, 0x92, 0x7a, 0x4e, 0x0c, 0x55, 0xa4),
	/* External facing port GUID: efcc06cc-73ac-4bc3-bff0-76143807c389 */
	GUID_INIT(0xefcc06cc, 0x73ac, 0x4bc3,
		  0xbf, 0xf0, 0x76, 0x14, 0x38, 0x07, 0xc3, 0x89),
	/* Thunderbolt GUID for IMR_VALID: c44d002f-69f9-4e7d-a904-a7baabdf43f7 */
	GUID_INIT(0xc44d002f, 0x69f9, 0x4e7d,
		  0xa9, 0x04, 0xa7, 0xba, 0xab, 0xdf, 0x43, 0xf7),
	/* Thunderbolt GUID for WAKE_SUPPORTED: 6c501103-c189-4296-ba72-9bf5a26ebe5d */
	GUID_INIT(0x6c501103, 0xc189, 0x4296,
		  0xba, 0x72, 0x9b, 0xf5, 0xa2, 0x6e, 0xbe, 0x5d),
	/* Storage device needs D3 GUID: 5025030f-842f-4ab4-a561-99a5189762d0 */
	GUID_INIT(0x5025030f, 0x842f, 0x4ab4,
		  0xa5, 0x61, 0x99, 0xa5, 0x18, 0x97, 0x62, 0xd0),
};

/* ACPI _DSD data subnodes GUID: dbb8e3e6-5886-4ba6-8795-1319f52a966b */
static const guid_t ads_guid =
	GUID_INIT(0xdbb8e3e6, 0x5886, 0x4ba6,
		  0x87, 0x95, 0x13, 0x19, 0xf5, 0x2a, 0x96, 0x6b);

static bool acpi_enumerate_nondev_subnodes(acpi_handle scope,
					   const union acpi_object *desc,
					   struct acpi_device_data *data,
					   struct fwnode_handle *parent);
static bool acpi_extract_properties(const union acpi_object *desc,
				    struct acpi_device_data *data);

static bool acpi_nondev_subnode_extract(const union acpi_object *desc,
					acpi_handle handle,
					const union acpi_object *link,
					struct list_head *list,
					struct fwnode_handle *parent)
{
	struct acpi_data_node *dn;
	bool result;

	dn = kzalloc(sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return false;

	dn->name = link->package.elements[0].string.pointer;
	fwnode_init(&dn->fwnode, &acpi_data_fwnode_ops);
	dn->parent = parent;
	INIT_LIST_HEAD(&dn->data.properties);
	INIT_LIST_HEAD(&dn->data.subnodes);

	result = acpi_extract_properties(desc, &dn->data);

	if (handle) {
		acpi_handle scope;
		acpi_status status;

		/*
		 * The scope for the subnode object lookup is the one of the
		 * namespace node (device) containing the object that has
		 * returned the package.  That is, it's the scope of that
		 * object's parent.
		 */
		status = acpi_get_parent(handle, &scope);
		if (ACPI_SUCCESS(status)
		    && acpi_enumerate_nondev_subnodes(scope, desc, &dn->data,
						      &dn->fwnode))
			result = true;
	} else if (acpi_enumerate_nondev_subnodes(NULL, desc, &dn->data,
						  &dn->fwnode)) {
		result = true;
	}

	if (result) {
		dn->handle = handle;
		dn->data.pointer = desc;
		list_add_tail(&dn->sibling, list);
		return true;
	}

	kfree(dn);
	acpi_handle_debug(handle, "Invalid properties/subnodes data, skipping\n");
	return false;
}

static bool acpi_nondev_subnode_data_ok(acpi_handle handle,
					const union acpi_object *link,
					struct list_head *list,
					struct fwnode_handle *parent)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	acpi_status status;

	status = acpi_evaluate_object_typed(handle, NULL, NULL, &buf,
					    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return false;

	if (acpi_nondev_subnode_extract(buf.pointer, handle, link, list,
					parent))
		return true;

	ACPI_FREE(buf.pointer);
	return false;
}

static bool acpi_nondev_subnode_ok(acpi_handle scope,
				   const union acpi_object *link,
				   struct list_head *list,
				   struct fwnode_handle *parent)
{
	acpi_handle handle;
	acpi_status status;

	if (!scope)
		return false;

	status = acpi_get_handle(scope, link->package.elements[1].string.pointer,
				 &handle);
	if (ACPI_FAILURE(status))
		return false;

	return acpi_nondev_subnode_data_ok(handle, link, list, parent);
}

static int acpi_add_nondev_subnodes(acpi_handle scope,
				    const union acpi_object *links,
				    struct list_head *list,
				    struct fwnode_handle *parent)
{
	bool ret = false;
	int i;

	for (i = 0; i < links->package.count; i++) {
		const union acpi_object *link, *desc;
		acpi_handle handle;
		bool result;

		link = &links->package.elements[i];
		/* Only two elements allowed. */
		if (link->package.count != 2)
			continue;

		/* The first one must be a string. */
		if (link->package.elements[0].type != ACPI_TYPE_STRING)
			continue;

		/* The second one may be a string, a reference or a package. */
		switch (link->package.elements[1].type) {
		case ACPI_TYPE_STRING:
			result = acpi_nondev_subnode_ok(scope, link, list,
							 parent);
			break;
		case ACPI_TYPE_LOCAL_REFERENCE:
			handle = link->package.elements[1].reference.handle;
			result = acpi_nondev_subnode_data_ok(handle, link, list,
							     parent);
			break;
		case ACPI_TYPE_PACKAGE:
			desc = &link->package.elements[1];
			result = acpi_nondev_subnode_extract(desc, NULL, link,
							     list, parent);
			break;
		default:
			result = false;
			break;
		}
		ret = ret || result;
	}

	return ret;
}

static bool acpi_enumerate_nondev_subnodes(acpi_handle scope,
					   const union acpi_object *desc,
					   struct acpi_device_data *data,
					   struct fwnode_handle *parent)
{
	int i;

	/* Look for the ACPI data subnodes GUID. */
	for (i = 0; i < desc->package.count; i += 2) {
		const union acpi_object *guid, *links;

		guid = &desc->package.elements[i];
		links = &desc->package.elements[i + 1];

		/*
		 * The first element must be a GUID and the second one must be
		 * a package.
		 */
		if (guid->type != ACPI_TYPE_BUFFER ||
		    guid->buffer.length != 16 ||
		    links->type != ACPI_TYPE_PACKAGE)
			break;

		if (!guid_equal((guid_t *)guid->buffer.pointer, &ads_guid))
			continue;

		return acpi_add_nondev_subnodes(scope, links, &data->subnodes,
						parent);
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

static bool acpi_is_property_guid(const guid_t *guid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prp_guids); i++) {
		if (guid_equal(guid, &prp_guids[i]))
			return true;
	}

	return false;
}

struct acpi_device_properties *
acpi_data_add_props(struct acpi_device_data *data, const guid_t *guid,
		    const union acpi_object *properties)
{
	struct acpi_device_properties *props;

	props = kzalloc(sizeof(*props), GFP_KERNEL);
	if (props) {
		INIT_LIST_HEAD(&props->list);
		props->guid = guid;
		props->properties = properties;
		list_add_tail(&props->list, &data->properties);
	}

	return props;
}

static bool acpi_extract_properties(const union acpi_object *desc,
				    struct acpi_device_data *data)
{
	int i;

	if (desc->package.count % 2)
		return false;

	/* Look for the device properties GUID. */
	for (i = 0; i < desc->package.count; i += 2) {
		const union acpi_object *guid, *properties;

		guid = &desc->package.elements[i];
		properties = &desc->package.elements[i + 1];

		/*
		 * The first element must be a GUID and the second one must be
		 * a package.
		 */
		if (guid->type != ACPI_TYPE_BUFFER ||
		    guid->buffer.length != 16 ||
		    properties->type != ACPI_TYPE_PACKAGE)
			break;

		if (!acpi_is_property_guid((guid_t *)guid->buffer.pointer))
			continue;

		/*
		 * We found the matching GUID. Now validate the format of the
		 * package immediately following it.
		 */
		if (!acpi_properties_format_valid(properties))
			continue;

		acpi_data_add_props(data, (const guid_t *)guid->buffer.pointer,
				    properties);
	}

	return !list_empty(&data->properties);
}

void acpi_init_properties(struct acpi_device *adev)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	struct acpi_hardware_id *hwid;
	acpi_status status;
	bool acpi_of = false;

	INIT_LIST_HEAD(&adev->data.properties);
	INIT_LIST_HEAD(&adev->data.subnodes);

	if (!adev->handle)
		return;

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
	if (acpi_enumerate_nondev_subnodes(adev->handle, buf.pointer,
					&adev->data, acpi_fwnode_handle(adev)))
		adev->data.pointer = buf.pointer;

	if (!adev->data.pointer) {
		acpi_handle_debug(adev->handle, "Invalid _DSD data, skipping\n");
		ACPI_FREE(buf.pointer);
	}

 out:
	if (acpi_of && !adev->flags.of_compatible_ok)
		acpi_handle_info(adev->handle,
			 ACPI_DT_NAMESPACE_HID " requires 'compatible' property\n");

	if (!adev->data.pointer)
		acpi_extract_apple_properties(adev);
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
	struct acpi_device_properties *props, *tmp;

	acpi_destroy_nondev_subnodes(&adev->data.subnodes);
	ACPI_FREE((void *)adev->data.pointer);
	adev->data.of_compatible = NULL;
	adev->data.pointer = NULL;
	list_for_each_entry_safe(props, tmp, &adev->data.properties, list) {
		list_del(&props->list);
		kfree(props);
	}
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
static int acpi_data_get_property(const struct acpi_device_data *data,
				  const char *name, acpi_object_type type,
				  const union acpi_object **obj)
{
	const struct acpi_device_properties *props;

	if (!data || !name)
		return -EINVAL;

	if (!data->pointer || list_empty(&data->properties))
		return -EINVAL;

	list_for_each_entry(props, &data->properties, list) {
		const union acpi_object *properties;
		unsigned int i;

		properties = props->properties;
		for (i = 0; i < properties->package.count; i++) {
			const union acpi_object *propname, *propvalue;
			const union acpi_object *property;

			property = &properties->package.elements[i];

			propname = &property->package.elements[0];
			propvalue = &property->package.elements[1];

			if (!strcmp(name, propname->string.pointer)) {
				if (type != ACPI_TYPE_ANY &&
				    propvalue->type != type)
					return -EPROTO;
				if (obj)
					*obj = propvalue;

				return 0;
			}
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
int acpi_dev_get_property(const struct acpi_device *adev, const char *name,
			  acpi_object_type type, const union acpi_object **obj)
{
	return adev ? acpi_data_get_property(&adev->data, name, type, obj) : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_dev_get_property);

static const struct acpi_device_data *
acpi_device_data_of_node(const struct fwnode_handle *fwnode)
{
	if (is_acpi_device_node(fwnode)) {
		const struct acpi_device *adev = to_acpi_device_node(fwnode);
		return &adev->data;
	} else if (is_acpi_data_node(fwnode)) {
		const struct acpi_data_node *dn = to_acpi_data_node(fwnode);
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
int acpi_node_prop_get(const struct fwnode_handle *fwnode,
		       const char *propname, void **valptr)
{
	return acpi_data_get_property(acpi_device_data_of_node(fwnode),
				      propname, ACPI_TYPE_ANY,
				      (const union acpi_object **)valptr);
}

/**
 * acpi_data_get_property_array - return an ACPI array property with given name
 * @data: ACPI data object to get the property from
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
static int acpi_data_get_property_array(const struct acpi_device_data *data,
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

static struct fwnode_handle *
acpi_fwnode_get_named_child_node(const struct fwnode_handle *fwnode,
				 const char *childname)
{
	struct fwnode_handle *child;

	fwnode_for_each_child_node(fwnode, child) {
		if (is_acpi_data_node(child)) {
			if (acpi_data_node_match(child, childname))
				return child;
			continue;
		}

		if (!strncmp(acpi_device_bid(to_acpi_device_node(child)),
			     childname, ACPI_NAMESEG_SIZE))
			return child;
	}

	return NULL;
}

/**
 * __acpi_node_get_property_reference - returns handle to the referenced object
 * @fwnode: Firmware node to get the property from
 * @propname: Name of the property
 * @index: Index of the reference to return
 * @num_args: Maximum number of arguments after each reference
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
 * It is possible to leave holes in the property value set like in the
 * example below:
 *
 * Package () {
 *     "cs-gpios",
 *     Package () {
 *        ^GPIO, 19, 0, 0,
 *        ^GPIO, 20, 0, 0,
 *        0,
 *        ^GPIO, 21, 0, 0,
 *     }
 * }
 *
 * Calling this function with index %2 or index %3 return %-ENOENT. If the
 * property does not contain any more values %-ENOENT is returned. The NULL
 * entry must be single integer and preferably contain value %0.
 *
 * Return: %0 on success, negative error code on failure.
 */
int __acpi_node_get_property_reference(const struct fwnode_handle *fwnode,
	const char *propname, size_t index, size_t num_args,
	struct fwnode_reference_args *args)
{
	const union acpi_object *element, *end;
	const union acpi_object *obj;
	const struct acpi_device_data *data;
	struct acpi_device *device;
	int ret, idx = 0;

	data = acpi_device_data_of_node(fwnode);
	if (!data)
		return -ENOENT;

	ret = acpi_data_get_property(data, propname, ACPI_TYPE_ANY, &obj);
	if (ret)
		return ret == -EINVAL ? -ENOENT : -EINVAL;

	/*
	 * The simplest case is when the value is a single reference.  Just
	 * return that reference then.
	 */
	if (obj->type == ACPI_TYPE_LOCAL_REFERENCE) {
		if (index)
			return -ENOENT;

		device = acpi_fetch_acpi_dev(obj->reference.handle);
		if (!device)
			return -EINVAL;

		args->fwnode = acpi_fwnode_handle(device);
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
	if (obj->type != ACPI_TYPE_PACKAGE)
		return -EINVAL;
	if (index >= obj->package.count)
		return -ENOENT;

	element = obj->package.elements;
	end = element + obj->package.count;

	while (element < end) {
		u32 nargs, i;

		if (element->type == ACPI_TYPE_LOCAL_REFERENCE) {
			struct fwnode_handle *ref_fwnode;

			device = acpi_fetch_acpi_dev(element->reference.handle);
			if (!device)
				return -EINVAL;

			nargs = 0;
			element++;

			/*
			 * Find the referred data extension node under the
			 * referred device node.
			 */
			for (ref_fwnode = acpi_fwnode_handle(device);
			     element < end && element->type == ACPI_TYPE_STRING;
			     element++) {
				ref_fwnode = acpi_fwnode_get_named_child_node(
					ref_fwnode, element->string.pointer);
				if (!ref_fwnode)
					return -EINVAL;
			}

			/* assume following integer elements are all args */
			for (i = 0; element + i < end && i < num_args; i++) {
				int type = element[i].type;

				if (type == ACPI_TYPE_INTEGER)
					nargs++;
				else if (type == ACPI_TYPE_LOCAL_REFERENCE)
					break;
				else
					return -EINVAL;
			}

			if (nargs > NR_FWNODE_REFERENCE_ARGS)
				return -EINVAL;

			if (idx == index) {
				args->fwnode = ref_fwnode;
				args->nargs = nargs;
				for (i = 0; i < nargs; i++)
					args->args[i] = element[i].integer.value;

				return 0;
			}

			element += nargs;
		} else if (element->type == ACPI_TYPE_INTEGER) {
			if (idx == index)
				return -ENOENT;
			element++;
		} else {
			return -EINVAL;
		}

		idx++;
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(__acpi_node_get_property_reference);

static int acpi_data_prop_read_single(const struct acpi_device_data *data,
				      const char *propname,
				      enum dev_prop_type proptype, void *val)
{
	const union acpi_object *obj;
	int ret;

	if (proptype >= DEV_PROP_U8 && proptype <= DEV_PROP_U64) {
		ret = acpi_data_get_property(data, propname, ACPI_TYPE_INTEGER, &obj);
		if (ret)
			return ret;

		switch (proptype) {
		case DEV_PROP_U8:
			if (obj->integer.value > U8_MAX)
				return -EOVERFLOW;

			if (val)
				*(u8 *)val = obj->integer.value;

			break;
		case DEV_PROP_U16:
			if (obj->integer.value > U16_MAX)
				return -EOVERFLOW;

			if (val)
				*(u16 *)val = obj->integer.value;

			break;
		case DEV_PROP_U32:
			if (obj->integer.value > U32_MAX)
				return -EOVERFLOW;

			if (val)
				*(u32 *)val = obj->integer.value;

			break;
		default:
			if (val)
				*(u64 *)val = obj->integer.value;

			break;
		}

		if (!val)
			return 1;
	} else if (proptype == DEV_PROP_STRING) {
		ret = acpi_data_get_property(data, propname, ACPI_TYPE_STRING, &obj);
		if (ret)
			return ret;

		if (val)
			*(char **)val = obj->string.pointer;

		return 1;
	} else {
		ret = -EINVAL;
	}
	return ret;
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
	return nval;
}

static int acpi_data_prop_read(const struct acpi_device_data *data,
			       const char *propname,
			       enum dev_prop_type proptype,
			       void *val, size_t nval)
{
	const union acpi_object *obj;
	const union acpi_object *items;
	int ret;

	if (nval == 1 || !val) {
		ret = acpi_data_prop_read_single(data, propname, proptype, val);
		/*
		 * The overflow error means that the property is there and it is
		 * single-value, but its type does not match, so return.
		 */
		if (ret >= 0 || ret == -EOVERFLOW)
			return ret;

		/*
		 * Reading this property as a single-value one failed, but its
		 * value may still be represented as one-element array, so
		 * continue.
		 */
	}

	ret = acpi_data_get_property_array(data, propname, ACPI_TYPE_ANY, &obj);
	if (ret)
		return ret;

	if (!val)
		return obj->package.count;

	if (proptype != DEV_PROP_STRING && nval > obj->package.count)
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
		ret = acpi_copy_property_array_string(
			items, (char **)val,
			min_t(u32, nval, obj->package.count));
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
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
static int acpi_node_prop_read(const struct fwnode_handle *fwnode,
			       const char *propname, enum dev_prop_type proptype,
			       void *val, size_t nval)
{
	return acpi_data_prop_read(acpi_device_data_of_node(fwnode),
				   propname, proptype, val, nval);
}

/**
 * acpi_get_next_subnode - Return the next child node handle for a fwnode
 * @fwnode: Firmware node to find the next child node for.
 * @child: Handle to one of the device's child nodes or a null handle.
 */
struct fwnode_handle *acpi_get_next_subnode(const struct fwnode_handle *fwnode,
					    struct fwnode_handle *child)
{
	const struct acpi_device *adev = to_acpi_device_node(fwnode);
	const struct list_head *head;
	struct list_head *next;

	if (!child || is_acpi_device_node(child)) {
		struct acpi_device *child_adev;

		if (adev)
			head = &adev->children;
		else
			goto nondev;

		if (list_empty(head))
			goto nondev;

		if (child) {
			adev = to_acpi_device_node(child);
			next = adev->node.next;
			if (next == head) {
				child = NULL;
				goto nondev;
			}
			child_adev = list_entry(next, struct acpi_device, node);
		} else {
			child_adev = list_first_entry(head, struct acpi_device,
						      node);
		}
		return acpi_fwnode_handle(child_adev);
	}

 nondev:
	if (!child || is_acpi_data_node(child)) {
		const struct acpi_data_node *data = to_acpi_data_node(fwnode);
		struct acpi_data_node *dn;

		/*
		 * We can have a combination of device and data nodes, e.g. with
		 * hierarchical _DSD properties. Make sure the adev pointer is
		 * restored before going through data nodes, otherwise we will
		 * be looking for data_nodes below the last device found instead
		 * of the common fwnode shared by device_nodes and data_nodes.
		 */
		adev = to_acpi_device_node(fwnode);
		if (adev)
			head = &adev->data.subnodes;
		else if (data)
			head = &data->data.subnodes;
		else
			return NULL;

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

/**
 * acpi_node_get_parent - Return parent fwnode of this fwnode
 * @fwnode: Firmware node whose parent to get
 *
 * Returns parent node of an ACPI device or data firmware node or %NULL if
 * not available.
 */
static struct fwnode_handle *
acpi_node_get_parent(const struct fwnode_handle *fwnode)
{
	if (is_acpi_data_node(fwnode)) {
		/* All data nodes have parent pointer so just return that */
		return to_acpi_data_node(fwnode)->parent;
	} else if (is_acpi_device_node(fwnode)) {
		struct device *dev = to_acpi_device_node(fwnode)->dev.parent;

		if (dev)
			return acpi_fwnode_handle(to_acpi_device(dev));
	}

	return NULL;
}

/*
 * Return true if the node is an ACPI graph node. Called on either ports
 * or endpoints.
 */
static bool is_acpi_graph_node(struct fwnode_handle *fwnode,
			       const char *str)
{
	unsigned int len = strlen(str);
	const char *name;

	if (!len || !is_acpi_data_node(fwnode))
		return false;

	name = to_acpi_data_node(fwnode)->name;

	return (fwnode_property_present(fwnode, "reg") &&
		!strncmp(name, str, len) && name[len] == '@') ||
		fwnode_property_present(fwnode, str);
}

/**
 * acpi_graph_get_next_endpoint - Get next endpoint ACPI firmware node
 * @fwnode: Pointer to the parent firmware node
 * @prev: Previous endpoint node or %NULL to get the first
 *
 * Looks up next endpoint ACPI firmware node below a given @fwnode. Returns
 * %NULL if there is no next endpoint or in case of error. In case of success
 * the next endpoint is returned.
 */
static struct fwnode_handle *acpi_graph_get_next_endpoint(
	const struct fwnode_handle *fwnode, struct fwnode_handle *prev)
{
	struct fwnode_handle *port = NULL;
	struct fwnode_handle *endpoint;

	if (!prev) {
		do {
			port = fwnode_get_next_child_node(fwnode, port);
			/*
			 * The names of the port nodes begin with "port@"
			 * followed by the number of the port node and they also
			 * have a "reg" property that also has the number of the
			 * port node. For compatibility reasons a node is also
			 * recognised as a port node from the "port" property.
			 */
			if (is_acpi_graph_node(port, "port"))
				break;
		} while (port);
	} else {
		port = fwnode_get_parent(prev);
	}

	if (!port)
		return NULL;

	endpoint = fwnode_get_next_child_node(port, prev);
	while (!endpoint) {
		port = fwnode_get_next_child_node(fwnode, port);
		if (!port)
			break;
		if (is_acpi_graph_node(port, "port"))
			endpoint = fwnode_get_next_child_node(port, NULL);
	}

	/*
	 * The names of the endpoint nodes begin with "endpoint@" followed by
	 * the number of the endpoint node and they also have a "reg" property
	 * that also has the number of the endpoint node. For compatibility
	 * reasons a node is also recognised as an endpoint node from the
	 * "endpoint" property.
	 */
	if (!is_acpi_graph_node(endpoint, "endpoint"))
		return NULL;

	return endpoint;
}

/**
 * acpi_graph_get_child_prop_value - Return a child with a given property value
 * @fwnode: device fwnode
 * @prop_name: The name of the property to look for
 * @val: the desired property value
 *
 * Return the port node corresponding to a given port number. Returns
 * the child node on success, NULL otherwise.
 */
static struct fwnode_handle *acpi_graph_get_child_prop_value(
	const struct fwnode_handle *fwnode, const char *prop_name,
	unsigned int val)
{
	struct fwnode_handle *child;

	fwnode_for_each_child_node(fwnode, child) {
		u32 nr;

		if (fwnode_property_read_u32(child, prop_name, &nr))
			continue;

		if (val == nr)
			return child;
	}

	return NULL;
}


/**
 * acpi_graph_get_remote_endpoint - Parses and returns remote end of an endpoint
 * @__fwnode: Endpoint firmware node pointing to a remote device
 *
 * Returns the remote endpoint corresponding to @__fwnode. NULL on error.
 */
static struct fwnode_handle *
acpi_graph_get_remote_endpoint(const struct fwnode_handle *__fwnode)
{
	struct fwnode_handle *fwnode;
	unsigned int port_nr, endpoint_nr;
	struct fwnode_reference_args args;
	int ret;

	memset(&args, 0, sizeof(args));
	ret = acpi_node_get_property_reference(__fwnode, "remote-endpoint", 0,
					       &args);
	if (ret)
		return NULL;

	/* Direct endpoint reference? */
	if (!is_acpi_device_node(args.fwnode))
		return args.nargs ? NULL : args.fwnode;

	/*
	 * Always require two arguments with the reference: port and
	 * endpoint indices.
	 */
	if (args.nargs != 2)
		return NULL;

	fwnode = args.fwnode;
	port_nr = args.args[0];
	endpoint_nr = args.args[1];

	fwnode = acpi_graph_get_child_prop_value(fwnode, "port", port_nr);

	return acpi_graph_get_child_prop_value(fwnode, "endpoint", endpoint_nr);
}

static bool acpi_fwnode_device_is_available(const struct fwnode_handle *fwnode)
{
	if (!is_acpi_device_node(fwnode))
		return false;

	return acpi_device_is_present(to_acpi_device_node(fwnode));
}

static bool acpi_fwnode_property_present(const struct fwnode_handle *fwnode,
					 const char *propname)
{
	return !acpi_node_prop_get(fwnode, propname, NULL);
}

static int
acpi_fwnode_property_read_int_array(const struct fwnode_handle *fwnode,
				    const char *propname,
				    unsigned int elem_size, void *val,
				    size_t nval)
{
	enum dev_prop_type type;

	switch (elem_size) {
	case sizeof(u8):
		type = DEV_PROP_U8;
		break;
	case sizeof(u16):
		type = DEV_PROP_U16;
		break;
	case sizeof(u32):
		type = DEV_PROP_U32;
		break;
	case sizeof(u64):
		type = DEV_PROP_U64;
		break;
	default:
		return -ENXIO;
	}

	return acpi_node_prop_read(fwnode, propname, type, val, nval);
}

static int
acpi_fwnode_property_read_string_array(const struct fwnode_handle *fwnode,
				       const char *propname, const char **val,
				       size_t nval)
{
	return acpi_node_prop_read(fwnode, propname, DEV_PROP_STRING,
				   val, nval);
}

static int
acpi_fwnode_get_reference_args(const struct fwnode_handle *fwnode,
			       const char *prop, const char *nargs_prop,
			       unsigned int args_count, unsigned int index,
			       struct fwnode_reference_args *args)
{
	return __acpi_node_get_property_reference(fwnode, prop, index,
						  args_count, args);
}

static const char *acpi_fwnode_get_name(const struct fwnode_handle *fwnode)
{
	const struct acpi_device *adev;
	struct fwnode_handle *parent;

	/* Is this the root node? */
	parent = fwnode_get_parent(fwnode);
	if (!parent)
		return "\\";

	fwnode_handle_put(parent);

	if (is_acpi_data_node(fwnode)) {
		const struct acpi_data_node *dn = to_acpi_data_node(fwnode);

		return dn->name;
	}

	adev = to_acpi_device_node(fwnode);
	if (WARN_ON(!adev))
		return NULL;

	return acpi_device_bid(adev);
}

static const char *
acpi_fwnode_get_name_prefix(const struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent;

	/* Is this the root node? */
	parent = fwnode_get_parent(fwnode);
	if (!parent)
		return "";

	/* Is this 2nd node from the root? */
	parent = fwnode_get_next_parent(parent);
	if (!parent)
		return "";

	fwnode_handle_put(parent);

	/* ACPI device or data node. */
	return ".";
}

static struct fwnode_handle *
acpi_fwnode_get_parent(struct fwnode_handle *fwnode)
{
	return acpi_node_get_parent(fwnode);
}

static int acpi_fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
					    struct fwnode_endpoint *endpoint)
{
	struct fwnode_handle *port_fwnode = fwnode_get_parent(fwnode);

	endpoint->local_fwnode = fwnode;

	if (fwnode_property_read_u32(port_fwnode, "reg", &endpoint->port))
		fwnode_property_read_u32(port_fwnode, "port", &endpoint->port);
	if (fwnode_property_read_u32(fwnode, "reg", &endpoint->id))
		fwnode_property_read_u32(fwnode, "endpoint", &endpoint->id);

	return 0;
}

static const void *
acpi_fwnode_device_get_match_data(const struct fwnode_handle *fwnode,
				  const struct device *dev)
{
	return acpi_device_get_match_data(dev);
}

#define DECLARE_ACPI_FWNODE_OPS(ops) \
	const struct fwnode_operations ops = {				\
		.device_is_available = acpi_fwnode_device_is_available, \
		.device_get_match_data = acpi_fwnode_device_get_match_data, \
		.property_present = acpi_fwnode_property_present,	\
		.property_read_int_array =				\
			acpi_fwnode_property_read_int_array,		\
		.property_read_string_array =				\
			acpi_fwnode_property_read_string_array,		\
		.get_parent = acpi_node_get_parent,			\
		.get_next_child_node = acpi_get_next_subnode,		\
		.get_named_child_node = acpi_fwnode_get_named_child_node, \
		.get_name = acpi_fwnode_get_name,			\
		.get_name_prefix = acpi_fwnode_get_name_prefix,		\
		.get_reference_args = acpi_fwnode_get_reference_args,	\
		.graph_get_next_endpoint =				\
			acpi_graph_get_next_endpoint,			\
		.graph_get_remote_endpoint =				\
			acpi_graph_get_remote_endpoint,			\
		.graph_get_port_parent = acpi_fwnode_get_parent,	\
		.graph_parse_endpoint = acpi_fwnode_graph_parse_endpoint, \
	};								\
	EXPORT_SYMBOL_GPL(ops)

DECLARE_ACPI_FWNODE_OPS(acpi_device_fwnode_ops);
DECLARE_ACPI_FWNODE_OPS(acpi_data_fwnode_ops);
const struct fwnode_operations acpi_static_fwnode_ops;

bool is_acpi_device_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) &&
		fwnode->ops == &acpi_device_fwnode_ops;
}
EXPORT_SYMBOL(is_acpi_device_node);

bool is_acpi_data_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &acpi_data_fwnode_ops;
}
EXPORT_SYMBOL(is_acpi_data_node);
