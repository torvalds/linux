// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_crc.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include <drm/drm_modeset_lock.h>

#include "dc.h"
#include "core_types.h"
#include "logger_types.h"
#include "opp.h"
#include "timing_generator.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_crc.h"
#include "amdgpu_dm_kunit_test_helpers.h"

struct dm_test_crc_dc_fixture {
	struct dc *dc;
	struct dc_context *dc_ctx;
	struct dc_state *dc_state;
	struct dc_stream_state *stream;
	struct dc_link *link;
	struct timing_generator *tg;
	struct output_pixel_processor *opp;
	struct dal_logger *logger;
	struct dm_crtc_state *dm_state;
	struct crc_params crc_params;
	enum dc_dynamic_expansion dyn_expansion;
	enum dc_dither_option dither_option;
	uint32_t crc_r;
	uint32_t crc_g;
	uint32_t crc_b;
	bool configure_crc_called;
	bool dyn_expansion_called;
	bool bit_depth_reduction_called;
	bool configure_crc_return;
	bool get_crc_called;
	bool get_crc_return;
};

static struct dm_test_crc_dc_fixture *dm_test_crc_dc_ctx;

static bool dm_test_configure_crc(struct timing_generator *tg,
					  const struct crc_params *params)
{
	if (!dm_test_crc_dc_ctx)
		return false;

	dm_test_crc_dc_ctx->configure_crc_called = true;
	dm_test_crc_dc_ctx->crc_params = *params;

	return dm_test_crc_dc_ctx->configure_crc_return;
}

static bool dm_test_get_crc(struct timing_generator *tg, uint8_t idx,
				    uint32_t *r_cr, uint32_t *g_y, uint32_t *b_cb)
{
	if (!dm_test_crc_dc_ctx)
		return false;

	dm_test_crc_dc_ctx->get_crc_called = true;
	*r_cr = dm_test_crc_dc_ctx->crc_r;
	*g_y = dm_test_crc_dc_ctx->crc_g;
	*b_cb = dm_test_crc_dc_ctx->crc_b;

	return dm_test_crc_dc_ctx->get_crc_return;
}

static void dm_test_opp_set_dyn_expansion(struct output_pixel_processor *opp,
						  enum dc_color_space color_sp,
						  enum dc_color_depth color_dpth,
						  enum signal_type signal)
{
	if (!dm_test_crc_dc_ctx)
		return;

	dm_test_crc_dc_ctx->dyn_expansion_called = true;
	dm_test_crc_dc_ctx->dyn_expansion = opp->dyn_expansion;
}

static void dm_test_opp_program_bit_depth_reduction(struct output_pixel_processor *opp,
							    const struct bit_depth_reduction_params *params)
{
	if (!dm_test_crc_dc_ctx)
		return;

	dm_test_crc_dc_ctx->bit_depth_reduction_called = true;
}

static const struct timing_generator_funcs dm_test_tg_funcs = {
	.configure_crc = dm_test_configure_crc,
	.get_crc = dm_test_get_crc,
};

static const struct opp_funcs dm_test_opp_funcs = {
	.opp_set_dyn_expansion = dm_test_opp_set_dyn_expansion,
	.opp_program_bit_depth_reduction = dm_test_opp_program_bit_depth_reduction,
};

static struct dm_test_crc_dc_fixture *dm_test_alloc_crc_dc_fixture(struct kunit *test,
								   struct amdgpu_device *adev)
{
	struct dm_test_crc_dc_fixture *fixture;
	struct pipe_ctx *pipe;

	fixture = kunit_kzalloc(test, sizeof(*fixture), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture);

	fixture->dm_state = kunit_kzalloc(test, sizeof(*fixture->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->dm_state);
	fixture->dc = kunit_kzalloc(test, sizeof(*fixture->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->dc);
	fixture->dc_ctx = kunit_kzalloc(test, sizeof(*fixture->dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->dc_ctx);
	fixture->dc_state = kunit_kzalloc(test, sizeof(*fixture->dc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->dc_state);
	fixture->tg = kunit_kzalloc(test, sizeof(*fixture->tg), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->tg);
	fixture->opp = kunit_kzalloc(test, sizeof(*fixture->opp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->opp);
	fixture->logger = kunit_kzalloc(test, sizeof(*fixture->logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fixture->logger);
	fixture->link = dm_kunit_alloc_link(test);
	fixture->stream = dm_kunit_alloc_stream(test, fixture->link);

	mutex_init(&adev->dm.dc_lock);
	adev->dm.dc = fixture->dc;
	fixture->dc->ctx = fixture->dc_ctx;
	fixture->dc->current_state = fixture->dc_state;
	fixture->dc_ctx->dc = fixture->dc;
	fixture->dc_ctx->logger = fixture->logger;
	fixture->link->dc = fixture->dc;
	fixture->stream->ctx = fixture->dc_ctx;
	fixture->stream->link = fixture->link;
	fixture->stream->timing.h_addressable = 1920;
	fixture->stream->timing.v_addressable = 1080;
	fixture->configure_crc_return = true;
	fixture->tg->funcs = &dm_test_tg_funcs;
	fixture->opp->funcs = &dm_test_opp_funcs;
	fixture->dm_state->stream = fixture->stream;

	pipe = &fixture->dc_state->res_ctx.pipe_ctx[0];
	pipe->stream = fixture->stream;
	pipe->pipe_idx = 0;
	pipe->stream_res.tg = fixture->tg;
	pipe->stream_res.opp = fixture->opp;

	return fixture;
}

static struct amdgpu_crtc *dm_test_alloc_crc_crtc(struct kunit *test,
							 struct amdgpu_device *adev)
{
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	acrtc->base.dev = &adev->ddev;
	drm_modeset_lock_init(&acrtc->base.mutex);
	spin_lock_init(&acrtc->base.commit_lock);
	INIT_LIST_HEAD(&acrtc->base.commit_list);

	return acrtc;
}

static void dm_test_parse_crc_source_none(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_NONE, dm_parse_crc_source("none"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_NONE, dm_parse_crc_source(NULL));
}

static void dm_test_parse_crc_source_crtc(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC, dm_parse_crc_source("crtc"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC, dm_parse_crc_source("auto"));
}

static void dm_test_parse_crc_source_dprx(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_DPRX, dm_parse_crc_source("dprx"));
}

static void dm_test_parse_crc_source_crtc_dither(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER,
			dm_parse_crc_source("crtc dither"));
}

static void dm_test_parse_crc_source_dprx_dither(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
			dm_parse_crc_source("dprx dither"));
}

static void dm_test_parse_crc_source_invalid(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source("invalid"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source("unknown"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source(""));
}

static void dm_test_is_crc_source_crtc(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_is_crc_source_dprx(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_need_crc_dither(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_is_valid_crc_source(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_MAX));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

/**
 * dm_test_crtc_get_crc_sources() - Test available CRC source strings.
 * @test: KUnit test context.
 *
 * Verifies that amdgpu_dm_crtc_get_crc_sources() returns the static source
 * list and reports all debugfs source names.
 */
static void dm_test_crtc_get_crc_sources(struct kunit *test)
{
	const char *const *sources;
	size_t count = 0;

	sources = amdgpu_dm_crtc_get_crc_sources(NULL, &count);

	KUNIT_ASSERT_NOT_NULL(test, sources);
	KUNIT_EXPECT_EQ(test, count, 6);
	KUNIT_EXPECT_STREQ(test, sources[0], "none");
	KUNIT_EXPECT_STREQ(test, sources[1], "crtc");
	KUNIT_EXPECT_STREQ(test, sources[2], "crtc dither");
	KUNIT_EXPECT_STREQ(test, sources[3], "dprx");
	KUNIT_EXPECT_STREQ(test, sources[4], "dprx dither");
	KUNIT_EXPECT_STREQ(test, sources[5], "auto");
}

/**
 * dm_test_crtc_verify_crc_source_valid() - Test valid CRC source verification.
 * @test: KUnit test context.
 *
 * Verifies that valid source strings return success and request three CRC
 * values.
 */
static void dm_test_crtc_verify_crc_source_valid(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	size_t values_cnt = 0;
	int ret;

	ret = amdgpu_dm_crtc_verify_crc_source(&acrtc->base, "crtc", &values_cnt);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, values_cnt, 3);
}

/**
 * dm_test_crtc_verify_crc_source_invalid() - Test invalid CRC source verification.
 * @test: KUnit test context.
 *
 * Verifies that invalid source strings are rejected without changing the
 * caller-provided values count.
 */
static void dm_test_crtc_verify_crc_source_invalid(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	size_t values_cnt = 7;
	int ret;

	ret = amdgpu_dm_crtc_verify_crc_source(&acrtc->base, "bad", &values_cnt);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, values_cnt, 7);
}

/**
 * dm_test_crtc_configure_crc_source_no_stream() - Test missing stream handling.
 * @test: KUnit test context.
 *
 * Verifies that configuration is deferred/rejected before any DC access when
 * the CRTC state does not have a stream.
 */
static void dm_test_crtc_configure_crc_source_no_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_crtc_state *dm_state;
	int ret;

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_CRTC);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_crtc_configure_crc_source_dprx() - Test DPRX configure path.
 * @test: KUnit test context.
 *
 * Verifies that a DPRX source can be configured with an empty DC resource
 * state, covering the non-CRTC path that only updates dither/dynamic expansion.
 */
static void dm_test_crtc_configure_crc_source_dprx(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_DPRX);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_TRUE(test, fixture->dyn_expansion_called);
	KUNIT_EXPECT_EQ(test, fixture->dyn_expansion, DYN_EXPANSION_DISABLE);
	KUNIT_EXPECT_TRUE(test, fixture->bit_depth_reduction_called);
}

/**
 * dm_test_crtc_configure_crc_source_dprx_dither() - Test DPRX dither path.
 * @test: KUnit test context.
 *
 * Verifies that a DPRX dither source reaches the default dither/dynamic
 * expansion path without requiring timing-generator callbacks.
 */
static void dm_test_crtc_configure_crc_source_dprx_dither(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_TRUE(test, fixture->dyn_expansion_called);
	KUNIT_EXPECT_EQ(test, fixture->dyn_expansion, DYN_EXPANSION_AUTO);
	KUNIT_EXPECT_TRUE(test, fixture->bit_depth_reduction_called);
}

/**
 * dm_test_crtc_configure_crc_source_crtc() - Test CRTC enable path.
 * @test: KUnit test context.
 *
 * Verifies that a CRTC source enables DC CRC capture and disables dither.
 */
static void dm_test_crtc_configure_crc_source_crtc(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_CRTC);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_TRUE(test, fixture->crc_params.enable);
	KUNIT_EXPECT_TRUE(test, fixture->crc_params.continuous_mode);
	KUNIT_EXPECT_TRUE(test, fixture->crc_params.reset);
	KUNIT_EXPECT_EQ(test, fixture->crc_params.windowa_x_end, 1920);
	KUNIT_EXPECT_EQ(test, fixture->crc_params.windowa_y_end, 1080);
	KUNIT_EXPECT_TRUE(test, fixture->dyn_expansion_called);
	KUNIT_EXPECT_EQ(test, fixture->dyn_expansion, DYN_EXPANSION_DISABLE);
	KUNIT_EXPECT_TRUE(test, fixture->bit_depth_reduction_called);
}

/**
 * dm_test_crtc_configure_crc_source_crtc_dcn36_poly() - Test CRC poly select.
 * @test: KUnit test context.
 *
 * Verifies that DCN3.6+ configurations use the CRTC-selected CRC polynomial.
 */
static void dm_test_crtc_configure_crc_source_crtc_dcn36_poly(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 6, 0);
	acrtc->dm_irq_params.crc_poly_mode = CRC_POLY_MODE_32;
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_CRTC);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_EQ(test, fixture->crc_params.crc_poly_mode, CRC_POLY_MODE_32);
}

/**
 * dm_test_crtc_configure_crc_source_crtc_configure_fails() - Test failure path.
 * @test: KUnit test context.
 *
 * Verifies that a DC CRC configuration failure is reported as -EINVAL and
 * stops before dither/dynamic expansion programming.
 */
static void dm_test_crtc_configure_crc_source_crtc_configure_fails(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	fixture->configure_crc_return = false;
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_CRTC);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_TRUE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_FALSE(test, fixture->dyn_expansion_called);
	KUNIT_EXPECT_FALSE(test, fixture->bit_depth_reduction_called);
}

/**
 * dm_test_crtc_configure_crc_source_none() - Test CRC disable path.
 * @test: KUnit test context.
 *
 * Verifies that source NONE disables DC CRC capture and restores default
 * dither/dynamic expansion.
 */
static void dm_test_crtc_configure_crc_source_none(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc = dm_test_alloc_crc_crtc(test, adev);
	struct dm_test_crc_dc_fixture *fixture;
	int ret;

	fixture = dm_test_alloc_crc_dc_fixture(test, adev);
	dm_test_crc_dc_ctx = fixture;

	ret = amdgpu_dm_crtc_configure_crc_source(&acrtc->base, fixture->dm_state,
						   AMDGPU_DM_PIPE_CRC_SOURCE_NONE);
	dm_test_crc_dc_ctx = NULL;

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, fixture->configure_crc_called);
	KUNIT_EXPECT_FALSE(test, fixture->crc_params.enable);
	KUNIT_EXPECT_FALSE(test, fixture->crc_params.continuous_mode);
	KUNIT_EXPECT_TRUE(test, fixture->crc_params.reset);
	KUNIT_EXPECT_TRUE(test, fixture->dyn_expansion_called);
	KUNIT_EXPECT_EQ(test, fixture->dyn_expansion, DYN_EXPANSION_AUTO);
	KUNIT_EXPECT_TRUE(test, fixture->bit_depth_reduction_called);
}

/**
 * dm_test_need_dp_aux() - Test dm_need_dp_aux().
 * @test: KUnit test context.
 *
 * Verifies that dm_need_dp_aux() returns true when the transition starts or
 * stops a DPRX CRC source (requiring the DP AUX handle), and false for
 * non-DPRX transitions such as CRTC or NONE→NONE.
 */
static void dm_test_need_dp_aux(struct kunit *test)
{
	/* Starting a DPRX source always needs AUX, regardless of current source */
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* Stopping a DPRX source (NONE requested, DPRX was active) needs AUX */
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	/* CRTC transitions do not need AUX */
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
}

/**
 * dm_test_crc_source_should_start_dprx() - Test dm_crc_source_should_start_dprx().
 * @test: KUnit test context.
 *
 * Verifies that dm_crc_source_should_start_dprx() returns true only when CRC
 * is transitioning from off (!enabled) to a DPRX source (enable &&
 * is_dprx(source)), and false for all other combinations including
 * already-enabled or non-DPRX targets.
 */
static void dm_test_crc_source_should_start_dprx(struct kunit *test)
{
	/* CRC off → DPRX: should start */
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* CRC already on (any source) → DPRX: should NOT start (already enabled) */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));

	/* CRC off → CRTC: not a DPRX start */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* Disabling: should not start */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
}

/**
 * dm_test_crc_source_should_stop_dprx() - Test dm_crc_source_should_stop_dprx().
 * @test: KUnit test context.
 *
 * Verifies that dm_crc_source_should_stop_dprx() returns true only when CRC
 * is transitioning from a DPRX source (enabled && is_dprx(cur_crc_src)) to
 * off (!enable), and false for non-DPRX disables, DPRX starts, and no-op
 * transitions.
 */
static void dm_test_crc_source_should_stop_dprx(struct kunit *test)
{
	/* DPRX → off: should stop */
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	/* CRTC → off: not a DPRX stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));

	/* off → DPRX: not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* DPRX → DPRX: no transition, not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));

	/* off → off: not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
}

static struct kunit_case dm_crc_test_cases[] = {
	/* dm_parse_crc_source() */
	KUNIT_CASE(dm_test_parse_crc_source_none),
	KUNIT_CASE(dm_test_parse_crc_source_crtc),
	KUNIT_CASE(dm_test_parse_crc_source_dprx),
	KUNIT_CASE(dm_test_parse_crc_source_crtc_dither),
	KUNIT_CASE(dm_test_parse_crc_source_dprx_dither),
	KUNIT_CASE(dm_test_parse_crc_source_invalid),
	/* dm_is_crc_source_crtc() */
	KUNIT_CASE(dm_test_is_crc_source_crtc),
	/* dm_is_crc_source_dprx() */
	KUNIT_CASE(dm_test_is_crc_source_dprx),
	/* dm_need_crc_dither() */
	KUNIT_CASE(dm_test_need_crc_dither),
	/* amdgpu_dm_is_valid_crc_source() */
	KUNIT_CASE(dm_test_is_valid_crc_source),
	/* amdgpu_dm_crtc_get_crc_sources() */
	KUNIT_CASE(dm_test_crtc_get_crc_sources),
	/* amdgpu_dm_crtc_verify_crc_source() */
	KUNIT_CASE(dm_test_crtc_verify_crc_source_valid),
	KUNIT_CASE(dm_test_crtc_verify_crc_source_invalid),
	/* amdgpu_dm_crtc_configure_crc_source() */
	KUNIT_CASE(dm_test_crtc_configure_crc_source_no_stream),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_dprx),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_dprx_dither),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_crtc),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_crtc_dcn36_poly),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_crtc_configure_fails),
	KUNIT_CASE(dm_test_crtc_configure_crc_source_none),
	/* dm_need_dp_aux() */
	KUNIT_CASE(dm_test_need_dp_aux),
	/* dm_crc_source_should_start_dprx() */
	KUNIT_CASE(dm_test_crc_source_should_start_dprx),
	/* dm_crc_source_should_stop_dprx() */
	KUNIT_CASE(dm_test_crc_source_should_stop_dprx),
	{}
};

static struct kunit_suite dm_crc_test_suite = {
	.name = "amdgpu_dm_crc",
	.test_cases = dm_crc_test_cases,
};

kunit_test_suite(dm_crc_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_crc");
MODULE_AUTHOR("AMD");
