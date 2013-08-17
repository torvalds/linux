/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_SWITCH_TO_H
#define __ASM_SWITCH_TO_H

#include <linux/thread_info.h>

extern struct task_struct *__switch_to(void *, void *);
extern void update_per_regs(struct task_struct *task);

static inline void save_fp_regs(s390_fp_regs *fpregs)
{
	asm volatile(
		"	std	0,%O0+8(%R0)\n"
		"	std	2,%O0+24(%R0)\n"
		"	std	4,%O0+40(%R0)\n"
		"	std	6,%O0+56(%R0)"
		: "=Q" (*fpregs) : "Q" (*fpregs));
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile(
		"	stfpc	%0\n"
		"	std	1,%O0+16(%R0)\n"
		"	std	3,%O0+32(%R0)\n"
		"	std	5,%O0+48(%R0)\n"
		"	std	7,%O0+64(%R0)\n"
		"	std	8,%O0+72(%R0)\n"
		"	std	9,%O0+80(%R0)\n"
		"	std	10,%O0+88(%R0)\n"
		"	std	11,%O0+96(%R0)\n"
		"	std	12,%O0+104(%R0)\n"
		"	std	13,%O0+112(%R0)\n"
		"	std	14,%O0+120(%R0)\n"
		"	std	15,%O0+128(%R0)\n"
		: "=Q" (*fpregs) : "Q" (*fpregs));
}

static inline void restore_fp_regs(s390_fp_regs *fpregs)
{
	asm volatile(
		"	ld	0,%O0+8(%R0)\n"
		"	ld	2,%O0+24(%R0)\n"
		"	ld	4,%O0+40(%R0)\n"
		"	ld	6,%O0+56(%R0)"
		: : "Q" (*fpregs));
	if (!MACHINE_HAS_IEEE)
		return;
	asm volatile(
		"	lfpc	%0\n"
		"	ld	1,%O0+16(%R0)\n"
		"	ld	3,%O0+32(%R0)\n"
		"	ld	5,%O0+48(%R0)\n"
		"	ld	7,%O0+64(%R0)\n"
		"	ld	8,%O0+72(%R0)\n"
		"	ld	9,%O0+80(%R0)\n"
		"	ld	10,%O0+88(%R0)\n"
		"	ld	11,%O0+96(%R0)\n"
		"	ld	12,%O0+104(%R0)\n"
		"	ld	13,%O0+112(%R0)\n"
		"	ld	14,%O0+120(%R0)\n"
		"	ld	15,%O0+128(%R0)\n"
		: : "Q" (*fpregs));
}

static inline void save_access_regs(unsigned int *acrs)
{
	asm volatile("stam 0,15,%0" : "=Q" (*acrs));
}

static inline void restore_access_regs(unsigned int *acrs)
{
	asm volatile("lam 0,15,%0" : : "Q" (*acrs));
}

#define switch_to(prev,next,last) do {					\
	if (prev->mm) {							\
		save_fp_regs(&prev->thread.fp_regs);			\
		save_access_regs(&prev->thread.acrs[0]);		\
	}								\
	if (next->mm) {							\
		restore_fp_regs(&next->thread.fp_regs);			\
		restore_access_regs(&next->thread.acrs[0]);		\
		update_per_regs(next);					\
	}								\
	prev = __switch_to(prev,next);					\
} while (0)

extern void account_vtime(struct task_struct *, struct task_struct *);
extern void account_tick_vtime(struct task_struct *);

#define finish_arch_switch(prev) do {					     \
	set_fs(current->thread.mm_segment);				     \
	account_vtime(prev, current);					     \
} while (0)

#endif /* __ASM_SWITCH_TO_H */
