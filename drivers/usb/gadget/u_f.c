// SPDX-License-Identifier: GPL-2.0
/*
 * u_f.c -- USB function utilities for Gadget stack
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#include <linux/usb/ch9.h>
#include <linux/usb/func_utils.h>

struct usb_request *alloc_ep_req(struct usb_ep *ep, size_t len)
{
	struct usb_request      *req;

	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (req) {
		req->length = usb_endpoint_dir_out(ep->desc) ?
			usb_ep_align(ep, len) : len;
		req->buf = kmalloc(req->length, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}
	return req;
}
EXPORT_SYMBOL_GPL(alloc_ep_req);
