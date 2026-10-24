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

/*
 * A plain drm_device (not an amdgpu_device) is enough: building the pipeline
 * only needs the DRM mode-config infrastructure. The pipeline is destroyed
 * before the DRM device so the colorops are freed while the device is valid.
 */
static struct drm_plane *dm_colorop_alloc_plane(struct kunit *test, struct drm_device **drm)
{
	struct drm_plane *plane;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	*drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(**drm), 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, *drm);

	plane = drm_kunit_helper_create_primary_plane(test, *drm, NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	kunit_add_action(test, kunit_colorop_pipeline_destroy, *drm);

	return plane;
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
	struct drm_device *drm;
	struct drm_plane *plane;
	struct drm_prop_enum_list list = {};
	int ret;

	plane = dm_colorop_alloc_plane(test, &drm);

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

/* Failure paths of amdgpu_dm_build_default_pipeline() */

/*
 * The DRM colorop constructors cannot be made to fail through their arguments,
 * so failures are injected with a fake allocator and with spies that delegate
 * to the real constructors and override the return value of one call.
 */
struct dm_colorop_fault {
	int alloc_fail_at;	/* allocation index returning NULL, -1 to disable */
	int init_fail_at;	/* init call index returning an error, -1 to disable */
	int allocs;
	int inits;
};

static struct dm_colorop_fault dm_colorop_fault;

static struct drm_colorop *dm_colorop_test_alloc(void)
{
	if (dm_colorop_fault.allocs++ == dm_colorop_fault.alloc_fail_at)
		return NULL;

	return kzalloc_obj(struct drm_colorop);
}

static int dm_colorop_test_init_ret(int ret)
{
	if (ret)
		return ret;

	if (dm_colorop_fault.inits++ == dm_colorop_fault.init_fail_at)
		return -EINVAL;

	return 0;
}

static int dm_colorop_test_curve_1d_init(struct drm_device *dev, struct drm_colorop *colorop,
					 struct drm_plane *plane,
					 const struct drm_colorop_funcs *funcs,
					 u64 supported_tfs, uint32_t flags)
{
	return dm_colorop_test_init_ret(drm_plane_colorop_curve_1d_init(dev, colorop, plane,
									funcs, supported_tfs,
									flags));
}

static int dm_colorop_test_1d_lut_init(struct drm_device *dev, struct drm_colorop *colorop,
				       struct drm_plane *plane,
				       const struct drm_colorop_funcs *funcs,
				       uint32_t lut_size,
				       enum drm_colorop_lut1d_interpolation_type interpolation,
				       uint32_t flags)
{
	return dm_colorop_test_init_ret(drm_plane_colorop_curve_1d_lut_init(dev, colorop, plane,
									    funcs, lut_size,
									    interpolation,
									    flags));
}

static int dm_colorop_test_ctm_3x4_init(struct drm_device *dev, struct drm_colorop *colorop,
					struct drm_plane *plane,
					const struct drm_colorop_funcs *funcs, uint32_t flags)
{
	return dm_colorop_test_init_ret(drm_plane_colorop_ctm_3x4_init(dev, colorop, plane,
								       funcs, flags));
}

static int dm_colorop_test_mult_init(struct drm_device *dev, struct drm_colorop *colorop,
				     struct drm_plane *plane,
				     const struct drm_colorop_funcs *funcs, uint32_t flags)
{
	return dm_colorop_test_init_ret(drm_plane_colorop_mult_init(dev, colorop, plane,
								    funcs, flags));
}

static int dm_colorop_test_3dlut_init(struct drm_device *dev, struct drm_colorop *colorop,
				      struct drm_plane *plane,
				      const struct drm_colorop_funcs *funcs, uint32_t lut_size,
				      enum drm_colorop_lut3d_interpolation_type interpolation,
				      uint32_t flags)
{
	return dm_colorop_test_init_ret(drm_plane_colorop_3dlut_init(dev, colorop, plane, funcs,
								     lut_size, interpolation,
								     flags));
}

static const struct amdgpu_dm_colorop_kunit_ops dm_colorop_test_ops = {
	.colorop_kzalloc_obj = dm_colorop_test_alloc,
	.curve_1d_init = dm_colorop_test_curve_1d_init,
	.curve_1d_lut_init = dm_colorop_test_1d_lut_init,
	.ctm_3x4_init = dm_colorop_test_ctm_3x4_init,
	.mult_init = dm_colorop_test_mult_init,
	.lut3d_init = dm_colorop_test_3dlut_init,
};

static void dm_colorop_test_reset_ops(void *unused)
{
	amdgpu_dm_colorop_kunit_set_ops(NULL);
}

/*
 * Build the full (3D LUT) pipeline with one injected failure and return the
 * error reported by amdgpu_dm_build_default_pipeline().
 */
static int dm_colorop_build_with_fault(struct kunit *test, struct drm_device **drm,
				       int alloc_fail_at, int init_fail_at)
{
	struct drm_prop_enum_list list = {};
	struct drm_plane *plane;
	int ret;

	plane = dm_colorop_alloc_plane(test, drm);

	dm_colorop_fault = (struct dm_colorop_fault){
		.alloc_fail_at = alloc_fail_at,
		.init_fail_at = init_fail_at,
	};

	amdgpu_dm_colorop_kunit_set_ops(&dm_colorop_test_ops);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_colorop_test_reset_ops, NULL), 0);

	ret = amdgpu_dm_build_default_pipeline(*drm, plane, true, &list);
	kfree(list.name);

	return ret;
}

struct dm_colorop_step_param {
	const char *name;
	int index;
};

/* Pipeline steps in creation order when the 3D LUT block is built. */
static const struct dm_colorop_step_param dm_colorop_step_params[] = {
	{ "degam_tf",	0 },
	{ "multiplier",	1 },
	{ "ctm_3x4",	2 },
	{ "shaper_tf",	3 },
	{ "shaper_lut",	4 },
	{ "lut3d",	5 },
	{ "blnd_tf",	6 },
	{ "blnd_lut",	7 },
};

KUNIT_ARRAY_PARAM_DESC(dm_colorop_step, dm_colorop_step_params, name);

/**
 * dm_test_build_pipeline_alloc_fail() - Verify a failed alloc returns -ENOMEM and cleans up.
 * @test: KUnit test context.
 */
static void dm_test_build_pipeline_alloc_fail(struct kunit *test)
{
	const struct dm_colorop_step_param *param = test->param_value;
	struct drm_device *drm;
	int ret;

	ret = dm_colorop_build_with_fault(test, &drm, param->index, -1);

	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	KUNIT_EXPECT_EQ(test, dm_colorop_fault.allocs, param->index + 1);
	KUNIT_EXPECT_EQ(test, dm_colorop_fault.inits, param->index);
	KUNIT_EXPECT_EQ(test, drm->mode_config.num_colorop, 0);
}

/**
 * dm_test_build_pipeline_init_fail() - Verify a failed init propagates the error and cleans up.
 * @test: KUnit test context.
 */
static void dm_test_build_pipeline_init_fail(struct kunit *test)
{
	const struct dm_colorop_step_param *param = test->param_value;
	struct drm_device *drm;
	int ret;

	ret = dm_colorop_build_with_fault(test, &drm, -1, param->index);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, dm_colorop_fault.allocs, param->index + 1);
	KUNIT_EXPECT_EQ(test, dm_colorop_fault.inits, param->index + 1);
	KUNIT_EXPECT_EQ(test, drm->mode_config.num_colorop, 0);
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
	/* amdgpu_dm_build_default_pipeline failure paths */
	KUNIT_CASE_PARAM(dm_test_build_pipeline_alloc_fail, dm_colorop_step_gen_params),
	KUNIT_CASE_PARAM(dm_test_build_pipeline_init_fail, dm_colorop_step_gen_params),
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
