/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TILE_DIV64_H
#define _ASM_TILE_DIV64_H

#include <linux/types.h>

#ifdef __tilegx__
static inline u64 mul_u32_u32(u32 a, u32 b)
{
	return __insn_mul_lu_lu(a, b);
}
#define mul_u32_u32 mul_u32_u32
#endif

#include <asm-generic/div64.h>

#endif /* _ASM_TILE_DIV64_H */
