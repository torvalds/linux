/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#ifndef __RISCV_KVM_GSTAGE_H_
#define __RISCV_KVM_GSTAGE_H_

#include <linux/kvm_types.h>

struct kvm_gstage {
	struct kvm *kvm;
	unsigned long flags;
#define KVM_GSTAGE_FLAGS_LOCAL		BIT(0)
	unsigned long vmid;
	pgd_t *pgd;
	unsigned long pgd_levels;
};

struct kvm_gstage_mapping {
	gpa_t addr;
	pte_t pte;
	u32 level;
};

#ifdef CONFIG_64BIT
#define kvm_riscv_gstage_index_bits	9
#else
#define kvm_riscv_gstage_index_bits	10
#endif

extern unsigned long kvm_riscv_gstage_max_pgd_levels;

#define kvm_riscv_gstage_pgd_xbits	2
#define kvm_riscv_gstage_pgd_size	(1UL << (HGATP_PAGE_SHIFT + kvm_riscv_gstage_pgd_xbits))

static inline unsigned long kvm_riscv_gstage_gpa_bits(unsigned long pgd_levels)
{
	return (HGATP_PAGE_SHIFT +
		pgd_levels * kvm_riscv_gstage_index_bits +
		kvm_riscv_gstage_pgd_xbits);
}

static inline gpa_t kvm_riscv_gstage_gpa_size(unsigned long pgd_levels)
{
	return BIT_ULL(kvm_riscv_gstage_gpa_bits(pgd_levels));
}

bool kvm_riscv_gstage_get_leaf(struct kvm_gstage *gstage, gpa_t addr,
			       pte_t **ptepp, u32 *ptep_level);

int kvm_riscv_gstage_set_pte(struct kvm_gstage *gstage,
			     struct kvm_mmu_memory_cache *pcache,
			     const struct kvm_gstage_mapping *map);

int kvm_riscv_gstage_map_page(struct kvm_gstage *gstage,
			      struct kvm_mmu_memory_cache *pcache,
			      gpa_t gpa, phys_addr_t hpa, unsigned long page_size,
			      bool page_rdonly, bool page_exec,
			      struct kvm_gstage_mapping *out_map);

int kvm_riscv_gstage_split_huge(struct kvm_gstage *gstage,
				struct kvm_mmu_memory_cache *pcache,
				gpa_t addr, u32 target_level, bool flush);

enum kvm_riscv_gstage_op {
	GSTAGE_OP_NOP = 0,	/* Nothing */
	GSTAGE_OP_CLEAR,	/* Clear/Unmap */
	GSTAGE_OP_WP,		/* Write-protect */
};

void kvm_riscv_gstage_op_pte(struct kvm_gstage *gstage, gpa_t addr,
			     pte_t *ptep, u32 ptep_level, enum kvm_riscv_gstage_op op);

void kvm_riscv_gstage_unmap_range(struct kvm_gstage *gstage,
				  gpa_t start, gpa_t size, bool may_block);

void kvm_riscv_gstage_wp_range(struct kvm_gstage *gstage, gpa_t start, gpa_t end);

void kvm_riscv_gstage_mode_detect(void);

static inline unsigned long kvm_riscv_gstage_mode(unsigned long pgd_levels)
{
	switch (pgd_levels) {
	case 2:
		return HGATP_MODE_SV32X4;
	case 3:
		return HGATP_MODE_SV39X4;
	case 4:
		return HGATP_MODE_SV48X4;
	case 5:
		return HGATP_MODE_SV57X4;
	default:
		WARN_ON_ONCE(1);
		return HGATP_MODE_OFF;
	}
}

static inline void kvm_riscv_gstage_init(struct kvm_gstage *gstage, struct kvm *kvm)
{
	gstage->kvm = kvm;
	gstage->flags = 0;
	gstage->vmid = READ_ONCE(kvm->arch.vmid.vmid);
	gstage->pgd = kvm->arch.pgd;
	gstage->pgd_levels = kvm->arch.pgd_levels;
}

#endif
