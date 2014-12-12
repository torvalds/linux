/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/cma.h>

#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>

#define KVM_CMA_CHUNK_ORDER	18

/*
 * Hash page table alignment on newer cpus(CPU_FTR_ARCH_206)
 * should be power of 2.
 */
#define HPT_ALIGN_PAGES		((1 << 18) >> PAGE_SHIFT) /* 256k */
/*
 * By default we reserve 5% of memory for hash pagetable allocation.
 */
static unsigned long kvm_cma_resv_ratio = 5;
/*
 * We allocate RMAs (real mode areas) for KVM guests from the KVM CMA area.
 * Each RMA has to be physically contiguous and of a size that the
 * hardware supports.  PPC970 and POWER7 support 64MB, 128MB and 256MB,
 * and other larger sizes.  Since we are unlikely to be allocate that
 * much physically contiguous memory after the system is up and running,
 * we preallocate a set of RMAs in early boot using CMA.
 * should be power of 2.
 */
unsigned long kvm_rma_pages = (1 << 27) >> PAGE_SHIFT;	/* 128MB */
EXPORT_SYMBOL_GPL(kvm_rma_pages);

static struct cma *kvm_cma;

/* Work out RMLS (real mode limit selector) field value for a given RMA size.
   Assumes POWER7 or PPC970. */
static inline int lpcr_rmls(unsigned long rma_size)
{
	switch (rma_size) {
	case 32ul << 20:	/* 32 MB */
		if (cpu_has_feature(CPU_FTR_ARCH_206))
			return 8;	/* only supported on POWER7 */
		return -1;
	case 64ul << 20:	/* 64 MB */
		return 3;
	case 128ul << 20:	/* 128 MB */
		return 7;
	case 256ul << 20:	/* 256 MB */
		return 4;
	case 1ul << 30:		/* 1 GB */
		return 2;
	case 16ul << 30:	/* 16 GB */
		return 1;
	case 256ul << 30:	/* 256 GB */
		return 0;
	default:
		return -1;
	}
}

static int __init early_parse_rma_size(char *p)
{
	unsigned long kvm_rma_size;

	pr_debug("%s(%s)\n", __func__, p);
	if (!p)
		return -EINVAL;
	kvm_rma_size = memparse(p, &p);
	/*
	 * Check that the requested size is one supported in hardware
	 */
	if (lpcr_rmls(kvm_rma_size) < 0) {
		pr_err("RMA size of 0x%lx not supported\n", kvm_rma_size);
		return -EINVAL;
	}
	kvm_rma_pages = kvm_rma_size >> PAGE_SHIFT;
	return 0;
}
early_param("kvm_rma_size", early_parse_rma_size);

struct kvm_rma_info *kvm_alloc_rma()
{
	struct page *page;
	struct kvm_rma_info *ri;

	ri = kmalloc(sizeof(struct kvm_rma_info), GFP_KERNEL);
	if (!ri)
		return NULL;
	page = cma_alloc(kvm_cma, kvm_rma_pages, order_base_2(kvm_rma_pages));
	if (!page)
		goto err_out;
	atomic_set(&ri->use_count, 1);
	ri->base_pfn = page_to_pfn(page);
	return ri;
err_out:
	kfree(ri);
	return NULL;
}
EXPORT_SYMBOL_GPL(kvm_alloc_rma);

void kvm_release_rma(struct kvm_rma_info *ri)
{
	if (atomic_dec_and_test(&ri->use_count)) {
		cma_release(kvm_cma, pfn_to_page(ri->base_pfn), kvm_rma_pages);
		kfree(ri);
	}
}
EXPORT_SYMBOL_GPL(kvm_release_rma);

static int __init early_parse_kvm_cma_resv(char *p)
{
	pr_debug("%s(%s)\n", __func__, p);
	if (!p)
		return -EINVAL;
	return kstrtoul(p, 0, &kvm_cma_resv_ratio);
}
early_param("kvm_cma_resv_ratio", early_parse_kvm_cma_resv);

struct page *kvm_alloc_hpt(unsigned long nr_pages)
{
	unsigned long align_pages = HPT_ALIGN_PAGES;

	VM_BUG_ON(order_base_2(nr_pages) < KVM_CMA_CHUNK_ORDER - PAGE_SHIFT);

	/* Old CPUs require HPT aligned on a multiple of its size */
	if (!cpu_has_feature(CPU_FTR_ARCH_206))
		align_pages = nr_pages;
	return cma_alloc(kvm_cma, nr_pages, order_base_2(align_pages));
}
EXPORT_SYMBOL_GPL(kvm_alloc_hpt);

void kvm_release_hpt(struct page *page, unsigned long nr_pages)
{
	cma_release(kvm_cma, page, nr_pages);
}
EXPORT_SYMBOL_GPL(kvm_release_hpt);

/**
 * kvm_cma_reserve() - reserve area for kvm hash pagetable
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the memblock allocator
 * has been activated and all other subsystems have already allocated/reserved
 * memory.
 */
void __init kvm_cma_reserve(void)
{
	unsigned long align_size;
	struct memblock_region *reg;
	phys_addr_t selected_size = 0;

	/*
	 * We need CMA reservation only when we are in HV mode
	 */
	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return;
	/*
	 * We cannot use memblock_phys_mem_size() here, because
	 * memblock_analyze() has not been called yet.
	 */
	for_each_memblock(memory, reg)
		selected_size += memblock_region_memory_end_pfn(reg) -
				 memblock_region_memory_base_pfn(reg);

	selected_size = (selected_size * kvm_cma_resv_ratio / 100) << PAGE_SHIFT;
	if (selected_size) {
		pr_debug("%s: reserving %ld MiB for global area\n", __func__,
			 (unsigned long)selected_size / SZ_1M);
		/*
		 * Old CPUs require HPT aligned on a multiple of its size. So for them
		 * make the alignment as max size we could request.
		 */
		if (!cpu_has_feature(CPU_FTR_ARCH_206))
			align_size = __rounddown_pow_of_two(selected_size);
		else
			align_size = HPT_ALIGN_PAGES << PAGE_SHIFT;

		align_size = max(kvm_rma_pages << PAGE_SHIFT, align_size);
		cma_declare_contiguous(0, selected_size, 0, align_size,
			KVM_CMA_CHUNK_ORDER - PAGE_SHIFT, false, &kvm_cma);
	}
}

/*
 * When running HV mode KVM we need to block certain operations while KVM VMs
 * exist in the system. We use a counter of VMs to track this.
 *
 * One of the operations we need to block is onlining of secondaries, so we
 * protect hv_vm_count with get/put_online_cpus().
 */
static atomic_t hv_vm_count;

void kvm_hv_vm_activated(void)
{
	get_online_cpus();
	atomic_inc(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_activated);

void kvm_hv_vm_deactivated(void)
{
	get_online_cpus();
	atomic_dec(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_deactivated);

bool kvm_hv_mode_active(void)
{
	return atomic_read(&hv_vm_count) != 0;
}

extern int hcall_real_table[], hcall_real_table_end[];

int kvmppc_hcall_impl_hv_realmode(unsigned long cmd)
{
	cmd /= 4;
	if (cmd < hcall_real_table_end - hcall_real_table &&
	    hcall_real_table[cmd])
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_hcall_impl_hv_realmode);
