// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test suite for GEM objects backed by shmem buffers
 *
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * Author: Marco Pagani <marpagan@redhat.com>
 */

#include <linux/dma-buf.h>
#include <linux/iosys-map.h>
#include <linux/sizes.h>

#include <kunit/test.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_kunit_helpers.h>

#define TEST_SIZE		SZ_1M
#define TEST_BYTE		0xae

/*
 * Wrappers to avoid an explicit type casting when passing action
 * functions to kunit_add_action().
 */
static void kfree_wrapper(void *ptr)
{
	const void *obj = ptr;

	kfree(obj);
}

static void sg_free_table_wrapper(void *ptr)
{
	struct sg_table *sgt = ptr;

	sg_free_table(sgt);
}

static void drm_gem_shmem_free_wrapper(void *ptr)
{
	struct drm_gem_shmem_object *shmem = ptr;

	drm_gem_shmem_free(shmem);
}

/*
 * Test creating a shmem GEM object backed by shmem buffer. The test
 * case succeeds if the GEM object is successfully allocated with the
 * shmem file node and object functions attributes set, and the size
 * attribute is equal to the correct size.
 */
static void drm_gem_shmem_test_obj_create(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);
	KUNIT_EXPECT_EQ(test, shmem->base.size, TEST_SIZE);
	KUNIT_EXPECT_NOT_NULL(test, shmem->base.filp);
	KUNIT_EXPECT_NOT_NULL(test, shmem->base.funcs);

	drm_gem_shmem_free(shmem);
}

/*
 * Test creating a shmem GEM object from a scatter/gather table exported
 * via a DMA-BUF. The test case succeed if the GEM object is successfully
 * created with the shmem file node attribute equal to NULL and the sgt
 * attribute pointing to the scatter/gather table that has been imported.
 */
static void drm_gem_shmem_test_obj_create_private(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *gem_obj;
	struct dma_buf buf_mock;
	struct dma_buf_attachment attach_mock;
	struct sg_table *sgt;
	char *buf;
	int ret;

	/* Create a mock scatter/gather table */
	buf = kunit_kzalloc(test, TEST_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sgt);

	ret = kunit_add_action_or_reset(test, kfree_wrapper, sgt);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test, sg_free_table_wrapper, sgt);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sg_init_one(sgt->sgl, buf, TEST_SIZE);

	/* Init a mock DMA-BUF */
	buf_mock.size = TEST_SIZE;
	attach_mock.dmabuf = &buf_mock;

	gem_obj = drm_gem_shmem_prime_import_sg_table(drm_dev, &attach_mock, sgt);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gem_obj);
	KUNIT_EXPECT_EQ(test, gem_obj->size, TEST_SIZE);
	KUNIT_EXPECT_NULL(test, gem_obj->filp);
	KUNIT_EXPECT_NOT_NULL(test, gem_obj->funcs);

	/* The scatter/gather table will be freed by drm_gem_shmem_free */
	kunit_remove_action(test, sg_free_table_wrapper, sgt);
	kunit_remove_action(test, kfree_wrapper, sgt);

	shmem = to_drm_gem_shmem_obj(gem_obj);
	KUNIT_EXPECT_PTR_EQ(test, shmem->sgt, sgt);

	drm_gem_shmem_free(shmem);
}

/*
 * Test pinning backing pages for a shmem GEM object. The test case
 * succeeds if a suitable number of backing pages are allocated, and
 * the pages table counter attribute is increased by one.
 */
static void drm_gem_shmem_test_pin_pages(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	int i, ret;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);
	KUNIT_EXPECT_NULL(test, shmem->pages);
	KUNIT_EXPECT_EQ(test, shmem->pages_use_count, 0);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_gem_shmem_pin(shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, shmem->pages);
	KUNIT_EXPECT_EQ(test, shmem->pages_use_count, 1);

	for (i = 0; i < (shmem->base.size >> PAGE_SHIFT); i++)
		KUNIT_ASSERT_NOT_NULL(test, shmem->pages[i]);

	drm_gem_shmem_unpin(shmem);
	KUNIT_EXPECT_NULL(test, shmem->pages);
	KUNIT_EXPECT_EQ(test, shmem->pages_use_count, 0);
}

/*
 * Test creating a virtual mapping for a shmem GEM object. The test
 * case succeeds if the backing memory is mapped and the reference
 * counter for virtual mapping is increased by one. Moreover, the test
 * case writes and then reads a test pattern over the mapped memory.
 */
static void drm_gem_shmem_test_vmap(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	struct iosys_map map;
	int ret, i;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);
	KUNIT_EXPECT_NULL(test, shmem->vaddr);
	KUNIT_EXPECT_EQ(test, shmem->vmap_use_count, 0);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_gem_shmem_vmap(shmem, &map);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, shmem->vaddr);
	KUNIT_ASSERT_FALSE(test, iosys_map_is_null(&map));
	KUNIT_EXPECT_EQ(test, shmem->vmap_use_count, 1);

	iosys_map_memset(&map, 0, TEST_BYTE, TEST_SIZE);
	for (i = 0; i < TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, iosys_map_rd(&map, i, u8), TEST_BYTE);

	drm_gem_shmem_vunmap(shmem, &map);
	KUNIT_EXPECT_NULL(test, shmem->vaddr);
	KUNIT_EXPECT_EQ(test, shmem->vmap_use_count, 0);
}

/*
 * Test exporting a scatter/gather table of pinned pages suitable for
 * PRIME usage from a shmem GEM object. The test case succeeds if a
 * scatter/gather table large enough to accommodate the backing memory
 * is successfully exported.
 */
static void drm_gem_shmem_test_get_pages_sgt(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	struct sg_table *sgt;
	struct scatterlist *sg;
	unsigned int si, len = 0;
	int ret;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_gem_shmem_pin(shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sgt = drm_gem_shmem_get_sg_table(shmem);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sgt);
	KUNIT_EXPECT_NULL(test, shmem->sgt);

	ret = kunit_add_action_or_reset(test, sg_free_table_wrapper, sgt);
	KUNIT_ASSERT_EQ(test, ret, 0);

	for_each_sgtable_sg(sgt, sg, si) {
		KUNIT_EXPECT_NOT_NULL(test, sg);
		len += sg->length;
	}

	KUNIT_EXPECT_GE(test, len, TEST_SIZE);
}

/*
 * Test pinning pages and exporting a scatter/gather table suitable for
 * driver usage from a shmem GEM object. The test case succeeds if the
 * backing pages are pinned and a scatter/gather table large enough to
 * accommodate the backing memory is successfully exported.
 */
static void drm_gem_shmem_test_get_sg_table(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	struct sg_table *sgt;
	struct scatterlist *sg;
	unsigned int si, ret, len = 0;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* The scatter/gather table will be freed by drm_gem_shmem_free */
	sgt = drm_gem_shmem_get_pages_sgt(shmem);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sgt);
	KUNIT_ASSERT_NOT_NULL(test, shmem->pages);
	KUNIT_EXPECT_EQ(test, shmem->pages_use_count, 1);
	KUNIT_EXPECT_PTR_EQ(test, sgt, shmem->sgt);

	for_each_sgtable_sg(sgt, sg, si) {
		KUNIT_EXPECT_NOT_NULL(test, sg);
		len += sg->length;
	}

	KUNIT_EXPECT_GE(test, len, TEST_SIZE);
}

/*
 * Test updating the madvise state of a shmem GEM object. The test
 * case checks that the function for setting madv updates it only if
 * its current value is greater or equal than zero and returns false
 * if it has a negative value.
 */
static void drm_gem_shmem_test_madvise(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	int ret;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);
	KUNIT_ASSERT_EQ(test, shmem->madv, 0);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_gem_shmem_madvise(shmem, 1);
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_ASSERT_EQ(test, shmem->madv, 1);

	/* Set madv to a negative value */
	ret = drm_gem_shmem_madvise(shmem, -1);
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_ASSERT_EQ(test, shmem->madv, -1);

	/* Check that madv cannot be set back to a positive value */
	ret = drm_gem_shmem_madvise(shmem, 0);
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_ASSERT_EQ(test, shmem->madv, -1);
}

/*
 * Test purging a shmem GEM object. First, assert that a newly created
 * shmem GEM object is not purgeable. Then, set madvise to a positive
 * value and call drm_gem_shmem_get_pages_sgt() to pin and dma-map the
 * backing pages. Finally, assert that the shmem GEM object is now
 * purgeable and purge it.
 */
static void drm_gem_shmem_test_purge(struct kunit *test)
{
	struct drm_device *drm_dev = test->priv;
	struct drm_gem_shmem_object *shmem;
	struct sg_table *sgt;
	int ret;

	shmem = drm_gem_shmem_create(drm_dev, TEST_SIZE);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, shmem);

	ret = kunit_add_action_or_reset(test, drm_gem_shmem_free_wrapper, shmem);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_gem_shmem_is_purgeable(shmem);
	KUNIT_EXPECT_FALSE(test, ret);

	ret = drm_gem_shmem_madvise(shmem, 1);
	KUNIT_EXPECT_TRUE(test, ret);

	/* The scatter/gather table will be freed by drm_gem_shmem_free */
	sgt = drm_gem_shmem_get_pages_sgt(shmem);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sgt);

	ret = drm_gem_shmem_is_purgeable(shmem);
	KUNIT_EXPECT_TRUE(test, ret);

	drm_gem_shmem_purge(shmem);
	KUNIT_EXPECT_NULL(test, shmem->pages);
	KUNIT_EXPECT_NULL(test, shmem->sgt);
	KUNIT_EXPECT_EQ(test, shmem->madv, -1);
}

static int drm_gem_shmem_test_init(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm_dev;

	/* Allocate a parent device */
	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	/*
	 * The DRM core will automatically initialize the GEM core and create
	 * a DRM Memory Manager object which provides an address space pool
	 * for GEM objects allocation.
	 */
	drm_dev = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*drm_dev),
						      0, DRIVER_GEM);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm_dev);

	test->priv = drm_dev;

	return 0;
}

static struct kunit_case drm_gem_shmem_test_cases[] = {
	KUNIT_CASE(drm_gem_shmem_test_obj_create),
	KUNIT_CASE(drm_gem_shmem_test_obj_create_private),
	KUNIT_CASE(drm_gem_shmem_test_pin_pages),
	KUNIT_CASE(drm_gem_shmem_test_vmap),
	KUNIT_CASE(drm_gem_shmem_test_get_pages_sgt),
	KUNIT_CASE(drm_gem_shmem_test_get_sg_table),
	KUNIT_CASE(drm_gem_shmem_test_madvise),
	KUNIT_CASE(drm_gem_shmem_test_purge),
	{}
};

static struct kunit_suite drm_gem_shmem_suite = {
	.name = "drm_gem_shmem",
	.init = drm_gem_shmem_test_init,
	.test_cases = drm_gem_shmem_test_cases
};

kunit_test_suite(drm_gem_shmem_suite);

MODULE_DESCRIPTION("KUnit test suite for GEM objects backed by shmem buffers");
MODULE_LICENSE("GPL");
