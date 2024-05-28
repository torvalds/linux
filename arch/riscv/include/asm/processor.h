/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PROCESSOR_H
#define _ASM_RISCV_PROCESSOR_H

#include <linux/const.h>
#include <linux/cache.h>
#include <linux/prctl.h>

#include <vdso/processor.h>

#include <asm/ptrace.h>

/*
 * addr is a hint to the maximum userspace address that mmap should provide, so
 * this macro needs to return the largest address space available so that
 * mmap_end < addr, being mmap_end the top of that address space.
 * See Documentation/arch/riscv/vm-layout.rst for more details.
 */
#define arch_get_mmap_end(addr, len, flags)			\
({								\
	unsigned long mmap_end;					\
	typeof(addr) _addr = (addr);				\
	if ((_addr) == 0 || is_compat_task() ||			\
	    ((_addr + len) > BIT(VA_BITS - 1)))			\
		mmap_end = STACK_TOP_MAX;			\
	else							\
		mmap_end = (_addr + len);			\
	mmap_end;						\
})

#define arch_get_mmap_base(addr, base)				\
({								\
	unsigned long mmap_base;				\
	typeof(addr) _addr = (addr);				\
	typeof(base) _base = (base);				\
	unsigned long rnd_gap = DEFAULT_MAP_WINDOW - (_base);	\
	if ((_addr) == 0 || is_compat_task() || 		\
	    ((_addr + len) > BIT(VA_BITS - 1)))			\
		mmap_base = (_base);				\
	else							\
		mmap_base = (_addr + len) - rnd_gap;		\
	mmap_base;						\
})

#ifdef CONFIG_64BIT
#define DEFAULT_MAP_WINDOW	(UL(1) << (MMAP_VA_BITS - 1))
#define STACK_TOP_MAX		TASK_SIZE_64
#else
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#define STACK_TOP_MAX		TASK_SIZE
#endif
#define STACK_ALIGN		16

#define STACK_TOP		DEFAULT_MAP_WINDOW

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#ifdef CONFIG_64BIT
#define TASK_UNMAPPED_BASE	PAGE_ALIGN((UL(1) << MMAP_MIN_VA_BITS) / 3)
#else
#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE / 3)
#endif

#ifndef __ASSEMBLY__

struct task_struct;
struct pt_regs;

/*
 * We use a flag to track in-kernel Vector context. Currently the flag has the
 * following meaning:
 *
 *  - bit 0: indicates whether the in-kernel Vector context is active. The
 *    activation of this state disables the preemption. On a non-RT kernel, it
 *    also disable bh.
 *  - bits 8: is used for tracking preemptible kernel-mode Vector, when
 *    RISCV_ISA_V_PREEMPTIVE is enabled. Calling kernel_vector_begin() does not
 *    disable the preemption if the thread's kernel_vstate.datap is allocated.
 *    Instead, the kernel set this bit field. Then the trap entry/exit code
 *    knows if we are entering/exiting the context that owns preempt_v.
 *     - 0: the task is not using preempt_v
 *     - 1: the task is actively using preempt_v. But whether does the task own
 *          the preempt_v context is decided by bits in RISCV_V_CTX_DEPTH_MASK.
 *  - bit 16-23 are RISCV_V_CTX_DEPTH_MASK, used by context tracking routine
 *     when preempt_v starts:
 *     - 0: the task is actively using, and own preempt_v context.
 *     - non-zero: the task was using preempt_v, but then took a trap within.
 *       Thus, the task does not own preempt_v. Any use of Vector will have to
 *       save preempt_v, if dirty, and fallback to non-preemptible kernel-mode
 *       Vector.
 *  - bit 30: The in-kernel preempt_v context is saved, and requries to be
 *    restored when returning to the context that owns the preempt_v.
 *  - bit 31: The in-kernel preempt_v context is dirty, as signaled by the
 *    trap entry code. Any context switches out-of current task need to save
 *    it to the task's in-kernel V context. Also, any traps nesting on-top-of
 *    preempt_v requesting to use V needs a save.
 */
#define RISCV_V_CTX_DEPTH_MASK		0x00ff0000

#define RISCV_V_CTX_UNIT_DEPTH		0x00010000
#define RISCV_KERNEL_MODE_V		0x00000001
#define RISCV_PREEMPT_V			0x00000100
#define RISCV_PREEMPT_V_DIRTY		0x80000000
#define RISCV_PREEMPT_V_NEED_RESTORE	0x40000000

/* CPU-specific state of a task */
struct thread_struct {
	/* Callee-saved registers */
	unsigned long ra;
	unsigned long sp;	/* Kernel mode stack */
	unsigned long s[12];	/* s[0]: frame pointer */
	struct __riscv_d_ext_state fstate;
	unsigned long bad_cause;
	u32 riscv_v_flags;
	u32 vstate_ctrl;
	struct __riscv_v_ext_state vstate;
	unsigned long align_ctl;
	struct __riscv_v_ext_state kernel_vstate;
};

/* Whitelist the fstate from the task_struct for hardened usercopy */
static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	*offset = offsetof(struct thread_struct, fstate);
	*size = sizeof_field(struct thread_struct, fstate);
}

#define INIT_THREAD {					\
	.sp = sizeof(init_stack) + (long)&init_stack,	\
	.align_ctl = PR_UNALIGN_NOPRINT,		\
}

#define task_pt_regs(tsk)						\
	((struct pt_regs *)(task_stack_page(tsk) + THREAD_SIZE		\
			    - ALIGN(sizeof(struct pt_regs), STACK_ALIGN)))

#define KSTK_EIP(tsk)		(task_pt_regs(tsk)->epc)
#define KSTK_ESP(tsk)		(task_pt_regs(tsk)->sp)


/* Do necessary setup to start up a newly executed thread. */
extern void start_thread(struct pt_regs *regs,
			unsigned long pc, unsigned long sp);

extern unsigned long __get_wchan(struct task_struct *p);


static inline void wait_for_interrupt(void)
{
	__asm__ __volatile__ ("wfi");
}

extern phys_addr_t dma32_phys_limit;

struct device_node;
int riscv_of_processor_hartid(struct device_node *node, unsigned long *hartid);
int riscv_early_of_processor_hartid(struct device_node *node, unsigned long *hartid);
int riscv_of_parent_hartid(struct device_node *node, unsigned long *hartid);

extern void riscv_fill_hwcap(void);
extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);

extern unsigned long signal_minsigstksz __ro_after_init;

#ifdef CONFIG_RISCV_ISA_V
/* Userspace interface for PR_RISCV_V_{SET,GET}_VS prctl()s: */
#define RISCV_V_SET_CONTROL(arg)	riscv_v_vstate_ctrl_set_current(arg)
#define RISCV_V_GET_CONTROL()		riscv_v_vstate_ctrl_get_current()
extern long riscv_v_vstate_ctrl_set_current(unsigned long arg);
extern long riscv_v_vstate_ctrl_get_current(void);
#endif /* CONFIG_RISCV_ISA_V */

extern int get_unalign_ctl(struct task_struct *tsk, unsigned long addr);
extern int set_unalign_ctl(struct task_struct *tsk, unsigned int val);

#define GET_UNALIGN_CTL(tsk, addr)	get_unalign_ctl((tsk), (addr))
#define SET_UNALIGN_CTL(tsk, val)	set_unalign_ctl((tsk), (val))

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_PROCESSOR_H */
