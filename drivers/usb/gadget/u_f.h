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

struct usb_ep;
struct usb_request;

struct usb_request *alloc_ep_req(struct usb_ep *ep, int len, int default_len);

#endif /* __U_F_H__ */


