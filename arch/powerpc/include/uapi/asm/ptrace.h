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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_ASM_POWERPC_PTRACE_H
#define _UAPI_ASM_POWERPC_PTRACE_H


#include <linux/types.h>

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long gpr[32];
	unsigned long nip;
	unsigned long msr;
	unsigned long orig_gpr3;	/* Used for restarting system calls */
	unsigned long ctr;
	unsigned long link;
	unsigned long xer;
	unsigned long ccr;
#ifdef __powerpc64__
	unsigned long softe;		/* Soft enabled/disabled */
#else
	unsigned long mq;		/* 601 only (not used at present) */
					/* Used on APUS to hold IPL value. */
#endif
	unsigned long trap;		/* Reason for being here */
	/* N.B. for critical exceptions on 4xx, the dar and dsisr
	   fields are overloaded to hold srr0 and srr1. */
	unsigned long dar;		/* Fault registers */
	unsigned long dsisr;		/* on 4xx/Book-E used for ESR */
	unsigned long result;		/* Result of a system call */
};

#endif /* __ASSEMBLY__ */


/*
 * Offsets used by 'ptrace' system call interface.
 * These can't be changed without breaking binary compatibility
 * with MkLinux, etc.
 */
#define PT_R0	0
#define PT_R1	1
#define PT_R2	2
#define PT_R3	3
#define PT_R4	4
#define PT_R5	5
#define PT_R6	6
#define PT_R7	7
#define PT_R8	8
#define PT_R9	9
#define PT_R10	10
#define PT_R11	11
#define PT_R12	12
#define PT_R13	13
#define PT_R14	14
#define PT_R15	15
#define PT_R16	16
#define PT_R17	17
#define PT_R18	18
#define PT_R19	19
#define PT_R20	20
#define PT_R21	21
#define PT_R22	22
#define PT_R23	23
#define PT_R24	24
#define PT_R25	25
#define PT_R26	26
#define PT_R27	27
#define PT_R28	28
#define PT_R29	29
#define PT_R30	30
#define PT_R31	31

#define PT_NIP	32
#define PT_MSR	33
#define PT_ORIG_R3 34
#define PT_CTR	35
#define PT_LNK	36
#define PT_XER	37
#define PT_CCR	38
#ifndef __powerpc64__
#define PT_MQ	39
#else
#define PT_SOFTE 39
#endif
#define PT_TRAP	40
#define PT_DAR	41
#define PT_DSISR 42
#define PT_RESULT 43
#define PT_DSCR 44
#define PT_REGS_COUNT 44

#define PT_FPR0	48	/* each FP reg occupies 2 slots in this space */

#ifndef __powerpc64__

#define PT_FPR31 (PT_FPR0 + 2*31)
#define PT_FPSCR (PT_FPR0 + 2*32 + 1)

#else /* __powerpc64__ */

#define PT_FPSCR (PT_FPR0 + 32)	/* each FP reg occupies 1 slot in 64-bit space */


#define PT_VR0 82	/* each Vector reg occupies 2 slots in 64-bit */
#define PT_VSCR (PT_VR0 + 32*2 + 1)
#define PT_VRSAVE (PT_VR0 + 33*2)


/*
 * Only store first 32 VSRs here. The second 32 VSRs in VR0-31
 */
#define PT_VSR0 150	/* each VSR reg occupies 2 slots in 64-bit */
#define PT_VSR31 (PT_VSR0 + 2*31)
#endif /* __powerpc64__ */

/*
 * Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go.
 * The transfer totals 34 quadword.  Quadwords 0-31 contain the
 * corresponding vector registers.  Quadword 32 contains the vscr as the
 * last word (offset 12) within that quadword.  Quadword 33 contains the
 * vrsave as the first word (offset 0) within the quadword.
 *
 * This definition of the VMX state is compatible with the current PPC32
 * ptrace interface.  This allows signal handling and ptrace to use the same
 * structures.  This also simplifies the implementation of a bi-arch
 * (combined (32- and 64-bit) gdb.
 */
#define PTRACE_GETVRREGS	0x12
#define PTRACE_SETVRREGS	0x13

/* Get/set all the upper 32-bits of the SPE registers, accumulator, and
 * spefscr, in one go */
#define PTRACE_GETEVRREGS	0x14
#define PTRACE_SETEVRREGS	0x15

/* Get the first 32 128bit VSX registers */
#define PTRACE_GETVSRREGS	0x1b
#define PTRACE_SETVSRREGS	0x1c

/*
 * Get or set a debug register. The first 16 are DABR registers and the
 * second 16 are IABR registers.
 */
#define PTRACE_GET_DEBUGREG	0x19
#define PTRACE_SET_DEBUGREG	0x1a

/* (new) PTRACE requests using the same numbers as x86 and the same
 * argument ordering. Additionally, they support more registers too
 */
#define PTRACE_GETREGS            0xc
#define PTRACE_SETREGS            0xd
#define PTRACE_GETFPREGS          0xe
#define PTRACE_SETFPREGS          0xf
#define PTRACE_GETREGS64	  0x16
#define PTRACE_SETREGS64	  0x17

/* Calls to trace a 64bit program from a 32bit program */
#define PPC_PTRACE_PEEKTEXT_3264 0x95
#define PPC_PTRACE_PEEKDATA_3264 0x94
#define PPC_PTRACE_POKETEXT_3264 0x93
#define PPC_PTRACE_POKEDATA_3264 0x92
#define PPC_PTRACE_PEEKUSR_3264  0x91
#define PPC_PTRACE_POKEUSR_3264  0x90

#define PTRACE_SINGLEBLOCK	0x100	/* resume execution until next branch */

#define PPC_PTRACE_GETHWDBGINFO	0x89
#define PPC_PTRACE_SETHWDEBUG	0x88
#define PPC_PTRACE_DELHWDEBUG	0x87

#ifndef __ASSEMBLY__

struct ppc_debug_info {
	__u32 version;			/* Only version 1 exists to date */
	__u32 num_instruction_bps;
	__u32 num_data_bps;
	__u32 num_condition_regs;
	__u32 data_bp_alignment;
	__u32 sizeof_condition;		/* size of the DVC register */
	__u64 features;
};

#endif /* __ASSEMBLY__ */

/*
 * features will have bits indication whether there is support for:
 */
#define PPC_DEBUG_FEATURE_INSN_BP_RANGE		0x0000000000000001
#define PPC_DEBUG_FEATURE_INSN_BP_MASK		0x0000000000000002
#define PPC_DEBUG_FEATURE_DATA_BP_RANGE		0x0000000000000004
#define PPC_DEBUG_FEATURE_DATA_BP_MASK		0x0000000000000008

#ifndef __ASSEMBLY__

struct ppc_hw_breakpoint {
	__u32 version;		/* currently, version must be 1 */
	__u32 trigger_type;	/* only some combinations allowed */
	__u32 addr_mode;	/* address match mode */
	__u32 condition_mode;	/* break/watchpoint condition flags */
	__u64 addr;		/* break/watchpoint address */
	__u64 addr2;		/* range end or mask */
	__u64 condition_value;	/* contents of the DVC register */
};

#endif /* __ASSEMBLY__ */

/*
 * Trigger Type
 */
#define PPC_BREAKPOINT_TRIGGER_EXECUTE	0x00000001
#define PPC_BREAKPOINT_TRIGGER_READ	0x00000002
#define PPC_BREAKPOINT_TRIGGER_WRITE	0x00000004
#define PPC_BREAKPOINT_TRIGGER_RW	\
	(PPC_BREAKPOINT_TRIGGER_READ | PPC_BREAKPOINT_TRIGGER_WRITE)

/*
 * Address Mode
 */
#define PPC_BREAKPOINT_MODE_EXACT		0x00000000
#define PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE	0x00000001
#define PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE	0x00000002
#define PPC_BREAKPOINT_MODE_MASK		0x00000003

/*
 * Condition Mode
 */
#define PPC_BREAKPOINT_CONDITION_MODE	0x00000003
#define PPC_BREAKPOINT_CONDITION_NONE	0x00000000
#define PPC_BREAKPOINT_CONDITION_AND	0x00000001
#define PPC_BREAKPOINT_CONDITION_EXACT	PPC_BREAKPOINT_CONDITION_AND
#define PPC_BREAKPOINT_CONDITION_OR	0x00000002
#define PPC_BREAKPOINT_CONDITION_AND_OR	0x00000003
#define PPC_BREAKPOINT_CONDITION_BE_ALL	0x00ff0000
#define PPC_BREAKPOINT_CONDITION_BE_SHIFT	16
#define PPC_BREAKPOINT_CONDITION_BE(n)	\
	(1<<((n)+PPC_BREAKPOINT_CONDITION_BE_SHIFT))

#endif /* _UAPI_ASM_POWERPC_PTRACE_H */
