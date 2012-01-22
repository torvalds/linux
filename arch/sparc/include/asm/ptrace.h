#ifndef __SPARC_PTRACE_H
#define __SPARC_PTRACE_H

#if defined(__sparc__) && defined(__arch64__)
/* 64 bit sparc */
#include <asm/pstate.h>

/* This struct defines the way the registers are stored on the
 * stack during a system call and basically all traps.
 */

/* This magic value must have the low 9 bits clear,
 * as that is where we encode the %tt value, see below.
 */
#define PT_REGS_MAGIC 0x57ac6c00

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct pt_regs {
	unsigned long u_regs[16]; /* globals and ins */
	unsigned long tstate;
	unsigned long tpc;
	unsigned long tnpc;
	unsigned int y;

	/* We encode a magic number, PT_REGS_MAGIC, along
	 * with the %tt (trap type) register value at trap
	 * entry time.  The magic number allows us to identify
	 * accurately a trap stack frame in the stack
	 * unwinder, and the %tt value allows us to test
	 * things like "in a system call" etc. for an arbitray
	 * process.
	 *
	 * The PT_REGS_MAGIC is chosen such that it can be
	 * loaded completely using just a sethi instruction.
	 */
	unsigned int magic;
};

struct pt_regs32 {
	unsigned int psr;
	unsigned int pc;
	unsigned int npc;
	unsigned int y;
	unsigned int u_regs[16]; /* globals and ins */
};

/* A V9 register window */
struct reg_window {
	unsigned long locals[8];
	unsigned long ins[8];
};

/* A 32-bit register window. */
struct reg_window32 {
	unsigned int locals[8];
	unsigned int ins[8];
};

/* A V9 Sparc stack frame */
struct sparc_stackf {
	unsigned long locals[8];
        unsigned long ins[6];
	struct sparc_stackf *fp;
	unsigned long callers_pc;
	char *structptr;
	unsigned long xargs[6];
	unsigned long xxargs[1];
};

/* A 32-bit Sparc stack frame */
struct sparc_stackf32 {
	unsigned int locals[8];
        unsigned int ins[6];
	unsigned int fp;
	unsigned int callers_pc;
	unsigned int structptr;
	unsigned int xargs[6];
	unsigned int xxargs[1];
};

struct sparc_trapf {
	unsigned long locals[8];
	unsigned long ins[8];
	unsigned long _unused;
	struct pt_regs *regs;
};
#endif /* (!__ASSEMBLY__) */
#else
/* 32 bit sparc */

#include <asm/psr.h>

/* This struct defines the way the registers are stored on the
 * stack during a system call and basically all traps.
 */
#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long psr;
	unsigned long pc;
	unsigned long npc;
	unsigned long y;
	unsigned long u_regs[16]; /* globals and ins */
};

/* A 32-bit register window. */
struct reg_window32 {
	unsigned long locals[8];
	unsigned long ins[8];
};

/* A Sparc stack frame */
struct sparc_stackf {
	unsigned long locals[8];
        unsigned long ins[6];
	struct sparc_stackf *fp;
	unsigned long callers_pc;
	char *structptr;
	unsigned long xargs[6];
	unsigned long xxargs[1];
};
#endif /* (!__ASSEMBLY__) */

#endif /* (defined(__sparc__) && defined(__arch64__))*/

#ifndef __ASSEMBLY__

#define TRACEREG_SZ	sizeof(struct pt_regs)
#define STACKFRAME_SZ	sizeof(struct sparc_stackf)

#define TRACEREG32_SZ	sizeof(struct pt_regs32)
#define STACKFRAME32_SZ	sizeof(struct sparc_stackf32)

#endif /* (!__ASSEMBLY__) */

#define UREG_G0        0
#define UREG_G1        1
#define UREG_G2        2
#define UREG_G3        3
#define UREG_G4        4
#define UREG_G5        5
#define UREG_G6        6
#define UREG_G7        7
#define UREG_I0        8
#define UREG_I1        9
#define UREG_I2        10
#define UREG_I3        11
#define UREG_I4        12
#define UREG_I5        13
#define UREG_I6        14
#define UREG_I7        15
#define UREG_FP        UREG_I6
#define UREG_RETPC     UREG_I7

#if defined(__sparc__) && defined(__arch64__)
/* 64 bit sparc */

#ifndef __ASSEMBLY__

#ifdef __KERNEL__

#include <linux/threads.h>
#include <asm/system.h>

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
extern struct global_reg_snapshot global_reg_snapshot[NR_CPUS];

#define force_successful_syscall_return()	    \
do {	current_thread_info()->syscall_noerror = 1; \
} while (0)
#define user_mode(regs) (!((regs)->tstate & TSTATE_PRIV))
#define instruction_pointer(regs) ((regs)->tpc)
#define instruction_pointer_set(regs, val) ((regs)->tpc = (val))
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
extern unsigned long profile_pc(struct pt_regs *);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif
#endif /* (__KERNEL__) */

#else /* __ASSEMBLY__ */
/* For assembly code. */
#define TRACEREG_SZ		0xa0
#define STACKFRAME_SZ		0xc0

#define TRACEREG32_SZ		0x50
#define STACKFRAME32_SZ		0x60
#endif /* __ASSEMBLY__ */

#else /* (defined(__sparc__) && defined(__arch64__)) */

/* 32 bit sparc */

#ifndef __ASSEMBLY__

#ifdef __KERNEL__

#include <asm/system.h>

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

#define user_mode(regs) (!((regs)->psr & PSR_PS))
#define instruction_pointer(regs) ((regs)->pc)
#define user_stack_pointer(regs) ((regs)->u_regs[UREG_FP])
unsigned long profile_pc(struct pt_regs *);
#endif /* (__KERNEL__) */

#else /* (!__ASSEMBLY__) */
/* For assembly code. */
#define TRACEREG_SZ       0x50
#define STACKFRAME_SZ     0x60
#endif /* (!__ASSEMBLY__) */

#endif /* (defined(__sparc__) && defined(__arch64__)) */

#ifdef __KERNEL__
#define STACK_BIAS		2047
#endif

/* These are for pt_regs. */
#define PT_V9_G0     0x00
#define PT_V9_G1     0x08
#define PT_V9_G2     0x10
#define PT_V9_G3     0x18
#define PT_V9_G4     0x20
#define PT_V9_G5     0x28
#define PT_V9_G6     0x30
#define PT_V9_G7     0x38
#define PT_V9_I0     0x40
#define PT_V9_I1     0x48
#define PT_V9_I2     0x50
#define PT_V9_I3     0x58
#define PT_V9_I4     0x60
#define PT_V9_I5     0x68
#define PT_V9_I6     0x70
#define PT_V9_FP     PT_V9_I6
#define PT_V9_I7     0x78
#define PT_V9_TSTATE 0x80
#define PT_V9_TPC    0x88
#define PT_V9_TNPC   0x90
#define PT_V9_Y      0x98
#define PT_V9_MAGIC  0x9c
#define PT_TSTATE	PT_V9_TSTATE
#define PT_TPC		PT_V9_TPC
#define PT_TNPC		PT_V9_TNPC

/* These for pt_regs32. */
#define PT_PSR    0x0
#define PT_PC     0x4
#define PT_NPC    0x8
#define PT_Y      0xc
#define PT_G0     0x10
#define PT_WIM    PT_G0
#define PT_G1     0x14
#define PT_G2     0x18
#define PT_G3     0x1c
#define PT_G4     0x20
#define PT_G5     0x24
#define PT_G6     0x28
#define PT_G7     0x2c
#define PT_I0     0x30
#define PT_I1     0x34
#define PT_I2     0x38
#define PT_I3     0x3c
#define PT_I4     0x40
#define PT_I5     0x44
#define PT_I6     0x48
#define PT_FP     PT_I6
#define PT_I7     0x4c

/* Reg_window offsets */
#define RW_V9_L0     0x00
#define RW_V9_L1     0x08
#define RW_V9_L2     0x10
#define RW_V9_L3     0x18
#define RW_V9_L4     0x20
#define RW_V9_L5     0x28
#define RW_V9_L6     0x30
#define RW_V9_L7     0x38
#define RW_V9_I0     0x40
#define RW_V9_I1     0x48
#define RW_V9_I2     0x50
#define RW_V9_I3     0x58
#define RW_V9_I4     0x60
#define RW_V9_I5     0x68
#define RW_V9_I6     0x70
#define RW_V9_I7     0x78

#define RW_L0     0x00
#define RW_L1     0x04
#define RW_L2     0x08
#define RW_L3     0x0c
#define RW_L4     0x10
#define RW_L5     0x14
#define RW_L6     0x18
#define RW_L7     0x1c
#define RW_I0     0x20
#define RW_I1     0x24
#define RW_I2     0x28
#define RW_I3     0x2c
#define RW_I4     0x30
#define RW_I5     0x34
#define RW_I6     0x38
#define RW_I7     0x3c

/* Stack_frame offsets */
#define SF_V9_L0     0x00
#define SF_V9_L1     0x08
#define SF_V9_L2     0x10
#define SF_V9_L3     0x18
#define SF_V9_L4     0x20
#define SF_V9_L5     0x28
#define SF_V9_L6     0x30
#define SF_V9_L7     0x38
#define SF_V9_I0     0x40
#define SF_V9_I1     0x48
#define SF_V9_I2     0x50
#define SF_V9_I3     0x58
#define SF_V9_I4     0x60
#define SF_V9_I5     0x68
#define SF_V9_FP     0x70
#define SF_V9_PC     0x78
#define SF_V9_RETP   0x80
#define SF_V9_XARG0  0x88
#define SF_V9_XARG1  0x90
#define SF_V9_XARG2  0x98
#define SF_V9_XARG3  0xa0
#define SF_V9_XARG4  0xa8
#define SF_V9_XARG5  0xb0
#define SF_V9_XXARG  0xb8

#define SF_L0     0x00
#define SF_L1     0x04
#define SF_L2     0x08
#define SF_L3     0x0c
#define SF_L4     0x10
#define SF_L5     0x14
#define SF_L6     0x18
#define SF_L7     0x1c
#define SF_I0     0x20
#define SF_I1     0x24
#define SF_I2     0x28
#define SF_I3     0x2c
#define SF_I4     0x30
#define SF_I5     0x34
#define SF_FP     0x38
#define SF_PC     0x3c
#define SF_RETP   0x40
#define SF_XARG0  0x44
#define SF_XARG1  0x48
#define SF_XARG2  0x4c
#define SF_XARG3  0x50
#define SF_XARG4  0x54
#define SF_XARG5  0x58
#define SF_XXARG  0x5c

#ifdef __KERNEL__

/* global_reg_snapshot offsets */
#define GR_SNAP_TSTATE	0x00
#define GR_SNAP_TPC	0x08
#define GR_SNAP_TNPC	0x10
#define GR_SNAP_O7	0x18
#define GR_SNAP_I7	0x20
#define GR_SNAP_RPC	0x28
#define GR_SNAP_THREAD	0x30
#define GR_SNAP_PAD1	0x38

#endif  /*  __KERNEL__  */

/* Stuff for the ptrace system call */
#define PTRACE_SPARC_DETACH       11
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#define PTRACE_READDATA           16
#define PTRACE_WRITEDATA          17
#define PTRACE_READTEXT           18
#define PTRACE_WRITETEXT          19
#define PTRACE_GETFPAREGS         20
#define PTRACE_SETFPAREGS         21

/* There are for debugging 64-bit processes, either from a 32 or 64 bit
 * parent.  Thus their complements are for debugging 32-bit processes only.
 */

#define PTRACE_GETREGS64	  22
#define PTRACE_SETREGS64	  23
/* PTRACE_SYSCALL is 24 */
#define PTRACE_GETFPREGS64	  25
#define PTRACE_SETFPREGS64	  26

#endif /* !(__SPARC_PTRACE_H) */
