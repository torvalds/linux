/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <asm/cputable.h>
#include <asm/current.h>
#include <asm/copro.h>

#include "cxl.h"

/*
 * Allocates space for a CXL context.
 */
struct cxl_context *cxl_context_alloc(void)
{
	return kzalloc(sizeof(struct cxl_context), GFP_KERNEL);
}

/*
 * Initialises a CXL context.
 */
int cxl_context_init(struct cxl_context *ctx, struct cxl_afu *afu, bool master,
		     struct address_space *mapping)
{
	int i;

	spin_lock_init(&ctx->sste_lock);
	ctx->afu = afu;
	ctx->master = master;
	ctx->pid = NULL; /* Set in start work ioctl */
	mutex_init(&ctx->mapping_lock);
	ctx->mapping = mapping;

	/*
	 * Allocate the segment table before we put it in the IDR so that we
	 * can always access it when dereferenced from IDR. For the same
	 * reason, the segment table is only destroyed after the context is
	 * removed from the IDR.  Access to this in the IOCTL is protected by
	 * Linux filesytem symantics (can't IOCTL until open is complete).
	 */
	i = cxl_alloc_sst(ctx);
	if (i)
		return i;

	INIT_WORK(&ctx->fault_work, cxl_handle_fault);

	init_waitqueue_head(&ctx->wq);
	spin_lock_init(&ctx->lock);

	ctx->irq_bitmap = NULL;
	ctx->pending_irq = false;
	ctx->pending_fault = false;
	ctx->pending_afu_err = false;

	/*
	 * When we have to destroy all contexts in cxl_context_detach_all() we
	 * end up with afu_release_irqs() called from inside a
	 * idr_for_each_entry(). Hence we need to make sure that anything
	 * dereferenced from this IDR is ok before we allocate the IDR here.
	 * This clears out the IRQ ranges to ensure this.
	 */
	for (i = 0; i < CXL_IRQ_RANGES; i++)
		ctx->irqs.range[i] = 0;

	mutex_init(&ctx->status_mutex);

	ctx->status = OPENED;

	/*
	 * Allocating IDR! We better make sure everything's setup that
	 * dereferences from it.
	 */
	mutex_lock(&afu->contexts_lock);
	idr_preload(GFP_KERNEL);
	i = idr_alloc(&ctx->afu->contexts_idr, ctx, 0,
		      ctx->afu->num_procs, GFP_NOWAIT);
	idr_preload_end();
	mutex_unlock(&afu->contexts_lock);
	if (i < 0)
		return i;

	ctx->pe = i;
	ctx->elem = &ctx->afu->spa[i];
	ctx->pe_inserted = false;
	return 0;
}

static int cxl_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct cxl_context *ctx = vma->vm_file->private_data;
	unsigned long address = (unsigned long)vmf->virtual_address;
	u64 area, offset;

	offset = vmf->pgoff << PAGE_SHIFT;

	pr_devel("%s: pe: %i address: 0x%lx offset: 0x%llx\n",
			__func__, ctx->pe, address, offset);

	if (ctx->afu->current_mode == CXL_MODE_DEDICATED) {
		area = ctx->afu->psn_phys;
		if (offset >= ctx->afu->adapter->ps_size)
			return VM_FAULT_SIGBUS;
	} else {
		area = ctx->psn_phys;
		if (offset >= ctx->psn_size)
			return VM_FAULT_SIGBUS;
	}

	mutex_lock(&ctx->status_mutex);

	if (ctx->status != STARTED) {
		mutex_unlock(&ctx->status_mutex);
		pr_devel("%s: Context not started, failing problem state access\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	vm_insert_pfn(vma, address, (area + offset) >> PAGE_SHIFT);

	mutex_unlock(&ctx->status_mutex);

	return VM_FAULT_NOPAGE;
}

static const struct vm_operations_struct cxl_mmap_vmops = {
	.fault = cxl_mmap_fault,
};

/*
 * Map a per-context mmio space into the given vma.
 */
int cxl_context_iomap(struct cxl_context *ctx, struct vm_area_struct *vma)
{
	u64 start = vma->vm_pgoff << PAGE_SHIFT;
	u64 len = vma->vm_end - vma->vm_start;

	if (ctx->afu->current_mode == CXL_MODE_DEDICATED) {
		if (start + len > ctx->afu->adapter->ps_size)
			return -EINVAL;
	} else {
		if (start + len > ctx->psn_size)
			return -EINVAL;
	}

	if (ctx->afu->current_mode != CXL_MODE_DEDICATED) {
		/* make sure there is a valid per process space for this AFU */
		if ((ctx->master && !ctx->afu->psa) || (!ctx->afu->pp_psa)) {
			pr_devel("AFU doesn't support mmio space\n");
			return -EINVAL;
		}

		/* Can't mmap until the AFU is enabled */
		if (!ctx->afu->enabled)
			return -EBUSY;
	}

	pr_devel("%s: mmio physical: %llx pe: %i master:%i\n", __func__,
		 ctx->psn_phys, ctx->pe , ctx->master);

	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &cxl_mmap_vmops;
	return 0;
}

/*
 * Detach a context from the hardware. This disables interrupts and doesn't
 * return until all outstanding interrupts for this context have completed. The
 * hardware should no longer access *ctx after this has returned.
 */
int __detach_context(struct cxl_context *ctx)
{
	enum cxl_context_status status;

	mutex_lock(&ctx->status_mutex);
	status = ctx->status;
	ctx->status = CLOSED;
	mutex_unlock(&ctx->status_mutex);
	if (status != STARTED)
		return -EBUSY;

	WARN_ON(cxl_detach_process(ctx));
	flush_work(&ctx->fault_work); /* Only needed for dedicated process */
	put_pid(ctx->pid);
	cxl_ctx_put();
	return 0;
}

/*
 * Detach the given context from the AFU. This doesn't actually
 * free the context but it should stop the context running in hardware
 * (ie. prevent this context from generating any further interrupts
 * so that it can be freed).
 */
void cxl_context_detach(struct cxl_context *ctx)
{
	int rc;

	rc = __detach_context(ctx);
	if (rc)
		return;

	afu_release_irqs(ctx, ctx);
	wake_up_all(&ctx->wq);
}

/*
 * Detach all contexts on the given AFU.
 */
void cxl_context_detach_all(struct cxl_afu *afu)
{
	struct cxl_context *ctx;
	int tmp;

	mutex_lock(&afu->contexts_lock);
	idr_for_each_entry(&afu->contexts_idr, ctx, tmp) {
		/*
		 * Anything done in here needs to be setup before the IDR is
		 * created and torn down after the IDR removed
		 */
		cxl_context_detach(ctx);

		/*
		 * We are force detaching - remove any active PSA mappings so
		 * userspace cannot interfere with the card if it comes back.
		 * Easiest way to exercise this is to unbind and rebind the
		 * driver via sysfs while it is in use.
		 */
		mutex_lock(&ctx->mapping_lock);
		if (ctx->mapping)
			unmap_mapping_range(ctx->mapping, 0, 0, 1);
		mutex_unlock(&ctx->mapping_lock);
	}
	mutex_unlock(&afu->contexts_lock);
}

static void reclaim_ctx(struct rcu_head *rcu)
{
	struct cxl_context *ctx = container_of(rcu, struct cxl_context, rcu);

	free_page((u64)ctx->sstp);
	ctx->sstp = NULL;

	kfree(ctx);
}

void cxl_context_free(struct cxl_context *ctx)
{
	mutex_lock(&ctx->afu->contexts_lock);
	idr_remove(&ctx->afu->contexts_idr, ctx->pe);
	mutex_unlock(&ctx->afu->contexts_lock);
	call_rcu(&ctx->rcu, reclaim_ctx);
}
