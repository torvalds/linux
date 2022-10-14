/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_PRIV_VM_MGR_H
#define _GH_PRIV_VM_MGR_H

#include <linux/gunyah_rsc_mgr.h>

#include <uapi/linux/gunyah.h>

long gh_dev_vm_mgr_ioctl(struct gh_rm *rm, unsigned int cmd, unsigned long arg);

struct gh_vm {
	u16 vmid;
	struct gh_rm *rm;
	struct device *parent;

	struct work_struct free_work;
};

#endif
