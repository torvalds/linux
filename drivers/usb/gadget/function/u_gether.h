/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_gether.h
 *
 * Utility definitions for the subset function
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_GETHER_H
#define U_GETHER_H

#include <linux/usb/composite.h>

/**
 * struct f_gether_opts - subset function options
 * @func_inst: USB function instance.
 * @net: The net_device associated with the subset function.
 * @bound: True if the net_device is shared and pre-registered during the
 *         legacy composite driver's bind phase (e.g., multi.c). If false,
 *         the subset function will register the net_device during its own
 *         bind phase.
 * @bind_count: Tracks the number of configurations the subset function is
 *              bound to, preventing double-registration of the @net device.
 * @lock: Protects the data from concurrent access by configfs read/write
 *        and create symlink/remove symlink operations.
 * @refcnt: Reference counter for the function instance.
 */
struct f_gether_opts {
	struct usb_function_instance	func_inst;
	struct net_device		*net;
	bool				bound;
	int				bind_count;
	struct mutex			lock;
	int				refcnt;
};

#endif /* U_GETHER_H */
