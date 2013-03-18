/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/init.h>

#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>

#define KVM_LINEAR_RMA		0
#define KVM_LINEAR_HPT		1

static void __init kvm_linear_init_one(ulong size, int count, int type);
static struct kvmppc_linear_info *kvm_alloc_linear(int type);
static void kvm_release_linear(struct kvmppc_linear_info *ri);

int kvm_hpt_order = KVM_DEFAULT_HPT_ORDER;
EXPORT_SYMBOL_GPL(kvm_hpt_order);

/*************** RMA *************/

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

struct kvmppc_linear_info *kvm_alloc_rma(void)
{
	return kvm_alloc_linear(KVM_LINEAR_RMA);
}
EXPORT_SYMBOL_GPL(kvm_alloc_rma);

void kvm_release_rma(struct kvmppc_linear_info *ri)
{
	kvm_release_linear(ri);
}
EXPORT_SYMBOL_GPL(kvm_release_rma);

/*************** HPT *************/

/*
 * This maintains a list of big linear HPT tables that contain the GVA->HPA
 * memory mappings. If we don't reserve those early on, we might not be able
 * to get a big (usually 16MB) linear memory region from the kernel anymore.
 */

static unsigned long kvm_hpt_count;

static int __init early_parse_hpt_count(char *p)
{
	if (!p)
		return 1;

	kvm_hpt_count = simple_strtoul(p, NULL, 0);

	return 0;
}
early_param("kvm_hpt_count", early_parse_hpt_count);

struct kvmppc_linear_info *kvm_alloc_hpt(void)
{
	return kvm_alloc_linear(KVM_LINEAR_HPT);
}
EXPORT_SYMBOL_GPL(kvm_alloc_hpt);

void kvm_release_hpt(struct kvmppc_linear_info *li)
{
	kvm_release_linear(li);
}
EXPORT_SYMBOL_GPL(kvm_release_hpt);

/*************** generic *************/

static LIST_HEAD(free_linears);
static DEFINE_SPINLOCK(linear_lock);

static void __init kvm_linear_init_one(ulong size, int count, int type)
{
	unsigned long i;
	unsigned long j, npages;
	void *linear;
	struct page *pg;
	const char *typestr;
	struct kvmppc_linear_info *linear_info;

	if (!count)
		return;

	typestr = (type == KVM_LINEAR_RMA) ? "RMA" : "HPT";

	npages = size >> PAGE_SHIFT;
	linear_info = alloc_bootmem(count * sizeof(struct kvmppc_linear_info));
	for (i = 0; i < count; ++i) {
		linear = alloc_bootmem_align(size, size);
		pr_debug("Allocated KVM %s at %p (%ld MB)\n", typestr, linear,
			 size >> 20);
		linear_info[i].base_virt = linear;
		linear_info[i].base_pfn = __pa(linear) >> PAGE_SHIFT;
		linear_info[i].npages = npages;
		linear_info[i].type = type;
		list_add_tail(&linear_info[i].list, &free_linears);
		atomic_set(&linear_info[i].use_count, 0);

		pg = pfn_to_page(linear_info[i].base_pfn);
		for (j = 0; j < npages; ++j) {
			atomic_inc(&pg->_count);
			++pg;
		}
	}
}

static struct kvmppc_linear_info *kvm_alloc_linear(int type)
{
	struct kvmppc_linear_info *ri, *ret;

	ret = NULL;
	spin_lock(&linear_lock);
	list_for_each_entry(ri, &free_linears, list) {
		if (ri->type != type)
			continue;

		list_del(&ri->list);
		atomic_inc(&ri->use_count);
		memset(ri->base_virt, 0, ri->npages << PAGE_SHIFT);
		ret = ri;
		break;
	}
	spin_unlock(&linear_lock);
	return ret;
}

static void kvm_release_linear(struct kvmppc_linear_info *ri)
{
	if (atomic_dec_and_test(&ri->use_count)) {
		spin_lock(&linear_lock);
		list_add_tail(&ri->list, &free_linears);
		spin_unlock(&linear_lock);

	}
}

/*
 * Called at boot time while the bootmem allocator is active,
 * to allocate contiguous physical memory for the hash page
 * tables for guests.
 */
void __init kvm_linear_init(void)
{
	/* HPT */
	kvm_linear_init_one(1 << kvm_hpt_order, kvm_hpt_count, KVM_LINEAR_HPT);

	/* RMA */
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

	kvm_linear_init_one(kvm_rma_size, kvm_rma_count, KVM_LINEAR_RMA);
}
