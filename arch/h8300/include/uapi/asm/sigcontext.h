/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_H8300_SIGCONTEXT_H
#define _ASM_H8300_SIGCONTEXT_H

struct sigcontext {
	unsigned long  sc_mask;		/* old sigmask */
	unsigned long  sc_usp;		/* old user stack pointer */
	unsigned long  sc_er0;
	unsigned long  sc_er1;
	unsigned long  sc_er2;
	unsigned long  sc_er3;
	unsigned long  sc_er4;
	unsigned long  sc_er5;
	unsigned long  sc_er6;
	unsigned short sc_ccr;
	unsigned long  sc_pc;
};

#endif
