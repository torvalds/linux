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
#include <linux/sched/task.h>

#include <asm/cputable.h>
#include <misc/cxl-base.h>

#include "cxl.h"
#include "trace.h"

static DEFINE_SPINLOCK(adapter_idr_lock);
static DEFINE_IDR(cxl_adapter_idr);

uint cxl_verbose;
module_param_named(verbose, cxl_verbose, uint, 0600);
MODULE_PARM_DESC(verbose, "Enable verbose dmesg output");

const struct cxl_backend_ops *cxl_ops;

int cxl_afu_slbia(struct cxl_afu *afu)
{
	unsigned long timeout = jiffies + (HZ * CXL_TIMEOUT);

	pr_devel("cxl_afu_slbia issuing SLBIA command\n");
	cxl_p2n_write(afu, CXL_SLBIA_An, CXL_TLB_SLB_IQ_ALL);
	while (cxl_p2n_read(afu, CXL_SLBIA_An) & CXL_TLB_SLB_P) {
		if (time_after_eq(jiffies, timeout)) {
			dev_warn(&afu->dev, "WARNING: CXL AFU SLBIA timed out!\n");
			return -EBUSY;
		}
		/* If the adapter has gone down, we can assume that we
		 * will PERST it and that will invalidate everything.
		 */
		if (!cxl_ops->link_ok(afu->adapter, afu))
			return -EIO;
		cpu_relax();
	}
	return 0;
}

static inline void _cxl_slbia(struct cxl_context *ctx, struct mm_struct *mm)
{
	unsigned long flags;

	if (ctx->mm != mm)
		return;

	pr_devel("%s matched mm - card: %i afu: %i pe: %i\n", __func__,
		 ctx->afu->adapter->adapter_num, ctx->afu->slice, ctx->pe);

	spin_lock_irqsave(&ctx->sste_lock, flags);
	trace_cxl_slbia(ctx);
	memset(ctx->sstp, 0, ctx->sst_size);
	spin_unlock_irqrestore(&ctx->sste_lock, flags);
	mb();
	cxl_afu_slbia(ctx->afu);
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
	.cxl_pci_associate_default_context = _cxl_pci_associate_default_context,
	.cxl_pci_disable_device = _cxl_pci_disable_device,
	.cxl_next_msi_hwirq = _cxl_next_msi_hwirq,
	.cxl_cx4_setup_msi_irqs = _cxl_cx4_setup_msi_irqs,
	.cxl_cx4_teardown_msi_irqs = _cxl_cx4_teardown_msi_irqs,
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

/* print buffer content as integers when debugging */
void cxl_dump_debug_buffer(void *buf, size_t buf_len)
{
#ifdef DEBUG
	int i, *ptr;

	/*
	 * We want to regroup up to 4 integers per line, which means they
	 * need to be in the same pr_devel() statement
	 */
	ptr = (int *) buf;
	for (i = 0; i * 4 < buf_len; i += 4) {
		if ((i + 3) * 4 < buf_len)
			pr_devel("%.8x %.8x %.8x %.8x\n", ptr[i], ptr[i + 1],
				ptr[i + 2], ptr[i + 3]);
		else if ((i + 2) * 4 < buf_len)
			pr_devel("%.8x %.8x %.8x\n", ptr[i], ptr[i + 1],
				ptr[i + 2]);
		else if ((i + 1) * 4 < buf_len)
			pr_devel("%.8x %.8x\n", ptr[i], ptr[i + 1]);
		else
			pr_devel("%.8x\n", ptr[i]);
	}
#endif /* DEBUG */
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

static int cxl_alloc_adapter_nr(struct cxl *adapter)
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

struct cxl *cxl_alloc_adapter(void)
{
	struct cxl *adapter;

	if (!(adapter = kzalloc(sizeof(struct cxl), GFP_KERNEL)))
		return NULL;

	spin_lock_init(&adapter->afu_list_lock);

	if (cxl_alloc_adapter_nr(adapter))
		goto err1;

	if (dev_set_name(&adapter->dev, "card%i", adapter->adapter_num))
		goto err2;

	/* start with context lock taken */
	atomic_set(&adapter->contexts_num, -1);

	return adapter;
err2:
	cxl_remove_adapter_nr(adapter);
err1:
	kfree(adapter);
	return NULL;
}

struct cxl_afu *cxl_alloc_afu(struct cxl *adapter, int slice)
{
	struct cxl_afu *afu;

	if (!(afu = kzalloc(sizeof(struct cxl_afu), GFP_KERNEL)))
		return NULL;

	afu->adapter = adapter;
	afu->dev.parent = &adapter->dev;
	afu->dev.release = cxl_ops->release_afu;
	afu->slice = slice;
	idr_init(&afu->contexts_idr);
	mutex_init(&afu->contexts_lock);
	spin_lock_init(&afu->afu_cntl_lock);
	atomic_set(&afu->configured_state, -1);
	afu->prefault_mode = CXL_PREFAULT_NONE;
	afu->irqs_max = afu->adapter->user_irqs;

	return afu;
}

int cxl_afu_select_best_mode(struct cxl_afu *afu)
{
	if (afu->modes_supported & CXL_MODE_DIRECTED)
		return cxl_ops->afu_activate_mode(afu, CXL_MODE_DIRECTED);

	if (afu->modes_supported & CXL_MODE_DEDICATED)
		return cxl_ops->afu_activate_mode(afu, CXL_MODE_DEDICATED);

	dev_warn(&afu->dev, "No supported programming modes available\n");
	/* We don't fail this so the user can inspect sysfs */
	return 0;
}

int cxl_adapter_context_get(struct cxl *adapter)
{
	int rc;

	rc = atomic_inc_unless_negative(&adapter->contexts_num);
	return rc >= 0 ? 0 : -EBUSY;
}

void cxl_adapter_context_put(struct cxl *adapter)
{
	atomic_dec_if_positive(&adapter->contexts_num);
}

int cxl_adapter_context_lock(struct cxl *adapter)
{
	int rc;
	/* no active contexts -> contexts_num == 0 */
	rc = atomic_cmpxchg(&adapter->contexts_num, 0, -1);
	return rc ? -EBUSY : 0;
}

void cxl_adapter_context_unlock(struct cxl *adapter)
{
	int val = atomic_cmpxchg(&adapter->contexts_num, -1, 0);

	/*
	 * contexts lock taken -> contexts_num == -1
	 * If not true then show a warning and force reset the lock.
	 * This will happen when context_unlock was requested without
	 * doing a context_lock.
	 */
	if (val != -1) {
		atomic_set(&adapter->contexts_num, 0);
		WARN(1, "Adapter context unlocked with %d active contexts",
		     val);
	}
}

static int __init init_cxl(void)
{
	int rc = 0;

	if ((rc = cxl_file_init()))
		return rc;

	cxl_debugfs_init();

	if ((rc = register_cxl_calls(&cxl_calls)))
		goto err;

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		cxl_ops = &cxl_native_ops;
		rc = pci_register_driver(&cxl_pci_driver);
	}
#ifdef CONFIG_PPC_PSERIES
	else {
		cxl_ops = &cxl_guest_ops;
		rc = platform_driver_register(&cxl_of_driver);
	}
#endif
	if (rc)
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
	if (cpu_has_feature(CPU_FTR_HVMODE))
		pci_unregister_driver(&cxl_pci_driver);
#ifdef CONFIG_PPC_PSERIES
	else
		platform_driver_unregister(&cxl_of_driver);
#endif

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
