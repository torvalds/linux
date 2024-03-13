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
extern const struct pkvm_module_ops module_ops;

int hyp_create_pcpu_fixmap(void);
void *hyp_fixmap_map(phys_addr_t phys);
void *hyp_fixmap_map_nc(phys_addr_t phys);
void hyp_fixmap_unmap(void);
void hyp_poison_page(phys_addr_t phys);

int hyp_create_idmap(u32 hyp_va_bits);
int hyp_map_vectors(void);
int hyp_back_vmemmap(phys_addr_t back);
int pkvm_cpu_set_vector(enum arm64_hyp_spectre_vector slot);
int pkvm_create_mappings(void *from, void *to, enum kvm_pgtable_prot prot);
int pkvm_create_mappings_locked(void *from, void *to, enum kvm_pgtable_prot prot);
int __pkvm_create_private_mapping(phys_addr_t phys, size_t size,
				  enum kvm_pgtable_prot prot,
				  unsigned long *haddr);
int pkvm_alloc_private_va_range(size_t size, unsigned long *haddr);
void pkvm_remove_mappings(void *from, void *to);

int __pkvm_map_module_page(u64 pfn, void *va, enum kvm_pgtable_prot prot, bool is_protected);
void __pkvm_unmap_module_page(u64 pfn, void *va);
void *__pkvm_alloc_module_va(u64 nr_pages);
#ifdef CONFIG_NVHE_EL2_DEBUG
void assert_in_mod_range(unsigned long addr);
#else
static inline void assert_in_mod_range(unsigned long addr) { }
#endif /* CONFIG_NVHE_EL2_DEBUG */
#endif /* __KVM_HYP_MM_H */
