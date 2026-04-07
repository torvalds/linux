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

/**
 * struct f_rndis_opts - RNDIS function options
 * @func_inst: USB function instance.
 * @vendor_id: Vendor ID.
 * @manufacturer: Manufacturer string.
 * @net: The net_device associated with the RNDIS function.
 * @bind_count: Tracks the number of configurations the RNDIS function is
 *              bound to, preventing double-registration of the @net device.
 * @borrowed_net: True if the net_device is shared and pre-registered during
 *                the legacy composite driver's bind phase (e.g., multi.c).
 *                If false, the RNDIS function will register the net_device
 *                during its own bind phase.
 * @rndis_interf_group: ConfigFS group for RNDIS interface.
 * @rndis_os_desc: USB OS descriptor for RNDIS.
 * @rndis_ext_compat_id: Extended compatibility ID.
 * @class: USB class.
 * @subclass: USB subclass.
 * @protocol: USB protocol.
 * @lock: Protects the data from concurrent access by configfs read/write
 *        and create symlink/remove symlink operations.
 * @refcnt: Reference counter for the function instance.
 */
struct f_rndis_opts {
	struct usb_function_instance	func_inst;
	u32				vendor_id;
	const char			*manufacturer;
	struct net_device		*net;
	int				bind_count;
	bool				borrowed_net;

	struct config_group		*rndis_interf_group;
	struct usb_os_desc		rndis_os_desc;
	char				rndis_ext_compat_id[16];

	u8				class;
	u8				subclass;
	u8				protocol;
	struct mutex			lock;
	int				refcnt;
};

void rndis_borrow_net(struct usb_function_instance *f, struct net_device *net);

#endif /* U_RNDIS_H */
