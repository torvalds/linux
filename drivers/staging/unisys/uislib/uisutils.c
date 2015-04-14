/* uisutils.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include "uniklog.h"
#include "uisutils.h"
#include "version.h"
#include "vbushelper.h"
#include <linux/skbuff.h>
#ifdef CONFIG_HIGHMEM
#include <linux/highmem.h>
#endif

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages
 */
#define CURRENT_FILE_PC UISLIB_PC_uisutils_c
#define __MYFILE__ "uisutils.c"

/* exports */
atomic_t uisutils_registered_services = ATOMIC_INIT(0);
					/* num registrations via
					 * uisctrl_register_req_handler() or
					 * uisctrl_register_req_handler_ex() */

/*****************************************************/
/* Utility functions                                 */
/*****************************************************/

int
uisutil_add_proc_line_ex(int *total, char **buffer, int *buffer_remaining,
			 char *format, ...)
{
	va_list args;
	int len;

	DBGINF("buffer = 0x%p : *buffer = 0x%p.\n", buffer, *buffer);
	va_start(args, format);
	len = vsnprintf(*buffer, *buffer_remaining, format, args);
	va_end(args);
	if (len >= *buffer_remaining) {
		*buffer += *buffer_remaining;
		*total += *buffer_remaining;
		*buffer_remaining = 0;
		LOGERR("bytes remaining is too small!\n");
		return -1;
	}
	*buffer_remaining -= len;
	*buffer += len;
	*total += len;
	return len;
}
EXPORT_SYMBOL_GPL(uisutil_add_proc_line_ex);

int
uisctrl_register_req_handler(int type, void *fptr,
			     struct ultra_vbus_deviceinfo *chipset_driver_info)
{
	LOGINF("type = %d, fptr = 0x%p.\n", type, fptr);

	switch (type) {
	case 2:
		if (fptr) {
			if (!virt_control_chan_func)
				atomic_inc(&uisutils_registered_services);
			virt_control_chan_func = fptr;
		} else {
			if (virt_control_chan_func)
				atomic_dec(&uisutils_registered_services);
			virt_control_chan_func = NULL;
		}
		break;

	default:
		LOGERR("invalid type %d.\n", type);
		return 0;
	}
	if (chipset_driver_info)
		bus_device_info_init(chipset_driver_info, "chipset", "uislib",
				     VERSION, NULL);

	return 1;
}
EXPORT_SYMBOL_GPL(uisctrl_register_req_handler);

int
uisctrl_register_req_handler_ex(uuid_le switch_uuid,
			const char *switch_type_name,
			int (*controlfunc)(struct io_msgs *),
			unsigned long min_channel_bytes,
			int (*server_channel_ok)(unsigned long channel_bytes),
			int (*server_channel_init)(void *x,
						unsigned char *client_str,
						u32 client_str_len, u64 bytes),
			struct ultra_vbus_deviceinfo *chipset_driver_info)
{
	struct req_handler_info *req_handler;

	LOGINF("type=%pUL, controlfunc=0x%p.\n",
	       &switch_uuid, controlfunc);
	if (!controlfunc) {
		LOGERR("%pUL: controlfunc must be supplied\n", &switch_uuid);
		return 0;
	}
	if (!server_channel_ok) {
		LOGERR("%pUL: Server_Channel_Ok must be supplied\n",
				&switch_uuid);
		return 0;
	}
	if (!server_channel_init) {
		LOGERR("%pUL: Server_Channel_Init must be supplied\n",
				&switch_uuid);
		return 0;
	}
	req_handler = req_handler_add(switch_uuid,
				      switch_type_name,
				      controlfunc,
				      min_channel_bytes,
				      server_channel_ok, server_channel_init);
	if (!req_handler) {
		LOGERR("failed to add %pUL to server list\n", &switch_uuid);
		return 0;
	}

	atomic_inc(&uisutils_registered_services);
	if (chipset_driver_info) {
		bus_device_info_init(chipset_driver_info, "chipset",
				     "uislib", VERSION, NULL);
		return 1;
	}

	LOGERR("failed to register type %pUL.\n", &switch_uuid);
	return 0;
}
EXPORT_SYMBOL_GPL(uisctrl_register_req_handler_ex);

int
uisctrl_unregister_req_handler_ex(uuid_le switch_uuid)
{
	LOGINF("type=%pUL.\n", &switch_uuid);
	if (req_handler_del(switch_uuid) < 0) {
		LOGERR("failed to remove %pUL from server list\n",
		       &switch_uuid);
		return 0;
	}
	atomic_dec(&uisutils_registered_services);
	return 1;
}
EXPORT_SYMBOL_GPL(uisctrl_unregister_req_handler_ex);

/*
 * unsigned int uisutil_copy_fragsinfo_from_skb(unsigned char *calling_ctx,
 *					     void *skb_in,
 *					     unsigned int firstfraglen,
 *					     unsigned int frags_max,
 *					     struct phys_info frags[])
 *
 *	calling_ctx - input -   a string that is displayed to show
 *				who called * this func
 *	void *skb_in -  skb whose frag info we're copying type is hidden so we
 *			don't need to include skbbuff in uisutils.h which is
 *			included in non-networking code.
 *	unsigned int firstfraglen - input - length of first fragment in skb
 *	unsigned int frags_max - input - max len of frags array
 *	struct phys_info frags[] - output - frags array filled in on output
 *					    return value indicates number of
 *					    entries filled in frags
 */
unsigned int
uisutil_copy_fragsinfo_from_skb(unsigned char *calling_ctx, void *skb_in,
				unsigned int firstfraglen,
				unsigned int frags_max,
				struct phys_info frags[])
{
	unsigned int count = 0, ii, size, offset = 0, numfrags;
	struct sk_buff *skb = skb_in;

	numfrags = skb_shinfo(skb)->nr_frags;

	while (firstfraglen) {
		if (count == frags_max) {
			LOGERR("%s frags array too small: max:%d count:%d\n",
			       calling_ctx, frags_max, count);
			return -1;	/* failure */
		}
		frags[count].pi_pfn =
		    page_to_pfn(virt_to_page(skb->data + offset));
		frags[count].pi_off =
		    (unsigned long)(skb->data + offset) & PI_PAGE_MASK;
		size =
		    min(firstfraglen,
			(unsigned int)(PI_PAGE_SIZE - frags[count].pi_off));
		/* can take smallest of firstfraglen(what's left) OR
		* bytes left in the page
		*/
		frags[count].pi_len = size;
		firstfraglen -= size;
		offset += size;
		count++;
	}
	if (!numfrags)
		goto dolist;

	if ((count + numfrags) > frags_max) {
		LOGERR("**** FAILED %s frags array too small: max:%d count+nr_frags:%d\n",
		       calling_ctx, frags_max, count + numfrags);
		return -1;	/* failure */
	}

	for (ii = 0; ii < numfrags; ii++) {
		count = add_physinfo_entries(page_to_pfn(
				skb_frag_page(&skb_shinfo(skb)->frags[ii])),
					skb_shinfo(skb)->frags[ii].
					page_offset,
					skb_shinfo(skb)->frags[ii].
					size, count, frags_max,
					frags);
		if (count == 0) {
			LOGERR("**** FAILED to add physinfo entries\n");
			return -1;	/* failure */
		}
	}

dolist: if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *skbinlist;
		int c;

		for (skbinlist = skb_shinfo(skb)->frag_list; skbinlist;
		     skbinlist = skbinlist->next) {
			c = uisutil_copy_fragsinfo_from_skb("recursive",
				skbinlist,
				skbinlist->len - skbinlist->data_len,
				frags_max - count,
				&frags[count]);
			if (c == -1) {
				LOGERR("**** FAILED recursive call failed\n");
				return -1;
			}
			count += c;
		}
	}
	return count;
}
EXPORT_SYMBOL_GPL(uisutil_copy_fragsinfo_from_skb);

static LIST_HEAD(req_handler_info_list); /* list of struct req_handler_info */
static DEFINE_SPINLOCK(req_handler_info_list_lock);

struct req_handler_info *
req_handler_add(uuid_le switch_uuid,
	      const char *switch_type_name,
	      int (*controlfunc)(struct io_msgs *),
	      unsigned long min_channel_bytes,
	      int (*server_channel_ok)(unsigned long channel_bytes),
	      int (*server_channel_init)
	       (void *x, unsigned char *clientstr, u32 clientstr_len,
		u64 bytes))
{
	struct req_handler_info *rc = NULL;

	rc = kzalloc(sizeof(*rc), GFP_ATOMIC);
	if (!rc)
		return NULL;
	rc->switch_uuid = switch_uuid;
	rc->controlfunc = controlfunc;
	rc->min_channel_bytes = min_channel_bytes;
	rc->server_channel_ok = server_channel_ok;
	rc->server_channel_init = server_channel_init;
	if (switch_type_name)
		strncpy(rc->switch_type_name, switch_type_name,
			sizeof(rc->switch_type_name) - 1);
	spin_lock(&req_handler_info_list_lock);
	list_add_tail(&rc->list_link, &req_handler_info_list);
	spin_unlock(&req_handler_info_list_lock);

	return rc;
}

struct req_handler_info *
req_handler_find(uuid_le switch_uuid)
{
	struct list_head *lelt, *tmp;
	struct req_handler_info *entry = NULL;

	spin_lock(&req_handler_info_list_lock);
	list_for_each_safe(lelt, tmp, &req_handler_info_list) {
		entry = list_entry(lelt, struct req_handler_info, list_link);
		if (uuid_le_cmp(entry->switch_uuid, switch_uuid) == 0) {
			spin_unlock(&req_handler_info_list_lock);
			return entry;
		}
	}
	spin_unlock(&req_handler_info_list_lock);
	return NULL;
}

int
req_handler_del(uuid_le switch_uuid)
{
	struct list_head *lelt, *tmp;
	struct req_handler_info *entry = NULL;
	int rc = -1;

	spin_lock(&req_handler_info_list_lock);
	list_for_each_safe(lelt, tmp, &req_handler_info_list) {
		entry = list_entry(lelt, struct req_handler_info, list_link);
		if (uuid_le_cmp(entry->switch_uuid, switch_uuid) == 0) {
			list_del(lelt);
			kfree(entry);
			rc++;
		}
	}
	spin_unlock(&req_handler_info_list_lock);
	return rc;
}
