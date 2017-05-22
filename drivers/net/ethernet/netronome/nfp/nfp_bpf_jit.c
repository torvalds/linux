/*
 * Copyright (C) 2016 Netronome Systems, Inc.
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

#define pr_fmt(fmt)	"NFP net bpf: " fmt

#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/pkt_cls.h>
#include <linux/unistd.h>

#include "nfp_asm.h"
#include "nfp_bpf.h"

/* --- NFP prog --- */
/* Foreach "multiple" entries macros provide pos and next<n> pointers.
 * It's safe to modify the next pointers (but not pos).
 */
#define nfp_for_each_insn_walk2(nfp_prog, pos, next)			\
	for (pos = list_first_entry(&(nfp_prog)->insns, typeof(*pos), l), \
	     next = list_next_entry(pos, l);			\
	     &(nfp_prog)->insns != &pos->l &&			\
	     &(nfp_prog)->insns != &next->l;			\
	     pos = nfp_meta_next(pos),				\
	     next = nfp_meta_next(pos))

#define nfp_for_each_insn_walk3(nfp_prog, pos, next, next2)		\
	for (pos = list_first_entry(&(nfp_prog)->insns, typeof(*pos), l), \
	     next = list_next_entry(pos, l),			\
	     next2 = list_next_entry(next, l);			\
	     &(nfp_prog)->insns != &pos->l &&			\
	     &(nfp_prog)->insns != &next->l &&			\
	     &(nfp_prog)->insns != &next2->l;			\
	     pos = nfp_meta_next(pos),				\
	     next = nfp_meta_next(pos),				\
	     next2 = nfp_meta_next(next))

static bool
nfp_meta_has_next(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return meta->l.next != &nfp_prog->insns;
}

static bool
nfp_meta_has_prev(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return meta->l.prev != &nfp_prog->insns;
}

static void nfp_prog_free(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *tmp;

	list_for_each_entry_safe(meta, tmp, &nfp_prog->insns, l) {
		list_del(&meta->l);
		kfree(meta);
	}
	kfree(nfp_prog);
}

static void nfp_prog_push(struct nfp_prog *nfp_prog, u64 insn)
{
	if (nfp_prog->__prog_alloc_len == nfp_prog->prog_len) {
		nfp_prog->error = -ENOSPC;
		return;
	}

	nfp_prog->prog[nfp_prog->prog_len] = insn;
	nfp_prog->prog_len++;
}

static unsigned int nfp_prog_current_offset(struct nfp_prog *nfp_prog)
{
	return nfp_prog->start_off + nfp_prog->prog_len;
}

static unsigned int
nfp_prog_offset_to_index(struct nfp_prog *nfp_prog, unsigned int offset)
{
	return offset - nfp_prog->start_off;
}

/* --- SW reg --- */
struct nfp_insn_ur_regs {
	enum alu_dst_ab dst_ab;
	u16 dst;
	u16 areg, breg;
	bool swap;
	bool wr_both;
};

struct nfp_insn_re_regs {
	enum alu_dst_ab dst_ab;
	u8 dst;
	u8 areg, breg;
	bool swap;
	bool wr_both;
	bool i8;
};

static u16 nfp_swreg_to_unreg(u32 swreg, bool is_dst)
{
	u16 val = FIELD_GET(NN_REG_VAL, swreg);

	switch (FIELD_GET(NN_REG_TYPE, swreg)) {
	case NN_REG_GPR_A:
	case NN_REG_GPR_B:
	case NN_REG_GPR_BOTH:
		return val;
	case NN_REG_NNR:
		return UR_REG_NN | val;
	case NN_REG_XFER:
		return UR_REG_XFR | val;
	case NN_REG_IMM:
		if (val & ~0xff) {
			pr_err("immediate too large\n");
			return 0;
		}
		return UR_REG_IMM_encode(val);
	case NN_REG_NONE:
		return is_dst ? UR_REG_NO_DST : REG_NONE;
	default:
		pr_err("unrecognized reg encoding %08x\n", swreg);
		return 0;
	}
}

static int
swreg_to_unrestricted(u32 dst, u32 lreg, u32 rreg, struct nfp_insn_ur_regs *reg)
{
	memset(reg, 0, sizeof(*reg));

	/* Decode destination */
	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_IMM)
		return -EFAULT;

	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_GPR_B)
		reg->dst_ab = ALU_DST_B;
	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_GPR_BOTH)
		reg->wr_both = true;
	reg->dst = nfp_swreg_to_unreg(dst, true);

	/* Decode source operands */
	if (FIELD_GET(NN_REG_TYPE, lreg) == FIELD_GET(NN_REG_TYPE, rreg))
		return -EFAULT;

	if (FIELD_GET(NN_REG_TYPE, lreg) == NN_REG_GPR_B ||
	    FIELD_GET(NN_REG_TYPE, rreg) == NN_REG_GPR_A) {
		reg->areg = nfp_swreg_to_unreg(rreg, false);
		reg->breg = nfp_swreg_to_unreg(lreg, false);
		reg->swap = true;
	} else {
		reg->areg = nfp_swreg_to_unreg(lreg, false);
		reg->breg = nfp_swreg_to_unreg(rreg, false);
	}

	return 0;
}

static u16 nfp_swreg_to_rereg(u32 swreg, bool is_dst, bool has_imm8, bool *i8)
{
	u16 val = FIELD_GET(NN_REG_VAL, swreg);

	switch (FIELD_GET(NN_REG_TYPE, swreg)) {
	case NN_REG_GPR_A:
	case NN_REG_GPR_B:
	case NN_REG_GPR_BOTH:
		return val;
	case NN_REG_XFER:
		return RE_REG_XFR | val;
	case NN_REG_IMM:
		if (val & ~(0x7f | has_imm8 << 7)) {
			pr_err("immediate too large\n");
			return 0;
		}
		*i8 = val & 0x80;
		return RE_REG_IMM_encode(val & 0x7f);
	case NN_REG_NONE:
		return is_dst ? RE_REG_NO_DST : REG_NONE;
	default:
		pr_err("unrecognized reg encoding\n");
		return 0;
	}
}

static int
swreg_to_restricted(u32 dst, u32 lreg, u32 rreg, struct nfp_insn_re_regs *reg,
		    bool has_imm8)
{
	memset(reg, 0, sizeof(*reg));

	/* Decode destination */
	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_IMM)
		return -EFAULT;

	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_GPR_B)
		reg->dst_ab = ALU_DST_B;
	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_GPR_BOTH)
		reg->wr_both = true;
	reg->dst = nfp_swreg_to_rereg(dst, true, false, NULL);

	/* Decode source operands */
	if (FIELD_GET(NN_REG_TYPE, lreg) == FIELD_GET(NN_REG_TYPE, rreg))
		return -EFAULT;

	if (FIELD_GET(NN_REG_TYPE, lreg) == NN_REG_GPR_B ||
	    FIELD_GET(NN_REG_TYPE, rreg) == NN_REG_GPR_A) {
		reg->areg = nfp_swreg_to_rereg(rreg, false, has_imm8, &reg->i8);
		reg->breg = nfp_swreg_to_rereg(lreg, false, has_imm8, &reg->i8);
		reg->swap = true;
	} else {
		reg->areg = nfp_swreg_to_rereg(lreg, false, has_imm8, &reg->i8);
		reg->breg = nfp_swreg_to_rereg(rreg, false, has_imm8, &reg->i8);
	}

	return 0;
}

/* --- Emitters --- */
static const struct cmd_tgt_act cmd_tgt_act[__CMD_TGT_MAP_SIZE] = {
	[CMD_TGT_WRITE8] =		{ 0x00, 0x42 },
	[CMD_TGT_READ8] =		{ 0x01, 0x43 },
	[CMD_TGT_READ_LE] =		{ 0x01, 0x40 },
	[CMD_TGT_READ_SWAP_LE] =	{ 0x03, 0x40 },
};

static void
__emit_cmd(struct nfp_prog *nfp_prog, enum cmd_tgt_map op,
	   u8 mode, u8 xfer, u8 areg, u8 breg, u8 size, bool sync)
{
	enum cmd_ctx_swap ctx;
	u64 insn;

	if (sync)
		ctx = CMD_CTX_SWAP;
	else
		ctx = CMD_CTX_NO_SWAP;

	insn =	FIELD_PREP(OP_CMD_A_SRC, areg) |
		FIELD_PREP(OP_CMD_CTX, ctx) |
		FIELD_PREP(OP_CMD_B_SRC, breg) |
		FIELD_PREP(OP_CMD_TOKEN, cmd_tgt_act[op].token) |
		FIELD_PREP(OP_CMD_XFER, xfer) |
		FIELD_PREP(OP_CMD_CNT, size) |
		FIELD_PREP(OP_CMD_SIG, sync) |
		FIELD_PREP(OP_CMD_TGT_CMD, cmd_tgt_act[op].tgt_cmd) |
		FIELD_PREP(OP_CMD_MODE, mode);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_cmd(struct nfp_prog *nfp_prog, enum cmd_tgt_map op,
	 u8 mode, u8 xfer, u32 lreg, u32 rreg, u8 size, bool sync)
{
	struct nfp_insn_re_regs reg;
	int err;

	err = swreg_to_restricted(reg_none(), lreg, rreg, &reg, false);
	if (err) {
		nfp_prog->error = err;
		return;
	}
	if (reg.swap) {
		pr_err("cmd can't swap arguments\n");
		nfp_prog->error = -EFAULT;
		return;
	}

	__emit_cmd(nfp_prog, op, mode, xfer, reg.areg, reg.breg, size, sync);
}

static void
__emit_br(struct nfp_prog *nfp_prog, enum br_mask mask, enum br_ev_pip ev_pip,
	  enum br_ctx_signal_state css, u16 addr, u8 defer)
{
	u16 addr_lo, addr_hi;
	u64 insn;

	addr_lo = addr & (OP_BR_ADDR_LO >> __bf_shf(OP_BR_ADDR_LO));
	addr_hi = addr != addr_lo;

	insn = OP_BR_BASE |
		FIELD_PREP(OP_BR_MASK, mask) |
		FIELD_PREP(OP_BR_EV_PIP, ev_pip) |
		FIELD_PREP(OP_BR_CSS, css) |
		FIELD_PREP(OP_BR_DEFBR, defer) |
		FIELD_PREP(OP_BR_ADDR_LO, addr_lo) |
		FIELD_PREP(OP_BR_ADDR_HI, addr_hi);

	nfp_prog_push(nfp_prog, insn);
}

static void emit_br_def(struct nfp_prog *nfp_prog, u16 addr, u8 defer)
{
	if (defer > 2) {
		pr_err("BUG: branch defer out of bounds %d\n", defer);
		nfp_prog->error = -EFAULT;
		return;
	}
	__emit_br(nfp_prog, BR_UNC, BR_EV_PIP_UNCOND, BR_CSS_NONE, addr, defer);
}

static void
emit_br(struct nfp_prog *nfp_prog, enum br_mask mask, u16 addr, u8 defer)
{
	__emit_br(nfp_prog, mask,
		  mask != BR_UNC ? BR_EV_PIP_COND : BR_EV_PIP_UNCOND,
		  BR_CSS_NONE, addr, defer);
}

static void
__emit_br_byte(struct nfp_prog *nfp_prog, u8 areg, u8 breg, bool imm8,
	       u8 byte, bool equal, u16 addr, u8 defer)
{
	u16 addr_lo, addr_hi;
	u64 insn;

	addr_lo = addr & (OP_BB_ADDR_LO >> __bf_shf(OP_BB_ADDR_LO));
	addr_hi = addr != addr_lo;

	insn = OP_BBYTE_BASE |
		FIELD_PREP(OP_BB_A_SRC, areg) |
		FIELD_PREP(OP_BB_BYTE, byte) |
		FIELD_PREP(OP_BB_B_SRC, breg) |
		FIELD_PREP(OP_BB_I8, imm8) |
		FIELD_PREP(OP_BB_EQ, equal) |
		FIELD_PREP(OP_BB_DEFBR, defer) |
		FIELD_PREP(OP_BB_ADDR_LO, addr_lo) |
		FIELD_PREP(OP_BB_ADDR_HI, addr_hi);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_br_byte_neq(struct nfp_prog *nfp_prog,
		 u32 dst, u8 imm, u8 byte, u16 addr, u8 defer)
{
	struct nfp_insn_re_regs reg;
	int err;

	err = swreg_to_restricted(reg_none(), dst, reg_imm(imm), &reg, true);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_br_byte(nfp_prog, reg.areg, reg.breg, reg.i8, byte, false, addr,
		       defer);
}

static void
__emit_immed(struct nfp_prog *nfp_prog, u16 areg, u16 breg, u16 imm_hi,
	     enum immed_width width, bool invert,
	     enum immed_shift shift, bool wr_both)
{
	u64 insn;

	insn = OP_IMMED_BASE |
		FIELD_PREP(OP_IMMED_A_SRC, areg) |
		FIELD_PREP(OP_IMMED_B_SRC, breg) |
		FIELD_PREP(OP_IMMED_IMM, imm_hi) |
		FIELD_PREP(OP_IMMED_WIDTH, width) |
		FIELD_PREP(OP_IMMED_INV, invert) |
		FIELD_PREP(OP_IMMED_SHIFT, shift) |
		FIELD_PREP(OP_IMMED_WR_AB, wr_both);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_immed(struct nfp_prog *nfp_prog, u32 dst, u16 imm,
	   enum immed_width width, bool invert, enum immed_shift shift)
{
	struct nfp_insn_ur_regs reg;
	int err;

	if (FIELD_GET(NN_REG_TYPE, dst) == NN_REG_IMM) {
		nfp_prog->error = -EFAULT;
		return;
	}

	err = swreg_to_unrestricted(dst, dst, reg_imm(imm & 0xff), &reg);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_immed(nfp_prog, reg.areg, reg.breg, imm >> 8, width,
		     invert, shift, reg.wr_both);
}

static void
__emit_shf(struct nfp_prog *nfp_prog, u16 dst, enum alu_dst_ab dst_ab,
	   enum shf_sc sc, u8 shift,
	   u16 areg, enum shf_op op, u16 breg, bool i8, bool sw, bool wr_both)
{
	u64 insn;

	if (!FIELD_FIT(OP_SHF_SHIFT, shift)) {
		nfp_prog->error = -EFAULT;
		return;
	}

	if (sc == SHF_SC_L_SHF)
		shift = 32 - shift;

	insn = OP_SHF_BASE |
		FIELD_PREP(OP_SHF_A_SRC, areg) |
		FIELD_PREP(OP_SHF_SC, sc) |
		FIELD_PREP(OP_SHF_B_SRC, breg) |
		FIELD_PREP(OP_SHF_I8, i8) |
		FIELD_PREP(OP_SHF_SW, sw) |
		FIELD_PREP(OP_SHF_DST, dst) |
		FIELD_PREP(OP_SHF_SHIFT, shift) |
		FIELD_PREP(OP_SHF_OP, op) |
		FIELD_PREP(OP_SHF_DST_AB, dst_ab) |
		FIELD_PREP(OP_SHF_WR_AB, wr_both);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_shf(struct nfp_prog *nfp_prog, u32 dst, u32 lreg, enum shf_op op, u32 rreg,
	 enum shf_sc sc, u8 shift)
{
	struct nfp_insn_re_regs reg;
	int err;

	err = swreg_to_restricted(dst, lreg, rreg, &reg, true);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_shf(nfp_prog, reg.dst, reg.dst_ab, sc, shift,
		   reg.areg, op, reg.breg, reg.i8, reg.swap, reg.wr_both);
}

static void
__emit_alu(struct nfp_prog *nfp_prog, u16 dst, enum alu_dst_ab dst_ab,
	   u16 areg, enum alu_op op, u16 breg, bool swap, bool wr_both)
{
	u64 insn;

	insn = OP_ALU_BASE |
		FIELD_PREP(OP_ALU_A_SRC, areg) |
		FIELD_PREP(OP_ALU_B_SRC, breg) |
		FIELD_PREP(OP_ALU_DST, dst) |
		FIELD_PREP(OP_ALU_SW, swap) |
		FIELD_PREP(OP_ALU_OP, op) |
		FIELD_PREP(OP_ALU_DST_AB, dst_ab) |
		FIELD_PREP(OP_ALU_WR_AB, wr_both);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_alu(struct nfp_prog *nfp_prog, u32 dst, u32 lreg, enum alu_op op, u32 rreg)
{
	struct nfp_insn_ur_regs reg;
	int err;

	err = swreg_to_unrestricted(dst, lreg, rreg, &reg);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_alu(nfp_prog, reg.dst, reg.dst_ab,
		   reg.areg, op, reg.breg, reg.swap, reg.wr_both);
}

static void
__emit_ld_field(struct nfp_prog *nfp_prog, enum shf_sc sc,
		u8 areg, u8 bmask, u8 breg, u8 shift, bool imm8,
		bool zero, bool swap, bool wr_both)
{
	u64 insn;

	insn = OP_LDF_BASE |
		FIELD_PREP(OP_LDF_A_SRC, areg) |
		FIELD_PREP(OP_LDF_SC, sc) |
		FIELD_PREP(OP_LDF_B_SRC, breg) |
		FIELD_PREP(OP_LDF_I8, imm8) |
		FIELD_PREP(OP_LDF_SW, swap) |
		FIELD_PREP(OP_LDF_ZF, zero) |
		FIELD_PREP(OP_LDF_BMASK, bmask) |
		FIELD_PREP(OP_LDF_SHF, shift) |
		FIELD_PREP(OP_LDF_WR_AB, wr_both);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_ld_field_any(struct nfp_prog *nfp_prog, enum shf_sc sc, u8 shift,
		  u32 dst, u8 bmask, u32 src, bool zero)
{
	struct nfp_insn_re_regs reg;
	int err;

	err = swreg_to_restricted(reg_none(), dst, src, &reg, true);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_ld_field(nfp_prog, sc, reg.areg, bmask, reg.breg, shift,
			reg.i8, zero, reg.swap, reg.wr_both);
}

static void
emit_ld_field(struct nfp_prog *nfp_prog, u32 dst, u8 bmask, u32 src,
	      enum shf_sc sc, u8 shift)
{
	emit_ld_field_any(nfp_prog, sc, shift, dst, bmask, src, false);
}

/* --- Wrappers --- */
static bool pack_immed(u32 imm, u16 *val, enum immed_shift *shift)
{
	if (!(imm & 0xffff0000)) {
		*val = imm;
		*shift = IMMED_SHIFT_0B;
	} else if (!(imm & 0xff0000ff)) {
		*val = imm >> 8;
		*shift = IMMED_SHIFT_1B;
	} else if (!(imm & 0x0000ffff)) {
		*val = imm >> 16;
		*shift = IMMED_SHIFT_2B;
	} else {
		return false;
	}

	return true;
}

static void wrp_immed(struct nfp_prog *nfp_prog, u32 dst, u32 imm)
{
	enum immed_shift shift;
	u16 val;

	if (pack_immed(imm, &val, &shift)) {
		emit_immed(nfp_prog, dst, val, IMMED_WIDTH_ALL, false, shift);
	} else if (pack_immed(~imm, &val, &shift)) {
		emit_immed(nfp_prog, dst, val, IMMED_WIDTH_ALL, true, shift);
	} else {
		emit_immed(nfp_prog, dst, imm & 0xffff, IMMED_WIDTH_ALL,
			   false, IMMED_SHIFT_0B);
		emit_immed(nfp_prog, dst, imm >> 16, IMMED_WIDTH_WORD,
			   false, IMMED_SHIFT_2B);
	}
}

/* ur_load_imm_any() - encode immediate or use tmp register (unrestricted)
 * If the @imm is small enough encode it directly in operand and return
 * otherwise load @imm to a spare register and return its encoding.
 */
static u32 ur_load_imm_any(struct nfp_prog *nfp_prog, u32 imm, u32 tmp_reg)
{
	if (FIELD_FIT(UR_REG_IMM_MAX, imm))
		return reg_imm(imm);

	wrp_immed(nfp_prog, tmp_reg, imm);
	return tmp_reg;
}

/* re_load_imm_any() - encode immediate or use tmp register (restricted)
 * If the @imm is small enough encode it directly in operand and return
 * otherwise load @imm to a spare register and return its encoding.
 */
static u32 re_load_imm_any(struct nfp_prog *nfp_prog, u32 imm, u32 tmp_reg)
{
	if (FIELD_FIT(RE_REG_IMM_MAX, imm))
		return reg_imm(imm);

	wrp_immed(nfp_prog, tmp_reg, imm);
	return tmp_reg;
}

static void
wrp_br_special(struct nfp_prog *nfp_prog, enum br_mask mask,
	       enum br_special special)
{
	emit_br(nfp_prog, mask, 0, 0);

	nfp_prog->prog[nfp_prog->prog_len - 1] |=
		FIELD_PREP(OP_BR_SPECIAL, special);
}

static void wrp_reg_mov(struct nfp_prog *nfp_prog, u16 dst, u16 src)
{
	emit_alu(nfp_prog, reg_both(dst), reg_none(), ALU_OP_NONE, reg_b(src));
}

static int
construct_data_ind_ld(struct nfp_prog *nfp_prog, u16 offset,
		      u16 src, bool src_valid, u8 size)
{
	unsigned int i;
	u16 shift, sz;
	u32 tmp_reg;

	/* We load the value from the address indicated in @offset and then
	 * shift out the data we don't need.  Note: this is big endian!
	 */
	sz = size < 4 ? 4 : size;
	shift = size < 4 ? 4 - size : 0;

	if (src_valid) {
		/* Calculate the true offset (src_reg + imm) */
		tmp_reg = ur_load_imm_any(nfp_prog, offset, imm_b(nfp_prog));
		emit_alu(nfp_prog, imm_both(nfp_prog),
			 reg_a(src), ALU_OP_ADD, tmp_reg);
		/* Check packet length (size guaranteed to fit b/c it's u8) */
		emit_alu(nfp_prog, imm_a(nfp_prog),
			 imm_a(nfp_prog), ALU_OP_ADD, reg_imm(size));
		emit_alu(nfp_prog, reg_none(),
			 NFP_BPF_ABI_LEN, ALU_OP_SUB, imm_a(nfp_prog));
		wrp_br_special(nfp_prog, BR_BLO, OP_BR_GO_ABORT);
		/* Load data */
		emit_cmd(nfp_prog, CMD_TGT_READ8, CMD_MODE_32b, 0,
			 pkt_reg(nfp_prog), imm_b(nfp_prog), sz - 1, true);
	} else {
		/* Check packet length */
		tmp_reg = ur_load_imm_any(nfp_prog, offset + size,
					  imm_a(nfp_prog));
		emit_alu(nfp_prog, reg_none(),
			 NFP_BPF_ABI_LEN, ALU_OP_SUB, tmp_reg);
		wrp_br_special(nfp_prog, BR_BLO, OP_BR_GO_ABORT);
		/* Load data */
		tmp_reg = re_load_imm_any(nfp_prog, offset, imm_b(nfp_prog));
		emit_cmd(nfp_prog, CMD_TGT_READ8, CMD_MODE_32b, 0,
			 pkt_reg(nfp_prog), tmp_reg, sz - 1, true);
	}

	i = 0;
	if (shift)
		emit_shf(nfp_prog, reg_both(0), reg_none(), SHF_OP_NONE,
			 reg_xfer(0), SHF_SC_R_SHF, shift * 8);
	else
		for (; i * 4 < size; i++)
			emit_alu(nfp_prog, reg_both(i),
				 reg_none(), ALU_OP_NONE, reg_xfer(i));

	if (i < 2)
		wrp_immed(nfp_prog, reg_both(1), 0);

	return 0;
}

static int construct_data_ld(struct nfp_prog *nfp_prog, u16 offset, u8 size)
{
	return construct_data_ind_ld(nfp_prog, offset, 0, false, size);
}

static int wrp_set_mark(struct nfp_prog *nfp_prog, u8 src)
{
	emit_alu(nfp_prog, NFP_BPF_ABI_MARK,
		 reg_none(), ALU_OP_NONE, reg_b(src));
	emit_alu(nfp_prog, NFP_BPF_ABI_FLAGS,
		 NFP_BPF_ABI_FLAGS, ALU_OP_OR, reg_imm(NFP_BPF_ABI_FLAG_MARK));

	return 0;
}

static void
wrp_alu_imm(struct nfp_prog *nfp_prog, u8 dst, enum alu_op alu_op, u32 imm)
{
	u32 tmp_reg;

	if (alu_op == ALU_OP_AND) {
		if (!imm)
			wrp_immed(nfp_prog, reg_both(dst), 0);
		if (!imm || !~imm)
			return;
	}
	if (alu_op == ALU_OP_OR) {
		if (!~imm)
			wrp_immed(nfp_prog, reg_both(dst), ~0U);
		if (!imm || !~imm)
			return;
	}
	if (alu_op == ALU_OP_XOR) {
		if (!~imm)
			emit_alu(nfp_prog, reg_both(dst), reg_none(),
				 ALU_OP_NEG, reg_b(dst));
		if (!imm || !~imm)
			return;
	}

	tmp_reg = ur_load_imm_any(nfp_prog, imm, imm_b(nfp_prog));
	emit_alu(nfp_prog, reg_both(dst), reg_a(dst), alu_op, tmp_reg);
}

static int
wrp_alu64_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      enum alu_op alu_op, bool skip)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */

	if (skip) {
		meta->skip = true;
		return 0;
	}

	wrp_alu_imm(nfp_prog, insn->dst_reg * 2, alu_op, imm & ~0U);
	wrp_alu_imm(nfp_prog, insn->dst_reg * 2 + 1, alu_op, imm >> 32);

	return 0;
}

static int
wrp_alu64_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      enum alu_op alu_op)
{
	u8 dst = meta->insn.dst_reg * 2, src = meta->insn.src_reg * 2;

	emit_alu(nfp_prog, reg_both(dst), reg_a(dst), alu_op, reg_b(src));
	emit_alu(nfp_prog, reg_both(dst + 1),
		 reg_a(dst + 1), alu_op, reg_b(src + 1));

	return 0;
}

static int
wrp_alu32_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      enum alu_op alu_op, bool skip)
{
	const struct bpf_insn *insn = &meta->insn;

	if (skip) {
		meta->skip = true;
		return 0;
	}

	wrp_alu_imm(nfp_prog, insn->dst_reg * 2, alu_op, insn->imm);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2 + 1), 0);

	return 0;
}

static int
wrp_alu32_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      enum alu_op alu_op)
{
	u8 dst = meta->insn.dst_reg * 2, src = meta->insn.src_reg * 2;

	emit_alu(nfp_prog, reg_both(dst), reg_a(dst), alu_op, reg_b(src));
	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return 0;
}

static void
wrp_test_reg_one(struct nfp_prog *nfp_prog, u8 dst, enum alu_op alu_op, u8 src,
		 enum br_mask br_mask, u16 off)
{
	emit_alu(nfp_prog, reg_none(), reg_a(dst), alu_op, reg_b(src));
	emit_br(nfp_prog, br_mask, off, 0);
}

static int
wrp_test_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	     enum alu_op alu_op, enum br_mask br_mask)
{
	const struct bpf_insn *insn = &meta->insn;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	wrp_test_reg_one(nfp_prog, insn->dst_reg * 2, alu_op,
			 insn->src_reg * 2, br_mask, insn->off);
	wrp_test_reg_one(nfp_prog, insn->dst_reg * 2 + 1, alu_op,
			 insn->src_reg * 2 + 1, br_mask, insn->off);

	return 0;
}

static int
wrp_cmp_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	    enum br_mask br_mask, bool swap)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	u8 reg = insn->dst_reg * 2;
	u32 tmp_reg;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	tmp_reg = ur_load_imm_any(nfp_prog, imm & ~0U, imm_b(nfp_prog));
	if (!swap)
		emit_alu(nfp_prog, reg_none(), reg_a(reg), ALU_OP_SUB, tmp_reg);
	else
		emit_alu(nfp_prog, reg_none(), tmp_reg, ALU_OP_SUB, reg_a(reg));

	tmp_reg = ur_load_imm_any(nfp_prog, imm >> 32, imm_b(nfp_prog));
	if (!swap)
		emit_alu(nfp_prog, reg_none(),
			 reg_a(reg + 1), ALU_OP_SUB_C, tmp_reg);
	else
		emit_alu(nfp_prog, reg_none(),
			 tmp_reg, ALU_OP_SUB_C, reg_a(reg + 1));

	emit_br(nfp_prog, br_mask, insn->off, 0);

	return 0;
}

static int
wrp_cmp_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	    enum br_mask br_mask, bool swap)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 areg = insn->src_reg * 2, breg = insn->dst_reg * 2;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	if (swap) {
		areg ^= breg;
		breg ^= areg;
		areg ^= breg;
	}

	emit_alu(nfp_prog, reg_none(), reg_a(areg), ALU_OP_SUB, reg_b(breg));
	emit_alu(nfp_prog, reg_none(),
		 reg_a(areg + 1), ALU_OP_SUB_C, reg_b(breg + 1));
	emit_br(nfp_prog, br_mask, insn->off, 0);

	return 0;
}

/* --- Callbacks --- */
static int mov_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	wrp_reg_mov(nfp_prog, insn->dst_reg * 2, insn->src_reg * 2);
	wrp_reg_mov(nfp_prog, insn->dst_reg * 2 + 1, insn->src_reg * 2 + 1);

	return 0;
}

static int mov_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	u64 imm = meta->insn.imm; /* sign extend */

	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2), imm & ~0U);
	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), imm >> 32);

	return 0;
}

static int xor_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_reg(nfp_prog, meta, ALU_OP_XOR);
}

static int xor_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_imm(nfp_prog, meta, ALU_OP_XOR, !meta->insn.imm);
}

static int and_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_reg(nfp_prog, meta, ALU_OP_AND);
}

static int and_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_imm(nfp_prog, meta, ALU_OP_AND, !~meta->insn.imm);
}

static int or_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_reg(nfp_prog, meta, ALU_OP_OR);
}

static int or_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu64_imm(nfp_prog, meta, ALU_OP_OR, !meta->insn.imm);
}

static int add_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2),
		 reg_a(insn->dst_reg * 2), ALU_OP_ADD,
		 reg_b(insn->src_reg * 2));
	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2 + 1),
		 reg_a(insn->dst_reg * 2 + 1), ALU_OP_ADD_C,
		 reg_b(insn->src_reg * 2 + 1));

	return 0;
}

static int add_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */

	wrp_alu_imm(nfp_prog, insn->dst_reg * 2, ALU_OP_ADD, imm & ~0U);
	wrp_alu_imm(nfp_prog, insn->dst_reg * 2 + 1, ALU_OP_ADD_C, imm >> 32);

	return 0;
}

static int sub_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2),
		 reg_a(insn->dst_reg * 2), ALU_OP_SUB,
		 reg_b(insn->src_reg * 2));
	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2 + 1),
		 reg_a(insn->dst_reg * 2 + 1), ALU_OP_SUB_C,
		 reg_b(insn->src_reg * 2 + 1));

	return 0;
}

static int sub_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */

	wrp_alu_imm(nfp_prog, insn->dst_reg * 2, ALU_OP_SUB, imm & ~0U);
	wrp_alu_imm(nfp_prog, insn->dst_reg * 2 + 1, ALU_OP_SUB_C, imm >> 32);

	return 0;
}

static int shl_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	if (insn->imm != 32)
		return 1; /* TODO */

	wrp_reg_mov(nfp_prog, insn->dst_reg * 2 + 1, insn->dst_reg * 2);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2), 0);

	return 0;
}

static int shr_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	if (insn->imm != 32)
		return 1; /* TODO */

	wrp_reg_mov(nfp_prog, insn->dst_reg * 2, insn->dst_reg * 2 + 1);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2 + 1), 0);

	return 0;
}

static int mov_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	wrp_reg_mov(nfp_prog, insn->dst_reg * 2,  insn->src_reg * 2);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2 + 1), 0);

	return 0;
}

static int mov_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2), insn->imm);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2 + 1), 0);

	return 0;
}

static int xor_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_reg(nfp_prog, meta, ALU_OP_XOR);
}

static int xor_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_imm(nfp_prog, meta, ALU_OP_XOR, !~meta->insn.imm);
}

static int and_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_reg(nfp_prog, meta, ALU_OP_AND);
}

static int and_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_imm(nfp_prog, meta, ALU_OP_AND, !~meta->insn.imm);
}

static int or_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_reg(nfp_prog, meta, ALU_OP_OR);
}

static int or_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_imm(nfp_prog, meta, ALU_OP_OR, !meta->insn.imm);
}

static int add_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_reg(nfp_prog, meta, ALU_OP_ADD);
}

static int add_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_imm(nfp_prog, meta, ALU_OP_ADD, !meta->insn.imm);
}

static int sub_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_reg(nfp_prog, meta, ALU_OP_SUB);
}

static int sub_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_alu32_imm(nfp_prog, meta, ALU_OP_SUB, !meta->insn.imm);
}

static int shl_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	if (!insn->imm)
		return 1; /* TODO: zero shift means indirect */

	emit_shf(nfp_prog, reg_both(insn->dst_reg * 2),
		 reg_none(), SHF_OP_NONE, reg_b(insn->dst_reg * 2),
		 SHF_SC_L_SHF, insn->imm);
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2 + 1), 0);

	return 0;
}

static int imm_ld8_part2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	wrp_immed(nfp_prog, reg_both(nfp_meta_prev(meta)->insn.dst_reg * 2 + 1),
		  meta->insn.imm);

	return 0;
}

static int imm_ld8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	meta->double_cb = imm_ld8_part2;
	wrp_immed(nfp_prog, reg_both(insn->dst_reg * 2), insn->imm);

	return 0;
}

static int data_ld1(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ld(nfp_prog, meta->insn.imm, 1);
}

static int data_ld2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ld(nfp_prog, meta->insn.imm, 2);
}

static int data_ld4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ld(nfp_prog, meta->insn.imm, 4);
}

static int data_ind_ld1(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ind_ld(nfp_prog, meta->insn.imm,
				     meta->insn.src_reg * 2, true, 1);
}

static int data_ind_ld2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ind_ld(nfp_prog, meta->insn.imm,
				     meta->insn.src_reg * 2, true, 2);
}

static int data_ind_ld4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ind_ld(nfp_prog, meta->insn.imm,
				     meta->insn.src_reg * 2, true, 4);
}

static int mem_ldx4_skb(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	if (meta->insn.off == offsetof(struct sk_buff, len))
		emit_alu(nfp_prog, reg_both(meta->insn.dst_reg * 2),
			 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_LEN);
	else
		return -EOPNOTSUPP;

	return 0;
}

static int mem_ldx4_xdp(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	u32 dst = reg_both(meta->insn.dst_reg * 2);

	if (meta->insn.off != offsetof(struct xdp_md, data) &&
	    meta->insn.off != offsetof(struct xdp_md, data_end))
		return -EOPNOTSUPP;

	emit_alu(nfp_prog, dst, reg_none(), ALU_OP_NONE, NFP_BPF_ABI_PKT);

	if (meta->insn.off == offsetof(struct xdp_md, data))
		return 0;

	emit_alu(nfp_prog, dst,	dst, ALU_OP_ADD, NFP_BPF_ABI_LEN);

	return 0;
}

static int mem_ldx4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	int ret;

	if (nfp_prog->act == NN_ACT_XDP)
		ret = mem_ldx4_xdp(nfp_prog, meta);
	else
		ret = mem_ldx4_skb(nfp_prog, meta);

	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return ret;
}

static int mem_stx4_skb(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	if (meta->insn.off == offsetof(struct sk_buff, mark))
		return wrp_set_mark(nfp_prog, meta->insn.src_reg * 2);

	return -EOPNOTSUPP;
}

static int mem_stx4_xdp(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return -EOPNOTSUPP;
}

static int mem_stx4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	if (nfp_prog->act == NN_ACT_XDP)
		return mem_stx4_xdp(nfp_prog, meta);
	return mem_stx4_skb(nfp_prog, meta);
}

static int jump(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	if (meta->insn.off < 0) /* TODO */
		return -EOPNOTSUPP;
	emit_br(nfp_prog, BR_UNC, meta->insn.off, 0);

	return 0;
}

static int jeq_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	u32 or1 = reg_a(insn->dst_reg * 2), or2 = reg_b(insn->dst_reg * 2 + 1);
	u32 tmp_reg;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	if (imm & ~0U) {
		tmp_reg = ur_load_imm_any(nfp_prog, imm & ~0U, imm_b(nfp_prog));
		emit_alu(nfp_prog, imm_a(nfp_prog),
			 reg_a(insn->dst_reg * 2), ALU_OP_XOR, tmp_reg);
		or1 = imm_a(nfp_prog);
	}

	if (imm >> 32) {
		tmp_reg = ur_load_imm_any(nfp_prog, imm >> 32, imm_b(nfp_prog));
		emit_alu(nfp_prog, imm_b(nfp_prog),
			 reg_a(insn->dst_reg * 2 + 1), ALU_OP_XOR, tmp_reg);
		or2 = imm_b(nfp_prog);
	}

	emit_alu(nfp_prog, reg_none(), or1, ALU_OP_OR, or2);
	emit_br(nfp_prog, BR_BEQ, insn->off, 0);

	return 0;
}

static int jgt_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BLO, false);
}

static int jge_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BHS, true);
}

static int jset_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	u32 tmp_reg;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	if (!imm) {
		meta->skip = true;
		return 0;
	}

	if (imm & ~0U) {
		tmp_reg = ur_load_imm_any(nfp_prog, imm & ~0U, imm_b(nfp_prog));
		emit_alu(nfp_prog, reg_none(),
			 reg_a(insn->dst_reg * 2), ALU_OP_AND, tmp_reg);
		emit_br(nfp_prog, BR_BNE, insn->off, 0);
	}

	if (imm >> 32) {
		tmp_reg = ur_load_imm_any(nfp_prog, imm >> 32, imm_b(nfp_prog));
		emit_alu(nfp_prog, reg_none(),
			 reg_a(insn->dst_reg * 2 + 1), ALU_OP_AND, tmp_reg);
		emit_br(nfp_prog, BR_BNE, insn->off, 0);
	}

	return 0;
}

static int jne_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	u32 tmp_reg;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	if (!imm) {
		emit_alu(nfp_prog, reg_none(), reg_a(insn->dst_reg * 2),
			 ALU_OP_OR, reg_b(insn->dst_reg * 2 + 1));
		emit_br(nfp_prog, BR_BNE, insn->off, 0);
	}

	tmp_reg = ur_load_imm_any(nfp_prog, imm & ~0U, imm_b(nfp_prog));
	emit_alu(nfp_prog, reg_none(),
		 reg_a(insn->dst_reg * 2), ALU_OP_XOR, tmp_reg);
	emit_br(nfp_prog, BR_BNE, insn->off, 0);

	tmp_reg = ur_load_imm_any(nfp_prog, imm >> 32, imm_b(nfp_prog));
	emit_alu(nfp_prog, reg_none(),
		 reg_a(insn->dst_reg * 2 + 1), ALU_OP_XOR, tmp_reg);
	emit_br(nfp_prog, BR_BNE, insn->off, 0);

	return 0;
}

static int jeq_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	if (insn->off < 0) /* TODO */
		return -EOPNOTSUPP;

	emit_alu(nfp_prog, imm_a(nfp_prog), reg_a(insn->dst_reg * 2),
		 ALU_OP_XOR, reg_b(insn->src_reg * 2));
	emit_alu(nfp_prog, imm_b(nfp_prog), reg_a(insn->dst_reg * 2 + 1),
		 ALU_OP_XOR, reg_b(insn->src_reg * 2 + 1));
	emit_alu(nfp_prog, reg_none(),
		 imm_a(nfp_prog), ALU_OP_OR, imm_b(nfp_prog));
	emit_br(nfp_prog, BR_BEQ, insn->off, 0);

	return 0;
}

static int jgt_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_reg(nfp_prog, meta, BR_BLO, false);
}

static int jge_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_reg(nfp_prog, meta, BR_BHS, true);
}

static int jset_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_test_reg(nfp_prog, meta, ALU_OP_AND, BR_BNE);
}

static int jne_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_test_reg(nfp_prog, meta, ALU_OP_XOR, BR_BNE);
}

static int goto_out(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	wrp_br_special(nfp_prog, BR_UNC, OP_BR_GO_OUT);

	return 0;
}

static const instr_cb_t instr_cb[256] = {
	[BPF_ALU64 | BPF_MOV | BPF_X] =	mov_reg64,
	[BPF_ALU64 | BPF_MOV | BPF_K] =	mov_imm64,
	[BPF_ALU64 | BPF_XOR | BPF_X] =	xor_reg64,
	[BPF_ALU64 | BPF_XOR | BPF_K] =	xor_imm64,
	[BPF_ALU64 | BPF_AND | BPF_X] =	and_reg64,
	[BPF_ALU64 | BPF_AND | BPF_K] =	and_imm64,
	[BPF_ALU64 | BPF_OR | BPF_X] =	or_reg64,
	[BPF_ALU64 | BPF_OR | BPF_K] =	or_imm64,
	[BPF_ALU64 | BPF_ADD | BPF_X] =	add_reg64,
	[BPF_ALU64 | BPF_ADD | BPF_K] =	add_imm64,
	[BPF_ALU64 | BPF_SUB | BPF_X] =	sub_reg64,
	[BPF_ALU64 | BPF_SUB | BPF_K] =	sub_imm64,
	[BPF_ALU64 | BPF_LSH | BPF_K] =	shl_imm64,
	[BPF_ALU64 | BPF_RSH | BPF_K] =	shr_imm64,
	[BPF_ALU | BPF_MOV | BPF_X] =	mov_reg,
	[BPF_ALU | BPF_MOV | BPF_K] =	mov_imm,
	[BPF_ALU | BPF_XOR | BPF_X] =	xor_reg,
	[BPF_ALU | BPF_XOR | BPF_K] =	xor_imm,
	[BPF_ALU | BPF_AND | BPF_X] =	and_reg,
	[BPF_ALU | BPF_AND | BPF_K] =	and_imm,
	[BPF_ALU | BPF_OR | BPF_X] =	or_reg,
	[BPF_ALU | BPF_OR | BPF_K] =	or_imm,
	[BPF_ALU | BPF_ADD | BPF_X] =	add_reg,
	[BPF_ALU | BPF_ADD | BPF_K] =	add_imm,
	[BPF_ALU | BPF_SUB | BPF_X] =	sub_reg,
	[BPF_ALU | BPF_SUB | BPF_K] =	sub_imm,
	[BPF_ALU | BPF_LSH | BPF_K] =	shl_imm,
	[BPF_LD | BPF_IMM | BPF_DW] =	imm_ld8,
	[BPF_LD | BPF_ABS | BPF_B] =	data_ld1,
	[BPF_LD | BPF_ABS | BPF_H] =	data_ld2,
	[BPF_LD | BPF_ABS | BPF_W] =	data_ld4,
	[BPF_LD | BPF_IND | BPF_B] =	data_ind_ld1,
	[BPF_LD | BPF_IND | BPF_H] =	data_ind_ld2,
	[BPF_LD | BPF_IND | BPF_W] =	data_ind_ld4,
	[BPF_LDX | BPF_MEM | BPF_W] =	mem_ldx4,
	[BPF_STX | BPF_MEM | BPF_W] =	mem_stx4,
	[BPF_JMP | BPF_JA | BPF_K] =	jump,
	[BPF_JMP | BPF_JEQ | BPF_K] =	jeq_imm,
	[BPF_JMP | BPF_JGT | BPF_K] =	jgt_imm,
	[BPF_JMP | BPF_JGE | BPF_K] =	jge_imm,
	[BPF_JMP | BPF_JSET | BPF_K] =	jset_imm,
	[BPF_JMP | BPF_JNE | BPF_K] =	jne_imm,
	[BPF_JMP | BPF_JEQ | BPF_X] =	jeq_reg,
	[BPF_JMP | BPF_JGT | BPF_X] =	jgt_reg,
	[BPF_JMP | BPF_JGE | BPF_X] =	jge_reg,
	[BPF_JMP | BPF_JSET | BPF_X] =	jset_reg,
	[BPF_JMP | BPF_JNE | BPF_X] =	jne_reg,
	[BPF_JMP | BPF_EXIT] =		goto_out,
};

/* --- Misc code --- */
static void br_set_offset(u64 *instr, u16 offset)
{
	u16 addr_lo, addr_hi;

	addr_lo = offset & (OP_BR_ADDR_LO >> __bf_shf(OP_BR_ADDR_LO));
	addr_hi = offset != addr_lo;
	*instr &= ~(OP_BR_ADDR_HI | OP_BR_ADDR_LO);
	*instr |= FIELD_PREP(OP_BR_ADDR_HI, addr_hi);
	*instr |= FIELD_PREP(OP_BR_ADDR_LO, addr_lo);
}

/* --- Assembler logic --- */
static int nfp_fixup_branches(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *next;
	u32 off, br_idx;
	u32 idx;

	nfp_for_each_insn_walk2(nfp_prog, meta, next) {
		if (meta->skip)
			continue;
		if (BPF_CLASS(meta->insn.code) != BPF_JMP)
			continue;

		br_idx = nfp_prog_offset_to_index(nfp_prog, next->off) - 1;
		if (!nfp_is_br(nfp_prog->prog[br_idx])) {
			pr_err("Fixup found block not ending in branch %d %02x %016llx!!\n",
			       br_idx, meta->insn.code, nfp_prog->prog[br_idx]);
			return -ELOOP;
		}
		/* Leave special branches for later */
		if (FIELD_GET(OP_BR_SPECIAL, nfp_prog->prog[br_idx]))
			continue;

		/* Find the target offset in assembler realm */
		off = meta->insn.off;
		if (!off) {
			pr_err("Fixup found zero offset!!\n");
			return -ELOOP;
		}

		while (off && nfp_meta_has_next(nfp_prog, next)) {
			next = nfp_meta_next(next);
			off--;
		}
		if (off) {
			pr_err("Fixup found too large jump!! %d\n", off);
			return -ELOOP;
		}

		if (next->skip) {
			pr_err("Branch landing on removed instruction!!\n");
			return -ELOOP;
		}

		for (idx = nfp_prog_offset_to_index(nfp_prog, meta->off);
		     idx <= br_idx; idx++) {
			if (!nfp_is_br(nfp_prog->prog[idx]))
				continue;
			br_set_offset(&nfp_prog->prog[idx], next->off);
		}
	}

	/* Fixup 'goto out's separately, they can be scattered around */
	for (br_idx = 0; br_idx < nfp_prog->prog_len; br_idx++) {
		enum br_special special;

		if ((nfp_prog->prog[br_idx] & OP_BR_BASE_MASK) != OP_BR_BASE)
			continue;

		special = FIELD_GET(OP_BR_SPECIAL, nfp_prog->prog[br_idx]);
		switch (special) {
		case OP_BR_NORMAL:
			break;
		case OP_BR_GO_OUT:
			br_set_offset(&nfp_prog->prog[br_idx],
				      nfp_prog->tgt_out);
			break;
		case OP_BR_GO_ABORT:
			br_set_offset(&nfp_prog->prog[br_idx],
				      nfp_prog->tgt_abort);
			break;
		}

		nfp_prog->prog[br_idx] &= ~OP_BR_SPECIAL;
	}

	return 0;
}

static void nfp_intro(struct nfp_prog *nfp_prog)
{
	emit_alu(nfp_prog, pkt_reg(nfp_prog),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_PKT);
}

static void nfp_outro_tc_legacy(struct nfp_prog *nfp_prog)
{
	const u8 act2code[] = {
		[NN_ACT_TC_DROP]  = 0x22,
		[NN_ACT_TC_REDIR] = 0x24
	};
	/* Target for aborts */
	nfp_prog->tgt_abort = nfp_prog_current_offset(nfp_prog);
	wrp_immed(nfp_prog, reg_both(0), 0);

	/* Target for normal exits */
	nfp_prog->tgt_out = nfp_prog_current_offset(nfp_prog);
	/* Legacy TC mode:
	 *   0        0x11 -> pass,  count as stat0
	 *  -1  drop  0x22 -> drop,  count as stat1
	 *     redir  0x24 -> redir, count as stat1
	 *  ife mark  0x21 -> pass,  count as stat1
	 *  ife + tx  0x24 -> redir, count as stat1
	 */
	emit_br_byte_neq(nfp_prog, reg_b(0), 0xff, 0, nfp_prog->tgt_done, 2);
	emit_alu(nfp_prog, reg_a(0),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_imm(0x11), SHF_SC_L_SHF, 16);

	emit_br(nfp_prog, BR_UNC, nfp_prog->tgt_done, 1);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_imm(act2code[nfp_prog->act]),
		      SHF_SC_L_SHF, 16);
}

static void nfp_outro_tc_da(struct nfp_prog *nfp_prog)
{
	/* TC direct-action mode:
	 *   0,1   ok        NOT SUPPORTED[1]
	 *   2   drop  0x22 -> drop,  count as stat1
	 *   4,5 nuke  0x02 -> drop
	 *   7  redir  0x44 -> redir, count as stat2
	 *   * unspec  0x11 -> pass,  count as stat0
	 *
	 * [1] We can't support OK and RECLASSIFY because we can't tell TC
	 *     the exact decision made.  We are forced to support UNSPEC
	 *     to handle aborts so that's the only one we handle for passing
	 *     packets up the stack.
	 */
	/* Target for aborts */
	nfp_prog->tgt_abort = nfp_prog_current_offset(nfp_prog);

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

	emit_alu(nfp_prog, reg_a(0),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_imm(0x11), SHF_SC_L_SHF, 16);

	/* Target for normal exits */
	nfp_prog->tgt_out = nfp_prog_current_offset(nfp_prog);

	/* if R0 > 7 jump to abort */
	emit_alu(nfp_prog, reg_none(), reg_imm(7), ALU_OP_SUB, reg_b(0));
	emit_br(nfp_prog, BR_BLO, nfp_prog->tgt_abort, 0);
	emit_alu(nfp_prog, reg_a(0),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_FLAGS);

	wrp_immed(nfp_prog, reg_b(2), 0x41221211);
	wrp_immed(nfp_prog, reg_b(3), 0x41001211);

	emit_shf(nfp_prog, reg_a(1),
		 reg_none(), SHF_OP_NONE, reg_b(0), SHF_SC_L_SHF, 2);

	emit_alu(nfp_prog, reg_none(), reg_a(1), ALU_OP_OR, reg_imm(0));
	emit_shf(nfp_prog, reg_a(2),
		 reg_imm(0xf), SHF_OP_AND, reg_b(2), SHF_SC_R_SHF, 0);

	emit_alu(nfp_prog, reg_none(), reg_a(1), ALU_OP_OR, reg_imm(0));
	emit_shf(nfp_prog, reg_b(2),
		 reg_imm(0xf), SHF_OP_AND, reg_b(3), SHF_SC_R_SHF, 0);

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

	emit_shf(nfp_prog, reg_b(2),
		 reg_a(2), SHF_OP_OR, reg_b(2), SHF_SC_L_SHF, 4);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_b(2), SHF_SC_L_SHF, 16);
}

static void nfp_outro_xdp(struct nfp_prog *nfp_prog)
{
	/* XDP return codes:
	 *   0 aborted  0x82 -> drop,  count as stat3
	 *   1    drop  0x22 -> drop,  count as stat1
	 *   2    pass  0x11 -> pass,  count as stat0
	 *   3      tx  0x44 -> redir, count as stat2
	 *   * unknown  0x82 -> drop,  count as stat3
	 */
	/* Target for aborts */
	nfp_prog->tgt_abort = nfp_prog_current_offset(nfp_prog);

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

	emit_alu(nfp_prog, reg_a(0),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_imm(0x82), SHF_SC_L_SHF, 16);

	/* Target for normal exits */
	nfp_prog->tgt_out = nfp_prog_current_offset(nfp_prog);

	/* if R0 > 3 jump to abort */
	emit_alu(nfp_prog, reg_none(), reg_imm(3), ALU_OP_SUB, reg_b(0));
	emit_br(nfp_prog, BR_BLO, nfp_prog->tgt_abort, 0);

	wrp_immed(nfp_prog, reg_b(2), 0x44112282);

	emit_shf(nfp_prog, reg_a(1),
		 reg_none(), SHF_OP_NONE, reg_b(0), SHF_SC_L_SHF, 3);

	emit_alu(nfp_prog, reg_none(), reg_a(1), ALU_OP_OR, reg_imm(0));
	emit_shf(nfp_prog, reg_b(2),
		 reg_imm(0xff), SHF_OP_AND, reg_b(2), SHF_SC_R_SHF, 0);

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

	emit_alu(nfp_prog, reg_a(0),
		 reg_none(), ALU_OP_NONE, NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_b(2), SHF_SC_L_SHF, 16);
}

static void nfp_outro(struct nfp_prog *nfp_prog)
{
	switch (nfp_prog->act) {
	case NN_ACT_DIRECT:
		nfp_outro_tc_da(nfp_prog);
		break;
	case NN_ACT_TC_DROP:
	case NN_ACT_TC_REDIR:
		nfp_outro_tc_legacy(nfp_prog);
		break;
	case NN_ACT_XDP:
		nfp_outro_xdp(nfp_prog);
		break;
	}
}

static int nfp_translate(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta;
	int err;

	nfp_intro(nfp_prog);
	if (nfp_prog->error)
		return nfp_prog->error;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		instr_cb_t cb = instr_cb[meta->insn.code];

		meta->off = nfp_prog_current_offset(nfp_prog);

		if (meta->skip) {
			nfp_prog->n_translated++;
			continue;
		}

		if (nfp_meta_has_prev(nfp_prog, meta) &&
		    nfp_meta_prev(meta)->double_cb)
			cb = nfp_meta_prev(meta)->double_cb;
		if (!cb)
			return -ENOENT;
		err = cb(nfp_prog, meta);
		if (err)
			return err;

		nfp_prog->n_translated++;
	}

	nfp_outro(nfp_prog);
	if (nfp_prog->error)
		return nfp_prog->error;

	return nfp_fixup_branches(nfp_prog);
}

static int
nfp_prog_prepare(struct nfp_prog *nfp_prog, const struct bpf_insn *prog,
		 unsigned int cnt)
{
	unsigned int i;

	for (i = 0; i < cnt; i++) {
		struct nfp_insn_meta *meta;

		meta = kzalloc(sizeof(*meta), GFP_KERNEL);
		if (!meta)
			return -ENOMEM;

		meta->insn = prog[i];
		meta->n = i;

		list_add_tail(&meta->l, &nfp_prog->insns);
	}

	return 0;
}

/* --- Optimizations --- */
static void nfp_bpf_opt_reg_init(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		struct bpf_insn insn = meta->insn;

		/* Programs converted from cBPF start with register xoring */
		if (insn.code == (BPF_ALU64 | BPF_XOR | BPF_X) &&
		    insn.src_reg == insn.dst_reg)
			continue;

		/* Programs start with R6 = R1 but we ignore the skb pointer */
		if (insn.code == (BPF_ALU64 | BPF_MOV | BPF_X) &&
		    insn.src_reg == 1 && insn.dst_reg == 6)
			meta->skip = true;

		/* Return as soon as something doesn't match */
		if (!meta->skip)
			return;
	}
}

/* Try to rename registers so that program uses only low ones */
static int nfp_bpf_opt_reg_rename(struct nfp_prog *nfp_prog)
{
	bool reg_used[MAX_BPF_REG] = {};
	u8 tgt_reg[MAX_BPF_REG] = {};
	struct nfp_insn_meta *meta;
	unsigned int i, j;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (meta->skip)
			continue;

		reg_used[meta->insn.src_reg] = true;
		reg_used[meta->insn.dst_reg] = true;
	}

	for (i = 0, j = 0; i < ARRAY_SIZE(tgt_reg); i++) {
		if (!reg_used[i])
			continue;

		tgt_reg[i] = j++;
	}
	nfp_prog->num_regs = j;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		meta->insn.src_reg = tgt_reg[meta->insn.src_reg];
		meta->insn.dst_reg = tgt_reg[meta->insn.dst_reg];
	}

	return 0;
}

/* Remove masking after load since our load guarantees this is not needed */
static void nfp_bpf_opt_ld_mask(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta1, *meta2;
	const s32 exp_mask[] = {
		[BPF_B] = 0x000000ffU,
		[BPF_H] = 0x0000ffffU,
		[BPF_W] = 0xffffffffU,
	};

	nfp_for_each_insn_walk2(nfp_prog, meta1, meta2) {
		struct bpf_insn insn, next;

		insn = meta1->insn;
		next = meta2->insn;

		if (BPF_CLASS(insn.code) != BPF_LD)
			continue;
		if (BPF_MODE(insn.code) != BPF_ABS &&
		    BPF_MODE(insn.code) != BPF_IND)
			continue;

		if (next.code != (BPF_ALU64 | BPF_AND | BPF_K))
			continue;

		if (!exp_mask[BPF_SIZE(insn.code)])
			continue;
		if (exp_mask[BPF_SIZE(insn.code)] != next.imm)
			continue;

		if (next.src_reg || next.dst_reg)
			continue;

		meta2->skip = true;
	}
}

static void nfp_bpf_opt_ld_shift(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta1, *meta2, *meta3;

	nfp_for_each_insn_walk3(nfp_prog, meta1, meta2, meta3) {
		struct bpf_insn insn, next1, next2;

		insn = meta1->insn;
		next1 = meta2->insn;
		next2 = meta3->insn;

		if (BPF_CLASS(insn.code) != BPF_LD)
			continue;
		if (BPF_MODE(insn.code) != BPF_ABS &&
		    BPF_MODE(insn.code) != BPF_IND)
			continue;
		if (BPF_SIZE(insn.code) != BPF_W)
			continue;

		if (!(next1.code == (BPF_LSH | BPF_K | BPF_ALU64) &&
		      next2.code == (BPF_RSH | BPF_K | BPF_ALU64)) &&
		    !(next1.code == (BPF_RSH | BPF_K | BPF_ALU64) &&
		      next2.code == (BPF_LSH | BPF_K | BPF_ALU64)))
			continue;

		if (next1.src_reg || next1.dst_reg ||
		    next2.src_reg || next2.dst_reg)
			continue;

		if (next1.imm != 0x20 || next2.imm != 0x20)
			continue;

		meta2->skip = true;
		meta3->skip = true;
	}
}

static int nfp_bpf_optimize(struct nfp_prog *nfp_prog)
{
	int ret;

	nfp_bpf_opt_reg_init(nfp_prog);

	ret = nfp_bpf_opt_reg_rename(nfp_prog);
	if (ret)
		return ret;

	nfp_bpf_opt_ld_mask(nfp_prog);
	nfp_bpf_opt_ld_shift(nfp_prog);

	return 0;
}

/**
 * nfp_bpf_jit() - translate BPF code into NFP assembly
 * @filter:	kernel BPF filter struct
 * @prog_mem:	memory to store assembler instructions
 * @act:	action attached to this eBPF program
 * @prog_start:	offset of the first instruction when loaded
 * @prog_done:	where to jump on exit
 * @prog_sz:	size of @prog_mem in instructions
 * @res:	achieved parameters of translation results
 */
int
nfp_bpf_jit(struct bpf_prog *filter, void *prog_mem,
	    enum nfp_bpf_action_type act,
	    unsigned int prog_start, unsigned int prog_done,
	    unsigned int prog_sz, struct nfp_bpf_result *res)
{
	struct nfp_prog *nfp_prog;
	int ret;

	nfp_prog = kzalloc(sizeof(*nfp_prog), GFP_KERNEL);
	if (!nfp_prog)
		return -ENOMEM;

	INIT_LIST_HEAD(&nfp_prog->insns);
	nfp_prog->act = act;
	nfp_prog->start_off = prog_start;
	nfp_prog->tgt_done = prog_done;

	ret = nfp_prog_prepare(nfp_prog, filter->insnsi, filter->len);
	if (ret)
		goto out;

	ret = nfp_prog_verify(nfp_prog, filter);
	if (ret)
		goto out;

	ret = nfp_bpf_optimize(nfp_prog);
	if (ret)
		goto out;

	if (nfp_prog->num_regs <= 7)
		nfp_prog->regs_per_thread = 16;
	else
		nfp_prog->regs_per_thread = 32;

	nfp_prog->prog = prog_mem;
	nfp_prog->__prog_alloc_len = prog_sz;

	ret = nfp_translate(nfp_prog);
	if (ret) {
		pr_err("Translation failed with error %d (translated: %u)\n",
		       ret, nfp_prog->n_translated);
		ret = -EINVAL;
	}

	res->n_instr = nfp_prog->prog_len;
	res->dense_mode = nfp_prog->num_regs <= 7;
out:
	nfp_prog_free(nfp_prog);

	return ret;
}
