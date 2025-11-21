/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/nospec-insn.h>

#define R2_OFF 0
#define R5_OFF (R2_OFF + (5 - 2 + 1) * 8)
#define R14_OFF (R5_OFF + 8)
#define RETADDR_OFF (R14_OFF + 8)
#define R15_OFF (RETADDR_OFF + 8)
#define BACKCHAIN_OFF (R15_OFF + 8)
#define FRAME_SIZE (BACKCHAIN_OFF + 8)
#define FRAME_OFF (STACK_FRAME_OVERHEAD - FRAME_SIZE)
#if (FRAME_OFF + BACKCHAIN_OFF) != __SF_BACKCHAIN
#error Stack frame layout calculation is broken
#endif

	GEN_BR_THUNK %r1

SYM_FUNC_START(arch_bpf_timed_may_goto)
	/*
	 * This function has a special ABI: the parameters are in %r12 and
	 * %r13; the return value is in %r12; all GPRs except %r0, %r1, and
	 * %r12 are callee-saved; and the return address is in %r0.
	 */
	stmg %r2,%r5,FRAME_OFF+R2_OFF(%r15)
	stg %r14,FRAME_OFF+R14_OFF(%r15)
	stg %r0,FRAME_OFF+RETADDR_OFF(%r15)
	stg %r15,FRAME_OFF+R15_OFF(%r15)
	lgr %r1,%r15
	lay %r15,-FRAME_SIZE(%r15)
	stg %r1,__SF_BACKCHAIN(%r15)

	lay %r2,0(%r12,%r13)
	brasl %r14,bpf_check_timed_may_goto
	lgr %r12,%r2

	lg %r15,FRAME_SIZE+FRAME_OFF+R15_OFF(%r15)
	lmg %r2,%r5,FRAME_OFF+R2_OFF(%r15)
	lg %r14,FRAME_OFF+R14_OFF(%r15)
	lg %r1,FRAME_OFF+RETADDR_OFF(%r15)
	BR_EX %r1
SYM_FUNC_END(arch_bpf_timed_may_goto)
