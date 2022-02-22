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

static int igt_buddy_alloc_limit(void *arg)
{
	u64 end, size = U64_MAX, start = 0;
	struct drm_buddy_block *block;
	unsigned long flags = 0;
	LIST_HEAD(allocated);
	struct drm_buddy mm;
	int err;

	size = end = round_down(size, 4096);
	err = drm_buddy_init(&mm, size, PAGE_SIZE);
	if (err)
		return err;

	if (mm.max_order != DRM_BUDDY_MAX_ORDER) {
		pr_err("mm.max_order(%d) != %d\n",
		       mm.max_order, DRM_BUDDY_MAX_ORDER);
		err = -EINVAL;
		goto out_fini;
	}

	err = drm_buddy_alloc_blocks(&mm, start, end, size,
				     PAGE_SIZE, &allocated, flags);

	if (unlikely(err))
		goto out_free;

	block = list_first_entry_or_null(&allocated,
					 struct drm_buddy_block,
					 link);

	if (!block) {
		err = -EINVAL;
		goto out_fini;
	}

	if (drm_buddy_block_order(block) != mm.max_order) {
		pr_err("block order(%d) != %d\n",
		       drm_buddy_block_order(block), mm.max_order);
		err = -EINVAL;
		goto out_free;
	}

	if (drm_buddy_block_size(&mm, block) !=
	    BIT_ULL(mm.max_order) * PAGE_SIZE) {
		pr_err("block size(%llu) != %llu\n",
		       drm_buddy_block_size(&mm, block),
		       BIT_ULL(mm.max_order) * PAGE_SIZE);
		err = -EINVAL;
		goto out_free;
	}

out_free:
	drm_buddy_free_list(&mm, &allocated);
out_fini:
	drm_buddy_fini(&mm);
	return err;
}

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
