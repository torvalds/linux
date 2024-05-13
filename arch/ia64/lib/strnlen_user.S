/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Returns 0 if exception before NUL or reaching the supplied limit (N),
 * a value greater than N if the string is longer than the limit, else
 * strlen.
 *
 * Inputs:
 *	in0:	address of buffer
 *	in1:	string length limit N
 * Outputs:
 *	r8:	0 in case of fault, strlen(buffer)+1 otherwise
 *
 * Copyright (C) 1999, 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/export.h>
#include <asm/asmmacro.h>

GLOBAL_ENTRY(__strnlen_user)
	.prologue
	alloc r2=ar.pfs,2,0,0,0
	.save ar.lc, r16
	mov r16=ar.lc			// preserve ar.lc

	.body

	add r3=-1,in1
	;;
	mov ar.lc=r3
	mov r9=0
	;;
	// XXX braindead strlen loop---this needs to be optimized
.Loop1:
	EXCLR(.Lexit, ld1 r8=[in0],1)
	add r9=1,r9
	;;
	cmp.eq p6,p0=r8,r0
(p6)	br.cond.dpnt .Lexit
	br.cloop.dptk.few .Loop1

	add r9=1,in1			// NUL not found---return N+1
	;;
.Lexit:
	mov r8=r9
	mov ar.lc=r16			// restore ar.lc
	br.ret.sptk.many rp
END(__strnlen_user)
EXPORT_SYMBOL(__strnlen_user)
