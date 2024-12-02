/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VM86_H
#define _ASM_X86_VM86_H

#include <asm/ptrace.h>
#include <uapi/asm/vm86.h>

/*
 * This is the (kernel) stack-layout when we have done a "SAVE_ALL" from vm86
 * mode - the main change is that the old segment descriptors aren't
 * useful any more and are forced to be zero by the kernel (and the
 * hardware when a trap occurs), and the real segment descriptors are
 * at the end of the structure. Look at ptrace.h to see the "normal"
 * setup. For user space layout see 'struct vm86_regs' above.
 */

struct kernel_vm86_regs {
/*
 * normal regs, with special meaning for the segment descriptors..
 */
	struct pt_regs pt;
/*
 * these are specific to v86 mode:
 */
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct vm86 {
	struct vm86plus_struct __user *user_vm86;
	struct pt_regs regs32;
	unsigned long veflags;
	unsigned long veflags_mask;
	unsigned long saved_sp0;

	unsigned long flags;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
};

#ifdef CONFIG_VM86

void handle_vm86_fault(struct kernel_vm86_regs *, long);
int handle_vm86_trap(struct kernel_vm86_regs *, long, int);
void save_v86_state(struct kernel_vm86_regs *, int);

struct task_struct;

#define free_vm86(t) do {				\
	struct thread_struct *__t = (t);		\
	if (__t->vm86 != NULL) {			\
		kfree(__t->vm86);			\
		__t->vm86 = NULL;			\
	}						\
} while (0)

/*
 * Support for VM86 programs to request interrupts for
 * real mode hardware drivers:
 */
#define FIRST_VM86_IRQ		 3
#define LAST_VM86_IRQ		15

static inline int invalid_vm86_irq(int irq)
{
	return irq < FIRST_VM86_IRQ || irq > LAST_VM86_IRQ;
}

void release_vm86_irqs(struct task_struct *);

#else

#define handle_vm86_fault(a, b)
#define release_vm86_irqs(a)

static inline int handle_vm86_trap(struct kernel_vm86_regs *a, long b, int c)
{
	return 0;
}

static inline void save_v86_state(struct kernel_vm86_regs *a, int b) { }

#define free_vm86(t) do { } while(0)

#endif /* CONFIG_VM86 */

#endif /* _ASM_X86_VM86_H */
