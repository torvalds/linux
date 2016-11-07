#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

#include <linux/notifier.h>

struct pt_regs;

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_KERNELDEBUG,
	DIE_TRAP,
	DIE_GPF,
	DIE_CALL,
	DIE_PAGE_FAULT,
	DIE_NMIUNKNOWN,
};

extern void printk_address(unsigned long address);
extern void die(const char *, struct pt_regs *,long);
extern int __must_check __die(const char *, struct pt_regs *, long);
extern void show_stack_regs(struct pt_regs *regs);
extern void __show_regs(struct pt_regs *regs, int all);
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long, struct pt_regs *, int signr);

#endif /* _ASM_X86_KDEBUG_H */
