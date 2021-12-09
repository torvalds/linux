// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#include <linux/debugfs.h>
#include <linux/wwan.h>

#include "iosm_ipc_imem.h"
#include "iosm_ipc_trace.h"
#include "iosm_ipc_debugfs.h"

void ipc_debugfs_init(struct iosm_imem *ipc_imem)
{
	struct dentry *debugfs_pdev = wwan_get_debugfs_dir(ipc_imem->dev);

	ipc_imem->debugfs_dir = debugfs_create_dir(KBUILD_MODNAME,
						   debugfs_pdev);

	ipc_imem->trace = ipc_trace_init(ipc_imem);
	if (!ipc_imem->trace)
		dev_warn(ipc_imem->dev, "trace channel init failed");
}

void ipc_debugfs_deinit(struct iosm_imem *ipc_imem)
{
	ipc_trace_deinit(ipc_imem->trace);
	debugfs_remove_recursive(ipc_imem->debugfs_dir);
}
