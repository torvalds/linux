/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2021 Intel Corporation
 */

#include <linux/console.h>

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_object.h"
#include "i915_active.h"
#include "i915_buddy.h"
#include "i915_params.h"
#include "i915_pci.h"
#include "i915_perf.h"
#include "i915_request.h"
#include "i915_scheduler.h"
#include "i915_selftest.h"
#include "i915_vma.h"

static int i915_check_nomodeset(void)
{
	bool use_kms = true;

	/*
	 * Enable KMS by default, unless explicitly overriden by
	 * either the i915.modeset prarameter or by the
	 * vga_text_mode_force boot option.
	 */

	if (i915_modparams.modeset == 0)
		use_kms = false;

	if (vgacon_text_force() && i915_modparams.modeset == -1)
		use_kms = false;

	if (!use_kms) {
		/* Silently fail loading to not upset userspace. */
		DRM_DEBUG_DRIVER("KMS disabled.\n");
		return 1;
	}

	return 0;
}

static const struct {
   int (*init)(void);
   void (*exit)(void);
} init_funcs[] = {
	{ .init = i915_check_nomodeset },
	{ .init = i915_active_module_init,
	  .exit = i915_active_module_exit },
	{ .init = i915_buddy_module_init,
	  .exit = i915_buddy_module_exit },
	{ .init = i915_context_module_init,
	  .exit = i915_context_module_exit },
	{ .init = i915_gem_context_module_init,
	  .exit = i915_gem_context_module_exit },
	{ .init = i915_objects_module_init,
	  .exit = i915_objects_module_exit },
	{ .init = i915_request_module_init,
	  .exit = i915_request_module_exit },
	{ .init = i915_scheduler_module_init,
	  .exit = i915_scheduler_module_exit },
	{ .init = i915_vma_module_init,
	  .exit = i915_vma_module_exit },
	{ .init = i915_mock_selftests },
	{ .init = i915_pmu_init,
	  .exit = i915_pmu_exit },
	{ .init = i915_pci_register_driver,
	  .exit = i915_pci_unregister_driver },
	{ .init = i915_perf_sysctl_register,
	  .exit = i915_perf_sysctl_unregister },
};
static int init_progress;

static int __init i915_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(init_funcs); i++) {
		err = init_funcs[i].init();
		if (err < 0) {
			while (i--) {
				if (init_funcs[i].exit)
					init_funcs[i].exit();
			}
			return err;
		} else if (err > 0) {
			/*
			 * Early-exit success is reserved for things which
			 * don't have an exit() function because we have no
			 * idea how far they got or how to partially tear
			 * them down.
			 */
			WARN_ON(init_funcs[i].exit);
			break;
		}
	}

	init_progress = i;

	return 0;
}

static void __exit i915_exit(void)
{
	int i;

	for (i = init_progress - 1; i >= 0; i--) {
		GEM_BUG_ON(i >= ARRAY_SIZE(init_funcs));
		if (init_funcs[i].exit)
			init_funcs[i].exit();
	}
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR("Tungsten Graphics, Inc.");
MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
