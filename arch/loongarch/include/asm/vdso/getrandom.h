// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Xi Ruoyao <xry111@xry111.site>. All Rights Reserved.
 */
#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLY__

#include <asm/unistd.h>
#include <asm/vdso/vdso.h>

static __always_inline ssize_t getrandom_syscall(void *_buffer, size_t _len, unsigned int _flags)
{
	register long ret asm("a0");
	register long nr asm("a7") = __NR_getrandom;
	register void *buffer asm("a0") = _buffer;
	register size_t len asm("a1") = _len;
	register unsigned int flags asm("a2") = _flags;

	asm volatile(
	"      syscall 0\n"
	: "+r" (ret)
	: "r" (nr), "r" (buffer), "r" (len), "r" (flags)
	: "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
	  "memory");

	return ret;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
