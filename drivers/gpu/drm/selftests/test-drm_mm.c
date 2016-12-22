/*
 * Test cases for the drm_mm range manager
 */

#define pr_fmt(fmt) "drm_mm: " fmt

#include <linux/module.h>
#include <linux/prime_numbers.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/vmalloc.h>

#include <drm/drm_mm.h>

#include "../lib/drm_random.h"

#define TESTS "drm_mm_selftests.h"
#include "drm_selftest.h"

static unsigned int random_seed;
static unsigned int max_iterations = 8192;
static unsigned int max_prime = 128;

static int igt_sanitycheck(void *ignored)
{
	pr_info("%s - ok!\n", __func__);
	return 0;
}

#include "drm_selftest.c"

static int __init test_drm_mm_init(void)
{
	int err;

	while (!random_seed)
		random_seed = get_random_int();

	pr_info("Testing DRM range manger (struct drm_mm), with random_seed=0x%x max_iterations=%u max_prime=%u\n",
		random_seed, max_iterations, max_prime);
	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}

static void __exit test_drm_mm_exit(void)
{
}

module_init(test_drm_mm_init);
module_exit(test_drm_mm_exit);

module_param(random_seed, uint, 0400);
module_param(max_iterations, uint, 0400);
module_param(max_prime, uint, 0400);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
