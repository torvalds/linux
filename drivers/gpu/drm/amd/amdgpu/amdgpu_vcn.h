/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_VCN_H__
#define __AMDGPU_VCN_H__

int amdgpu_vcn_sw_init(struct amdgpu_device *adev);
int amdgpu_vcn_sw_fini(struct amdgpu_device *adev);
int amdgpu_vcn_suspend(struct amdgpu_device *adev);
int amdgpu_vcn_resume(struct amdgpu_device *adev);
void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_vcn_ring_end_use(struct amdgpu_ring *ring);
int amdgpu_vcn_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout);

#endif
