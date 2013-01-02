#ifndef _ASM_CRIS_ARCH_USER_H
#define _ASM_CRIS_ARCH_USER_H

/* User-mode register used for core dumps. */

struct user_regs_struct {
	unsigned long r0;	/* General registers. */
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long sp;	/* R14, Stack pointer. */
	unsigned long acr;	/* R15, Address calculation register. */
	unsigned long bz;	/* P0, Constant zero (8-bits). */
	unsigned long vr;	/* P1, Version register (8-bits). */
	unsigned long pid;	/* P2, Process ID (8-bits). */
	unsigned long srs;	/* P3, Support register select (8-bits). */
	unsigned long wz;	/* P4, Constant zero (16-bits). */
	unsigned long exs;	/* P5, Exception status. */
	unsigned long eda;	/* P6, Exception data address. */
	unsigned long mof;	/* P7, Multiply overflow regiter. */
	unsigned long dz;	/* P8, Constant zero (32-bits). */
	unsigned long ebp;	/* P9, Exception base pointer. */
	unsigned long erp;	/* P10, Exception return pointer. */
	unsigned long srp;	/* P11, Subroutine return pointer. */
	unsigned long nrp;	/* P12, NMI return pointer. */
	unsigned long ccs;	/* P13, Condition code stack. */
	unsigned long usp;	/* P14, User mode stack pointer. */
	unsigned long spc;	/* P15, Single step PC. */
};

#endif /* _ASM_CRIS_ARCH_USER_H */
