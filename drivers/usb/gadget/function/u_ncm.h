/* SPDX-License-Identifier: GPL-2.0 */
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

/**
 * struct f_ncm_opts - NCM function options
 * @func_inst: USB function instance.
 * @net: The net_device associated with the NCM function.
 * @bind_count: Tracks the number of configurations the NCM function is
 *              bound to, preventing double-registration of the @net device.
 * @ncm_interf_group: ConfigFS group for NCM interface.
 * @ncm_os_desc: USB OS descriptor for NCM.
 * @ncm_ext_compat_id: Extended compatibility ID.
 * @lock: Protects the data from concurrent access by configfs read/write
 *        and create symlink/remove symlink operations.
 * @refcnt: Reference counter for the function instance.
 * @max_segment_size: Maximum segment size.
 */
struct f_ncm_opts {
	struct usb_function_instance	func_inst;
	struct net_device		*net;
	int				bind_count;

	struct config_group		*ncm_interf_group;
	struct usb_os_desc		ncm_os_desc;
	char				ncm_ext_compat_id[16];

	struct mutex			lock;
	int				refcnt;

	u16				max_segment_size;
};

#endif /* U_NCM_H */
