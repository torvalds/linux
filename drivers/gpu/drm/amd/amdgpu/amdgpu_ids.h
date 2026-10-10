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
#include <linux/bitops.h>
#include <linux/find.h>
#include <linux/dma-fence.h>

#include "amdgpu_sync.h"

/* maximum number of VMIDs */
#define AMDGPU_NUM_VMID	16

/**
 * for_each_vmid_and_zero - iterate over VMID 0 plus every VMID set in a hub's
 *                          vmid_mask
 * @vmid:	loop cursor (int / unsigned int)
 * @adev:	struct amdgpu_device *
 * @hub:	vmhub index into adev->vm_manager.id_mgr[]
 *
 * Equivalent to OR-ing BIT(0) into id_mgr[hub].vmid_mask and walking
 * the result with for_each_set_bit(). Used by the gfx_v*_constants_init()
 * paths which must always touch VMID 0 (system) in addition to the regular
 * VMIDs owned by the hub.
 *
 * Relies on AMDGPU_NUM_VMID (16) fitting in a single unsigned long, i.e.
 * vmid_mask[] being a 1-element bitmap.
 */
#define for_each_vmid_and_zero(vmid, adev, hub)				\
	for (unsigned long __vmid_mask =				\
		(adev)->vm_manager.id_mgr[(hub)].vmid_mask[0] | BIT(0);	\
	     __vmid_mask; __vmid_mask = 0)				\
		for_each_set_bit((vmid), &__vmid_mask, AMDGPU_NUM_VMID)

struct amdgpu_device;
struct amdgpu_fpriv;
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

struct amdgpu_kq_vmid_mgr {
	struct mutex		lock;
	struct list_head	ids_lru;
	struct amdgpu_vmid	ids[AMDGPU_NUM_VMID];
	bool			reserved_vmid;
	DECLARE_BITMAP(vmid_mask, AMDGPU_NUM_VMID);
};

int amdgpu_pasid_alloc(unsigned int bits, struct amdgpu_fpriv *fpriv);
void amdgpu_pasid_lock(unsigned long *flags);
void amdgpu_pasid_unlock(unsigned long flags);
struct amdgpu_fpriv *amdgpu_pasid_get_fpriv_locked(u32 pasid);
void amdgpu_pasid_free(u32 pasid);
void amdgpu_pasid_free_delayed(struct dma_resv *resv,
			       u32 pasid);
void amdgpu_pasid_mgr_cleanup(void);

bool amdgpu_vmid_had_gpu_reset(struct amdgpu_device *adev,
			       struct amdgpu_vmid *id);
bool amdgpu_vmid_uses_reserved(struct amdgpu_vm *vm, unsigned int vmhub);
int amdgpu_vmid_alloc_reserved(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			       unsigned vmhub);
void amdgpu_vmid_free_reserved(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			       unsigned vmhub);
int amdgpu_vmid_grab(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_job *job, struct dma_fence **fence);
void amdgpu_vmid_reset(struct amdgpu_device *adev, unsigned vmhub,
		       unsigned vmid);
void amdgpu_vmid_reset_all(struct amdgpu_device *adev);

void amdgpu_vmid_mgr_init(struct amdgpu_device *adev);
void amdgpu_vmid_mgr_fini(struct amdgpu_device *adev);
void amdgpu_vmid_mgr_set_vmid_mask(struct amdgpu_device *adev,
				   unsigned long vmid_mask, bool for_mmhub);

#endif
