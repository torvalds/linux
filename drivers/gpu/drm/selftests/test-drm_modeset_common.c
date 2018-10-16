// SPDX-License-Identifier: GPL-2.0
/*
 * Common file for modeset selftests.
 */

#include <linux/module.h>

#include "test-drm_modeset_common.h"

static int __init test_drm_modeset_init(void)
{
	return test_drm_plane_helper();
}

static void __exit test_drm_modeset_exit(void)
{
}

module_init(test_drm_modeset_init);
module_exit(test_drm_modeset_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
