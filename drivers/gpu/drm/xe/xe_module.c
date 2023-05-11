// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_module.h"

#include <linux/init.h>
#include <linux/module.h>

#include "xe_drv.h"
#include "xe_hw_fence.h"
#include "xe_module.h"
#include "xe_pci.h"
#include "xe_sched_job.h"

bool enable_guc = true;
module_param_named_unsafe(enable_guc, enable_guc, bool, 0444);
MODULE_PARM_DESC(enable_guc, "Enable GuC submission");

u32 xe_force_vram_bar_size;
module_param_named(vram_bar_size, xe_force_vram_bar_size, uint, 0600);
MODULE_PARM_DESC(vram_bar_size, "Set the vram bar size(in MiB)");

int xe_guc_log_level = 5;
module_param_named(guc_log_level, xe_guc_log_level, int, 0600);
MODULE_PARM_DESC(guc_log_level, "GuC firmware logging level (0=disable, 1..5=enable with verbosity min..max)");

char *xe_param_force_probe = CONFIG_DRM_XE_FORCE_PROBE;
module_param_named_unsafe(force_probe, xe_param_force_probe, charp, 0400);
MODULE_PARM_DESC(force_probe,
		 "Force probe options for specified devices. See CONFIG_DRM_XE_FORCE_PROBE for details.");

struct init_funcs {
	int (*init)(void);
	void (*exit)(void);
};

static const struct init_funcs init_funcs[] = {
	{
		.init = xe_hw_fence_module_init,
		.exit = xe_hw_fence_module_exit,
	},
	{
		.init = xe_sched_job_module_init,
		.exit = xe_sched_job_module_exit,
	},
};

static int __init xe_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(init_funcs); i++) {
		err = init_funcs[i].init();
		if (err) {
			while (i--)
				init_funcs[i].exit();
			return err;
		}
	}

	return xe_register_pci_driver();
}

static void __exit xe_exit(void)
{
	int i;

	xe_unregister_pci_driver();

	for (i = ARRAY_SIZE(init_funcs) - 1; i >= 0; i--)
		init_funcs[i].exit();
}

module_init(xe_init);
module_exit(xe_exit);

MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
