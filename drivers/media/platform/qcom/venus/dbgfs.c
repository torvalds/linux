// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Linaro Ltd.
 */

#include <linux/debugfs.h>
#include <linux/fault-inject.h>

#include "core.h"

#ifdef CONFIG_FAULT_INJECTION
DECLARE_FAULT_ATTR(venus_ssr_attr);
#endif

void venus_dbgfs_init(struct venus_core *core)
{
	core->root = debugfs_create_dir("venus", NULL);
	debugfs_create_x32("fw_level", 0644, core->root, &venus_fw_debug);

#ifdef CONFIG_FAULT_INJECTION
	fault_create_debugfs_attr("fail_ssr", core->root, &venus_ssr_attr);
#endif
}

void venus_dbgfs_deinit(struct venus_core *core)
{
	debugfs_remove_recursive(core->root);
}
