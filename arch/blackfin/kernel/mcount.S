/*
 * linux/arch/blackfin/mcount.S
 *
 * Copyright (C) 2006 Analog Devices Inc.
 *
 * 2007/04/12 Save index, length, modify and base registers. --rpm
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>

.text

.align 4 	/* just in case */

ENTRY(__mcount)
	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;
	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;
	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;
	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = ( r7:0, p5:0 );
	[--sp] = ASTAT;

	p1.L = _ipipe_trace_enable;
	p1.H = _ipipe_trace_enable;
	r7 = [p1];
	CC = r7 == 0;
	if CC jump out;
	link 0x10;
	r0 = 0x0;
	[sp + 0xc] = r0; /* v */
	r0 = 0x0;	/* type: IPIPE_TRACE_FN */
	r1 = rets;
	p0 = [fp];	/* p0: Prior FP */
	r2 = [p0 + 4];	/* r2: Prior RETS */
	call ___ipipe_trace;
	unlink;
out:
	ASTAT = [sp++];
	( r7:0, p5:0 ) = [sp++];
	b3 = [sp++];
	b2 = [sp++];
	b1 = [sp++];
	b0 = [sp++];
	m3 = [sp++];
	m2 = [sp++];
	m1 = [sp++];
	m0 = [sp++];
	l3 = [sp++];
	l2 = [sp++];
	l1 = [sp++];
	l0 = [sp++];
	i3 = [sp++];
	i2 = [sp++];
	i1 = [sp++];
	i0 = [sp++];
	rts;
ENDPROC(__mcount)
