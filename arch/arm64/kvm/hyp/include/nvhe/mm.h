/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_MM_H
#define __KVM_HYP_MM_H

#include <asm/kvm_pgtable.h>
#include <asm/spectre.h>
#include <linux/memblock.h>
#include <linux/types.h>

#include <nvhe/memory.h>
#include <nvhe/spinlock.h>

#define HYP_MEMBLOCK_REGIONS 128
extern struct memblock_region kvm_nvhe_sym(hyp_memory)[];
extern unsigned int kvm_nvhe_sym(hyp_memblock_nr);
extern struct kvm_pgtable pkvm_pgtable;
extern hyp_spinlock_t pkvm_pgd_lock;
extern struct hyp_pool hpool;
extern u64 __io_map_base;

int hyp_create_idmap(u32 hyp_va_bits);
int hyp_map_vectors(void);
int hyp_back_vmemmap(phys_addr_t phys, unsigned long size, phys_addr_t back);
int pkvm_cpu_set_vector(enum arm64_hyp_spectre_vector slot);
int pkvm_create_mappings(void *from, void *to, enum kvm_pgtable_prot prot);
int __pkvm_create_mappings(unsigned long start, unsigned long size,
			   unsigned long phys, enum kvm_pgtable_prot prot);
unsigned long __pkvm_create_private_mapping(phys_addr_t phys, size_t size,
					    enum kvm_pgtable_prot prot);

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

static inline unsigned long __hyp_pgtable_max_pages(unsigned long nr_pages)
{
	unsigned long total = 0, i;

	/* Provision the worst case scenario */
	for (i = 0; i < KVM_PGTABLE_MAX_LEVELS; i++) {
		nr_pages = DIV_ROUND_UP(nr_pages, PTRS_PER_PTE);
		total += nr_pages;
	}

	return total;
}

static inline unsigned long __hyp_pgtable_total_pages(void)
{
	unsigned long res = 0, i;

	/* Cover all of memory with page-granularity */
	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		struct memblock_region *reg = &kvm_nvhe_sym(hyp_memory)[i];
		res += __hyp_pgtable_max_pages(reg->size >> PAGE_SHIFT);
	}

	return res;
}

static inline unsigned long hyp_s1_pgtable_pages(void)
{
	unsigned long res;

	res = __hyp_pgtable_total_pages();

	/* Allow 1 GiB for private mappings */
	res += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT);

	return res;
}

static inline unsigned long host_s2_mem_pgtable_pages(void)
{
	/*
	 * Include an extra 16 pages to safely upper-bound the worst case of
	 * concatenated pgds.
	 */
	return __hyp_pgtable_total_pages() + 16;
}

static inline unsigned long host_s2_dev_pgtable_pages(void)
{
	/* Allow 1 GiB for MMIO mappings */
	return __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT);
}

#endif /* __KVM_HYP_MM_H */
