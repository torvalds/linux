/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_XWREG_H
#define __ASM_XWREG_H

#include <asm/types.h>

#define __xwreg_t_x		u64
#define __xwreg_t_w		u32
#define xwreg_t(xw)		__xwreg_t_##xw

/*
 * Zero extend 'v' from 'sz' bits (8/16/32/64) to fill an X or W register.
 */
#define xwreg_zero_extend(v, xw, sz)	((xwreg_t(xw))(u##sz)(v))

#endif /* __ASM_XWREG_H */
