// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/pgtable.h>
#include <asm/kvm_gstage.h>

#ifdef CONFIG_64BIT
unsigned long kvm_riscv_gstage_mode __ro_after_init = HGATP_MODE_SV39X4;
unsigned long kvm_riscv_gstage_pgd_levels __ro_after_init = 3;
#else
unsigned long kvm_riscv_gstage_mode __ro_after_init = HGATP_MODE_SV32X4;
unsigned long kvm_riscv_gstage_pgd_levels __ro_after_init = 2;
#endif

#define gstage_pte_leaf(__ptep)	\
	(pte_val(*(__ptep)) & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC))

static inline unsigned long gstage_pte_index(gpa_t addr, u32 level)
{
	unsigned long mask;
	unsigned long shift = HGATP_PAGE_SHIFT + (kvm_riscv_gstage_index_bits * level);

	if (level == (kvm_riscv_gstage_pgd_levels - 1))
		mask = (PTRS_PER_PTE * (1UL << kvm_riscv_gstage_pgd_xbits)) - 1;
	else
		mask = PTRS_PER_PTE - 1;

	return (addr >> shift) & mask;
}

static inline unsigned long gstage_pte_page_vaddr(pte_t pte)
{
	return (unsigned long)pfn_to_virt(__page_val_to_pfn(pte_val(pte)));
}

static int gstage_page_size_to_level(unsigned long page_size, u32 *out_level)
{
	u32 i;
	unsigned long psz = 1UL << 12;

	for (i = 0; i < kvm_riscv_gstage_pgd_levels; i++) {
		if (page_size == (psz << (i * kvm_riscv_gstage_index_bits))) {
			*out_level = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int gstage_level_to_page_order(u32 level, unsigned long *out_pgorder)
{
	if (kvm_riscv_gstage_pgd_levels < level)
		return -EINVAL;

	*out_pgorder = 12 + (level * kvm_riscv_gstage_index_bits);
	return 0;
}

static int gstage_level_to_page_size(u32 level, unsigned long *out_pgsize)
{
	int rc;
	unsigned long page_order = PAGE_SHIFT;

	rc = gstage_level_to_page_order(level, &page_order);
	if (rc)
		return rc;

	*out_pgsize = BIT(page_order);
	return 0;
}

bool kvm_riscv_gstage_get_leaf(struct kvm_gstage *gstage, gpa_t addr,
			       pte_t **ptepp, u32 *ptep_level)
{
	pte_t *ptep;
	u32 current_level = kvm_riscv_gstage_pgd_levels - 1;

	*ptep_level = current_level;
	ptep = (pte_t *)gstage->pgd;
	ptep = &ptep[gstage_pte_index(addr, current_level)];
	while (ptep && pte_val(ptep_get(ptep))) {
		if (gstage_pte_leaf(ptep)) {
			*ptep_level = current_level;
			*ptepp = ptep;
			return true;
		}

		if (current_level) {
			current_level--;
			*ptep_level = current_level;
			ptep = (pte_t *)gstage_pte_page_vaddr(ptep_get(ptep));
			ptep = &ptep[gstage_pte_index(addr, current_level)];
		} else {
			ptep = NULL;
		}
	}

	return false;
}

static void gstage_tlb_flush(struct kvm_gstage *gstage, u32 level, gpa_t addr)
{
	unsigned long order = PAGE_SHIFT;

	if (gstage_level_to_page_order(level, &order))
		return;
	addr &= ~(BIT(order) - 1);

	if (gstage->flags & KVM_GSTAGE_FLAGS_LOCAL)
		kvm_riscv_local_hfence_gvma_vmid_gpa(gstage->vmid, addr, BIT(order), order);
	else
		kvm_riscv_hfence_gvma_vmid_gpa(gstage->kvm, -1UL, 0, addr, BIT(order), order,
					       gstage->vmid);
}

int kvm_riscv_gstage_set_pte(struct kvm_gstage *gstage,
			     struct kvm_mmu_memory_cache *pcache,
			     const struct kvm_gstage_mapping *map)
{
	u32 current_level = kvm_riscv_gstage_pgd_levels - 1;
	pte_t *next_ptep = (pte_t *)gstage->pgd;
	pte_t *ptep = &next_ptep[gstage_pte_index(map->addr, current_level)];

	if (current_level < map->level)
		return -EINVAL;

	while (current_level != map->level) {
		if (gstage_pte_leaf(ptep))
			return -EEXIST;

		if (!pte_val(ptep_get(ptep))) {
			if (!pcache)
				return -ENOMEM;
			next_ptep = kvm_mmu_memory_cache_alloc(pcache);
			if (!next_ptep)
				return -ENOMEM;
			set_pte(ptep, pfn_pte(PFN_DOWN(__pa(next_ptep)),
					      __pgprot(_PAGE_TABLE)));
		} else {
			if (gstage_pte_leaf(ptep))
				return -EEXIST;
			next_ptep = (pte_t *)gstage_pte_page_vaddr(ptep_get(ptep));
		}

		current_level--;
		ptep = &next_ptep[gstage_pte_index(map->addr, current_level)];
	}

	if (pte_val(*ptep) != pte_val(map->pte)) {
		set_pte(ptep, map->pte);
		if (gstage_pte_leaf(ptep))
			gstage_tlb_flush(gstage, current_level, map->addr);
	}

	return 0;
}

int kvm_riscv_gstage_map_page(struct kvm_gstage *gstage,
			      struct kvm_mmu_memory_cache *pcache,
			      gpa_t gpa, phys_addr_t hpa, unsigned long page_size,
			      bool page_rdonly, bool page_exec,
			      struct kvm_gstage_mapping *out_map)
{
	pgprot_t prot;
	int ret;

	out_map->addr = gpa;
	out_map->level = 0;

	ret = gstage_page_size_to_level(page_size, &out_map->level);
	if (ret)
		return ret;

	/*
	 * A RISC-V implementation can choose to either:
	 * 1) Update 'A' and 'D' PTE bits in hardware
	 * 2) Generate page fault when 'A' and/or 'D' bits are not set
	 *    PTE so that software can update these bits.
	 *
	 * We support both options mentioned above. To achieve this, we
	 * always set 'A' and 'D' PTE bits at time of creating G-stage
	 * mapping. To support KVM dirty page logging with both options
	 * mentioned above, we will write-protect G-stage PTEs to track
	 * dirty pages.
	 */

	if (page_exec) {
		if (page_rdonly)
			prot = PAGE_READ_EXEC;
		else
			prot = PAGE_WRITE_EXEC;
	} else {
		if (page_rdonly)
			prot = PAGE_READ;
		else
			prot = PAGE_WRITE;
	}
	out_map->pte = pfn_pte(PFN_DOWN(hpa), prot);
	out_map->pte = pte_mkdirty(out_map->pte);

	return kvm_riscv_gstage_set_pte(gstage, pcache, out_map);
}

void kvm_riscv_gstage_op_pte(struct kvm_gstage *gstage, gpa_t addr,
			     pte_t *ptep, u32 ptep_level, enum kvm_riscv_gstage_op op)
{
	int i, ret;
	pte_t old_pte, *next_ptep;
	u32 next_ptep_level;
	unsigned long next_page_size, page_size;

	ret = gstage_level_to_page_size(ptep_level, &page_size);
	if (ret)
		return;

	WARN_ON(addr & (page_size - 1));

	if (!pte_val(ptep_get(ptep)))
		return;

	if (ptep_level && !gstage_pte_leaf(ptep)) {
		next_ptep = (pte_t *)gstage_pte_page_vaddr(ptep_get(ptep));
		next_ptep_level = ptep_level - 1;
		ret = gstage_level_to_page_size(next_ptep_level, &next_page_size);
		if (ret)
			return;

		if (op == GSTAGE_OP_CLEAR)
			set_pte(ptep, __pte(0));
		for (i = 0; i < PTRS_PER_PTE; i++)
			kvm_riscv_gstage_op_pte(gstage, addr + i * next_page_size,
						&next_ptep[i], next_ptep_level, op);
		if (op == GSTAGE_OP_CLEAR)
			put_page(virt_to_page(next_ptep));
	} else {
		old_pte = *ptep;
		if (op == GSTAGE_OP_CLEAR)
			set_pte(ptep, __pte(0));
		else if (op == GSTAGE_OP_WP)
			set_pte(ptep, __pte(pte_val(ptep_get(ptep)) & ~_PAGE_WRITE));
		if (pte_val(*ptep) != pte_val(old_pte))
			gstage_tlb_flush(gstage, ptep_level, addr);
	}
}

void kvm_riscv_gstage_unmap_range(struct kvm_gstage *gstage,
				  gpa_t start, gpa_t size, bool may_block)
{
	int ret;
	pte_t *ptep;
	u32 ptep_level;
	bool found_leaf;
	unsigned long page_size;
	gpa_t addr = start, end = start + size;

	while (addr < end) {
		found_leaf = kvm_riscv_gstage_get_leaf(gstage, addr, &ptep, &ptep_level);
		ret = gstage_level_to_page_size(ptep_level, &page_size);
		if (ret)
			break;

		if (!found_leaf)
			goto next;

		if (!(addr & (page_size - 1)) && ((end - addr) >= page_size))
			kvm_riscv_gstage_op_pte(gstage, addr, ptep,
						ptep_level, GSTAGE_OP_CLEAR);

next:
		addr += page_size;

		/*
		 * If the range is too large, release the kvm->mmu_lock
		 * to prevent starvation and lockup detector warnings.
		 */
		if (!(gstage->flags & KVM_GSTAGE_FLAGS_LOCAL) && may_block && addr < end)
			cond_resched_lock(&gstage->kvm->mmu_lock);
	}
}

void kvm_riscv_gstage_wp_range(struct kvm_gstage *gstage, gpa_t start, gpa_t end)
{
	int ret;
	pte_t *ptep;
	u32 ptep_level;
	bool found_leaf;
	gpa_t addr = start;
	unsigned long page_size;

	while (addr < end) {
		found_leaf = kvm_riscv_gstage_get_leaf(gstage, addr, &ptep, &ptep_level);
		ret = gstage_level_to_page_size(ptep_level, &page_size);
		if (ret)
			break;

		if (!found_leaf)
			goto next;

		if (!(addr & (page_size - 1)) && ((end - addr) >= page_size))
			kvm_riscv_gstage_op_pte(gstage, addr, ptep,
						ptep_level, GSTAGE_OP_WP);

next:
		addr += page_size;
	}
}

void __init kvm_riscv_gstage_mode_detect(void)
{
#ifdef CONFIG_64BIT
	/* Try Sv57x4 G-stage mode */
	csr_write(CSR_HGATP, HGATP_MODE_SV57X4 << HGATP_MODE_SHIFT);
	if ((csr_read(CSR_HGATP) >> HGATP_MODE_SHIFT) == HGATP_MODE_SV57X4) {
		kvm_riscv_gstage_mode = HGATP_MODE_SV57X4;
		kvm_riscv_gstage_pgd_levels = 5;
		goto done;
	}

	/* Try Sv48x4 G-stage mode */
	csr_write(CSR_HGATP, HGATP_MODE_SV48X4 << HGATP_MODE_SHIFT);
	if ((csr_read(CSR_HGATP) >> HGATP_MODE_SHIFT) == HGATP_MODE_SV48X4) {
		kvm_riscv_gstage_mode = HGATP_MODE_SV48X4;
		kvm_riscv_gstage_pgd_levels = 4;
		goto done;
	}

	/* Try Sv39x4 G-stage mode */
	csr_write(CSR_HGATP, HGATP_MODE_SV39X4 << HGATP_MODE_SHIFT);
	if ((csr_read(CSR_HGATP) >> HGATP_MODE_SHIFT) == HGATP_MODE_SV39X4) {
		kvm_riscv_gstage_mode = HGATP_MODE_SV39X4;
		kvm_riscv_gstage_pgd_levels = 3;
		goto done;
	}
#else /* CONFIG_32BIT */
	/* Try Sv32x4 G-stage mode */
	csr_write(CSR_HGATP, HGATP_MODE_SV32X4 << HGATP_MODE_SHIFT);
	if ((csr_read(CSR_HGATP) >> HGATP_MODE_SHIFT) == HGATP_MODE_SV32X4) {
		kvm_riscv_gstage_mode = HGATP_MODE_SV32X4;
		kvm_riscv_gstage_pgd_levels = 2;
		goto done;
	}
#endif

	/* KVM depends on !HGATP_MODE_OFF */
	kvm_riscv_gstage_mode = HGATP_MODE_OFF;
	kvm_riscv_gstage_pgd_levels = 0;

done:
	csr_write(CSR_HGATP, 0);
	kvm_riscv_local_hfence_gvma_all();
}
