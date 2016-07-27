/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UAPI_LINUX_SW_SYNC_H
#define _UAPI_LINUX_SW_SYNC_H

#include <linux/types.h>

struct sw_sync_create_fence_data {
	__u32	value;
	char	name[32];
	__s32	fence; /* fd of new fence */
};

#define SW_SYNC_IOC_MAGIC	'W'

#define SW_SYNC_IOC_CREATE_FENCE	_IOWR(SW_SYNC_IOC_MAGIC, 0,\
		struct sw_sync_create_fence_data)
#define SW_SYNC_IOC_INC			_IOW(SW_SYNC_IOC_MAGIC, 1, __u32)

#endif /* _UAPI_LINUX_SW_SYNC_H */
