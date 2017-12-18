/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* MN10300 Userspace signal context
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SIGCONTEXT_H
#define _ASM_SIGCONTEXT_H

struct fpucontext {
	/* Regular FPU environment */
	unsigned long	fs[32];		/* fpu registers */
	unsigned long	fpcr;		/* fpu control register */
};

struct sigcontext {
	unsigned long	d0;
	unsigned long	d1;
	unsigned long	d2;
	unsigned long	d3;
	unsigned long	a0;
	unsigned long	a1;
	unsigned long	a2;
	unsigned long	a3;
	unsigned long	e0;
	unsigned long	e1;
	unsigned long	e2;
	unsigned long	e3;
	unsigned long	e4;
	unsigned long	e5;
	unsigned long	e6;
	unsigned long	e7;
	unsigned long	lar;
	unsigned long	lir;
	unsigned long	mdr;
	unsigned long	mcvf;
	unsigned long	mcrl;
	unsigned long	mcrh;
	unsigned long	mdrq;
	unsigned long	sp;
	unsigned long	epsw;
	unsigned long	pc;
	struct fpucontext *fpucontext;
	unsigned long	oldmask;
};


#endif /* _ASM_SIGCONTEXT_H */
