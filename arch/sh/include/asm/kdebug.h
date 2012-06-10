#ifndef __ASM_SH_KDEBUG_H
#define __ASM_SH_KDEBUG_H

/* Grossly misnamed. */
enum die_val {
	DIE_TRAP,
	DIE_NMI,
	DIE_OOPS,
	DIE_BREAKPOINT,
	DIE_SSTEP,
};

extern void printk_address(unsigned long address, int reliable);

#endif /* __ASM_SH_KDEBUG_H */
