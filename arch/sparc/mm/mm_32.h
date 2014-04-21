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

extern void (*poke_srmmu)(void);
