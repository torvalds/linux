#ifndef _SPARC64_MM_INIT_H
#define _SPARC64_MM_INIT_H

#include <asm/page.h>

/* Most of the symbols in this file are defined in init.c and
 * marked non-static so that assembler code can get at them.
 */

#define MAX_PHYS_ADDRESS	(1UL << MAX_PHYS_ADDRESS_BITS)

extern unsigned long kern_linear_pte_xor[4];
extern unsigned int sparc64_highest_unlocked_tlb_ent;
extern unsigned long sparc64_kern_pri_context;
extern unsigned long sparc64_kern_pri_nuc_bits;
extern unsigned long sparc64_kern_sec_context;
void mmu_info(struct seq_file *m);

struct linux_prom_translation {
	unsigned long virt;
	unsigned long size;
	unsigned long data;
};

/* Exported for kernel TLB miss handling in ktlb.S */
extern struct linux_prom_translation prom_trans[512];
extern unsigned int prom_trans_ents;

/* Exported for SMP bootup purposes. */
extern unsigned long kern_locked_tte_data;

void prom_world(int enter);

#endif /* _SPARC64_MM_INIT_H */
