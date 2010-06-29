/*
 * arch/arm/mach-tegra/include/linux/nvrpc_ioctl.h
 *
 * structure declarations for nvrpc user-space ioctls
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/ioctl.h>
#include <linux/types.h>

#if !defined(__KERNEL__)
#define __user
#endif

#ifndef _MACH_TEGRA_NVRPC_IOCTL_H_
#define _MACH_TEGRA_NVRPC_IOCTL_H_

struct nvrpc_handle_param {
	__u32 handle;
	__u32 param;
	__u32 ret_val;              /* operation status */
};

struct nvrpc_open_params {
	__u32 rm_handle;            /* rm device handle */
	__u32 port_name_size;       /* port name buffer size */
	__u32 sem;                  /* receive semaphore handle */
	__u32 transport_handle;     /* transport handle */
	__u32 ret_val;              /* operation status */
	unsigned long port_name;    /* port name */
};

struct nvrpc_set_queue_depth_params {
	__u32 transport_handle;     /* transport handle */
	__u32 max_queue_depth;      /* maximum number of message in Queue */
	__u32 max_message_size;     /* maximum size of the message in bytes */
	__u32 ret_val;              /* operation status */
};

struct nvrpc_msg_params {
	__u32 transport_handle;     /* transport handle */
	__u32 max_message_size;     /* maximum size of the message in bytes */
	__u32 params;               /* timeout in ms */
	__u32 ret_val;              /* operation status */
	unsigned long msg_buffer;
};

#define NVRPC_IOC_MAGIC 'N'

#define NVRPC_IOCTL_INIT                    \
	_IOWR(NVRPC_IOC_MAGIC, 0x30, struct nvrpc_handle_param)
#define NVRPC_IOCTL_OPEN                    \
	_IOWR(NVRPC_IOC_MAGIC, 0x31, struct nvrpc_open_params)
#define NVRPC_IOCTL_GET_PORTNAME            \
	_IOWR(NVRPC_IOC_MAGIC, 0x32, struct nvrpc_open_params)
#define NVRPC_IOCTL_CLOSE                   \
	_IOWR(NVRPC_IOC_MAGIC, 0x33, struct nvrpc_handle_param)
#define NVRPC_IOCTL_DEINIT                   \
	_IOWR(NVRPC_IOC_MAGIC, 0x34, struct nvrpc_handle_param)
#define NVRPC_IOCTL_WAIT_FOR_CONNECT        \
	_IOWR(NVRPC_IOC_MAGIC, 0x35, struct nvrpc_handle_param)
#define NVRPC_IOCTL_CONNECT                 \
	_IOWR(NVRPC_IOC_MAGIC, 0x36, struct nvrpc_handle_param)
#define NVRPC_IOCTL_SET_QUEUE_DEPTH         \
	_IOWR(NVRPC_IOC_MAGIC, 0x37, struct nvrpc_set_queue_depth_params)
#define NVRPC_IOCTL_SEND_MSG                \
	_IOWR(NVRPC_IOC_MAGIC, 0x38, struct nvrpc_msg_params)
#define NVRPC_IOCTL_SEND_MSG_LP0            \
	_IOWR(NVRPC_IOC_MAGIC, 0x39, struct nvrpc_msg_params)
#define NVRPC_IOCTL_RECV_MSG           \
	_IOWR(NVRPC_IOC_MAGIC, 0x3A, struct nvrpc_msg_params)
#define NVRPC_IOCTL_XPC_INIT                \
	_IOWR(NVRPC_IOC_MAGIC, 0x3B, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_ACQUIRE             \
	_IOWR(NVRPC_IOC_MAGIC, 0x3C, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_RELEASE             \
	_IOWR(NVRPC_IOC_MAGIC, 0x3D, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_GET_MSG             \
	_IOWR(NVRPC_IOC_MAGIC, 0x3E, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_SEND_MSG            \
	_IOWR(NVRPC_IOC_MAGIC, 0x3F, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_DESTROY             \
	_IOWR(NVRPC_IOC_MAGIC, 0x40, struct nvrpc_handle_param)
#define NVRPC_IOCTL_XPC_CREATE              \
	_IOWR(NVRPC_IOC_MAGIC, 0x41, struct nvrpc_handle_param)

#endif
