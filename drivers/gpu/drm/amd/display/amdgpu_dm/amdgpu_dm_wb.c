// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services_types.h"

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_wb.h"
#include "amdgpu_display.h"
#include "dc.h"

#include <drm/drm_edid.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_modeset_helper_vtables.h>

static const u32 amdgpu_dm_wb_formats[] = {
	DRM_FORMAT_XRGB2101010,
};

static int amdgpu_dm_wb_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct drm_framebuffer *fb;
	const struct drm_display_mode *mode = &crtc_state->mode;
	bool found = false;
	uint8_t i;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;
	if (fb->width != mode->hdisplay || fb->height != mode->vdisplay) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u\n",
			      fb->width, fb->height);
		return -EINVAL;
	}

	for (i = 0; i < sizeof(amdgpu_dm_wb_formats) / sizeof(u32); i++) {
		if (fb->format->format == amdgpu_dm_wb_formats[i])
			found = true;
	}

	if (!found) {
		DRM_DEBUG_KMS("Invalid pixel format %p4cc\n",
			      &fb->format->format);
		return -EINVAL;
	}

	return 0;
}


static int amdgpu_dm_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static int amdgpu_dm_wb_prepare_job(struct drm_writeback_connector *wb_connector,
			       struct drm_writeback_job *job)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_device *adev;
	struct amdgpu_bo *rbo;
	uint32_t domain;
	int r;

	if (!job->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(job->fb);
	obj = job->fb->obj[0];
	rbo = gem_to_amdgpu_bo(obj);
	adev = amdgpu_ttm_adev(rbo->tbo.bdev);

	r = amdgpu_bo_reserve(rbo, true);
	if (r) {
		dev_err(adev->dev, "fail to reserve bo (%d)\n", r);
		return r;
	}

	r = dma_resv_reserve_fences(rbo->tbo.base.resv, 1);
	if (r) {
		dev_err(adev->dev, "reserving fence slot failed (%d)\n", r);
		goto error_unlock;
	}

	domain = amdgpu_display_supported_domains(adev, rbo->flags);

	r = amdgpu_bo_pin(rbo, domain);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to pin framebuffer with error %d\n", r);
		goto error_unlock;
	}

	r = amdgpu_ttm_alloc_gart(&rbo->tbo);
	if (unlikely(r != 0)) {
		DRM_ERROR("%p bind failed\n", rbo);
		goto error_unpin;
	}

	amdgpu_bo_unreserve(rbo);

	afb->address = amdgpu_bo_gpu_offset(rbo);

	amdgpu_bo_ref(rbo);

	return 0;

error_unpin:
	amdgpu_bo_unpin(rbo);

error_unlock:
	amdgpu_bo_unreserve(rbo);
	return r;
}

static void amdgpu_dm_wb_cleanup_job(struct drm_writeback_connector *connector,
				struct drm_writeback_job *job)
{
	struct amdgpu_bo *rbo;
	int r;

	if (!job->fb)
		return;

	rbo = gem_to_amdgpu_bo(job->fb->obj[0]);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	}

	amdgpu_bo_unpin(rbo);
	amdgpu_bo_unreserve(rbo);
	amdgpu_bo_unref(&rbo);
}

static const struct drm_encoder_helper_funcs amdgpu_dm_wb_encoder_helper_funcs = {
	.atomic_check = amdgpu_dm_wb_encoder_atomic_check,
};

static const struct drm_connector_funcs amdgpu_dm_wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs amdgpu_dm_wb_conn_helper_funcs = {
	.get_modes = amdgpu_dm_wb_connector_get_modes,
	.prepare_writeback_job = amdgpu_dm_wb_prepare_job,
	.cleanup_writeback_job = amdgpu_dm_wb_cleanup_job,
};

int amdgpu_dm_wb_connector_init(struct amdgpu_display_manager *dm,
				struct amdgpu_dm_wb_connector *wbcon,
				uint32_t link_index)
{
	struct dc *dc = dm->dc;
	struct dc_link *link = dc_get_link_at_index(dc, link_index);
	int res = 0;

	wbcon->link = link;

	drm_connector_helper_add(&wbcon->base.base, &amdgpu_dm_wb_conn_helper_funcs);

	res = drm_writeback_connector_init(&dm->adev->ddev, &wbcon->base,
					    &amdgpu_dm_wb_connector_funcs,
					    &amdgpu_dm_wb_encoder_helper_funcs,
					    amdgpu_dm_wb_formats,
					    ARRAY_SIZE(amdgpu_dm_wb_formats),
					    amdgpu_dm_get_encoder_crtc_mask(dm->adev));

	if (res)
		return res;
	/*
	 * Some of the properties below require access to state, like bpc.
	 * Allocate some default initial connector state with our reset helper.
	 */
	if (wbcon->base.base.funcs->reset)
		wbcon->base.base.funcs->reset(&wbcon->base.base);

	return 0;
}
