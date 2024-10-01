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
	struct psp_device *psp;

	union dbc_buffer *mbox;

	struct mutex ioctl_mutex;

	struct miscdevice char_dev;

	/* used to abstract communication path */
	bool	use_ext;
	u32	header_size;
	u32	*payload_size;
	u32	*result;
	void	*payload;
};

union dbc_buffer {
	struct psp_request		pa_req;
	struct psp_ext_request		ext_req;
};

void dbc_dev_destroy(struct psp_device *psp);
int dbc_dev_init(struct psp_device *psp);

#endif /* __DBC_H */
