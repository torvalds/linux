/*
 * Copyright (C) 2008 Imagination Technologies Ltd.
 * Licensed under the GPL
 *
 */

#include <asm/ftrace.h>

	.text
#ifdef CONFIG_DYNAMIC_FTRACE
	.global	_mcount_wrapper
	.type	_mcount_wrapper,function
_mcount_wrapper:
	MOV	PC,D0.4

	.global _ftrace_caller
	.type	_ftrace_caller,function
_ftrace_caller:
	MSETL   [A0StP], D0Ar6, D0Ar4, D0Ar2, D0.4
	MOV     D1Ar1, D0.4
	MOV     D0Ar2, D1RtP
	SUB	D1Ar1,D1Ar1,#MCOUNT_INSN_SIZE

	.global _ftrace_call
_ftrace_call:
	MOVT	D1RtP,#HI(_ftrace_stub)
	CALL	D1RtP,#LO(_ftrace_stub)
	GETL    D0.4,  D1RtP, [A0StP++#(-8)]
	GETL    D0Ar2, D1Ar1, [A0StP++#(-8)]
	GETL    D0Ar4, D1Ar3, [A0StP++#(-8)]
	GETL    D0Ar6, D1Ar5, [A0StP++#(-8)]
	MOV     PC, D0.4
#else

	.global	_mcount_wrapper
	.type	_mcount_wrapper,function
_mcount_wrapper:
	MSETL   [A0StP], D0Ar6, D0Ar4, D0Ar2, D0.4
	MOV     D1Ar1, D0.4
	MOV     D0Ar2, D1RtP
	MOVT    D0Re0,#HI(_ftrace_trace_function)
	ADD	D0Re0,D0Re0,#LO(_ftrace_trace_function)
	GET	D1Ar3,[D0Re0]
	MOVT	D1Re0,#HI(_ftrace_stub)
	ADD	D1Re0,D1Re0,#LO(_ftrace_stub)
	CMP	D1Ar3,D1Re0
	BEQ	$Ltrace_exit
	MOV	D1RtP,D1Ar3
	SUB	D1Ar1,D1Ar1,#MCOUNT_INSN_SIZE
	SWAP	PC,D1RtP
$Ltrace_exit:
	GETL    D0.4,  D1RtP, [A0StP++#(-8)]
	GETL    D0Ar2, D1Ar1, [A0StP++#(-8)]
	GETL    D0Ar4, D1Ar3, [A0StP++#(-8)]
	GETL    D0Ar6, D1Ar5, [A0StP++#(-8)]
	MOV     PC, D0.4

#endif	/* CONFIG_DYNAMIC_FTRACE */

	.global _ftrace_stub
_ftrace_stub:
	MOV 	PC,D1RtP
