// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_wb.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_writeback.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_wb.h"
#include "amdgpu_dm_kunit_test_helpers.h"


/* Helper functions */

static struct drm_crtc_state *alloc_test_crtc_state(struct kunit *test,
						    int hdisplay, int vdisplay)
{
	struct drm_crtc_state *crtc_state;

	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);

	crtc_state->mode.hdisplay = hdisplay;
	crtc_state->mode.vdisplay = vdisplay;

	return crtc_state;
}

static struct drm_connector_state *alloc_test_conn_state(struct kunit *test,
							 int fb_width,
							 int fb_height,
							 u32 format)
{
	struct drm_connector_state *conn_state;
	struct drm_writeback_job *job;
	struct drm_framebuffer *fb;
	struct drm_format_info *fmt_info;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	fmt_info = kunit_kzalloc(test, sizeof(*fmt_info), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fmt_info);

	fb->width = fb_width;
	fb->height = fb_height;
	fmt_info->format = format;
	fb->format = fmt_info;

	job->fb = fb;
	conn_state->writeback_job = job;

	return conn_state;
}



/* Tests for amdgpu_dm_wb_encoder_atomic_check */

/**
 * dm_test_wb_atomic_check_no_job - Verify early return when no writeback job
 * @test: KUnit test context
 *
 * When conn_state->writeback_job is NULL, no writeback is requested and the
 * function should return 0 without further validation.
 */
static void dm_test_wb_atomic_check_no_job(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	/* No writeback_job — should return 0 */
	conn_state->writeback_job = NULL;
	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_wb_atomic_check_no_fb - Verify early return when job has no framebuffer
 * @test: KUnit test context
 *
 * When a writeback job exists but job->fb is NULL, the function should return 0
 * without validating dimensions or pixel format.
 */
static void dm_test_wb_atomic_check_no_fb(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_writeback_job *job;
	int ret;

	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);

	/* writeback_job exists but no fb — should return 0 */
	job->fb = NULL;
	conn_state->writeback_job = job;
	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_wb_atomic_check_valid - Verify success with matching size and supported format
 * @test: KUnit test context
 *
 * When the framebuffer dimensions match the CRTC mode and the pixel format is
 * in the supported formats list, the function should return 0.
 */
static void dm_test_wb_atomic_check_valid(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = alloc_test_conn_state(test, 1920, 1080,
					   DRM_FORMAT_XRGB2101010);

	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_wb_atomic_check_size_mismatch - Verify rejection when both dimensions differ
 * @test: KUnit test context
 *
 * When both framebuffer width and height differ from the CRTC mode, the
 * function should return -EINVAL.
 */
static void dm_test_wb_atomic_check_size_mismatch(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	/* FB is 3840x2160 but mode is 1920x1080 */
	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = alloc_test_conn_state(test, 3840, 2160,
					   DRM_FORMAT_XRGB2101010);

	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_wb_atomic_check_width_mismatch - Verify rejection when width alone differs
 * @test: KUnit test context
 *
 * When only the framebuffer width differs from the CRTC mode hdisplay, the
 * function should return -EINVAL.
 */
static void dm_test_wb_atomic_check_width_mismatch(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	/* Width doesn't match */
	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = alloc_test_conn_state(test, 1280, 1080,
					   DRM_FORMAT_XRGB2101010);

	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_wb_atomic_check_height_mismatch - Verify rejection when height alone differs
 * @test: KUnit test context
 *
 * When only the framebuffer height differs from the CRTC mode vdisplay, the
 * function should return -EINVAL.
 */
static void dm_test_wb_atomic_check_height_mismatch(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	/* Height doesn't match */
	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = alloc_test_conn_state(test, 1920, 720,
					   DRM_FORMAT_XRGB2101010);

	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_wb_atomic_check_invalid_format - Verify rejection of unsupported pixel format
 * @test: KUnit test context
 *
 * When the framebuffer dimensions match but the pixel format is not in
 * amdgpu_dm_wb_formats[], the function should return -EINVAL.
 */
static void dm_test_wb_atomic_check_invalid_format(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;

	/* Correct size but unsupported format */
	crtc_state = alloc_test_crtc_state(test, 1920, 1080);
	conn_state = alloc_test_conn_state(test, 1920, 1080,
					   DRM_FORMAT_XRGB8888);

	ret = amdgpu_dm_wb_encoder_atomic_check(NULL, crtc_state, conn_state);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Tests for amdgpu_dm_wb_connector_get_modes using DRM mock */

static const struct drm_connector_funcs dm_wb_test_connector_funcs = {
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.reset = drm_atomic_helper_connector_reset,
};

/**
 * dm_test_wb_get_modes_returns_modes - Verify at least one mode is returned
 * @test: KUnit test context
 *
 * Uses a DRM mock connector to verify that amdgpu_dm_wb_connector_get_modes()
 * populates the connector with at least one display mode.
 */
static void dm_test_wb_get_modes_returns_modes(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	int count;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_wb_test_connector_funcs,
			    DRM_MODE_CONNECTOR_VIRTUAL, NULL);

	count = amdgpu_dm_wb_connector_get_modes(connector);

	/* drm_add_modes_noedid should return at least one mode */
	KUNIT_EXPECT_GT(test, count, 0);
}

/**
 * dm_test_wb_get_modes_bounded_by_max - Verify all modes are within max resolution
 * @test: KUnit test context
 *
 * Uses a DRM mock connector to verify that all modes returned by
 * amdgpu_dm_wb_connector_get_modes() have hdisplay <= 3840 and
 * vdisplay <= 2160, matching the DWB hardware maximum.
 */
static void dm_test_wb_get_modes_bounded_by_max(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	struct drm_display_mode *mode;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_wb_test_connector_funcs,
			    DRM_MODE_CONNECTOR_VIRTUAL, NULL);

	amdgpu_dm_wb_connector_get_modes(connector);

	/* All modes must fit within 3840x2160 */
	list_for_each_entry(mode, &connector->probed_modes, head) {
		KUNIT_EXPECT_LE(test, mode->hdisplay, 3840);
		KUNIT_EXPECT_LE(test, mode->vdisplay, 2160);
	}
}

/* Tests for amdgpu_dm_wb_connector_init using DRM mock */

/**
 * dm_test_wb_connector_init_success - Verify writeback connector initialization
 * @test: KUnit test context
 *
 * Uses a DRM mock device embedded in struct amdgpu_device to verify that
 * amdgpu_dm_wb_connector_init() initializes the writeback connector, stores
 * the DC link, installs connector state through reset, and wires the expected
 * DRM callbacks.
 */
static void dm_test_wb_connector_init_success(struct kunit *test)
{
	struct amdgpu_dm_wb_connector *wbcon;
	struct amdgpu_display_manager *dm;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;
	int ret;

	adev = dm_kunit_alloc_adev(test);
	adev->mode_info.num_crtc = 1;
	dm = &adev->dm;
	dm->adev = adev;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	dc->links[0] = link;
	dm->dc = dc;

	wbcon = kunit_kzalloc(test, sizeof(*wbcon), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, wbcon);

	ret = amdgpu_dm_wb_connector_init(dm, wbcon, 0);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_PTR_EQ(test, wbcon->link, link);
	KUNIT_EXPECT_TRUE(test, wbcon->base.base.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, wbcon->base.base.helper_private != NULL);
	KUNIT_EXPECT_TRUE(test, wbcon->base.base.state != NULL);
	KUNIT_EXPECT_TRUE(test, wbcon->base.encoder.funcs != NULL);
	KUNIT_EXPECT_EQ(test, wbcon->base.encoder.possible_crtcs, 0x1);
}

/* Tests for amdgpu_dm_wb_prepare_job / amdgpu_dm_wb_cleanup_job */

/**
 * dm_test_wb_prepare_job_no_fb - Verify prepare_job early return without a framebuffer
 * @test: KUnit test context
 *
 * When job->fb is NULL there is nothing to pin, so amdgpu_dm_wb_prepare_job()
 * must return 0 without touching any buffer object.
 */
static void dm_test_wb_prepare_job_no_fb(struct kunit *test)
{
	struct drm_writeback_job *job;
	int ret;

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);

	job->fb = NULL;
	ret = amdgpu_dm_wb_prepare_job(NULL, job);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_wb_cleanup_job_no_fb - Verify cleanup_job early return without a framebuffer
 * @test: KUnit test context
 *
 * When job->fb is NULL there is nothing to unpin, so amdgpu_dm_wb_cleanup_job()
 * must return immediately.
 */
static void dm_test_wb_cleanup_job_no_fb(struct kunit *test)
{
	struct drm_writeback_job *job;

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);

	job->fb = NULL;
	/* Should return without dereferencing any buffer object. */
	amdgpu_dm_wb_cleanup_job(NULL, job);
}

static struct kunit_case dm_wb_test_cases[] = {
	/* amdgpu_dm_wb_encoder_atomic_check */
	KUNIT_CASE(dm_test_wb_atomic_check_no_job),
	KUNIT_CASE(dm_test_wb_atomic_check_no_fb),
	KUNIT_CASE(dm_test_wb_atomic_check_valid),
	KUNIT_CASE(dm_test_wb_atomic_check_size_mismatch),
	KUNIT_CASE(dm_test_wb_atomic_check_width_mismatch),
	KUNIT_CASE(dm_test_wb_atomic_check_height_mismatch),
	KUNIT_CASE(dm_test_wb_atomic_check_invalid_format),
	/* amdgpu_dm_wb_connector_get_modes */
	KUNIT_CASE(dm_test_wb_get_modes_returns_modes),
	KUNIT_CASE(dm_test_wb_get_modes_bounded_by_max),
	/* amdgpu_dm_wb_connector_init */
	KUNIT_CASE(dm_test_wb_connector_init_success),
	/* amdgpu_dm_wb_prepare_job / amdgpu_dm_wb_cleanup_job */
	KUNIT_CASE(dm_test_wb_prepare_job_no_fb),
	KUNIT_CASE(dm_test_wb_cleanup_job_no_fb),
	{}
};

static struct kunit_suite dm_wb_test_suite = {
	.name = "amdgpu_dm_wb",
	.test_cases = dm_wb_test_cases,
};

kunit_test_suite(dm_wb_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_wb");
