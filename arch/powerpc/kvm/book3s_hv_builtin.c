/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/init.h>

#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>

/*
 * This maintains a list of RMAs (real mode areas) for KVM guests to use.
 * Each RMA has to be physically contiguous and of a size that the
 * hardware supports.  PPC970 and POWER7 support 64MB, 128MB and 256MB,
 * and other larger sizes.  Since we are unlikely to be allocate that
 * much physically contiguous memory after the system is up and running,
 * we preallocate a set of RMAs in early boot for KVM to use.
 */
static unsigned long kvm_rma_size = 64 << 20;	/* 64MB */
static unsigned long kvm_rma_count;

static int __init early_parse_rma_size(char *p)
{
	if (!p)
		return 1;

	kvm_rma_size = memparse(p, &p);

	return 0;
}
early_param("kvm_rma_size", early_parse_rma_size);

static int __init early_parse_rma_count(char *p)
{
	if (!p)
		return 1;

	kvm_rma_count = simple_strtoul(p, NULL, 0);

	return 0;
}
early_param("kvm_rma_count", early_parse_rma_count);

static struct kvmppc_rma_info *rma_info;
static LIST_HEAD(free_rmas);
static DEFINE_SPINLOCK(rma_lock);

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

/*
 * Called at boot time while the bootmem allocator is active,
 * to allocate contiguous physical memory for the real memory
 * areas for guests.
 */
void kvm_rma_init(void)
{
	unsigned long i;
	unsigned long j, npages;
	void *rma;
	struct page *pg;

	/* Only do this on PPC970 in HV mode */
	if (!cpu_has_feature(CPU_FTR_HVMODE) ||
	    !cpu_has_feature(CPU_FTR_ARCH_201))
		return;

	if (!kvm_rma_size || !kvm_rma_count)
		return;

	/* Check that the requested size is one supported in hardware */
	if (lpcr_rmls(kvm_rma_size) < 0) {
		pr_err("RMA size of 0x%lx not supported\n", kvm_rma_size);
		return;
	}

	npages = kvm_rma_size >> PAGE_SHIFT;
	rma_info = alloc_bootmem(kvm_rma_count * sizeof(struct kvmppc_rma_info));
	for (i = 0; i < kvm_rma_count; ++i) {
		rma = alloc_bootmem_align(kvm_rma_size, kvm_rma_size);
		pr_info("Allocated KVM RMA at %p (%ld MB)\n", rma,
			kvm_rma_size >> 20);
		rma_info[i].base_virt = rma;
		rma_info[i].base_pfn = __pa(rma) >> PAGE_SHIFT;
		rma_info[i].npages = npages;
		list_add_tail(&rma_info[i].list, &free_rmas);
		atomic_set(&rma_info[i].use_count, 0);

		pg = pfn_to_page(rma_info[i].base_pfn);
		for (j = 0; j < npages; ++j) {
			atomic_inc(&pg->_count);
			++pg;
		}
	}
}

struct kvmppc_rma_info *kvm_alloc_rma(void)
{
	struct kvmppc_rma_info *ri;

	ri = NULL;
	spin_lock(&rma_lock);
	if (!list_empty(&free_rmas)) {
		ri = list_first_entry(&free_rmas, struct kvmppc_rma_info, list);
		list_del(&ri->list);
		atomic_inc(&ri->use_count);
	}
	spin_unlock(&rma_lock);
	return ri;
}
EXPORT_SYMBOL_GPL(kvm_alloc_rma);

void kvm_release_rma(struct kvmppc_rma_info *ri)
{
	if (atomic_dec_and_test(&ri->use_count)) {
		spin_lock(&rma_lock);
		list_add_tail(&ri->list, &free_rmas);
		spin_unlock(&rma_lock);

	}
}
EXPORT_SYMBOL_GPL(kvm_release_rma);

