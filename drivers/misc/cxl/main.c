/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <asm/cputable.h>
#include <misc/cxl-base.h>

#include "cxl.h"
#include "trace.h"

static DEFINE_SPINLOCK(adapter_idr_lock);
static DEFINE_IDR(cxl_adapter_idr);

uint cxl_verbose;
module_param_named(verbose, cxl_verbose, uint, 0600);
MODULE_PARM_DESC(verbose, "Enable verbose dmesg output");

static inline void _cxl_slbia(struct cxl_context *ctx, struct mm_struct *mm)
{
	struct task_struct *task;
	unsigned long flags;
	if (!(task = get_pid_task(ctx->pid, PIDTYPE_PID))) {
		pr_devel("%s unable to get task %i\n",
			 __func__, pid_nr(ctx->pid));
		return;
	}

	if (task->mm != mm)
		goto out_put;

	pr_devel("%s matched mm - card: %i afu: %i pe: %i\n", __func__,
		 ctx->afu->adapter->adapter_num, ctx->afu->slice, ctx->pe);

	spin_lock_irqsave(&ctx->sste_lock, flags);
	trace_cxl_slbia(ctx);
	memset(ctx->sstp, 0, ctx->sst_size);
	spin_unlock_irqrestore(&ctx->sste_lock, flags);
	mb();
	cxl_afu_slbia(ctx->afu);
out_put:
	put_task_struct(task);
}

static inline void cxl_slbia_core(struct mm_struct *mm)
{
	struct cxl *adapter;
	struct cxl_afu *afu;
	struct cxl_context *ctx;
	int card, slice, id;

	pr_devel("%s called\n", __func__);

	spin_lock(&adapter_idr_lock);
	idr_for_each_entry(&cxl_adapter_idr, adapter, card) {
		/* XXX: Make this lookup faster with link from mm to ctx */
		spin_lock(&adapter->afu_list_lock);
		for (slice = 0; slice < adapter->slices; slice++) {
			afu = adapter->afu[slice];
			if (!afu || !afu->enabled)
				continue;
			rcu_read_lock();
			idr_for_each_entry(&afu->contexts_idr, ctx, id)
				_cxl_slbia(ctx, mm);
			rcu_read_unlock();
		}
		spin_unlock(&adapter->afu_list_lock);
	}
	spin_unlock(&adapter_idr_lock);
}

static struct cxl_calls cxl_calls = {
	.cxl_slbia = cxl_slbia_core,
	.owner = THIS_MODULE,
};

int cxl_alloc_sst(struct cxl_context *ctx)
{
	unsigned long vsid;
	u64 ea_mask, size, sstp0, sstp1;

	sstp0 = 0;
	sstp1 = 0;

	ctx->sst_size = PAGE_SIZE;
	ctx->sst_lru = 0;
	ctx->sstp = (struct cxl_sste *)get_zeroed_page(GFP_KERNEL);
	if (!ctx->sstp) {
		pr_err("cxl_alloc_sst: Unable to allocate segment table\n");
		return -ENOMEM;
	}
	pr_devel("SSTP allocated at 0x%p\n", ctx->sstp);

	vsid  = get_kernel_vsid((u64)ctx->sstp, mmu_kernel_ssize) << 12;

	sstp0 |= (u64)mmu_kernel_ssize << CXL_SSTP0_An_B_SHIFT;
	sstp0 |= (SLB_VSID_KERNEL | mmu_psize_defs[mmu_linear_psize].sllp) << 50;

	size = (((u64)ctx->sst_size >> 8) - 1) << CXL_SSTP0_An_SegTableSize_SHIFT;
	if (unlikely(size & ~CXL_SSTP0_An_SegTableSize_MASK)) {
		WARN(1, "Impossible segment table size\n");
		return -EINVAL;
	}
	sstp0 |= size;

	if (mmu_kernel_ssize == MMU_SEGSIZE_256M)
		ea_mask = 0xfffff00ULL;
	else
		ea_mask = 0xffffffff00ULL;

	sstp0 |=  vsid >>     (50-14);  /*   Top 14 bits of VSID */
	sstp1 |= (vsid << (64-(50-14))) & ~ea_mask;
	sstp1 |= (u64)ctx->sstp & ea_mask;
	sstp1 |= CXL_SSTP1_An_V;

	pr_devel("Looked up %#llx: slbfee. %#llx (ssize: %x, vsid: %#lx), copied to SSTP0: %#llx, SSTP1: %#llx\n",
			(u64)ctx->sstp, (u64)ctx->sstp & ESID_MASK, mmu_kernel_ssize, vsid, sstp0, sstp1);

	/* Store calculated sstp hardware points for use later */
	ctx->sstp0 = sstp0;
	ctx->sstp1 = sstp1;

	return 0;
}

/* Find a CXL adapter by it's number and increase it's refcount */
struct cxl *get_cxl_adapter(int num)
{
	struct cxl *adapter;

	spin_lock(&adapter_idr_lock);
	if ((adapter = idr_find(&cxl_adapter_idr, num)))
		get_device(&adapter->dev);
	spin_unlock(&adapter_idr_lock);

	return adapter;
}

int cxl_alloc_adapter_nr(struct cxl *adapter)
{
	int i;

	idr_preload(GFP_KERNEL);
	spin_lock(&adapter_idr_lock);
	i = idr_alloc(&cxl_adapter_idr, adapter, 0, 0, GFP_NOWAIT);
	spin_unlock(&adapter_idr_lock);
	idr_preload_end();
	if (i < 0)
		return i;

	adapter->adapter_num = i;

	return 0;
}

void cxl_remove_adapter_nr(struct cxl *adapter)
{
	idr_remove(&cxl_adapter_idr, adapter->adapter_num);
}

int cxl_afu_select_best_mode(struct cxl_afu *afu)
{
	if (afu->modes_supported & CXL_MODE_DIRECTED)
		return cxl_afu_activate_mode(afu, CXL_MODE_DIRECTED);

	if (afu->modes_supported & CXL_MODE_DEDICATED)
		return cxl_afu_activate_mode(afu, CXL_MODE_DEDICATED);

	dev_warn(&afu->dev, "No supported programming modes available\n");
	/* We don't fail this so the user can inspect sysfs */
	return 0;
}

static int __init init_cxl(void)
{
	int rc = 0;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return -EPERM;

	if ((rc = cxl_file_init()))
		return rc;

	cxl_debugfs_init();

	if ((rc = register_cxl_calls(&cxl_calls)))
		goto err;

	if ((rc = pci_register_driver(&cxl_pci_driver)))
		goto err1;

	return 0;
err1:
	unregister_cxl_calls(&cxl_calls);
err:
	cxl_debugfs_exit();
	cxl_file_exit();

	return rc;
}

static void exit_cxl(void)
{
	pci_unregister_driver(&cxl_pci_driver);

	cxl_debugfs_exit();
	cxl_file_exit();
	unregister_cxl_calls(&cxl_calls);
	idr_destroy(&cxl_adapter_idr);
}

module_init(init_cxl);
module_exit(exit_cxl);

MODULE_DESCRIPTION("IBM Coherent Accelerator");
MODULE_AUTHOR("Ian Munsie <imunsie@au1.ibm.com>");
MODULE_LICENSE("GPL");
