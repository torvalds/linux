/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */
#ifndef __ASM_VDSO_GETRANDOM_H
#define __ASM_VDSO_GETRANDOM_H

#ifndef __ASSEMBLER__

#include <asm/unistd.h>

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

extern struct vdso_rng_data vdso_rng_data
	__attribute__((visibility("hidden")));

static __always_inline const struct vdso_rng_data *__arch_get_vdso_rng_data(void)
{
	if (IS_ENABLED(CONFIG_TIME_NS) && __arch_get_vdso_data()->clock_mode == VDSO_CLOCKMODE_TIMENS)
		return (void *)&vdso_rng_data + ((void *)&timens_page - (void *)__arch_get_vdso_data());
	return &vdso_rng_data;
}

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
