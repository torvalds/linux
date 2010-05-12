/* drivers/staging/dream/include/linux/msm_rpcrouter.h
 *
 * Copyright (c) QUALCOMM Incorporated
 * Copyright (C) 2007 Google, Inc.
 * Author: San Mehat <san@android.com>
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
#ifndef __LINUX_MSM_RPCROUTER_H
#define __LINUX_MSM_RPCROUTER_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define RPC_ROUTER_VERSION_V1 0x00010000

struct rpcrouter_ioctl_server_args {
	uint32_t prog;
	uint32_t vers;
};

#define RPC_ROUTER_IOCTL_MAGIC (0xC1)

#define RPC_ROUTER_IOCTL_GET_VERSION \
	_IOR(RPC_ROUTER_IOCTL_MAGIC, 0, unsigned int)

#define RPC_ROUTER_IOCTL_GET_MTU \
	_IOR(RPC_ROUTER_IOCTL_MAGIC, 1, unsigned int)

#define RPC_ROUTER_IOCTL_REGISTER_SERVER \
	_IOWR(RPC_ROUTER_IOCTL_MAGIC, 2, unsigned int)

#define RPC_ROUTER_IOCTL_UNREGISTER_SERVER \
	_IOWR(RPC_ROUTER_IOCTL_MAGIC, 3, unsigned int)

#define RPC_ROUTER_IOCTL_GET_MINOR_VERSION \
	_IOW(RPC_ROUTER_IOCTL_MAGIC, 4, unsigned int)

#endif
