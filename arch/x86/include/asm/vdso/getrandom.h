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

/**
 * __arch_chacha20_blocks_nostack - Generate ChaCha20 stream without using the stack.
 * @dst_bytes:	Destination buffer to hold @nblocks * 64 bytes of output.
 * @key:	32-byte input key.
 * @counter:	8-byte counter, read on input and updated on return.
 * @nblocks:	Number of blocks to generate.
 *
 * Generates a given positive number of blocks of ChaCha20 output with nonce=0, and does not write
 * to any stack or memory outside of the parameters passed to it, in order to mitigate stack data
 * leaking into forked child processes.
 */
extern void __arch_chacha20_blocks_nostack(u8 *dst_bytes, const u32 *key, u32 *counter, size_t nblocks);

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETRANDOM_H */
