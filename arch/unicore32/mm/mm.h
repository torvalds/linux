/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/mm/mm.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <asm/hwdef-copro.h>

/* the upper-most page table pointer */
extern pmd_t *top_pmd;
extern int sysctl_overcommit_memory;

#define TOP_PTE(x)	pte_offset_kernel(top_pmd, x)

struct mem_type {
	unsigned int prot_pte;
	unsigned int prot_l1;
	unsigned int prot_sect;
};

const struct mem_type *get_mem_type(unsigned int type);

extern void __flush_dcache_page(struct address_space *, struct page *);
extern void hook_fault_code(int nr, int (*fn)
		(unsigned long, unsigned int, struct pt_regs *),
		int sig, int code, const char *name);

void __init bootmem_init(void);
void uc32_mm_memblock_reserve(void);
