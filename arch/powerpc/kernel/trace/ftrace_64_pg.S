/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Split from ftrace_64.S
 */

#include <linux/magic.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/ppc-opcode.h>
#include <asm/export.h>

_GLOBAL_TOC(ftrace_caller)
	lbz	r3, PACA_FTRACE_ENABLED(r13)
	cmpdi	r3, 0
	beqlr

	/* Taken from output of objdump from lib64/glibc */
	mflr	r3
	ld	r11, 0(r1)
	stdu	r1, -112(r1)
	std	r3, 128(r1)
	ld	r4, 16(r11)
	subi	r3, r3, MCOUNT_INSN_SIZE
.globl ftrace_call
ftrace_call:
	bl	ftrace_stub
	nop
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	b	ftrace_graph_stub
_GLOBAL(ftrace_graph_stub)
#endif
	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112

_GLOBAL(ftrace_stub)
	blr

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(ftrace_graph_caller)
	addi	r5, r1, 112
	/* load r4 with local address */
	ld	r4, 128(r1)
	subi	r4, r4, MCOUNT_INSN_SIZE

	/* Grab the LR out of the caller stack frame */
	ld	r11, 112(r1)
	ld	r3, 16(r11)

	bl	prepare_ftrace_return
	nop

	/*
	 * prepare_ftrace_return gives us the address we divert to.
	 * Change the LR in the callers stack frame to this.
	 */
	ld	r11, 112(r1)
	std	r3, 16(r11)

	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
