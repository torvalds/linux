/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_TLBEX_H
#define __ASM_TLBEX_H

#include <asm/uasm.h>

/*
 * Write random or indexed TLB entry, and care about the hazards from
 * the preceding mtc0 and for the following eret.
 */
enum tlb_write_entry {
	tlb_random,
	tlb_indexed
};

extern int pgd_reg;

void build_get_pmde64(u32 **p, struct uasm_label **l, struct uasm_reloc **r,
		      unsigned int tmp, unsigned int ptr);
void build_get_pgde32(u32 **p, unsigned int tmp, unsigned int ptr);
void build_get_ptep(u32 **p, unsigned int tmp, unsigned int ptr);
void build_update_entries(u32 **p, unsigned int tmp, unsigned int ptep);
void build_tlb_write_entry(u32 **p, struct uasm_label **l,
			   struct uasm_reloc **r,
			   enum tlb_write_entry wmode);

extern void handle_tlbl(void);
extern char handle_tlbl_end[];

extern void handle_tlbs(void);
extern char handle_tlbs_end[];

extern void handle_tlbm(void);
extern char handle_tlbm_end[];

#endif /* __ASM_TLBEX_H */
