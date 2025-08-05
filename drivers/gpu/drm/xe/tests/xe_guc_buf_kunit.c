// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_guc_ct.h"
#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"

#define DUT_GGTT_START		SZ_1M
#define DUT_GGTT_SIZE		SZ_2M

static struct xe_bo *replacement_xe_managed_bo_create_pin_map(struct xe_device *xe,
							      struct xe_tile *tile,
							      size_t size, u32 flags)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_bo *bo;
	void *buf;

	bo = drmm_kzalloc(&xe->drm, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bo);

	buf = drmm_kzalloc(&xe->drm, size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	bo->tile = tile;
	bo->ttm.bdev = &xe->ttm;
	bo->ttm.base.size = size;
	iosys_map_set_vaddr(&bo->vmap, buf);

	if (flags & XE_BO_FLAG_GGTT) {
		struct xe_ggtt *ggtt = tile->mem.ggtt;

		bo->ggtt_node[tile->id] = xe_ggtt_node_init(ggtt);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bo->ggtt_node[tile->id]);

		KUNIT_ASSERT_EQ(test, 0,
				xe_ggtt_node_insert(bo->ggtt_node[tile->id],
						    xe_bo_size(bo), SZ_4K));
	}

	return bo;
}

static int guc_buf_test_init(struct kunit *test)
{
	struct xe_pci_fake_data fake = {
		.sriov_mode = XE_SRIOV_MODE_PF,
		.platform = XE_TIGERLAKE, /* some random platform */
		.subplatform = XE_SUBPLATFORM_NONE,
	};
	struct xe_ggtt *ggtt;
	struct xe_guc *guc;

	test->priv = &fake;
	xe_kunit_helper_xe_device_test_init(test);

	ggtt = xe_device_get_root_tile(test->priv)->mem.ggtt;
	guc = &xe_device_get_gt(test->priv, 0)->uc.guc;

	KUNIT_ASSERT_EQ(test, 0,
			xe_ggtt_init_kunit(ggtt, DUT_GGTT_START,
					   DUT_GGTT_START + DUT_GGTT_SIZE));

	kunit_activate_static_stub(test, xe_managed_bo_create_pin_map,
				   replacement_xe_managed_bo_create_pin_map);

	KUNIT_ASSERT_EQ(test, 0, xe_guc_buf_cache_init(&guc->buf));

	test->priv = &guc->buf;
	return 0;
}

static void test_smallest(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;

	buf = xe_guc_buf_reserve(cache, 1);
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
	KUNIT_EXPECT_NOT_NULL(test, xe_guc_buf_cpu_ptr(buf));
	KUNIT_EXPECT_NE(test, 0, xe_guc_buf_gpu_addr(buf));
	KUNIT_EXPECT_LE(test, DUT_GGTT_START, xe_guc_buf_gpu_addr(buf));
	KUNIT_EXPECT_GT(test, DUT_GGTT_START + DUT_GGTT_SIZE, xe_guc_buf_gpu_addr(buf));
	xe_guc_buf_release(buf);
}

static void test_largest(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;

	buf = xe_guc_buf_reserve(cache, xe_guc_buf_cache_dwords(cache));
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
	KUNIT_EXPECT_NOT_NULL(test, xe_guc_buf_cpu_ptr(buf));
	KUNIT_EXPECT_NE(test, 0, xe_guc_buf_gpu_addr(buf));
	KUNIT_EXPECT_LE(test, DUT_GGTT_START, xe_guc_buf_gpu_addr(buf));
	KUNIT_EXPECT_GT(test, DUT_GGTT_START + DUT_GGTT_SIZE, xe_guc_buf_gpu_addr(buf));
	xe_guc_buf_release(buf);
}

static void test_granular(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf *bufs;
	int n, dwords;

	dwords = xe_guc_buf_cache_dwords(cache);
	bufs = kunit_kcalloc(test, dwords, sizeof(*bufs), GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, bufs);

	for (n = 0; n < dwords; n++)
		bufs[n] = xe_guc_buf_reserve(cache, 1);

	for (n = 0; n < dwords; n++)
		KUNIT_EXPECT_TRUE_MSG(test, xe_guc_buf_is_valid(bufs[n]), "n=%d", n);

	for (n = 0; n < dwords; n++)
		xe_guc_buf_release(bufs[n]);
}

static void test_unique(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf *bufs;
	int n, m, dwords;

	dwords = xe_guc_buf_cache_dwords(cache);
	bufs = kunit_kcalloc(test, dwords, sizeof(*bufs), GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, bufs);

	for (n = 0; n < dwords; n++)
		bufs[n] = xe_guc_buf_reserve(cache, 1);

	for (n = 0; n < dwords; n++) {
		for (m = n + 1; m < dwords; m++) {
			KUNIT_EXPECT_PTR_NE_MSG(test, xe_guc_buf_cpu_ptr(bufs[n]),
						xe_guc_buf_cpu_ptr(bufs[m]), "n=%d, m=%d", n, m);
			KUNIT_ASSERT_NE_MSG(test, xe_guc_buf_gpu_addr(bufs[n]),
					    xe_guc_buf_gpu_addr(bufs[m]), "n=%d, m=%d", n, m);
		}
	}

	for (n = 0; n < dwords; n++)
		xe_guc_buf_release(bufs[n]);
}

static void test_overlap(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf b1, b2;
	u32 dwords = xe_guc_buf_cache_dwords(cache) / 2;
	u32 bytes = dwords * sizeof(u32);
	void *p1, *p2;
	u64 a1, a2;

	b1 = xe_guc_buf_reserve(cache, dwords);
	b2 = xe_guc_buf_reserve(cache, dwords);

	p1 = xe_guc_buf_cpu_ptr(b1);
	p2 = xe_guc_buf_cpu_ptr(b2);

	a1 = xe_guc_buf_gpu_addr(b1);
	a2 = xe_guc_buf_gpu_addr(b2);

	KUNIT_EXPECT_PTR_NE(test, p1, p2);
	if (p1 < p2)
		KUNIT_EXPECT_LT(test, (uintptr_t)(p1 + bytes - 1), (uintptr_t)p2);
	else
		KUNIT_EXPECT_LT(test, (uintptr_t)(p2 + bytes - 1), (uintptr_t)p1);

	KUNIT_EXPECT_NE(test, a1, a2);
	if (a1 < a2)
		KUNIT_EXPECT_LT(test, a1 + bytes - 1, a2);
	else
		KUNIT_EXPECT_LT(test, a2 + bytes - 1, a1);

	xe_guc_buf_release(b1);
	xe_guc_buf_release(b2);
}

static void test_reusable(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf b1, b2;
	void *p1;
	u64 a1;

	b1 = xe_guc_buf_reserve(cache, xe_guc_buf_cache_dwords(cache));
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(b1));
	KUNIT_EXPECT_NOT_NULL(test, p1 = xe_guc_buf_cpu_ptr(b1));
	KUNIT_EXPECT_NE(test, 0, a1 = xe_guc_buf_gpu_addr(b1));
	xe_guc_buf_release(b1);

	b2 = xe_guc_buf_reserve(cache, xe_guc_buf_cache_dwords(cache));
	KUNIT_EXPECT_PTR_EQ(test, p1, xe_guc_buf_cpu_ptr(b2));
	KUNIT_EXPECT_EQ(test, a1, xe_guc_buf_gpu_addr(b2));
	xe_guc_buf_release(b2);
}

static void test_too_big(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;

	buf = xe_guc_buf_reserve(cache, xe_guc_buf_cache_dwords(cache) + 1);
	KUNIT_EXPECT_FALSE(test, xe_guc_buf_is_valid(buf));
	xe_guc_buf_release(buf); /* shouldn't crash */
}

static void test_flush(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;
	const u32 dwords = xe_guc_buf_cache_dwords(cache);
	const u32 bytes = dwords * sizeof(u32);
	u32 *s, *p, *d;
	int n;

	KUNIT_ASSERT_NOT_NULL(test, s = kunit_kcalloc(test, dwords, sizeof(u32), GFP_KERNEL));
	KUNIT_ASSERT_NOT_NULL(test, d = kunit_kcalloc(test, dwords, sizeof(u32), GFP_KERNEL));

	for (n = 0; n < dwords; n++)
		s[n] = n;

	buf = xe_guc_buf_reserve(cache, dwords);
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
	KUNIT_ASSERT_NOT_NULL(test, p = xe_guc_buf_cpu_ptr(buf));
	KUNIT_EXPECT_PTR_NE(test, p, s);
	KUNIT_EXPECT_PTR_NE(test, p, d);

	memcpy(p, s, bytes);
	KUNIT_EXPECT_NE(test, 0, xe_guc_buf_flush(buf));

	iosys_map_memcpy_from(d, &cache->sam->bo->vmap, 0, bytes);
	KUNIT_EXPECT_MEMEQ(test, s, d, bytes);

	xe_guc_buf_release(buf);
}

static void test_lookup(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;
	u32 dwords;
	u64 addr;
	u32 *p;
	int n;

	dwords = xe_guc_buf_cache_dwords(cache);
	buf = xe_guc_buf_reserve(cache, dwords);
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
	KUNIT_ASSERT_NOT_NULL(test, p = xe_guc_buf_cpu_ptr(buf));
	KUNIT_ASSERT_NE(test, 0, addr = xe_guc_buf_gpu_addr(buf));

	KUNIT_EXPECT_EQ(test, 0, xe_guc_cache_gpu_addr_from_ptr(cache, p - 1, sizeof(u32)));
	KUNIT_EXPECT_EQ(test, 0, xe_guc_cache_gpu_addr_from_ptr(cache, p + dwords, sizeof(u32)));

	for (n = 0; n < dwords; n++)
		KUNIT_EXPECT_EQ_MSG(test, xe_guc_cache_gpu_addr_from_ptr(cache, p + n, sizeof(u32)),
				    addr + n * sizeof(u32), "n=%d", n);

	xe_guc_buf_release(buf);
}

static void test_data(struct kunit *test)
{
	static const u32 data[] = { 1, 2, 3, 4, 5, 6 };
	struct xe_guc_buf_cache *cache = test->priv;
	struct xe_guc_buf buf;
	void *p;

	buf = xe_guc_buf_from_data(cache, data, sizeof(data));
	KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
	KUNIT_ASSERT_NOT_NULL(test, p = xe_guc_buf_cpu_ptr(buf));
	KUNIT_EXPECT_MEMEQ(test, p, data, sizeof(data));

	xe_guc_buf_release(buf);
}

static void test_class(struct kunit *test)
{
	struct xe_guc_buf_cache *cache = test->priv;
	u32 dwords = xe_guc_buf_cache_dwords(cache);

	{
		CLASS(xe_guc_buf, buf)(cache, dwords);
		KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
		KUNIT_EXPECT_NOT_NULL(test, xe_guc_buf_cpu_ptr(buf));
		KUNIT_EXPECT_NE(test, 0, xe_guc_buf_gpu_addr(buf));
		KUNIT_EXPECT_LE(test, DUT_GGTT_START, xe_guc_buf_gpu_addr(buf));
		KUNIT_EXPECT_GT(test, DUT_GGTT_START + DUT_GGTT_SIZE, xe_guc_buf_gpu_addr(buf));
	}

	{
		CLASS(xe_guc_buf, buf)(cache, dwords);
		KUNIT_ASSERT_TRUE(test, xe_guc_buf_is_valid(buf));
		KUNIT_EXPECT_NOT_NULL(test, xe_guc_buf_cpu_ptr(buf));
		KUNIT_EXPECT_NE(test, 0, xe_guc_buf_gpu_addr(buf));
		KUNIT_EXPECT_LE(test, DUT_GGTT_START, xe_guc_buf_gpu_addr(buf));
		KUNIT_EXPECT_GT(test, DUT_GGTT_START + DUT_GGTT_SIZE, xe_guc_buf_gpu_addr(buf));
	}
}

static struct kunit_case guc_buf_test_cases[] = {
	KUNIT_CASE(test_smallest),
	KUNIT_CASE(test_largest),
	KUNIT_CASE(test_granular),
	KUNIT_CASE(test_unique),
	KUNIT_CASE(test_overlap),
	KUNIT_CASE(test_reusable),
	KUNIT_CASE(test_too_big),
	KUNIT_CASE(test_flush),
	KUNIT_CASE(test_lookup),
	KUNIT_CASE(test_data),
	KUNIT_CASE(test_class),
	{}
};

static struct kunit_suite guc_buf_suite = {
	.name = "guc_buf",
	.test_cases = guc_buf_test_cases,
	.init = guc_buf_test_init,
};

kunit_test_suites(&guc_buf_suite);
