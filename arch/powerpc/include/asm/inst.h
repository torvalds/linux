/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

/*
 * Instruction data type for POWER
 */

#define ppc_inst(x) (x)

static inline u32 ppc_inst_val(u32 x)
{
	return x;
}

static inline int ppc_inst_primary_opcode(u32 x)
{
	return ppc_inst_val(x) >> 26;
}

static inline u32 ppc_inst_swab(u32 x)
{
	return ppc_inst(swab32(ppc_inst_val(x)));
}

static inline bool ppc_inst_equal(u32 x, u32 y)
{
	return x == y;
}

#endif /* _ASM_POWERPC_INST_H */
