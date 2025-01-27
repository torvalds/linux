/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Christophe Leroy <christophe.leroy@csgroup.eu>, CS GROUP France
 */
#ifndef _ASM_POWERPC_VDSO_GETRANDOM_H
#define _ASM_POWERPC_VDSO_GETRANDOM_H

#ifndef __ASSEMBLY__

#include <asm/vdso_datapage.h>

static __always_inline int do_syscall_3(const unsigned long _r0, const unsigned long _r3,
					const unsigned long _r4, const unsigned long _r5)
{
	register long r0 asm("r0") = _r0;
	register unsigned long r3 asm("r3") = _r3;
	register unsigned long r4 asm("r4") = _r4;
	register unsigned long r5 asm("r5") = _r5;
	register int ret asm ("r3");

	asm volatile(
		"       sc\n"
		"	bns+	1f\n"
		"	neg	%0, %0\n"
		"1:\n"
	: "=r" (ret), "+r" (r4), "+r" (r5), "+r" (r0)
	: "r" (r3)
	: "memory", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "cr0", "ctr");

	return ret;
}

/**
 * getrandom_syscall - Invoke the getrandom() syscall.
 * @buffer:	Destination buffer to fill with random bytes.
 * @len:	Size of @buffer in bytes.
 * @flags:	Zero or more GRND_* flags.
 * Returns:	The number of bytes written to @buffer, or a negative value indicating an error.
 */
static __always_inline ssize_t getrandom_syscall(void *buffer, size_t len, unsigned int flags)
{
	return do_syscall_3(__NR_getrandom, (unsigned long)buffer,
			    (unsigned long)len, (unsigned long)flags);
}

static __always_inline struct vdso_rng_data *__arch_get_vdso_rng_data(void)
{
	struct vdso_arch_data *data;

	asm (
		"	bcl	20, 31, .+4 ;"
		"0:	mflr	%0 ;"
		"	addis	%0, %0, (_vdso_datapage - 0b)@ha ;"
		"	addi	%0, %0, (_vdso_datapage - 0b)@l  ;"
		: "=r" (data) : : "lr"
	);

	return &data->rng_data;
}

ssize_t __c_kernel_getrandom(void *buffer, size_t len, unsigned int flags, void *opaque_state,
			     size_t opaque_len);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_VDSO_GETRANDOM_H */
