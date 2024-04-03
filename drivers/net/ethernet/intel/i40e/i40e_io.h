/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Intel Corporation. */

#ifndef _I40E_IO_H_
#define _I40E_IO_H_

/* get readq/writeq support for 32 bit kernels, use the low-first version */
#include <linux/io-64-nonatomic-lo-hi.h>

#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))

#define rd64(a, reg)		readq((a)->hw_addr + (reg))
#define i40e_flush(a)		readl((a)->hw_addr + I40E_GLGEN_STAT)

#endif /* _I40E_IO_H_ */
