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

#ifndef __AMDGPU_VCE_H__
#define __AMDGPU_VCE_H__

int amdgpu_vce_sw_init(struct amdgpu_device *adev, unsigned long size);
int amdgpu_vce_sw_fini(struct amdgpu_device *adev);
int amdgpu_vce_suspend(struct amdgpu_device *adev);
int amdgpu_vce_resume(struct amdgpu_device *adev);
int amdgpu_vce_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct fence **fence);
int amdgpu_vce_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       bool direct, struct fence **fence);
void amdgpu_vce_free_handles(struct amdgpu_device *adev, struct drm_file *filp);
int amdgpu_vce_ring_parse_cs(struct amdgpu_cs_parser *p, uint32_t ib_idx);
void amdgpu_vce_ring_emit_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
void amdgpu_vce_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				unsigned flags);
int amdgpu_vce_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vce_ring_test_ib(struct amdgpu_ring *ring);

#endif
