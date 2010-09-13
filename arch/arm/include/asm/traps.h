#ifndef _ASMARM_TRAP_H
#define _ASMARM_TRAP_H

#include <linux/list.h>

struct undef_hook {
	struct list_head node;
	u32 instr_mask;
	u32 instr_val;
	u32 cpsr_mask;
	u32 cpsr_val;
	int (*fn)(struct pt_regs *regs, unsigned int instr);
};

void register_undef_hook(struct undef_hook *hook);
void unregister_undef_hook(struct undef_hook *hook);

static inline int in_exception_text(unsigned long ptr)
{
	extern char __exception_text_start[];
	extern char __exception_text_end[];

	return ptr >= (unsigned long)&__exception_text_start &&
	       ptr < (unsigned long)&__exception_text_end;
}

extern void __init early_trap_init(void);
extern void dump_backtrace_entry(unsigned long where, unsigned long from, unsigned long frame);

extern void *vectors_page;

#endif
