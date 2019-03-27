/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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


#ifndef DOORBELL_H
#define DOORBELL_H

#include <stdint.h>
#include "mlx5.h"

#if SIZEOF_LONG == 8

#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define MLX5_PAIR_TO_64(val) ((uint64_t) val[1] << 32 | val[0])
#elif __BYTE_ORDER == __BIG_ENDIAN
#  define MLX5_PAIR_TO_64(val) ((uint64_t) val[0] << 32 | val[1])
#else
#  error __BYTE_ORDER not defined
#endif

static inline void mlx5_write64(uint32_t val[2], void *dest, struct mlx5_spinlock *lock)
{
	*(volatile uint64_t *)dest = MLX5_PAIR_TO_64(val);
}

#else

static inline void mlx5_write64(uint32_t val[2], void *dest, struct mlx5_spinlock *lock)
{
	mlx5_spin_lock(lock);
	*(volatile uint32_t *)dest		= val[0];
	*(volatile uint32_t *)(dest + 4)	= val[1];
	mlx5_spin_unlock(lock);
}

#endif

#endif /* DOORBELL_H */
