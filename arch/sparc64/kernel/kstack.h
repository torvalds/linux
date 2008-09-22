#ifndef _KSTACK_H
#define _KSTACK_H

#include <linux/thread_info.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/irq.h>

/* SP must be STACK_BIAS adjusted already.  */
static inline bool kstack_valid(struct thread_info *tp, unsigned long sp)
{
	unsigned long base = (unsigned long) tp;

	if (sp >= (base + sizeof(struct thread_info)) &&
	    sp <= (base + THREAD_SIZE - sizeof(struct sparc_stackf)))
		return true;

	if (hardirq_stack[tp->cpu]) {
		base = (unsigned long) hardirq_stack[tp->cpu];
		if (sp >= base &&
		    sp <= (base + THREAD_SIZE - sizeof(struct sparc_stackf)))
			return true;
		base = (unsigned long) softirq_stack[tp->cpu];
		if (sp >= base &&
		    sp <= (base + THREAD_SIZE - sizeof(struct sparc_stackf)))
			return true;
	}
	return false;
}

/* Does "regs" point to a valid pt_regs trap frame?  */
static inline bool kstack_is_trap_frame(struct thread_info *tp, struct pt_regs *regs)
{
	unsigned long base = (unsigned long) tp;
	unsigned long addr = (unsigned long) regs;

	if (addr >= base &&
	    addr <= (base + THREAD_SIZE - sizeof(*regs)))
		goto check_magic;

	if (hardirq_stack[tp->cpu]) {
		base = (unsigned long) hardirq_stack[tp->cpu];
		if (addr >= base &&
		    addr <= (base + THREAD_SIZE - sizeof(*regs)))
			goto check_magic;
		base = (unsigned long) softirq_stack[tp->cpu];
		if (addr >= base &&
		    addr <= (base + THREAD_SIZE - sizeof(*regs)))
			goto check_magic;
	}
	return false;

check_magic:
	if ((regs->magic & ~0x1ff) == PT_REGS_MAGIC)
		return true;
	return false;

}

#endif /* _KSTACK_H */
