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

#define AMDGPU_DEFAULT_UVD_HANDLES	10
#define AMDGPU_MAX_UVD_HANDLES		40
#define AMDGPU_UVD_STACK_SIZE		(200*1024)
#define AMDGPU_UVD_HEAP_SIZE		(256*1024)
#define AMDGPU_UVD_SESSION_SIZE		(50*1024)
#define AMDGPU_UVD_FIRMWARE_OFFSET	256

#define AMDGPU_MAX_UVD_INSTANCES			2

#define AMDGPU_UVD_FIRMWARE_SIZE(adev)    \
	(AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(((const struct common_firmware_header *)(adev)->uvd.fw->data)->ucode_size_bytes) + \
			       8) - AMDGPU_UVD_FIRMWARE_OFFSET)

struct amdgpu_uvd_inst {
	struct amdgpu_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	void			*saved_bo;
	struct amdgpu_ring	ring;
	struct amdgpu_ring	ring_enc[AMDGPU_MAX_UVD_ENC_RINGS];
	struct amdgpu_irq_src	irq;
	uint32_t                srbm_soft_reset;
};

#define AMDGPU_UVD_HARVEST_UVD0 (1 << 0)
#define AMDGPU_UVD_HARVEST_UVD1 (1 << 1)

struct amdgpu_uvd {
	const struct firmware	*fw;	/* UVD firmware */
	unsigned		fw_version;
	unsigned		max_handles;
	unsigned		num_enc_rings;
	uint8_t			num_uvd_inst;
	bool			address_64_bit;
	bool			use_ctx_buf;
	struct amdgpu_uvd_inst	inst[AMDGPU_MAX_UVD_INSTANCES];
	struct drm_file		*filp[AMDGPU_MAX_UVD_HANDLES];
	atomic_t		handles[AMDGPU_MAX_UVD_HANDLES];
	struct drm_sched_entity entity;
	struct delayed_work	idle_work;
	unsigned		harvest_config;
	/* store image width to adjust nb memory state */
	unsigned		decode_image_width;
	uint32_t                keyselect;
	struct amdgpu_bo	*ib_bo;
};

int amdgpu_uvd_sw_init(struct amdgpu_device *adev);
int amdgpu_uvd_sw_fini(struct amdgpu_device *adev);
int amdgpu_uvd_entity_init(struct amdgpu_device *adev, struct amdgpu_ring *ring);
int amdgpu_uvd_prepare_suspend(struct amdgpu_device *adev);
int amdgpu_uvd_suspend(struct amdgpu_device *adev);
int amdgpu_uvd_resume(struct amdgpu_device *adev);
int amdgpu_uvd_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct dma_fence **fence);
int amdgpu_uvd_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       bool direct, struct dma_fence **fence);
void amdgpu_uvd_free_handles(struct amdgpu_device *adev,
			     struct drm_file *filp);
int amdgpu_uvd_ring_parse_cs(struct amdgpu_cs_parser *parser,
			     struct amdgpu_job *job,
			     struct amdgpu_ib *ib);
void amdgpu_uvd_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_uvd_ring_end_use(struct amdgpu_ring *ring);
int amdgpu_uvd_ring_test_ib(struct amdgpu_ring *ring, long timeout);
uint32_t amdgpu_uvd_used_handles(struct amdgpu_device *adev);

#endif
