#ifndef __ASM_SH_PROCESSOR_64_H
#define __ASM_SH_PROCESSOR_64_H

/*
 * include/asm-sh/processor_64.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/types.h>
#include <cpu/registers.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ \
void *pc; \
unsigned long long __dummy = 0; \
__asm__("gettr	tr0, %1\n\t" \
	"pta	4, tr0\n\t" \
	"gettr	tr0, %0\n\t" \
	"ptabs	%1, tr0\n\t"	\
	:"=r" (pc), "=r" (__dummy) \
	: "1" (__dummy)); \
pc; })

#endif

/*
 * User space process size: 2GB - 4k.
 */
#define TASK_SIZE	0x7ffff000UL

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE / 3)

/*
 * Bit of SR register
 *
 * FD-bit:
 *     When it's set, it means the processor doesn't have right to use FPU,
 *     and it results exception when the floating operation is executed.
 *
 * IMASK-bit:
 *     Interrupt level mask
 *
 * STEP-bit:
 *     Single step bit
 *
 */
#if defined(CONFIG_SH64_SR_WATCH)
#define SR_MMU   0x84000000
#else
#define SR_MMU   0x80000000
#endif

#define SR_IMASK 0x000000f0
#define SR_FD    0x00008000
#define SR_SSTEP 0x08000000

#ifndef __ASSEMBLY__

/*
 * FPU structure and data : require 8-byte alignment as we need to access it
   with fld.p, fst.p
 */

struct sh_fpu_hard_struct {
	unsigned long fp_regs[64];
	unsigned int fpscr;
	/* long status; * software status information */
};

/* Dummy fpu emulator  */
struct sh_fpu_soft_struct {
	unsigned long fp_regs[64];
	unsigned int fpscr;
	unsigned char lookahead;
	unsigned long entry_pc;
};

union thread_xstate {
	struct sh_fpu_hard_struct hardfpu;
	struct sh_fpu_soft_struct softfpu;
	/*
	 * The structure definitions only produce 32 bit alignment, yet we need
	 * to access them using 64 bit load/store as well.
	 */
	unsigned long long alignment_dummy;
};

struct thread_struct {
	unsigned long sp;
	unsigned long pc;

	/* Various thread flags, see SH_THREAD_xxx */
	unsigned long flags;

	/* This stores the address of the pt_regs built during a context
	   switch, or of the register save area built for a kernel mode
	   exception.  It is used for backtracing the stack of a sleeping task
	   or one that traps in kernel mode. */
        struct pt_regs *kregs;
	/* This stores the address of the pt_regs constructed on entry from
	   user mode.  It is a fixed value over the lifetime of a process, or
	   NULL for a kernel thread. */
	struct pt_regs *uregs;

	unsigned long address;
	/* Hardware debugging registers may come here */

	/* floating point info */
	union thread_xstate *xstate;
};

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_THREAD  {				\
	.sp		= sizeof(init_stack) +	\
			  (long) &init_stack,	\
	.pc		= 0,			\
        .kregs		= &fake_swapper_regs,	\
	.uregs	        = NULL,			\
	.address	= 0,			\
	.flags		= 0,			\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define SR_USER (SR_MMU | SR_FD)

#define start_thread(_regs, new_pc, new_sp)			\
	_regs->sr = SR_USER;	/* User mode. */		\
	_regs->pc = new_pc - 4;	/* Compensate syscall exit */	\
	_regs->pc |= 1;		/* Set SHmedia ! */		\
	_regs->regs[18] = 0;					\
	_regs->regs[15] = new_sp

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while (0)
#define release_segments(mm)	do { } while (0)
#define forget_segments()	do { } while (0)
/*
 * FPU lazy state save handling.
 */

static inline void disable_fpu(void)
{
	unsigned long long __dummy;

	/* Set FD flag in SR */
	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy)
			     : "r" (SR_FD));
}

static inline void enable_fpu(void)
{
	unsigned long long __dummy;

	/* Clear out FD flag in SR */
	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy)
			     : "r" (~SR_FD));
}

/* Round to nearest, no exceptions on inexact, overflow, underflow,
   zero-divide, invalid.  Configure option for whether to flush denorms to
   zero, or except if a denorm is encountered.  */
#if defined(CONFIG_SH64_FPU_DENORM_FLUSH)
#define FPSCR_INIT  0x00040000
#else
#define FPSCR_INIT  0x00000000
#endif

#ifdef CONFIG_SH_FPU
/* Initialise the FP state of a task */
void fpinit(struct sh_fpu_hard_struct *fpregs);
#else
#define fpinit(fpregs)	do { } while (0)
#endif

extern struct task_struct *last_task_used_math;

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.pc)
#define KSTK_ESP(tsk)  ((tsk)->thread.sp)

#endif	/* __ASSEMBLY__ */
#endif /* __ASM_SH_PROCESSOR_64_H */
