#ifndef __SPARC_PTRACE_H
#define __SPARC_PTRACE_H

#include <uapi/asm/ptrace.h>

#if defined(__sparc__) && defined(__arch64__)
#ifndef __ASSEMBLY__

#include <linux/threads.h>
#include <asm/switch_to.h>

static inline int pt_regs_trap_type(struct pt_regs *regs)
{
	return regs->magic & 0x1ff;
}

static inline bool pt_regs_is_syscall(struct pt_regs *regs)
{
	return (regs->tstate & TSTATE_SYSCALL);
}

static inline bool pt_regs_clear_syscall(struct pt_regs *regs)
{
	return (regs->tstate &= ~TSTATE_SYSCALL);
}

#define arch_ptrace_stop_needed(exit_code, info) \
({	flush_user_windows(); \
	get_thread_wsaved() != 0; \
})

#define arch_ptrace_stop(exit_code, info) \
	synchronize_user_stack()

#define current_pt_regs() \
	((struct pt_regs *)((unsigned long)current_thread_info() + THREAD_SIZE) - 1)

struct global_reg_snapshot {
	unsigned long		tstate;
	unsigned long		tpc;
	unsigned long		tnpc;
	unsigned long		o7;
	unsigned long		i7;
	unsigned long		rpc;
	struct thread_info	*thread;
	unsigned long		pad1;
};

struct global_pmu_snapshot {
	unsigned long		pcr[4];
	unsigned long		pic[4];
};

union global_cpu_snapshot {
	struct global_reg_snapshot	reg;
	struct global_pmu_snapshot	pmu;
};

extern union global_cpu_snapshot global_cpu_snapshot[NR_CPUS];

#define force_successful_syscall_return() set_thread_noerror(1)
#define user_mode(regs) (!((regs)->tstate & TSTATE_PRIV))
#define instruction_pointer(regs) ((regs)->tpc)
#define instruction_pointer_set(regs, val) do { \
		(regs)->tpc = (val); \
		(regs)->tnpc = (val)+4; \
	} while (0)
#define user_stack_pointer(regs) ((regs)->u_regs[UREG_FP])
static inline int is_syscall_success(struct pt_regs *regs)
{
	return !(regs->tstate & (TSTATE_XCARRY | TSTATE_ICARRY));
}

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->u_regs[UREG_I0];
}
#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif

#define MAX_REG_OFFSET (offsetof(struct pt_regs, magic))

extern int regs_query_register_offset(const char *name);

/**
 * regs_get_register() - get register value from its offset
 * @regs:	pt_regs from which register value is gotten
 * @offset:	offset number of the register.
 *
 * regs_get_register returns the value of a register whose
 * offset from @regs. The @offset is the offset of the register
 * in struct pt_regs. If @offset is bigger than MAX_REG_OFFSET,
 * this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
					     unsigned long offset)
{
	if (unlikely(offset >= MAX_REG_OFFSET))
		return 0;
	if (offset == PT_V9_Y)
		return *(unsigned int *)((unsigned long)regs + offset);
	return *(unsigned long *)((unsigned long)regs + offset);
}

/* Valid only for Kernel mode traps. */
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->u_regs[UREG_I6];
}
#else /* __ASSEMBLY__ */
#endif /* __ASSEMBLY__ */
#else /* (defined(__sparc__) && defined(__arch64__)) */
#ifndef __ASSEMBLY__
#include <asm/switch_to.h>

static inline bool pt_regs_is_syscall(struct pt_regs *regs)
{
	return (regs->psr & PSR_SYSCALL);
}

static inline bool pt_regs_clear_syscall(struct pt_regs *regs)
{
	return (regs->psr &= ~PSR_SYSCALL);
}

#define arch_ptrace_stop_needed(exit_code, info) \
({	flush_user_windows(); \
	current_thread_info()->w_saved != 0;	\
})

#define arch_ptrace_stop(exit_code, info) \
	synchronize_user_stack()

#define current_pt_regs() \
	((struct pt_regs *)((unsigned long)current_thread_info() + THREAD_SIZE) - 1)

#define user_mode(regs) (!((regs)->psr & PSR_PS))
#define instruction_pointer(regs) ((regs)->pc)
#define user_stack_pointer(regs) ((regs)->u_regs[UREG_FP])
unsigned long profile_pc(struct pt_regs *);
#else /* (!__ASSEMBLY__) */
#endif /* (!__ASSEMBLY__) */
#endif /* (defined(__sparc__) && defined(__arch64__)) */
#define STACK_BIAS		2047

/* global_reg_snapshot offsets */
#define GR_SNAP_TSTATE	0x00
#define GR_SNAP_TPC	0x08
#define GR_SNAP_TNPC	0x10
#define GR_SNAP_O7	0x18
#define GR_SNAP_I7	0x20
#define GR_SNAP_RPC	0x28
#define GR_SNAP_THREAD	0x30
#define GR_SNAP_PAD1	0x38

#endif /* !(__SPARC_PTRACE_H) */
