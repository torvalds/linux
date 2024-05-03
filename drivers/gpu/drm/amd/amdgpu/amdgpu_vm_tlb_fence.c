// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 */

#include <linux/dma-fence.h>
#include <linux/workqueue.h>

#include "amdgpu.h"
#include "amdgpu_vm.h"
#include "amdgpu_gmc.h"

struct amdgpu_tlb_fence {
	struct dma_fence	base;
	struct amdgpu_device	*adev;
	struct dma_fence	*dependency;
	struct work_struct	work;
	spinlock_t		lock;
	uint16_t		pasid;

};

static const char *amdgpu_tlb_fence_get_driver_name(struct dma_fence *fence)
{
	return "amdgpu tlb fence";
}

static const char *amdgpu_tlb_fence_get_timeline_name(struct dma_fence *f)
{
	return "amdgpu tlb timeline";
}

static void amdgpu_tlb_fence_work(struct work_struct *work)
{
	struct amdgpu_tlb_fence *f = container_of(work, typeof(*f), work);
	int r;

	if (f->dependency) {
		dma_fence_wait(f->dependency, false);
		dma_fence_put(f->dependency);
		f->dependency = NULL;
	}

	r = amdgpu_gmc_flush_gpu_tlb_pasid(f->adev, f->pasid, 2, true, 0);
	if (r) {
		dev_err(f->adev->dev, "TLB flush failed for PASID %d.\n",
			f->pasid);
		dma_fence_set_error(&f->base, r);
	}

	dma_fence_signal(&f->base);
	dma_fence_put(&f->base);
}

static const struct dma_fence_ops amdgpu_tlb_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = amdgpu_tlb_fence_get_driver_name,
	.get_timeline_name = amdgpu_tlb_fence_get_timeline_name
};

void amdgpu_vm_tlb_fence_create(struct amdgpu_device *adev, struct amdgpu_vm *vm,
				struct dma_fence **fence)
{
	struct amdgpu_tlb_fence *f;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f) {
		/*
		 * We can't fail since the PDEs and PTEs are already updated, so
		 * just block for the dependency and execute the TLB flush
		 */
		if (*fence)
			dma_fence_wait(*fence, false);

		amdgpu_gmc_flush_gpu_tlb_pasid(adev, vm->pasid, 2, true, 0);
		*fence = dma_fence_get_stub();
		return;
	}

	f->adev = adev;
	f->dependency = *fence;
	f->pasid = vm->pasid;
	INIT_WORK(&f->work, amdgpu_tlb_fence_work);
	spin_lock_init(&f->lock);

	dma_fence_init(&f->base, &amdgpu_tlb_fence_ops, &f->lock,
		       vm->tlb_fence_context, atomic64_read(&vm->tlb_seq));

	/* TODO: We probably need a separate wq here */
	dma_fence_get(&f->base);
	schedule_work(&f->work);

	*fence = &f->base;
}
