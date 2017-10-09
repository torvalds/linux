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
	[CMD_TGT_WRITE8] =		{ 0x00, 0x42 },
	[CMD_TGT_READ8] =		{ 0x01, 0x43 },
	[CMD_TGT_READ_LE] =		{ 0x01, 0x40 },
	[CMD_TGT_READ_SWAP_LE] =	{ 0x03, 0x40 },
};

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
	if (swreg_type(lreg) == swreg_type(rreg))
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
	if (swreg_type(lreg) == swreg_type(rreg))
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

	return 0;
}
