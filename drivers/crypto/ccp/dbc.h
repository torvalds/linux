/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Platform Security Processor (PSP) Dynamic Boost Control support
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#ifndef __DBC_H__
#define __DBC_H__

#include <uapi/linux/psp-dbc.h>

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/psp-platform-access.h>

#include "psp-dev.h"

struct psp_dbc_device {
	struct device *dev;

	union dbc_buffer *mbox;

	struct mutex ioctl_mutex;

	struct miscdevice char_dev;
};

struct dbc_nonce {
	struct psp_req_buffer_hdr	header;
	struct dbc_user_nonce		user;
} __packed;

struct dbc_set_uid {
	struct psp_req_buffer_hdr	header;
	struct dbc_user_setuid		user;
} __packed;

struct dbc_param {
	struct psp_req_buffer_hdr	header;
	struct dbc_user_param		user;
} __packed;

union dbc_buffer {
	struct psp_request		req;
	struct dbc_nonce		dbc_nonce;
	struct dbc_set_uid		dbc_set_uid;
	struct dbc_param		dbc_param;
};

void dbc_dev_destroy(struct psp_device *psp);
int dbc_dev_init(struct psp_device *psp);

#endif /* __DBC_H */
