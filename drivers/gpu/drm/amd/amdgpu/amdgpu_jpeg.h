/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_JPEG_H__
#define __AMDGPU_JPEG_H__

#define AMDGPU_MAX_JPEG_INSTANCES	2

#define AMDGPU_JPEG_HARVEST_JPEG0 (1 << 0)
#define AMDGPU_JPEG_HARVEST_JPEG1 (1 << 1)

struct amdgpu_jpeg_reg{
	unsigned jpeg_pitch;
};

struct amdgpu_jpeg_inst {
	struct amdgpu_ring ring_dec;
	struct amdgpu_irq_src irq;
	struct amdgpu_jpeg_reg external;
};

struct amdgpu_jpeg {
	uint8_t	num_jpeg_inst;
	struct amdgpu_jpeg_inst inst[AMDGPU_MAX_JPEG_INSTANCES];
	struct amdgpu_jpeg_reg internal;
	struct drm_gpu_scheduler *jpeg_sched[AMDGPU_MAX_JPEG_INSTANCES];
	uint32_t num_jpeg_sched;
	unsigned harvest_config;
	struct delayed_work idle_work;
	enum amd_powergating_state cur_state;
};

int amdgpu_jpeg_sw_init(struct amdgpu_device *adev);
int amdgpu_jpeg_sw_fini(struct amdgpu_device *adev);
int amdgpu_jpeg_suspend(struct amdgpu_device *adev);
int amdgpu_jpeg_resume(struct amdgpu_device *adev);

void amdgpu_jpeg_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_jpeg_ring_end_use(struct amdgpu_ring *ring);

int amdgpu_jpeg_dec_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_jpeg_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout);

#endif /*__AMDGPU_JPEG_H__*/
