/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef __ASM_ARC_PTRACE_H
#define __ASM_ARC_PTRACE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* THE pt_regs: Defines how regs are saved during entry into kernel */

struct pt_regs {
	/*
	 * 1 word gutter after reg-file has been saved
	 * Technically not needed, Since SP always points to a "full" location
	 * (vs. "empty"). But pt_regs is shared with tools....
	 */
	long res;

	/* Real registers */
	long bta;	/* bta_l1, bta_l2, erbta */
	long lp_start;
	long lp_end;
	long lp_count;
	long status32;	/* status32_l1, status32_l2, erstatus */
	long ret;	/* ilink1, ilink2 or eret */
	long blink;
	long fp;
	long r26;	/* gp */
	long r12;
	long r11;
	long r10;
	long r9;
	long r8;
	long r7;
	long r6;
	long r5;
	long r4;
	long r3;
	long r2;
	long r1;
	long r0;
	long sp;	/* user/kernel sp depending on where we came from  */
	long orig_r0;
	long orig_r8;	/*to distinguish bet excp, sys call, int1 or int2 */
};

/* Callee saved registers - need to be saved only when you are scheduled out */

struct callee_regs {
	long res;	/* Again this is not needed */
	long r25;
	long r24;
	long r23;
	long r22;
	long r21;
	long r20;
	long r19;
	long r18;
	long r17;
	long r16;
	long r15;
	long r14;
	long r13;
};

#define instruction_pointer(regs)	((regs)->ret)
#define profile_pc(regs)		instruction_pointer(regs)

/* return 1 if user mode or 0 if kernel mode */
#define user_mode(regs) (regs->status32 & STATUS_U_MASK)

#define user_stack_pointer(regs)\
({  unsigned int sp;		\
	if (user_mode(regs))	\
		sp = (regs)->sp;\
	else			\
		sp = -1;	\
	sp;			\
})
#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#ifndef __ASSEMBLY__
/*
 * Userspace ABI: Register state needed by
 *  -ptrace (gdbserver)
 *  -sigcontext (SA_SIGNINFO signal frame)
 *
 * This is to decouple pt_regs from user-space ABI, to be able to change it
 * w/o affecting the ABI.
 * Although the layout (initial padding) is similar to pt_regs to have some
 * optimizations when copying pt_regs to/from user_regs_struct.
 *
 * Also, sigcontext only care about the scratch regs as that is what we really
 * save/restore for signal handling.
*/
struct user_regs_struct {

	struct scratch {
		long pad;
		long bta, lp_start, lp_end, lp_count;
		long status32, ret, blink, fp, gp;
		long r12, r11, r10, r9, r8, r7, r6, r5, r4, r3, r2, r1, r0;
		long sp;
	} scratch;
	struct callee {
		long pad;
		long r25, r24, r23, r22, r21, r20;
		long r19, r18, r17, r16, r15, r14, r13;
	} callee;
	long efa;	/* break pt addr, for break points in delay slots */
	long stop_pc;	/* give dbg stop_pc directly after checking orig_r8 */
};
#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PTRACE_H */
