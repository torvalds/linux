// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#define pr_fmt(fmt) "drm_buddy: " fmt

#include <linux/module.h>

#include <drm/drm_buddy.h>

#include "../lib/drm_random.h"

#define TESTS "drm_buddy_selftests.h"
#include "drm_selftest.h"

static unsigned int random_seed;

static int igt_sanitycheck(void *ignored)
{
	pr_info("%s - ok!\n", __func__);
	return 0;
}

#include "drm_selftest.c"

static int __init test_drm_buddy_init(void)
{
	int err;

	while (!random_seed)
		random_seed = get_random_int();

	pr_info("Testing DRM buddy manager (struct drm_buddy), with random_seed=0x%x\n",
		random_seed);
	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

static void __exit test_drm_buddy_exit(void)
{
}

module_init(test_drm_buddy_init);
module_exit(test_drm_buddy_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
