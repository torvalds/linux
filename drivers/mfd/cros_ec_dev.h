/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cros_ec_dev - expose the Chrome OS Embedded Controller to userspace
 *
 * Copyright (C) 2014 Google, Inc.
 */

#ifndef _CROS_EC_DEV_H_
#define _CROS_EC_DEV_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/mfd/cros_ec.h>

#define CROS_EC_DEV_VERSION "1.0.0"

/**
 * struct cros_ec_readmem - Struct used to read mapped memory.
 * @offset: Within EC_LPC_ADDR_MEMMAP region.
 * @bytes: Number of bytes to read. Zero means "read a string" (including '\0')
 *         At most only EC_MEMMAP_SIZE bytes can be read.
 * @buffer: Where to store the result. The ioctl returns the number of bytes
 *         read or negative on error.
 */
struct cros_ec_readmem {
	uint32_t offset;
	uint32_t bytes;
	uint8_t buffer[EC_MEMMAP_SIZE];
};

#define CROS_EC_DEV_IOC       0xEC
#define CROS_EC_DEV_IOCXCMD   _IOWR(CROS_EC_DEV_IOC, 0, struct cros_ec_command)
#define CROS_EC_DEV_IOCRDMEM  _IOWR(CROS_EC_DEV_IOC, 1, struct cros_ec_readmem)

#endif /* _CROS_EC_DEV_H_ */
