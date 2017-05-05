/* EFI signature/key/certificate list parser
 *
 * Copyright (C) 2012, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "EFI: "fmt
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/efi.h>

/**
 * parse_efi_signature_list - Parse an EFI signature list for certificates
 * @source: The source of the key
 * @data: The data blob to parse
 * @size: The size of the data blob
 * @get_handler_for_guid: Get the handler func for the sig type (or NULL)
 *
 * Parse an EFI signature list looking for elements of interest.  A list is
 * made up of a series of sublists, where all the elements in a sublist are of
 * the same type, but sublists can be of different types.
 *
 * For each sublist encountered, the @get_handler_for_guid function is called
 * with the type specifier GUID and returns either a pointer to a function to
 * handle elements of that type or NULL if the type is not of interest.
 *
 * If the sublist is of interest, each element is passed to the handler
 * function in turn.
 *
 * Error EBADMSG is returned if the list doesn't parse correctly and 0 is
 * returned if the list was parsed correctly.  No error can be returned from
 * the @get_handler_for_guid function or the element handler function it
 * returns.
 */
int __init parse_efi_signature_list(
	const char *source,
	const void *data, size_t size,
	efi_element_handler_t (*get_handler_for_guid)(const efi_guid_t *))
{
	efi_element_handler_t handler;
	unsigned offs = 0;

	pr_devel("-->%s(,%zu)\n", __func__, size);

	while (size > 0) {
		const efi_signature_data_t *elem;
		efi_signature_list_t list;
		size_t lsize, esize, hsize, elsize;

		if (size < sizeof(list))
			return -EBADMSG;

		memcpy(&list, data, sizeof(list));
		pr_devel("LIST[%04x] guid=%pUl ls=%x hs=%x ss=%x\n",
			 offs,
			 list.signature_type.b, list.signature_list_size,
			 list.signature_header_size, list.signature_size);

		lsize = list.signature_list_size;
		hsize = list.signature_header_size;
		esize = list.signature_size;
		elsize = lsize - sizeof(list) - hsize;

		if (lsize > size) {
			pr_devel("<--%s() = -EBADMSG [overrun @%x]\n",
				 __func__, offs);
			return -EBADMSG;
		}

		if (lsize < sizeof(list) ||
		    lsize - sizeof(list) < hsize ||
		    esize < sizeof(*elem) ||
		    elsize < esize ||
		    elsize % esize != 0) {
			pr_devel("- bad size combo @%x\n", offs);
			return -EBADMSG;
		}

		handler = get_handler_for_guid(&list.signature_type);
		if (!handler) {
			data += lsize;
			size -= lsize;
			offs += lsize;
			continue;
		}

		data += sizeof(list) + hsize;
		size -= sizeof(list) + hsize;
		offs += sizeof(list) + hsize;

		for (; elsize > 0; elsize -= esize) {
			elem = data;

			pr_devel("ELEM[%04x]\n", offs);
			handler(source,
				&elem->signature_data,
				esize - sizeof(*elem));

			data += esize;
			size -= esize;
			offs += esize;
		}
	}

	return 0;
}
