/*
 * Split from entry_64.S
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/magic.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/ppc-opcode.h>
#include <asm/export.h>

#ifdef CONFIG_DYNAMIC_FTRACE
_GLOBAL(mcount)
_GLOBAL(_mcount)
EXPORT_SYMBOL(_mcount)
	mflr	r12
	mtctr	r12
	mtlr	r0
	bctr

#else /* CONFIG_DYNAMIC_FTRACE */
_GLOBAL_TOC(_mcount)
EXPORT_SYMBOL(_mcount)
	/* Taken from output of objdump from lib64/glibc */
	mflr	r3
	ld	r11, 0(r1)
	stdu	r1, -112(r1)
	std	r3, 128(r1)
	ld	r4, 16(r11)

	subi	r3, r3, MCOUNT_INSN_SIZE
	LOAD_REG_ADDR(r5,ftrace_trace_function)
	ld	r5,0(r5)
	ld	r5,0(r5)
	mtctr	r5
	bctrl
	nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	b	ftrace_graph_caller
#endif
	ld	r0, 128(r1)
	mtlr	r0
	addi	r1, r1, 112
_GLOBAL(ftrace_stub)
	blr
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(return_to_handler)
	/* need to save return values */
	std	r4,  -32(r1)
	std	r3,  -24(r1)
	/* save TOC */
	std	r2,  -16(r1)
	std	r31, -8(r1)
	mr	r31, r1
	stdu	r1, -112(r1)

	/*
	 * We might be called from a module.
	 * Switch to our TOC to run inside the core kernel.
	 */
	ld	r2, PACATOC(r13)

	bl	ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

	ld	r1, 0(r1)
	ld	r4,  -32(r1)
	ld	r3,  -24(r1)
	ld	r2,  -16(r1)
	ld	r31, -8(r1)

	/* Jump back to real return address */
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
