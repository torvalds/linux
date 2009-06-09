/*
 * mcount and friends -- ftrace stuff
 *
 * Copyright (C) 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/ftrace.h>

.text

/* GCC will have called us before setting up the function prologue, so we
 * can clobber the normal scratch registers, but we need to make sure to
 * save/restore the registers used for argument passing (R0-R2) in case
 * the profiled function is using them.  With data registers, R3 is the
 * only one we can blow away.  With pointer registers, we have P0-P2.
 *
 * Upon entry, the RETS will point to the top of the current profiled
 * function.  And since GCC setup the frame for us, the previous function
 * will be waiting there.  mmmm pie.
 */
ENTRY(__mcount)
	/* save third function arg early so we can do testing below */
	[--sp] = r2;

	/* load the function pointer to the tracer */
	p0.l = _ftrace_trace_function;
	p0.h = _ftrace_trace_function;
	r3 = [p0];

	/* optional micro optimization: don't call the stub tracer */
	r2.l = _ftrace_stub;
	r2.h = _ftrace_stub;
	cc = r2 == r3;
	if ! cc jump .Ldo_trace;

	r2 = [sp++];
	rts;

.Ldo_trace:

	/* save first/second function arg and the return register */
	[--sp] = r0;
	[--sp] = r1;
	[--sp] = rets;

	/* setup the tracer function */
	p0 = r3;

	/* tracer(ulong frompc, ulong selfpc):
	 *  frompc: the pc that did the call to ...
	 *  selfpc: ... this location
	 * the selfpc itself will need adjusting for the mcount call
	 */
	r1 = rets;
	r0 = [fp + 4];
	r1 += -MCOUNT_INSN_SIZE;

	/* call the tracer */
	call (p0);

	/* restore state and get out of dodge */
	rets = [sp++];
	r1 = [sp++];
	r0 = [sp++];
	r2 = [sp++];

.globl _ftrace_stub
_ftrace_stub:
	rts;
ENDPROC(__mcount)
