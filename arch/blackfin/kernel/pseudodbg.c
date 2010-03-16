/* The fake debug assert instructions
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>

#define PseudoDbg_Assert_opcode         0xf0000000
#define PseudoDbg_Assert_expected_bits  0
#define PseudoDbg_Assert_expected_mask  0xffff
#define PseudoDbg_Assert_regtest_bits   16
#define PseudoDbg_Assert_regtest_mask   0x7
#define PseudoDbg_Assert_grp_bits       19
#define PseudoDbg_Assert_grp_mask       0x7
#define PseudoDbg_Assert_dbgop_bits     22
#define PseudoDbg_Assert_dbgop_mask     0x3
#define PseudoDbg_Assert_dontcare_bits  24
#define PseudoDbg_Assert_dontcare_mask  0x7
#define PseudoDbg_Assert_code_bits      27
#define PseudoDbg_Assert_code_mask      0x1f

bool execute_pseudodbg_assert(struct pt_regs *fp, unsigned int opcode)
{
	int expected = ((opcode >> PseudoDbg_Assert_expected_bits) & PseudoDbg_Assert_expected_mask);
	int dbgop    = ((opcode >> (PseudoDbg_Assert_dbgop_bits)) & PseudoDbg_Assert_dbgop_mask);
	int grp      = ((opcode >> (PseudoDbg_Assert_grp_bits)) & PseudoDbg_Assert_grp_mask);
	int regtest  = ((opcode >> (PseudoDbg_Assert_regtest_bits)) & PseudoDbg_Assert_regtest_mask);
	long *value = &fp->r0;

	if ((opcode & 0xFF000000) != PseudoDbg_Assert_opcode)
		return false;

	/* Only do Dregs and Pregs for now */
	if (grp > 1)
		return false;

	/*
	 * Unfortunately, the pt_regs structure is not laid out the same way as the
	 * hardware register file, so we need to do some fix ups.
	 */
	if (grp == 0 || (grp == 1 && regtest < 6))
		value -= (regtest + 8 * grp);
	else if (grp == 1 && regtest == 6)
		value = &fp->usp;
	else if (grp == 1 && regtest == 7)
		value = &fp->fp;

	if (dbgop == 0 || dbgop == 2) {
		/* DBGA ( regs_lo , uimm16 ) */
		/* DBGAL ( regs , uimm16 ) */
		if (expected != (*value & 0xFFFF)) {
			pr_notice("DBGA (%s%i.L,0x%x) failure, got 0x%x\n", grp ? "P" : "R",
				regtest, expected, (unsigned int)(*value & 0xFFFF));
			return false;
		}

	} else if (dbgop == 1 || dbgop == 3) {
		/* DBGA ( regs_hi , uimm16 ) */
		/* DBGAH ( regs , uimm16 ) */
		if (expected != ((*value >> 16) & 0xFFFF)) {
			pr_notice("DBGA (%s%i.H,0x%x) failure, got 0x%x\n", grp ? "P" : "R",
				regtest, expected, (unsigned int)((*value >> 16) & 0xFFFF));
			return false;
		}
	}

	fp->pc += 4;
	return true;
}
