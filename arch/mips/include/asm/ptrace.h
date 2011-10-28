/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

/* 0 - 31 are integer registers, 32 - 63 are fp registers.  */
#define FPR_BASE	32
#define PC		64
#define CAUSE		65
#define BADVADDR	66
#define MMHI		67
#define MMLO		68
#define FPC_CSR		69
#define FPC_EIR		70
#define DSP_BASE	71		/* 3 more hi / lo register pairs */
#define DSP_CONTROL	77
#define ACX		78

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
#ifdef CONFIG_32BIT
	/* Pad bytes for argument save space on the stack. */
	unsigned long pad0[6];
#endif

	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Saved special registers. */
	unsigned long cp0_status;
	unsigned long hi;
	unsigned long lo;
#ifdef CONFIG_CPU_HAS_SMARTMIPS
	unsigned long acx;
#endif
	unsigned long cp0_badvaddr;
	unsigned long cp0_cause;
	unsigned long cp0_epc;
#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long cp0_tcstatus;
#endif /* CONFIG_MIPS_MT_SMTC */
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	unsigned long long mpl[3];        /* MTM{0,1,2} */
	unsigned long long mtp[3];        /* MTP{0,1,2} */
#endif
} __attribute__ ((aligned (8)));

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS		14
#define PTRACE_SETFPREGS		15
/* #define PTRACE_GETFPXREGS		18 */
/* #define PTRACE_SETFPXREGS		19 */

#define PTRACE_OLDSETOPTIONS	21

#define PTRACE_GET_THREAD_AREA	25
#define PTRACE_SET_THREAD_AREA	26

/* Calls to trace a 64bit program from a 32bit program.  */
#define PTRACE_PEEKTEXT_3264	0xc0
#define PTRACE_PEEKDATA_3264	0xc1
#define PTRACE_POKETEXT_3264	0xc2
#define PTRACE_POKEDATA_3264	0xc3
#define PTRACE_GET_THREAD_AREA_3264	0xc4

/* Read and write watchpoint registers.  */
enum pt_watch_style {
	pt_watch_style_mips32,
	pt_watch_style_mips64
};
struct mips32_watch_regs {
	unsigned int watchlo[8];
	/* Lower 16 bits of watchhi. */
	unsigned short watchhi[8];
	/* Valid mask and I R W bits.
	 * bit 0 -- 1 if W bit is usable.
	 * bit 1 -- 1 if R bit is usable.
	 * bit 2 -- 1 if I bit is usable.
	 * bits 3 - 11 -- Valid watchhi mask bits.
	 */
	unsigned short watch_masks[8];
	/* The number of valid watch register pairs.  */
	unsigned int num_valid;
} __attribute__((aligned(8)));

struct mips64_watch_regs {
	unsigned long long watchlo[8];
	unsigned short watchhi[8];
	unsigned short watch_masks[8];
	unsigned int num_valid;
} __attribute__((aligned(8)));

struct pt_watch_regs {
	enum pt_watch_style style;
	union {
		struct mips32_watch_regs mips32;
		struct mips64_watch_regs mips64;
	};
};

#define PTRACE_GET_WATCH_REGS	0xd0
#define PTRACE_SET_WATCH_REGS	0xd1

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/isadep.h>

struct task_struct;

extern int ptrace_getregs(struct task_struct *child, __s64 __user *data);
extern int ptrace_setregs(struct task_struct *child, __s64 __user *data);

extern int ptrace_getfpregs(struct task_struct *child, __u32 __user *data);
extern int ptrace_setfpregs(struct task_struct *child, __u32 __user *data);

extern int ptrace_get_watch_regs(struct task_struct *child,
	struct pt_watch_regs __user *addr);
extern int ptrace_set_watch_regs(struct task_struct *child,
	struct pt_watch_regs __user *addr);

/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) (((regs)->cp0_status & KU_MASK) == KU_USER)

#define regs_return_value(_regs) ((_regs)->regs[2])
#define instruction_pointer(regs) ((regs)->cp0_epc)
#define profile_pc(regs) instruction_pointer(regs)

extern asmlinkage void syscall_trace_enter(struct pt_regs *regs);
extern asmlinkage void syscall_trace_leave(struct pt_regs *regs);

extern NORET_TYPE void die(const char *, struct pt_regs *) ATTRIB_NORET;

static inline void die_if_kernel(const char *str, struct pt_regs *regs)
{
	if (unlikely(!user_mode(regs)))
		die(str, regs);
}

#endif

#endif /* _ASM_PTRACE_H */
