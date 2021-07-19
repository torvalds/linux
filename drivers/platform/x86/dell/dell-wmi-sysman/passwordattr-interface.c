// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to SET password methods under BIOS attributes interface GUID
 *
 *  Copyright (c) 2020 Dell Inc.
 */

#include <linux/wmi.h>
#include "dell-wmi-sysman.h"

static int call_password_interface(struct wmi_device *wdev, char *in_args, size_t size)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer input;
	union acpi_object *obj;
	acpi_status status;
	int ret = -EIO;

	input.length =  (acpi_size) size;
	input.pointer = in_args;
	status = wmidev_evaluate_method(wdev, 0, 1, &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;
	obj = (union acpi_object *)output.pointer;
	if (obj->type == ACPI_TYPE_INTEGER)
		ret = obj->integer.value;

	kfree(output.pointer);
	/* let userland know it may need to check is_password_set again */
	kobject_uevent(&wmi_priv.class_dev->kobj, KOBJ_CHANGE);
	return map_wmi_error(ret);
}

/**
 * set_new_password() - Sets a system admin password
 * @password_type: The type of password to set
 * @new: The new password
 *
 * Sets the password using plaintext interface
 */
int set_new_password(const char *password_type, const char *new)
{
	size_t password_type_size, current_password_size, new_size;
	size_t security_area_size, buffer_size;
	char *buffer = NULL, *start;
	char *current_password;
	int ret;

	mutex_lock(&wmi_priv.mutex);
	if (!wmi_priv.password_attr_wdev) {
		ret = -ENODEV;
		goto out;
	}
	if (strcmp(password_type, "Admin") == 0) {
		current_password = wmi_priv.current_admin_password;
	} else if (strcmp(password_type, "System") == 0) {
		current_password = wmi_priv.current_system_password;
	} else {
		ret = -EINVAL;
		dev_err(&wmi_priv.password_attr_wdev->dev, "unknown password type %s\n",
			password_type);
		goto out;
	}

	/* build/calculate buffer */
	security_area_size = calculate_security_buffer(wmi_priv.current_admin_password);
	password_type_size = calculate_string_buffer(password_type);
	current_password_size = calculate_string_buffer(current_password);
	new_size = calculate_string_buffer(new);
	buffer_size = security_area_size + password_type_size + current_password_size + new_size;
	buffer = kzalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out;
	}

	/* build security area */
	populate_security_buffer(buffer, wmi_priv.current_admin_password);

	/* build variables to set */
	start = buffer + security_area_size;
	ret = populate_string_buffer(start, password_type_size, password_type);
	if (ret < 0)
		goto out;

	start += ret;
	ret = populate_string_buffer(start, current_password_size, current_password);
	if (ret < 0)
		goto out;

	start += ret;
	ret = populate_string_buffer(start, new_size, new);
	if (ret < 0)
		goto out;

	print_hex_dump_bytes("set new password data: ", DUMP_PREFIX_NONE, buffer, buffer_size);
	ret = call_password_interface(wmi_priv.password_attr_wdev, buffer, buffer_size);
	/* clear current_password here and use user input from wmi_priv.current_password */
	if (!ret)
		memset(current_password, 0, MAX_BUFF);
	/* explain to user the detailed failure reason */
	else if (ret == -EOPNOTSUPP)
		dev_err(&wmi_priv.password_attr_wdev->dev, "admin password must be configured\n");
	else if (ret == -EACCES)
		dev_err(&wmi_priv.password_attr_wdev->dev, "invalid password\n");

out:
	kfree(buffer);
	mutex_unlock(&wmi_priv.mutex);

	return ret;
}

static int bios_attr_pass_interface_probe(struct wmi_device *wdev, const void *context)
{
	mutex_lock(&wmi_priv.mutex);
	wmi_priv.password_attr_wdev = wdev;
	mutex_unlock(&wmi_priv.mutex);
	return 0;
}

static void bios_attr_pass_interface_remove(struct wmi_device *wdev)
{
	mutex_lock(&wmi_priv.mutex);
	wmi_priv.password_attr_wdev = NULL;
	mutex_unlock(&wmi_priv.mutex);
}

static const struct wmi_device_id bios_attr_pass_interface_id_table[] = {
	{ .guid_string = DELL_WMI_BIOS_PASSWORD_INTERFACE_GUID },
	{ },
};
static struct wmi_driver bios_attr_pass_interface_driver = {
	.driver = {
		.name = DRIVER_NAME"-password"
	},
	.probe = bios_attr_pass_interface_probe,
	.remove = bios_attr_pass_interface_remove,
	.id_table = bios_attr_pass_interface_id_table,
};

int init_bios_attr_pass_interface(void)
{
	return wmi_driver_register(&bios_attr_pass_interface_driver);
}

void exit_bios_attr_pass_interface(void)
{
	wmi_driver_unregister(&bios_attr_pass_interface_driver);
}

MODULE_DEVICE_TABLE(wmi, bios_attr_pass_interface_id_table);
