/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGARCH_KVM_MMU_H__
#define __ASM_LOONGARCH_KVM_MMU_H__

#include <linux/kvm_host.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

/*
 * KVM_MMU_CACHE_MIN_PAGES is the number of GPA page table translation levels
 * for which pages need to be cached.
 */
#define KVM_MMU_CACHE_MIN_PAGES	(CONFIG_PGTABLE_LEVELS - 1)

#define _KVM_FLUSH_PGTABLE	0x1
#define _KVM_HAS_PGMASK		0x2
#define kvm_pfn_pte(pfn, prot)	(((pfn) << PFN_PTE_SHIFT) | pgprot_val(prot))
#define kvm_pte_pfn(x)		((phys_addr_t)((x & _PFN_MASK) >> PFN_PTE_SHIFT))

typedef unsigned long kvm_pte_t;
typedef struct kvm_ptw_ctx kvm_ptw_ctx;
typedef int (*kvm_pte_ops)(kvm_pte_t *pte, phys_addr_t addr, kvm_ptw_ctx *ctx);

struct kvm_ptw_ctx {
	kvm_pte_ops     ops;
	unsigned long   flag;

	/* for kvm_arch_mmu_enable_log_dirty_pt_masked use */
	unsigned long   mask;
	unsigned long   gfn;

	/* page walk mmu info */
	unsigned int    level;
	unsigned long   pgtable_shift;
	unsigned long   invalid_entry;
	unsigned long   *invalid_ptes;
	unsigned int    *pte_shifts;
	void		*opaque;

	/* free pte table page list */
	struct list_head list;
};

kvm_pte_t *kvm_pgd_alloc(void);

static inline void kvm_set_pte(kvm_pte_t *ptep, kvm_pte_t val)
{
	WRITE_ONCE(*ptep, val);
}

static inline int kvm_pte_write(kvm_pte_t pte) { return pte & _PAGE_WRITE; }
static inline int kvm_pte_dirty(kvm_pte_t pte) { return pte & _PAGE_DIRTY; }
static inline int kvm_pte_young(kvm_pte_t pte) { return pte & _PAGE_ACCESSED; }
static inline int kvm_pte_huge(kvm_pte_t pte) { return pte & _PAGE_HUGE; }

static inline kvm_pte_t kvm_pte_mkyoung(kvm_pte_t pte)
{
	return pte | _PAGE_ACCESSED;
}

static inline kvm_pte_t kvm_pte_mkold(kvm_pte_t pte)
{
	return pte & ~_PAGE_ACCESSED;
}

static inline kvm_pte_t kvm_pte_mkdirty(kvm_pte_t pte)
{
	return pte | _PAGE_DIRTY;
}

static inline kvm_pte_t kvm_pte_mkclean(kvm_pte_t pte)
{
	return pte & ~_PAGE_DIRTY;
}

static inline kvm_pte_t kvm_pte_mkhuge(kvm_pte_t pte)
{
	return pte | _PAGE_HUGE;
}

static inline kvm_pte_t kvm_pte_mksmall(kvm_pte_t pte)
{
	return pte & ~_PAGE_HUGE;
}

static inline int kvm_need_flush(kvm_ptw_ctx *ctx)
{
	return ctx->flag & _KVM_FLUSH_PGTABLE;
}

static inline kvm_pte_t *kvm_pgtable_offset(kvm_ptw_ctx *ctx, kvm_pte_t *table,
					phys_addr_t addr)
{

	return table + ((addr >> ctx->pgtable_shift) & (PTRS_PER_PTE - 1));
}

static inline phys_addr_t kvm_pgtable_addr_end(kvm_ptw_ctx *ctx,
				phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t boundary, size;

	size = 0x1UL << ctx->pgtable_shift;
	boundary = (addr + size) & ~(size - 1);
	return (boundary - 1 < end - 1) ? boundary : end;
}

static inline int kvm_pte_present(kvm_ptw_ctx *ctx, kvm_pte_t *entry)
{
	if (!ctx || ctx->level == 0)
		return !!(*entry & _PAGE_PRESENT);

	return *entry != ctx->invalid_entry;
}

static inline int kvm_pte_none(kvm_ptw_ctx *ctx, kvm_pte_t *entry)
{
	return *entry == ctx->invalid_entry;
}

static inline void kvm_ptw_enter(kvm_ptw_ctx *ctx)
{
	ctx->level--;
	ctx->pgtable_shift = ctx->pte_shifts[ctx->level];
	ctx->invalid_entry = ctx->invalid_ptes[ctx->level];
}

static inline void kvm_ptw_exit(kvm_ptw_ctx *ctx)
{
	ctx->level++;
	ctx->pgtable_shift = ctx->pte_shifts[ctx->level];
	ctx->invalid_entry = ctx->invalid_ptes[ctx->level];
}

#endif /* __ASM_LOONGARCH_KVM_MMU_H__ */
