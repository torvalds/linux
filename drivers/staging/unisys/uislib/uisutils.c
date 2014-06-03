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
#include <commontypes.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include "uniklog.h"
#include "uisutils.h"
#include "version.h"
#include "vbushelper.h"
#include <linux/uuid.h>
#include <linux/skbuff.h>
#include <linux/uuid.h>
#ifdef CONFIG_HIGHMEM
#include <linux/highmem.h>
#endif

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages
 */
#define CURRENT_FILE_PC UISLIB_PC_uisutils_c
#define __MYFILE__ "uisutils.c"

/* exports */
atomic_t UisUtils_Registered_Services = ATOMIC_INIT(0);
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
			     ULTRA_VBUS_DEVICEINFO *chipset_DriverInfo)
{
	LOGINF("type = %d, fptr = 0x%p.\n", type, fptr);

	switch (type) {
	case 2:
		if (fptr) {
			if (!VirtControlChanFunc)
				atomic_inc(&UisUtils_Registered_Services);
			VirtControlChanFunc = fptr;
		} else {
			if (VirtControlChanFunc)
				atomic_dec(&UisUtils_Registered_Services);
			VirtControlChanFunc = NULL;
		}
		break;

	default:
		LOGERR("invalid type %d.\n", type);
		return 0;
	}
	if (chipset_DriverInfo)
		BusDeviceInfo_Init(chipset_DriverInfo,
				   "chipset", "uislib",
				   VERSION, NULL, __DATE__, __TIME__);

	return 1;
}
EXPORT_SYMBOL_GPL(uisctrl_register_req_handler);

int
uisctrl_register_req_handler_ex(uuid_le switchTypeGuid,
				const char *switch_type_name,
				int (*controlfunc)(struct io_msgs *),
				unsigned long min_channel_bytes,
				int (*Server_Channel_Ok)(unsigned long
							  channelBytes),
				int (*Server_Channel_Init)
				 (void *x, unsigned char *clientStr,
				  U32 clientStrLen, U64 bytes),
				ULTRA_VBUS_DEVICEINFO *chipset_DriverInfo)
{
	ReqHandlerInfo_t *pReqHandlerInfo;
	int rc = 0;		/* assume failure */
	LOGINF("type=%pUL, controlfunc=0x%p.\n",
	       &switchTypeGuid, controlfunc);
	if (!controlfunc) {
		LOGERR("%pUL: controlfunc must be supplied\n", &switchTypeGuid);
		goto Away;
	}
	if (!Server_Channel_Ok) {
		LOGERR("%pUL: Server_Channel_Ok must be supplied\n",
				&switchTypeGuid);
		goto Away;
	}
	if (!Server_Channel_Init) {
		LOGERR("%pUL: Server_Channel_Init must be supplied\n",
				&switchTypeGuid);
		goto Away;
	}
	pReqHandlerInfo = ReqHandlerAdd(switchTypeGuid,
					switch_type_name,
					controlfunc,
					min_channel_bytes,
					Server_Channel_Ok, Server_Channel_Init);
	if (!pReqHandlerInfo) {
		LOGERR("failed to add %pUL to server list\n", &switchTypeGuid);
		goto Away;
	}

	atomic_inc(&UisUtils_Registered_Services);
	rc = 1;			/* success */
Away:
	if (rc) {
		if (chipset_DriverInfo)
			BusDeviceInfo_Init(chipset_DriverInfo,
					   "chipset", "uislib",
					   VERSION, NULL,
					   __DATE__, __TIME__);
	} else
		LOGERR("failed to register type %pUL.\n", &switchTypeGuid);

	return rc;
}
EXPORT_SYMBOL_GPL(uisctrl_register_req_handler_ex);

int
uisctrl_unregister_req_handler_ex(uuid_le switchTypeGuid)
{
	int rc = 0;		/* assume failure */
	LOGINF("type=%pUL.\n", &switchTypeGuid);
	if (ReqHandlerDel(switchTypeGuid) < 0) {
		LOGERR("failed to remove %pUL from server list\n",
				&switchTypeGuid);
		goto Away;
	}
	atomic_dec(&UisUtils_Registered_Services);
	rc = 1;			/* success */
Away:
	if (!rc)
		LOGERR("failed to unregister type %pUL.\n", &switchTypeGuid);
	return rc;
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
		    (unsigned long) (skb->data + offset) & PI_PAGE_MASK;
		size =
		    min(firstfraglen,
			(unsigned int) (PI_PAGE_SIZE - frags[count].pi_off));
		/* can take smallest of firstfraglen(what's left) OR
		* bytes left in the page
		*/
		frags[count].pi_len = size;
		firstfraglen -= size;
		offset += size;
		count++;
	}
	if (numfrags) {
		if ((count + numfrags) > frags_max) {
			LOGERR("**** FAILED %s frags array too small: max:%d count+nr_frags:%d\n",
			     calling_ctx, frags_max, count + numfrags);
			return -1;	/* failure */
		}

		for (ii = 0; ii < numfrags; ii++) {
			count = add_physinfo_entries(page_to_pfn(skb_frag_page(&skb_shinfo(skb)->frags[ii])),	/* pfn */
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
	}
	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *skbinlist;
		int c;
		for (skbinlist = skb_shinfo(skb)->frag_list; skbinlist;
		     skbinlist = skbinlist->next) {

			c = uisutil_copy_fragsinfo_from_skb("recursive",
							    skbinlist,
							    skbinlist->len -
							    skbinlist->data_len,
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

static LIST_HEAD(ReqHandlerInfo_list);	/* list of ReqHandlerInfo_t */
static DEFINE_SPINLOCK(ReqHandlerInfo_list_lock);

ReqHandlerInfo_t *
ReqHandlerAdd(uuid_le switchTypeGuid,
	      const char *switch_type_name,
	      int (*controlfunc)(struct io_msgs *),
	      unsigned long min_channel_bytes,
	      int (*Server_Channel_Ok)(unsigned long channelBytes),
	      int (*Server_Channel_Init)
	       (void *x, unsigned char *clientStr, U32 clientStrLen, U64 bytes))
{
	ReqHandlerInfo_t *rc = NULL;

	rc = kzalloc(sizeof(*rc), GFP_ATOMIC);
	if (!rc)
		return NULL;
	rc->switchTypeGuid = switchTypeGuid;
	rc->controlfunc = controlfunc;
	rc->min_channel_bytes = min_channel_bytes;
	rc->Server_Channel_Ok = Server_Channel_Ok;
	rc->Server_Channel_Init = Server_Channel_Init;
	if (switch_type_name)
		strncpy(rc->switch_type_name, switch_type_name,
			sizeof(rc->switch_type_name) - 1);
	spin_lock(&ReqHandlerInfo_list_lock);
	list_add_tail(&(rc->list_link), &ReqHandlerInfo_list);
	spin_unlock(&ReqHandlerInfo_list_lock);

	return rc;
}

ReqHandlerInfo_t *
ReqHandlerFind(uuid_le switchTypeGuid)
{
	struct list_head *lelt, *tmp;
	ReqHandlerInfo_t *entry = NULL;
	spin_lock(&ReqHandlerInfo_list_lock);
	list_for_each_safe(lelt, tmp, &ReqHandlerInfo_list) {
		entry = list_entry(lelt, ReqHandlerInfo_t, list_link);
		if (uuid_le_cmp(entry->switchTypeGuid, switchTypeGuid) == 0) {
			spin_unlock(&ReqHandlerInfo_list_lock);
			return entry;
		}
	}
	spin_unlock(&ReqHandlerInfo_list_lock);
	return NULL;
}

int
ReqHandlerDel(uuid_le switchTypeGuid)
{
	struct list_head *lelt, *tmp;
	ReqHandlerInfo_t *entry = NULL;
	int rc = -1;
	spin_lock(&ReqHandlerInfo_list_lock);
	list_for_each_safe(lelt, tmp, &ReqHandlerInfo_list) {
		entry = list_entry(lelt, ReqHandlerInfo_t, list_link);
		if (uuid_le_cmp(entry->switchTypeGuid, switchTypeGuid) == 0) {
			list_del(lelt);
			kfree(entry);
			rc++;
		}
	}
	spin_unlock(&ReqHandlerInfo_list_lock);
	return rc;
}
