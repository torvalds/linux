// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_colorop.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_colorop.h>
#include <drm/drm_kunit_helpers.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm_colorop.h"
#include "amdgpu_dm_kunit_test_helpers.h"

/* Tests for amdgpu_dm_supported_degam_tfs */

static void dm_test_supported_degam_tfs_has_srgb_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_degam_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF));
}

static void dm_test_supported_degam_tfs_has_pq125_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_degam_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF));
}

static void dm_test_supported_degam_tfs_has_bt2020_inv_oetf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_degam_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF));
}

static void dm_test_supported_degam_tfs_has_gamma22(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_degam_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_GAMMA22));
}

static void dm_test_supported_degam_tfs_no_extra_bits(struct kunit *test)
{
	u64 expected = BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
		       BIT(DRM_COLOROP_1D_CURVE_GAMMA22);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_supported_degam_tfs, expected);
}

/* Tests for amdgpu_dm_supported_shaper_tfs */

static void dm_test_supported_shaper_tfs_has_srgb_inv_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_shaper_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF));
}

static void dm_test_supported_shaper_tfs_has_pq125_inv_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_shaper_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF));
}

static void dm_test_supported_shaper_tfs_has_bt2020_oetf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_shaper_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_BT2020_OETF));
}

static void dm_test_supported_shaper_tfs_has_gamma22_inv(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_shaper_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_GAMMA22_INV));
}

static void dm_test_supported_shaper_tfs_no_extra_bits(struct kunit *test)
{
	u64 expected = BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_BT2020_OETF) |
		       BIT(DRM_COLOROP_1D_CURVE_GAMMA22_INV);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_supported_shaper_tfs, expected);
}

/* Tests for amdgpu_dm_supported_blnd_tfs */

static void dm_test_supported_blnd_tfs_has_srgb_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_blnd_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF));
}

static void dm_test_supported_blnd_tfs_has_pq125_eotf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_blnd_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF));
}

static void dm_test_supported_blnd_tfs_has_bt2020_inv_oetf(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_blnd_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF));
}

static void dm_test_supported_blnd_tfs_has_gamma22(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_supported_blnd_tfs &
			  BIT(DRM_COLOROP_1D_CURVE_GAMMA22));
}

static void dm_test_supported_blnd_tfs_no_extra_bits(struct kunit *test)
{
	u64 expected = BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
		       BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
		       BIT(DRM_COLOROP_1D_CURVE_GAMMA22);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_supported_blnd_tfs, expected);
}

/* degam and blnd should support the same set of EOTF curves */
static void dm_test_degam_and_blnd_tfs_match(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, amdgpu_dm_supported_degam_tfs,
			amdgpu_dm_supported_blnd_tfs);
}

/* Tests for amdgpu_dm_initialize_default_pipeline */

static void kunit_colorop_pipeline_destroy(void *drm)
{
	drm_colorop_pipeline_destroy((struct drm_device *)drm);
}

static void dm_expect_colorop_pipeline(struct kunit *test, struct drm_device *drm,
				       const struct drm_prop_enum_list *list,
				       const enum drm_colorop_type *expected,
				       int expected_count)
{
	struct drm_colorop *op, *first = NULL;
	int i = 0;

	drm_for_each_colorop(op, drm) {
		if (op->base.id == (uint32_t)list->type) {
			first = op;
			break;
		}
	}
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, first);

	for (op = first; op; op = op->next, i++) {
		KUNIT_ASSERT_LT(test, i, expected_count);
		KUNIT_EXPECT_EQ(test, op->type, expected[i]);
		KUNIT_EXPECT_NOT_NULL(test, op->bypass_property);
	}
	KUNIT_EXPECT_EQ(test, i, expected_count);
}

/**
 * dm_test_initialize_default_pipeline() - Verify amdgpu_dm_build_default_pipeline()
 *   produces the expected colorop chain with all ops bypassable.
 * @test: KUnit test context.
 */
static void dm_test_initialize_default_pipeline(struct kunit *test)
{
	static const enum drm_colorop_type expected[] = {
		DRM_COLOROP_1D_CURVE,	/* degam TF */
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,	/* shaper TF */
		DRM_COLOROP_1D_LUT,	/* shaper LUT */
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,	/* blnd TF */
		DRM_COLOROP_1D_LUT,	/* blnd LUT */
	};
	struct device *dev;
	struct drm_device *drm;
	struct drm_plane *plane;
	struct drm_prop_enum_list list = {};
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	/*
	 * Allocate a plain drm_device (not an amdgpu_device) — sufficient
	 * because amdgpu_dm_build_default_pipeline() only needs the DRM
	 * mode-config infrastructure, not the amdgpu device wrapper.
	 */
	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	plane = drm_kunit_helper_create_primary_plane(test, drm,
						       NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	/*
	 * Destroy the pipeline before the DRM device is cleaned up so
	 * that the colorop objects (kzalloc'd inside the function) are
	 * freed while the device is still valid.
	 */
	kunit_add_action(test, kunit_colorop_pipeline_destroy, drm);

	ret = amdgpu_dm_build_default_pipeline(drm, plane, true, &list);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kfree(list.name);

	dm_expect_colorop_pipeline(test, drm, &list, expected, ARRAY_SIZE(expected));
}

static void dm_test_initialize_default_pipeline_caps(struct kunit *test,
					     bool dpp_hw_3d_lut,
					     bool mpc_preblend,
					     const enum drm_colorop_type *expected,
					     int expected_count)
{
	struct drm_prop_enum_list list = {};
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct drm_plane *plane;
	struct dc *dc;
	int ret;

	adev = dm_kunit_alloc_adev(test);
	drm = &adev->ddev;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;
	adev->dm.dc->caps.color.dpp.hw_3d_lut = dpp_hw_3d_lut;
	adev->dm.dc->caps.color.mpc.preblend = mpc_preblend;

	plane = drm_kunit_helper_create_primary_plane(test, drm,
						       NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	kunit_add_action(test, kunit_colorop_pipeline_destroy, drm);

	ret = amdgpu_dm_initialize_default_pipeline(plane, &list);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kfree(list.name);

	dm_expect_colorop_pipeline(test, drm, &list, expected, expected_count);
}

/**
 * dm_test_initialize_default_pipeline_dpp_3d_lut() - Test DPP 3D LUT cap.
 * @test: KUnit test context.
 */
static void dm_test_initialize_default_pipeline_dpp_3d_lut(struct kunit *test)
{
	static const enum drm_colorop_type expected[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};

	dm_test_initialize_default_pipeline_caps(test, true, false,
						 expected, ARRAY_SIZE(expected));
}

/**
 * dm_test_initialize_default_pipeline_mpc_preblend() - Test MPC preblend cap.
 * @test: KUnit test context.
 */
static void dm_test_initialize_default_pipeline_mpc_preblend(struct kunit *test)
{
	static const enum drm_colorop_type expected[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};

	dm_test_initialize_default_pipeline_caps(test, false, true,
						 expected, ARRAY_SIZE(expected));
}

/**
 * dm_test_initialize_default_pipeline_no_3d_lut() - Test no 3D LUT caps.
 * @test: KUnit test context.
 */
static void dm_test_initialize_default_pipeline_no_3d_lut(struct kunit *test)
{
	static const enum drm_colorop_type expected[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};

	dm_test_initialize_default_pipeline_caps(test, false, false,
						 expected, ARRAY_SIZE(expected));
}

static struct kunit_case dm_colorop_test_cases[] = {
	/* degam TFs */
	KUNIT_CASE(dm_test_supported_degam_tfs_has_srgb_eotf),
	KUNIT_CASE(dm_test_supported_degam_tfs_has_pq125_eotf),
	KUNIT_CASE(dm_test_supported_degam_tfs_has_bt2020_inv_oetf),
	KUNIT_CASE(dm_test_supported_degam_tfs_has_gamma22),
	KUNIT_CASE(dm_test_supported_degam_tfs_no_extra_bits),
	/* shaper TFs */
	KUNIT_CASE(dm_test_supported_shaper_tfs_has_srgb_inv_eotf),
	KUNIT_CASE(dm_test_supported_shaper_tfs_has_pq125_inv_eotf),
	KUNIT_CASE(dm_test_supported_shaper_tfs_has_bt2020_oetf),
	KUNIT_CASE(dm_test_supported_shaper_tfs_has_gamma22_inv),
	KUNIT_CASE(dm_test_supported_shaper_tfs_no_extra_bits),
	/* blnd TFs */
	KUNIT_CASE(dm_test_supported_blnd_tfs_has_srgb_eotf),
	KUNIT_CASE(dm_test_supported_blnd_tfs_has_pq125_eotf),
	KUNIT_CASE(dm_test_supported_blnd_tfs_has_bt2020_inv_oetf),
	KUNIT_CASE(dm_test_supported_blnd_tfs_has_gamma22),
	KUNIT_CASE(dm_test_supported_blnd_tfs_no_extra_bits),
	/* cross-check */
	KUNIT_CASE(dm_test_degam_and_blnd_tfs_match),
	/* amdgpu_dm_initialize_default_pipeline */
	KUNIT_CASE(dm_test_initialize_default_pipeline),
	KUNIT_CASE(dm_test_initialize_default_pipeline_dpp_3d_lut),
	KUNIT_CASE(dm_test_initialize_default_pipeline_mpc_preblend),
	KUNIT_CASE(dm_test_initialize_default_pipeline_no_3d_lut),
	{}
};

static struct kunit_suite dm_colorop_test_suite = {
	.name = "amdgpu_dm_colorop",
	.test_cases = dm_colorop_test_cases,
};

kunit_test_suite(dm_colorop_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_colorop");
MODULE_AUTHOR("AMD");
