// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mem_encrypt.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/pagewalk.h>

#include <asm/cacheflush.h>
#include <asm/pgtable-prot.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>
#include <asm/kfence.h>

struct page_change_data {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

static ptdesc_t set_pageattr_masks(ptdesc_t val, struct mm_walk *walk)
{
	struct page_change_data *masks = walk->private;

	val &= ~(pgprot_val(masks->clear_mask));
	val |= (pgprot_val(masks->set_mask));

	return val;
}

static int pageattr_pud_entry(pud_t *pud, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pud_t val = pudp_get(pud);

	if (pud_sect(val)) {
		if (WARN_ON_ONCE((next - addr) != PUD_SIZE))
			return -EINVAL;
		val = __pud(set_pageattr_masks(pud_val(val), walk));
		set_pud(pud, val);
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int pageattr_pmd_entry(pmd_t *pmd, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pmd_t val = pmdp_get(pmd);

	if (pmd_sect(val)) {
		if (WARN_ON_ONCE((next - addr) != PMD_SIZE))
			return -EINVAL;
		val = __pmd(set_pageattr_masks(pmd_val(val), walk));
		set_pmd(pmd, val);
		walk->action = ACTION_CONTINUE;
	}

	return 0;
}

static int pageattr_pte_entry(pte_t *pte, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pte_t val = __ptep_get(pte);

	val = __pte(set_pageattr_masks(pte_val(val), walk));
	__set_pte(pte, val);

	return 0;
}

static const struct mm_walk_ops pageattr_ops = {
	.pud_entry	= pageattr_pud_entry,
	.pmd_entry	= pageattr_pmd_entry,
	.pte_entry	= pageattr_pte_entry,
};

bool rodata_full __ro_after_init = true;

bool can_set_direct_map(void)
{
	/*
	 * rodata_full, DEBUG_PAGEALLOC and a Realm guest all require linear
	 * map to be mapped at page granularity, so that it is possible to
	 * protect/unprotect single pages.
	 *
	 * KFENCE pool requires page-granular mapping if initialized late.
	 *
	 * Realms need to make pages shared/protected at page granularity.
	 */
	return rodata_full || debug_pagealloc_enabled() ||
		arm64_kfence_can_set_direct_map() || is_realm_world();
}

static int update_range_prot(unsigned long start, unsigned long size,
			     pgprot_t set_mask, pgprot_t clear_mask)
{
	struct page_change_data data;
	int ret;

	data.set_mask = set_mask;
	data.clear_mask = clear_mask;

	ret = split_kernel_leaf_mapping(start, start + size);
	if (WARN_ON_ONCE(ret))
		return ret;

	arch_enter_lazy_mmu_mode();

	/*
	 * The caller must ensure that the range we are operating on does not
	 * partially overlap a block mapping, or a cont mapping. Any such case
	 * must be eliminated by splitting the mapping.
	 */
	ret = walk_kernel_page_table_range_lockless(start, start + size,
						    &pageattr_ops, NULL, &data);
	arch_leave_lazy_mmu_mode();

	return ret;
}

static int __change_memory_common(unsigned long start, unsigned long size,
				  pgprot_t set_mask, pgprot_t clear_mask)
{
	int ret;

	ret = update_range_prot(start, size, set_mask, clear_mask);

	/*
	 * If the memory is being made valid without changing any other bits
	 * then a TLBI isn't required as a non-valid entry cannot be cached in
	 * the TLB.
	 */
	if (pgprot_val(set_mask) != PTE_VALID || pgprot_val(clear_mask))
		flush_tlb_kernel_range(start, start + size);
	return ret;
}

static int change_memory_common(unsigned long addr, int numpages,
				pgprot_t set_mask, pgprot_t clear_mask)
{
	unsigned long start = addr;
	unsigned long size = PAGE_SIZE * numpages;
	unsigned long end = start + size;
	struct vm_struct *area;
	int i;

	if (!PAGE_ALIGNED(addr)) {
		start &= PAGE_MASK;
		end = start + size;
		WARN_ON_ONCE(1);
	}

	/*
	 * Kernel VA mappings are always live, and splitting live section
	 * mappings into page mappings may cause TLB conflicts. This means
	 * we have to ensure that changing the permission bits of the range
	 * we are operating on does not result in such splitting.
	 *
	 * Let's restrict ourselves to mappings created by vmalloc (or vmap).
	 * Disallow VM_ALLOW_HUGE_VMAP mappings to guarantee that only page
	 * mappings are updated and splitting is never needed.
	 *
	 * So check whether the [addr, addr + size) interval is entirely
	 * covered by precisely one VM area that has the VM_ALLOC flag set.
	 */
	area = find_vm_area((void *)addr);
	if (!area ||
	    end > (unsigned long)kasan_reset_tag(area->addr) + area->size ||
	    ((area->flags & (VM_ALLOC | VM_ALLOW_HUGE_VMAP)) != VM_ALLOC))
		return -EINVAL;

	if (!numpages)
		return 0;

	/*
	 * If we are manipulating read-only permissions, apply the same
	 * change to the linear mapping of the pages that back this VM area.
	 */
	if (rodata_full && (pgprot_val(set_mask) == PTE_RDONLY ||
			    pgprot_val(clear_mask) == PTE_RDONLY)) {
		for (i = 0; i < area->nr_pages; i++) {
			__change_memory_common((u64)page_address(area->pages[i]),
					       PAGE_SIZE, set_mask, clear_mask);
		}
	}

	/*
	 * Get rid of potentially aliasing lazily unmapped vm areas that may
	 * have permissions set that deviate from the ones we are setting here.
	 */
	vm_unmap_aliases();

	return __change_memory_common(start, size, set_mask, clear_mask);
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_RDONLY),
					__pgprot(PTE_WRITE));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_WRITE),
					__pgprot(PTE_RDONLY));
}

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_PXN),
					__pgprot(PTE_MAYBE_GP));
}

int set_memory_x(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_MAYBE_GP),
					__pgprot(PTE_PXN));
}

int set_memory_valid(unsigned long addr, int numpages, int enable)
{
	if (enable)
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					__pgprot(PTE_VALID),
					__pgprot(0));
	else
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					__pgprot(0),
					__pgprot(PTE_VALID));
}

int set_direct_map_invalid_noflush(struct page *page)
{
	pgprot_t clear_mask = __pgprot(PTE_VALID);
	pgprot_t set_mask = __pgprot(0);

	if (!can_set_direct_map())
		return 0;

	return update_range_prot((unsigned long)page_address(page),
				 PAGE_SIZE, set_mask, clear_mask);
}

int set_direct_map_default_noflush(struct page *page)
{
	pgprot_t set_mask = __pgprot(PTE_VALID | PTE_WRITE);
	pgprot_t clear_mask = __pgprot(PTE_RDONLY);

	if (!can_set_direct_map())
		return 0;

	return update_range_prot((unsigned long)page_address(page),
				 PAGE_SIZE, set_mask, clear_mask);
}

static int __set_memory_enc_dec(unsigned long addr,
				int numpages,
				bool encrypt)
{
	unsigned long set_prot = 0, clear_prot = 0;
	phys_addr_t start, end;
	int ret;

	if (!is_realm_world())
		return 0;

	if (!__is_lm_address(addr))
		return -EINVAL;

	start = __virt_to_phys(addr);
	end = start + numpages * PAGE_SIZE;

	if (encrypt)
		clear_prot = PROT_NS_SHARED;
	else
		set_prot = PROT_NS_SHARED;

	/*
	 * Break the mapping before we make any changes to avoid stale TLB
	 * entries or Synchronous External Aborts caused by RIPAS_EMPTY
	 */
	ret = __change_memory_common(addr, PAGE_SIZE * numpages,
				     __pgprot(set_prot),
				     __pgprot(clear_prot | PTE_VALID));

	if (ret)
		return ret;

	if (encrypt)
		ret = rsi_set_memory_range_protected(start, end);
	else
		ret = rsi_set_memory_range_shared(start, end);

	if (ret)
		return ret;

	return __change_memory_common(addr, PAGE_SIZE * numpages,
				      __pgprot(PTE_VALID),
				      __pgprot(0));
}

static int realm_set_memory_encrypted(unsigned long addr, int numpages)
{
	int ret = __set_memory_enc_dec(addr, numpages, true);

	/*
	 * If the request to change state fails, then the only sensible cause
	 * of action for the caller is to leak the memory
	 */
	WARN(ret, "Failed to encrypt memory, %d pages will be leaked",
	     numpages);

	return ret;
}

static int realm_set_memory_decrypted(unsigned long addr, int numpages)
{
	int ret = __set_memory_enc_dec(addr, numpages, false);

	WARN(ret, "Failed to decrypt memory, %d pages will be leaked",
	     numpages);

	return ret;
}

static const struct arm64_mem_crypt_ops realm_crypt_ops = {
	.encrypt = realm_set_memory_encrypted,
	.decrypt = realm_set_memory_decrypted,
};

int realm_register_memory_enc_ops(void)
{
	return arm64_mem_crypt_ops_register(&realm_crypt_ops);
}

int set_direct_map_valid_noflush(struct page *page, unsigned nr, bool valid)
{
	unsigned long addr = (unsigned long)page_address(page);

	if (!can_set_direct_map())
		return 0;

	return set_memory_valid(addr, nr, valid);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
/*
 * This is - apart from the return value - doing the same
 * thing as the new set_direct_map_valid_noflush() function.
 *
 * Unify? Explain the conceptual differences?
 */
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (!can_set_direct_map())
		return;

	set_memory_valid((unsigned long)page_address(page), numpages, enable);
}
#endif /* CONFIG_DEBUG_PAGEALLOC */

/*
 * This function is used to determine if a linear map page has been marked as
 * not-valid. Walk the page table and check the PTE_VALID bit.
 *
 * Because this is only called on the kernel linear map,  p?d_sect() implies
 * p?d_present(). When debug_pagealloc is enabled, sections mappings are
 * disabled.
 */
bool kernel_page_present(struct page *page)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep;
	unsigned long addr = (unsigned long)page_address(page);

	pgdp = pgd_offset_k(addr);
	if (pgd_none(READ_ONCE(*pgdp)))
		return false;

	p4dp = p4d_offset(pgdp, addr);
	if (p4d_none(READ_ONCE(*p4dp)))
		return false;

	pudp = pud_offset(p4dp, addr);
	pud = READ_ONCE(*pudp);
	if (pud_none(pud))
		return false;
	if (pud_sect(pud))
		return true;

	pmdp = pmd_offset(pudp, addr);
	pmd = READ_ONCE(*pmdp);
	if (pmd_none(pmd))
		return false;
	if (pmd_sect(pmd))
		return true;

	ptep = pte_offset_kernel(pmdp, addr);
	return pte_valid(__ptep_get(ptep));
}
