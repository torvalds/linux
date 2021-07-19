/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HYP_EARLY_ALLOC_H
#define __KVM_HYP_EARLY_ALLOC_H

#include <asm/kvm_pgtable.h>

void hyp_early_alloc_init(void *virt, unsigned long size);
unsigned long hyp_early_alloc_nr_used_pages(void);
void *hyp_early_alloc_page(void *arg);
void *hyp_early_alloc_contig(unsigned int nr_pages);

extern struct kvm_pgtable_mm_ops hyp_early_alloc_mm_ops;

#endif /* __KVM_HYP_EARLY_ALLOC_H */
