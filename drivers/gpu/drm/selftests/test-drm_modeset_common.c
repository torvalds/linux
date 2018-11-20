// SPDX-License-Identifier: GPL-2.0
/*
 * Common file for modeset selftests.
 */

#include <linux/module.h>

#include "test-drm_modeset_common.h"

#define TESTS "drm_modeset_selftests.h"
#include "drm_selftest.h"

#include "drm_selftest.c"

static int __init test_drm_modeset_init(void)
{
	int err;

	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

static void __exit test_drm_modeset_exit(void)
{
}

module_init(test_drm_modeset_init);
module_exit(test_drm_modeset_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
