/* SPDX-License-Identifier: MIT */
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

#ifndef __AMDGPU_DM_WB_H__
#define __AMDGPU_DM_WB_H__

#include <drm/drm_writeback.h>

struct amdgpu_display_manager;
struct amdgpu_dm_wb_connector;
struct amdgpu_bo;
struct dma_resv;
struct ttm_buffer_object;

int amdgpu_dm_wb_connector_init(struct amdgpu_display_manager *dm,
				struct amdgpu_dm_wb_connector *dm_wbcon,
				uint32_t link_index);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>

struct amdgpu_dm_wb_kunit_ops {
	int (*reserve)(struct amdgpu_bo *bo, bool interruptible);
	int (*reserve_fences)(struct dma_resv *resv, unsigned int num_fences);
	int (*pin)(struct amdgpu_bo *bo, u32 domain);
	int (*alloc_gart)(struct ttm_buffer_object *tbo);
	void (*unreserve)(struct amdgpu_bo *bo);
	u64 (*gpu_offset)(struct amdgpu_bo *bo);
	struct amdgpu_bo *(*ref)(struct amdgpu_bo *bo);
	void (*unpin)(struct amdgpu_bo *bo);
	void (*unref)(struct amdgpu_bo **bo);
};

int amdgpu_dm_wb_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state);
int amdgpu_dm_wb_connector_get_modes(struct drm_connector *connector);
int amdgpu_dm_wb_prepare_job(struct drm_writeback_connector *wb_connector,
			     struct drm_writeback_job *job);
void amdgpu_dm_wb_cleanup_job(struct drm_writeback_connector *connector,
			      struct drm_writeback_job *job);
void amdgpu_dm_wb_kunit_set_ops(const struct amdgpu_dm_wb_kunit_ops *ops);
#endif

#endif
