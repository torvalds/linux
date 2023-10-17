// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to methods under BIOS interface GUID
 * for use with hp-bioscfg driver.
 *
 *  Copyright (c) 2022 Hewlett-Packard Inc.
 */

#include <linux/wmi.h>
#include "bioscfg.h"

/*
 * struct bios_args buffer is dynamically allocated.  New WMI command types
 * were introduced that exceeds 128-byte data size.  Changes to handle
 * the data size allocation scheme were kept in hp_wmi_perform_query function.
 */
struct bios_args {
	u32 signature;
	u32 command;
	u32 commandtype;
	u32 datasize;
	u8 data[];
};

/**
 * hp_set_attribute
 *
 * @a_name: The attribute name
 * @a_value: The attribute value
 *
 * Sets an attribute to new value
 *
 * Returns zero on success
 *	-ENODEV if device is not found
 *	-EINVAL if the instance of 'Setup Admin' password is not found.
 *	-ENOMEM unable to allocate memory
 */
int hp_set_attribute(const char *a_name, const char *a_value)
{
	int security_area_size;
	int a_name_size, a_value_size;
	u16 *buffer = NULL;
	u16 *start;
	int  buffer_size, instance, ret;
	char *auth_token_choice;

	mutex_lock(&bioscfg_drv.mutex);

	instance = hp_get_password_instance_for_type(SETUP_PASSWD);
	if (instance < 0) {
		ret = -EINVAL;
		goto out_set_attribute;
	}

	/* Select which auth token to use; password or [auth token] */
	if (bioscfg_drv.spm_data.auth_token)
		auth_token_choice = bioscfg_drv.spm_data.auth_token;
	else
		auth_token_choice = bioscfg_drv.password_data[instance].current_password;

	a_name_size = hp_calculate_string_buffer(a_name);
	a_value_size = hp_calculate_string_buffer(a_value);
	security_area_size = hp_calculate_security_buffer(auth_token_choice);
	buffer_size = a_name_size + a_value_size + security_area_size;

	buffer = kmalloc(buffer_size + 1, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_set_attribute;
	}

	/* build variables to set */
	start = buffer;
	start = hp_ascii_to_utf16_unicode(start, a_name);
	if (!start) {
		ret = -EINVAL;
		goto out_set_attribute;
	}

	start = hp_ascii_to_utf16_unicode(start, a_value);
	if (!start) {
		ret = -EINVAL;
		goto out_set_attribute;
	}

	ret = hp_populate_security_buffer(start, auth_token_choice);
	if (ret < 0)
		goto out_set_attribute;

	ret = hp_wmi_set_bios_setting(buffer, buffer_size);

out_set_attribute:
	kfree(buffer);
	mutex_unlock(&bioscfg_drv.mutex);
	return ret;
}

/**
 * hp_wmi_perform_query
 *
 * @query:	The commandtype (enum hp_wmi_commandtype)
 * @command:	The command (enum hp_wmi_command)
 * @buffer:	Buffer used as input and/or output
 * @insize:	Size of input buffer
 * @outsize:	Size of output buffer
 *
 * returns zero on success
 *         an HP WMI query specific error code (which is positive)
 *         -EINVAL if the query was not successful at all
 *         -EINVAL if the output buffer size exceeds buffersize
 *
 * Note: The buffersize must at least be the maximum of the input and output
 *       size. E.g. Battery info query is defined to have 1 byte input
 *       and 128 byte output. The caller would do:
 *       buffer = kzalloc(128, GFP_KERNEL);
 *       ret = hp_wmi_perform_query(HPWMI_BATTERY_QUERY, HPWMI_READ,
 *				    buffer, 1, 128)
 */
int hp_wmi_perform_query(int query, enum hp_wmi_command command, void *buffer,
			 u32 insize, u32 outsize)
{
	struct acpi_buffer input, output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct bios_return *bios_return;
	union acpi_object *obj = NULL;
	struct bios_args *args = NULL;
	int mid, actual_outsize, ret;
	size_t bios_args_size;

	mid = hp_encode_outsize_for_pvsz(outsize);
	if (WARN_ON(mid < 0))
		return mid;

	bios_args_size = struct_size(args, data, insize);
	args = kmalloc(bios_args_size, GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	input.length = bios_args_size;
	input.pointer = args;

	/* BIOS expects 'SECU' in hex as the signature value*/
	args->signature = 0x55434553;
	args->command = command;
	args->commandtype = query;
	args->datasize = insize;
	memcpy(args->data, buffer, flex_array_size(args, data, insize));

	ret = wmi_evaluate_method(HP_WMI_BIOS_GUID, 0, mid, &input, &output);
	if (ret)
		goto out_free;

	obj = output.pointer;
	if (!obj) {
		ret = -EINVAL;
		goto out_free;
	}

	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(*bios_return)) {
		pr_warn("query 0x%x returned wrong type or too small buffer\n", query);
		ret = -EINVAL;
		goto out_free;
	}

	bios_return = (struct bios_return *)obj->buffer.pointer;
	ret = bios_return->return_code;
	if (ret) {
		if (ret != INVALID_CMD_VALUE && ret != INVALID_CMD_TYPE)
			pr_warn("query 0x%x returned error 0x%x\n", query, ret);
		goto out_free;
	}

	/* Ignore output data of zero size */
	if (!outsize)
		goto out_free;

	actual_outsize = min_t(u32, outsize, obj->buffer.length - sizeof(*bios_return));
	memcpy_and_pad(buffer, outsize, obj->buffer.pointer + sizeof(*bios_return),
		       actual_outsize, 0);

out_free:
	ret = hp_wmi_error_and_message(ret);

	kfree(obj);
	kfree(args);
	return ret;
}

static void *utf16_empty_string(u16 *p)
{
	*p++ = 2;
	*p++ = 0x00;
	return p;
}

/**
 * hp_ascii_to_utf16_unicode -  Convert ascii string to UTF-16 unicode
 *
 * BIOS supports UTF-16 characters that are 2 bytes long.  No variable
 * multi-byte language supported.
 *
 * @p:   Unicode buffer address
 * @str: string to convert to unicode
 *
 * Returns a void pointer to the buffer string
 */
void *hp_ascii_to_utf16_unicode(u16 *p, const u8 *str)
{
	int len = strlen(str);
	int ret;

	/*
	 * Add null character when reading an empty string
	 * "02 00 00 00"
	 */
	if (len == 0)
		return utf16_empty_string(p);

	/* Move pointer len * 2 number of bytes */
	*p++ = len * 2;
	ret = utf8s_to_utf16s(str, strlen(str), UTF16_HOST_ENDIAN, p, len);
	if (ret < 0) {
		dev_err(bioscfg_drv.class_dev, "UTF16 conversion failed\n");
		return NULL;
	}

	if (ret * sizeof(u16) > U16_MAX) {
		dev_err(bioscfg_drv.class_dev, "Error string too long\n");
		return NULL;
	}

	p += len;
	return p;
}

/**
 * hp_wmi_set_bios_setting - Set setting's value in BIOS
 *
 * @input_buffer: Input buffer address
 * @input_size:   Input buffer size
 *
 * Returns: Count of unicode characters written to BIOS if successful, otherwise
 *		-ENOMEM unable to allocate memory
 *		-EINVAL buffer not allocated or too small
 */
int hp_wmi_set_bios_setting(u16 *input_buffer, u32 input_size)
{
	union acpi_object *obj;
	struct acpi_buffer input = {input_size, input_buffer};
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	int ret;

	ret = wmi_evaluate_method(HP_WMI_SET_BIOS_SETTING_GUID, 0, 1, &input, &output);

	obj = output.pointer;
	if (!obj)
		return -EINVAL;

	if (obj->type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	ret = obj->integer.value;
	if (ret) {
		ret = hp_wmi_error_and_message(ret);
		goto out_free;
	}

out_free:
	kfree(obj);
	return ret;
}

static int hp_attr_set_interface_probe(struct wmi_device *wdev, const void *context)
{
	mutex_lock(&bioscfg_drv.mutex);
	mutex_unlock(&bioscfg_drv.mutex);
	return 0;
}

static void hp_attr_set_interface_remove(struct wmi_device *wdev)
{
	mutex_lock(&bioscfg_drv.mutex);
	mutex_unlock(&bioscfg_drv.mutex);
}

static const struct wmi_device_id hp_attr_set_interface_id_table[] = {
	{ .guid_string = HP_WMI_BIOS_GUID},
	{ }
};

static struct wmi_driver hp_attr_set_interface_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.probe = hp_attr_set_interface_probe,
	.remove = hp_attr_set_interface_remove,
	.id_table = hp_attr_set_interface_id_table,
};

int hp_init_attr_set_interface(void)
{
	return wmi_driver_register(&hp_attr_set_interface_driver);
}

void hp_exit_attr_set_interface(void)
{
	wmi_driver_unregister(&hp_attr_set_interface_driver);
}

MODULE_DEVICE_TABLE(wmi, hp_attr_set_interface_id_table);
