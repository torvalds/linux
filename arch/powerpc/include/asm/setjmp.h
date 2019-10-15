/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright © 2008 Michael Neuling IBM Corporation
 */
#ifndef _ASM_POWERPC_SETJMP_H
#define _ASM_POWERPC_SETJMP_H

#define JMP_BUF_LEN    23

extern long setjmp(long *) __attribute__((returns_twice));
extern void longjmp(long *, long) __attribute__((noreturn));

#endif /* _ASM_POWERPC_SETJMP_H */
