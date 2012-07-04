/*
 *  Copyright (C) 2004, 2006, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.34: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_PTRACE_H
#define _ASM_C6X_PTRACE_H

#define BKPT_OPCODE	0x56454314	/* illegal opcode */

#ifdef _BIG_ENDIAN
#define PT_LO(odd, even)  odd
#define PT_HI(odd, even)  even
#else
#define PT_LO(odd, even)  even
#define PT_HI(odd, even)  odd
#endif

#define PT_A4_ORG  PT_LO(1, 0)
#define PT_TSR	   PT_HI(1, 0)
#define PT_ILC	   PT_LO(3, 2)
#define PT_RILC    PT_HI(3, 2)
#define PT_CSR	   PT_LO(5, 4)
#define PT_PC	   PT_HI(5, 4)
#define PT_B16	   PT_LO(7, 6)
#define PT_B17	   PT_HI(7, 6)
#define PT_B18	   PT_LO(9, 8)
#define PT_B19	   PT_HI(9, 8)
#define PT_B20	   PT_LO(11, 10)
#define PT_B21	   PT_HI(11, 10)
#define PT_B22	   PT_LO(13, 12)
#define PT_B23	   PT_HI(13, 12)
#define PT_B24	   PT_LO(15, 14)
#define PT_B25	   PT_HI(15, 14)
#define PT_B26	   PT_LO(17, 16)
#define PT_B27	   PT_HI(17, 16)
#define PT_B28	   PT_LO(19, 18)
#define PT_B29	   PT_HI(19, 18)
#define PT_B30	   PT_LO(21, 20)
#define PT_B31	   PT_HI(21, 20)
#define PT_B0	   PT_LO(23, 22)
#define PT_B1	   PT_HI(23, 22)
#define PT_B2	   PT_LO(25, 24)
#define PT_B3	   PT_HI(25, 24)
#define PT_B4	   PT_LO(27, 26)
#define PT_B5	   PT_HI(27, 26)
#define PT_B6	   PT_LO(29, 28)
#define PT_B7	   PT_HI(29, 28)
#define PT_B8	   PT_LO(31, 30)
#define PT_B9	   PT_HI(31, 30)
#define PT_B10	   PT_LO(33, 32)
#define PT_B11	   PT_HI(33, 32)
#define PT_B12	   PT_LO(35, 34)
#define PT_B13	   PT_HI(35, 34)
#define PT_A16	   PT_LO(37, 36)
#define PT_A17	   PT_HI(37, 36)
#define PT_A18	   PT_LO(39, 38)
#define PT_A19	   PT_HI(39, 38)
#define PT_A20	   PT_LO(41, 40)
#define PT_A21	   PT_HI(41, 40)
#define PT_A22	   PT_LO(43, 42)
#define PT_A23	   PT_HI(43, 42)
#define PT_A24	   PT_LO(45, 44)
#define PT_A25	   PT_HI(45, 44)
#define PT_A26	   PT_LO(47, 46)
#define PT_A27	   PT_HI(47, 46)
#define PT_A28	   PT_LO(49, 48)
#define PT_A29	   PT_HI(49, 48)
#define PT_A30	   PT_LO(51, 50)
#define PT_A31	   PT_HI(51, 50)
#define PT_A0	   PT_LO(53, 52)
#define PT_A1	   PT_HI(53, 52)
#define PT_A2	   PT_LO(55, 54)
#define PT_A3	   PT_HI(55, 54)
#define PT_A4	   PT_LO(57, 56)
#define PT_A5	   PT_HI(57, 56)
#define PT_A6	   PT_LO(59, 58)
#define PT_A7	   PT_HI(59, 58)
#define PT_A8	   PT_LO(61, 60)
#define PT_A9	   PT_HI(61, 60)
#define PT_A10	   PT_LO(63, 62)
#define PT_A11	   PT_HI(63, 62)
#define PT_A12	   PT_LO(65, 64)
#define PT_A13	   PT_HI(65, 64)
#define PT_A14	   PT_LO(67, 66)
#define PT_A15	   PT_HI(67, 66)
#define PT_B14	   PT_LO(69, 68)
#define PT_B15	   PT_HI(69, 68)

#define NR_PTREGS  70

#define PT_DP	   PT_B14  /* Data Segment Pointer (B14) */
#define PT_SP	   PT_B15  /* Stack Pointer (B15)  */

#define PTRACE_GETFDPIC		31	/* get the ELF fdpic loadmap address */

#define PTRACE_GETFDPIC_EXEC	0	/* [addr] request the executable loadmap */
#define PTRACE_GETFDPIC_INTERP	1	/* [addr] request the interpreter loadmap */

#ifndef __ASSEMBLY__

#ifdef _BIG_ENDIAN
#define REG_PAIR(odd, even) unsigned long odd; unsigned long even
#else
#define REG_PAIR(odd, even) unsigned long even; unsigned long odd
#endif

/*
 * this struct defines the way the registers are stored on the
 * stack during a system call. fields defined with REG_PAIR
 * are saved and restored using double-word memory operations
 * which means the word ordering of the pair depends on endianess.
 */
struct pt_regs {
	REG_PAIR(tsr, orig_a4);
	REG_PAIR(rilc, ilc);
	REG_PAIR(pc, csr);

	REG_PAIR(b17, b16);
	REG_PAIR(b19, b18);
	REG_PAIR(b21, b20);
	REG_PAIR(b23, b22);
	REG_PAIR(b25, b24);
	REG_PAIR(b27, b26);
	REG_PAIR(b29, b28);
	REG_PAIR(b31, b30);

	REG_PAIR(b1, b0);
	REG_PAIR(b3, b2);
	REG_PAIR(b5, b4);
	REG_PAIR(b7, b6);
	REG_PAIR(b9, b8);
	REG_PAIR(b11, b10);
	REG_PAIR(b13, b12);

	REG_PAIR(a17, a16);
	REG_PAIR(a19, a18);
	REG_PAIR(a21, a20);
	REG_PAIR(a23, a22);
	REG_PAIR(a25, a24);
	REG_PAIR(a27, a26);
	REG_PAIR(a29, a28);
	REG_PAIR(a31, a30);

	REG_PAIR(a1, a0);
	REG_PAIR(a3, a2);
	REG_PAIR(a5, a4);
	REG_PAIR(a7, a6);
	REG_PAIR(a9, a8);
	REG_PAIR(a11, a10);
	REG_PAIR(a13, a12);

	REG_PAIR(a15, a14);
	REG_PAIR(sp, dp);
};

#ifdef __KERNEL__

#include <linux/linkage.h>

#define user_mode(regs)	((((regs)->tsr) & 0x40) != 0)

#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)
#define user_stack_pointer(regs) ((regs)->sp)

extern void show_regs(struct pt_regs *);

extern asmlinkage unsigned long syscall_trace_entry(struct pt_regs *regs);
extern asmlinkage void syscall_trace_exit(struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _ASM_C6X_PTRACE_H */
