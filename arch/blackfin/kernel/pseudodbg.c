/* The fake debug assert instructions
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>

const char * const greg_names[] = {
	"R0",    "R1",      "R2",     "R3",    "R4",    "R5",    "R6",     "R7",
	"P0",    "P1",      "P2",     "P3",    "P4",    "P5",    "SP",     "FP",
	"I0",    "I1",      "I2",     "I3",    "M0",    "M1",    "M2",     "M3",
	"B0",    "B1",      "B2",     "B3",    "L0",    "L1",    "L2",     "L3",
	"A0.X",  "A0.W",    "A1.X",   "A1.W",  "<res>", "<res>", "ASTAT",  "RETS",
	"<res>", "<res>",   "<res>",  "<res>", "<res>", "<res>", "<res>",  "<res>",
	"LC0",   "LT0",     "LB0",    "LC1",   "LT1",   "LB1",   "CYCLES", "CYCLES2",
	"USP",   "SEQSTAT", "SYSCFG", "RETI",  "RETX",  "RETN",  "RETE",   "EMUDAT",
};

static const char *get_allreg_name(int grp, int reg)
{
	return greg_names[(grp << 3) | reg];
}

/*
 * Unfortunately, the pt_regs structure is not laid out the same way as the
 * hardware register file, so we need to do some fix ups.
 *
 * CYCLES is not stored in the pt_regs structure - so, we just read it from
 * the hardware.
 *
 * Don't support:
 *  - All reserved registers
 *  - All in group 7 are (supervisors only)
 */

static bool fix_up_reg(struct pt_regs *fp, long *value, int grp, int reg)
{
	long *val = &fp->r0;
	unsigned long tmp;

	/* Only do Dregs and Pregs for now */
	if (grp == 5 ||
	   (grp == 4 && (reg == 4 || reg == 5)) ||
	   (grp == 7))
		return false;

	if (grp == 0 || (grp == 1 && reg < 6))
		val -= (reg + 8 * grp);
	else if (grp == 1 && reg == 6)
		val = &fp->usp;
	else if (grp == 1 && reg == 7)
		val = &fp->fp;
	else if (grp == 2) {
		val = &fp->i0;
		val -= reg;
	} else if (grp == 3 && reg >= 4) {
		val = &fp->l0;
		val -= (reg - 4);
	} else if (grp == 3 && reg < 4) {
		val = &fp->b0;
		val -= reg;
	} else if (grp == 4 && reg < 4) {
		val = &fp->a0x;
		val -= reg;
	} else if (grp == 4 && reg == 6)
		val = &fp->astat;
	else if (grp == 4 && reg == 7)
		val = &fp->rets;
	else if (grp == 6 && reg < 6) {
		val = &fp->lc0;
		val -= reg;
	} else if (grp == 6 && reg == 6) {
		__asm__ __volatile__("%0 = cycles;\n" : "=d"(tmp));
		val = &tmp;
	} else if (grp == 6 && reg == 7) {
		__asm__ __volatile__("%0 = cycles2;\n" : "=d"(tmp));
		val = &tmp;
	}

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
			pr_notice("DBGA (%s.L,0x%x) failure, got 0x%x\n",
				get_allreg_name(grp, regtest),
				expected, (unsigned int)(value & 0xFFFF));
			return false;
		}

	} else if (dbgop == 1 || dbgop == 3) {
		/* DBGA ( regs_hi , uimm16 ) */
		/* DBGAH ( regs , uimm16 ) */
		if (expected != ((value >> 16) & 0xFFFF)) {
			pr_notice("DBGA (%s.H,0x%x) failure, got 0x%x\n",
				get_allreg_name(grp, regtest),
				expected, (unsigned int)((value >> 16) & 0xFFFF));
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
	long value, value1;

	if ((opcode & 0xFF000000) != PseudoDbg_opcode)
		return false;

	opcode >>= 16;
	grp = ((opcode >> PseudoDbg_grp_bits) & PseudoDbg_reg_mask);
	fn  = ((opcode >> PseudoDbg_fn_bits)  & PseudoDbg_fn_mask);
	reg = ((opcode >> PseudoDbg_reg_bits) & PseudoDbg_reg_mask);

	if (fn == 3 && (reg == 0 || reg == 1)) {
		if (!fix_up_reg(fp, &value, 4, 2 * reg))
			return false;
		if (!fix_up_reg(fp, &value1, 4, 2 * reg + 1))
			return false;

		pr_notice("DBG A%i = %02lx%08lx\n", reg, value & 0xFF, value1);
		fp->pc += 2;
		return true;

	} else if (fn == 0) {
		if (!fix_up_reg(fp, &value, grp, reg))
			return false;

		pr_notice("DBG %s = %08lx\n", get_allreg_name(grp, reg), value);
		fp->pc += 2;
		return true;
	}

	return false;
}
