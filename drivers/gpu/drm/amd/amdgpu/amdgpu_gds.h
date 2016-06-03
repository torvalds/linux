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

#ifndef __AMDGPU_GDS_H__
#define __AMDGPU_GDS_H__

/* Because TTM request that alloacted buffer should be PAGE_SIZE aligned,
 * we should report GDS/GWS/OA size as PAGE_SIZE aligned
 * */
#define AMDGPU_GDS_SHIFT	2
#define AMDGPU_GWS_SHIFT	PAGE_SHIFT
#define AMDGPU_OA_SHIFT		PAGE_SHIFT

#define AMDGPU_PL_GDS		TTM_PL_PRIV0
#define AMDGPU_PL_GWS		TTM_PL_PRIV1
#define AMDGPU_PL_OA		TTM_PL_PRIV2

#define AMDGPU_PL_FLAG_GDS		TTM_PL_FLAG_PRIV0
#define AMDGPU_PL_FLAG_GWS		TTM_PL_FLAG_PRIV1
#define AMDGPU_PL_FLAG_OA		TTM_PL_FLAG_PRIV2

struct amdgpu_ring;
struct amdgpu_bo;

struct amdgpu_gds_asic_info {
	uint32_t	total_size;
	uint32_t	gfx_partition_size;
	uint32_t	cs_partition_size;
};

struct amdgpu_gds {
	struct amdgpu_gds_asic_info	mem;
	struct amdgpu_gds_asic_info	gws;
	struct amdgpu_gds_asic_info	oa;
	/* At present, GDS, GWS and OA resources for gfx (graphics)
	 * is always pre-allocated and available for graphics operation.
	 * Such resource is shared between all gfx clients.
	 * TODO: move this operation to user space
	 * */
	struct amdgpu_bo*		gds_gfx_bo;
	struct amdgpu_bo*		gws_gfx_bo;
	struct amdgpu_bo*		oa_gfx_bo;
};

struct amdgpu_gds_reg_offset {
	uint32_t	mem_base;
	uint32_t	mem_size;
	uint32_t	gws;
	uint32_t	oa;
};

#endif /* __AMDGPU_GDS_H__ */
