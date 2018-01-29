// SPDX-License-Identifier: GPL-2.0
/*
 * u_printer.h
 *
 * Utility definitions for the printer function
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */

#ifndef U_PRINTER_H
#define U_PRINTER_H

#include <linux/usb/composite.h>

struct f_printer_opts {
	struct usb_function_instance	func_inst;
	int				minor;
	char				*pnp_string;
	bool				pnp_string_allocated;
	unsigned			q_len;

	/*
	 * Protect the data from concurrent access by read/write
	 * and create symlink/remove symlink
	 */
	struct mutex			lock;
	int				refcnt;
};

#endif /* U_PRINTER_H */
