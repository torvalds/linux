/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * OPAL Runtime Diagnostics interface driver
 * Supported on POWERNV platform
 *
 * (C) Copyright IBM 2015
 *
 * Author: Vaidyanathan Srinivasan <svaidy at linux.vnet.ibm.com>
 * Author: Jeremy Kerr <jk@ozlabs.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _UAPI_ASM_POWERPC_OPAL_PRD_H_
#define _UAPI_ASM_POWERPC_OPAL_PRD_H_

#include <linux/types.h>

/**
 * The version of the kernel interface of the PRD system. This describes the
 * interface available for the /dev/opal-prd device. The actual PRD message
 * layout and content is private to the firmware <--> userspace interface, so
 * is not covered by this versioning.
 *
 * Future interface versions are backwards-compatible; if a later kernel
 * version is encountered, functionality provided in earlier versions
 * will work.
 */
#define OPAL_PRD_KERNEL_VERSION		1

#define OPAL_PRD_GET_INFO		_IOR('o', 0x01, struct opal_prd_info)
#define OPAL_PRD_SCOM_READ		_IOR('o', 0x02, struct opal_prd_scom)
#define OPAL_PRD_SCOM_WRITE		_IOW('o', 0x03, struct opal_prd_scom)

#ifndef __ASSEMBLY__

struct opal_prd_info {
	__u64	version;
	__u64	reserved[3];
};

struct opal_prd_scom {
	__u64	chip;
	__u64	addr;
	__u64	data;
	__s64	rc;
};

#endif /* __ASSEMBLY__ */

#endif /* _UAPI_ASM_POWERPC_OPAL_PRD_H */
