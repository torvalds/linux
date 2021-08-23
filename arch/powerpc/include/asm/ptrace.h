/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 *
 * this should only contain volatile regs
 * since we can keep non-volatile in the thread_struct
 * should set this up when only volatiles are saved
 * by intr code.
 *
 * Since this is going on the stack, *CARE MUST BE TAKEN* to insure
 * that the overall structure is a multiple of 16 bytes in length.
 *
 * Note that the offsets of the fields in this struct correspond with
 * the PT_* values below.  This simplifies arch/powerpc/kernel/ptrace.c.
 */
#ifndef _ASM_POWERPC_PTRACE_H
#define _ASM_POWERPC_PTRACE_H

#include <linux/err.h>
#include <uapi/asm/ptrace.h>
#include <asm/asm-const.h>
#include <asm/reg.h>

#ifndef __ASSEMBLY__
struct pt_regs
{
	union {
		struct user_pt_regs user_regs;
		struct {
			unsigned long gpr[32];
			unsigned long nip;
			unsigned long msr;
			unsigned long orig_gpr3;
			unsigned long ctr;
			unsigned long link;
			unsigned long xer;
			unsigned long ccr;
#ifdef CONFIG_PPC64
			unsigned long softe;
#else
			unsigned long mq;
#endif
			unsigned long trap;
			union {
				unsigned long dar;
				unsigned long dear;
			};
			union {
				unsigned long dsisr;
				unsigned long esr;
			};
			unsigned long result;
		};
	};
#if defined(CONFIG_PPC64) || defined(CONFIG_PPC_KUAP)
	union {
		struct {
#ifdef CONFIG_PPC64
			unsigned long ppr;
			unsigned long exit_result;
#endif
			union {
#ifdef CONFIG_PPC_KUAP
				unsigned long kuap;
#endif
#ifdef CONFIG_PPC_PKEY
				unsigned long amr;
#endif
			};
#ifdef CONFIG_PPC_PKEY
			unsigned long iamr;
#endif
		};
		unsigned long __pad[4];	/* Maintain 16 byte interrupt stack alignment */
	};
#endif
};
#endif


#define STACK_FRAME_WITH_PT_REGS (STACK_FRAME_OVERHEAD + sizeof(struct pt_regs))

#ifdef __powerpc64__

/*
 * Size of redzone that userspace is allowed to use below the stack
 * pointer.  This is 288 in the 64-bit big-endian ELF ABI, and 512 in
 * the new ELFv2 little-endian ABI, so we allow the larger amount.
 *
 * For kernel code we allow a 288-byte redzone, in order to conserve
 * kernel stack space; gcc currently only uses 288 bytes, and will
 * hopefully allow explicit control of the redzone size in future.
 */
#define USER_REDZONE_SIZE	512
#define KERNEL_REDZONE_SIZE	288

#define STACK_FRAME_OVERHEAD	112	/* size of minimum stack frame */
#define STACK_FRAME_LR_SAVE	2	/* Location of LR in stack frame */
#define STACK_FRAME_REGS_MARKER	ASM_CONST(0x7265677368657265)
#define STACK_INT_FRAME_SIZE	(sizeof(struct pt_regs) + \
				 STACK_FRAME_OVERHEAD + KERNEL_REDZONE_SIZE)
#define STACK_FRAME_MARKER	12

#ifdef PPC64_ELF_ABI_v2
#define STACK_FRAME_MIN_SIZE	32
#else
#define STACK_FRAME_MIN_SIZE	STACK_FRAME_OVERHEAD
#endif

/* Size of dummy stack frame allocated when calling signal handler. */
#define __SIGNAL_FRAMESIZE	128
#define __SIGNAL_FRAMESIZE32	64

#else /* __powerpc64__ */

#define USER_REDZONE_SIZE	0
#define KERNEL_REDZONE_SIZE	0
#define STACK_FRAME_OVERHEAD	16	/* size of minimum stack frame */
#define STACK_FRAME_LR_SAVE	1	/* Location of LR in stack frame */
#define STACK_FRAME_REGS_MARKER	ASM_CONST(0x72656773)
#define STACK_INT_FRAME_SIZE	(sizeof(struct pt_regs) + STACK_FRAME_OVERHEAD)
#define STACK_FRAME_MARKER	2
#define STACK_FRAME_MIN_SIZE	STACK_FRAME_OVERHEAD

/* Size of stack frame allocated when calling signal handler. */
#define __SIGNAL_FRAMESIZE	64

#endif /* __powerpc64__ */

#ifndef __ASSEMBLY__
#include <asm/paca.h>

#ifdef CONFIG_SMP
extern unsigned long profile_pc(struct pt_regs *regs);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif

long do_syscall_trace_enter(struct pt_regs *regs);
void do_syscall_trace_leave(struct pt_regs *regs);

static inline void set_return_regs_changed(void)
{
#ifdef CONFIG_PPC_BOOK3S_64
	local_paca->hsrr_valid = 0;
	local_paca->srr_valid = 0;
#endif
}

static inline void regs_set_return_ip(struct pt_regs *regs, unsigned long ip)
{
	regs->nip = ip;
	set_return_regs_changed();
}

static inline void regs_set_return_msr(struct pt_regs *regs, unsigned long msr)
{
	regs->msr = msr;
	set_return_regs_changed();
}

static inline void regs_add_return_ip(struct pt_regs *regs, long offset)
{
	regs_set_return_ip(regs, regs->nip + offset);
}

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->nip;
}

static inline void instruction_pointer_set(struct pt_regs *regs,
		unsigned long val)
{
	regs_set_return_ip(regs, val);
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->gpr[1];
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return 0;
}

#define user_mode(regs) (((regs)->msr & MSR_PR) != 0)

#define force_successful_syscall_return()   \
	do { \
		set_thread_flag(TIF_NOERROR); \
	} while(0)

#define current_pt_regs() \
	((struct pt_regs *)((unsigned long)task_stack_page(current) + THREAD_SIZE) - 1)

/*
 * The 4 low bits (0xf) are available as flags to overload the trap word,
 * because interrupt vectors have minimum alignment of 0x10. TRAP_FLAGS_MASK
 * must cover the bits used as flags, including bit 0 which is used as the
 * "norestart" bit.
 */
#ifdef __powerpc64__
#define TRAP_FLAGS_MASK		0x1
#else
/*
 * On 4xx we use bit 1 in the trap word to indicate whether the exception
 * is a critical exception (1 means it is).
 */
#define TRAP_FLAGS_MASK		0xf
#define IS_CRITICAL_EXC(regs)	(((regs)->trap & 2) != 0)
#define IS_MCHECK_EXC(regs)	(((regs)->trap & 4) != 0)
#define IS_DEBUG_EXC(regs)	(((regs)->trap & 8) != 0)
#endif /* __powerpc64__ */
#define TRAP(regs)		((regs)->trap & ~TRAP_FLAGS_MASK)

static __always_inline void set_trap(struct pt_regs *regs, unsigned long val)
{
	regs->trap = (regs->trap & TRAP_FLAGS_MASK) | (val & ~TRAP_FLAGS_MASK);
}

static inline bool trap_is_scv(struct pt_regs *regs)
{
	return (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && TRAP(regs) == 0x3000);
}

static inline bool trap_is_unsupported_scv(struct pt_regs *regs)
{
	return IS_ENABLED(CONFIG_PPC_BOOK3S_64) && TRAP(regs) == 0x7ff0;
}

static inline bool trap_is_syscall(struct pt_regs *regs)
{
	return (trap_is_scv(regs) || TRAP(regs) == 0xc00);
}

static inline bool trap_norestart(struct pt_regs *regs)
{
	return regs->trap & 0x1;
}

static __always_inline void set_trap_norestart(struct pt_regs *regs)
{
	regs->trap |= 0x1;
}

#define kernel_stack_pointer(regs) ((regs)->gpr[1])
static inline int is_syscall_success(struct pt_regs *regs)
{
	if (trap_is_scv(regs))
		return !IS_ERR_VALUE((unsigned long)regs->gpr[3]);
	else
		return !(regs->ccr & 0x10000000);
}

static inline long regs_return_value(struct pt_regs *regs)
{
	if (trap_is_scv(regs))
		return regs->gpr[3];

	if (is_syscall_success(regs))
		return regs->gpr[3];
	else
		return -regs->gpr[3];
}

static inline void regs_set_return_value(struct pt_regs *regs, unsigned long rc)
{
	regs->gpr[3] = rc;
}

static inline bool cpu_has_msr_ri(void)
{
	return !IS_ENABLED(CONFIG_BOOKE) && !IS_ENABLED(CONFIG_40x);
}

static inline bool regs_is_unrecoverable(struct pt_regs *regs)
{
	return unlikely(cpu_has_msr_ri() && !(regs->msr & MSR_RI));
}

static inline void regs_set_recoverable(struct pt_regs *regs)
{
	if (cpu_has_msr_ri())
		regs_set_return_msr(regs, regs->msr | MSR_RI);
}

static inline void regs_set_unrecoverable(struct pt_regs *regs)
{
	if (cpu_has_msr_ri())
		regs_set_return_msr(regs, regs->msr & ~MSR_RI);
}

#define arch_has_single_step()	(1)
#define arch_has_block_step()	(true)
#define ARCH_HAS_USER_SINGLE_STEP_REPORT

/*
 * kprobe-based event tracer support
 */

#include <linux/stddef.h>
#include <linux/thread_info.h>
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);
#define MAX_REG_OFFSET (offsetof(struct pt_regs, dsisr))

/**
 * regs_get_register() - get register value from its offset
 * @regs:	   pt_regs from which register value is gotten
 * @offset:    offset number of the register.
 *
 * regs_get_register returns the value of a register whose offset from @regs.
 * The @offset is the offset of the register in struct pt_regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
						unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:      pt_regs which contains kernel stack pointer.
 * @addr:      address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */

static inline bool regs_within_kernel_stack(struct pt_regs *regs,
						unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1)));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);
	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

#endif /* __ASSEMBLY__ */

#ifndef __powerpc64__
/* We need PT_SOFTE defined at all time to avoid #ifdefs */
#define PT_SOFTE PT_MQ
#else /* __powerpc64__ */
#define PT_FPSCR32 (PT_FPR0 + 2*32 + 1)	/* each FP reg occupies 2 32-bit userspace slots */
#define PT_VR0_32 164	/* each Vector reg occupies 4 slots in 32-bit */
#define PT_VSCR_32 (PT_VR0 + 32*4 + 3)
#define PT_VRSAVE_32 (PT_VR0 + 33*4)
#define PT_VSR0_32 300 	/* each VSR reg occupies 4 slots in 32-bit */
#endif /* __powerpc64__ */
#endif /* _ASM_POWERPC_PTRACE_H */
