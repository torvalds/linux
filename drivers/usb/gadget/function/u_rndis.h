/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_rndis.h
 *
 * Utility definitions for the subset function
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_RNDIS_H
#define U_RNDIS_H

#include <linux/usb/composite.h>

struct f_rndis_opts {
	struct usb_function_instance	func_inst;
	u32				vendor_id;
	const char			*manufacturer;
	struct net_device		*net;
	bool				bound;
	bool				borrowed_net;

	struct config_group		*rndis_interf_group;
	struct usb_os_desc		rndis_os_desc;
	char				rndis_ext_compat_id[16];

	u8				class;
	u8				subclass;
	u8				protocol;

	/*
	 * Read/write access to configfs attributes is handled by configfs.
	 *
	 * This is to protect the data from concurrent access by read/write
	 * and create symlink/remove symlink.
	 */
	struct mutex			lock;
	int				refcnt;

	/* "Wireless" RNDIS; auto-detected by Windows */
	bool	wceis;
};

void rndis_borrow_net(struct usb_function_instance *f, struct net_device *net);

#endif /* U_RNDIS_H */
