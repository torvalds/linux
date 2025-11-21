/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Platform Security Processor (PSP) Seamless Firmware (SFS) Support.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 *
 * Author: Ashish Kalra <ashish.kalra@amd.com>
 */

#ifndef __SFS_H__
#define __SFS_H__

#include <uapi/linux/psp-sfs.h>

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/psp-sev.h>
#include <linux/psp-platform-access.h>
#include <linux/set_memory.h>

#include "psp-dev.h"

struct sfs_misc_dev {
	struct kref refcount;
	struct miscdevice misc;
};

struct sfs_command {
	struct psp_ext_req_buffer_hdr hdr;
	u8 buf[PAGE_SIZE - sizeof(struct psp_ext_req_buffer_hdr)];
	u8 sfs_buffer[];
} __packed;

struct sfs_device {
	struct device *dev;
	struct psp_device *psp;

	struct page *page;
	struct sfs_command *command_buf;

	struct sfs_misc_dev *misc;
};

void sfs_dev_destroy(struct psp_device *psp);
int sfs_dev_init(struct psp_device *psp);

#endif /* __SFS_H__ */
