// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_module.h"

#include <linux/init.h>
#include <linux/module.h>

#include "xe_drv.h"
#include "xe_hw_fence.h"
#include "xe_pci.h"
#include "xe_sched_job.h"

struct xe_modparam xe_modparam = {
	.enable_display = true,
	.guc_log_level = 5,
	.force_probe = CONFIG_DRM_XE_FORCE_PROBE,
	/* the rest are 0 by default */
};

module_param_named_unsafe(force_execlist, xe_modparam.force_execlist, bool, 0444);
MODULE_PARM_DESC(force_execlist, "Force Execlist submission");

module_param_named(enable_display, xe_modparam.enable_display, bool, 0444);
MODULE_PARM_DESC(enable_display, "Enable display");

module_param_named(vram_bar_size, xe_modparam.force_vram_bar_size, uint, 0600);
MODULE_PARM_DESC(vram_bar_size, "Set the vram bar size(in MiB)");

module_param_named(guc_log_level, xe_modparam.guc_log_level, int, 0600);
MODULE_PARM_DESC(guc_log_level, "GuC firmware logging level (0=disable, 1..5=enable with verbosity min..max)");

module_param_named_unsafe(guc_firmware_path, xe_modparam.guc_firmware_path, charp, 0400);
MODULE_PARM_DESC(guc_firmware_path,
		 "GuC firmware path to use instead of the default one");

module_param_named_unsafe(huc_firmware_path, xe_modparam.huc_firmware_path, charp, 0400);
MODULE_PARM_DESC(huc_firmware_path,
		 "HuC firmware path to use instead of the default one - empty string disables");

module_param_named_unsafe(gsc_firmware_path, xe_modparam.gsc_firmware_path, charp, 0400);
MODULE_PARM_DESC(gsc_firmware_path,
		 "GSC firmware path to use instead of the default one - empty string disables");

module_param_named_unsafe(force_probe, xe_modparam.force_probe, charp, 0400);
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
	{
		.init = xe_register_pci_driver,
		.exit = xe_unregister_pci_driver,
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

	return 0;
}

static void __exit xe_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(init_funcs) - 1; i >= 0; i--)
		init_funcs[i].exit();
}

module_init(xe_init);
module_exit(xe_exit);

MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
