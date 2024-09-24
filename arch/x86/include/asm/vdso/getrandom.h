/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */
#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLY__

#include <asm/unistd.h>
#include <asm/vvar.h>

/**
 * getrandom_syscall - Invoke the getrandom() syscall.
 * @buffer:	Destination buffer to fill with random bytes.
 * @len:	Size of @buffer in bytes.
 * @flags:	Zero or more GRND_* flags.
 * Returns:	The number of random bytes written to @buffer, or a negative value indicating an error.
 */
static __always_inline ssize_t getrandom_syscall(void *buffer, size_t len, unsigned int flags)
{
	long ret;

	asm ("syscall" : "=a" (ret) :
	     "0" (__NR_getrandom), "D" (buffer), "S" (len), "d" (flags) :
	     "rcx", "r11", "memory");

	return ret;
}

#define __vdso_rng_data (VVAR(_vdso_rng_data))

static __always_inline const struct vdso_rng_data *__arch_get_vdso_rng_data(void)
{
	if (IS_ENABLED(CONFIG_TIME_NS) && __vdso_data->clock_mode == VDSO_CLOCKMODE_TIMENS)
		return (void *)&__vdso_rng_data + ((void *)&__timens_vdso_data - (void *)&__vdso_data);
	return &__vdso_rng_data;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
