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
#include <drm/drm_managed.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_writeback.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_wb.h"
#include "amdgpu_dm_kunit_test_helpers.h"

struct dm_wb_test_bo {
	struct amdgpu_bo bo;
	struct amdgpu_device *adev;
	int reserve_ret;
	int reserve_fences_ret;
	int pin_ret;
	int alloc_gart_ret;
	u64 gpu_offset;
	unsigned int reserve_count;
	unsigned int reserve_fences_count;
	unsigned int pin_count;
	unsigned int alloc_gart_count;
	unsigned int unreserve_count;
	unsigned int ref_count;
	unsigned int unpin_count;
	unsigned int unref_count;
	unsigned int call_seq;
	unsigned int reserve_seq;
	unsigned int unreserve_seq;
	unsigned int unpin_seq;
	unsigned int unref_seq;
};

static struct dm_wb_test_bo *to_dm_wb_test_bo(struct amdgpu_bo *bo)
{
	return container_of(bo, struct dm_wb_test_bo, bo);
}

static int dm_wb_test_reserve(struct amdgpu_bo *bo, bool interruptible)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(bo);

	test_bo->reserve_count++;
	test_bo->reserve_seq = ++test_bo->call_seq;
	return test_bo->reserve_ret;
}

static int dm_wb_test_reserve_fences(struct dma_resv *resv,
				     unsigned int num_fences)
{
	struct dm_wb_test_bo *test_bo;

	test_bo = container_of(resv, struct dm_wb_test_bo, bo.tbo.base._resv);
	test_bo->reserve_fences_count++;
	return test_bo->reserve_fences_ret;
}

static int dm_wb_test_pin(struct amdgpu_bo *bo, u32 domain)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(bo);

	test_bo->pin_count++;
	return test_bo->pin_ret;
}

static int dm_wb_test_alloc_gart(struct ttm_buffer_object *tbo)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(ttm_to_amdgpu_bo(tbo));

	test_bo->alloc_gart_count++;
	return test_bo->alloc_gart_ret;
}

static void dm_wb_test_unreserve(struct amdgpu_bo *bo)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(bo);

	test_bo->unreserve_count++;
	test_bo->unreserve_seq = ++test_bo->call_seq;
}

static u64 dm_wb_test_gpu_offset(struct amdgpu_bo *bo)
{
	return to_dm_wb_test_bo(bo)->gpu_offset;
}

static struct amdgpu_bo *dm_wb_test_ref(struct amdgpu_bo *bo)
{
	to_dm_wb_test_bo(bo)->ref_count++;
	return bo;
}

static void dm_wb_test_unpin(struct amdgpu_bo *bo)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(bo);

	test_bo->unpin_count++;
	test_bo->unpin_seq = ++test_bo->call_seq;
}

static void dm_wb_test_unref(struct amdgpu_bo **bo)
{
	struct dm_wb_test_bo *test_bo = to_dm_wb_test_bo(*bo);

	test_bo->unref_count++;
	test_bo->unref_seq = ++test_bo->call_seq;
	*bo = NULL;
}

static const struct amdgpu_dm_wb_kunit_ops dm_wb_test_ops = {
	.reserve = dm_wb_test_reserve,
	.reserve_fences = dm_wb_test_reserve_fences,
	.pin = dm_wb_test_pin,
	.alloc_gart = dm_wb_test_alloc_gart,
	.unreserve = dm_wb_test_unreserve,
	.gpu_offset = dm_wb_test_gpu_offset,
	.ref = dm_wb_test_ref,
	.unpin = dm_wb_test_unpin,
	.unref = dm_wb_test_unref,
};

static void dm_wb_test_reset_ops(void *unused)
{
	amdgpu_dm_wb_kunit_set_ops(NULL);
}

static struct drm_writeback_job *dm_wb_test_alloc_job(struct kunit *test,
						      struct dm_wb_test_bo **test_bo)
{
	struct amdgpu_framebuffer *afb;
	struct drm_writeback_job *job;

	*test_bo = kunit_kzalloc(test, sizeof(**test_bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, *test_bo);
	(*test_bo)->adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_NULL(test, (*test_bo)->adev);

	/* Let the driver's real BO and device lookups resolve to the fake BO. */
	(*test_bo)->bo.tbo.bdev = &(*test_bo)->adev->mman.bdev;
	(*test_bo)->bo.tbo.base.resv = &(*test_bo)->bo.tbo.base._resv;

	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, afb);
	afb->base.obj[0] = &(*test_bo)->bo.tbo.base;

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);
	job->fb = &afb->base;

	amdgpu_dm_wb_kunit_set_ops(&dm_wb_test_ops);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_wb_test_reset_ops, NULL), 0);

	return job;
}


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

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_wb_test_connector_funcs,
			    DRM_MODE_CONNECTOR_VIRTUAL, NULL);

	mutex_lock(&drm->mode_config.mutex);
	count = amdgpu_dm_wb_connector_get_modes(connector);
	mutex_unlock(&drm->mode_config.mutex);

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

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_wb_test_connector_funcs,
			    DRM_MODE_CONNECTOR_VIRTUAL, NULL);

	mutex_lock(&drm->mode_config.mutex);
	amdgpu_dm_wb_connector_get_modes(connector);
	mutex_unlock(&drm->mode_config.mutex);

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
 * dm_test_wb_prepare_job_success - Verify successful BO preparation
 * @test: KUnit test context
 *
 * The writeback BO should be reserved, pinned, mapped into GART, referenced,
 * and assigned its GPU address.
 */
static void dm_test_wb_prepare_job_success(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;
	struct amdgpu_framebuffer *afb;
	int ret;

	job = dm_wb_test_alloc_job(test, &test_bo);
	afb = to_amdgpu_framebuffer(job->fb);
	test_bo->gpu_offset = 0x12340000;

	ret = amdgpu_dm_wb_prepare_job(NULL, job);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, test_bo->reserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->reserve_fences_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->pin_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->alloc_gart_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->ref_count, 1);
	KUNIT_EXPECT_EQ(test, afb->address, test_bo->gpu_offset);
	KUNIT_EXPECT_TRUE(test, test_bo->bo.flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS);
}

/**
 * dm_test_wb_prepare_job_reserve_failure - Verify reserve errors are returned
 * @test: KUnit test context
 *
 * A BO reserve failure should stop preparation without attempting cleanup on
 * a BO that was never reserved.
 */
static void dm_test_wb_prepare_job_reserve_failure(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;
	int ret;

	job = dm_wb_test_alloc_job(test, &test_bo);
	test_bo->reserve_ret = -EBUSY;

	ret = amdgpu_dm_wb_prepare_job(NULL, job);

	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	KUNIT_EXPECT_EQ(test, test_bo->reserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->reserve_fences_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 0);
}

/**
 * dm_test_wb_prepare_job_fence_failure - Verify fence reservation cleanup
 * @test: KUnit test context
 *
 * Failure to reserve fence slots should release the BO reservation without
 * attempting to pin the BO.
 */
static void dm_test_wb_prepare_job_fence_failure(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;
	int ret;

	job = dm_wb_test_alloc_job(test, &test_bo);
	test_bo->reserve_fences_ret = -ENOMEM;

	ret = amdgpu_dm_wb_prepare_job(NULL, job);

	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	KUNIT_EXPECT_EQ(test, test_bo->reserve_fences_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->pin_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 1);
}

/**
 * dm_test_wb_prepare_job_pin_failure - Verify pin failure cleanup
 * @test: KUnit test context
 *
 * A pin failure should release the BO reservation without trying to unpin a
 * BO that was not successfully pinned.
 */
static void dm_test_wb_prepare_job_pin_failure(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;
	int ret;

	job = dm_wb_test_alloc_job(test, &test_bo);
	test_bo->pin_ret = -EINVAL;

	ret = amdgpu_dm_wb_prepare_job(NULL, job);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, test_bo->pin_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->alloc_gart_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unpin_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 1);
}

/**
 * dm_test_wb_prepare_job_gart_failure - Verify GART allocation cleanup
 * @test: KUnit test context
 *
 * A GART allocation failure should unpin and unreserve the BO.
 */
static void dm_test_wb_prepare_job_gart_failure(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;
	int ret;

	job = dm_wb_test_alloc_job(test, &test_bo);
	test_bo->alloc_gart_ret = -ENOMEM;

	ret = amdgpu_dm_wb_prepare_job(NULL, job);

	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	KUNIT_EXPECT_EQ(test, test_bo->alloc_gart_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unpin_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->ref_count, 0);
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

/**
 * dm_test_wb_cleanup_job_success - Verify successful BO cleanup
 * @test: KUnit test context
 *
 * Cleanup should reserve, unpin, unreserve, and drop the writeback BO
 * reference in order.
 */
static void dm_test_wb_cleanup_job_success(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;

	job = dm_wb_test_alloc_job(test, &test_bo);

	amdgpu_dm_wb_cleanup_job(NULL, job);

	KUNIT_EXPECT_EQ(test, test_bo->reserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unpin_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unref_count, 1);

	/* The BO stays reserved across the unpin, and is released last. */
	KUNIT_EXPECT_LT(test, test_bo->reserve_seq, test_bo->unpin_seq);
	KUNIT_EXPECT_LT(test, test_bo->unpin_seq, test_bo->unreserve_seq);
	KUNIT_EXPECT_LT(test, test_bo->unreserve_seq, test_bo->unref_seq);
}

/**
 * dm_test_wb_cleanup_job_reserve_failure - Verify cleanup reserve failure
 * @test: KUnit test context
 *
 * If cleanup cannot reserve the BO, it should leave the pin and reference
 * untouched for a later cleanup attempt.
 */
static void dm_test_wb_cleanup_job_reserve_failure(struct kunit *test)
{
	struct dm_wb_test_bo *test_bo;
	struct drm_writeback_job *job;

	job = dm_wb_test_alloc_job(test, &test_bo);
	test_bo->reserve_ret = -EBUSY;

	amdgpu_dm_wb_cleanup_job(NULL, job);

	KUNIT_EXPECT_EQ(test, test_bo->reserve_count, 1);
	KUNIT_EXPECT_EQ(test, test_bo->unpin_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unreserve_count, 0);
	KUNIT_EXPECT_EQ(test, test_bo->unref_count, 0);
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
	KUNIT_CASE(dm_test_wb_prepare_job_success),
	KUNIT_CASE(dm_test_wb_prepare_job_reserve_failure),
	KUNIT_CASE(dm_test_wb_prepare_job_fence_failure),
	KUNIT_CASE(dm_test_wb_prepare_job_pin_failure),
	KUNIT_CASE(dm_test_wb_prepare_job_gart_failure),
	KUNIT_CASE(dm_test_wb_cleanup_job_no_fb),
	KUNIT_CASE(dm_test_wb_cleanup_job_success),
	KUNIT_CASE(dm_test_wb_cleanup_job_reserve_failure),
	{}
};

static struct kunit_suite dm_wb_test_suite = {
	.name = "amdgpu_dm_wb",
	.test_cases = dm_wb_test_cases,
};

kunit_test_suite(dm_wb_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_wb");
