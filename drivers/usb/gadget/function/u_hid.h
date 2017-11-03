// SPDX-License-Identifier: GPL-2.0
/*
 * u_hid.h
 *
 * Utility definitions for the hid function
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

#ifndef U_HID_H
#define U_HID_H

#include <linux/usb/composite.h>

struct f_hid_opts {
	struct usb_function_instance	func_inst;
	int				minor;
	unsigned char			subclass;
	unsigned char			protocol;
	unsigned short			report_length;
	unsigned short			report_desc_length;
	unsigned char			*report_desc;
	bool				report_desc_alloc;

	/*
	 * Protect the data form concurrent access by read/write
	 * and create symlink/remove symlink.
	 */
	 struct mutex			lock;
	 int				refcnt;
};

int ghid_setup(struct usb_gadget *g, int count);
void ghid_cleanup(void);

#endif /* U_HID_H */
