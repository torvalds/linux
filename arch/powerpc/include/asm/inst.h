/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

/*
 * Instruction data type for POWER
 */

struct ppc_inst {
	u32 val;
} __packed;

#define ppc_inst(x) ((struct ppc_inst){ .val = x })

static inline u32 ppc_inst_val(struct ppc_inst x)
{
	return x.val;
}

static inline int ppc_inst_primary_opcode(struct ppc_inst x)
{
	return ppc_inst_val(x) >> 26;
}

static inline struct ppc_inst ppc_inst_swab(struct ppc_inst x)
{
	return ppc_inst(swab32(ppc_inst_val(x)));
}

static inline struct ppc_inst ppc_inst_read(const struct ppc_inst *ptr)
{
	return *ptr;
}

static inline bool ppc_inst_equal(struct ppc_inst x, struct ppc_inst y)
{
	return ppc_inst_val(x) == ppc_inst_val(y);
}

#endif /* _ASM_POWERPC_INST_H */
