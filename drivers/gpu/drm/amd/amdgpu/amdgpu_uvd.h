/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#ifndef __AMDGPU_UVD_H__
#define __AMDGPU_UVD_H__

int amdgpu_uvd_sw_init(struct amdgpu_device *adev);
int amdgpu_uvd_sw_fini(struct amdgpu_device *adev);
int amdgpu_uvd_suspend(struct amdgpu_device *adev);
int amdgpu_uvd_resume(struct amdgpu_device *adev);
int amdgpu_uvd_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct dma_fence **fence);
int amdgpu_uvd_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       bool direct, struct dma_fence **fence);
void amdgpu_uvd_free_handles(struct amdgpu_device *adev,
			     struct drm_file *filp);
int amdgpu_uvd_ring_parse_cs(struct amdgpu_cs_parser *parser, uint32_t ib_idx);
void amdgpu_uvd_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_uvd_ring_end_use(struct amdgpu_ring *ring);
int amdgpu_uvd_ring_test_ib(struct amdgpu_ring *ring, long timeout);
uint32_t amdgpu_uvd_used_handles(struct amdgpu_device *adev);

#endif
