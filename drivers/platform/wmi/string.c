// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * WMI string utility functions.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/build_bug.h>
#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/nls.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <asm/byteorder.h>

static_assert(sizeof(__le16) == sizeof(wchar_t));

/**
 * wmi_string_to_utf8s - Convert a WMI string into a UTF8 string.
 * @str: WMI string representation
 * @dst: Buffer to fill with UTF8 characters
 * @length: Length of the destination buffer
 *
 * Convert as WMI string into a standard UTF8 string. The conversion will stop
 * once a NUL character is detected or when the buffer is full. Any invalid UTF16
 * characters will be ignored. The resulting UTF8 string will always be NUL-terminated
 * when this function returns successfully.
 *
 * Return: Length of the resulting UTF8 string or negative errno code on failure.
 */
ssize_t wmi_string_to_utf8s(const struct wmi_string *str, u8 *dst, size_t length)
{
	/* Contains the maximum number of UTF16 code points to read */
	int inlen = le16_to_cpu(str->length) / 2;
	int ret;

	if (length < 1)
		return -EINVAL;

	/* We must leave room for the NUL character at the end of the destination buffer */
	ret = utf16s_to_utf8s((__force const wchar_t *)str->chars, inlen, UTF16_LITTLE_ENDIAN, dst,
			      length - 1);
	if (ret < 0)
		return ret;

	dst[ret] = '\0';

	return ret;
}
EXPORT_SYMBOL_GPL(wmi_string_to_utf8s);

/**
 * wmi_string_from_utf8s - Convert a UTF8 string into a WMI string.
 * @str: WMI string representation
 * @max_chars: Maximum number of UTF16 code points to store inside the WMI string
 * @src: UTF8 string to convert
 * @src_length: Length of the source string without any trailing NUL-characters
 *
 * Convert a UTF8 string into a WMI string. The conversion will stop when the WMI string is
 * full. The resulting WMI string will always be NUL-terminated and have its length field set
 * to and appropriate value when this function returns successfully.
 *
 * Return: Number of UTF16 code points inside the WMI string or negative errno code on failure.
 */
ssize_t wmi_string_from_utf8s(struct wmi_string *str, size_t max_chars, const u8 *src,
			      size_t src_length)
{
	size_t str_length;
	int ret;

	if (max_chars < 1)
		return -EINVAL;

	/* We must leave room for the NUL character at the end of the WMI string */
	ret = utf8s_to_utf16s(src, src_length, UTF16_LITTLE_ENDIAN, (__force wchar_t *)str->chars,
			      max_chars - 1);
	if (ret < 0)
		return ret;

	str_length = (ret + 1) * sizeof(u16);
	if (str_length > U16_MAX)
		return -EOVERFLOW;

	str->length = cpu_to_le16(str_length);
	str->chars[ret] = '\0';

	return ret;
}
EXPORT_SYMBOL_GPL(wmi_string_from_utf8s);
