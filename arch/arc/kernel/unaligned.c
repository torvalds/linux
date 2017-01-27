/*
 * Copyright (C) 2011-2012 Synopsys (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg : May 2011
 *  -Adapted (from .26 to .35)
 *  -original contribution by Tim.yao@amlogic.com
 *
 */

#include <linux/types.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <asm/disasm.h>

#ifdef CONFIG_CPU_BIG_ENDIAN
#define BE		1
#define FIRST_BYTE_16	"swap %1, %1\n swape %1, %1\n"
#define FIRST_BYTE_32	"swape %1, %1\n"
#else
#define BE		0
#define FIRST_BYTE_16
#define FIRST_BYTE_32
#endif

#define __get8_unaligned_check(val, addr, err)		\
	__asm__(					\
	"1:	ldb.ab	%1, [%2, 1]\n"			\
	"2:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.align	4\n"				\
	"3:	mov	%0, 1\n"			\
	"	j	2b\n"				\
	"	.previous\n"				\
	"	.section __ex_table,\"a\"\n"		\
	"	.align	4\n"				\
	"	.long	1b, 3b\n"			\
	"	.previous\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get16_unaligned_check(val, addr)		\
	do {						\
		unsigned int err = 0, v, a = addr;	\
		__get8_unaligned_check(v, a, err);	\
		val =  v << ((BE) ? 8 : 0);		\
		__get8_unaligned_check(v, a, err);	\
		val |= v << ((BE) ? 0 : 8);		\
		if (err)				\
			goto fault;			\
	} while (0)

#define get32_unaligned_check(val, addr)		\
	do {						\
		unsigned int err = 0, v, a = addr;	\
		__get8_unaligned_check(v, a, err);	\
		val =  v << ((BE) ? 24 : 0);		\
		__get8_unaligned_check(v, a, err);	\
		val |= v << ((BE) ? 16 : 8);		\
		__get8_unaligned_check(v, a, err);	\
		val |= v << ((BE) ? 8 : 16);		\
		__get8_unaligned_check(v, a, err);	\
		val |= v << ((BE) ? 0 : 24);		\
		if (err)				\
			goto fault;			\
	} while (0)

#define put16_unaligned_check(val, addr)		\
	do {						\
		unsigned int err = 0, v = val, a = addr;\
							\
		__asm__(				\
		FIRST_BYTE_16				\
		"1:	stb.ab	%1, [%2, 1]\n"		\
		"	lsr %1, %1, 8\n"		\
		"2:	stb	%1, [%2]\n"		\
		"3:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"	.align	4\n"			\
		"4:	mov	%0, 1\n"		\
		"	j	3b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.align	4\n"			\
		"	.long	1b, 4b\n"		\
		"	.long	2b, 4b\n"		\
		"	.previous\n"			\
		: "=r" (err), "=&r" (v), "=&r" (a)	\
		: "0" (err), "1" (v), "2" (a));		\
							\
		if (err)				\
			goto fault;			\
	} while (0)

#define put32_unaligned_check(val, addr)		\
	do {						\
		unsigned int err = 0, v = val, a = addr;\
							\
		__asm__(				\
		FIRST_BYTE_32				\
		"1:	stb.ab	%1, [%2, 1]\n"		\
		"	lsr %1, %1, 8\n"		\
		"2:	stb.ab	%1, [%2, 1]\n"		\
		"	lsr %1, %1, 8\n"		\
		"3:	stb.ab	%1, [%2, 1]\n"		\
		"	lsr %1, %1, 8\n"		\
		"4:	stb	%1, [%2]\n"		\
		"5:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"	.align	4\n"			\
		"6:	mov	%0, 1\n"		\
		"	j	5b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.align	4\n"			\
		"	.long	1b, 6b\n"		\
		"	.long	2b, 6b\n"		\
		"	.long	3b, 6b\n"		\
		"	.long	4b, 6b\n"		\
		"	.previous\n"			\
		: "=r" (err), "=&r" (v), "=&r" (a)	\
		: "0" (err), "1" (v), "2" (a));		\
							\
		if (err)				\
			goto fault;			\
	} while (0)

/* sysctl hooks */
int unaligned_enabled __read_mostly = 1;	/* Enabled by default */
int no_unaligned_warning __read_mostly = 1;	/* Only 1 warning by default */

static void fixup_load(struct disasm_state *state, struct pt_regs *regs,
			struct callee_regs *cregs)
{
	int val;

	/* register write back */
	if ((state->aa == 1) || (state->aa == 2)) {
		set_reg(state->wb_reg, state->src1 + state->src2, regs, cregs);

		if (state->aa == 2)
			state->src2 = 0;
	}

	if (state->zz == 0) {
		get32_unaligned_check(val, state->src1 + state->src2);
	} else {
		get16_unaligned_check(val, state->src1 + state->src2);

		if (state->x)
			val = (val << 16) >> 16;
	}

	if (state->pref == 0)
		set_reg(state->dest, val, regs, cregs);

	return;

fault:	state->fault = 1;
}

static void fixup_store(struct disasm_state *state, struct pt_regs *regs,
			struct callee_regs *cregs)
{
	/* register write back */
	if ((state->aa == 1) || (state->aa == 2)) {
		set_reg(state->wb_reg, state->src2 + state->src3, regs, cregs);

		if (state->aa == 3)
			state->src3 = 0;
	} else if (state->aa == 3) {
		if (state->zz == 2) {
			set_reg(state->wb_reg, state->src2 + (state->src3 << 1),
				regs, cregs);
		} else if (!state->zz) {
			set_reg(state->wb_reg, state->src2 + (state->src3 << 2),
				regs, cregs);
		} else {
			goto fault;
		}
	}

	/* write fix-up */
	if (!state->zz)
		put32_unaligned_check(state->src1, state->src2 + state->src3);
	else
		put16_unaligned_check(state->src1, state->src2 + state->src3);

	return;

fault:	state->fault = 1;
}

/*
 * Handle an unaligned access
 * Returns 0 if successfully handled, 1 if some error happened
 */
int misaligned_fixup(unsigned long address, struct pt_regs *regs,
		     struct callee_regs *cregs)
{
	struct disasm_state state;
	char buf[TASK_COMM_LEN];

	/* handle user mode only and only if enabled by sysadmin */
	if (!user_mode(regs) || !unaligned_enabled)
		return 1;

	if (no_unaligned_warning) {
		pr_warn_once("%s(%d) made unaligned access which was emulated"
			     " by kernel assist\n. This can degrade application"
			     " performance significantly\n. To enable further"
			     " logging of such instances, please \n"
			     " echo 0 > /proc/sys/kernel/ignore-unaligned-usertrap\n",
			     get_task_comm(buf, current), task_pid_nr(current));
	} else {
		/* Add rate limiting if it gets down to it */
		pr_warn("%s(%d): unaligned access to/from 0x%lx by PC: 0x%lx\n",
			get_task_comm(buf, current), task_pid_nr(current),
			address, regs->ret);

	}

	disasm_instr(regs->ret, &state, 1, regs, cregs);

	if (state.fault)
		goto fault;

	/* ldb/stb should not have unaligned exception */
	if ((state.zz == 1) || (state.di))
		goto fault;

	if (!state.write)
		fixup_load(&state, regs, cregs);
	else
		fixup_store(&state, regs, cregs);

	if (state.fault)
		goto fault;

	/* clear any remanants of delay slot */
	if (delay_mode(regs)) {
		regs->ret = regs->bta ~1U;
		regs->status32 &= ~STATUS_DE_MASK;
	} else {
		regs->ret += state.instr_len;

		/* handle zero-overhead-loop */
		if ((regs->ret == regs->lp_end) && (regs->lp_count)) {
			regs->ret = regs->lp_start;
			regs->lp_count--;
		}
	}

	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, address);
	return 0;

fault:
	pr_err("Alignment trap: fault in fix-up %08lx at [<%08lx>]\n",
		state.words[0], address);

	return 1;
}
