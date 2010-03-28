/* The fake debug assert instructions
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>

/*
 * Unfortunately, the pt_regs structure is not laid out the same way as the
 * hardware register file, so we need to do some fix ups.
 */
static bool fix_up_reg(struct pt_regs *fp, long *value, int grp, int reg)
{
	long *val = &fp->r0;

	/* Only do Dregs and Pregs for now */
	if (grp > 1)
		return false;

	if (grp == 0 || (grp == 1 && reg < 6))
		val -= (reg + 8 * grp);
	else if (grp == 1 && reg == 6)
		val = &fp->usp;
	else if (grp == 1 && reg == 7)
		val = &fp->fp;

	*value = *val;
	return true;

}

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

/*
 * DBGA - debug assert
 */
bool execute_pseudodbg_assert(struct pt_regs *fp, unsigned int opcode)
{
	int expected = ((opcode >> PseudoDbg_Assert_expected_bits) & PseudoDbg_Assert_expected_mask);
	int dbgop    = ((opcode >> (PseudoDbg_Assert_dbgop_bits)) & PseudoDbg_Assert_dbgop_mask);
	int grp      = ((opcode >> (PseudoDbg_Assert_grp_bits)) & PseudoDbg_Assert_grp_mask);
	int regtest  = ((opcode >> (PseudoDbg_Assert_regtest_bits)) & PseudoDbg_Assert_regtest_mask);
	long value;

	if ((opcode & 0xFF000000) != PseudoDbg_Assert_opcode)
		return false;

	if (!fix_up_reg(fp, &value, grp, regtest))
		return false;

	if (dbgop == 0 || dbgop == 2) {
		/* DBGA ( regs_lo , uimm16 ) */
		/* DBGAL ( regs , uimm16 ) */
		if (expected != (value & 0xFFFF)) {
			pr_notice("DBGA (%s%i.L,0x%x) failure, got 0x%x\n", grp ? "P" : "R",
				regtest, expected, (unsigned int)(value & 0xFFFF));
			return false;
		}

	} else if (dbgop == 1 || dbgop == 3) {
		/* DBGA ( regs_hi , uimm16 ) */
		/* DBGAH ( regs , uimm16 ) */
		if (expected != ((value >> 16) & 0xFFFF)) {
			pr_notice("DBGA (%s%i.H,0x%x) failure, got 0x%x\n", grp ? "P" : "R",
				regtest, expected, (unsigned int)((value >> 16) & 0xFFFF));
			return false;
		}
	}

	fp->pc += 4;
	return true;
}

#define PseudoDbg_opcode        0xf8000000
#define PseudoDbg_reg_bits      0
#define PseudoDbg_reg_mask      0x7
#define PseudoDbg_grp_bits      3
#define PseudoDbg_grp_mask      0x7
#define PseudoDbg_fn_bits       6
#define PseudoDbg_fn_mask       0x3
#define PseudoDbg_code_bits     8
#define PseudoDbg_code_mask     0xff

/*
 * DBG - debug (dump a register value out)
 */
bool execute_pseudodbg(struct pt_regs *fp, unsigned int opcode)
{
	int grp, fn, reg;
	long value;

	if ((opcode & 0xFF000000) != PseudoDbg_opcode)
		return false;

	opcode >>= 16;
	grp = ((opcode >> PseudoDbg_grp_bits) & PseudoDbg_reg_mask);
	fn  = ((opcode >> PseudoDbg_fn_bits)  & PseudoDbg_fn_mask);
	reg = ((opcode >> PseudoDbg_reg_bits) & PseudoDbg_reg_mask);

	if (!fix_up_reg(fp, &value, grp, reg))
		return false;

	pr_notice("DBG %s%d = %08lx\n", grp ? "P" : "R", reg, value);

	fp->pc += 2;
	return true;
}
