/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLER__

#include <vdso/datapage.h>
#include <asm/vdso/vsyscall.h>
#include <asm/syscall.h>
#include <asm/unistd.h>
#include <asm/page.h>

/**
 * getrandom_syscall - Invoke the getrandom() syscall.
 * @buffer:	Destination buffer to fill with random bytes.
 * @len:	Size of @buffer in bytes.
 * @flags:	Zero or more GRND_* flags.
 * Returns:	The number of random bytes written to @buffer, or a negative value indicating an error.
 */
static __always_inline ssize_t getrandom_syscall(void *buffer, size_t len, unsigned int flags)
{
	return syscall3(__NR_getrandom, (long)buffer, (long)len, (long)flags);
}

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
