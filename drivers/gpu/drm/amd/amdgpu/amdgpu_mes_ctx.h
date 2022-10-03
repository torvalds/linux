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

#ifndef __AMDGPU_MES_CTX_H__
#define __AMDGPU_MES_CTX_H__

#include "v10_structs.h"

enum {
	AMDGPU_MES_CTX_RPTR_OFFS = 0,
	AMDGPU_MES_CTX_WPTR_OFFS,
	AMDGPU_MES_CTX_FENCE_OFFS,
	AMDGPU_MES_CTX_COND_EXE_OFFS,
	AMDGPU_MES_CTX_TRAIL_FENCE_OFFS,
	AMDGPU_MES_CTX_MAX_OFFS,
};

enum {
	AMDGPU_MES_CTX_RING_OFFS = AMDGPU_MES_CTX_MAX_OFFS,
	AMDGPU_MES_CTX_IB_OFFS,
	AMDGPU_MES_CTX_PADDING_OFFS,
};

#define AMDGPU_MES_CTX_MAX_GFX_RINGS            1
#define AMDGPU_MES_CTX_MAX_COMPUTE_RINGS        4
#define AMDGPU_MES_CTX_MAX_SDMA_RINGS           2
#define AMDGPU_MES_CTX_MAX_RINGS					\
	(AMDGPU_MES_CTX_MAX_GFX_RINGS +					\
	 AMDGPU_MES_CTX_MAX_COMPUTE_RINGS +				\
	 AMDGPU_MES_CTX_MAX_SDMA_RINGS)

#define AMDGPU_CSA_SDMA_SIZE    64
#define GFX10_MEC_HPD_SIZE	2048

struct amdgpu_wb_slot {
	uint32_t data[8];
};

struct amdgpu_mes_ctx_meta_data {
	struct {
		uint8_t ring[PAGE_SIZE * 4];

		/* gfx csa */
		struct v10_gfx_meta_data gfx_meta_data;

		uint8_t gds_backup[64 * 1024];

		struct amdgpu_wb_slot slots[AMDGPU_MES_CTX_MAX_OFFS];

		/* only for ib test */
		uint32_t ib[256] __aligned(256);

		uint32_t padding[64];

	} __aligned(PAGE_SIZE) gfx[AMDGPU_MES_CTX_MAX_GFX_RINGS];

	struct {
		uint8_t ring[PAGE_SIZE * 4];

		uint8_t mec_hpd[GFX10_MEC_HPD_SIZE];

		struct amdgpu_wb_slot slots[AMDGPU_MES_CTX_MAX_OFFS];

		/* only for ib test */
		uint32_t ib[256] __aligned(256);

		uint32_t padding[64];

	} __aligned(PAGE_SIZE) compute[AMDGPU_MES_CTX_MAX_COMPUTE_RINGS];

	struct {
		uint8_t ring[PAGE_SIZE * 4];

		/* sdma csa for mcbp */
		uint8_t sdma_meta_data[AMDGPU_CSA_SDMA_SIZE];

		struct amdgpu_wb_slot slots[AMDGPU_MES_CTX_MAX_OFFS];

		/* only for ib test */
		uint32_t ib[256] __aligned(256);

		uint32_t padding[64];

	} __aligned(PAGE_SIZE) sdma[AMDGPU_MES_CTX_MAX_SDMA_RINGS];
};

struct amdgpu_mes_ctx_data {
	struct amdgpu_bo	*meta_data_obj;
	uint64_t                meta_data_gpu_addr;
	uint64_t                meta_data_mc_addr;
	struct amdgpu_bo_va	*meta_data_va;
	void                    *meta_data_ptr;
	uint32_t                gang_ids[AMDGPU_HW_IP_DMA+1];
};

#define AMDGPU_FENCE_MES_QUEUE_FLAG     0x1000000u
#define AMDGPU_FENCE_MES_QUEUE_ID_MASK  (AMDGPU_FENCE_MES_QUEUE_FLAG - 1)

#define AMDGPU_FENCE_MES_QUEUE_FLAG     0x1000000u
#define AMDGPU_FENCE_MES_QUEUE_ID_MASK  (AMDGPU_FENCE_MES_QUEUE_FLAG - 1)

#endif
