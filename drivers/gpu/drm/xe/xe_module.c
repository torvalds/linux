// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_module.h"

#include <linux/init.h>
#include <linux/module.h>

#include <drm/drm_module.h>

#include "xe_drv.h"
#include "xe_configfs.h"
#include "xe_hw_fence.h"
#include "xe_pci.h"
#include "xe_pm.h"
#include "xe_observation.h"
#include "xe_sched_job.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define DEFAULT_GUC_LOG_LEVEL		3
#else
#define DEFAULT_GUC_LOG_LEVEL		1
#endif

#define DEFAULT_PROBE_DISPLAY		true
#define DEFAULT_VRAM_BAR_SIZE		0
#define DEFAULT_FORCE_PROBE		CONFIG_DRM_XE_FORCE_PROBE
#define DEFAULT_MAX_VFS			~0
#define DEFAULT_MAX_VFS_STR		"unlimited"
#define DEFAULT_WEDGED_MODE		1
#define DEFAULT_SVM_NOTIFIER_SIZE	512

struct xe_modparam xe_modparam = {
	.probe_display =	DEFAULT_PROBE_DISPLAY,
	.guc_log_level =	DEFAULT_GUC_LOG_LEVEL,
	.force_probe =		DEFAULT_FORCE_PROBE,
#ifdef CONFIG_PCI_IOV
	.max_vfs =		DEFAULT_MAX_VFS,
#endif
	.wedged_mode =		DEFAULT_WEDGED_MODE,
	.svm_notifier_size =	DEFAULT_SVM_NOTIFIER_SIZE,
	/* the rest are 0 by default */
};

module_param_named(svm_notifier_size, xe_modparam.svm_notifier_size, uint, 0600);
MODULE_PARM_DESC(svm_notifier_size, "Set the svm notifier size in MiB, must be power of 2 "
		 "[default=" __stringify(DEFAULT_SVM_NOTIFIER_SIZE) "]");

module_param_named_unsafe(force_execlist, xe_modparam.force_execlist, bool, 0444);
MODULE_PARM_DESC(force_execlist, "Force Execlist submission");

module_param_named(probe_display, xe_modparam.probe_display, bool, 0444);
MODULE_PARM_DESC(probe_display, "Probe display HW, otherwise it's left untouched "
		 "[default=" __stringify(DEFAULT_PROBE_DISPLAY) "])");

module_param_named(vram_bar_size, xe_modparam.force_vram_bar_size, int, 0600);
MODULE_PARM_DESC(vram_bar_size, "Set the vram bar size in MiB (<0=disable-resize, 0=max-needed-size, >0=force-size "
		 "[default=" __stringify(DEFAULT_VRAM_BAR_SIZE) "])");

module_param_named(guc_log_level, xe_modparam.guc_log_level, int, 0600);
MODULE_PARM_DESC(guc_log_level, "GuC firmware logging level (0=disable, 1=normal, 2..5=verbose-levels "
		 "[default=" __stringify(DEFAULT_GUC_LOG_LEVEL) "])");

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
		 "Force probe options for specified devices. See CONFIG_DRM_XE_FORCE_PROBE for details "
		 "[default=" DEFAULT_FORCE_PROBE "])");

#ifdef CONFIG_PCI_IOV
module_param_named(max_vfs, xe_modparam.max_vfs, uint, 0400);
MODULE_PARM_DESC(max_vfs,
		 "Limit number of Virtual Functions (VFs) that could be managed. "
		 "(0=no VFs; N=allow up to N VFs "
		 "[default=" DEFAULT_MAX_VFS_STR "])");
#endif

module_param_named_unsafe(wedged_mode, xe_modparam.wedged_mode, int, 0600);
MODULE_PARM_DESC(wedged_mode,
		 "Module's default policy for the wedged mode (0=never, 1=upon-critical-errors, 2=upon-any-hang "
		 "[default=" __stringify(DEFAULT_WEDGED_MODE) "])");

static int xe_check_nomodeset(void)
{
	if (drm_firmware_drivers_only())
		return -ENODEV;

	return 0;
}

struct init_funcs {
	int (*init)(void);
	void (*exit)(void);
};

static const struct init_funcs init_funcs[] = {
	{
		.init = xe_check_nomodeset,
	},
	{
		.init = xe_configfs_init,
		.exit = xe_configfs_exit,
	},
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
	{
		.init = xe_observation_sysctl_register,
		.exit = xe_observation_sysctl_unregister,
	},
	{
		.init = xe_pm_module_init,
	},
};

static int __init xe_call_init_func(const struct init_funcs *func)
{
	if (func->init)
		return func->init();
	return 0;
}

static void xe_call_exit_func(const struct init_funcs *func)
{
	if (func->exit)
		func->exit();
}

static int __init xe_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(init_funcs); i++) {
		err = xe_call_init_func(init_funcs + i);
		if (err) {
			pr_info("%s: module_init aborted at %ps %pe\n",
				DRIVER_NAME, init_funcs[i].init, ERR_PTR(err));
			while (i--)
				xe_call_exit_func(init_funcs + i);
			return err;
		}
	}

	return 0;
}

static void __exit xe_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(init_funcs) - 1; i >= 0; i--)
		xe_call_exit_func(init_funcs + i);
}

module_init(xe_init);
module_exit(xe_exit);

MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
