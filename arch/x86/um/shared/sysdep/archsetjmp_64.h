/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/um/include/sysdep-x86_64/archsetjmp.h
 */

#ifndef _KLIBC_ARCHSETJMP_H
#define _KLIBC_ARCHSETJMP_H

struct __jmp_buf {
	unsigned long __rbx;
	unsigned long __rsp;
	unsigned long __rbp;
	unsigned long __r12;
	unsigned long __r13;
	unsigned long __r14;
	unsigned long __r15;
	unsigned long __rip;
};

typedef struct __jmp_buf jmp_buf[1];

#define JB_IP __rip
#define JB_SP __rsp

#endif				/* _SETJMP_H */
