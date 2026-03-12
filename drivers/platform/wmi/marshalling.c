// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ACPI-WMI buffer marshalling.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/acpi.h>
#include <linux/align.h>
#include <linux/math.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include <kunit/visibility.h>

#include "internal.h"

static int wmi_adjust_buffer_length(size_t *length, const union acpi_object *obj)
{
	size_t alignment, size;

	switch (obj->type) {
	case ACPI_TYPE_INTEGER:
		/*
		 * Integers are threated as 32 bit even if the ACPI DSDT
		 * declares 64 bit integer width.
		 */
		alignment = 4;
		size = sizeof(u32);
		break;
	case ACPI_TYPE_STRING:
		/*
		 * Strings begin with a single little-endian 16-bit field containing
		 * the string length in bytes and are encoded as UTF-16LE with a terminating
		 * nul character.
		 */
		if (obj->string.length + 1 > U16_MAX / 2)
			return -EOVERFLOW;

		alignment = 2;
		size = struct_size_t(struct wmi_string, chars, obj->string.length + 1);
		break;
	case ACPI_TYPE_BUFFER:
		/*
		 * Buffers are copied as-is.
		 */
		alignment = 1;
		size = obj->buffer.length;
		break;
	default:
		return -EPROTO;
	}

	*length = size_add(ALIGN(*length, alignment), size);

	return 0;
}

static int wmi_obj_get_buffer_length(const union acpi_object *obj, size_t *length)
{
	size_t total = 0;
	int ret;

	if (obj->type == ACPI_TYPE_PACKAGE) {
		for (int i = 0; i < obj->package.count; i++) {
			ret = wmi_adjust_buffer_length(&total, &obj->package.elements[i]);
			if (ret < 0)
				return ret;
		}
	} else {
		ret = wmi_adjust_buffer_length(&total, obj);
		if (ret < 0)
			return ret;
	}

	*length = total;

	return 0;
}

static int wmi_obj_transform_simple(const union acpi_object *obj, u8 *buffer, size_t *consumed)
{
	struct wmi_string *string;
	size_t length;
	__le32 value;
	u8 *aligned;

	switch (obj->type) {
	case ACPI_TYPE_INTEGER:
		aligned = PTR_ALIGN(buffer, 4);
		length = sizeof(value);

		value = cpu_to_le32(obj->integer.value);
		memcpy(aligned, &value, length);
		break;
	case ACPI_TYPE_STRING:
		aligned = PTR_ALIGN(buffer, 2);
		string = (struct wmi_string *)aligned;
		length = struct_size(string, chars, obj->string.length + 1);

		/* We do not have to worry about unaligned accesses here as the WMI
		 * string will already be aligned on a two-byte boundary.
		 */
		string->length = cpu_to_le16((obj->string.length + 1) * 2);
		for (int i = 0; i < obj->string.length; i++)
			string->chars[i] = cpu_to_le16(obj->string.pointer[i]);

		/*
		 * The Windows WMI-ACPI driver always emits a terminating nul character,
		 * so we emulate this behavior here as well.
		 */
		string->chars[obj->string.length] = '\0';
		break;
	case ACPI_TYPE_BUFFER:
		aligned = buffer;
		length = obj->buffer.length;

		memcpy(aligned, obj->buffer.pointer, length);
		break;
	default:
		return -EPROTO;
	}

	*consumed = (aligned - buffer) + length;

	return 0;
}

static int wmi_obj_transform(const union acpi_object *obj, u8 *buffer)
{
	size_t consumed;
	int ret;

	if (obj->type == ACPI_TYPE_PACKAGE) {
		for (int i = 0; i < obj->package.count; i++) {
			ret = wmi_obj_transform_simple(&obj->package.elements[i], buffer,
						       &consumed);
			if (ret < 0)
				return ret;

			buffer += consumed;
		}
	} else {
		ret = wmi_obj_transform_simple(obj, buffer, &consumed);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int wmi_unmarshal_acpi_object(const union acpi_object *obj, struct wmi_buffer *buffer)
{
	size_t length, alloc_length;
	u8 *data;
	int ret;

	ret = wmi_obj_get_buffer_length(obj, &length);
	if (ret < 0)
		return ret;

	if (ARCH_KMALLOC_MINALIGN < 8) {
		/*
		 * kmalloc() guarantees that the alignment of the resulting memory allocation is at
		 * least the largest power-of-two divisor of the allocation size. The WMI buffer
		 * data needs to be aligned on a 8 byte boundary to properly support 64-bit WMI
		 * integers, so we have to round the allocation size to the next multiple of 8.
		 */
		alloc_length = round_up(length, 8);
	} else {
		alloc_length = length;
	}

	data = kzalloc(alloc_length, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = wmi_obj_transform(obj, data);
	if (ret < 0) {
		kfree(data);
		return ret;
	}

	buffer->length = length;
	buffer->data = data;

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(wmi_unmarshal_acpi_object);

int wmi_marshal_string(const struct wmi_buffer *buffer, struct acpi_buffer *out)
{
	const struct wmi_string *string;
	u16 length, value;
	size_t chars;
	char *str;

	if (buffer->length < sizeof(*string))
		return -ENODATA;

	string = buffer->data;
	length = get_unaligned_le16(&string->length);
	if (buffer->length < sizeof(*string) + length)
		return -ENODATA;

	/* Each character needs to be 16 bits long */
	if (length % 2)
		return -EINVAL;

	chars = length / 2;
	str = kmalloc(chars + 1, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (int i = 0; i < chars; i++) {
		value = get_unaligned_le16(&string->chars[i]);

		/* ACPI only accepts ASCII strings */
		if (value > 0x7F) {
			kfree(str);
			return -EINVAL;
		}

		str[i] = value & 0xFF;

		/*
		 * ACPI strings should only contain a single nul character at the end.
		 * Because of this we must not copy any padding from the WMI string.
		 */
		if (!value) {
			/* ACPICA wants the length of the string without the nul character */
			out->length = i;
			out->pointer = str;
			return 0;
		}
	}

	str[chars] = '\0';

	out->length = chars;
	out->pointer = str;

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(wmi_marshal_string);
