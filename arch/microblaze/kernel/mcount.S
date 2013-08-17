/*
 * Low-level ftrace handling
 *
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/linkage.h>

#define NOALIGN_ENTRY(name)	.globl name; name:

/* FIXME MS: I think that I don't need to save all regs */
#define SAVE_REGS		\
	addik	r1, r1, -120;	\
	swi	r2, r1, 4;	\
	swi	r3, r1, 8;	\
	swi	r4, r1, 12;	\
	swi	r5, r1, 116;	\
	swi	r6, r1, 16;	\
	swi	r7, r1, 20;	\
	swi	r8, r1, 24;	\
	swi	r9, r1, 28;	\
	swi	r10, r1, 32;	\
	swi	r11, r1, 36;	\
	swi	r12, r1, 40;	\
	swi	r13, r1, 44;	\
	swi	r14, r1, 48;	\
	swi	r16, r1, 52;	\
	swi	r17, r1, 56;	\
	swi	r18, r1, 60;	\
	swi	r19, r1, 64;	\
	swi	r20, r1, 68;	\
	swi	r21, r1, 72;	\
	swi	r22, r1, 76;	\
	swi	r23, r1, 80;	\
	swi	r24, r1, 84;	\
	swi	r25, r1, 88;	\
	swi	r26, r1, 92;	\
	swi	r27, r1, 96;	\
	swi	r28, r1, 100;	\
	swi	r29, r1, 104;	\
	swi	r30, r1, 108;	\
	swi	r31, r1, 112;

#define RESTORE_REGS		\
	lwi	r2, r1, 4;	\
	lwi	r3, r1, 8;	\
	lwi	r4, r1, 12;	\
	lwi	r5, r1, 116;	\
	lwi	r6, r1, 16;	\
	lwi	r7, r1, 20;	\
	lwi	r8, r1, 24;	\
	lwi	r9, r1, 28;	\
	lwi	r10, r1, 32;	\
	lwi	r11, r1, 36;	\
	lwi	r12, r1, 40;	\
	lwi	r13, r1, 44;	\
	lwi	r14, r1, 48;	\
	lwi	r16, r1, 52;	\
	lwi	r17, r1, 56;	\
	lwi	r18, r1, 60;	\
	lwi	r19, r1, 64;	\
	lwi	r20, r1, 68;	\
	lwi	r21, r1, 72;	\
	lwi	r22, r1, 76;	\
	lwi	r23, r1, 80;	\
	lwi	r24, r1, 84;	\
	lwi	r25, r1, 88;	\
	lwi	r26, r1, 92;	\
	lwi	r27, r1, 96;	\
	lwi	r28, r1, 100;	\
	lwi	r29, r1, 104;	\
	lwi	r30, r1, 108;	\
	lwi	r31, r1, 112;	\
	addik	r1, r1, 120;

ENTRY(ftrace_stub)
	rtsd	r15, 8;
	nop;

ENTRY(_mcount)
#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(ftrace_caller)
	/* MS: It is just barrier which is removed from C code */
	rtsd	r15, 8
	nop
#endif /* CONFIG_DYNAMIC_FTRACE */
	SAVE_REGS
	swi	r15, r1, 0;
	/* MS: HAVE_FUNCTION_TRACE_MCOUNT_TEST begin of checking */
	lwi	r5, r0, function_trace_stop;
	bneid	r5, end;
	nop;
	/* MS: HAVE_FUNCTION_TRACE_MCOUNT_TEST end of checking */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#ifndef CONFIG_DYNAMIC_FTRACE
	lwi	r5, r0, ftrace_graph_return;
	addik	r6, r0, ftrace_stub; /* asm implementation */
	cmpu	r5, r5, r6; /* ftrace_graph_return != ftrace_stub */
	beqid	r5, end_graph_tracer;
	nop;

	lwi	r6, r0, ftrace_graph_entry;
	addik	r5, r0, ftrace_graph_entry_stub; /* implemented in C */
	cmpu	r5, r5, r6; /* ftrace_graph_entry != ftrace_graph_entry_stub */
	beqid	r5, end_graph_tracer;
	nop;
#else /* CONFIG_DYNAMIC_FTRACE */
NOALIGN_ENTRY(ftrace_call_graph)
	/* MS: jump over graph function - replaced from C code */
	bri	end_graph_tracer
#endif /* CONFIG_DYNAMIC_FTRACE */
	addik	r5, r1, 120; /* MS: load parent addr */
	addik	r6, r15, 0; /* MS: load current function addr */
	bralid	r15, prepare_ftrace_return;
	nop;
	/* MS: graph was taken that's why - can jump over function trace */
	brid	end;
	nop;
end_graph_tracer:
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#ifndef CONFIG_DYNAMIC_FTRACE
	/* MS: test function trace if is taken or not */
	lwi	r20, r0, ftrace_trace_function;
	addik	r6, r0, ftrace_stub;
	cmpu	r5, r20, r6; /* ftrace_trace_function != ftrace_stub */
	beqid	r5, end; /* MS: not taken -> jump over */
	nop;
#else /* CONFIG_DYNAMIC_FTRACE */
NOALIGN_ENTRY(ftrace_call)
/* instruction for setup imm FUNC_part1, addik r20, r0, FUNC_part2 */
	nop
	nop
#endif /* CONFIG_DYNAMIC_FTRACE */
/* static normal trace */
	lwi	r6, r1, 120; /* MS: load parent addr */
	addik	r5, r15, 0; /* MS: load current function addr */
	/* MS: here is dependency on previous code */
	brald	r15, r20; /* MS: jump to ftrace handler */
	nop;
end:
	lwi	r15, r1, 0;
	RESTORE_REGS

	rtsd	r15, 8; /* MS: jump back */
	nop;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(return_to_handler)
	nop; /* MS: just barrier for rtsd r15, 8 */
	nop;
	SAVE_REGS
	swi	r15, r1, 0;

	/* MS: find out returning address */
	bralid	r15, ftrace_return_to_handler;
	nop;

	/* MS: return value from ftrace_return_to_handler is my returning addr
	 * must be before restore regs because I have to restore r3 content */
	addik	r15, r3, 0;
	RESTORE_REGS

	rtsd	r15, 8; /* MS: jump back */
	nop;
#endif	/* CONFIG_FUNCTION_TRACER */
