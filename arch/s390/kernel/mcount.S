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
#include <asm/export.h>


#define STACK_FRAME_SIZE	(STACK_FRAME_OVERHEAD + __PT_SIZE)
#define STACK_PTREGS		(STACK_FRAME_OVERHEAD)
#define STACK_PTREGS_GPRS	(STACK_PTREGS + __PT_GPRS)
#define STACK_PTREGS_PSW	(STACK_PTREGS + __PT_PSW)
#define STACK_PTREGS_ORIG_GPR2	(STACK_PTREGS + __PT_ORIG_GPR2)
#define STACK_PTREGS_FLAGS	(STACK_PTREGS + __PT_FLAGS)
/* packed stack: allocate just enough for r14, r15 and backchain */
#define TRACED_FUNC_FRAME_SIZE	24

#ifdef CONFIG_FUNCTION_TRACER

	GEN_BR_THUNK %r1
	GEN_BR_THUNK %r14

	.section .kprobes.text, "ax"

ENTRY(ftrace_stub)
	BR_EX	%r14
ENDPROC(ftrace_stub)

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
	# allocate pt_regs and stack frame for ftrace_trace_function
	aghi	%r15,-STACK_FRAME_SIZE
	stg	%r1,(STACK_PTREGS_GPRS+15*8)(%r15)
	xc	STACK_PTREGS_ORIG_GPR2(8,%r15),STACK_PTREGS_ORIG_GPR2(%r15)

	.if \allregs == 1
	stg	%r14,(STACK_PTREGS_PSW)(%r15)
	mvghi	STACK_PTREGS_FLAGS(%r15),_PIF_FTRACE_FULL_REGS
	.else
	xc	STACK_PTREGS_FLAGS(8,%r15),STACK_PTREGS_FLAGS(%r15)
	.endif

	lg	%r14,(__SF_GPRS+8*8)(%r1)	# restore original return address
	aghi	%r1,-TRACED_FUNC_FRAME_SIZE
	stg	%r1,__SF_BACKCHAIN(%r15)
	stg	%r0,(STACK_PTREGS_PSW+8)(%r15)
	stmg	%r2,%r14,(STACK_PTREGS_GPRS+2*8)(%r15)
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
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
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
	la	%r5,STACK_PTREGS(%r15)
	BASR_EX	%r14,%r1
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
# The j instruction gets runtime patched to a nop instruction.
# See ftrace_enable_ftrace_graph_caller.
SYM_INNER_LABEL(ftrace_graph_caller, SYM_L_GLOBAL)
	j	.Lftrace_graph_caller_end
	lmg	%r2,%r3,(STACK_PTREGS_GPRS+14*8)(%r15)
	lg	%r4,(STACK_PTREGS_PSW+8)(%r15)
	brasl	%r14,prepare_ftrace_return
	stg	%r2,(STACK_PTREGS_GPRS+14*8)(%r15)
.Lftrace_graph_caller_end:
#endif
	lg	%r0,(STACK_PTREGS_PSW+8)(%r15)
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	ltg	%r1,STACK_PTREGS_ORIG_GPR2(%r15)
	locgrz	%r1,%r0
#else
	lg	%r1,STACK_PTREGS_ORIG_GPR2(%r15)
	ltgr	%r1,%r1
	jnz	0f
	lgr	%r1,%r0
#endif
0:	lmg	%r2,%r15,(STACK_PTREGS_GPRS+2*8)(%r15)
	BR_EX	%r1
SYM_CODE_END(ftrace_common)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

SYM_FUNC_START(return_to_handler)
	stmg	%r2,%r5,32(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)
	brasl	%r14,ftrace_return_to_handler
	aghi	%r15,STACK_FRAME_OVERHEAD
	lgr	%r14,%r2
	lmg	%r2,%r5,32(%r15)
	BR_EX	%r14
SYM_FUNC_END(return_to_handler)

#endif
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_KPROBES

SYM_FUNC_START(__kretprobe_trampoline)

	stg	%r14,(__SF_GPRS+8*8)(%r15)
	lay	%r15,-STACK_FRAME_SIZE(%r15)
	stmg	%r0,%r14,STACK_PTREGS_GPRS(%r15)

	# store original stack pointer in backchain and pt_regs
	lay	%r7,STACK_FRAME_SIZE(%r15)
	stg	%r7,__SF_BACKCHAIN(%r15)
	stg	%r7,STACK_PTREGS_GPRS+(15*8)(%r15)

	# store full psw
	epsw	%r2,%r3
	risbg	%r3,%r2,0,31,32
	stg	%r3,STACK_PTREGS_PSW(%r15)
	larl	%r1,__kretprobe_trampoline
	stg	%r1,STACK_PTREGS_PSW+8(%r15)

	lay	%r2,STACK_PTREGS(%r15)
	brasl	%r14,trampoline_probe_handler

	mvc	__SF_EMPTY(16,%r7),STACK_PTREGS_PSW(%r15)
	lmg	%r0,%r15,STACK_PTREGS_GPRS(%r15)
	lpswe	__SF_EMPTY(%r15)

SYM_FUNC_END(__kretprobe_trampoline)

#endif /* CONFIG_KPROBES */
