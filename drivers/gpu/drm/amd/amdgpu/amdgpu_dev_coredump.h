/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_DEV_COREDUMP_H__
#define __AMDGPU_DEV_COREDUMP_H__

#include "amdgpu.h"
#include "amdgpu_reset.h"

#ifdef CONFIG_DEV_COREDUMP

#define AMDGPU_COREDUMP_VERSION "1"

struct amdgpu_coredump_info {
	struct amdgpu_device            *adev;
	struct amdgpu_task_info         reset_task_info;
	struct timespec64               reset_time;
	bool                            reset_vram_lost;
	struct amdgpu_ring              *ring;
};
#endif

void amdgpu_coredump(struct amdgpu_device *adev, bool vram_lost,
		     struct amdgpu_reset_context *reset_context);

#endif
