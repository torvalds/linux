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

struct kernel_vm86_struct {
	struct kernel_vm86_regs regs;
/*
 * the below part remains on the kernel stack while we are in VM86 mode.
 * 'tss.esp0' then contains the address of VM86_TSS_ESP0 below, and when we
 * get forced back from VM86, the CPU and "SAVE_ALL" will restore the above
 * 'struct kernel_vm86_regs' with the then actual values.
 * Therefore, pt_regs in fact points to a complete 'kernel_vm86_struct'
 * in kernelspace, hence we need not reget the data from userspace.
 */
#define VM86_TSS_ESP0 flags
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
	struct pt_regs *regs32;   /* here we save the pointer to the old regs */
/*
 * The below is not part of the structure, but the stack layout continues
 * this way. In front of 'return-eip' may be some data, depending on
 * compilation, so we don't rely on this and save the pointer to 'oldregs'
 * in 'regs32' above.
 * However, with GCC-2.7.2 and the current CFLAGS you see exactly this:

	long return-eip;        from call to vm86()
	struct pt_regs oldregs;  user space registers as saved by syscall
 */
};

#ifdef CONFIG_VM86

void handle_vm86_fault(struct kernel_vm86_regs *, long);
int handle_vm86_trap(struct kernel_vm86_regs *, long, int);
struct pt_regs *save_v86_state(struct kernel_vm86_regs *);

struct task_struct;
void release_vm86_irqs(struct task_struct *);

#else

#define handle_vm86_fault(a, b)
#define release_vm86_irqs(a)

static inline int handle_vm86_trap(struct kernel_vm86_regs *a, long b, int c)
{
	return 0;
}

#endif /* CONFIG_VM86 */

#endif /* _ASM_X86_VM86_H */
