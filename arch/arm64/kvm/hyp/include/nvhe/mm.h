/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_MM_H
#define __KVM_HYP_MM_H

#include <asm/kvm_pgtable.h>
#include <asm/spectre.h>
#include <linux/memblock.h>
#include <linux/types.h>

#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

extern struct kvm_pgtable pkvm_pgtable;
extern hyp_spinlock_t pkvm_pgd_lock;

int hyp_create_idmap(u32 hyp_va_bits);
int hyp_map_vectors(void);
int hyp_back_vmemmap(phys_addr_t phys, unsigned long size, phys_addr_t back);
int pkvm_cpu_set_vector(enum arm64_hyp_spectre_vector slot);
int pkvm_create_mappings(void *from, void *to, enum kvm_pgtable_prot prot);
int pkvm_create_mappings_locked(void *from, void *to, enum kvm_pgtable_prot prot);
int __pkvm_create_private_mapping(phys_addr_t phys, size_t size,
				  enum kvm_pgtable_prot prot,
				  unsigned long *haddr);
int pkvm_alloc_private_va_range(size_t size, unsigned long *haddr);

static inline void hyp_vmemmap_range(phys_addr_t phys, unsigned long size,
				     unsigned long *start, unsigned long *end)
{
	unsigned long nr_pages = size >> PAGE_SHIFT;
	struct hyp_page *p = hyp_phys_to_page(phys);

	*start = (unsigned long)p;
	*end = *start + nr_pages * sizeof(struct hyp_page);
	*start = ALIGN_DOWN(*start, PAGE_SIZE);
	*end = ALIGN(*end, PAGE_SIZE);
}

#endif /* __KVM_HYP_MM_H */
