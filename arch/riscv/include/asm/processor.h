/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PROCESSOR_H
#define _ASM_RISCV_PROCESSOR_H

#include <linux/const.h>
#include <linux/cache.h>

#include <vdso/processor.h>

#include <asm/ptrace.h>

#ifdef CONFIG_64BIT
#define DEFAULT_MAP_WINDOW	(UL(1) << (MMAP_VA_BITS - 1))
#define STACK_TOP_MAX		TASK_SIZE_64

#define arch_get_mmap_end(addr, len, flags)			\
({								\
	unsigned long mmap_end;					\
	typeof(addr) _addr = (addr);				\
	if ((_addr) == 0 || (IS_ENABLED(CONFIG_COMPAT) && is_compat_task())) \
		mmap_end = STACK_TOP_MAX;			\
	else if ((_addr) >= VA_USER_SV57)			\
		mmap_end = STACK_TOP_MAX;			\
	else if ((((_addr) >= VA_USER_SV48)) && (VA_BITS >= VA_BITS_SV48)) \
		mmap_end = VA_USER_SV48;			\
	else							\
		mmap_end = VA_USER_SV39;			\
	mmap_end;						\
})

#define arch_get_mmap_base(addr, base)				\
({								\
	unsigned long mmap_base;				\
	typeof(addr) _addr = (addr);				\
	typeof(base) _base = (base);				\
	unsigned long rnd_gap = DEFAULT_MAP_WINDOW - (_base);	\
	if ((_addr) == 0 || (IS_ENABLED(CONFIG_COMPAT) && is_compat_task())) \
		mmap_base = (_base);				\
	else if (((_addr) >= VA_USER_SV57) && (VA_BITS >= VA_BITS_SV57)) \
		mmap_base = VA_USER_SV57 - rnd_gap;		\
	else if ((((_addr) >= VA_USER_SV48)) && (VA_BITS >= VA_BITS_SV48)) \
		mmap_base = VA_USER_SV48 - rnd_gap;		\
	else							\
		mmap_base = VA_USER_SV39 - rnd_gap;		\
	mmap_base;						\
})

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

/* CPU-specific state of a task */
struct thread_struct {
	/* Callee-saved registers */
	unsigned long ra;
	unsigned long sp;	/* Kernel mode stack */
	unsigned long s[12];	/* s[0]: frame pointer */
	struct __riscv_d_ext_state fstate;
	unsigned long bad_cause;
	unsigned long vstate_ctrl;
	struct __riscv_v_ext_state vstate;
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

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_PROCESSOR_H */
