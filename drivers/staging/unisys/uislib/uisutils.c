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

	va_start(args, format);
	len = vsnprintf(*buffer, *buffer_remaining, format, args);
	va_end(args);
	if (len >= *buffer_remaining) {
		*buffer += *buffer_remaining;
		*total += *buffer_remaining;
		*buffer_remaining = 0;
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
		return 0;
	}
	if (chipset_driver_info)
		bus_device_info_init(chipset_driver_info, "chipset", "uislib",
				     VERSION, NULL);

	return 1;
}
EXPORT_SYMBOL_GPL(uisctrl_register_req_handler);

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

static LIST_HEAD(req_handler_info_list); /* list of struct req_handler_info */
static DEFINE_SPINLOCK(req_handler_info_list_lock);

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
