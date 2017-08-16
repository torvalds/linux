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

#ifndef __AMDGPU_ATOMFIRMWARE_H__
#define __AMDGPU_ATOMFIRMWARE_H__

bool amdgpu_atomfirmware_gpu_supports_virtualization(struct amdgpu_device *adev);
void amdgpu_atomfirmware_scratch_regs_init(struct amdgpu_device *adev);
void amdgpu_atomfirmware_scratch_regs_save(struct amdgpu_device *adev);
void amdgpu_atomfirmware_scratch_regs_restore(struct amdgpu_device *adev);
void amdgpu_atomfirmware_scratch_regs_engine_hung(struct amdgpu_device *adev,
						  bool hung);
int amdgpu_atomfirmware_allocate_fb_scratch(struct amdgpu_device *adev);

#endif
