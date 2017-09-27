/*
 * u_os_desc.h
 *
 * Utility definitions for "OS Descriptors" support
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __U_OS_DESC_H__
#define __U_OS_DESC_H__

#include <asm/unaligned.h>
#include <linux/nls.h>

#define USB_EXT_PROP_DW_SIZE			0
#define USB_EXT_PROP_DW_PROPERTY_DATA_TYPE	4
#define USB_EXT_PROP_W_PROPERTY_NAME_LENGTH	8
#define USB_EXT_PROP_B_PROPERTY_NAME		10
#define USB_EXT_PROP_DW_PROPERTY_DATA_LENGTH	10
#define USB_EXT_PROP_B_PROPERTY_DATA		14

#define USB_EXT_PROP_RESERVED			0
#define USB_EXT_PROP_UNICODE			1
#define USB_EXT_PROP_UNICODE_ENV		2
#define USB_EXT_PROP_BINARY			3
#define USB_EXT_PROP_LE32			4
#define USB_EXT_PROP_BE32			5
#define USB_EXT_PROP_UNICODE_LINK		6
#define USB_EXT_PROP_UNICODE_MULTI		7

static inline u8 *__usb_ext_prop_ptr(u8 *buf, size_t offset)
{
	return buf + offset;
}

static inline u8 *usb_ext_prop_size_ptr(u8 *buf)
{
	return __usb_ext_prop_ptr(buf, USB_EXT_PROP_DW_SIZE);
}

static inline u8 *usb_ext_prop_type_ptr(u8 *buf)
{
	return __usb_ext_prop_ptr(buf, USB_EXT_PROP_DW_PROPERTY_DATA_TYPE);
}

static inline u8 *usb_ext_prop_name_len_ptr(u8 *buf)
{
	return __usb_ext_prop_ptr(buf, USB_EXT_PROP_W_PROPERTY_NAME_LENGTH);
}

static inline u8 *usb_ext_prop_name_ptr(u8 *buf)
{
	return __usb_ext_prop_ptr(buf, USB_EXT_PROP_B_PROPERTY_NAME);
}

static inline u8 *usb_ext_prop_data_len_ptr(u8 *buf, size_t off)
{
	return __usb_ext_prop_ptr(buf,
				  USB_EXT_PROP_DW_PROPERTY_DATA_LENGTH + off);
}

static inline u8 *usb_ext_prop_data_ptr(u8 *buf, size_t off)
{
	return __usb_ext_prop_ptr(buf, USB_EXT_PROP_B_PROPERTY_DATA + off);
}

static inline void usb_ext_prop_put_size(u8 *buf, int dw_size)
{
	put_unaligned_le32(dw_size, usb_ext_prop_size_ptr(buf));
}

static inline void usb_ext_prop_put_type(u8 *buf, int type)
{
	put_unaligned_le32(type, usb_ext_prop_type_ptr(buf));
}

static inline int usb_ext_prop_put_name(u8 *buf, const char *name, int pnl)
{
	int result;

	put_unaligned_le16(pnl, usb_ext_prop_name_len_ptr(buf));
	result = utf8s_to_utf16s(name, strlen(name), UTF16_LITTLE_ENDIAN,
		(wchar_t *) usb_ext_prop_name_ptr(buf), pnl - 2);
	if (result < 0)
		return result;

	put_unaligned_le16(0, &buf[USB_EXT_PROP_B_PROPERTY_NAME + pnl - 2]);

	return pnl;
}

static inline void usb_ext_prop_put_binary(u8 *buf, int pnl, const u8 *data,
					   int data_len)
{
	put_unaligned_le32(data_len, usb_ext_prop_data_len_ptr(buf, pnl));
	memcpy(usb_ext_prop_data_ptr(buf, pnl), data, data_len);
}

static inline int usb_ext_prop_put_unicode(u8 *buf, int pnl, const char *string,
					   int data_len)
{
	int result;
	put_unaligned_le32(data_len, usb_ext_prop_data_len_ptr(buf, pnl));
	result = utf8s_to_utf16s(string, data_len >> 1, UTF16_LITTLE_ENDIAN,
			(wchar_t *) usb_ext_prop_data_ptr(buf, pnl),
			data_len - 2);
	if (result < 0)
		return result;

	put_unaligned_le16(0,
		&buf[USB_EXT_PROP_B_PROPERTY_DATA + pnl + data_len - 2]);

	return data_len;
}

#endif /* __U_OS_DESC_H__ */
