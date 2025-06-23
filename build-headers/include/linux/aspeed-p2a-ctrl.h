/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2019 Google Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Provides a simple driver to control the ASPEED P2A interface which allows
 * the host to read and write to various regions of the BMC's memory.
 */

#ifndef _LINUX_ASPEED_P2A_CTRL_H
#define _LINUX_ASPEED_P2A_CTRL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ASPEED_P2A_CTRL_READ_ONLY 0
#define ASPEED_P2A_CTRL_READWRITE 1

/*
 * This driver provides a mechanism for enabling or disabling the read-write
 * property of specific windows into the ASPEED BMC's memory.
 *
 * A user can map a region of the BMC's memory as read-only or read-write, with
 * the caveat that once any region is mapped, all regions are unlocked for
 * reading.
 */

/*
 * Unlock a region of BMC physical memory for access from the host.
 *
 * Also used to read back the optional memory-region configuration for the
 * driver.
 */
struct aspeed_p2a_ctrl_mapping {
	__u64 addr;
	__u32 length;
	__u32 flags;
};

#define __ASPEED_P2A_CTRL_IOCTL_MAGIC 0xb3

/*
 * This IOCTL is meant to configure a region or regions of memory given a
 * starting address and length to be readable by the host, or
 * readable-writeable.
 */
#define ASPEED_P2A_CTRL_IOCTL_SET_WINDOW _IOW(__ASPEED_P2A_CTRL_IOCTL_MAGIC, \
		0x00, struct aspeed_p2a_ctrl_mapping)

/*
 * This IOCTL is meant to read back to the user the base address and length of
 * the memory-region specified to the driver for use with mmap.
 */
#define ASPEED_P2A_CTRL_IOCTL_GET_MEMORY_CONFIG \
	_IOWR(__ASPEED_P2A_CTRL_IOCTL_MAGIC, \
		0x01, struct aspeed_p2a_ctrl_mapping)

#endif /* _LINUX_ASPEED_P2A_CTRL_H */
