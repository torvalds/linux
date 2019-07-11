// SPDX-License-Identifier: GPL-2.0
/*
 * u_ncm.h
 *
 * Utility definitions for the ncm function
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_NCM_H
#define U_NCM_H

#include <linux/usb/composite.h>

struct f_ncm_opts {
	struct usb_function_instance	func_inst;
	struct net_device		*net;
	bool				bound;

	/*
	 * Read/write access to configfs attributes is handled by configfs.
	 *
	 * This is to protect the data from concurrent access by read/write
	 * and create symlink/remove symlink.
	 */
	struct mutex			lock;
	int				refcnt;
};

#endif /* U_NCM_H */
