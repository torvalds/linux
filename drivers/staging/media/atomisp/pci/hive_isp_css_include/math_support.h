/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MATH_SUPPORT_H
#define __MATH_SUPPORT_H

/* Override the definition of max/min from Linux kernel */
#include <linux/minmax.h>

/* force a value to a lower even value */
#define EVEN_FLOOR(x)        ((x) & ~1)

#define CEIL_DIV(a, b)       (((b) != 0) ? ((a) + (b) - 1) / (b) : 0)
#define CEIL_MUL(a, b)       (CEIL_DIV(a, b) * (b))
#define CEIL_MUL2(a, b)      (((a) + (b) - 1) & ~((b) - 1))
#define CEIL_SHIFT(a, b)     (((a) + (1 << (b)) - 1) >> (b))
#define CEIL_SHIFT_MUL(a, b) (CEIL_SHIFT(a, b) << (b))

#if !defined(PIPE_GENERATION)

#define ceil_div(a, b)		(CEIL_DIV(a, b))

#endif /* !defined(PIPE_GENERATION) */

/*
 * For SP and ISP, SDK provides the definition of OP_std_modadd.
 * We need it only for host
 */
#define OP_std_modadd(base, offset, size) ((base + offset) % (size))

#endif /* __MATH_SUPPORT_H */
