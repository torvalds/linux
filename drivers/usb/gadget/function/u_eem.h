/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_eem.h
 *
 * Utility definitions for the eem function
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_EEM_H
#define U_EEM_H

#include <linux/usb/composite.h>

/**
 * struct f_eem_opts - EEM function options
 * @func_inst: USB function instance.
 * @net: The net_device associated with the EEM function.
 * @bound: True if the net_device is shared and pre-registered during the
 *         legacy composite driver's bind phase (e.g., multi.c). If false,
 *         the EEM function will register the net_device during its own
 *         bind phase.
 * @bind_count: Tracks the number of configurations the EEM function is
 *              bound to, preventing double-registration of the @net device.
 * @lock: Protects the data from concurrent access by configfs read/write
 *        and create symlink/remove symlink operations.
 * @refcnt: Reference counter for the function instance.
 */
struct f_eem_opts {
	struct usb_function_instance	func_inst;
	struct net_device		*net;
	bool				bound;
	int				bind_count;

	struct mutex			lock;
	int				refcnt;
};

#endif /* U_EEM_H */
