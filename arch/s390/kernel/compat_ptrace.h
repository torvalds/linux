/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PTRACE32_H
#define _PTRACE32_H

#include <asm/ptrace.h>    /* needed for NUM_CR_WORDS */
#include "compat_linux.h"  /* needed for psw_compat_t */

struct compat_per_struct_kernel {
	__u32 cr9;		/* PER control bits */
	__u32 cr10;		/* PER starting address */
	__u32 cr11;		/* PER ending address */
	__u32 bits;		/* Obsolete software bits */
	__u32 starting_addr;	/* User specified start address */
	__u32 ending_addr;	/* User specified end address */
	__u16 perc_atmid;	/* PER trap ATMID */
	__u32 address;		/* PER trap instruction address */
	__u8  access_id;	/* PER trap access identification */
};

struct compat_user_regs_struct
{
	psw_compat_t psw;
	u32 gprs[NUM_GPRS];
	u32 acrs[NUM_ACRS];
	u32 orig_gpr2;
	/* nb: there's a 4-byte hole here */
	s390_fp_regs fp_regs;
	/*
	 * These per registers are in here so that gdb can modify them
	 * itself as there is no "official" ptrace interface for hardware
	 * watchpoints. This is the way intel does it.
	 */
	struct compat_per_struct_kernel per_info;
	u32  ieee_instruction_pointer;	/* obsolete, always 0 */
};

struct compat_user {
	/* We start with the registers, to mimic the way that "memory"
	   is returned from the ptrace(3,...) function.  */
	struct compat_user_regs_struct regs;
	/* The rest of this junk is to help gdb figure out what goes where */
	u32 u_tsize;		/* Text segment size (pages). */
	u32 u_dsize;	        /* Data segment size (pages). */
	u32 u_ssize;	        /* Stack segment size (pages). */
	u32 start_code;         /* Starting virtual address of text. */
	u32 start_stack;	/* Starting virtual address of stack area.
				   This is actually the bottom of the stack,
				   the top of the stack is always found in the
				   esp register.  */
	s32 signal;     	 /* Signal that caused the core dump. */
	u32 u_ar0;               /* Used by gdb to help find the values for */
	                         /* the registers. */
	u32 magic;		 /* To uniquely identify a core file */
	char u_comm[32];	 /* User command that was responsible */
};

typedef struct
{
	__u32   len;
	__u32   kernel_addr;
	__u32   process_addr;
} compat_ptrace_area;

#endif /* _PTRACE32_H */
