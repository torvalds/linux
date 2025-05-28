/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLY__

#include <asm/unistd.h>
#include <asm/vdso/vsyscall.h>
#include <vdso/datapage.h>

/**
 * getrandom_syscall - Invoke the getrandom() syscall.
 * @buffer:	Destination buffer to fill with random bytes.
 * @len:	Size of @buffer in bytes.
 * @flags:	Zero or more GRND_* flags.
 * Returns:	The number of random bytes written to @buffer, or a negative value indicating an error.
 */
static __always_inline ssize_t getrandom_syscall(void *_buffer, size_t _len, unsigned int _flags)
{
	register void *buffer asm ("x0") = _buffer;
	register size_t len asm ("x1") = _len;
	register unsigned int flags asm ("x2") = _flags;
	register long ret asm ("x0");
	register long nr asm ("x8") = __NR_getrandom;

	asm volatile(
	"       svc #0\n"
	: "=r" (ret)
	: "r" (buffer), "r" (len), "r" (flags), "r" (nr)
	: "memory");

	return ret;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
