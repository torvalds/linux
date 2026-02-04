// SPDX-License-Identifier: GPL-2.0
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2020, 2024
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 David Hildenbrand <david@redhat.com>
 *		 Janosch Frank <frankja@linux.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/pagewalk.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/ksm.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pgtable.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/pgalloc.h>

#include <asm/page-states.h>
#include <asm/tlb.h>
#include "dat.h"

int kvm_s390_mmu_cache_topup(struct kvm_s390_mmu_cache *mc)
{
	void *o;

	for ( ; mc->n_crsts < KVM_S390_MMU_CACHE_N_CRSTS; mc->n_crsts++) {
		o = (void *)__get_free_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
		if (!o)
			return -ENOMEM;
		mc->crsts[mc->n_crsts] = o;
	}
	for ( ; mc->n_pts < KVM_S390_MMU_CACHE_N_PTS; mc->n_pts++) {
		o = (void *)__get_free_page(GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->pts[mc->n_pts] = o;
	}
	for ( ; mc->n_rmaps < KVM_S390_MMU_CACHE_N_RMAPS; mc->n_rmaps++) {
		o = kzalloc(sizeof(*mc->rmaps[0]), GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->rmaps[mc->n_rmaps] = o;
	}
	return 0;
}

static inline struct page_table *dat_alloc_pt_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct page_table *res;

	res = kvm_s390_mmu_cache_alloc_pt(mc);
	if (res)
		__arch_set_page_dat(res, 1);
	return res;
}

static inline struct crst_table *dat_alloc_crst_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct crst_table *res;

	res = kvm_s390_mmu_cache_alloc_crst(mc);
	if (res)
		__arch_set_page_dat(res, 1UL << CRST_ALLOC_ORDER);
	return res;
}

struct crst_table *dat_alloc_crst_sleepable(unsigned long init)
{
	struct page *page;
	void *virt;

	page = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
	if (!page)
		return NULL;
	virt = page_to_virt(page);
	__arch_set_page_dat(virt, 1UL << CRST_ALLOC_ORDER);
	crst_table_init(virt, init);
	return virt;
}

void dat_free_level(struct crst_table *table, bool owns_ptes)
{
	unsigned int i;

	for (i = 0; i < _CRST_ENTRIES; i++) {
		if (table->crstes[i].h.fc || table->crstes[i].h.i)
			continue;
		if (!is_pmd(table->crstes[i]))
			dat_free_level(dereference_crste(table->crstes[i]), owns_ptes);
		else if (owns_ptes)
			dat_free_pt(dereference_pmd(table->crstes[i].pmd));
	}
	dat_free_crst(table);
}
