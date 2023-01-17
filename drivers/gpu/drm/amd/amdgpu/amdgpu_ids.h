/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_IDS_H__
#define __AMDGPU_IDS_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/dma-fence.h>

#include "amdgpu_sync.h"

/* maximum number of VMIDs */
#define AMDGPU_NUM_VMID	16

struct amdgpu_device;
struct amdgpu_vm;
struct amdgpu_ring;
struct amdgpu_sync;
struct amdgpu_job;

struct amdgpu_vmid {
	struct list_head	list;
	struct amdgpu_sync	active;
	struct dma_fence	*last_flush;
	uint64_t		owner;

	uint64_t		pd_gpu_addr;
	/* last flushed PD/PT update */
	uint64_t		flushed_updates;

	uint32_t                current_gpu_reset_count;

	uint32_t		gds_base;
	uint32_t		gds_size;
	uint32_t		gws_base;
	uint32_t		gws_size;
	uint32_t		oa_base;
	uint32_t		oa_size;

	unsigned		pasid;
	struct dma_fence	*pasid_mapping;
};

struct amdgpu_vmid_mgr {
	struct mutex		lock;
	unsigned		num_ids;
	struct list_head	ids_lru;
	struct amdgpu_vmid	ids[AMDGPU_NUM_VMID];
	struct amdgpu_vmid	*reserved;
	unsigned int		reserved_use_count;
};

int amdgpu_pasid_alloc(unsigned int bits);
void amdgpu_pasid_free(u32 pasid);
void amdgpu_pasid_free_delayed(struct dma_resv *resv,
			       u32 pasid);

bool amdgpu_vmid_had_gpu_reset(struct amdgpu_device *adev,
			       struct amdgpu_vmid *id);
int amdgpu_vmid_alloc_reserved(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       unsigned vmhub);
void amdgpu_vmid_free_reserved(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       unsigned vmhub);
int amdgpu_vmid_grab(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_job *job, struct dma_fence **fence);
void amdgpu_vmid_reset(struct amdgpu_device *adev, unsigned vmhub,
		       unsigned vmid);
void amdgpu_vmid_reset_all(struct amdgpu_device *adev);

void amdgpu_vmid_mgr_init(struct amdgpu_device *adev);
void amdgpu_vmid_mgr_fini(struct amdgpu_device *adev);

#endif
