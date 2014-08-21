/*
 * arch/sh/lib/mcount.S
 *
 *  Copyright (C) 2008, 2009  Paul Mundt
 *  Copyright (C) 2008, 2009  Matt Fleming
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <asm/ftrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

#define MCOUNT_ENTER()		\
	mov.l	r4, @-r15;	\
	mov.l	r5, @-r15;	\
	mov.l	r6, @-r15;	\
	mov.l	r7, @-r15;	\
	sts.l	pr, @-r15;	\
				\
	mov.l	@(20,r15),r4;	\
	sts	pr, r5

#define MCOUNT_LEAVE()		\
	lds.l	@r15+, pr;	\
	mov.l	@r15+, r7;	\
	mov.l	@r15+, r6;	\
	mov.l	@r15+, r5;	\
	rts;			\
	 mov.l	@r15+, r4

#ifdef CONFIG_STACK_DEBUG
/*
 * Perform diagnostic checks on the state of the kernel stack.
 *
 * Check for stack overflow. If there is less than 1KB free
 * then it has overflowed.
 *
 * Make sure the stack pointer contains a valid address. Valid
 * addresses for kernel stacks are anywhere after the bss
 * (after __bss_stop) and anywhere in init_thread_union (init_stack).
 */
#define STACK_CHECK()					\
	mov	#(THREAD_SIZE >> 10), r0;		\
	shll8	r0;					\
	shll2	r0;					\
							\
	/* r1 = sp & (THREAD_SIZE - 1) */		\
	mov	#-1, r1;				\
	add	r0, r1;					\
	and	r15, r1;				\
							\
	mov	#TI_SIZE, r3;				\
	mov	#(STACK_WARN >> 8), r2;			\
	shll8	r2;					\
	add	r3, r2;					\
							\
	/* Is the stack overflowing? */			\
	cmp/hi	r2, r1;					\
	bf	stack_panic;				\
							\
	/* If sp > __bss_stop then we're OK. */		\
	mov.l	.L_ebss, r1;				\
	cmp/hi	r1, r15;				\
	bt	1f;					\
							\
	/* If sp < init_stack, we're not OK. */		\
	mov.l	.L_init_thread_union, r1;		\
	cmp/hs	r1, r15;				\
	bf	stack_panic;				\
							\
	/* If sp > init_stack && sp < __bss_stop, not OK. */	\
	add	r0, r1;					\
	cmp/hs	r1, r15;				\
	bt	stack_panic;				\
1:
#else
#define STACK_CHECK()
#endif /* CONFIG_STACK_DEBUG */

	.align 2
	.globl	_mcount
	.type	_mcount,@function
	.globl	mcount
	.type	mcount,@function
_mcount:
mcount:
	STACK_CHECK()

#ifndef CONFIG_FUNCTION_TRACER
	rts
	 nop
#else
	MCOUNT_ENTER()

#ifdef CONFIG_DYNAMIC_FTRACE
	.globl	mcount_call
mcount_call:
	mov.l	.Lftrace_stub, r6
#else
	mov.l	.Lftrace_trace_function, r6
	mov.l	ftrace_stub, r7
	cmp/eq	r6, r7
	bt	skip_trace
	mov.l	@r6, r6
#endif

	jsr	@r6
	 nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	mov.l   .Lftrace_graph_return, r6
	mov.l   .Lftrace_stub, r7
	cmp/eq  r6, r7
	bt      1f

	mov.l   .Lftrace_graph_caller, r0
	jmp     @r0
	 nop

1:
	mov.l	.Lftrace_graph_entry, r6
	mov.l	.Lftrace_graph_entry_stub, r7
	cmp/eq	r6, r7
	bt	skip_trace

	mov.l   .Lftrace_graph_caller, r0
	jmp	@r0
	 nop

	.align 2
.Lftrace_graph_return:
	.long   ftrace_graph_return
.Lftrace_graph_entry:
	.long   ftrace_graph_entry
.Lftrace_graph_entry_stub:
	.long   ftrace_graph_entry_stub
.Lftrace_graph_caller:
	.long   ftrace_graph_caller
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	.globl skip_trace
skip_trace:
	MCOUNT_LEAVE()

	.align 2
.Lftrace_trace_function:
	.long   ftrace_trace_function

#ifdef CONFIG_DYNAMIC_FTRACE
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * NOTE: Do not move either ftrace_graph_call or ftrace_caller
 * as this will affect the calculation of GRAPH_INSN_OFFSET.
 */
	.globl ftrace_graph_call
ftrace_graph_call:
	mov.l	.Lskip_trace, r0
	jmp	@r0
	 nop

	.align 2
.Lskip_trace:
	.long	skip_trace
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	.globl ftrace_caller
ftrace_caller:
	MCOUNT_ENTER()

	.globl ftrace_call
ftrace_call:
	mov.l	.Lftrace_stub, r6
	jsr	@r6
	 nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	bra	ftrace_graph_call
	 nop
#else
	MCOUNT_LEAVE()
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_DYNAMIC_FTRACE */

	.align 2

/*
 * NOTE: From here on the locations of the .Lftrace_stub label and
 * ftrace_stub itself are fixed. Adding additional data here will skew
 * the displacement for the memory table and break the block replacement.
 * Place new labels either after the ftrace_stub body, or before
 * ftrace_caller. You have been warned.
 */
.Lftrace_stub:
	.long	ftrace_stub

	.globl	ftrace_stub
ftrace_stub:
	rts
	 nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl	ftrace_graph_caller
ftrace_graph_caller:
	mov.l	2f, r1
	jmp	@r1
	 nop
1:
	/*
	 * MCOUNT_ENTER() pushed 5 registers onto the stack, so
	 * the stack address containing our return address is
	 * r15 + 20.
	 */
	mov	#20, r0
	add	r15, r0
	mov	r0, r4

	mov.l	.Lprepare_ftrace_return, r0
	jsr	@r0
	 nop

	MCOUNT_LEAVE()

	.align 2
2:	.long	skip_trace
.Lprepare_ftrace_return:
	.long	prepare_ftrace_return

	.globl	return_to_handler
return_to_handler:
	/*
	 * Save the return values.
	 */
	mov.l	r0, @-r15
	mov.l	r1, @-r15

	mov	#0, r4

	mov.l	.Lftrace_return_to_handler, r0
	jsr	@r0
	 nop

	/*
	 * The return value from ftrace_return_handler has the real
	 * address that we should return to.
	 */
	lds	r0, pr
	mov.l	@r15+, r1
	rts
	 mov.l	@r15+, r0


	.align 2
.Lftrace_return_to_handler:
	.long	ftrace_return_to_handler
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_STACK_DEBUG
	.globl	stack_panic
stack_panic:
	mov.l	.Ldump_stack, r0
	jsr	@r0
	 nop

	mov.l	.Lpanic, r0
	jsr	@r0
	 mov.l	.Lpanic_s, r4

	rts
	 nop

	.align 2
.L_init_thread_union:
	.long	init_thread_union
.L_ebss:
	.long	__bss_stop
.Lpanic:
	.long	panic
.Lpanic_s:
	.long	.Lpanic_str
.Ldump_stack:
	.long	dump_stack

	.section	.rodata
	.align 2
.Lpanic_str:
	.string "Stack error"
#endif /* CONFIG_STACK_DEBUG */
