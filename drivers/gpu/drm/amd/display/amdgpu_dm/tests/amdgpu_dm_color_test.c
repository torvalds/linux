// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_color.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/types.h>
#include <drm/drm_atomic.h>
#include <drm/drm_colorop.h>
#include <drm/drm_property.h>
#include <uapi/drm/drm_mode.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_color.h"
#include "amdgpu_dm_kunit_test_helpers.h"

/* ---- Tests for amdgpu_dm_fixpt_from_s3132 ---- */

static void dm_test_fixpt_from_s3132_zero(struct kunit *test)
{
	struct fixed31_32 val = amdgpu_dm_fixpt_from_s3132(0ULL);

	KUNIT_EXPECT_EQ(test, val.value, 0LL);
}

static void dm_test_fixpt_from_s3132_one(struct kunit *test)
{
	/* 1.0 in S31.32 signed-magnitude = 1ULL << 32 */
	struct fixed31_32 val = amdgpu_dm_fixpt_from_s3132(1ULL << 32);

	KUNIT_EXPECT_EQ(test, val.value, (long long)(1ULL << 32));
}

static void dm_test_fixpt_from_s3132_negative_one(struct kunit *test)
{
	/* -1.0 in S31.32 signed-magnitude: sign bit set | magnitude 1<<32 */
	__u64 neg_one = (1ULL << 63) | (1ULL << 32);
	struct fixed31_32 val = amdgpu_dm_fixpt_from_s3132(neg_one);

	/* 2's complement of 1.0 is -(1<<32) */
	KUNIT_EXPECT_EQ(test, val.value, -(long long)(1ULL << 32));
}

static void dm_test_fixpt_from_s3132_half(struct kunit *test)
{
	/* 0.5 in S31.32 = 1ULL << 31 */
	struct fixed31_32 val = amdgpu_dm_fixpt_from_s3132(1ULL << 31);

	KUNIT_EXPECT_EQ(test, val.value, (long long)(1ULL << 31));
}

static void dm_test_fixpt_from_s3132_neg_half(struct kunit *test)
{
	__u64 neg_half = (1ULL << 63) | (1ULL << 31);
	struct fixed31_32 val = amdgpu_dm_fixpt_from_s3132(neg_half);

	KUNIT_EXPECT_EQ(test, val.value, -(long long)(1ULL << 31));
}

/* ---- Tests for __is_lut_linear ---- */

static void dm_test_is_lut_linear_with_linear_lut(struct kunit *test)
{
	const uint32_t size = 256;
	struct drm_color_lut *lut;
	int i;

	lut = kunit_kcalloc(test, size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, lut);

	for (i = 0; i < size; i++) {
		uint16_t val = (uint16_t)(i * MAX_DRM_LUT_VALUE / (size - 1));

		lut[i].red = val;
		lut[i].green = val;
		lut[i].blue = val;
	}

	KUNIT_EXPECT_TRUE(test, __is_lut_linear(lut, size));
}

static void dm_test_is_lut_linear_with_nonlinear_lut(struct kunit *test)
{
	const uint32_t size = 256;
	struct drm_color_lut *lut;
	int i;

	lut = kunit_kcalloc(test, size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, lut);

	/* Fill with all-max values: clearly non-linear */
	for (i = 0; i < size; i++) {
		lut[i].red = MAX_DRM_LUT_VALUE;
		lut[i].green = MAX_DRM_LUT_VALUE;
		lut[i].blue = MAX_DRM_LUT_VALUE;
	}

	KUNIT_EXPECT_FALSE(test, __is_lut_linear(lut, size));
}

static void dm_test_is_lut_linear_rgb_mismatch(struct kunit *test)
{
	const uint32_t size = 256;
	struct drm_color_lut *lut;
	int i;

	lut = kunit_kcalloc(test, size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, lut);

	for (i = 0; i < size; i++) {
		uint16_t val = (uint16_t)(i * MAX_DRM_LUT_VALUE / (size - 1));

		lut[i].red = val;
		lut[i].green = val;
		lut[i].blue = val;
	}

	/* Introduce R/G mismatch at entry 128 */
	lut[128].red = lut[128].green + 10;

	KUNIT_EXPECT_FALSE(test, __is_lut_linear(lut, size));
}

/* ---- Tests for __drm_ctm_to_dc_matrix ---- */

static void dm_test_drm_ctm_to_dc_matrix_identity(struct kunit *test)
{
	struct drm_color_ctm ctm;
	struct fixed31_32 matrix[12];
	int i;
	long long one = 1LL << 32;

	memset(&ctm, 0, sizeof(ctm));

	/* Identity 3x3 in S31.32 signed-magnitude: diagonal = 1.0 */
	ctm.matrix[0] = 1ULL << 32; /* [0][0] */
	ctm.matrix[4] = 1ULL << 32; /* [1][1] */
	ctm.matrix[8] = 1ULL << 32; /* [2][2] */

	__drm_ctm_to_dc_matrix(&ctm, matrix);

	/* Expect 3x4 identity: diag=1.0, 4th column=0, off-diag=0 */
	for (i = 0; i < 12; i++) {
		if (i == 0 || i == 5 || i == 10)
			KUNIT_EXPECT_EQ(test, matrix[i].value, one);
		else
			KUNIT_EXPECT_EQ(test, matrix[i].value, 0LL);
	}
}

static void dm_test_drm_ctm_to_dc_matrix_negative(struct kunit *test)
{
	struct drm_color_ctm ctm;
	struct fixed31_32 matrix[12];
	long long neg_one = -(1LL << 32);

	memset(&ctm, 0, sizeof(ctm));

	/* -1.0 in S31.32 signed-magnitude */
	ctm.matrix[0] = (1ULL << 63) | (1ULL << 32);

	__drm_ctm_to_dc_matrix(&ctm, matrix);

	KUNIT_EXPECT_EQ(test, matrix[0].value, neg_one);
}

static void dm_test_drm_ctm_to_dc_matrix_4th_col_zero(struct kunit *test)
{
	struct drm_color_ctm ctm;
	struct fixed31_32 matrix[12];

	memset(&ctm, 0, sizeof(ctm));

	/* Fill all 9 CTM entries with 1.0 */
	for (int i = 0; i < 9; i++)
		ctm.matrix[i] = 1ULL << 32;

	__drm_ctm_to_dc_matrix(&ctm, matrix);

	/* 4th column (indices 3, 7, 11) must always be zero */
	KUNIT_EXPECT_EQ(test, matrix[3].value, 0LL);
	KUNIT_EXPECT_EQ(test, matrix[7].value, 0LL);
	KUNIT_EXPECT_EQ(test, matrix[11].value, 0LL);
}

/* ---- Tests for __drm_ctm_3x4_to_dc_matrix ---- */

static void dm_test_drm_ctm_3x4_to_dc_matrix_identity(struct kunit *test)
{
	struct drm_color_ctm_3x4 ctm;
	struct fixed31_32 matrix[12];
	int i;
	long long one = 1LL << 32;

	memset(&ctm, 0, sizeof(ctm));

	/* Identity with offsets in 4th column */
	ctm.matrix[0] = 1ULL << 32;  /* [0][0] */
	ctm.matrix[5] = 1ULL << 32;  /* [1][1] */
	ctm.matrix[10] = 1ULL << 32; /* [2][2] */

	__drm_ctm_3x4_to_dc_matrix(&ctm, matrix);

	for (i = 0; i < 12; i++) {
		if (i == 0 || i == 5 || i == 10)
			KUNIT_EXPECT_EQ(test, matrix[i].value, one);
		else
			KUNIT_EXPECT_EQ(test, matrix[i].value, 0LL);
	}
}

static void dm_test_drm_ctm_3x4_to_dc_matrix_offset(struct kunit *test)
{
	struct drm_color_ctm_3x4 ctm;
	struct fixed31_32 matrix[12];
	long long half = 1LL << 31;

	memset(&ctm, 0, sizeof(ctm));

	/* Set 4th column (offsets) to 0.5 */
	ctm.matrix[3] = 1ULL << 31;
	ctm.matrix[7] = 1ULL << 31;
	ctm.matrix[11] = 1ULL << 31;

	__drm_ctm_3x4_to_dc_matrix(&ctm, matrix);

	KUNIT_EXPECT_EQ(test, matrix[3].value, half);
	KUNIT_EXPECT_EQ(test, matrix[7].value, half);
	KUNIT_EXPECT_EQ(test, matrix[11].value, half);
}

/* ---- Tests for amdgpu_tf_to_dc_tf ---- */

static void dm_test_tf_to_dc_tf_default(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_DEFAULT),
			(int)TRANSFER_FUNCTION_LINEAR);
}

static void dm_test_tf_to_dc_tf_identity(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_IDENTITY),
			(int)TRANSFER_FUNCTION_LINEAR);
}

static void dm_test_tf_to_dc_tf_srgb(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_SRGB_EOTF),
			(int)TRANSFER_FUNCTION_SRGB);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_SRGB_INV_EOTF),
			(int)TRANSFER_FUNCTION_SRGB);
}

static void dm_test_tf_to_dc_tf_bt709(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_BT709_OETF),
			(int)TRANSFER_FUNCTION_BT709);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_BT709_INV_OETF),
			(int)TRANSFER_FUNCTION_BT709);
}

static void dm_test_tf_to_dc_tf_pq(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_PQ_EOTF),
			(int)TRANSFER_FUNCTION_PQ);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_PQ_INV_EOTF),
			(int)TRANSFER_FUNCTION_PQ);
}

static void dm_test_tf_to_dc_tf_gamma22(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA22_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA22);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA22_INV_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA22);
}

static void dm_test_tf_to_dc_tf_gamma24(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA24_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA24);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA24_INV_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA24);
}

static void dm_test_tf_to_dc_tf_gamma26(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA26_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA26);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_tf_to_dc_tf(AMDGPU_TRANSFER_FUNCTION_GAMMA26_INV_EOTF),
			(int)TRANSFER_FUNCTION_GAMMA26);
}

/* ---- Tests for amdgpu_colorop_tf_to_dc_tf ---- */

static void dm_test_colorop_tf_to_dc_tf_srgb(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_SRGB_EOTF),
			(int)TRANSFER_FUNCTION_SRGB);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF),
			(int)TRANSFER_FUNCTION_SRGB);
}

static void dm_test_colorop_tf_to_dc_tf_pq(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_PQ_125_EOTF),
			(int)TRANSFER_FUNCTION_PQ);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF),
			(int)TRANSFER_FUNCTION_PQ);
}

static void dm_test_colorop_tf_to_dc_tf_bt2020(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF),
			(int)TRANSFER_FUNCTION_BT709);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_BT2020_OETF),
			(int)TRANSFER_FUNCTION_BT709);
}

static void dm_test_colorop_tf_to_dc_tf_gamma22(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_GAMMA22),
			(int)TRANSFER_FUNCTION_GAMMA22);
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_GAMMA22_INV),
			(int)TRANSFER_FUNCTION_GAMMA22);
}

static void dm_test_colorop_tf_to_dc_tf_default(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_colorop_tf_to_dc_tf(DRM_COLOROP_1D_CURVE_COUNT),
			(int)TRANSFER_FUNCTION_LINEAR);
}

/* ---- Tests for __drm_lut_to_dc_gamma (legacy path) ---- */

static void dm_test_drm_lut_to_dc_gamma_legacy_zero(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_gamma *gamma;

	lut = kunit_kcalloc(test, MAX_COLOR_LEGACY_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	__drm_lut_to_dc_gamma(lut, gamma, true);

	/* All-zero LUT should produce all-zero gamma entries */
	for (int i = 0; i < MAX_COLOR_LEGACY_LUT_ENTRIES; i++) {
		KUNIT_EXPECT_EQ(test, gamma->entries.red[i].value, 0LL);
		KUNIT_EXPECT_EQ(test, gamma->entries.green[i].value, 0LL);
		KUNIT_EXPECT_EQ(test, gamma->entries.blue[i].value, 0LL);
	}
}

static void dm_test_drm_lut_to_dc_gamma_legacy_max(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_gamma *gamma;
	long long expected = (long long)MAX_DRM_LUT_VALUE << 32;

	lut = kunit_kcalloc(test, MAX_COLOR_LEGACY_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	/* Set first and last entries to max */
	lut[0].red = MAX_DRM_LUT_VALUE;
	lut[0].green = MAX_DRM_LUT_VALUE;
	lut[0].blue = MAX_DRM_LUT_VALUE;
	lut[MAX_COLOR_LEGACY_LUT_ENTRIES - 1].red = MAX_DRM_LUT_VALUE;
	lut[MAX_COLOR_LEGACY_LUT_ENTRIES - 1].green = MAX_DRM_LUT_VALUE;
	lut[MAX_COLOR_LEGACY_LUT_ENTRIES - 1].blue = MAX_DRM_LUT_VALUE;

	__drm_lut_to_dc_gamma(lut, gamma, true);

	/* Legacy uses dc_fixpt_from_int(val) = val << 32 */
	KUNIT_EXPECT_EQ(test, gamma->entries.red[0].value, expected);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[0].value, expected);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[0].value, expected);
	KUNIT_EXPECT_EQ(test, gamma->entries.red[MAX_COLOR_LEGACY_LUT_ENTRIES - 1].value,
			expected);
}

static void dm_test_drm_lut_to_dc_gamma_legacy_channels(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_gamma *gamma;

	lut = kunit_kcalloc(test, MAX_COLOR_LEGACY_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	/* Set distinct values per channel at index 1 */
	lut[1].red = 100;
	lut[1].green = 200;
	lut[1].blue = 300;

	__drm_lut_to_dc_gamma(lut, gamma, true);

	KUNIT_EXPECT_EQ(test, gamma->entries.red[1].value, 100LL << 32);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[1].value, 200LL << 32);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[1].value, 300LL << 32);
}

/* ---- Tests for __drm_lut_to_dc_gamma (non-legacy path) ---- */

static void dm_test_drm_lut_to_dc_gamma_nonlegacy_zero(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_gamma *gamma;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	__drm_lut_to_dc_gamma(lut, gamma, false);

	/* All-zero LUT → fraction(0, 0xFFFF) = 0 */
	KUNIT_EXPECT_EQ(test, gamma->entries.red[0].value, 0LL);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[0].value, 0LL);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[0].value, 0LL);
}

static void dm_test_drm_lut_to_dc_gamma_nonlegacy_max(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_gamma *gamma;
	long long one = 1LL << 32;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	/* Max LUT value should map to 1.0 in fixed-point */
	lut[0].red = MAX_DRM_LUT_VALUE;
	lut[0].green = MAX_DRM_LUT_VALUE;
	lut[0].blue = MAX_DRM_LUT_VALUE;

	__drm_lut_to_dc_gamma(lut, gamma, false);

	/* dc_fixpt_from_fraction(0xFFFF, 0xFFFF) = 1.0 */
	KUNIT_EXPECT_EQ(test, gamma->entries.red[0].value, one);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[0].value, one);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[0].value, one);
}

/* ---- Tests for __drm_lut32_to_dc_gamma ---- */

static void dm_test_drm_lut32_to_dc_gamma_zero(struct kunit *test)
{
	struct drm_color_lut32 *lut;
	struct dc_gamma *gamma;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	__drm_lut32_to_dc_gamma(lut, gamma);

	/* All-zero LUT → fraction(0, 0xFFFFFFFF) = 0 */
	KUNIT_EXPECT_EQ(test, gamma->entries.red[0].value, 0LL);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[0].value, 0LL);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[0].value, 0LL);
}

static void dm_test_drm_lut32_to_dc_gamma_max(struct kunit *test)
{
	struct drm_color_lut32 *lut;
	struct dc_gamma *gamma;
	long long one = 1LL << 32;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	/* Max 32-bit LUT value should map to 1.0 in fixed-point */
	lut[0].red = MAX_DRM_LUT32_VALUE;
	lut[0].green = MAX_DRM_LUT32_VALUE;
	lut[0].blue = MAX_DRM_LUT32_VALUE;

	__drm_lut32_to_dc_gamma(lut, gamma);

	/* dc_fixpt_from_fraction(0xFFFFFFFF, 0xFFFFFFFF) = 1.0 */
	KUNIT_EXPECT_EQ(test, gamma->entries.red[0].value, one);
	KUNIT_EXPECT_EQ(test, gamma->entries.green[0].value, one);
	KUNIT_EXPECT_EQ(test, gamma->entries.blue[0].value, one);
}

static void dm_test_drm_lut32_to_dc_gamma_channels(struct kunit *test)
{
	struct drm_color_lut32 *lut;
	struct dc_gamma *gamma;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES,
			    sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	gamma = kunit_kzalloc(test, sizeof(*gamma), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, gamma);

	/* Set distinct values per channel at index 1 */
	lut[1].red = 1000;
	lut[1].green = 2000;
	lut[1].blue = 3000;

	__drm_lut32_to_dc_gamma(lut, gamma);

	/* Channels should differ */
	KUNIT_EXPECT_NE(test, gamma->entries.red[1].value,
			gamma->entries.green[1].value);
	KUNIT_EXPECT_NE(test, gamma->entries.green[1].value,
			gamma->entries.blue[1].value);
	/* Red < Green < Blue since 1000 < 2000 < 3000 */
	KUNIT_EXPECT_LT(test, gamma->entries.red[1].value,
			 gamma->entries.green[1].value);
	KUNIT_EXPECT_LT(test, gamma->entries.green[1].value,
			 gamma->entries.blue[1].value);
}

/* ---- Tests for __extract_blob_lut ---- */

static void dm_test_extract_blob_lut_null(struct kunit *test)
{
	uint32_t size = 42;
	const struct drm_color_lut *lut;

	lut = __extract_blob_lut(NULL, &size);

	KUNIT_EXPECT_NULL(test, lut);
	KUNIT_EXPECT_EQ(test, size, 0);
}

static void dm_test_extract_blob_lut_valid(struct kunit *test)
{
	const int num_entries = 4;
	struct drm_property_blob *blob;
	struct drm_color_lut *data;
	const struct drm_color_lut *lut;
	uint32_t size = 0;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);

	data = kunit_kcalloc(test, num_entries, sizeof(*data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	data[0].red = 100;
	data[0].green = 200;
	data[0].blue = 300;

	blob->data = data;
	blob->length = num_entries * sizeof(struct drm_color_lut);

	lut = __extract_blob_lut(blob, &size);

	KUNIT_EXPECT_EQ(test, size, (uint32_t)num_entries);
	KUNIT_ASSERT_NOT_NULL(test, lut);
	KUNIT_EXPECT_EQ(test, lut[0].red, 100);
	KUNIT_EXPECT_EQ(test, lut[0].green, 200);
	KUNIT_EXPECT_EQ(test, lut[0].blue, 300);
}

/* ---- Tests for __extract_blob_lut32 ---- */

static void dm_test_extract_blob_lut32_null(struct kunit *test)
{
	uint32_t size = 42;
	const struct drm_color_lut32 *lut;

	lut = __extract_blob_lut32(NULL, &size);

	KUNIT_EXPECT_NULL(test, lut);
	KUNIT_EXPECT_EQ(test, size, 0);
}

static void dm_test_extract_blob_lut32_valid(struct kunit *test)
{
	const int num_entries = 4;
	struct drm_property_blob *blob;
	struct drm_color_lut32 *data;
	const struct drm_color_lut32 *lut;
	uint32_t size = 0;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);

	data = kunit_kcalloc(test, num_entries, sizeof(*data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	data[0].red = 100000;
	data[0].green = 200000;
	data[0].blue = 300000;

	blob->data = data;
	blob->length = num_entries * sizeof(struct drm_color_lut32);

	lut = __extract_blob_lut32(blob, &size);

	KUNIT_EXPECT_EQ(test, size, (uint32_t)num_entries);
	KUNIT_ASSERT_NOT_NULL(test, lut);
	KUNIT_EXPECT_EQ(test, lut[0].red, (uint32_t)100000);
	KUNIT_EXPECT_EQ(test, lut[0].green, (uint32_t)200000);
	KUNIT_EXPECT_EQ(test, lut[0].blue, (uint32_t)300000);
}

/* ---- Tests for __to_dc_lut3d_color ---- */

static void dm_test_to_dc_lut3d_color_zero(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut lut = {0};

	__to_dc_lut3d_color(&rgb, lut, 12);

	KUNIT_EXPECT_EQ(test, rgb.red, 0U);
	KUNIT_EXPECT_EQ(test, rgb.green, 0U);
	KUNIT_EXPECT_EQ(test, rgb.blue, 0U);
}

static void dm_test_to_dc_lut3d_color_max(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut lut = {
		.red = 0xFFFF,
		.green = 0xFFFF,
		.blue = 0xFFFF,
	};

	/* 12-bit extraction: 0xFFFF maps to (1 << 12) - 1 = 4095 */
	__to_dc_lut3d_color(&rgb, lut, 12);

	KUNIT_EXPECT_EQ(test, rgb.red, 4095U);
	KUNIT_EXPECT_EQ(test, rgb.green, 4095U);
	KUNIT_EXPECT_EQ(test, rgb.blue, 4095U);
}

static void dm_test_to_dc_lut3d_color_channels(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut lut = {
		.red = 0x8000,
		.green = 0x4000,
		.blue = 0xC000,
	};

	__to_dc_lut3d_color(&rgb, lut, 12);

	/* Channels should be distinct and ordered: green < red < blue */
	KUNIT_EXPECT_GT(test, rgb.red, rgb.green);
	KUNIT_EXPECT_GT(test, rgb.blue, rgb.red);
}

/* ---- Tests for __drm_3dlut_to_dc_3dlut ---- */

static void dm_test_3dlut_to_dc_3dlut_distribution(struct kunit *test)
{
	/*
	 * Use 9 entries: loop processes 2 groups of 4, then lut0 gets
	 * one extra final entry. Total: lut0=3, lut1=2, lut2=2, lut3=2.
	 */
	const uint32_t lut3d_size = 9;
	struct drm_color_lut *lut;
	struct tetrahedral_params *params;
	int i;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	/* Fill LUT with distinct values per entry */
	for (i = 0; i < lut3d_size; i++) {
		lut[i].red = (i + 1) * 1000;
		lut[i].green = (i + 1) * 2000;
		lut[i].blue = (i + 1) * 3000;
	}

	__drm_3dlut_to_dc_3dlut(lut, lut3d_size, params, true, 12);

	/* Group 0: lut[0]→lut0[0], lut[1]→lut1[0], lut[2]→lut2[0], lut[3]→lut3[0] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].red,
			drm_color_lut_extract(lut[0].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut1[0].red,
			drm_color_lut_extract(lut[1].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut2[0].red,
			drm_color_lut_extract(lut[2].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].red,
			drm_color_lut_extract(lut[3].red, 12));

	/* Group 1: lut[4]→lut0[1], lut[5]→lut1[1], lut[6]→lut2[1], lut[7]→lut3[1] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[1].red,
			drm_color_lut_extract(lut[4].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut1[1].red,
			drm_color_lut_extract(lut[5].red, 12));

	/* Final extra entry: lut[8]→lut0[2] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[2].red,
			drm_color_lut_extract(lut[8].red, 12));
}

static void dm_test_3dlut_to_dc_3dlut_tetrahedral_17(struct kunit *test)
{
	/* Minimal test with 5 entries using tetrahedral_17 path */
	const uint32_t lut3d_size = 5;
	struct drm_color_lut *lut;
	struct tetrahedral_params *params;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	lut[0].red = 0xFFFF;
	lut[0].green = 0;
	lut[0].blue = 0;
	lut[1].red = 0;
	lut[1].green = 0xFFFF;
	lut[1].blue = 0;
	lut[2].red = 0;
	lut[2].green = 0;
	lut[2].blue = 0xFFFF;
	lut[3].red = 0x8000;
	lut[3].green = 0x8000;
	lut[3].blue = 0x8000;
	lut[4].red = 0xFFFF;
	lut[4].green = 0xFFFF;
	lut[4].blue = 0xFFFF;

	__drm_3dlut_to_dc_3dlut(lut, lut3d_size, params, false, 12);

	/* lut[0]→lut0[0]: red=4095, green=0, blue=0 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].red, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].green, 0U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].blue, 0U);

	/* lut[1]→lut1[0]: red=0, green=4095, blue=0 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut1[0].green, 4095U);

	/* lut[2]→lut2[0]: red=0, green=0, blue=4095 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut2[0].blue, 4095U);

	/* lut[4]→lut0[1] (extra final entry): all 4095 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].red, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].green, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].blue, 4095U);
}

static void dm_test_3dlut_to_dc_3dlut_green_blue(struct kunit *test)
{
	/* Verify green and blue channels are also correctly distributed */
	const uint32_t lut3d_size = 5;
	struct drm_color_lut *lut;
	struct tetrahedral_params *params;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	lut[0].red = 100;
	lut[0].green = 200;
	lut[0].blue = 300;
	lut[3].red = 400;
	lut[3].green = 500;
	lut[3].blue = 600;

	__drm_3dlut_to_dc_3dlut(lut, lut3d_size, params, true, 12);

	/* lut[0]→lut0[0]: verify all channels */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].red,
			drm_color_lut_extract(100, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].green,
			drm_color_lut_extract(200, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].blue,
			drm_color_lut_extract(300, 12));

	/* lut[3]→lut3[0]: verify all channels */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].red,
			drm_color_lut_extract(400, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].green,
			drm_color_lut_extract(500, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].blue,
			drm_color_lut_extract(600, 12));
}

/* ---- Tests for __to_dc_lut3d_32_color ---- */

static void dm_test_to_dc_lut3d_32_color_zero(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut32 lut = {0};

	__to_dc_lut3d_32_color(&rgb, lut, 12);

	KUNIT_EXPECT_EQ(test, rgb.red, 0U);
	KUNIT_EXPECT_EQ(test, rgb.green, 0U);
	KUNIT_EXPECT_EQ(test, rgb.blue, 0U);
}

static void dm_test_to_dc_lut3d_32_color_max(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut32 lut = {
		.red = 0xFFFFFFFF,
		.green = 0xFFFFFFFF,
		.blue = 0xFFFFFFFF,
	};

	/* 12-bit extraction: 0xFFFFFFFF maps to (1 << 12) - 1 = 4095 */
	__to_dc_lut3d_32_color(&rgb, lut, 12);

	KUNIT_EXPECT_EQ(test, rgb.red, 4095U);
	KUNIT_EXPECT_EQ(test, rgb.green, 4095U);
	KUNIT_EXPECT_EQ(test, rgb.blue, 4095U);
}

static void dm_test_to_dc_lut3d_32_color_channels(struct kunit *test)
{
	struct dc_rgb rgb = {0};
	struct drm_color_lut32 lut = {
		.red = 0x80000000,
		.green = 0x40000000,
		.blue = 0xC0000000,
	};

	__to_dc_lut3d_32_color(&rgb, lut, 12);

	/* Channels should be distinct and ordered: green < red < blue */
	KUNIT_EXPECT_GT(test, rgb.red, rgb.green);
	KUNIT_EXPECT_GT(test, rgb.blue, rgb.red);
}

/* ---- Tests for __drm_3dlut32_to_dc_3dlut ---- */

static void dm_test_3dlut32_to_dc_3dlut_distribution(struct kunit *test)
{
	/*
	 * Use 9 entries: loop processes 2 groups of 4, then lut0 gets
	 * one extra final entry. Total: lut0=3, lut1=2, lut2=2, lut3=2.
	 */
	const uint32_t lut3d_size = 9;
	struct drm_color_lut32 *lut;
	struct tetrahedral_params *params;
	int i;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	/* Fill LUT with distinct values per entry */
	for (i = 0; i < lut3d_size; i++) {
		lut[i].red = (i + 1) * 100000;
		lut[i].green = (i + 1) * 200000;
		lut[i].blue = (i + 1) * 300000;
	}

	__drm_3dlut32_to_dc_3dlut(lut, lut3d_size, params, true, 12);

	/* Group 0: lut[0]→lut0[0], lut[1]→lut1[0], lut[2]→lut2[0], lut[3]→lut3[0] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].red,
			drm_color_lut32_extract(lut[0].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut1[0].red,
			drm_color_lut32_extract(lut[1].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut2[0].red,
			drm_color_lut32_extract(lut[2].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].red,
			drm_color_lut32_extract(lut[3].red, 12));

	/* Group 1: lut[4]→lut0[1], lut[5]→lut1[1], lut[6]→lut2[1], lut[7]→lut3[1] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[1].red,
			drm_color_lut32_extract(lut[4].red, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut1[1].red,
			drm_color_lut32_extract(lut[5].red, 12));

	/* Final extra entry: lut[8]→lut0[2] */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[2].red,
			drm_color_lut32_extract(lut[8].red, 12));
}

static void dm_test_3dlut32_to_dc_3dlut_tetrahedral_17(struct kunit *test)
{
	/* Minimal test with 5 entries using tetrahedral_17 path */
	const uint32_t lut3d_size = 5;
	struct drm_color_lut32 *lut;
	struct tetrahedral_params *params;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	lut[0].red = 0xFFFFFFFF;
	lut[0].green = 0;
	lut[0].blue = 0;
	lut[1].red = 0;
	lut[1].green = 0xFFFFFFFF;
	lut[1].blue = 0;
	lut[2].red = 0;
	lut[2].green = 0;
	lut[2].blue = 0xFFFFFFFF;
	lut[3].red = 0x80000000;
	lut[3].green = 0x80000000;
	lut[3].blue = 0x80000000;
	lut[4].red = 0xFFFFFFFF;
	lut[4].green = 0xFFFFFFFF;
	lut[4].blue = 0xFFFFFFFF;

	__drm_3dlut32_to_dc_3dlut(lut, lut3d_size, params, false, 12);

	/* lut[0]→lut0[0]: red=4095, green=0, blue=0 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].red, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].green, 0U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[0].blue, 0U);

	/* lut[1]→lut1[0]: red=0, green=4095, blue=0 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut1[0].green, 4095U);

	/* lut[2]→lut2[0]: red=0, green=0, blue=4095 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut2[0].blue, 4095U);

	/* lut[4]→lut0[1] (extra final entry): all 4095 */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].red, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].green, 4095U);
	KUNIT_EXPECT_EQ(test, params->tetrahedral_17.lut0[1].blue, 4095U);
}

static void dm_test_3dlut32_to_dc_3dlut_green_blue(struct kunit *test)
{
	/* Verify green and blue channels are also correctly distributed */
	const uint32_t lut3d_size = 5;
	struct drm_color_lut32 *lut;
	struct tetrahedral_params *params;

	lut = kunit_kcalloc(test, lut3d_size, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	params = kunit_kzalloc(test, sizeof(*params), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, params);

	lut[0].red = 100000;
	lut[0].green = 200000;
	lut[0].blue = 300000;
	lut[3].red = 400000;
	lut[3].green = 500000;
	lut[3].blue = 600000;

	__drm_3dlut32_to_dc_3dlut(lut, lut3d_size, params, true, 12);

	/* lut[0]→lut0[0]: verify all channels */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].red,
			drm_color_lut32_extract(100000, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].green,
			drm_color_lut32_extract(200000, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut0[0].blue,
			drm_color_lut32_extract(300000, 12));

	/* lut[3]→lut3[0]: verify all channels */
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].red,
			drm_color_lut32_extract(400000, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].green,
			drm_color_lut32_extract(500000, 12));
	KUNIT_EXPECT_EQ(test, params->tetrahedral_9.lut3[0].blue,
			drm_color_lut32_extract(600000, 12));
}

/* ---- Tests for amdgpu_dm_verify_lut_sizes ---- */

/**
 * dm_test_make_lut_blob - Allocate a fake drm_property_blob for testing
 * @test: KUnit test context
 * @num_entries: number of LUT entries the blob will report
 *
 * Allocates a fake blob whose drm_color_lut_size() returns exactly
 * @num_entries.  The data pointer is non-NULL so that
 * __extract_blob_lut() returns a non-NULL lut pointer and the size
 * check inside amdgpu_dm_verify_lut_sizes() is actually exercised.
 *
 * Return: pointer to the allocated blob
 */
static struct drm_property_blob *
dm_test_make_lut_blob(struct kunit *test, uint32_t num_entries)
{
	struct drm_property_blob *blob;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);

	blob->length = num_entries * sizeof(struct drm_color_lut);
	blob->data = kunit_kcalloc(test, num_entries,
				   sizeof(struct drm_color_lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob->data);

	return blob;
}

/**
 * dm_test_verify_lut_sizes_null_luts - Both LUTs absent: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_null_luts(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	/* degamma_lut and gamma_lut are NULL (zeroed allocation) */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), 0);
}

/**
 * dm_test_verify_lut_sizes_valid_degamma - Degamma LUT with the correct atomic size: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_valid_degamma(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->degamma_lut = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), 0);
}

/**
 * dm_test_verify_lut_sizes_invalid_degamma - Degamma LUT with a wrong size: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_invalid_degamma(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	/* Use an arbitrary size that is neither atomic nor legacy */
	state->degamma_lut = dm_test_make_lut_blob(test, 128);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), -EINVAL);
}

/**
 * dm_test_verify_lut_sizes_valid_gamma_atomic - Gamma LUT with correct atomic size: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_valid_gamma_atomic(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->gamma_lut = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), 0);
}

/**
 * dm_test_verify_lut_sizes_valid_gamma_legacy - Gamma LUT with legacy 256-entry size: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_valid_gamma_legacy(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->gamma_lut = dm_test_make_lut_blob(test, MAX_COLOR_LEGACY_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), 0);
}

/**
 * dm_test_verify_lut_sizes_invalid_gamma - Size is neither atomic nor legacy: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_invalid_gamma(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->gamma_lut = dm_test_make_lut_blob(test, 128);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), -EINVAL);
}

/**
 * dm_test_verify_lut_sizes_both_valid - Both LUTs set to valid sizes: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_both_valid(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->degamma_lut = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);
	state->gamma_lut   = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), 0);
}

/**
 * dm_test_verify_lut_sizes_invalid_degamma_valid_gamma - Bad degamma overrides valid gamma: -EINVAL
 * @test: KUnit test context
 */
static void dm_test_verify_lut_sizes_invalid_degamma_valid_gamma(struct kunit *test)
{
	struct drm_crtc_state *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->degamma_lut = dm_test_make_lut_blob(test, 128);
	state->gamma_lut   = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut_sizes(state), -EINVAL);
}

/* ---- Tests for amdgpu_dm_atomic_lut3d ---- */

/**
 * dm_test_atomic_lut3d_zero_size - Zero LUT size: initialized must be cleared, no LUT data written
 * @test: KUnit test context
 */
static void dm_test_atomic_lut3d_zero_size(struct kunit *test)
{
	struct dc_plane_cm *cm;
	u32 initialized;

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	/* Pre-set initialized so we can confirm it is cleared */
	cm->lut3d_func.state.bits.initialized = 1;

	amdgpu_dm_atomic_lut3d(NULL, 0, cm);

	/* Copy bit-field: typeof cannot be applied to a bit-field */
	initialized = cm->lut3d_func.state.bits.initialized;
	KUNIT_EXPECT_EQ(test, initialized, 0U);
}

/**
 * dm_test_atomic_lut3d_nonzero_state_bits - Non-zero size: state bits and mode flags must be set
 * @test: KUnit test context
 */
static void dm_test_atomic_lut3d_nonzero_state_bits(struct kunit *test)
{
	const uint32_t lut3d_size = 5;
	struct drm_color_lut *lut_data;
	struct dc_plane_cm *cm;
	u32 initialized;

	lut_data = kunit_kcalloc(test, lut3d_size, sizeof(*lut_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut_data);

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	amdgpu_dm_atomic_lut3d(lut_data, lut3d_size, cm);

	/* Copy bit-field: typeof cannot be applied to a bit-field */
	initialized = cm->lut3d_func.state.bits.initialized;
	KUNIT_EXPECT_EQ(test, initialized, 1U);
	KUNIT_EXPECT_FALSE(test, cm->lut3d_func.lut_3d.use_tetrahedral_9);
	KUNIT_EXPECT_TRUE(test, cm->lut3d_func.lut_3d.use_12bits);
}

/**
 * dm_test_atomic_lut3d_data_forwarded - Non-zero size: LUT data forwarded to tetrahedral_17
 * @test: KUnit test context
 */
static void dm_test_atomic_lut3d_data_forwarded(struct kunit *test)
{
	const uint32_t lut3d_size = 5;
	struct drm_color_lut *lut_data;
	struct dc_plane_cm *cm;

	lut_data = kunit_kcalloc(test, lut3d_size, sizeof(*lut_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut_data);

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	lut_data[0].red   = 0xFFFF;
	lut_data[0].green = 0x8000;
	lut_data[0].blue  = 0x4000;

	amdgpu_dm_atomic_lut3d(lut_data, lut3d_size, cm);

	/*
	 * use_tetrahedral_9 == false → data goes into tetrahedral_17.
	 * lut[0] maps to lut0[0] (first element of the first group).
	 */
	KUNIT_EXPECT_EQ(test, cm->lut3d_func.lut_3d.tetrahedral_17.lut0[0].red,
			drm_color_lut_extract(0xFFFF, MAX_COLOR_3DLUT_BITDEPTH));
	KUNIT_EXPECT_EQ(test, cm->lut3d_func.lut_3d.tetrahedral_17.lut0[0].green,
			drm_color_lut_extract(0x8000, MAX_COLOR_3DLUT_BITDEPTH));
	KUNIT_EXPECT_EQ(test, cm->lut3d_func.lut_3d.tetrahedral_17.lut0[0].blue,
			drm_color_lut_extract(0x4000, MAX_COLOR_3DLUT_BITDEPTH));
}

/* ---- Tests for __set_colorop_3dlut ---- */

/**
 * dm_test_set_colorop_3dlut_zero_size - Zero LUT size: must return -EINVAL and clear initialized
 * @test: KUnit test context
 */
static void dm_test_set_colorop_3dlut_zero_size(struct kunit *test)
{
	struct dc_3dlut *lut;
	u32 initialized;

	lut = kunit_kzalloc(test, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	lut->state.bits.initialized = 1;

	KUNIT_EXPECT_EQ(test, __set_colorop_3dlut(NULL, 0, lut), -EINVAL);
	/* Copy bit-field: typeof cannot be applied to a bit-field */
	initialized = lut->state.bits.initialized;
	KUNIT_EXPECT_EQ(test, initialized, 0U);
}

/**
 * dm_test_set_colorop_3dlut_nonzero_state_bits - Non-zero size: must return 0 and set state bits
 * @test: KUnit test context
 */
static void dm_test_set_colorop_3dlut_nonzero_state_bits(struct kunit *test)
{
	const uint32_t lut3d_size = 5;
	struct drm_color_lut32 *lut_data;
	struct dc_3dlut *lut;
	u32 initialized;

	lut_data = kunit_kcalloc(test, lut3d_size, sizeof(*lut_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut_data);

	lut = kunit_kzalloc(test, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	KUNIT_EXPECT_EQ(test, __set_colorop_3dlut(lut_data, lut3d_size, lut), 0);
	/* Copy bit-field: typeof cannot be applied to a bit-field */
	initialized = lut->state.bits.initialized;
	KUNIT_EXPECT_EQ(test, initialized, 1U);
	KUNIT_EXPECT_FALSE(test, lut->lut_3d.use_tetrahedral_9);
	KUNIT_EXPECT_TRUE(test, lut->lut_3d.use_12bits);
}

/**
 * dm_test_set_colorop_3dlut_data_forwarded - Non-zero size: 32-bit data forwarded to tetrahedral_17
 * @test: KUnit test context
 */
static void dm_test_set_colorop_3dlut_data_forwarded(struct kunit *test)
{
	const uint32_t lut3d_size = 5;
	struct drm_color_lut32 *lut_data;
	struct dc_3dlut *lut;

	lut_data = kunit_kcalloc(test, lut3d_size, sizeof(*lut_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut_data);

	lut = kunit_kzalloc(test, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	lut_data[0].red   = 0xFFFFFFFF;
	lut_data[0].green = 0x80000000;
	lut_data[0].blue  = 0x40000000;

	KUNIT_EXPECT_EQ(test, __set_colorop_3dlut(lut_data, lut3d_size, lut), 0);

	/*
	 * use_tetrahedral_9 == false → data goes into tetrahedral_17.
	 * lut[0] maps to lut0[0].  Bit depth used by __set_colorop_3dlut is 12.
	 */
	KUNIT_EXPECT_EQ(test, lut->lut_3d.tetrahedral_17.lut0[0].red,
			drm_color_lut32_extract(0xFFFFFFFF, 12));
	KUNIT_EXPECT_EQ(test, lut->lut_3d.tetrahedral_17.lut0[0].green,
			drm_color_lut32_extract(0x80000000, 12));
	KUNIT_EXPECT_EQ(test, lut->lut_3d.tetrahedral_17.lut0[0].blue,
			drm_color_lut32_extract(0x40000000, 12));
}

/**
 * dm_test_set_tf_bypass - __set_tf_bypass: sets TF_TYPE_BYPASS and TRANSFER_FUNCTION_LINEAR
 * @test: KUnit test context
 */
static void dm_test_set_tf_bypass(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);

	tf->type = TF_TYPE_DISTRIBUTED_POINTS;
	tf->tf = TRANSFER_FUNCTION_SRGB;

	__set_tf_bypass(tf);

	KUNIT_EXPECT_EQ(test, (int)tf->type, (int)TF_TYPE_BYPASS);
	KUNIT_EXPECT_EQ(test, (int)tf->tf, (int)TRANSFER_FUNCTION_LINEAR);
}

/**
 * dm_test_set_tf_distributed_points_srgb - __set_tf_distributed_points: sRGB predefined TF
 * @test: KUnit test context
 */
static void dm_test_set_tf_distributed_points_srgb(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);

	__set_tf_distributed_points(tf, TRANSFER_FUNCTION_SRGB);

	KUNIT_EXPECT_EQ(test, (int)tf->type, (int)TF_TYPE_DISTRIBUTED_POINTS);
	KUNIT_EXPECT_EQ(test, (int)tf->tf, (int)TRANSFER_FUNCTION_SRGB);
	KUNIT_EXPECT_EQ(test, tf->sdr_ref_white_level, 80U);
}

/**
 * dm_test_set_tf_distributed_points_pq - __set_tf_distributed_points: PQ predefined TF
 * @test: KUnit test context
 */
static void dm_test_set_tf_distributed_points_pq(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);

	__set_tf_distributed_points(tf, TRANSFER_FUNCTION_PQ);

	KUNIT_EXPECT_EQ(test, (int)tf->type, (int)TF_TYPE_DISTRIBUTED_POINTS);
	KUNIT_EXPECT_EQ(test, (int)tf->tf, (int)TRANSFER_FUNCTION_PQ);
	KUNIT_EXPECT_EQ(test, tf->sdr_ref_white_level, 80U);
}

/**
 * dm_test_set_legacy_tf_identity - Legacy identity LUT uses the sRGB ROM path
 * @test: KUnit test context
 */
static void dm_test_set_legacy_tf_identity(struct kunit *test)
{
	struct drm_color_lut *lut;
	struct dc_transfer_func *tf;
	int i;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);
	lut = kunit_kcalloc(test, MAX_COLOR_LEGACY_LUT_ENTRIES, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	for (i = 0; i < MAX_COLOR_LEGACY_LUT_ENTRIES; i++) {
		u16 value = i * MAX_DRM_LUT_VALUE / (MAX_COLOR_LEGACY_LUT_ENTRIES - 1);

		lut[i].red = value;
		lut[i].green = value;
		lut[i].blue = value;
	}

	tf->type = TF_TYPE_PREDEFINED;
	tf->tf = TRANSFER_FUNCTION_SRGB;

	KUNIT_EXPECT_EQ(test,
			__set_legacy_tf(tf, lut, MAX_COLOR_LEGACY_LUT_ENTRIES, true),
			0);
}

/**
 * dm_test_set_output_tf_linear - Linear output without a LUT calculates degamma
 * @test: KUnit test context
 */
static void dm_test_set_output_tf_linear(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);
	tf->type = TF_TYPE_PREDEFINED;
	tf->tf = TRANSFER_FUNCTION_LINEAR;

	KUNIT_EXPECT_EQ(test, __set_output_tf(tf, NULL, 0, false), 0);
}

/**
 * dm_test_set_output_tf_32_srgb_rom - sRGB output uses the no-LUT ROM path
 * @test: KUnit test context
 */
static void dm_test_set_output_tf_32_srgb_rom(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);
	tf->type = TF_TYPE_PREDEFINED;
	tf->tf = TRANSFER_FUNCTION_SRGB;

	KUNIT_EXPECT_EQ(test, __set_output_tf_32(tf, NULL, 0, true), 0);
}

/**
 * dm_test_set_input_tf_srgb - Predefined sRGB input needs no generated curve
 * @test: KUnit test context
 */
static void dm_test_set_input_tf_srgb(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);
	tf->type = TF_TYPE_PREDEFINED;
	tf->tf = TRANSFER_FUNCTION_SRGB;

	KUNIT_EXPECT_EQ(test, __set_input_tf(NULL, tf, NULL, 0), 0);
}

/**
 * dm_test_set_input_tf_32_srgb - 32-bit input wrapper accepts predefined sRGB
 * @test: KUnit test context
 */
static void dm_test_set_input_tf_32_srgb(struct kunit *test)
{
	struct dc_transfer_func *tf;

	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);
	tf->type = TF_TYPE_PREDEFINED;
	tf->tf = TRANSFER_FUNCTION_SRGB;

	KUNIT_EXPECT_EQ(test, __set_input_tf_32(NULL, tf, NULL, 0), 0);
}

/**
 * dm_test_set_transfer_funcs_with_luts - LUT-backed transfer functions succeed
 * @test: KUnit test context
 */
static void dm_test_set_transfer_funcs_with_luts(struct kunit *test)
{
	struct drm_color_lut32 *lut32;
	struct drm_color_lut *lut;
	struct dc_transfer_func *tf;

	lut = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);
	lut32 = kunit_kcalloc(test, MAX_COLOR_LUT_ENTRIES, sizeof(*lut32), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut32);
	tf = kunit_kzalloc(test, sizeof(*tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tf);

	tf->type = TF_TYPE_DISTRIBUTED_POINTS;
	tf->tf = TRANSFER_FUNCTION_LINEAR;
	KUNIT_EXPECT_EQ(test,
			__set_output_tf(tf, lut, MAX_COLOR_LUT_ENTRIES, false),
			0);

	memset(tf, 0, sizeof(*tf));
	tf->type = TF_TYPE_DISTRIBUTED_POINTS;
	tf->tf = TRANSFER_FUNCTION_LINEAR;
	KUNIT_EXPECT_EQ(test,
			__set_output_tf_32(tf, lut32, MAX_COLOR_LUT_ENTRIES, false),
			0);

	memset(tf, 0, sizeof(*tf));
	tf->type = TF_TYPE_DISTRIBUTED_POINTS;
	tf->tf = TRANSFER_FUNCTION_SRGB;
	KUNIT_EXPECT_EQ(test,
			__set_input_tf(NULL, tf, lut, MAX_COLOR_LUT_ENTRIES),
			0);

	memset(tf, 0, sizeof(*tf));
	tf->type = TF_TYPE_DISTRIBUTED_POINTS;
	tf->tf = TRANSFER_FUNCTION_SRGB;
	KUNIT_EXPECT_EQ(test,
			__set_input_tf_32(NULL, tf, lut32, MAX_COLOR_LUT_ENTRIES),
			0);
}

/**
 * dm_test_set_atomic_regamma_bypass - No LUT and linear TF: must take bypass path
 * @test: KUnit test context
 */
static void dm_test_set_atomic_regamma_bypass(struct kunit *test)
{
	struct dc_transfer_func *out_tf;

	out_tf = kunit_kzalloc(test, sizeof(*out_tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, out_tf);

	/* size=0 and tf=LINEAR: must take the bypass branch */
	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_set_atomic_regamma(out_tf, NULL, 0, false,
					     TRANSFER_FUNCTION_LINEAR),
		0);
	KUNIT_EXPECT_EQ(test, (int)out_tf->type, (int)TF_TYPE_BYPASS);
	KUNIT_EXPECT_EQ(test, (int)out_tf->tf, (int)TRANSFER_FUNCTION_LINEAR);
}

/**
 * dm_test_set_atomic_regamma_srgb - Non-linear TF enables generated regamma
 * @test: KUnit test context
 */
static void dm_test_set_atomic_regamma_srgb(struct kunit *test)
{
	struct dc_transfer_func *out_tf;

	out_tf = kunit_kzalloc(test, sizeof(*out_tf), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, out_tf);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_atomic_regamma(out_tf, NULL, 0, true, TRANSFER_FUNCTION_SRGB),
			0);
	KUNIT_EXPECT_EQ(test, (int)out_tf->type, (int)TF_TYPE_DISTRIBUTED_POINTS);
}

/**
 * dm_test_atomic_shaper_lut_bypass - No LUT and linear TF: must take bypass path
 * @test: KUnit test context
 */
static void dm_test_atomic_shaper_lut_bypass(struct kunit *test)
{
	struct dc_plane_cm *cm;

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	/* size=0 and tf=LINEAR: must take the bypass branch */
	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_atomic_shaper_lut(NULL, false,
					    TRANSFER_FUNCTION_LINEAR,
					    0, cm),
		0);
	KUNIT_EXPECT_EQ(test, (int)cm->shaper_func.type, (int)TF_TYPE_BYPASS);
	KUNIT_EXPECT_EQ(test, (int)cm->shaper_func.tf, (int)TRANSFER_FUNCTION_LINEAR);
}

/**
 * dm_test_atomic_blend_lut_bypass - amdgpu_dm_atomic_blend_lut bypass: no LUT, linear TF -> bypass
 * @test: KUnit test context
 */
static void dm_test_atomic_blend_lut_bypass(struct kunit *test)
{
	struct dc_plane_cm *cm;

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	/* size=0 and tf=LINEAR: must take the bypass branch */
	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_atomic_blend_lut(NULL, false,
					   TRANSFER_FUNCTION_LINEAR,
					   0, cm),
		0);
	KUNIT_EXPECT_EQ(test, (int)cm->blend_func.type, (int)TF_TYPE_BYPASS);
	KUNIT_EXPECT_EQ(test, (int)cm->blend_func.tf, (int)TRANSFER_FUNCTION_LINEAR);
}

/**
 * dm_test_atomic_shaper_blend_srgb - Non-linear TFs enable shaper and blend
 * @test: KUnit test context
 */
static void dm_test_atomic_shaper_blend_srgb(struct kunit *test)
{
	struct dc_plane_cm *cm;

	cm = kunit_kzalloc(test, sizeof(*cm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cm);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_atomic_shaper_lut(NULL, true, TRANSFER_FUNCTION_SRGB, 0, cm),
			0);
	KUNIT_EXPECT_TRUE(test, cm->flags.bits.shaper_enable);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_atomic_blend_lut(NULL, false, TRANSFER_FUNCTION_SRGB, 0, cm),
			0);
	KUNIT_EXPECT_TRUE(test, cm->flags.bits.blend_enable);
}

/* ---- Tests for __set_colorop_in_tf_1d_curve ---- */

/**
 * dm_test_set_colorop_in_tf_1d_curve_invalid_type - Non-1D colorop type must be rejected
 * @test: KUnit test context
 */
static void dm_test_set_colorop_in_tf_1d_curve_invalid_type(struct kunit *test)
{
	struct dc_plane_state *dc_plane_state;
	struct drm_colorop *colorop;
	struct drm_colorop_state *colorop_state;

	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);

	colorop = kunit_kzalloc(test, sizeof(*colorop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop);

	colorop_state = kunit_kzalloc(test, sizeof(*colorop_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop_state);

	colorop->type = DRM_COLOROP_3D_LUT;
	colorop_state->colorop = colorop;
	colorop_state->curve_1d_type = DRM_COLOROP_1D_CURVE_SRGB_EOTF;

	KUNIT_EXPECT_EQ(test,
		__set_colorop_in_tf_1d_curve(dc_plane_state, colorop_state),
		-EINVAL);
}

/**
 * dm_test_set_colorop_in_tf_1d_curve_unsupported_curve - Unsupported 1D curve type must be rejected
 * @test: KUnit test context
 */
static void dm_test_set_colorop_in_tf_1d_curve_unsupported_curve(struct kunit *test)
{
	struct dc_plane_state *dc_plane_state;
	struct drm_colorop *colorop;
	struct drm_colorop_state *colorop_state;

	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);

	colorop = kunit_kzalloc(test, sizeof(*colorop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop);

	colorop_state = kunit_kzalloc(test, sizeof(*colorop_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop_state);

	colorop->type = DRM_COLOROP_1D_CURVE;
	colorop_state->colorop = colorop;
	colorop_state->curve_1d_type = DRM_COLOROP_1D_CURVE_COUNT;

	KUNIT_EXPECT_EQ(test,
		__set_colorop_in_tf_1d_curve(dc_plane_state, colorop_state),
		-EINVAL);
}

/**
 * dm_test_set_colorop_in_tf_1d_curve_bypass - Bypass mode forces linear bypass transfer function
 * @test: KUnit test context
 */
static void dm_test_set_colorop_in_tf_1d_curve_bypass(struct kunit *test)
{
	struct dc_plane_state *dc_plane_state;
	struct drm_colorop *colorop;
	struct drm_colorop_state *colorop_state;

	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);

	colorop = kunit_kzalloc(test, sizeof(*colorop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop);

	colorop_state = kunit_kzalloc(test, sizeof(*colorop_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop_state);

	colorop->type = DRM_COLOROP_1D_CURVE;
	colorop_state->colorop = colorop;
	colorop_state->curve_1d_type = DRM_COLOROP_1D_CURVE_SRGB_EOTF;
	colorop_state->bypass = true;

	KUNIT_EXPECT_EQ(test,
		__set_colorop_in_tf_1d_curve(dc_plane_state, colorop_state),
		0);
	KUNIT_EXPECT_EQ(test,
		(int)dc_plane_state->in_transfer_func.type,
		(int)TF_TYPE_BYPASS);
	KUNIT_EXPECT_EQ(test,
		(int)dc_plane_state->in_transfer_func.tf,
		(int)TRANSFER_FUNCTION_LINEAR);
}

/* ---- Tests for amdgpu_dm_init_color_mod ---- */

/**
 * dm_test_init_color_mod - Smoke test: must initialize x-points without crashing
 * @test: KUnit test context
 */
static void dm_test_init_color_mod(struct kunit *test)
{
	amdgpu_dm_init_color_mod();
	KUNIT_SUCCEED(test);
}

/* ---- Tests for amdgpu_dm_verify_lut3d_size ---- */

/**
 * dm_test_verify_lut3d_alloc_plane - Allocate a dm_plane_state for lut3d tests
 * @test: KUnit test context
 *
 * Returns: a drm_plane_state pointer embedded in a zeroed dm_plane_state.
 */
static struct drm_plane_state *
dm_test_verify_lut3d_alloc_plane(struct kunit *test)
{
	struct dm_plane_state *dm_plane_state;

	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);

	return &dm_plane_state->base;
}

/**
 * dm_test_verify_lut3d_alloc_adev - Allocate adev with a DC and given 3D LUT cap
 * @test: KUnit test context
 * @has_3dlut: value to program into caps.color.dpp.hw_3d_lut
 *
 * Returns: an amdgpu_device with adev->dm.dc allocated.
 */
static struct amdgpu_device *
dm_test_verify_lut3d_alloc_adev(struct kunit *test, bool has_3dlut)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dm.dc = kunit_kzalloc(test, sizeof(*adev->dm.dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.dc);
	adev->dm.dc->caps.color.dpp.hw_3d_lut = has_3dlut;

	return adev;
}

/**
 * dm_test_verify_lut3d_no_luts - No shaper/3D LUT blobs: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut3d_no_luts(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_verify_lut3d_alloc_adev(test, false);
	struct drm_plane_state *plane_state = dm_test_verify_lut3d_alloc_plane(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut3d_size(adev, plane_state), 0);
}

/**
 * dm_test_verify_lut3d_bad_shaper - Shaper LUT with wrong size: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_verify_lut3d_bad_shaper(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_verify_lut3d_alloc_adev(test, true);
	struct drm_plane_state *plane_state = dm_test_verify_lut3d_alloc_plane(test);
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(plane_state);

	/* has_3dlut => expected shaper size is MAX_COLOR_LUT_ENTRIES */
	dm_plane_state->shaper_lut = dm_test_make_lut_blob(test, 128);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut3d_size(adev, plane_state), -EINVAL);
}

/**
 * dm_test_verify_lut3d_bad_lut3d - 3D LUT with wrong size: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_verify_lut3d_bad_lut3d(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_verify_lut3d_alloc_adev(test, true);
	struct drm_plane_state *plane_state = dm_test_verify_lut3d_alloc_plane(test);
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(plane_state);

	/* Valid shaper, but wrong 3D LUT size */
	dm_plane_state->shaper_lut = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);
	dm_plane_state->lut3d = dm_test_make_lut_blob(test, 128);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut3d_size(adev, plane_state), -EINVAL);
}

/**
 * dm_test_verify_lut3d_valid - Correct shaper and 3D LUT sizes: must succeed
 * @test: KUnit test context
 */
static void dm_test_verify_lut3d_valid(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_verify_lut3d_alloc_adev(test, true);
	struct drm_plane_state *plane_state = dm_test_verify_lut3d_alloc_plane(test);
	struct dm_plane_state *dm_plane_state = to_dm_plane_state(plane_state);
	const uint32_t lut3d_entries =
		MAX_COLOR_3DLUT_SIZE * MAX_COLOR_3DLUT_SIZE * MAX_COLOR_3DLUT_SIZE;

	dm_plane_state->shaper_lut = dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);
	dm_plane_state->lut3d = dm_test_make_lut_blob(test, lut3d_entries);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_verify_lut3d_size(adev, plane_state), 0);
}

/* ---- Tests for plane colorop helpers ---- */

/**
 * struct dm_test_colorop_fixture - shared state for plane colorop walk tests
 * @adev: backing amdgpu device (provides a real DRM device)
 * @state: fabricated atomic state with a single colorop slot
 * @colorop: the colorop under test
 * @colorop_state: the new state attached to @colorop
 * @plane_state: plane state whose ->state points at @state
 * @dc_plane_state: DC plane state written by the helpers
 */
struct dm_test_colorop_fixture {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct drm_colorop *colorop;
	struct drm_colorop_state *colorop_state;
	struct drm_plane_state *plane_state;
	struct dc_plane_state *dc_plane_state;
};

/**
 * dm_test_colorop_setup - build a single-colorop atomic state fixture
 * @test: KUnit test context
 * @type: colorop type to assign
 *
 * Fabricates a minimal drm_atomic_commit with one colorop slot so that
 * for_each_new_colorop_in_state() finds exactly the colorop under test.
 *
 * Returns: a populated fixture (by value).
 */
static struct dm_test_colorop_fixture
dm_test_colorop_setup(struct kunit *test, enum drm_colorop_type type)
{
	struct dm_test_colorop_fixture f = {0};
	struct __drm_colorops_state *colorops;
	struct dm_plane_state *dm_plane_state;

	f.adev = dm_kunit_alloc_adev(test);
	f.adev->ddev.mode_config.num_colorop = 1;

	f.colorop = kunit_kzalloc(test, sizeof(*f.colorop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.colorop);
	f.colorop->dev = &f.adev->ddev;
	f.colorop->type = type;

	f.colorop_state = kunit_kzalloc(test, sizeof(*f.colorop_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.colorop_state);
	f.colorop_state->colorop = f.colorop;

	colorops = kunit_kcalloc(test, 1, sizeof(*colorops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorops);
	colorops[0].ptr = f.colorop;
	colorops[0].new_state = f.colorop_state;

	f.state = kunit_kzalloc(test, sizeof(*f.state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.state);
	f.state->dev = &f.adev->ddev;
	f.state->colorops = colorops;

	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);
	f.plane_state = &dm_plane_state->base;
	f.plane_state->state = f.state;

	f.dc_plane_state = kunit_kzalloc(test, sizeof(*f.dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.dc_plane_state);

	return f;
}

/**
 * dm_test_colorop_multiplier_applied - Multiplier colorop programs hdr_mult
 * @test: KUnit test context
 */
static void dm_test_colorop_multiplier_applied(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_MULTIPLIER);

	/* 1.0 in S31.32 sign-magnitude */
	f.colorop_state->multiplier = 1ULL << 32;

	KUNIT_EXPECT_EQ(test,
		__set_dm_plane_colorop_multiplier(f.plane_state, f.dc_plane_state, f.colorop),
		0);
	KUNIT_EXPECT_EQ(test, f.dc_plane_state->hdr_mult.value, (long long)(1ULL << 32));
}

/**
 * dm_test_colorop_multiplier_no_match - Non-multiplier colorop leaves hdr_mult untouched
 * @test: KUnit test context
 */
static void dm_test_colorop_multiplier_no_match(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_1D_CURVE);

	f.colorop_state->multiplier = 1ULL << 32;

	KUNIT_EXPECT_EQ(test,
			__set_dm_plane_colorop_multiplier(f.plane_state, f.dc_plane_state, f.colorop),
			0);
	KUNIT_EXPECT_EQ(test, f.dc_plane_state->hdr_mult.value, 0LL);
}

/**
 * dm_test_colorop_3x4_matrix_applied - CTM 3x4 colorop enables gamut remap
 * @test: KUnit test context
 */
static void dm_test_colorop_3x4_matrix_applied(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_CTM_3X4);
	struct drm_property_blob *blob;
	struct drm_color_ctm_3x4 *ctm;

	ctm = kunit_kzalloc(test, sizeof(*ctm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctm);
	ctm->matrix[0] = 1ULL << 32; /* identity diagonal */
	ctm->matrix[5] = 1ULL << 32;
	ctm->matrix[10] = 1ULL << 32;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->data = ctm;
	blob->length = sizeof(struct drm_color_ctm_3x4);
	f.colorop_state->data = blob;

	KUNIT_EXPECT_EQ(test,
			__set_dm_plane_colorop_3x4_matrix(f.plane_state, f.dc_plane_state, f.colorop),
			0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->gamut_remap_matrix.enable_remap);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->input_csc_color_matrix.enable_adjustment);
}

/**
 * dm_test_colorop_3x4_matrix_bad_length - Wrong blob length: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_colorop_3x4_matrix_bad_length(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_CTM_3X4);
	struct drm_property_blob *blob;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->data = kunit_kzalloc(test, 8, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob->data);
	blob->length = 7; /* not sizeof(struct drm_color_ctm_3x4) */
	f.colorop_state->data = blob;

	KUNIT_EXPECT_EQ(test,
			__set_dm_plane_colorop_3x4_matrix(f.plane_state, f.dc_plane_state, f.colorop),
			-EINVAL);
}

/**
 * dm_test_colorop_degamma_predefined - Degamma 1D curve programs predefined TF
 * @test: KUnit test context
 */
static void dm_test_colorop_degamma_predefined(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_1D_CURVE);

	/* SRGB_EOTF is part of amdgpu_dm_supported_degam_tfs */
	f.colorop_state->curve_1d_type = DRM_COLOROP_1D_CURVE_SRGB_EOTF;
	f.colorop_state->bypass = false;

	KUNIT_EXPECT_EQ(test,
			__set_dm_plane_colorop_degamma(f.plane_state, f.dc_plane_state, f.colorop),
			0);
	KUNIT_EXPECT_EQ(test,
			(int)f.dc_plane_state->in_transfer_func.type,
			(int)TF_TYPE_PREDEFINED);
	KUNIT_EXPECT_EQ(test,
			(int)f.dc_plane_state->in_transfer_func.tf,
			(int)TRANSFER_FUNCTION_SRGB);
}

/**
 * dm_test_colorop_degamma_no_match - Unsupported degamma curve: must return -EINVAL
 * @test: KUnit test context
 */
static void dm_test_colorop_degamma_no_match(struct kunit *test)
{
	struct dm_test_colorop_fixture f =
		dm_test_colorop_setup(test, DRM_COLOROP_1D_CURVE);

	/* SRGB_INV_EOTF is a shaper TF, not in amdgpu_dm_supported_degam_tfs */
	f.colorop_state->curve_1d_type = DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF;

	KUNIT_EXPECT_EQ(test,
			__set_dm_plane_colorop_degamma(f.plane_state, f.dc_plane_state, f.colorop),
			-EINVAL);
}

/* ---- Tests for CRTC and plane color management update paths ---- */

/**
 * struct dm_test_color_update_fixture - minimal color update fixture
 * @adev: backing amdgpu device
 * @state: DRM atomic state with @adev's DRM device
 * @crtc_state: DM CRTC state under test
 * @stream: DC stream referenced by @crtc_state
 * @dm_plane_state: DM plane state under test
 * @plane: DRM plane referenced by @dm_plane_state
 * @dc_plane_state: DC plane state under test
 */
struct dm_test_color_update_fixture {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct dm_crtc_state *crtc_state;
	struct dc_stream_state *stream;
	struct dm_plane_state *dm_plane_state;
	struct drm_plane *plane;
	struct dc_plane_state *dc_plane_state;
};

/**
 * dm_test_color_update_setup - allocate a minimal color update fixture
 * @test: KUnit test context
 *
 * Returns: a populated fixture with all large DC/DRM state heap-allocated.
 */
static struct dm_test_color_update_fixture
dm_test_color_update_setup(struct kunit *test)
{
	struct dm_test_color_update_fixture f = {0};

	f.adev = dm_kunit_alloc_adev(test);
	f.adev->dm.dc = kunit_kzalloc(test, sizeof(*f.adev->dm.dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.adev->dm.dc);

	f.state = kunit_kzalloc(test, sizeof(*f.state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.state);
	f.state->dev = &f.adev->ddev;

	f.stream = kunit_kzalloc(test, sizeof(*f.stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.stream);

	f.crtc_state = kunit_kzalloc(test, sizeof(*f.crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.crtc_state);
	f.crtc_state->base.state = f.state;
	f.crtc_state->stream = f.stream;

	f.plane = kunit_kzalloc(test, sizeof(*f.plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.plane);
	f.plane->dev = &f.adev->ddev;

	f.dm_plane_state = kunit_kzalloc(test, sizeof(*f.dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.dm_plane_state);
	f.dm_plane_state->base.state = f.state;
	f.dm_plane_state->base.plane = f.plane;

	f.dc_plane_state = kunit_kzalloc(test, sizeof(*f.dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f.dc_plane_state);

	return f;
}

/**
 * dm_test_make_ctm_blob - Allocate a fake drm_property_blob for CTM data
 * @test: KUnit test context
 * @data: CTM data pointer
 * @size: CTM data size in bytes
 *
 * Returns: a fake property blob pointing at @data.
 */
static struct drm_property_blob *
dm_test_make_ctm_blob(struct kunit *test, void *data, size_t size)
{
	struct drm_property_blob *blob;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->data = data;
	blob->length = size;

	return blob;
}

/**
 * dm_test_check_crtc_color_mgmt_no_luts - No CRTC LUTs check succeeds
 * @test: KUnit test context
 */
static void dm_test_check_crtc_color_mgmt_no_luts(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.crtc_state->cm_has_degamma = true;
	f.crtc_state->cm_is_degamma_srgb = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_check_crtc_color_mgmt(f.crtc_state, true), 0);
	KUNIT_EXPECT_FALSE(test, f.crtc_state->cm_has_degamma);
	KUNIT_EXPECT_FALSE(test, f.crtc_state->cm_is_degamma_srgb);
}

/**
 * dm_test_check_crtc_color_mgmt_legacy_regamma - Legacy LUT uses sRGB regamma
 * @test: KUnit test context
 */
static void dm_test_check_crtc_color_mgmt_legacy_regamma(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.crtc_state->base.gamma_lut = dm_test_make_lut_blob(test, MAX_COLOR_LEGACY_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_check_crtc_color_mgmt(f.crtc_state, true), 0);
	KUNIT_EXPECT_TRUE(test, f.crtc_state->cm_is_degamma_srgb);
}

/**
 * dm_test_check_crtc_color_mgmt_degamma - Atomic degamma is mapped to the plane
 * @test: KUnit test context
 */
static void dm_test_check_crtc_color_mgmt_degamma(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.crtc_state->base.degamma_lut =
		dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_check_crtc_color_mgmt(f.crtc_state, true), 0);
	KUNIT_EXPECT_TRUE(test, f.crtc_state->cm_has_degamma);
	KUNIT_EXPECT_FALSE(test, f.crtc_state->cm_is_degamma_srgb);
}

/**
 * dm_test_update_crtc_color_mgmt_no_ctm - No CRTC CTM leaves remap bypassed
 * @test: KUnit test context
 */
static void dm_test_update_crtc_color_mgmt_no_ctm(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.stream->gamut_remap_matrix.enable_remap = true;
	f.stream->csc_color_matrix.enable_adjustment = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_update_crtc_color_mgmt(f.crtc_state), 0);
	KUNIT_EXPECT_FALSE(test, f.stream->gamut_remap_matrix.enable_remap);
	KUNIT_EXPECT_FALSE(test, f.stream->csc_color_matrix.enable_adjustment);
}

/**
 * dm_test_update_crtc_color_mgmt_ctm - CRTC CTM enables stream gamut remap
 * @test: KUnit test context
 */
static void dm_test_update_crtc_color_mgmt_ctm(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_color_ctm *ctm;

	ctm = kunit_kzalloc(test, sizeof(*ctm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctm);
	ctm->matrix[0] = 1ULL << 32;
	ctm->matrix[4] = 1ULL << 32;
	ctm->matrix[8] = 1ULL << 32;
	f.crtc_state->base.ctm = dm_test_make_ctm_blob(test, ctm, sizeof(*ctm));

	KUNIT_EXPECT_EQ(test, amdgpu_dm_update_crtc_color_mgmt(f.crtc_state), 0);
	KUNIT_EXPECT_TRUE(test, f.stream->gamut_remap_matrix.enable_remap);
	KUNIT_EXPECT_FALSE(test, f.stream->csc_color_matrix.enable_adjustment);
}

/**
 * dm_test_update_plane_color_mgmt_bad_lut3d - Bad 3D LUT size fails early
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_bad_lut3d(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->lut3d = dm_test_make_lut_blob(test, 128);

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base,
						  f.dc_plane_state),
		-EINVAL);
}

/**
 * dm_test_update_plane_color_mgmt_fallback_defaults - Legacy fallback default path
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_fallback_defaults(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_EQ(test, f.dc_plane_state->hdr_mult.value, 0LL);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->gamut_remap_matrix.enable_remap);
}

/**
 * dm_test_update_plane_color_mgmt_maps_crtc_degamma - CRTC implicit degamma path
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_maps_crtc_degamma(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.crtc_state->cm_is_degamma_srgb = true;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_EQ(test,
			(int)f.dc_plane_state->in_transfer_func.type,
			(int)TF_TYPE_PREDEFINED);
	KUNIT_EXPECT_EQ(test,
			(int)f.dc_plane_state->in_transfer_func.tf,
			(int)TRANSFER_FUNCTION_SRGB);
}

/**
 * dm_test_update_plane_color_mgmt_maps_crtc_degamma_lut - CRTC LUT maps to plane degamma
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_maps_crtc_degamma_lut(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	int ret;

	f.crtc_state->cm_has_degamma = true;
	f.crtc_state->cm_is_degamma_srgb = true;
	f.crtc_state->base.degamma_lut =
		dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->in_transfer_func.type,
			(int)TF_TYPE_DISTRIBUTED_POINTS);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->in_transfer_func.tf,
			(int)TRANSFER_FUNCTION_SRGB);
}

/**
 * dm_test_update_plane_color_mgmt_maps_bt709_degamma - Video uses BT.709 degamma
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_maps_bt709_degamma(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	int ret;

	f.crtc_state->cm_is_degamma_srgb = true;
	f.dc_plane_state->format = SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->in_transfer_func.tf,
			(int)TRANSFER_FUNCTION_BT709);
}

/**
 * dm_test_update_plane_color_mgmt_plane_degamma_lut - Non-linear plane LUT succeeds
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_plane_degamma_lut(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct drm_color_lut *lut;
	int ret;

	f.dm_plane_state->degamma_lut =
		dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);
	lut = f.dm_plane_state->degamma_lut->data;
	lut[0].red = MAX_DRM_LUT_VALUE;
	lut[0].green = MAX_DRM_LUT_VALUE;
	lut[0].blue = MAX_DRM_LUT_VALUE;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->in_transfer_func.type,
			(int)TF_TYPE_DISTRIBUTED_POINTS);
}

/**
 * dm_test_update_plane_color_mgmt_rejects_dual_degamma - Reject two degamma stages
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_rejects_dual_degamma(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct drm_crtc *crtc;
	int ret;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	crtc->dev = &f.adev->ddev;
	f.crtc_state->base.crtc = crtc;
	f.crtc_state->cm_has_degamma = true;
	f.dm_plane_state->degamma_tf = AMDGPU_TRANSFER_FUNCTION_SRGB_EOTF;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_update_plane_color_mgmt_uses_color_caps - DC context provides color caps
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_uses_color_caps(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct dc_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->dc = f.adev->dm.dc;
	f.dc_plane_state->ctx = ctx;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
}

/**
 * dm_test_update_plane_color_mgmt_legacy_luts - Legacy shaper and blend LUTs succeed
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_legacy_luts(struct kunit *test)
{
	const u32 lut3d_entries =
		MAX_COLOR_3DLUT_SIZE * MAX_COLOR_3DLUT_SIZE * MAX_COLOR_3DLUT_SIZE;
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	int ret;

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->shaper_lut =
		dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);
	f.dm_plane_state->lut3d = dm_test_make_lut_blob(test, lut3d_entries);
	f.dm_plane_state->blend_lut =
		dm_test_make_lut_blob(test, MAX_COLOR_LUT_ENTRIES);

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
}

/**
 * dm_test_update_plane_color_mgmt_plane_ctm - Plane CTM maps to gamut remap
 * @test: KUnit test context
 *
 * When the plane has a CTM blob, the update path programs the DPP gamut
 * remap matrix and enables remapping (and disables the input CSC).
 */
static void dm_test_update_plane_color_mgmt_plane_ctm(struct kunit *test)
{
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_color_ctm_3x4 *ctm;

	ctm = kunit_kzalloc(test, sizeof(*ctm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctm);
	/* Identity-ish entries; values are irrelevant to the branch taken. */
	ctm->matrix[0] = 0x100000000ULL;
	ctm->matrix[5] = 0x100000000ULL;
	ctm->matrix[10] = 0x100000000ULL;

	f.dm_plane_state->ctm = dm_test_make_ctm_blob(test, ctm, sizeof(*ctm));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->gamut_remap_matrix.enable_remap);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->input_csc_color_matrix.enable_adjustment);
}

/**
 * dm_test_colorop_pipeline_add - append one colorop to a fabricated pipeline
 * @test: KUnit test context
 * @f: color update fixture that owns the atomic state
 * @index: colorop array index to populate
 * @type: colorop type
 * @curve_1d_type: 1D curve type for DRM_COLOROP_1D_CURVE states
 * @bypass: bypass flag for the new colorop state
 *
 * Returns: the newly allocated colorop.
 */
static struct drm_colorop *
dm_test_colorop_pipeline_add(struct kunit *test,
			     struct dm_test_color_update_fixture *f,
			     int index, enum drm_colorop_type type,
			     enum drm_colorop_curve_1d_type curve_1d_type,
			     bool bypass)
{
	struct drm_colorop_state *colorop_state;
	struct drm_colorop *colorop;

	colorop = kunit_kzalloc(test, sizeof(*colorop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop);
	colorop->dev = &f->adev->ddev;
	colorop->type = type;
	colorop->size = MAX_COLOR_LUT_ENTRIES;

	colorop_state = kunit_kzalloc(test, sizeof(*colorop_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorop_state);
	colorop_state->colorop = colorop;
	colorop_state->curve_1d_type = curve_1d_type;
	colorop_state->bypass = bypass;

	f->state->colorops[index].ptr = colorop;
	f->state->colorops[index].new_state = colorop_state;

	return colorop;
}

/**
 * dm_test_colorop_pipeline_setup - build a linked colorop pipeline prefix
 * @test: KUnit test context
 * @f: color update fixture that owns the atomic state
 * @types: colorop types to create
 * @curves: curve type for each colorop state
 * @bypass: bypass flag for each colorop state
 * @count: number of colorops to create
 *
 * Returns: the first colorop in the linked pipeline.
 */
static struct drm_colorop *
dm_test_colorop_pipeline_setup(struct kunit *test,
			       struct dm_test_color_update_fixture *f,
			       const enum drm_colorop_type *types,
			       const enum drm_colorop_curve_1d_type *curves,
			       const bool *bypass,
			       int count)
{
	struct drm_colorop **colorops;
	int i;

	f->state->colorops = kunit_kcalloc(test, count, sizeof(*f->state->colorops),
					   GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f->state->colorops);
	f->adev->ddev.mode_config.num_colorop = count;

	colorops = kunit_kcalloc(test, count, sizeof(*colorops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, colorops);

	for (i = 0; i < count; i++)
		colorops[i] = dm_test_colorop_pipeline_add(test, f, i, types[i], curves[i], bypass[i]);

	for (i = 0; i < count - 1; i++)
		colorops[i]->next = colorops[i + 1];

	return colorops[0];
}

/**
 * dm_test_update_plane_color_mgmt_colorop_bypass_pipeline - bypassed pipeline succeeds
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_bypass_pipeline(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, true, true, true, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass, ARRAY_SIZE(types));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_missing_multiplier - missing second op falls back
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_missing_multiplier(struct kunit *test)
{
	static const enum drm_colorop_type types[] = { DRM_COLOROP_1D_CURVE };
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = { true };
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass, ARRAY_SIZE(types));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_missing_3x4 - missing third op falls back
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_missing_3x4(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = { true, true };
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass, ARRAY_SIZE(types));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base, f.dc_plane_state),
			0);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_truncated - truncated pipelines fall back
 * @test: KUnit test context
 *
 * Covers each pipeline exit after the 3x4 matrix while all present colorops
 * remain bypassed, avoiding the floating-point color calculation paths.
 */
static void dm_test_update_plane_color_mgmt_colorop_truncated(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, true, true, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct dc_plane_state *dc_plane_state = f.dc_plane_state;
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct dm_crtc_state *crtc_state = f.crtc_state;
	int count, ret;

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;

	for (count = 3; count <= ARRAY_SIZE(types); count++) {
		f.dm_plane_state->base.color_pipeline =
			dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass, count);
		memset(f.dc_plane_state, 0, sizeof(*f.dc_plane_state));

		ret = amdgpu_dm_update_plane_color_mgmt(crtc_state, plane_state, dc_plane_state);
		KUNIT_EXPECT_EQ_MSG(test, ret, 0, "pipeline length %d should fall back", count);
		KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
		KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
		KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
	}
}

/**
 * dm_test_update_plane_color_mgmt_colorop_no_3dlut_hw - no 3D LUT skips 3D ops
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_no_3dlut_hw(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, true, true, false, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass,
					       ARRAY_SIZE(types));
	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = false;

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_update_plane_color_mgmt(f.crtc_state, &f.dm_plane_state->base,
						  f.dc_plane_state),
		0);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_shaper - enabled shaper TF and LUT succeed
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_shaper(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, false, false, true, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_colorop_state *shaper_lut_state;
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct drm_property_blob *blob;
	int ret;

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass,
					       ARRAY_SIZE(types));
	shaper_lut_state = f.state->colorops[4].new_state;
	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->length = MAX_COLOR_LUT_ENTRIES * sizeof(struct drm_color_lut32);
	blob->data = kunit_kzalloc(test, blob->length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob->data);
	shaper_lut_state->data = blob;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.shaper_enable);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->cm.shaper_func.type,
			(int)TF_TYPE_DISTRIBUTED_POINTS);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_3dlut - enabled 3D LUT succeeds
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_3dlut(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, true, true, false, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_colorop_state *lut3d_state;
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct drm_property_blob *blob;
	int ret;

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass,
					       ARRAY_SIZE(types));
	lut3d_state = f.state->colorops[5].new_state;
	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->length = 5 * sizeof(struct drm_color_lut32);
	blob->data = kunit_kzalloc(test, blob->length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob->data);
	lut3d_state->data = blob;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->cm.shaper_func.type,
			(int)TF_TYPE_DISTRIBUTED_POINTS);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->cm.shaper_func.tf,
			(int)TRANSFER_FUNCTION_LINEAR);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_3dlut_no_data - empty 3D LUT falls back
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_3dlut_no_data(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
		DRM_COLOROP_3D_LUT,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = {
		true, true, true, true, true, false, true, true,
	};
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	int ret;

	f.adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass,
					       ARRAY_SIZE(types));

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, f.dc_plane_state->cm.flags.bits.lut3d_enable);
}

/**
 * dm_test_update_plane_color_mgmt_colorop_blend - enabled blend TF and LUT succeed
 * @test: KUnit test context
 */
static void dm_test_update_plane_color_mgmt_colorop_blend(struct kunit *test)
{
	static const enum drm_colorop_type types[] = {
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_MULTIPLIER,
		DRM_COLOROP_CTM_3X4,
		DRM_COLOROP_1D_CURVE,
		DRM_COLOROP_1D_LUT,
	};
	static const enum drm_colorop_curve_1d_type curves[] = {
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
		DRM_COLOROP_1D_CURVE_SRGB_EOTF,
	};
	static const bool bypass[] = { true, true, true, false, false };
	struct dm_test_color_update_fixture f = dm_test_color_update_setup(test);
	struct drm_colorop_state *blend_lut_state;
	struct drm_plane_state *plane_state = &f.dm_plane_state->base;
	struct drm_property_blob *blob;
	int ret;

	f.dm_plane_state->base.color_pipeline =
		dm_test_colorop_pipeline_setup(test, &f, types, curves, bypass,
					       ARRAY_SIZE(types));
	blend_lut_state = f.state->colorops[4].new_state;
	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	blob->length = MAX_COLOR_LUT_ENTRIES * sizeof(struct drm_color_lut32);
	blob->data = kunit_kzalloc(test, blob->length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob->data);
	blend_lut_state->data = blob;

	ret = amdgpu_dm_update_plane_color_mgmt(f.crtc_state, plane_state,
						f.dc_plane_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, f.dc_plane_state->cm.flags.bits.blend_enable);
	KUNIT_EXPECT_EQ(test, (int)f.dc_plane_state->cm.blend_func.type,
			(int)TF_TYPE_DISTRIBUTED_POINTS);
}

static struct kunit_case dm_color_test_cases[] = {
	/* amdgpu_dm_fixpt_from_s3132 */
	KUNIT_CASE(dm_test_fixpt_from_s3132_zero),
	KUNIT_CASE(dm_test_fixpt_from_s3132_one),
	KUNIT_CASE(dm_test_fixpt_from_s3132_negative_one),
	KUNIT_CASE(dm_test_fixpt_from_s3132_half),
	KUNIT_CASE(dm_test_fixpt_from_s3132_neg_half),
	/* __is_lut_linear */
	KUNIT_CASE(dm_test_is_lut_linear_with_linear_lut),
	KUNIT_CASE(dm_test_is_lut_linear_with_nonlinear_lut),
	KUNIT_CASE(dm_test_is_lut_linear_rgb_mismatch),
	/* __drm_ctm_to_dc_matrix */
	KUNIT_CASE(dm_test_drm_ctm_to_dc_matrix_identity),
	KUNIT_CASE(dm_test_drm_ctm_to_dc_matrix_negative),
	KUNIT_CASE(dm_test_drm_ctm_to_dc_matrix_4th_col_zero),
	/* __drm_ctm_3x4_to_dc_matrix */
	KUNIT_CASE(dm_test_drm_ctm_3x4_to_dc_matrix_identity),
	KUNIT_CASE(dm_test_drm_ctm_3x4_to_dc_matrix_offset),
	/* amdgpu_tf_to_dc_tf */
	KUNIT_CASE(dm_test_tf_to_dc_tf_default),
	KUNIT_CASE(dm_test_tf_to_dc_tf_identity),
	KUNIT_CASE(dm_test_tf_to_dc_tf_srgb),
	KUNIT_CASE(dm_test_tf_to_dc_tf_bt709),
	KUNIT_CASE(dm_test_tf_to_dc_tf_pq),
	KUNIT_CASE(dm_test_tf_to_dc_tf_gamma22),
	KUNIT_CASE(dm_test_tf_to_dc_tf_gamma24),
	KUNIT_CASE(dm_test_tf_to_dc_tf_gamma26),
	/* amdgpu_colorop_tf_to_dc_tf */
	KUNIT_CASE(dm_test_colorop_tf_to_dc_tf_srgb),
	KUNIT_CASE(dm_test_colorop_tf_to_dc_tf_pq),
	KUNIT_CASE(dm_test_colorop_tf_to_dc_tf_bt2020),
	KUNIT_CASE(dm_test_colorop_tf_to_dc_tf_gamma22),
	KUNIT_CASE(dm_test_colorop_tf_to_dc_tf_default),
	/* __drm_lut_to_dc_gamma */
	KUNIT_CASE(dm_test_drm_lut_to_dc_gamma_legacy_zero),
	KUNIT_CASE(dm_test_drm_lut_to_dc_gamma_legacy_max),
	KUNIT_CASE(dm_test_drm_lut_to_dc_gamma_legacy_channels),
	KUNIT_CASE(dm_test_drm_lut_to_dc_gamma_nonlegacy_zero),
	KUNIT_CASE(dm_test_drm_lut_to_dc_gamma_nonlegacy_max),
	/* __drm_lut32_to_dc_gamma */
	KUNIT_CASE(dm_test_drm_lut32_to_dc_gamma_zero),
	KUNIT_CASE(dm_test_drm_lut32_to_dc_gamma_max),
	KUNIT_CASE(dm_test_drm_lut32_to_dc_gamma_channels),
	/* __extract_blob_lut */
	KUNIT_CASE(dm_test_extract_blob_lut_null),
	KUNIT_CASE(dm_test_extract_blob_lut_valid),
	/* __extract_blob_lut32 */
	KUNIT_CASE(dm_test_extract_blob_lut32_null),
	KUNIT_CASE(dm_test_extract_blob_lut32_valid),
	/* __to_dc_lut3d_color */
	KUNIT_CASE(dm_test_to_dc_lut3d_color_zero),
	KUNIT_CASE(dm_test_to_dc_lut3d_color_max),
	KUNIT_CASE(dm_test_to_dc_lut3d_color_channels),
	/* __drm_3dlut_to_dc_3dlut */
	KUNIT_CASE(dm_test_3dlut_to_dc_3dlut_distribution),
	KUNIT_CASE(dm_test_3dlut_to_dc_3dlut_tetrahedral_17),
	KUNIT_CASE(dm_test_3dlut_to_dc_3dlut_green_blue),
	/* __to_dc_lut3d_32_color */
	KUNIT_CASE(dm_test_to_dc_lut3d_32_color_zero),
	KUNIT_CASE(dm_test_to_dc_lut3d_32_color_max),
	KUNIT_CASE(dm_test_to_dc_lut3d_32_color_channels),
	/* __drm_3dlut32_to_dc_3dlut */
	KUNIT_CASE(dm_test_3dlut32_to_dc_3dlut_distribution),
	KUNIT_CASE(dm_test_3dlut32_to_dc_3dlut_tetrahedral_17),
	KUNIT_CASE(dm_test_3dlut32_to_dc_3dlut_green_blue),
	/* amdgpu_dm_verify_lut_sizes */
	KUNIT_CASE(dm_test_verify_lut_sizes_null_luts),
	KUNIT_CASE(dm_test_verify_lut_sizes_valid_degamma),
	KUNIT_CASE(dm_test_verify_lut_sizes_invalid_degamma),
	KUNIT_CASE(dm_test_verify_lut_sizes_valid_gamma_atomic),
	KUNIT_CASE(dm_test_verify_lut_sizes_valid_gamma_legacy),
	KUNIT_CASE(dm_test_verify_lut_sizes_invalid_gamma),
	KUNIT_CASE(dm_test_verify_lut_sizes_both_valid),
	KUNIT_CASE(dm_test_verify_lut_sizes_invalid_degamma_valid_gamma),
	/* amdgpu_dm_atomic_lut3d */
	KUNIT_CASE(dm_test_atomic_lut3d_zero_size),
	KUNIT_CASE(dm_test_atomic_lut3d_nonzero_state_bits),
	KUNIT_CASE(dm_test_atomic_lut3d_data_forwarded),
	/* __set_colorop_3dlut */
	KUNIT_CASE(dm_test_set_colorop_3dlut_zero_size),
	KUNIT_CASE(dm_test_set_colorop_3dlut_nonzero_state_bits),
	KUNIT_CASE(dm_test_set_colorop_3dlut_data_forwarded),
	/* __set_tf_bypass */
	KUNIT_CASE(dm_test_set_tf_bypass),
	/* __set_tf_distributed_points */
	KUNIT_CASE(dm_test_set_tf_distributed_points_srgb),
	KUNIT_CASE(dm_test_set_tf_distributed_points_pq),
	/* Transfer-function calculation helpers */
	KUNIT_CASE(dm_test_set_legacy_tf_identity),
	KUNIT_CASE(dm_test_set_output_tf_linear),
	KUNIT_CASE(dm_test_set_output_tf_32_srgb_rom),
	KUNIT_CASE(dm_test_set_input_tf_srgb),
	KUNIT_CASE(dm_test_set_input_tf_32_srgb),
	KUNIT_CASE(dm_test_set_transfer_funcs_with_luts),
	/* amdgpu_dm_set_atomic_regamma */
	KUNIT_CASE(dm_test_set_atomic_regamma_bypass),
	KUNIT_CASE(dm_test_set_atomic_regamma_srgb),
	/* amdgpu_dm_atomic_shaper_lut */
	KUNIT_CASE(dm_test_atomic_shaper_lut_bypass),
	/* amdgpu_dm_atomic_blend_lut */
	KUNIT_CASE(dm_test_atomic_blend_lut_bypass),
	KUNIT_CASE(dm_test_atomic_shaper_blend_srgb),
	/* __set_colorop_in_tf_1d_curve */
	KUNIT_CASE(dm_test_set_colorop_in_tf_1d_curve_invalid_type),
	KUNIT_CASE(dm_test_set_colorop_in_tf_1d_curve_unsupported_curve),
	KUNIT_CASE(dm_test_set_colorop_in_tf_1d_curve_bypass),
	/* amdgpu_dm_init_color_mod */
	KUNIT_CASE(dm_test_init_color_mod),
	/* amdgpu_dm_verify_lut3d_size */
	KUNIT_CASE(dm_test_verify_lut3d_no_luts),
	KUNIT_CASE(dm_test_verify_lut3d_bad_shaper),
	KUNIT_CASE(dm_test_verify_lut3d_bad_lut3d),
	KUNIT_CASE(dm_test_verify_lut3d_valid),
	/* __set_dm_plane_colorop_multiplier */
	KUNIT_CASE(dm_test_colorop_multiplier_applied),
	KUNIT_CASE(dm_test_colorop_multiplier_no_match),
	/* __set_dm_plane_colorop_3x4_matrix */
	KUNIT_CASE(dm_test_colorop_3x4_matrix_applied),
	KUNIT_CASE(dm_test_colorop_3x4_matrix_bad_length),
	/* __set_dm_plane_colorop_degamma */
	KUNIT_CASE(dm_test_colorop_degamma_predefined),
	KUNIT_CASE(dm_test_colorop_degamma_no_match),
	/* CRTC and plane color management update paths */
	KUNIT_CASE(dm_test_check_crtc_color_mgmt_no_luts),
	KUNIT_CASE(dm_test_check_crtc_color_mgmt_legacy_regamma),
	KUNIT_CASE(dm_test_check_crtc_color_mgmt_degamma),
	KUNIT_CASE(dm_test_update_crtc_color_mgmt_no_ctm),
	KUNIT_CASE(dm_test_update_crtc_color_mgmt_ctm),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_bad_lut3d),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_fallback_defaults),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_maps_crtc_degamma),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_maps_crtc_degamma_lut),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_maps_bt709_degamma),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_plane_degamma_lut),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_rejects_dual_degamma),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_uses_color_caps),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_legacy_luts),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_plane_ctm),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_bypass_pipeline),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_missing_multiplier),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_missing_3x4),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_truncated),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_no_3dlut_hw),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_shaper),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_3dlut),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_3dlut_no_data),
	KUNIT_CASE(dm_test_update_plane_color_mgmt_colorop_blend),
	{}
};

static struct kunit_suite dm_color_test_suite = {
	.name = "amdgpu_dm_color",
	.test_cases = dm_color_test_cases,
};

kunit_test_suite(dm_color_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_color");
MODULE_AUTHOR("AMD");
