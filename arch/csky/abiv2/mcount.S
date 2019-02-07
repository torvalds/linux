/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include <asm/ftrace.h>

/*
 * csky-gcc with -pg will put the following asm after prologue:
 *      push	r15
 *      jsri	_mcount
 *
 * stack layout after mcount_enter in _mcount():
 *
 * current sp => 0:+-------+
 *                 | a0-a3 | -> must save all argument regs
 *             +16:+-------+
 *                 | lr    | -> _mcount lr (instrumente function's pc)
 *             +20:+-------+
 *                 | fp=r8 | -> instrumented function fp
 *             +24:+-------+
 *                 | plr   | -> instrumented function lr (parent's pc)
 *                 +-------+
 */

.macro mcount_enter
	subi	sp, 24
	stw	a0, (sp, 0)
	stw	a1, (sp, 4)
	stw	a2, (sp, 8)
	stw	a3, (sp, 12)
	stw	lr, (sp, 16)
	stw	r8, (sp, 20)
.endm

.macro mcount_exit
	ldw	a0, (sp, 0)
	ldw	a1, (sp, 4)
	ldw	a2, (sp, 8)
	ldw	a3, (sp, 12)
	ldw	t1, (sp, 16)
	ldw	r8, (sp, 20)
	ldw	lr, (sp, 24)
	addi	sp, 28
	jmp	t1
.endm

.macro save_return_regs
	subi	sp, 16
	stw	a0, (sp, 0)
	stw	a1, (sp, 4)
	stw	a2, (sp, 8)
	stw	a3, (sp, 12)
.endm

.macro restore_return_regs
	mov	lr, a0
	ldw	a0, (sp, 0)
	ldw	a1, (sp, 4)
	ldw	a2, (sp, 8)
	ldw	a3, (sp, 12)
	addi	sp, 16
.endm

ENTRY(ftrace_stub)
	jmp	lr
END(ftrace_stub)

ENTRY(_mcount)
	mcount_enter

	/* r26 is link register, only used with jsri translation */
	lrw	r26, ftrace_trace_function
	ldw	r26, (r26, 0)
	lrw	a1, ftrace_stub
	cmpne	r26, a1
	bf	skip_ftrace

	mov	a0, lr
	subi	a0, MCOUNT_INSN_SIZE
	ldw	a1, (sp, 24)

	jsr	r26

#ifndef CONFIG_FUNCTION_GRAPH_TRACER
skip_ftrace:
	mcount_exit
#else
skip_ftrace:
	lrw	a0, ftrace_graph_return
	ldw	a0, (a0, 0)
	lrw	a1, ftrace_stub
	cmpne	a0, a1
	bt	ftrace_graph_caller

	lrw	a0, ftrace_graph_entry
	ldw	a0, (a0, 0)
	lrw	a1, ftrace_graph_entry_stub
	cmpne	a0, a1
	bt	ftrace_graph_caller

	mcount_exit
#endif
END(_mcount)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	mov	a0, sp
	addi	a0, 24
	ldw	a1, (sp, 16)
	subi	a1, MCOUNT_INSN_SIZE
	mov	a2, r8
	lrw	r26, prepare_ftrace_return
	jsr	r26
	mcount_exit
END(ftrace_graph_caller)

ENTRY(return_to_handler)
	save_return_regs
	mov	a0, r8
	jsri	ftrace_return_to_handler
	restore_return_regs
	jmp	lr
END(return_to_handler)
#endif
