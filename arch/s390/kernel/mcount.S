/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2008, 2009
 *
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/nospec-insn.h>
#include <asm/ptrace.h>
#include <asm/march.h>

#define STACK_FRAME_SIZE_PTREGS		(STACK_FRAME_OVERHEAD + __PT_SIZE)
#define STACK_PTREGS			(STACK_FRAME_OVERHEAD)
#define STACK_PTREGS_GPRS		(STACK_PTREGS + __PT_GPRS)
#define STACK_PTREGS_PSW		(STACK_PTREGS + __PT_PSW)

#define STACK_FRAME_SIZE_FREGS		(STACK_FRAME_OVERHEAD + __FTRACE_REGS_SIZE)
#define STACK_FREGS			(STACK_FRAME_OVERHEAD)
#define STACK_FREGS_PTREGS		(STACK_FRAME_OVERHEAD + __FTRACE_REGS_PT_REGS)
#define STACK_FREGS_PTREGS_GPRS		(STACK_FREGS_PTREGS + __PT_GPRS)
#define STACK_FREGS_PTREGS_PSW		(STACK_FREGS_PTREGS + __PT_PSW)
#define STACK_FREGS_PTREGS_ORIG_GPR2	(STACK_FREGS_PTREGS + __PT_ORIG_GPR2)
#define STACK_FREGS_PTREGS_FLAGS	(STACK_FREGS_PTREGS + __PT_FLAGS)

/* packed stack: allocate just enough for r14, r15 and backchain */
#define TRACED_FUNC_FRAME_SIZE	24

#ifdef CONFIG_FUNCTION_TRACER

	GEN_BR_THUNK %r1
	GEN_BR_THUNK %r14

	.section .kprobes.text, "ax"

SYM_FUNC_START(ftrace_stub)
	BR_EX	%r14
SYM_FUNC_END(ftrace_stub)

SYM_CODE_START(ftrace_stub_direct_tramp)
	lgr	%r1, %r0
	BR_EX	%r1
SYM_CODE_END(ftrace_stub_direct_tramp)

	.macro	ftrace_regs_entry, allregs=0
	stg	%r14,(__SF_GPRS+8*8)(%r15)	# save traced function caller

	.if \allregs == 1
	# save psw mask
	# don't put any instructions clobbering CC before this point
	epsw	%r1,%r14
	risbg	%r14,%r1,0,31,32
	.endif

	lgr	%r1,%r15
	# allocate stack frame for ftrace_caller to contain traced function
	aghi	%r15,-TRACED_FUNC_FRAME_SIZE
	stg	%r1,__SF_BACKCHAIN(%r15)
	stg	%r0,(__SF_GPRS+8*8)(%r15)
	stg	%r15,(__SF_GPRS+9*8)(%r15)
	# allocate ftrace_regs and stack frame for ftrace_trace_function
	aghi	%r15,-STACK_FRAME_SIZE_FREGS
	stg	%r1,(STACK_FREGS_PTREGS_GPRS+15*8)(%r15)
	xc	STACK_FREGS_PTREGS_ORIG_GPR2(8,%r15),STACK_FREGS_PTREGS_ORIG_GPR2(%r15)

	.if \allregs == 1
	stg	%r14,(STACK_FREGS_PTREGS_PSW)(%r15)
	mvghi	STACK_FREGS_PTREGS_FLAGS(%r15),_PIF_FTRACE_FULL_REGS
	.else
	xc	STACK_FREGS_PTREGS_FLAGS(8,%r15),STACK_FREGS_PTREGS_FLAGS(%r15)
	.endif

	lg	%r14,(__SF_GPRS+8*8)(%r1)	# restore original return address
	aghi	%r1,-TRACED_FUNC_FRAME_SIZE
	stg	%r1,__SF_BACKCHAIN(%r15)
	stg	%r0,(STACK_FREGS_PTREGS_PSW+8)(%r15)
	stmg	%r2,%r14,(STACK_FREGS_PTREGS_GPRS+2*8)(%r15)
	.endm

SYM_CODE_START(ftrace_regs_caller)
	ftrace_regs_entry	1
	j	ftrace_common
SYM_CODE_END(ftrace_regs_caller)

SYM_CODE_START(ftrace_caller)
	ftrace_regs_entry	0
	j	ftrace_common
SYM_CODE_END(ftrace_caller)

SYM_CODE_START(ftrace_common)
#ifdef MARCH_HAS_Z196_FEATURES
	aghik	%r2,%r0,-MCOUNT_INSN_SIZE
	lgrl	%r4,function_trace_op
	lgrl	%r1,ftrace_func
#else
	lgr	%r2,%r0
	aghi	%r2,-MCOUNT_INSN_SIZE
	larl	%r4,function_trace_op
	lg	%r4,0(%r4)
	larl	%r1,ftrace_func
	lg	%r1,0(%r1)
#endif
	lgr	%r3,%r14
	la	%r5,STACK_FREGS(%r15)
	BASR_EX	%r14,%r1
	lg	%r0,(STACK_FREGS_PTREGS_PSW+8)(%r15)
#ifdef MARCH_HAS_Z196_FEATURES
	ltg	%r1,STACK_FREGS_PTREGS_ORIG_GPR2(%r15)
	locgrz	%r1,%r0
#else
	lg	%r1,STACK_FREGS_PTREGS_ORIG_GPR2(%r15)
	ltgr	%r1,%r1
	jnz	0f
	lgr	%r1,%r0
#endif
0:	lmg	%r2,%r15,(STACK_FREGS_PTREGS_GPRS+2*8)(%r15)
	BR_EX	%r1
SYM_CODE_END(ftrace_common)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

SYM_FUNC_START(return_to_handler)
	stmg	%r2,%r5,32(%r15)
	lgr	%r1,%r15
	# allocate ftrace_regs and stack frame for ftrace_return_to_handler
	aghi	%r15,-STACK_FRAME_SIZE_FREGS
	stg	%r1,__SF_BACKCHAIN(%r15)
	stg	%r2,(STACK_FREGS_PTREGS_GPRS+2*8)(%r15)
	stg	%r1,(STACK_FREGS_PTREGS_GPRS+15*8)(%r15)
	la	%r2,STACK_FRAME_OVERHEAD(%r15)
	brasl	%r14,ftrace_return_to_handler
	aghi	%r15,STACK_FRAME_SIZE_FREGS
	lgr	%r14,%r2
	lmg	%r2,%r5,32(%r15)
	BR_EX	%r14
SYM_FUNC_END(return_to_handler)

#endif
#endif /* CONFIG_FUNCTION_TRACER */

SYM_CODE_START(ftrace_shared_hotpatch_trampoline_br)
	lmg	%r0,%r1,2(%r1)
	br	%r1
SYM_INNER_LABEL(ftrace_shared_hotpatch_trampoline_br_end, SYM_L_GLOBAL)
SYM_CODE_END(ftrace_shared_hotpatch_trampoline_br)

#ifdef CONFIG_EXPOLINE
SYM_CODE_START(ftrace_shared_hotpatch_trampoline_exrl)
	lmg	%r0,%r1,2(%r1)
	exrl	%r0,0f
	j	.
0:	br	%r1
SYM_INNER_LABEL(ftrace_shared_hotpatch_trampoline_exrl_end, SYM_L_GLOBAL)
SYM_CODE_END(ftrace_shared_hotpatch_trampoline_exrl)
#endif /* CONFIG_EXPOLINE */

#ifdef CONFIG_RETHOOK

SYM_CODE_START(arch_rethook_trampoline)
	stg	%r14,(__SF_GPRS+8*8)(%r15)
	lay	%r15,-STACK_FRAME_SIZE_PTREGS(%r15)
	stmg	%r0,%r14,STACK_PTREGS_GPRS(%r15)

	# store original stack pointer in backchain and pt_regs
	lay	%r7,STACK_FRAME_SIZE_PTREGS(%r15)
	stg	%r7,__SF_BACKCHAIN(%r15)
	stg	%r7,STACK_PTREGS_GPRS+(15*8)(%r15)

	# store full psw
	epsw	%r2,%r3
	risbg	%r3,%r2,0,31,32
	stg	%r3,STACK_PTREGS_PSW(%r15)
	larl	%r1,arch_rethook_trampoline
	stg	%r1,STACK_PTREGS_PSW+8(%r15)

	lay	%r2,STACK_PTREGS(%r15)
	brasl	%r14,arch_rethook_trampoline_callback

	mvc	__SF_EMPTY(16,%r7),STACK_PTREGS_PSW(%r15)
	lmg	%r0,%r15,STACK_PTREGS_GPRS(%r15)
	lpswe	__SF_EMPTY(%r15)
SYM_CODE_END(arch_rethook_trampoline)

#endif /* CONFIG_RETHOOK */
