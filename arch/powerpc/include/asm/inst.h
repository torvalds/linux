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

#endif /* _ASM_POWERPC_INST_H */
