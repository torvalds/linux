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

	/* Stack pointer must be 16-byte aligned.  */
	if (sp & (16UL - 1))
		return false;

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

static inline __attribute__((always_inline)) void *set_hardirq_stack(void)
{
	void *orig_sp, *sp = hardirq_stack[smp_processor_id()];

	__asm__ __volatile__("mov %%sp, %0" : "=r" (orig_sp));
	if (orig_sp < sp ||
	    orig_sp > (sp + THREAD_SIZE)) {
		sp += THREAD_SIZE - 192 - STACK_BIAS;
		__asm__ __volatile__("mov %0, %%sp" : : "r" (sp));
	}

	return orig_sp;
}

static inline __attribute__((always_inline)) void restore_hardirq_stack(void *orig_sp)
{
	__asm__ __volatile__("mov %0, %%sp" : : "r" (orig_sp));
}

#endif /* _KSTACK_H */
