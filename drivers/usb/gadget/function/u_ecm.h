/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_ecm.h
 *
 * Utility definitions for the ecm function
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_ECM_H
#define U_ECM_H

#include <linux/usb/composite.h>

struct f_ecm_opts {
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

#endif /* U_ECM_H */
