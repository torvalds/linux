/*
 * Split from entry_32.S
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/magic.h>
#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/export.h>

#ifdef CONFIG_DYNAMIC_FTRACE
_GLOBAL(mcount)
_GLOBAL(_mcount)
	/*
	 * It is required that _mcount on PPC32 must preserve the
	 * link register. But we have r0 to play with. We use r0
	 * to push the return address back to the caller of mcount
	 * into the ctr register, restore the link register and
	 * then jump back using the ctr register.
	 */
	mflr	r0
	mtctr	r0
	lwz	r0, 4(r1)
	mtlr	r0
	bctr

_GLOBAL(ftrace_caller)
	MCOUNT_SAVE_FRAME
	/* r3 ends up with link register */
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
	MCOUNT_RESTORE_FRAME
	/* old link register ends up in ctr reg */
	bctr
#else
_GLOBAL(mcount)
_GLOBAL(_mcount)

	MCOUNT_SAVE_FRAME

	subi	r3, r3, MCOUNT_INSN_SIZE
	LOAD_REG_ADDR(r5, ftrace_trace_function)
	lwz	r5,0(r5)

	mtctr	r5
	bctrl
	nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	b	ftrace_graph_caller
#endif
	MCOUNT_RESTORE_FRAME
	bctr
#endif
EXPORT_SYMBOL(_mcount)

_GLOBAL(ftrace_stub)
	blr

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(ftrace_graph_caller)
	/* load r4 with local address */
	lwz	r4, 44(r1)
	subi	r4, r4, MCOUNT_INSN_SIZE

	/* Grab the LR out of the caller stack frame */
	lwz	r3,52(r1)

	bl	prepare_ftrace_return
	nop

        /*
         * prepare_ftrace_return gives us the address we divert to.
         * Change the LR in the callers stack frame to this.
         */
	stw	r3,52(r1)

	MCOUNT_RESTORE_FRAME
	/* old link register ends up in ctr reg */
	bctr

_GLOBAL(return_to_handler)
	/* need to save return values */
	stwu	r1, -32(r1)
	stw	r3, 20(r1)
	stw	r4, 16(r1)
	stw	r31, 12(r1)
	mr	r31, r1

	bl	ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

	lwz	r3, 20(r1)
	lwz	r4, 16(r1)
	lwz	r31,12(r1)
	lwz	r1, 0(r1)

	/* Jump back to real return address */
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
