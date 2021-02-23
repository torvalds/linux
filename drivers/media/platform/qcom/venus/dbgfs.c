// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Linaro Ltd.
 */

#include <linux/debugfs.h>

#include "core.h"

void venus_dbgfs_init(struct venus_core *core)
{
	core->root = debugfs_create_dir("venus", NULL);
	debugfs_create_x32("fw_level", 0644, core->root, &venus_fw_debug);
}

void venus_dbgfs_deinit(struct venus_core *core)
{
	debugfs_remove_recursive(core->root);
}
