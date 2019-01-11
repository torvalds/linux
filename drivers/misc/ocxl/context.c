// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/sched/mm.h>
#include "trace.h"
#include "ocxl_internal.h"

struct ocxl_context *ocxl_context_alloc(void)
{
	return kzalloc(sizeof(struct ocxl_context), GFP_KERNEL);
}

int ocxl_context_init(struct ocxl_context *ctx, struct ocxl_afu *afu,
		struct address_space *mapping)
{
	int pasid;

	ctx->afu = afu;
	mutex_lock(&afu->contexts_lock);
	pasid = idr_alloc(&afu->contexts_idr, ctx, afu->pasid_base,
			afu->pasid_base + afu->pasid_max, GFP_KERNEL);
	if (pasid < 0) {
		mutex_unlock(&afu->contexts_lock);
		return pasid;
	}
	afu->pasid_count++;
	mutex_unlock(&afu->contexts_lock);

	ctx->pasid = pasid;
	ctx->status = OPENED;
	mutex_init(&ctx->status_mutex);
	ctx->mapping = mapping;
	mutex_init(&ctx->mapping_lock);
	init_waitqueue_head(&ctx->events_wq);
	mutex_init(&ctx->xsl_error_lock);
	mutex_init(&ctx->irq_lock);
	idr_init(&ctx->irq_idr);
	ctx->tidr = 0;

	/*
	 * Keep a reference on the AFU to make sure it's valid for the
	 * duration of the life of the context
	 */
	ocxl_afu_get(afu);
	return 0;
}

/*
 * Callback for when a translation fault triggers an error
 * data:	a pointer to the context which triggered the fault
 * addr:	the address that triggered the error
 * dsisr:	the value of the PPC64 dsisr register
 */
static void xsl_fault_error(void *data, u64 addr, u64 dsisr)
{
	struct ocxl_context *ctx = (struct ocxl_context *) data;

	mutex_lock(&ctx->xsl_error_lock);
	ctx->xsl_error.addr = addr;
	ctx->xsl_error.dsisr = dsisr;
	ctx->xsl_error.count++;
	mutex_unlock(&ctx->xsl_error_lock);

	wake_up_all(&ctx->events_wq);
}

int ocxl_context_attach(struct ocxl_context *ctx, u64 amr)
{
	int rc;

	// Locks both status & tidr
	mutex_lock(&ctx->status_mutex);
	if (ctx->status != OPENED) {
		rc = -EIO;
		goto out;
	}

	rc = ocxl_link_add_pe(ctx->afu->fn->link, ctx->pasid,
			current->mm->context.id, ctx->tidr, amr, current->mm,
			xsl_fault_error, ctx);
	if (rc)
		goto out;

	ctx->status = ATTACHED;
out:
	mutex_unlock(&ctx->status_mutex);
	return rc;
}

static vm_fault_t map_afu_irq(struct vm_area_struct *vma, unsigned long address,
		u64 offset, struct ocxl_context *ctx)
{
	u64 trigger_addr;

	trigger_addr = ocxl_afu_irq_get_addr(ctx, offset);
	if (!trigger_addr)
		return VM_FAULT_SIGBUS;

	return vmf_insert_pfn(vma, address, trigger_addr >> PAGE_SHIFT);
}

static vm_fault_t map_pp_mmio(struct vm_area_struct *vma, unsigned long address,
		u64 offset, struct ocxl_context *ctx)
{
	u64 pp_mmio_addr;
	int pasid_off;
	vm_fault_t ret;

	if (offset >= ctx->afu->config.pp_mmio_stride)
		return VM_FAULT_SIGBUS;

	mutex_lock(&ctx->status_mutex);
	if (ctx->status != ATTACHED) {
		mutex_unlock(&ctx->status_mutex);
		pr_debug("%s: Context not attached, failing mmio mmap\n",
			__func__);
		return VM_FAULT_SIGBUS;
	}

	pasid_off = ctx->pasid - ctx->afu->pasid_base;
	pp_mmio_addr = ctx->afu->pp_mmio_start +
		pasid_off * ctx->afu->config.pp_mmio_stride +
		offset;

	ret = vmf_insert_pfn(vma, address, pp_mmio_addr >> PAGE_SHIFT);
	mutex_unlock(&ctx->status_mutex);
	return ret;
}

static vm_fault_t ocxl_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ocxl_context *ctx = vma->vm_file->private_data;
	u64 offset;
	vm_fault_t ret;

	offset = vmf->pgoff << PAGE_SHIFT;
	pr_debug("%s: pasid %d address 0x%lx offset 0x%llx\n", __func__,
		ctx->pasid, vmf->address, offset);

	if (offset < ctx->afu->irq_base_offset)
		ret = map_pp_mmio(vma, vmf->address, offset, ctx);
	else
		ret = map_afu_irq(vma, vmf->address, offset, ctx);
	return ret;
}

static const struct vm_operations_struct ocxl_vmops = {
	.fault = ocxl_mmap_fault,
};

static int check_mmap_afu_irq(struct ocxl_context *ctx,
			struct vm_area_struct *vma)
{
	/* only one page */
	if (vma_pages(vma) != 1)
		return -EINVAL;

	/* check offset validty */
	if (!ocxl_afu_irq_get_addr(ctx, vma->vm_pgoff << PAGE_SHIFT))
		return -EINVAL;

	/*
	 * trigger page should only be accessible in write mode.
	 *
	 * It's a bit theoretical, as a page mmaped with only
	 * PROT_WRITE is currently readable, but it doesn't hurt.
	 */
	if ((vma->vm_flags & VM_READ) || (vma->vm_flags & VM_EXEC) ||
		!(vma->vm_flags & VM_WRITE))
		return -EINVAL;
	vma->vm_flags &= ~(VM_MAYREAD | VM_MAYEXEC);
	return 0;
}

static int check_mmap_mmio(struct ocxl_context *ctx,
			struct vm_area_struct *vma)
{
	if ((vma_pages(vma) + vma->vm_pgoff) >
		(ctx->afu->config.pp_mmio_stride >> PAGE_SHIFT))
		return -EINVAL;
	return 0;
}

int ocxl_context_mmap(struct ocxl_context *ctx, struct vm_area_struct *vma)
{
	int rc;

	if ((vma->vm_pgoff << PAGE_SHIFT) < ctx->afu->irq_base_offset)
		rc = check_mmap_mmio(ctx, vma);
	else
		rc = check_mmap_afu_irq(ctx, vma);
	if (rc)
		return rc;

	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &ocxl_vmops;
	return 0;
}

int ocxl_context_detach(struct ocxl_context *ctx)
{
	struct pci_dev *dev;
	int afu_control_pos;
	enum ocxl_context_status status;
	int rc;

	mutex_lock(&ctx->status_mutex);
	status = ctx->status;
	ctx->status = CLOSED;
	mutex_unlock(&ctx->status_mutex);
	if (status != ATTACHED)
		return 0;

	dev = to_pci_dev(ctx->afu->fn->dev.parent);
	afu_control_pos = ctx->afu->config.dvsec_afu_control_pos;

	mutex_lock(&ctx->afu->afu_control_lock);
	rc = ocxl_config_terminate_pasid(dev, afu_control_pos, ctx->pasid);
	mutex_unlock(&ctx->afu->afu_control_lock);
	trace_ocxl_terminate_pasid(ctx->pasid, rc);
	if (rc) {
		/*
		 * If we timeout waiting for the AFU to terminate the
		 * pasid, then it's dangerous to clean up the Process
		 * Element entry in the SPA, as it may be referenced
		 * in the future by the AFU. In which case, we would
		 * checkstop because of an invalid PE access (FIR
		 * register 2, bit 42). So leave the PE
		 * defined. Caller shouldn't free the context so that
		 * PASID remains allocated.
		 *
		 * A link reset will be required to cleanup the AFU
		 * and the SPA.
		 */
		if (rc == -EBUSY)
			return rc;
	}
	rc = ocxl_link_remove_pe(ctx->afu->fn->link, ctx->pasid);
	if (rc) {
		dev_warn(&ctx->afu->dev,
			"Couldn't remove PE entry cleanly: %d\n", rc);
	}
	return 0;
}

void ocxl_context_detach_all(struct ocxl_afu *afu)
{
	struct ocxl_context *ctx;
	int tmp;

	mutex_lock(&afu->contexts_lock);
	idr_for_each_entry(&afu->contexts_idr, ctx, tmp) {
		ocxl_context_detach(ctx);
		/*
		 * We are force detaching - remove any active mmio
		 * mappings so userspace cannot interfere with the
		 * card if it comes back.  Easiest way to exercise
		 * this is to unbind and rebind the driver via sysfs
		 * while it is in use.
		 */
		mutex_lock(&ctx->mapping_lock);
		if (ctx->mapping)
			unmap_mapping_range(ctx->mapping, 0, 0, 1);
		mutex_unlock(&ctx->mapping_lock);
	}
	mutex_unlock(&afu->contexts_lock);
}

void ocxl_context_free(struct ocxl_context *ctx)
{
	mutex_lock(&ctx->afu->contexts_lock);
	ctx->afu->pasid_count--;
	idr_remove(&ctx->afu->contexts_idr, ctx->pasid);
	mutex_unlock(&ctx->afu->contexts_lock);

	ocxl_afu_irq_free_all(ctx);
	idr_destroy(&ctx->irq_idr);
	/* reference to the AFU taken in ocxl_context_init */
	ocxl_afu_put(ctx->afu);
	kfree(ctx);
}
