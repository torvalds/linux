// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debugfs interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/debugfs.h>

#include "wfx.h"

#define CREATE_TRACE_POINTS
#include "traces.h"

int wfx_debug_init(struct wfx_dev *wdev)
{
	struct dentry *d;

	d = debugfs_create_dir("wfx", wdev->hw->wiphy->debugfsdir);

	return 0;
}
