/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/um/include/sysdep-i386/archsetjmp.h
 */

#ifndef _KLIBC_ARCHSETJMP_H
#define _KLIBC_ARCHSETJMP_H

struct __jmp_buf {
	unsigned int __ebx;
	unsigned int __esp;
	unsigned int __ebp;
	unsigned int __esi;
	unsigned int __edi;
	unsigned int __eip;
};

typedef struct __jmp_buf jmp_buf[1];

#define JB_IP __eip
#define JB_SP __esp

#endif				/* _SETJMP_H */
