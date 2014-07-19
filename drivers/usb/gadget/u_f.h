/*
 * u_f.h
 *
 * Utility definitions for USB functions
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __U_F_H__
#define __U_F_H__

/* Variable Length Array Macros **********************************************/
#define vla_group(groupname) size_t groupname##__next = 0
#define vla_group_size(groupname) groupname##__next

#define vla_item(groupname, type, name, n) \
	size_t groupname##_##name##__offset = ({			       \
		size_t align_mask = __alignof__(type) - 1;		       \
		size_t offset = (groupname##__next + align_mask) & ~align_mask;\
		size_t size = (n) * sizeof(type);			       \
		groupname##__next = offset + size;			       \
		offset;							       \
	})

#define vla_item_with_sz(groupname, type, name, n) \
	size_t groupname##_##name##__sz = (n) * sizeof(type);		       \
	size_t groupname##_##name##__offset = ({			       \
		size_t align_mask = __alignof__(type) - 1;		       \
		size_t offset = (groupname##__next + align_mask) & ~align_mask;\
		size_t size = groupname##_##name##__sz;			       \
		groupname##__next = offset + size;			       \
		offset;							       \
	})

#define vla_ptr(ptr, groupname, name) \
	((void *) ((char *)ptr + groupname##_##name##__offset))

struct usb_ep;
struct usb_request;

struct usb_request *alloc_ep_req(struct usb_ep *ep, int len, int default_len);

#endif /* __U_F_H__ */


