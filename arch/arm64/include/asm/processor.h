/*
 * Based on arch/arm/include/asm/processor.h
 *
 * Copyright (C) 1995-1999 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PROCESSOR_H
#define __ASM_PROCESSOR_H

#define TASK_SIZE_64		(UL(1) << VA_BITS)

#define KERNEL_DS	UL(-1)
#define USER_DS		(TASK_SIZE_64 - 1)

#ifndef __ASSEMBLY__

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#ifdef __KERNEL__

#include <linux/build_bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/string.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/hw_breakpoint.h>
#include <asm/lse.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptrace.h>
#include <asm/types.h>

/*
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 */
#ifdef CONFIG_COMPAT
#define TASK_SIZE_32		UL(0x100000000)
#define TASK_SIZE		(test_thread_flag(TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#define TASK_SIZE_OF(tsk)	(test_tsk_thread_flag(tsk, TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#else
#define TASK_SIZE		TASK_SIZE_64
#endif /* CONFIG_COMPAT */

#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 4))

#define STACK_TOP_MAX		TASK_SIZE_64
#ifdef CONFIG_COMPAT
#define AARCH32_VECTORS_BASE	0xffff0000
#define STACK_TOP		(test_thread_flag(TIF_32BIT) ? \
				AARCH32_VECTORS_BASE : STACK_TOP_MAX)
#else
#define STACK_TOP		STACK_TOP_MAX
#endif /* CONFIG_COMPAT */

extern phys_addr_t arm64_dma_phys_limit;
#define ARCH_LOW_ADDRESS_LIMIT	(arm64_dma_phys_limit - 1)

struct debug_info {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	/* Have we suspended stepping by a debugger? */
	int			suspended_step;
	/* Allow breakpoints and watchpoints to be disabled for this thread. */
	int			bps_disabled;
	int			wps_disabled;
	/* Hardware breakpoints pinned to this task. */
	struct perf_event	*hbp_break[ARM_MAX_BRP];
	struct perf_event	*hbp_watch[ARM_MAX_WRP];
#endif
};

struct cpu_context {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};

struct thread_struct {
	struct cpu_context	cpu_context;	/* cpu context */

	/*
	 * Whitelisted fields for hardened usercopy:
	 * Maintainers must ensure manually that this contains no
	 * implicit padding.
	 */
	struct {
		unsigned long	tp_value;	/* TLS register */
		unsigned long	tp2_value;
		struct user_fpsimd_state fpsimd_state;
	} uw;

	unsigned int		fpsimd_cpu;
	void			*sve_state;	/* SVE registers, if any */
	unsigned int		sve_vl;		/* SVE vector length */
	unsigned int		sve_vl_onexec;	/* SVE vl after next exec */
	unsigned long		fault_address;	/* fault info */
	unsigned long		fault_code;	/* ESR_EL1 value */
	struct debug_info	debug;		/* debugging */
};

static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	/* Verify that there is no padding among the whitelisted fields: */
	BUILD_BUG_ON(sizeof_field(struct thread_struct, uw) !=
		     sizeof_field(struct thread_struct, uw.tp_value) +
		     sizeof_field(struct thread_struct, uw.tp2_value) +
		     sizeof_field(struct thread_struct, uw.fpsimd_state));

	*offset = offsetof(struct thread_struct, uw);
	*size = sizeof_field(struct thread_struct, uw);
}

#ifdef CONFIG_COMPAT
#define task_user_tls(t)						\
({									\
	unsigned long *__tls;						\
	if (is_compat_thread(task_thread_info(t)))			\
		__tls = &(t)->thread.uw.tp2_value;			\
	else								\
		__tls = &(t)->thread.uw.tp_value;			\
	__tls;								\
 })
#else
#define task_user_tls(t)	(&(t)->thread.uw.tp_value)
#endif

/* Sync TPIDR_EL0 back to thread_struct for current */
void tls_preserve_current_state(void);

#define INIT_THREAD {				\
	.fpsimd_cpu = NR_CPUS,			\
}

static inline void start_thread_common(struct pt_regs *regs, unsigned long pc)
{
	memset(regs, 0, sizeof(*regs));
	forget_syscall(regs);
	regs->pc = pc;
}

static inline void start_thread(struct pt_regs *regs, unsigned long pc,
				unsigned long sp)
{
	start_thread_common(regs, pc);
	regs->pstate = PSR_MODE_EL0t;

	if (arm64_get_ssbd_state() != ARM64_SSBD_FORCE_ENABLE)
		regs->pstate |= PSR_SSBS_BIT;

	regs->sp = sp;
}

#ifdef CONFIG_COMPAT
static inline void compat_start_thread(struct pt_regs *regs, unsigned long pc,
				       unsigned long sp)
{
	start_thread_common(regs, pc);
	regs->pstate = PSR_AA32_MODE_USR;
	if (pc & 1)
		regs->pstate |= PSR_AA32_T_BIT;

#ifdef __AARCH64EB__
	regs->pstate |= PSR_AA32_E_BIT;
#endif

	if (arm64_get_ssbd_state() != ARM64_SSBD_FORCE_ENABLE)
		regs->pstate |= PSR_AA32_SSBS_BIT;

	regs->compat_sp = sp;
}
#endif

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

unsigned long get_wchan(struct task_struct *p);

static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}

/* Thread switching */
extern struct task_struct *cpu_switch_to(struct task_struct *prev,
					 struct task_struct *next);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

#define KSTK_EIP(tsk)	((unsigned long)task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)	user_stack_pointer(task_pt_regs(tsk))

/*
 * Prefetching support
 */
#define ARCH_HAS_PREFETCH
static inline void prefetch(const void *ptr)
{
	asm volatile("prfm pldl1keep, %a0\n" : : "p" (ptr));
}

#define ARCH_HAS_PREFETCHW
static inline void prefetchw(const void *ptr)
{
	asm volatile("prfm pstl1keep, %a0\n" : : "p" (ptr));
}

#define ARCH_HAS_SPINLOCK_PREFETCH
static inline void spin_lock_prefetch(const void *ptr)
{
	asm volatile(ARM64_LSE_ATOMIC_INSN(
		     "prfm pstl1strm, %a0",
		     "nop") : : "p" (ptr));
}

#define HAVE_ARCH_PICK_MMAP_LAYOUT

#endif

extern unsigned long __ro_after_init signal_minsigstksz; /* sigframe size */
extern void __init minsigstksz_setup(void);

/*
 * Not at the top of the file due to a direct #include cycle between
 * <asm/fpsimd.h> and <asm/processor.h>.  Deferring this #include
 * ensures that contents of processor.h are visible to fpsimd.h even if
 * processor.h is included first.
 *
 * These prctl helpers are the only things in this file that require
 * fpsimd.h.  The core code expects them to be in this header.
 */
#include <asm/fpsimd.h>

/* Userspace interface for PR_SVE_{SET,GET}_VL prctl()s: */
#define SVE_SET_VL(arg)	sve_set_current_vl(arg)
#define SVE_GET_VL()	sve_get_current_vl()

/*
 * For CONFIG_GCC_PLUGIN_STACKLEAK
 *
 * These need to be macros because otherwise we get stuck in a nightmare
 * of header definitions for the use of task_stack_page.
 */

#define current_top_of_stack()							\
({										\
	struct stack_info _info;						\
	BUG_ON(!on_accessible_stack(current, current_stack_pointer, &_info));	\
	_info.high;								\
})
#define on_thread_stack()	(on_task_stack(current, current_stack_pointer, NULL))

#endif /* __ASSEMBLY__ */
#endif /* __ASM_PROCESSOR_H */
