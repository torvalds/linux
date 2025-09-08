/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __MATH_SUPPORT_H
#define __MATH_SUPPORT_H

/* Override the definition of max/min from Linux kernel */
#include <linux/minmax.h>

#define CEIL_DIV(a, b)       (((b) != 0) ? ((a) + (b) - 1) / (b) : 0)
#define CEIL_MUL(a, b)       (CEIL_DIV(a, b) * (b))
#define CEIL_SHIFT(a, b)     (((a) + (1 << (b)) - 1) >> (b))

/*
 * For SP and ISP, SDK provides the definition of OP_std_modadd.
 * We need it only for host
 */
#define OP_std_modadd(base, offset, size) ((base + offset) % (size))

#endif /* __MATH_SUPPORT_H */
