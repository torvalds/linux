/*
 * drivers/acpi/device_sysfs.c - ACPI device sysfs attributes and modalias.
 *
 * Copyright (C) 2015, Intel Corp.
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/nls.h>

#include "internal.h"

static ssize_t acpi_object_path(acpi_handle handle, char *buf)
{
	struct acpi_buffer path = {ACPI_ALLOCATE_BUFFER, NULL};
	int result;

	result = acpi_get_name(handle, ACPI_FULL_PATHNAME, &path);
	if (result)
		return result;

	result = sprintf(buf, "%s\n", (char *)path.pointer);
	kfree(path.pointer);
	return result;
}

struct acpi_data_node_attr {
	struct attribute attr;
	ssize_t (*show)(struct acpi_data_node *, char *);
	ssize_t (*store)(struct acpi_data_node *, const char *, size_t count);
};

#define DATA_NODE_ATTR(_name)			\
	static struct acpi_data_node_attr data_node_##_name =	\
		__ATTR(_name, 0444, data_node_show_##_name, NULL)

static ssize_t data_node_show_path(struct acpi_data_node *dn, char *buf)
{
	return acpi_object_path(dn->handle, buf);
}

DATA_NODE_ATTR(path);

static struct attribute *acpi_data_node_default_attrs[] = {
	&data_node_path.attr,
	NULL
};

#define to_data_node(k) container_of(k, struct acpi_data_node, kobj)
#define to_attr(a) container_of(a, struct acpi_data_node_attr, attr)

static ssize_t acpi_data_node_attr_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct acpi_data_node *dn = to_data_node(kobj);
	struct acpi_data_node_attr *dn_attr = to_attr(attr);

	return dn_attr->show ? dn_attr->show(dn, buf) : -ENXIO;
}

static const struct sysfs_ops acpi_data_node_sysfs_ops = {
	.show	= acpi_data_node_attr_show,
};

static void acpi_data_node_release(struct kobject *kobj)
{
	struct acpi_data_node *dn = to_data_node(kobj);
	complete(&dn->kobj_done);
}

static struct kobj_type acpi_data_node_ktype = {
	.sysfs_ops = &acpi_data_node_sysfs_ops,
	.default_attrs = acpi_data_node_default_attrs,
	.release = acpi_data_node_release,
};

static void acpi_expose_nondev_subnodes(struct kobject *kobj,
					struct acpi_device_data *data)
{
	struct list_head *list = &data->subnodes;
	struct acpi_data_node *dn;

	if (list_empty(list))
		return;

	list_for_each_entry(dn, list, sibling) {
		int ret;

		init_completion(&dn->kobj_done);
		ret = kobject_init_and_add(&dn->kobj, &acpi_data_node_ktype,
					   kobj, "%s", dn->name);
		if (ret)
			acpi_handle_err(dn->handle, "Failed to expose (%d)\n", ret);
		else
			acpi_expose_nondev_subnodes(&dn->kobj, &dn->data);
	}
}

static void acpi_hide_nondev_subnodes(struct acpi_device_data *data)
{
	struct list_head *list = &data->subnodes;
	struct acpi_data_node *dn;

	if (list_empty(list))
		return;

	list_for_each_entry_reverse(dn, list, sibling) {
		acpi_hide_nondev_subnodes(&dn->data);
		kobject_put(&dn->kobj);
	}
}

/**
 * create_pnp_modalias - Create hid/cid(s) string for modalias and uevent
 * @acpi_dev: ACPI device object.
 * @modalias: Buffer to print into.
 * @size: Size of the buffer.
 *
 * Creates hid/cid(s) string needed for modalias and uevent
 * e.g. on a device with hid:IBM0001 and cid:ACPI0001 you get:
 * char *modalias: "acpi:IBM0001:ACPI0001"
 * Return: 0: no _HID and no _CID
 *         -EINVAL: output error
 *         -ENOMEM: output is truncated
*/
static int create_pnp_modalias(struct acpi_device *acpi_dev, char *modalias,
			       int size)
{
	int len;
	int count;
	struct acpi_hardware_id *id;

	/*
	 * Since we skip ACPI_DT_NAMESPACE_HID from the modalias below, 0 should
	 * be returned if ACPI_DT_NAMESPACE_HID is the only ACPI/PNP ID in the
	 * device's list.
	 */
	count = 0;
	list_for_each_entry(id, &acpi_dev->pnp.ids, list)
		if (strcmp(id->id, ACPI_DT_NAMESPACE_HID))
			count++;

	if (!count)
		return 0;

	len = snprintf(modalias, size, "acpi:");
	if (len <= 0)
		return len;

	size -= len;

	list_for_each_entry(id, &acpi_dev->pnp.ids, list) {
		if (!strcmp(id->id, ACPI_DT_NAMESPACE_HID))
			continue;

		count = snprintf(&modalias[len], size, "%s:", id->id);
		if (count < 0)
			return -EINVAL;

		if (count >= size)
			return -ENOMEM;

		len += count;
		size -= count;
	}
	modalias[len] = '\0';
	return len;
}

/**
 * create_of_modalias - Creates DT compatible string for modalias and uevent
 * @acpi_dev: ACPI device object.
 * @modalias: Buffer to print into.
 * @size: Size of the buffer.
 *
 * Expose DT compatible modalias as of:NnameTCcompatible.  This function should
 * only be called for devices having ACPI_DT_NAMESPACE_HID in their list of
 * ACPI/PNP IDs.
 */
static int create_of_modalias(struct acpi_device *acpi_dev, char *modalias,
			      int size)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	const union acpi_object *of_compatible, *obj;
	int len, count;
	int i, nval;
	char *c;

	acpi_get_name(acpi_dev->handle, ACPI_SINGLE_NAME, &buf);
	/* DT strings are all in lower case */
	for (c = buf.pointer; *c != '\0'; c++)
		*c = tolower(*c);

	len = snprintf(modalias, size, "of:N%sT", (char *)buf.pointer);
	ACPI_FREE(buf.pointer);

	if (len <= 0)
		return len;

	of_compatible = acpi_dev->data.of_compatible;
	if (of_compatible->type == ACPI_TYPE_PACKAGE) {
		nval = of_compatible->package.count;
		obj = of_compatible->package.elements;
	} else { /* Must be ACPI_TYPE_STRING. */
		nval = 1;
		obj = of_compatible;
	}
	for (i = 0; i < nval; i++, obj++) {
		count = snprintf(&modalias[len], size, "C%s",
				 obj->string.pointer);
		if (count < 0)
			return -EINVAL;

		if (count >= size)
			return -ENOMEM;

		len += count;
		size -= count;
	}
	modalias[len] = '\0';
	return len;
}

int __acpi_device_uevent_modalias(struct acpi_device *adev,
				  struct kobj_uevent_env *env)
{
	int len;

	if (!adev)
		return -ENODEV;

	if (list_empty(&adev->pnp.ids))
		return 0;

	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	len = create_pnp_modalias(adev, &env->buf[env->buflen - 1],
				  sizeof(env->buf) - env->buflen);
	if (len < 0)
		return len;

	env->buflen += len;
	if (!adev->data.of_compatible)
		return 0;

	if (len > 0 && add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	len = create_of_modalias(adev, &env->buf[env->buflen - 1],
				 sizeof(env->buf) - env->buflen);
	if (len < 0)
		return len;

	env->buflen += len;

	return 0;
}

/**
 * acpi_device_uevent_modalias - uevent modalias for ACPI-enumerated devices.
 *
 * Create the uevent modalias field for ACPI-enumerated devices.
 *
 * Because other buses do not support ACPI HIDs & CIDs, e.g. for a device with
 * hid:IBM0001 and cid:ACPI0001 you get: "acpi:IBM0001:ACPI0001".
 */
int acpi_device_uevent_modalias(struct device *dev, struct kobj_uevent_env *env)
{
	return __acpi_device_uevent_modalias(acpi_companion_match(dev), env);
}
EXPORT_SYMBOL_GPL(acpi_device_uevent_modalias);

static int __acpi_device_modalias(struct acpi_device *adev, char *buf, int size)
{
	int len, count;

	if (!adev)
		return -ENODEV;

	if (list_empty(&adev->pnp.ids))
		return 0;

	len = create_pnp_modalias(adev, buf, size - 1);
	if (len < 0) {
		return len;
	} else if (len > 0) {
		buf[len++] = '\n';
		size -= len;
	}
	if (!adev->data.of_compatible)
		return len;

	count = create_of_modalias(adev, buf + len, size - 1);
	if (count < 0) {
		return count;
	} else if (count > 0) {
		len += count;
		buf[len++] = '\n';
	}

	return len;
}

/**
 * acpi_device_modalias - modalias sysfs attribute for ACPI-enumerated devices.
 *
 * Create the modalias sysfs attribute for ACPI-enumerated devices.
 *
 * Because other buses do not support ACPI HIDs & CIDs, e.g. for a device with
 * hid:IBM0001 and cid:ACPI0001 you get: "acpi:IBM0001:ACPI0001".
 */
int acpi_device_modalias(struct device *dev, char *buf, int size)
{
	return __acpi_device_modalias(acpi_companion_match(dev), buf, size);
}
EXPORT_SYMBOL_GPL(acpi_device_modalias);

static ssize_t
acpi_device_modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return __acpi_device_modalias(to_acpi_device(dev), buf, 1024);
}
static DEVICE_ATTR(modalias, 0444, acpi_device_modalias_show, NULL);

static ssize_t real_power_state_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct acpi_device *adev = to_acpi_device(dev);
	int state;
	int ret;

	ret = acpi_device_get_power(adev, &state);
	if (ret)
		return ret;

	return sprintf(buf, "%s\n", acpi_power_state_string(state));
}

static DEVICE_ATTR(real_power_state, 0444, real_power_state_show, NULL);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct acpi_device *adev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_power_state_string(adev->power.state));
}

static DEVICE_ATTR(power_state, 0444, power_state_show, NULL);

static ssize_t
acpi_eject_store(struct device *d, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct acpi_device *acpi_device = to_acpi_device(d);
	acpi_object_type not_used;
	acpi_status status;

	if (!count || buf[0] != '1')
		return -EINVAL;

	if ((!acpi_device->handler || !acpi_device->handler->hotplug.enabled)
	    && !acpi_device->driver)
		return -ENODEV;

	status = acpi_get_type(acpi_device->handle, &not_used);
	if (ACPI_FAILURE(status) || !acpi_device->flags.ejectable)
		return -ENODEV;

	get_device(&acpi_device->dev);
	status = acpi_hotplug_schedule(acpi_device, ACPI_OST_EC_OSPM_EJECT);
	if (ACPI_SUCCESS(status))
		return count;

	put_device(&acpi_device->dev);
	acpi_evaluate_ost(acpi_device->handle, ACPI_OST_EC_OSPM_EJECT,
			  ACPI_OST_SC_NON_SPECIFIC_FAILURE, NULL);
	return status == AE_NO_MEMORY ? -ENOMEM : -EAGAIN;
}

static DEVICE_ATTR(eject, 0200, NULL, acpi_eject_store);

static ssize_t
acpi_device_hid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_device_hid(acpi_dev));
}
static DEVICE_ATTR(hid, 0444, acpi_device_hid_show, NULL);

static ssize_t acpi_device_uid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_dev->pnp.unique_id);
}
static DEVICE_ATTR(uid, 0444, acpi_device_uid_show, NULL);

static ssize_t acpi_device_adr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "0x%08x\n",
		       (unsigned int)(acpi_dev->pnp.bus_address));
}
static DEVICE_ATTR(adr, 0444, acpi_device_adr_show, NULL);

static ssize_t acpi_device_path_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return acpi_object_path(acpi_dev->handle, buf);
}
static DEVICE_ATTR(path, 0444, acpi_device_path_show, NULL);

/* sysfs file that shows description text from the ACPI _STR method */
static ssize_t description_show(struct device *dev,
				struct device_attribute *attr,
				char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int result;

	if (acpi_dev->pnp.str_obj == NULL)
		return 0;

	/*
	 * The _STR object contains a Unicode identifier for a device.
	 * We need to convert to utf-8 so it can be displayed.
	 */
	result = utf16s_to_utf8s(
		(wchar_t *)acpi_dev->pnp.str_obj->buffer.pointer,
		acpi_dev->pnp.str_obj->buffer.length,
		UTF16_LITTLE_ENDIAN, buf,
		PAGE_SIZE);

	buf[result++] = '\n';

	return result;
}
static DEVICE_ATTR(description, 0444, description_show, NULL);

static ssize_t
acpi_device_sun_show(struct device *dev, struct device_attribute *attr,
		     char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	acpi_status status;
	unsigned long long sun;

	status = acpi_evaluate_integer(acpi_dev->handle, "_SUN", NULL, &sun);
	if (ACPI_FAILURE(status))
		return -EIO;

	return sprintf(buf, "%llu\n", sun);
}
static DEVICE_ATTR(sun, 0444, acpi_device_sun_show, NULL);

static ssize_t
acpi_device_hrv_show(struct device *dev, struct device_attribute *attr,
		     char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	acpi_status status;
	unsigned long long hrv;

	status = acpi_evaluate_integer(acpi_dev->handle, "_HRV", NULL, &hrv);
	if (ACPI_FAILURE(status))
		return -EIO;

	return sprintf(buf, "%llu\n", hrv);
}
static DEVICE_ATTR(hrv, 0444, acpi_device_hrv_show, NULL);

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
				char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	acpi_status status;
	unsigned long long sta;

	status = acpi_evaluate_integer(acpi_dev->handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return -EIO;

	return sprintf(buf, "%llu\n", sta);
}
static DEVICE_ATTR_RO(status);

/**
 * acpi_device_setup_files - Create sysfs attributes of an ACPI device.
 * @dev: ACPI device object.
 */
int acpi_device_setup_files(struct acpi_device *dev)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;
	int result = 0;

	/*
	 * Devices gotten from FADT don't have a "path" attribute
	 */
	if (dev->handle) {
		result = device_create_file(&dev->dev, &dev_attr_path);
		if (result)
			goto end;
	}

	if (!list_empty(&dev->pnp.ids)) {
		result = device_create_file(&dev->dev, &dev_attr_hid);
		if (result)
			goto end;

		result = device_create_file(&dev->dev, &dev_attr_modalias);
		if (result)
			goto end;
	}

	/*
	 * If device has _STR, 'description' file is created
	 */
	if (acpi_has_method(dev->handle, "_STR")) {
		status = acpi_evaluate_object(dev->handle, "_STR",
					NULL, &buffer);
		if (ACPI_FAILURE(status))
			buffer.pointer = NULL;
		dev->pnp.str_obj = buffer.pointer;
		result = device_create_file(&dev->dev, &dev_attr_description);
		if (result)
			goto end;
	}

	if (dev->pnp.type.bus_address)
		result = device_create_file(&dev->dev, &dev_attr_adr);
	if (dev->pnp.unique_id)
		result = device_create_file(&dev->dev, &dev_attr_uid);

	if (acpi_has_method(dev->handle, "_SUN")) {
		result = device_create_file(&dev->dev, &dev_attr_sun);
		if (result)
			goto end;
	}

	if (acpi_has_method(dev->handle, "_HRV")) {
		result = device_create_file(&dev->dev, &dev_attr_hrv);
		if (result)
			goto end;
	}

	if (acpi_has_method(dev->handle, "_STA")) {
		result = device_create_file(&dev->dev, &dev_attr_status);
		if (result)
			goto end;
	}

	/*
	 * If device has _EJ0, 'eject' file is created that is used to trigger
	 * hot-removal function from userland.
	 */
	if (acpi_has_method(dev->handle, "_EJ0")) {
		result = device_create_file(&dev->dev, &dev_attr_eject);
		if (result)
			return result;
	}

	if (dev->flags.power_manageable) {
		result = device_create_file(&dev->dev, &dev_attr_power_state);
		if (result)
			return result;

		if (dev->power.flags.power_resources)
			result = device_create_file(&dev->dev,
						    &dev_attr_real_power_state);
	}

	acpi_expose_nondev_subnodes(&dev->dev.kobj, &dev->data);

end:
	return result;
}

/**
 * acpi_device_remove_files - Remove sysfs attributes of an ACPI device.
 * @dev: ACPI device object.
 */
void acpi_device_remove_files(struct acpi_device *dev)
{
	acpi_hide_nondev_subnodes(&dev->data);

	if (dev->flags.power_manageable) {
		device_remove_file(&dev->dev, &dev_attr_power_state);
		if (dev->power.flags.power_resources)
			device_remove_file(&dev->dev,
					   &dev_attr_real_power_state);
	}

	/*
	 * If device has _STR, remove 'description' file
	 */
	if (acpi_has_method(dev->handle, "_STR")) {
		kfree(dev->pnp.str_obj);
		device_remove_file(&dev->dev, &dev_attr_description);
	}
	/*
	 * If device has _EJ0, remove 'eject' file.
	 */
	if (acpi_has_method(dev->handle, "_EJ0"))
		device_remove_file(&dev->dev, &dev_attr_eject);

	if (acpi_has_method(dev->handle, "_SUN"))
		device_remove_file(&dev->dev, &dev_attr_sun);

	if (acpi_has_method(dev->handle, "_HRV"))
		device_remove_file(&dev->dev, &dev_attr_hrv);

	if (dev->pnp.unique_id)
		device_remove_file(&dev->dev, &dev_attr_uid);
	if (dev->pnp.type.bus_address)
		device_remove_file(&dev->dev, &dev_attr_adr);
	device_remove_file(&dev->dev, &dev_attr_modalias);
	device_remove_file(&dev->dev, &dev_attr_hid);
	if (acpi_has_method(dev->handle, "_STA"))
		device_remove_file(&dev->dev, &dev_attr_status);
	if (dev->handle)
		device_remove_file(&dev->dev, &dev_attr_path);
}
