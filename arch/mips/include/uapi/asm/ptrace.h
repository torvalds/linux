/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _UAPI_ASM_PTRACE_H
#define _UAPI_ASM_PTRACE_H

#include <linux/types.h>

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
 * This struct defines the registers as used by PTRACE_{GET,SET}REGS. The
 * format is the same for both 32- and 64-bit processes. Registers for 32-bit
 * processes are sign extended.
 */
#ifdef __KERNEL__
struct user_pt_regs {
#else
struct pt_regs {
#endif
	/* Saved main processor registers. */
	__u64 regs[32];

	/* Saved special registers. */
	__u64 lo;
	__u64 hi;
	__u64 cp0_epc;
	__u64 cp0_badvaddr;
	__u64 cp0_status;
	__u64 cp0_cause;
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

/* Calls to trace a 64bit program from a 32bit program.	 */
#define PTRACE_PEEKTEXT_3264	0xc0
#define PTRACE_PEEKDATA_3264	0xc1
#define PTRACE_POKETEXT_3264	0xc2
#define PTRACE_POKEDATA_3264	0xc3
#define PTRACE_GET_THREAD_AREA_3264	0xc4

/* Read and write watchpoint registers.	 */
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


#endif /* _UAPI_ASM_PTRACE_H */
