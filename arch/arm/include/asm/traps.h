#ifndef _ASMARM_TRAP_H
#define _ASMARM_TRAP_H

#include <linux/list.h>

struct pt_regs;
struct task_struct;

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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static inline int __in_irqentry_text(unsigned long ptr)
{
	extern char __irqentry_text_start[];
	extern char __irqentry_text_end[];

	return ptr >= (unsigned long)&__irqentry_text_start &&
	       ptr < (unsigned long)&__irqentry_text_end;
}
#else
static inline int __in_irqentry_text(unsigned long ptr)
{
	return 0;
}
#endif

static inline int in_exception_text(unsigned long ptr)
{
	extern char __exception_text_start[];
	extern char __exception_text_end[];
	int in;

	in = ptr >= (unsigned long)&__exception_text_start &&
	     ptr < (unsigned long)&__exception_text_end;

	return in ? : __in_irqentry_text(ptr);
}

extern void __init early_trap_init(void);
extern void dump_backtrace_entry(unsigned long where, unsigned long from, unsigned long frame);
extern void ptrace_break(struct task_struct *tsk, struct pt_regs *regs);

extern void *vectors_page;

#endif
