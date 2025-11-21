/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Xi Ruoyao <xry111@xry111.site>. All Rights Reserved.
 */
#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLER__

#include <asm/unistd.h>

static __always_inline ssize_t getrandom_syscall(void *_buffer, size_t _len, unsigned int _flags)
{
	register long ret asm("a0");
	register long nr asm("a7") = __NR_getrandom;
	register void *buffer asm("a0") = _buffer;
	register size_t len asm("a1") = _len;
	register unsigned int flags asm("a2") = _flags;

	asm volatile ("ecall\n"
		      : "=r" (ret)
		      : "r" (nr), "r" (buffer), "r" (len), "r" (flags)
		      : "memory");

	return ret;
}

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
