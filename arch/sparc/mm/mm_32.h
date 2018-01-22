/* SPDX-License-Identifier: GPL-2.0 */
/* fault_32.c - visible as they are called from assembler */
asmlinkage int lookup_fault(unsigned long pc, unsigned long ret_pc,
                            unsigned long address);
asmlinkage void do_sparc_fault(struct pt_regs *regs, int text_fault, int write,
                               unsigned long address);

void window_overflow_fault(void);
void window_underflow_fault(unsigned long sp);
void window_ret_fault(struct pt_regs *regs);

/* srmmu.c */
extern char *srmmu_name;
extern int viking_mxcc_present;
extern int flush_page_for_dma_global;

extern void (*poke_srmmu)(void);

void __init srmmu_paging_init(void);

/* iommu.c */
void ld_mmu_iommu(void);

/* io-unit.c */
void ld_mmu_iounit(void);
