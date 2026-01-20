/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef __AMDGPU_RAS_PROCESS_H__
#define __AMDGPU_RAS_PROCESS_H__
#include "ras_process.h"
#include "amdgpu_ras_mgr.h"

enum ras_ih_type;
int amdgpu_ras_process_init(struct amdgpu_device *adev);
int amdgpu_ras_process_fini(struct amdgpu_device *adev);
int amdgpu_ras_process_handle_umc_interrupt(struct amdgpu_device *adev,
		void *data);
int amdgpu_ras_process_handle_unexpected_interrupt(struct amdgpu_device *adev,
		void *data);
int amdgpu_ras_process_handle_consumption_interrupt(struct amdgpu_device *adev,
		void *data);
int amdgpu_ras_process_begin(struct amdgpu_device *adev);
int amdgpu_ras_process_end(struct amdgpu_device *adev);
int amdgpu_ras_process_pre_reset(struct amdgpu_device *adev);
int amdgpu_ras_process_post_reset(struct amdgpu_device *adev);
#endif
