/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_CS_H__
#define __AMDGPU_CS_H__

#include <linux/ww_mutex.h>
#include <drm/drm_exec.h>

#include "amdgpu_job.h"
#include "amdgpu_bo_list.h"
#include "amdgpu_ring.h"

#define AMDGPU_CS_GANG_SIZE	4

struct amdgpu_bo_va_mapping;

struct amdgpu_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	void			*kdata;
};

struct amdgpu_cs_post_dep {
	struct drm_syncobj *syncobj;
	struct dma_fence_chain *chain;
	u64 point;
};

struct amdgpu_cs_parser {
	struct amdgpu_device	*adev;
	struct drm_file		*filp;
	struct amdgpu_ctx	*ctx;

	/* chunks */
	unsigned		nchunks;
	struct amdgpu_cs_chunk	*chunks;

	/* scheduler job objects */
	unsigned int		gang_size;
	unsigned int		gang_leader_idx;
	struct drm_sched_entity	*entities[AMDGPU_CS_GANG_SIZE];
	struct amdgpu_job	*jobs[AMDGPU_CS_GANG_SIZE];
	struct amdgpu_job	*gang_leader;

	/* buffer objects */
	struct drm_exec			exec;
	struct amdgpu_bo_list		*bo_list;
	struct amdgpu_mn		*mn;
	struct dma_fence		*fence;
	uint64_t			bytes_moved_threshold;
	uint64_t			bytes_moved_vis_threshold;
	uint64_t			bytes_moved;
	uint64_t			bytes_moved_vis;

	/* user fence */
	struct amdgpu_bo		*uf_bo;

	unsigned			num_post_deps;
	struct amdgpu_cs_post_dep	*post_deps;

	struct amdgpu_sync		sync;
};

int amdgpu_cs_find_mapping(struct amdgpu_cs_parser *parser,
			   uint64_t addr, struct amdgpu_bo **bo,
			   struct amdgpu_bo_va_mapping **mapping);

#endif
