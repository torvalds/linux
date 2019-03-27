/*
 * Copyright (c) 2000, 2011 Mellanox Technology Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <errno.h>
#include "mlx5.h"

/* Only ia64 requires this */
#ifdef __ia64__
#define MLX5_SHM_ADDR ((void *)0x8000000000000000UL)
#define MLX5_SHMAT_FLAGS (SHM_RND)
#else
#define MLX5_SHM_ADDR NULL
#define MLX5_SHMAT_FLAGS 0
#endif

#define BITS_PER_LONG		(8 * sizeof(long))
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_LONG)

#ifndef HPAGE_SIZE
#define HPAGE_SIZE		(2UL * 1024 * 1024)
#endif

#define MLX5_SHM_LENGTH		HPAGE_SIZE
#define MLX5_Q_CHUNK_SIZE	32768
#define MLX5_SHM_NUM_REGION	64

static inline unsigned long mlx5_ffz(uint32_t word)
{
	return __builtin_ffs(~word) - 1;
}

static inline uint32_t mlx5_find_first_zero_bit(const unsigned long *addr,
					 uint32_t size)
{
	const unsigned long *p = addr;
	uint32_t result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = *(p++);
		if (~tmp)
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) | (~0UL << size);
	if (tmp == (uint32_t)~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found:
	return result + mlx5_ffz(tmp);
}

static inline void mlx5_set_bit(unsigned int nr, unsigned long *addr)
{
	addr[(nr / BITS_PER_LONG)] |= (1 << (nr % BITS_PER_LONG));
}

static inline void mlx5_clear_bit(unsigned int nr,  unsigned long *addr)
{
	addr[(nr / BITS_PER_LONG)] &= ~(1 << (nr % BITS_PER_LONG));
}

static inline int mlx5_test_bit(unsigned int nr, const unsigned long *addr)
{
	return !!(addr[(nr / BITS_PER_LONG)] & (1 <<  (nr % BITS_PER_LONG)));
}

#endif
