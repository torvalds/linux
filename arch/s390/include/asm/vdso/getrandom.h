/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLY__

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

static __always_inline const struct vdso_rng_data *__arch_get_vdso_rng_data(void)
{
	/*
	 * The RNG data is in the real VVAR data page, but if a task belongs to a time namespace
	 * then VVAR_DATA_PAGE_OFFSET points to the namespace-specific VVAR page and VVAR_TIMENS_
	 * PAGE_OFFSET points to the real VVAR page.
	 */
	if (IS_ENABLED(CONFIG_TIME_NS) && _vdso_data->clock_mode == VDSO_CLOCKMODE_TIMENS)
		return (void *)&_vdso_rng_data + VVAR_TIMENS_PAGE_OFFSET * PAGE_SIZE;
	return &_vdso_rng_data;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
