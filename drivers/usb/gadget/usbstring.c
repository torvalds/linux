// SPDX-License-Identifier: LGPL-2.1+
/*
 * Copyright (C) 2003 David Brownell
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/nls.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>


/**
 * usb_gadget_get_string - fill out a string descriptor 
 * @table: of c strings encoded using UTF-8
 * @id: string id, from low byte of wValue in get string descriptor
 * @buf: at least 256 bytes, must be 16-bit aligned
 *
 * Finds the UTF-8 string matching the ID, and converts it into a
 * string descriptor in utf16-le.
 * Returns length of descriptor (always even) or negative errno
 *
 * If your driver needs stings in multiple languages, you'll probably
 * "switch (wIndex) { ... }"  in your ep0 string descriptor logic,
 * using this routine after choosing which set of UTF-8 strings to use.
 * Note that US-ASCII is a strict subset of UTF-8; any string bytes with
 * the eighth bit set will be multibyte UTF-8 characters, not ISO-8859/1
 * characters (which are also widely used in C strings).
 */
int
usb_gadget_get_string (const struct usb_gadget_strings *table, int id, u8 *buf)
{
	struct usb_string	*s;
	int			len;

	/* descriptor 0 has the language id */
	if (id == 0) {
		buf [0] = 4;
		buf [1] = USB_DT_STRING;
		buf [2] = (u8) table->language;
		buf [3] = (u8) (table->language >> 8);
		return 4;
	}
	for (s = table->strings; s && s->s; s++)
		if (s->id == id)
			break;

	/* unrecognized: stall. */
	if (!s || !s->s)
		return -EINVAL;

	/* string descriptors have length, tag, then UTF16-LE text */
	len = min((size_t)USB_MAX_STRING_LEN, strlen(s->s));
	len = utf8s_to_utf16s(s->s, len, UTF16_LITTLE_ENDIAN,
			(wchar_t *) &buf[2], USB_MAX_STRING_LEN);
	if (len < 0)
		return -EINVAL;
	buf [0] = (len + 1) * 2;
	buf [1] = USB_DT_STRING;
	return buf [0];
}
EXPORT_SYMBOL_GPL(usb_gadget_get_string);

/**
 * usb_validate_langid - validate usb language identifiers
 * @lang: usb language identifier
 *
 * Returns true for valid language identifier, otherwise false.
 */
bool usb_validate_langid(u16 langid)
{
	u16 primary_lang = langid & 0x3ff;	/* bit [9:0] */
	u16 sub_lang = langid >> 10;		/* bit [15:10] */

	switch (primary_lang) {
	case 0:
	case 0x62 ... 0xfe:
	case 0x100 ... 0x3ff:
		return false;
	}
	if (!sub_lang)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(usb_validate_langid);
