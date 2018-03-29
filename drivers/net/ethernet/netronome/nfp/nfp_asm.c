/*
 * Copyright (C) 2016-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "nfp_asm.h"

const struct cmd_tgt_act cmd_tgt_act[__CMD_TGT_MAP_SIZE] = {
	[CMD_TGT_WRITE8_SWAP] =		{ 0x02, 0x42 },
	[CMD_TGT_WRITE32_SWAP] =	{ 0x02, 0x5f },
	[CMD_TGT_READ8] =		{ 0x01, 0x43 },
	[CMD_TGT_READ32] =		{ 0x00, 0x5c },
	[CMD_TGT_READ32_LE] =		{ 0x01, 0x5c },
	[CMD_TGT_READ32_SWAP] =		{ 0x02, 0x5c },
	[CMD_TGT_READ_LE] =		{ 0x01, 0x40 },
	[CMD_TGT_READ_SWAP_LE] =	{ 0x03, 0x40 },
	[CMD_TGT_ADD_IMM] =		{ 0x02, 0x47 },
};

static bool unreg_is_imm(u16 reg)
{
	return (reg & UR_REG_IMM) == UR_REG_IMM;
}

u16 br_get_offset(u64 instr)
{
	u16 addr_lo, addr_hi;

	addr_lo = FIELD_GET(OP_BR_ADDR_LO, instr);
	addr_hi = FIELD_GET(OP_BR_ADDR_HI, instr);

	return (addr_hi * ((OP_BR_ADDR_LO >> __bf_shf(OP_BR_ADDR_LO)) + 1)) |
		addr_lo;
}

void br_set_offset(u64 *instr, u16 offset)
{
	u16 addr_lo, addr_hi;

	addr_lo = offset & (OP_BR_ADDR_LO >> __bf_shf(OP_BR_ADDR_LO));
	addr_hi = offset != addr_lo;
	*instr &= ~(OP_BR_ADDR_HI | OP_BR_ADDR_LO);
	*instr |= FIELD_PREP(OP_BR_ADDR_HI, addr_hi);
	*instr |= FIELD_PREP(OP_BR_ADDR_LO, addr_lo);
}

void br_add_offset(u64 *instr, u16 offset)
{
	u16 addr;

	addr = br_get_offset(*instr);
	br_set_offset(instr, addr + offset);
}

static bool immed_can_modify(u64 instr)
{
	if (FIELD_GET(OP_IMMED_INV, instr) ||
	    FIELD_GET(OP_IMMED_SHIFT, instr) ||
	    FIELD_GET(OP_IMMED_WIDTH, instr) != IMMED_WIDTH_ALL) {
		pr_err("Can't decode/encode immed!\n");
		return false;
	}
	return true;
}

u16 immed_get_value(u64 instr)
{
	u16 reg;

	if (!immed_can_modify(instr))
		return 0;

	reg = FIELD_GET(OP_IMMED_A_SRC, instr);
	if (!unreg_is_imm(reg))
		reg = FIELD_GET(OP_IMMED_B_SRC, instr);

	return (reg & 0xff) | FIELD_GET(OP_IMMED_IMM, instr) << 8;
}

void immed_set_value(u64 *instr, u16 immed)
{
	if (!immed_can_modify(*instr))
		return;

	if (unreg_is_imm(FIELD_GET(OP_IMMED_A_SRC, *instr))) {
		*instr &= ~FIELD_PREP(OP_IMMED_A_SRC, 0xff);
		*instr |= FIELD_PREP(OP_IMMED_A_SRC, immed & 0xff);
	} else {
		*instr &= ~FIELD_PREP(OP_IMMED_B_SRC, 0xff);
		*instr |= FIELD_PREP(OP_IMMED_B_SRC, immed & 0xff);
	}

	*instr &= ~OP_IMMED_IMM;
	*instr |= FIELD_PREP(OP_IMMED_IMM, immed >> 8);
}

void immed_add_value(u64 *instr, u16 offset)
{
	u16 val;

	if (!immed_can_modify(*instr))
		return;

	val = immed_get_value(*instr);
	immed_set_value(instr, val + offset);
}

static u16 nfp_swreg_to_unreg(swreg reg, bool is_dst)
{
	bool lm_id, lm_dec = false;
	u16 val = swreg_value(reg);

	switch (swreg_type(reg)) {
	case NN_REG_GPR_A:
	case NN_REG_GPR_B:
	case NN_REG_GPR_BOTH:
		return val;
	case NN_REG_NNR:
		return UR_REG_NN | val;
	case NN_REG_XFER:
		return UR_REG_XFR | val;
	case NN_REG_LMEM:
		lm_id = swreg_lm_idx(reg);

		switch (swreg_lm_mode(reg)) {
		case NN_LM_MOD_NONE:
			if (val & ~UR_REG_LM_IDX_MAX) {
				pr_err("LM offset too large\n");
				return 0;
			}
			return UR_REG_LM | FIELD_PREP(UR_REG_LM_IDX, lm_id) |
				val;
		case NN_LM_MOD_DEC:
			lm_dec = true;
			/* fall through */
		case NN_LM_MOD_INC:
			if (val) {
				pr_err("LM offset in inc/dev mode\n");
				return 0;
			}
			return UR_REG_LM | UR_REG_LM_POST_MOD |
				FIELD_PREP(UR_REG_LM_IDX, lm_id) |
				FIELD_PREP(UR_REG_LM_POST_MOD_DEC, lm_dec);
		default:
			pr_err("bad LM mode for unrestricted operands %d\n",
			       swreg_lm_mode(reg));
			return 0;
		}
	case NN_REG_IMM:
		if (val & ~0xff) {
			pr_err("immediate too large\n");
			return 0;
		}
		return UR_REG_IMM_encode(val);
	case NN_REG_NONE:
		return is_dst ? UR_REG_NO_DST : REG_NONE;
	}

	pr_err("unrecognized reg encoding %08x\n", reg);
	return 0;
}

int swreg_to_unrestricted(swreg dst, swreg lreg, swreg rreg,
			  struct nfp_insn_ur_regs *reg)
{
	memset(reg, 0, sizeof(*reg));

	/* Decode destination */
	if (swreg_type(dst) == NN_REG_IMM)
		return -EFAULT;

	if (swreg_type(dst) == NN_REG_GPR_B)
		reg->dst_ab = ALU_DST_B;
	if (swreg_type(dst) == NN_REG_GPR_BOTH)
		reg->wr_both = true;
	reg->dst = nfp_swreg_to_unreg(dst, true);

	/* Decode source operands */
	if (swreg_type(lreg) == swreg_type(rreg) &&
	    swreg_type(lreg) != NN_REG_NONE)
		return -EFAULT;

	if (swreg_type(lreg) == NN_REG_GPR_B ||
	    swreg_type(rreg) == NN_REG_GPR_A) {
		reg->areg = nfp_swreg_to_unreg(rreg, false);
		reg->breg = nfp_swreg_to_unreg(lreg, false);
		reg->swap = true;
	} else {
		reg->areg = nfp_swreg_to_unreg(lreg, false);
		reg->breg = nfp_swreg_to_unreg(rreg, false);
	}

	reg->dst_lmextn = swreg_lmextn(dst);
	reg->src_lmextn = swreg_lmextn(lreg) | swreg_lmextn(rreg);

	return 0;
}

static u16 nfp_swreg_to_rereg(swreg reg, bool is_dst, bool has_imm8, bool *i8)
{
	u16 val = swreg_value(reg);
	bool lm_id;

	switch (swreg_type(reg)) {
	case NN_REG_GPR_A:
	case NN_REG_GPR_B:
	case NN_REG_GPR_BOTH:
		return val;
	case NN_REG_XFER:
		return RE_REG_XFR | val;
	case NN_REG_LMEM:
		lm_id = swreg_lm_idx(reg);

		if (swreg_lm_mode(reg) != NN_LM_MOD_NONE) {
			pr_err("bad LM mode for restricted operands %d\n",
			       swreg_lm_mode(reg));
			return 0;
		}

		if (val & ~RE_REG_LM_IDX_MAX) {
			pr_err("LM offset too large\n");
			return 0;
		}

		return RE_REG_LM | FIELD_PREP(RE_REG_LM_IDX, lm_id) | val;
	case NN_REG_IMM:
		if (val & ~(0x7f | has_imm8 << 7)) {
			pr_err("immediate too large\n");
			return 0;
		}
		*i8 = val & 0x80;
		return RE_REG_IMM_encode(val & 0x7f);
	case NN_REG_NONE:
		return is_dst ? RE_REG_NO_DST : REG_NONE;
	case NN_REG_NNR:
		pr_err("NNRs used with restricted encoding\n");
		return 0;
	}

	pr_err("unrecognized reg encoding\n");
	return 0;
}

int swreg_to_restricted(swreg dst, swreg lreg, swreg rreg,
			struct nfp_insn_re_regs *reg, bool has_imm8)
{
	memset(reg, 0, sizeof(*reg));

	/* Decode destination */
	if (swreg_type(dst) == NN_REG_IMM)
		return -EFAULT;

	if (swreg_type(dst) == NN_REG_GPR_B)
		reg->dst_ab = ALU_DST_B;
	if (swreg_type(dst) == NN_REG_GPR_BOTH)
		reg->wr_both = true;
	reg->dst = nfp_swreg_to_rereg(dst, true, false, NULL);

	/* Decode source operands */
	if (swreg_type(lreg) == swreg_type(rreg) &&
	    swreg_type(lreg) != NN_REG_NONE)
		return -EFAULT;

	if (swreg_type(lreg) == NN_REG_GPR_B ||
	    swreg_type(rreg) == NN_REG_GPR_A) {
		reg->areg = nfp_swreg_to_rereg(rreg, false, has_imm8, &reg->i8);
		reg->breg = nfp_swreg_to_rereg(lreg, false, has_imm8, &reg->i8);
		reg->swap = true;
	} else {
		reg->areg = nfp_swreg_to_rereg(lreg, false, has_imm8, &reg->i8);
		reg->breg = nfp_swreg_to_rereg(rreg, false, has_imm8, &reg->i8);
	}

	reg->dst_lmextn = swreg_lmextn(dst);
	reg->src_lmextn = swreg_lmextn(lreg) | swreg_lmextn(rreg);

	return 0;
}

#define NFP_USTORE_ECC_POLY_WORDS		7
#define NFP_USTORE_OP_BITS			45

static const u64 nfp_ustore_ecc_polynomials[NFP_USTORE_ECC_POLY_WORDS] = {
	0x0ff800007fffULL,
	0x11f801ff801fULL,
	0x1e387e0781e1ULL,
	0x17cb8e388e22ULL,
	0x1af5b2c93244ULL,
	0x1f56d5525488ULL,
	0x0daf69a46910ULL,
};

static bool parity(u64 value)
{
	return hweight64(value) & 1;
}

int nfp_ustore_check_valid_no_ecc(u64 insn)
{
	if (insn & ~GENMASK_ULL(NFP_USTORE_OP_BITS, 0))
		return -EINVAL;

	return 0;
}

u64 nfp_ustore_calc_ecc_insn(u64 insn)
{
	u8 ecc = 0;
	int i;

	for (i = 0; i < NFP_USTORE_ECC_POLY_WORDS; i++)
		ecc |= parity(nfp_ustore_ecc_polynomials[i] & insn) << i;

	return insn | (u64)ecc << NFP_USTORE_OP_BITS;
}
