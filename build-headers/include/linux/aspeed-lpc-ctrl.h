/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_ASPEED_LPC_CTRL_H
#define _LINUX_ASPEED_LPC_CTRL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Window types */
#define ASPEED_LPC_CTRL_WINDOW_FLASH	1
#define ASPEED_LPC_CTRL_WINDOW_MEMORY	2

/*
 * This driver provides a window for the host to access a BMC resource
 * across the BMC <-> Host LPC bus.
 *
 * window_type: The BMC resource that the host will access through the
 * window. BMC flash and BMC RAM.
 *
 * window_id: For each window type there may be multiple windows,
 * these are referenced by ID.
 *
 * flags: Reserved for future use, this field is expected to be
 * zeroed.
 *
 * addr: Address on the host LPC bus that the specified window should
 * be mapped. This address must be power of two aligned.
 *
 * offset: Offset into the BMC window that should be mapped to the
 * host (at addr). This must be a multiple of size.
 *
 * size: The size of the mapping. The smallest possible size is 64K.
 * This must be power of two aligned.
 *
 */

struct aspeed_lpc_ctrl_mapping {
	__u8	window_type;
	__u8	window_id;
	__u16	flags;
	__u32	addr;
	__u32	offset;
	__u32	size;
};

#define __ASPEED_LPC_CTRL_IOCTL_MAGIC	0xb2

#define ASPEED_LPC_CTRL_IOCTL_GET_SIZE	_IOWR(__ASPEED_LPC_CTRL_IOCTL_MAGIC, \
		0x00, struct aspeed_lpc_ctrl_mapping)

#define ASPEED_LPC_CTRL_IOCTL_MAP	_IOW(__ASPEED_LPC_CTRL_IOCTL_MAGIC, \
		0x01, struct aspeed_lpc_ctrl_mapping)

#endif /* _LINUX_ASPEED_LPC_CTRL_H */
