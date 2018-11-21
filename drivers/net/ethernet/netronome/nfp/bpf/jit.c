/*
 * Copyright (C) 2016-2018 Netronome Systems, Inc.
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

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/pkt_cls.h>
#include <linux/unistd.h>

#include "main.h"
#include "../nfp_asm.h"
#include "../nfp_net_ctrl.h"

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
nfp_meta_has_prev(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return meta->l.prev != &nfp_prog->insns;
}

static void nfp_prog_push(struct nfp_prog *nfp_prog, u64 insn)
{
	if (nfp_prog->__prog_alloc_len / sizeof(u64) == nfp_prog->prog_len) {
		pr_warn("instruction limit reached (%u NFP instructions)\n",
			nfp_prog->prog_len);
		nfp_prog->error = -ENOSPC;
		return;
	}

	nfp_prog->prog[nfp_prog->prog_len] = insn;
	nfp_prog->prog_len++;
}

static unsigned int nfp_prog_current_offset(struct nfp_prog *nfp_prog)
{
	return nfp_prog->prog_len;
}

static bool
nfp_prog_confirm_current_offset(struct nfp_prog *nfp_prog, unsigned int off)
{
	/* If there is a recorded error we may have dropped instructions;
	 * that doesn't have to be due to translator bug, and the translation
	 * will fail anyway, so just return OK.
	 */
	if (nfp_prog->error)
		return true;
	return !WARN_ON_ONCE(nfp_prog_current_offset(nfp_prog) != off);
}

/* --- Emitters --- */
static void
__emit_cmd(struct nfp_prog *nfp_prog, enum cmd_tgt_map op,
	   u8 mode, u8 xfer, u8 areg, u8 breg, u8 size, enum cmd_ctx_swap ctx,
	   bool indir)
{
	u64 insn;

	insn =	FIELD_PREP(OP_CMD_A_SRC, areg) |
		FIELD_PREP(OP_CMD_CTX, ctx) |
		FIELD_PREP(OP_CMD_B_SRC, breg) |
		FIELD_PREP(OP_CMD_TOKEN, cmd_tgt_act[op].token) |
		FIELD_PREP(OP_CMD_XFER, xfer) |
		FIELD_PREP(OP_CMD_CNT, size) |
		FIELD_PREP(OP_CMD_SIG, ctx != CMD_CTX_NO_SWAP) |
		FIELD_PREP(OP_CMD_TGT_CMD, cmd_tgt_act[op].tgt_cmd) |
		FIELD_PREP(OP_CMD_INDIR, indir) |
		FIELD_PREP(OP_CMD_MODE, mode);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_cmd_any(struct nfp_prog *nfp_prog, enum cmd_tgt_map op, u8 mode, u8 xfer,
	     swreg lreg, swreg rreg, u8 size, enum cmd_ctx_swap ctx, bool indir)
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
	if (reg.dst_lmextn || reg.src_lmextn) {
		pr_err("cmd can't use LMextn\n");
		nfp_prog->error = -EFAULT;
		return;
	}

	__emit_cmd(nfp_prog, op, mode, xfer, reg.areg, reg.breg, size, ctx,
		   indir);
}

static void
emit_cmd(struct nfp_prog *nfp_prog, enum cmd_tgt_map op, u8 mode, u8 xfer,
	 swreg lreg, swreg rreg, u8 size, enum cmd_ctx_swap ctx)
{
	emit_cmd_any(nfp_prog, op, mode, xfer, lreg, rreg, size, ctx, false);
}

static void
emit_cmd_indir(struct nfp_prog *nfp_prog, enum cmd_tgt_map op, u8 mode, u8 xfer,
	       swreg lreg, swreg rreg, u8 size, enum cmd_ctx_swap ctx)
{
	emit_cmd_any(nfp_prog, op, mode, xfer, lreg, rreg, size, ctx, true);
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

static void
emit_br_relo(struct nfp_prog *nfp_prog, enum br_mask mask, u16 addr, u8 defer,
	     enum nfp_relo_type relo)
{
	if (mask == BR_UNC && defer > 2) {
		pr_err("BUG: branch defer out of bounds %d\n", defer);
		nfp_prog->error = -EFAULT;
		return;
	}

	__emit_br(nfp_prog, mask,
		  mask != BR_UNC ? BR_EV_PIP_COND : BR_EV_PIP_UNCOND,
		  BR_CSS_NONE, addr, defer);

	nfp_prog->prog[nfp_prog->prog_len - 1] |=
		FIELD_PREP(OP_RELO_TYPE, relo);
}

static void
emit_br(struct nfp_prog *nfp_prog, enum br_mask mask, u16 addr, u8 defer)
{
	emit_br_relo(nfp_prog, mask, addr, defer, RELO_BR_REL);
}

static void
__emit_br_bit(struct nfp_prog *nfp_prog, u16 areg, u16 breg, u16 addr, u8 defer,
	      bool set, bool src_lmextn)
{
	u16 addr_lo, addr_hi;
	u64 insn;

	addr_lo = addr & (OP_BR_BIT_ADDR_LO >> __bf_shf(OP_BR_BIT_ADDR_LO));
	addr_hi = addr != addr_lo;

	insn = OP_BR_BIT_BASE |
		FIELD_PREP(OP_BR_BIT_A_SRC, areg) |
		FIELD_PREP(OP_BR_BIT_B_SRC, breg) |
		FIELD_PREP(OP_BR_BIT_BV, set) |
		FIELD_PREP(OP_BR_BIT_DEFBR, defer) |
		FIELD_PREP(OP_BR_BIT_ADDR_LO, addr_lo) |
		FIELD_PREP(OP_BR_BIT_ADDR_HI, addr_hi) |
		FIELD_PREP(OP_BR_BIT_SRC_LMEXTN, src_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_br_bit_relo(struct nfp_prog *nfp_prog, swreg src, u8 bit, u16 addr,
		 u8 defer, bool set, enum nfp_relo_type relo)
{
	struct nfp_insn_re_regs reg;
	int err;

	/* NOTE: The bit to test is specified as an rotation amount, such that
	 *	 the bit to test will be placed on the MSB of the result when
	 *	 doing a rotate right. For bit X, we need right rotate X + 1.
	 */
	bit += 1;

	err = swreg_to_restricted(reg_none(), src, reg_imm(bit), &reg, false);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_br_bit(nfp_prog, reg.areg, reg.breg, addr, defer, set,
		      reg.src_lmextn);

	nfp_prog->prog[nfp_prog->prog_len - 1] |=
		FIELD_PREP(OP_RELO_TYPE, relo);
}

static void
emit_br_bset(struct nfp_prog *nfp_prog, swreg src, u8 bit, u16 addr, u8 defer)
{
	emit_br_bit_relo(nfp_prog, src, bit, addr, defer, true, RELO_BR_REL);
}

static void
__emit_immed(struct nfp_prog *nfp_prog, u16 areg, u16 breg, u16 imm_hi,
	     enum immed_width width, bool invert,
	     enum immed_shift shift, bool wr_both,
	     bool dst_lmextn, bool src_lmextn)
{
	u64 insn;

	insn = OP_IMMED_BASE |
		FIELD_PREP(OP_IMMED_A_SRC, areg) |
		FIELD_PREP(OP_IMMED_B_SRC, breg) |
		FIELD_PREP(OP_IMMED_IMM, imm_hi) |
		FIELD_PREP(OP_IMMED_WIDTH, width) |
		FIELD_PREP(OP_IMMED_INV, invert) |
		FIELD_PREP(OP_IMMED_SHIFT, shift) |
		FIELD_PREP(OP_IMMED_WR_AB, wr_both) |
		FIELD_PREP(OP_IMMED_SRC_LMEXTN, src_lmextn) |
		FIELD_PREP(OP_IMMED_DST_LMEXTN, dst_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_immed(struct nfp_prog *nfp_prog, swreg dst, u16 imm,
	   enum immed_width width, bool invert, enum immed_shift shift)
{
	struct nfp_insn_ur_regs reg;
	int err;

	if (swreg_type(dst) == NN_REG_IMM) {
		nfp_prog->error = -EFAULT;
		return;
	}

	err = swreg_to_unrestricted(dst, dst, reg_imm(imm & 0xff), &reg);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	/* Use reg.dst when destination is No-Dest. */
	__emit_immed(nfp_prog,
		     swreg_type(dst) == NN_REG_NONE ? reg.dst : reg.areg,
		     reg.breg, imm >> 8, width, invert, shift,
		     reg.wr_both, reg.dst_lmextn, reg.src_lmextn);
}

static void
__emit_shf(struct nfp_prog *nfp_prog, u16 dst, enum alu_dst_ab dst_ab,
	   enum shf_sc sc, u8 shift,
	   u16 areg, enum shf_op op, u16 breg, bool i8, bool sw, bool wr_both,
	   bool dst_lmextn, bool src_lmextn)
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
		FIELD_PREP(OP_SHF_WR_AB, wr_both) |
		FIELD_PREP(OP_SHF_SRC_LMEXTN, src_lmextn) |
		FIELD_PREP(OP_SHF_DST_LMEXTN, dst_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_shf(struct nfp_prog *nfp_prog, swreg dst,
	 swreg lreg, enum shf_op op, swreg rreg, enum shf_sc sc, u8 shift)
{
	struct nfp_insn_re_regs reg;
	int err;

	err = swreg_to_restricted(dst, lreg, rreg, &reg, true);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_shf(nfp_prog, reg.dst, reg.dst_ab, sc, shift,
		   reg.areg, op, reg.breg, reg.i8, reg.swap, reg.wr_both,
		   reg.dst_lmextn, reg.src_lmextn);
}

static void
emit_shf_indir(struct nfp_prog *nfp_prog, swreg dst,
	       swreg lreg, enum shf_op op, swreg rreg, enum shf_sc sc)
{
	if (sc == SHF_SC_R_ROT) {
		pr_err("indirect shift is not allowed on rotation\n");
		nfp_prog->error = -EFAULT;
		return;
	}

	emit_shf(nfp_prog, dst, lreg, op, rreg, sc, 0);
}

static void
__emit_alu(struct nfp_prog *nfp_prog, u16 dst, enum alu_dst_ab dst_ab,
	   u16 areg, enum alu_op op, u16 breg, bool swap, bool wr_both,
	   bool dst_lmextn, bool src_lmextn)
{
	u64 insn;

	insn = OP_ALU_BASE |
		FIELD_PREP(OP_ALU_A_SRC, areg) |
		FIELD_PREP(OP_ALU_B_SRC, breg) |
		FIELD_PREP(OP_ALU_DST, dst) |
		FIELD_PREP(OP_ALU_SW, swap) |
		FIELD_PREP(OP_ALU_OP, op) |
		FIELD_PREP(OP_ALU_DST_AB, dst_ab) |
		FIELD_PREP(OP_ALU_WR_AB, wr_both) |
		FIELD_PREP(OP_ALU_SRC_LMEXTN, src_lmextn) |
		FIELD_PREP(OP_ALU_DST_LMEXTN, dst_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_alu(struct nfp_prog *nfp_prog, swreg dst,
	 swreg lreg, enum alu_op op, swreg rreg)
{
	struct nfp_insn_ur_regs reg;
	int err;

	err = swreg_to_unrestricted(dst, lreg, rreg, &reg);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_alu(nfp_prog, reg.dst, reg.dst_ab,
		   reg.areg, op, reg.breg, reg.swap, reg.wr_both,
		   reg.dst_lmextn, reg.src_lmextn);
}

static void
__emit_ld_field(struct nfp_prog *nfp_prog, enum shf_sc sc,
		u8 areg, u8 bmask, u8 breg, u8 shift, bool imm8,
		bool zero, bool swap, bool wr_both,
		bool dst_lmextn, bool src_lmextn)
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
		FIELD_PREP(OP_LDF_WR_AB, wr_both) |
		FIELD_PREP(OP_LDF_SRC_LMEXTN, src_lmextn) |
		FIELD_PREP(OP_LDF_DST_LMEXTN, dst_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void
emit_ld_field_any(struct nfp_prog *nfp_prog, swreg dst, u8 bmask, swreg src,
		  enum shf_sc sc, u8 shift, bool zero)
{
	struct nfp_insn_re_regs reg;
	int err;

	/* Note: ld_field is special as it uses one of the src regs as dst */
	err = swreg_to_restricted(dst, dst, src, &reg, true);
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_ld_field(nfp_prog, sc, reg.areg, bmask, reg.breg, shift,
			reg.i8, zero, reg.swap, reg.wr_both,
			reg.dst_lmextn, reg.src_lmextn);
}

static void
emit_ld_field(struct nfp_prog *nfp_prog, swreg dst, u8 bmask, swreg src,
	      enum shf_sc sc, u8 shift)
{
	emit_ld_field_any(nfp_prog, dst, bmask, src, sc, shift, false);
}

static void
__emit_lcsr(struct nfp_prog *nfp_prog, u16 areg, u16 breg, bool wr, u16 addr,
	    bool dst_lmextn, bool src_lmextn)
{
	u64 insn;

	insn = OP_LCSR_BASE |
		FIELD_PREP(OP_LCSR_A_SRC, areg) |
		FIELD_PREP(OP_LCSR_B_SRC, breg) |
		FIELD_PREP(OP_LCSR_WRITE, wr) |
		FIELD_PREP(OP_LCSR_ADDR, addr / 4) |
		FIELD_PREP(OP_LCSR_SRC_LMEXTN, src_lmextn) |
		FIELD_PREP(OP_LCSR_DST_LMEXTN, dst_lmextn);

	nfp_prog_push(nfp_prog, insn);
}

static void emit_csr_wr(struct nfp_prog *nfp_prog, swreg src, u16 addr)
{
	struct nfp_insn_ur_regs reg;
	int err;

	/* This instruction takes immeds instead of reg_none() for the ignored
	 * operand, but we can't encode 2 immeds in one instr with our normal
	 * swreg infra so if param is an immed, we encode as reg_none() and
	 * copy the immed to both operands.
	 */
	if (swreg_type(src) == NN_REG_IMM) {
		err = swreg_to_unrestricted(reg_none(), src, reg_none(), &reg);
		reg.breg = reg.areg;
	} else {
		err = swreg_to_unrestricted(reg_none(), src, reg_imm(0), &reg);
	}
	if (err) {
		nfp_prog->error = err;
		return;
	}

	__emit_lcsr(nfp_prog, reg.areg, reg.breg, true, addr,
		    false, reg.src_lmextn);
}

/* CSR value is read in following immed[gpr, 0] */
static void __emit_csr_rd(struct nfp_prog *nfp_prog, u16 addr)
{
	__emit_lcsr(nfp_prog, 0, 0, false, addr, false, false);
}

static void emit_nop(struct nfp_prog *nfp_prog)
{
	__emit_immed(nfp_prog, UR_REG_IMM, UR_REG_IMM, 0, 0, 0, 0, 0, 0, 0);
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

static void wrp_immed(struct nfp_prog *nfp_prog, swreg dst, u32 imm)
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

static void
wrp_immed_relo(struct nfp_prog *nfp_prog, swreg dst, u32 imm,
	       enum nfp_relo_type relo)
{
	if (imm > 0xffff) {
		pr_err("relocation of a large immediate!\n");
		nfp_prog->error = -EFAULT;
		return;
	}
	emit_immed(nfp_prog, dst, imm, IMMED_WIDTH_ALL, false, IMMED_SHIFT_0B);

	nfp_prog->prog[nfp_prog->prog_len - 1] |=
		FIELD_PREP(OP_RELO_TYPE, relo);
}

/* ur_load_imm_any() - encode immediate or use tmp register (unrestricted)
 * If the @imm is small enough encode it directly in operand and return
 * otherwise load @imm to a spare register and return its encoding.
 */
static swreg ur_load_imm_any(struct nfp_prog *nfp_prog, u32 imm, swreg tmp_reg)
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
static swreg re_load_imm_any(struct nfp_prog *nfp_prog, u32 imm, swreg tmp_reg)
{
	if (FIELD_FIT(RE_REG_IMM_MAX, imm))
		return reg_imm(imm);

	wrp_immed(nfp_prog, tmp_reg, imm);
	return tmp_reg;
}

static void wrp_nops(struct nfp_prog *nfp_prog, unsigned int count)
{
	while (count--)
		emit_nop(nfp_prog);
}

static void wrp_mov(struct nfp_prog *nfp_prog, swreg dst, swreg src)
{
	emit_alu(nfp_prog, dst, reg_none(), ALU_OP_NONE, src);
}

static void wrp_reg_mov(struct nfp_prog *nfp_prog, u16 dst, u16 src)
{
	wrp_mov(nfp_prog, reg_both(dst), reg_b(src));
}

/* wrp_reg_subpart() - load @field_len bytes from @offset of @src, write the
 * result to @dst from low end.
 */
static void
wrp_reg_subpart(struct nfp_prog *nfp_prog, swreg dst, swreg src, u8 field_len,
		u8 offset)
{
	enum shf_sc sc = offset ? SHF_SC_R_SHF : SHF_SC_NONE;
	u8 mask = (1 << field_len) - 1;

	emit_ld_field_any(nfp_prog, dst, mask, src, sc, offset * 8, true);
}

/* wrp_reg_or_subpart() - load @field_len bytes from low end of @src, or the
 * result to @dst from offset, there is no change on the other bits of @dst.
 */
static void
wrp_reg_or_subpart(struct nfp_prog *nfp_prog, swreg dst, swreg src,
		   u8 field_len, u8 offset)
{
	enum shf_sc sc = offset ? SHF_SC_L_SHF : SHF_SC_NONE;
	u8 mask = ((1 << field_len) - 1) << offset;

	emit_ld_field(nfp_prog, dst, mask, src, sc, 32 - offset * 8);
}

static void
addr40_offset(struct nfp_prog *nfp_prog, u8 src_gpr, swreg offset,
	      swreg *rega, swreg *regb)
{
	if (offset == reg_imm(0)) {
		*rega = reg_a(src_gpr);
		*regb = reg_b(src_gpr + 1);
		return;
	}

	emit_alu(nfp_prog, imm_a(nfp_prog), reg_a(src_gpr), ALU_OP_ADD, offset);
	emit_alu(nfp_prog, imm_b(nfp_prog), reg_b(src_gpr + 1), ALU_OP_ADD_C,
		 reg_imm(0));
	*rega = imm_a(nfp_prog);
	*regb = imm_b(nfp_prog);
}

/* NFP has Command Push Pull bus which supports bluk memory operations. */
static int nfp_cpp_memcpy(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	bool descending_seq = meta->ldst_gather_len < 0;
	s16 len = abs(meta->ldst_gather_len);
	swreg src_base, off;
	bool src_40bit_addr;
	unsigned int i;
	u8 xfer_num;

	off = re_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));
	src_40bit_addr = meta->ptr.type == PTR_TO_MAP_VALUE;
	src_base = reg_a(meta->insn.src_reg * 2);
	xfer_num = round_up(len, 4) / 4;

	if (src_40bit_addr)
		addr40_offset(nfp_prog, meta->insn.src_reg, off, &src_base,
			      &off);

	/* Setup PREV_ALU fields to override memory read length. */
	if (len > 32)
		wrp_immed(nfp_prog, reg_none(),
			  CMD_OVE_LEN | FIELD_PREP(CMD_OV_LEN, xfer_num - 1));

	/* Memory read from source addr into transfer-in registers. */
	emit_cmd_any(nfp_prog, CMD_TGT_READ32_SWAP,
		     src_40bit_addr ? CMD_MODE_40b_BA : CMD_MODE_32b, 0,
		     src_base, off, xfer_num - 1, CMD_CTX_SWAP, len > 32);

	/* Move from transfer-in to transfer-out. */
	for (i = 0; i < xfer_num; i++)
		wrp_mov(nfp_prog, reg_xfer(i), reg_xfer(i));

	off = re_load_imm_any(nfp_prog, meta->paired_st->off, imm_b(nfp_prog));

	if (len <= 8) {
		/* Use single direct_ref write8. */
		emit_cmd(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b, 0,
			 reg_a(meta->paired_st->dst_reg * 2), off, len - 1,
			 CMD_CTX_SWAP);
	} else if (len <= 32 && IS_ALIGNED(len, 4)) {
		/* Use single direct_ref write32. */
		emit_cmd(nfp_prog, CMD_TGT_WRITE32_SWAP, CMD_MODE_32b, 0,
			 reg_a(meta->paired_st->dst_reg * 2), off, xfer_num - 1,
			 CMD_CTX_SWAP);
	} else if (len <= 32) {
		/* Use single indirect_ref write8. */
		wrp_immed(nfp_prog, reg_none(),
			  CMD_OVE_LEN | FIELD_PREP(CMD_OV_LEN, len - 1));
		emit_cmd_indir(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b, 0,
			       reg_a(meta->paired_st->dst_reg * 2), off,
			       len - 1, CMD_CTX_SWAP);
	} else if (IS_ALIGNED(len, 4)) {
		/* Use single indirect_ref write32. */
		wrp_immed(nfp_prog, reg_none(),
			  CMD_OVE_LEN | FIELD_PREP(CMD_OV_LEN, xfer_num - 1));
		emit_cmd_indir(nfp_prog, CMD_TGT_WRITE32_SWAP, CMD_MODE_32b, 0,
			       reg_a(meta->paired_st->dst_reg * 2), off,
			       xfer_num - 1, CMD_CTX_SWAP);
	} else if (len <= 40) {
		/* Use one direct_ref write32 to write the first 32-bytes, then
		 * another direct_ref write8 to write the remaining bytes.
		 */
		emit_cmd(nfp_prog, CMD_TGT_WRITE32_SWAP, CMD_MODE_32b, 0,
			 reg_a(meta->paired_st->dst_reg * 2), off, 7,
			 CMD_CTX_SWAP);

		off = re_load_imm_any(nfp_prog, meta->paired_st->off + 32,
				      imm_b(nfp_prog));
		emit_cmd(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b, 8,
			 reg_a(meta->paired_st->dst_reg * 2), off, len - 33,
			 CMD_CTX_SWAP);
	} else {
		/* Use one indirect_ref write32 to write 4-bytes aligned length,
		 * then another direct_ref write8 to write the remaining bytes.
		 */
		u8 new_off;

		wrp_immed(nfp_prog, reg_none(),
			  CMD_OVE_LEN | FIELD_PREP(CMD_OV_LEN, xfer_num - 2));
		emit_cmd_indir(nfp_prog, CMD_TGT_WRITE32_SWAP, CMD_MODE_32b, 0,
			       reg_a(meta->paired_st->dst_reg * 2), off,
			       xfer_num - 2, CMD_CTX_SWAP);
		new_off = meta->paired_st->off + (xfer_num - 1) * 4;
		off = re_load_imm_any(nfp_prog, new_off, imm_b(nfp_prog));
		emit_cmd(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b,
			 xfer_num - 1, reg_a(meta->paired_st->dst_reg * 2), off,
			 (len & 0x3) - 1, CMD_CTX_SWAP);
	}

	/* TODO: The following extra load is to make sure data flow be identical
	 *  before and after we do memory copy optimization.
	 *
	 *  The load destination register is not guaranteed to be dead, so we
	 *  need to make sure it is loaded with the value the same as before
	 *  this transformation.
	 *
	 *  These extra loads could be removed once we have accurate register
	 *  usage information.
	 */
	if (descending_seq)
		xfer_num = 0;
	else if (BPF_SIZE(meta->insn.code) != BPF_DW)
		xfer_num = xfer_num - 1;
	else
		xfer_num = xfer_num - 2;

	switch (BPF_SIZE(meta->insn.code)) {
	case BPF_B:
		wrp_reg_subpart(nfp_prog, reg_both(meta->insn.dst_reg * 2),
				reg_xfer(xfer_num), 1,
				IS_ALIGNED(len, 4) ? 3 : (len & 3) - 1);
		break;
	case BPF_H:
		wrp_reg_subpart(nfp_prog, reg_both(meta->insn.dst_reg * 2),
				reg_xfer(xfer_num), 2, (len & 3) ^ 2);
		break;
	case BPF_W:
		wrp_mov(nfp_prog, reg_both(meta->insn.dst_reg * 2),
			reg_xfer(0));
		break;
	case BPF_DW:
		wrp_mov(nfp_prog, reg_both(meta->insn.dst_reg * 2),
			reg_xfer(xfer_num));
		wrp_mov(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1),
			reg_xfer(xfer_num + 1));
		break;
	}

	if (BPF_SIZE(meta->insn.code) != BPF_DW)
		wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return 0;
}

static int
data_ld(struct nfp_prog *nfp_prog, swreg offset, u8 dst_gpr, int size)
{
	unsigned int i;
	u16 shift, sz;

	/* We load the value from the address indicated in @offset and then
	 * shift out the data we don't need.  Note: this is big endian!
	 */
	sz = max(size, 4);
	shift = size < 4 ? 4 - size : 0;

	emit_cmd(nfp_prog, CMD_TGT_READ8, CMD_MODE_32b, 0,
		 pptr_reg(nfp_prog), offset, sz - 1, CMD_CTX_SWAP);

	i = 0;
	if (shift)
		emit_shf(nfp_prog, reg_both(dst_gpr), reg_none(), SHF_OP_NONE,
			 reg_xfer(0), SHF_SC_R_SHF, shift * 8);
	else
		for (; i * 4 < size; i++)
			wrp_mov(nfp_prog, reg_both(dst_gpr + i), reg_xfer(i));

	if (i < 2)
		wrp_immed(nfp_prog, reg_both(dst_gpr + 1), 0);

	return 0;
}

static int
data_ld_host_order(struct nfp_prog *nfp_prog, u8 dst_gpr,
		   swreg lreg, swreg rreg, int size, enum cmd_mode mode)
{
	unsigned int i;
	u8 mask, sz;

	/* We load the value from the address indicated in rreg + lreg and then
	 * mask out the data we don't need.  Note: this is little endian!
	 */
	sz = max(size, 4);
	mask = size < 4 ? GENMASK(size - 1, 0) : 0;

	emit_cmd(nfp_prog, CMD_TGT_READ32_SWAP, mode, 0,
		 lreg, rreg, sz / 4 - 1, CMD_CTX_SWAP);

	i = 0;
	if (mask)
		emit_ld_field_any(nfp_prog, reg_both(dst_gpr), mask,
				  reg_xfer(0), SHF_SC_NONE, 0, true);
	else
		for (; i * 4 < size; i++)
			wrp_mov(nfp_prog, reg_both(dst_gpr + i), reg_xfer(i));

	if (i < 2)
		wrp_immed(nfp_prog, reg_both(dst_gpr + 1), 0);

	return 0;
}

static int
data_ld_host_order_addr32(struct nfp_prog *nfp_prog, u8 src_gpr, swreg offset,
			  u8 dst_gpr, u8 size)
{
	return data_ld_host_order(nfp_prog, dst_gpr, reg_a(src_gpr), offset,
				  size, CMD_MODE_32b);
}

static int
data_ld_host_order_addr40(struct nfp_prog *nfp_prog, u8 src_gpr, swreg offset,
			  u8 dst_gpr, u8 size)
{
	swreg rega, regb;

	addr40_offset(nfp_prog, src_gpr, offset, &rega, &regb);

	return data_ld_host_order(nfp_prog, dst_gpr, rega, regb,
				  size, CMD_MODE_40b_BA);
}

static int
construct_data_ind_ld(struct nfp_prog *nfp_prog, u16 offset, u16 src, u8 size)
{
	swreg tmp_reg;

	/* Calculate the true offset (src_reg + imm) */
	tmp_reg = ur_load_imm_any(nfp_prog, offset, imm_b(nfp_prog));
	emit_alu(nfp_prog, imm_both(nfp_prog), reg_a(src), ALU_OP_ADD, tmp_reg);

	/* Check packet length (size guaranteed to fit b/c it's u8) */
	emit_alu(nfp_prog, imm_a(nfp_prog),
		 imm_a(nfp_prog), ALU_OP_ADD, reg_imm(size));
	emit_alu(nfp_prog, reg_none(),
		 plen_reg(nfp_prog), ALU_OP_SUB, imm_a(nfp_prog));
	emit_br_relo(nfp_prog, BR_BLO, BR_OFF_RELO, 0, RELO_BR_GO_ABORT);

	/* Load data */
	return data_ld(nfp_prog, imm_b(nfp_prog), 0, size);
}

static int construct_data_ld(struct nfp_prog *nfp_prog, u16 offset, u8 size)
{
	swreg tmp_reg;

	/* Check packet length */
	tmp_reg = ur_load_imm_any(nfp_prog, offset + size, imm_a(nfp_prog));
	emit_alu(nfp_prog, reg_none(), plen_reg(nfp_prog), ALU_OP_SUB, tmp_reg);
	emit_br_relo(nfp_prog, BR_BLO, BR_OFF_RELO, 0, RELO_BR_GO_ABORT);

	/* Load data */
	tmp_reg = re_load_imm_any(nfp_prog, offset, imm_b(nfp_prog));
	return data_ld(nfp_prog, tmp_reg, 0, size);
}

static int
data_stx_host_order(struct nfp_prog *nfp_prog, u8 dst_gpr, swreg offset,
		    u8 src_gpr, u8 size)
{
	unsigned int i;

	for (i = 0; i * 4 < size; i++)
		wrp_mov(nfp_prog, reg_xfer(i), reg_a(src_gpr + i));

	emit_cmd(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b, 0,
		 reg_a(dst_gpr), offset, size - 1, CMD_CTX_SWAP);

	return 0;
}

static int
data_st_host_order(struct nfp_prog *nfp_prog, u8 dst_gpr, swreg offset,
		   u64 imm, u8 size)
{
	wrp_immed(nfp_prog, reg_xfer(0), imm);
	if (size == 8)
		wrp_immed(nfp_prog, reg_xfer(1), imm >> 32);

	emit_cmd(nfp_prog, CMD_TGT_WRITE8_SWAP, CMD_MODE_32b, 0,
		 reg_a(dst_gpr), offset, size - 1, CMD_CTX_SWAP);

	return 0;
}

typedef int
(*lmem_step)(struct nfp_prog *nfp_prog, u8 gpr, u8 gpr_byte, s32 off,
	     unsigned int size, bool first, bool new_gpr, bool last, bool lm3,
	     bool needs_inc);

static int
wrp_lmem_load(struct nfp_prog *nfp_prog, u8 dst, u8 dst_byte, s32 off,
	      unsigned int size, bool first, bool new_gpr, bool last, bool lm3,
	      bool needs_inc)
{
	bool should_inc = needs_inc && new_gpr && !last;
	u32 idx, src_byte;
	enum shf_sc sc;
	swreg reg;
	int shf;
	u8 mask;

	if (WARN_ON_ONCE(dst_byte + size > 4 || off % 4 + size > 4))
		return -EOPNOTSUPP;

	idx = off / 4;

	/* Move the entire word */
	if (size == 4) {
		wrp_mov(nfp_prog, reg_both(dst),
			should_inc ? reg_lm_inc(3) : reg_lm(lm3 ? 3 : 0, idx));
		return 0;
	}

	if (WARN_ON_ONCE(lm3 && idx > RE_REG_LM_IDX_MAX))
		return -EOPNOTSUPP;

	src_byte = off % 4;

	mask = (1 << size) - 1;
	mask <<= dst_byte;

	if (WARN_ON_ONCE(mask > 0xf))
		return -EOPNOTSUPP;

	shf = abs(src_byte - dst_byte) * 8;
	if (src_byte == dst_byte) {
		sc = SHF_SC_NONE;
	} else if (src_byte < dst_byte) {
		shf = 32 - shf;
		sc = SHF_SC_L_SHF;
	} else {
		sc = SHF_SC_R_SHF;
	}

	/* ld_field can address fewer indexes, if offset too large do RMW.
	 * Because we RMV twice we waste 2 cycles on unaligned 8 byte writes.
	 */
	if (idx <= RE_REG_LM_IDX_MAX) {
		reg = reg_lm(lm3 ? 3 : 0, idx);
	} else {
		reg = imm_a(nfp_prog);
		/* If it's not the first part of the load and we start a new GPR
		 * that means we are loading a second part of the LMEM word into
		 * a new GPR.  IOW we've already looked that LMEM word and
		 * therefore it has been loaded into imm_a().
		 */
		if (first || !new_gpr)
			wrp_mov(nfp_prog, reg, reg_lm(0, idx));
	}

	emit_ld_field_any(nfp_prog, reg_both(dst), mask, reg, sc, shf, new_gpr);

	if (should_inc)
		wrp_mov(nfp_prog, reg_none(), reg_lm_inc(3));

	return 0;
}

static int
wrp_lmem_store(struct nfp_prog *nfp_prog, u8 src, u8 src_byte, s32 off,
	       unsigned int size, bool first, bool new_gpr, bool last, bool lm3,
	       bool needs_inc)
{
	bool should_inc = needs_inc && new_gpr && !last;
	u32 idx, dst_byte;
	enum shf_sc sc;
	swreg reg;
	int shf;
	u8 mask;

	if (WARN_ON_ONCE(src_byte + size > 4 || off % 4 + size > 4))
		return -EOPNOTSUPP;

	idx = off / 4;

	/* Move the entire word */
	if (size == 4) {
		wrp_mov(nfp_prog,
			should_inc ? reg_lm_inc(3) : reg_lm(lm3 ? 3 : 0, idx),
			reg_b(src));
		return 0;
	}

	if (WARN_ON_ONCE(lm3 && idx > RE_REG_LM_IDX_MAX))
		return -EOPNOTSUPP;

	dst_byte = off % 4;

	mask = (1 << size) - 1;
	mask <<= dst_byte;

	if (WARN_ON_ONCE(mask > 0xf))
		return -EOPNOTSUPP;

	shf = abs(src_byte - dst_byte) * 8;
	if (src_byte == dst_byte) {
		sc = SHF_SC_NONE;
	} else if (src_byte < dst_byte) {
		shf = 32 - shf;
		sc = SHF_SC_L_SHF;
	} else {
		sc = SHF_SC_R_SHF;
	}

	/* ld_field can address fewer indexes, if offset too large do RMW.
	 * Because we RMV twice we waste 2 cycles on unaligned 8 byte writes.
	 */
	if (idx <= RE_REG_LM_IDX_MAX) {
		reg = reg_lm(lm3 ? 3 : 0, idx);
	} else {
		reg = imm_a(nfp_prog);
		/* Only first and last LMEM locations are going to need RMW,
		 * the middle location will be overwritten fully.
		 */
		if (first || last)
			wrp_mov(nfp_prog, reg, reg_lm(0, idx));
	}

	emit_ld_field(nfp_prog, reg, mask, reg_b(src), sc, shf);

	if (new_gpr || last) {
		if (idx > RE_REG_LM_IDX_MAX)
			wrp_mov(nfp_prog, reg_lm(0, idx), reg);
		if (should_inc)
			wrp_mov(nfp_prog, reg_none(), reg_lm_inc(3));
	}

	return 0;
}

static int
mem_op_stack(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	     unsigned int size, unsigned int ptr_off, u8 gpr, u8 ptr_gpr,
	     bool clr_gpr, lmem_step step)
{
	s32 off = nfp_prog->stack_depth + meta->insn.off + ptr_off;
	bool first = true, last;
	bool needs_inc = false;
	swreg stack_off_reg;
	u8 prev_gpr = 255;
	u32 gpr_byte = 0;
	bool lm3 = true;
	int ret;

	if (meta->ptr_not_const) {
		/* Use of the last encountered ptr_off is OK, they all have
		 * the same alignment.  Depend on low bits of value being
		 * discarded when written to LMaddr register.
		 */
		stack_off_reg = ur_load_imm_any(nfp_prog, meta->insn.off,
						stack_imm(nfp_prog));

		emit_alu(nfp_prog, imm_b(nfp_prog),
			 reg_a(ptr_gpr), ALU_OP_ADD, stack_off_reg);

		needs_inc = true;
	} else if (off + size <= 64) {
		/* We can reach bottom 64B with LMaddr0 */
		lm3 = false;
	} else if (round_down(off, 32) == round_down(off + size - 1, 32)) {
		/* We have to set up a new pointer.  If we know the offset
		 * and the entire access falls into a single 32 byte aligned
		 * window we won't have to increment the LM pointer.
		 * The 32 byte alignment is imporant because offset is ORed in
		 * not added when doing *l$indexN[off].
		 */
		stack_off_reg = ur_load_imm_any(nfp_prog, round_down(off, 32),
						stack_imm(nfp_prog));
		emit_alu(nfp_prog, imm_b(nfp_prog),
			 stack_reg(nfp_prog), ALU_OP_ADD, stack_off_reg);

		off %= 32;
	} else {
		stack_off_reg = ur_load_imm_any(nfp_prog, round_down(off, 4),
						stack_imm(nfp_prog));

		emit_alu(nfp_prog, imm_b(nfp_prog),
			 stack_reg(nfp_prog), ALU_OP_ADD, stack_off_reg);

		needs_inc = true;
	}
	if (lm3) {
		emit_csr_wr(nfp_prog, imm_b(nfp_prog), NFP_CSR_ACT_LM_ADDR3);
		/* For size < 4 one slot will be filled by zeroing of upper. */
		wrp_nops(nfp_prog, clr_gpr && size < 8 ? 2 : 3);
	}

	if (clr_gpr && size < 8)
		wrp_immed(nfp_prog, reg_both(gpr + 1), 0);

	while (size) {
		u32 slice_end;
		u8 slice_size;

		slice_size = min(size, 4 - gpr_byte);
		slice_end = min(off + slice_size, round_up(off + 1, 4));
		slice_size = slice_end - off;

		last = slice_size == size;

		if (needs_inc)
			off %= 4;

		ret = step(nfp_prog, gpr, gpr_byte, off, slice_size,
			   first, gpr != prev_gpr, last, lm3, needs_inc);
		if (ret)
			return ret;

		prev_gpr = gpr;
		first = false;

		gpr_byte += slice_size;
		if (gpr_byte >= 4) {
			gpr_byte -= 4;
			gpr++;
		}

		size -= slice_size;
		off += slice_size;
	}

	return 0;
}

static void
wrp_alu_imm(struct nfp_prog *nfp_prog, u8 dst, enum alu_op alu_op, u32 imm)
{
	swreg tmp_reg;

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
				 ALU_OP_NOT, reg_b(dst));
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

	wrp_test_reg_one(nfp_prog, insn->dst_reg * 2, alu_op,
			 insn->src_reg * 2, br_mask, insn->off);
	wrp_test_reg_one(nfp_prog, insn->dst_reg * 2 + 1, alu_op,
			 insn->src_reg * 2 + 1, br_mask, insn->off);

	return 0;
}

static const struct jmp_code_map {
	enum br_mask br_mask;
	bool swap;
} jmp_code_map[] = {
	[BPF_JGT >> 4]	= { BR_BLO, true },
	[BPF_JGE >> 4]	= { BR_BHS, false },
	[BPF_JLT >> 4]	= { BR_BLO, false },
	[BPF_JLE >> 4]	= { BR_BHS, true },
	[BPF_JSGT >> 4]	= { BR_BLT, true },
	[BPF_JSGE >> 4]	= { BR_BGE, false },
	[BPF_JSLT >> 4]	= { BR_BLT, false },
	[BPF_JSLE >> 4]	= { BR_BGE, true },
};

static const struct jmp_code_map *nfp_jmp_code_get(struct nfp_insn_meta *meta)
{
	unsigned int op;

	op = BPF_OP(meta->insn.code) >> 4;
	/* br_mask of 0 is BR_BEQ which we don't use in jump code table */
	if (WARN_ONCE(op >= ARRAY_SIZE(jmp_code_map) ||
		      !jmp_code_map[op].br_mask,
		      "no code found for jump instruction"))
		return NULL;

	return &jmp_code_map[op];
}

static int cmp_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	const struct jmp_code_map *code;
	enum alu_op alu_op, carry_op;
	u8 reg = insn->dst_reg * 2;
	swreg tmp_reg;

	code = nfp_jmp_code_get(meta);
	if (!code)
		return -EINVAL;

	alu_op = meta->jump_neg_op ? ALU_OP_ADD : ALU_OP_SUB;
	carry_op = meta->jump_neg_op ? ALU_OP_ADD_C : ALU_OP_SUB_C;

	tmp_reg = ur_load_imm_any(nfp_prog, imm & ~0U, imm_b(nfp_prog));
	if (!code->swap)
		emit_alu(nfp_prog, reg_none(), reg_a(reg), alu_op, tmp_reg);
	else
		emit_alu(nfp_prog, reg_none(), tmp_reg, alu_op, reg_a(reg));

	tmp_reg = ur_load_imm_any(nfp_prog, imm >> 32, imm_b(nfp_prog));
	if (!code->swap)
		emit_alu(nfp_prog, reg_none(),
			 reg_a(reg + 1), carry_op, tmp_reg);
	else
		emit_alu(nfp_prog, reg_none(),
			 tmp_reg, carry_op, reg_a(reg + 1));

	emit_br(nfp_prog, code->br_mask, insn->off, 0);

	return 0;
}

static int cmp_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	const struct jmp_code_map *code;
	u8 areg, breg;

	code = nfp_jmp_code_get(meta);
	if (!code)
		return -EINVAL;

	areg = insn->dst_reg * 2;
	breg = insn->src_reg * 2;

	if (code->swap) {
		areg ^= breg;
		breg ^= areg;
		areg ^= breg;
	}

	emit_alu(nfp_prog, reg_none(), reg_a(areg), ALU_OP_SUB, reg_b(breg));
	emit_alu(nfp_prog, reg_none(),
		 reg_a(areg + 1), ALU_OP_SUB_C, reg_b(breg + 1));
	emit_br(nfp_prog, code->br_mask, insn->off, 0);

	return 0;
}

static void wrp_end32(struct nfp_prog *nfp_prog, swreg reg_in, u8 gpr_out)
{
	emit_ld_field(nfp_prog, reg_both(gpr_out), 0xf, reg_in,
		      SHF_SC_R_ROT, 8);
	emit_ld_field(nfp_prog, reg_both(gpr_out), 0x5, reg_a(gpr_out),
		      SHF_SC_R_ROT, 16);
}

static int adjust_head(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	swreg tmp = imm_a(nfp_prog), tmp_len = imm_b(nfp_prog);
	struct nfp_bpf_cap_adjust_head *adjust_head;
	u32 ret_einval, end;

	adjust_head = &nfp_prog->bpf->adjust_head;

	/* Optimized version - 5 vs 14 cycles */
	if (nfp_prog->adjust_head_location != UINT_MAX) {
		if (WARN_ON_ONCE(nfp_prog->adjust_head_location != meta->n))
			return -EINVAL;

		emit_alu(nfp_prog, pptr_reg(nfp_prog),
			 reg_a(2 * 2), ALU_OP_ADD, pptr_reg(nfp_prog));
		emit_alu(nfp_prog, plen_reg(nfp_prog),
			 plen_reg(nfp_prog), ALU_OP_SUB, reg_a(2 * 2));
		emit_alu(nfp_prog, pv_len(nfp_prog),
			 pv_len(nfp_prog), ALU_OP_SUB, reg_a(2 * 2));

		wrp_immed(nfp_prog, reg_both(0), 0);
		wrp_immed(nfp_prog, reg_both(1), 0);

		/* TODO: when adjust head is guaranteed to succeed we can
		 * also eliminate the following if (r0 == 0) branch.
		 */

		return 0;
	}

	ret_einval = nfp_prog_current_offset(nfp_prog) + 14;
	end = ret_einval + 2;

	/* We need to use a temp because offset is just a part of the pkt ptr */
	emit_alu(nfp_prog, tmp,
		 reg_a(2 * 2), ALU_OP_ADD_2B, pptr_reg(nfp_prog));

	/* Validate result will fit within FW datapath constraints */
	emit_alu(nfp_prog, reg_none(),
		 tmp, ALU_OP_SUB, reg_imm(adjust_head->off_min));
	emit_br(nfp_prog, BR_BLO, ret_einval, 0);
	emit_alu(nfp_prog, reg_none(),
		 reg_imm(adjust_head->off_max), ALU_OP_SUB, tmp);
	emit_br(nfp_prog, BR_BLO, ret_einval, 0);

	/* Validate the length is at least ETH_HLEN */
	emit_alu(nfp_prog, tmp_len,
		 plen_reg(nfp_prog), ALU_OP_SUB, reg_a(2 * 2));
	emit_alu(nfp_prog, reg_none(),
		 tmp_len, ALU_OP_SUB, reg_imm(ETH_HLEN));
	emit_br(nfp_prog, BR_BMI, ret_einval, 0);

	/* Load the ret code */
	wrp_immed(nfp_prog, reg_both(0), 0);
	wrp_immed(nfp_prog, reg_both(1), 0);

	/* Modify the packet metadata */
	emit_ld_field(nfp_prog, pptr_reg(nfp_prog), 0x3, tmp, SHF_SC_NONE, 0);

	/* Skip over the -EINVAL ret code (defer 2) */
	emit_br(nfp_prog, BR_UNC, end, 2);

	emit_alu(nfp_prog, plen_reg(nfp_prog),
		 plen_reg(nfp_prog), ALU_OP_SUB, reg_a(2 * 2));
	emit_alu(nfp_prog, pv_len(nfp_prog),
		 pv_len(nfp_prog), ALU_OP_SUB, reg_a(2 * 2));

	/* return -EINVAL target */
	if (!nfp_prog_confirm_current_offset(nfp_prog, ret_einval))
		return -EINVAL;

	wrp_immed(nfp_prog, reg_both(0), -22);
	wrp_immed(nfp_prog, reg_both(1), ~0);

	if (!nfp_prog_confirm_current_offset(nfp_prog, end))
		return -EINVAL;

	return 0;
}

static int
map_call_stack_common(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	bool load_lm_ptr;
	u32 ret_tgt;
	s64 lm_off;

	/* We only have to reload LM0 if the key is not at start of stack */
	lm_off = nfp_prog->stack_depth;
	lm_off += meta->arg2.reg.var_off.value + meta->arg2.reg.off;
	load_lm_ptr = meta->arg2.var_off || lm_off;

	/* Set LM0 to start of key */
	if (load_lm_ptr)
		emit_csr_wr(nfp_prog, reg_b(2 * 2), NFP_CSR_ACT_LM_ADDR0);
	if (meta->func_id == BPF_FUNC_map_update_elem)
		emit_csr_wr(nfp_prog, reg_b(3 * 2), NFP_CSR_ACT_LM_ADDR2);

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO + meta->func_id,
		     2, RELO_BR_HELPER);
	ret_tgt = nfp_prog_current_offset(nfp_prog) + 2;

	/* Load map ID into A0 */
	wrp_mov(nfp_prog, reg_a(0), reg_a(2));

	/* Load the return address into B0 */
	wrp_immed_relo(nfp_prog, reg_b(0), ret_tgt, RELO_IMMED_REL);

	if (!nfp_prog_confirm_current_offset(nfp_prog, ret_tgt))
		return -EINVAL;

	/* Reset the LM0 pointer */
	if (!load_lm_ptr)
		return 0;

	emit_csr_wr(nfp_prog, stack_reg(nfp_prog), NFP_CSR_ACT_LM_ADDR0);
	wrp_nops(nfp_prog, 3);

	return 0;
}

static int
nfp_get_prandom_u32(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	__emit_csr_rd(nfp_prog, NFP_CSR_PSEUDO_RND_NUM);
	/* CSR value is read in following immed[gpr, 0] */
	emit_immed(nfp_prog, reg_both(0), 0,
		   IMMED_WIDTH_ALL, false, IMMED_SHIFT_0B);
	emit_immed(nfp_prog, reg_both(1), 0,
		   IMMED_WIDTH_ALL, false, IMMED_SHIFT_0B);
	return 0;
}

static int
nfp_perf_event_output(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	swreg ptr_type;
	u32 ret_tgt;

	ptr_type = ur_load_imm_any(nfp_prog, meta->arg1.type, imm_a(nfp_prog));

	ret_tgt = nfp_prog_current_offset(nfp_prog) + 3;

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO + meta->func_id,
		     2, RELO_BR_HELPER);

	/* Load ptr type into A1 */
	wrp_mov(nfp_prog, reg_a(1), ptr_type);

	/* Load the return address into B0 */
	wrp_immed_relo(nfp_prog, reg_b(0), ret_tgt, RELO_IMMED_REL);

	if (!nfp_prog_confirm_current_offset(nfp_prog, ret_tgt))
		return -EINVAL;

	return 0;
}

static int
nfp_queue_select(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	u32 jmp_tgt;

	jmp_tgt = nfp_prog_current_offset(nfp_prog) + 5;

	/* Make sure the queue id fits into FW field */
	emit_alu(nfp_prog, reg_none(), reg_a(meta->insn.src_reg * 2),
		 ALU_OP_AND_NOT_B, reg_imm(0xff));
	emit_br(nfp_prog, BR_BEQ, jmp_tgt, 2);

	/* Set the 'queue selected' bit and the queue value */
	emit_shf(nfp_prog, pv_qsel_set(nfp_prog),
		 pv_qsel_set(nfp_prog), SHF_OP_OR, reg_imm(1),
		 SHF_SC_L_SHF, PKT_VEL_QSEL_SET_BIT);
	emit_ld_field(nfp_prog,
		      pv_qsel_val(nfp_prog), 0x1, reg_b(meta->insn.src_reg * 2),
		      SHF_SC_NONE, 0);
	/* Delay slots end here, we will jump over next instruction if queue
	 * value fits into the field.
	 */
	emit_ld_field(nfp_prog,
		      pv_qsel_val(nfp_prog), 0x1, reg_imm(NFP_NET_RXR_MAX),
		      SHF_SC_NONE, 0);

	if (!nfp_prog_confirm_current_offset(nfp_prog, jmp_tgt))
		return -EINVAL;

	return 0;
}

/* --- Callbacks --- */
static int mov_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;
	u8 src = insn->src_reg * 2;

	if (insn->src_reg == BPF_REG_10) {
		swreg stack_depth_reg;

		stack_depth_reg = ur_load_imm_any(nfp_prog,
						  nfp_prog->stack_depth,
						  stack_imm(nfp_prog));
		emit_alu(nfp_prog, reg_both(dst),
			 stack_reg(nfp_prog), ALU_OP_ADD, stack_depth_reg);
		wrp_immed(nfp_prog, reg_both(dst + 1), 0);
	} else {
		wrp_reg_mov(nfp_prog, dst, src);
		wrp_reg_mov(nfp_prog, dst + 1, src + 1);
	}

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

static int neg_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;

	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2), reg_imm(0),
		 ALU_OP_SUB, reg_b(insn->dst_reg * 2));
	emit_alu(nfp_prog, reg_both(insn->dst_reg * 2 + 1), reg_imm(0),
		 ALU_OP_SUB_C, reg_b(insn->dst_reg * 2 + 1));

	return 0;
}

/* Pseudo code:
 *   if shift_amt >= 32
 *     dst_high = dst_low << shift_amt[4:0]
 *     dst_low = 0;
 *   else
 *     dst_high = (dst_high, dst_low) >> (32 - shift_amt)
 *     dst_low = dst_low << shift_amt
 *
 * The indirect shift will use the same logic at runtime.
 */
static int __shl_imm64(struct nfp_prog *nfp_prog, u8 dst, u8 shift_amt)
{
	if (shift_amt < 32) {
		emit_shf(nfp_prog, reg_both(dst + 1), reg_a(dst + 1),
			 SHF_OP_NONE, reg_b(dst), SHF_SC_R_DSHF,
			 32 - shift_amt);
		emit_shf(nfp_prog, reg_both(dst), reg_none(), SHF_OP_NONE,
			 reg_b(dst), SHF_SC_L_SHF, shift_amt);
	} else if (shift_amt == 32) {
		wrp_reg_mov(nfp_prog, dst + 1, dst);
		wrp_immed(nfp_prog, reg_both(dst), 0);
	} else if (shift_amt > 32) {
		emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_NONE,
			 reg_b(dst), SHF_SC_L_SHF, shift_amt - 32);
		wrp_immed(nfp_prog, reg_both(dst), 0);
	}

	return 0;
}

static int shl_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;

	return __shl_imm64(nfp_prog, dst, insn->imm);
}

static void shl_reg64_lt32_high(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, imm_both(nfp_prog), reg_imm(32), ALU_OP_SUB,
		 reg_b(src));
	emit_alu(nfp_prog, reg_none(), imm_a(nfp_prog), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst + 1), reg_a(dst + 1), SHF_OP_NONE,
		       reg_b(dst), SHF_SC_R_DSHF);
}

/* NOTE: for indirect left shift, HIGH part should be calculated first. */
static void shl_reg64_lt32_low(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst), reg_none(), SHF_OP_NONE,
		       reg_b(dst), SHF_SC_L_SHF);
}

static void shl_reg64_lt32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	shl_reg64_lt32_high(nfp_prog, dst, src);
	shl_reg64_lt32_low(nfp_prog, dst, src);
}

static void shl_reg64_ge32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_NONE,
		       reg_b(dst), SHF_SC_L_SHF);
	wrp_immed(nfp_prog, reg_both(dst), 0);
}

static int shl_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 umin, umax;
	u8 dst, src;

	dst = insn->dst_reg * 2;
	umin = meta->umin;
	umax = meta->umax;
	if (umin == umax)
		return __shl_imm64(nfp_prog, dst, umin);

	src = insn->src_reg * 2;
	if (umax < 32) {
		shl_reg64_lt32(nfp_prog, dst, src);
	} else if (umin >= 32) {
		shl_reg64_ge32(nfp_prog, dst, src);
	} else {
		/* Generate different instruction sequences depending on runtime
		 * value of shift amount.
		 */
		u16 label_ge32, label_end;

		label_ge32 = nfp_prog_current_offset(nfp_prog) + 7;
		emit_br_bset(nfp_prog, reg_a(src), 5, label_ge32, 0);

		shl_reg64_lt32_high(nfp_prog, dst, src);
		label_end = nfp_prog_current_offset(nfp_prog) + 6;
		emit_br(nfp_prog, BR_UNC, label_end, 2);
		/* shl_reg64_lt32_low packed in delay slot. */
		shl_reg64_lt32_low(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_ge32))
			return -EINVAL;
		shl_reg64_ge32(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_end))
			return -EINVAL;
	}

	return 0;
}

/* Pseudo code:
 *   if shift_amt >= 32
 *     dst_high = 0;
 *     dst_low = dst_high >> shift_amt[4:0]
 *   else
 *     dst_high = dst_high >> shift_amt
 *     dst_low = (dst_high, dst_low) >> shift_amt
 *
 * The indirect shift will use the same logic at runtime.
 */
static int __shr_imm64(struct nfp_prog *nfp_prog, u8 dst, u8 shift_amt)
{
	if (shift_amt < 32) {
		emit_shf(nfp_prog, reg_both(dst), reg_a(dst + 1), SHF_OP_NONE,
			 reg_b(dst), SHF_SC_R_DSHF, shift_amt);
		emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_NONE,
			 reg_b(dst + 1), SHF_SC_R_SHF, shift_amt);
	} else if (shift_amt == 32) {
		wrp_reg_mov(nfp_prog, dst, dst + 1);
		wrp_immed(nfp_prog, reg_both(dst + 1), 0);
	} else if (shift_amt > 32) {
		emit_shf(nfp_prog, reg_both(dst), reg_none(), SHF_OP_NONE,
			 reg_b(dst + 1), SHF_SC_R_SHF, shift_amt - 32);
		wrp_immed(nfp_prog, reg_both(dst + 1), 0);
	}

	return 0;
}

static int shr_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;

	return __shr_imm64(nfp_prog, dst, insn->imm);
}

/* NOTE: for indirect right shift, LOW part should be calculated first. */
static void shr_reg64_lt32_high(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_NONE,
		       reg_b(dst + 1), SHF_SC_R_SHF);
}

static void shr_reg64_lt32_low(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst), reg_a(dst + 1), SHF_OP_NONE,
		       reg_b(dst), SHF_SC_R_DSHF);
}

static void shr_reg64_lt32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	shr_reg64_lt32_low(nfp_prog, dst, src);
	shr_reg64_lt32_high(nfp_prog, dst, src);
}

static void shr_reg64_ge32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_imm(0));
	emit_shf_indir(nfp_prog, reg_both(dst), reg_none(), SHF_OP_NONE,
		       reg_b(dst + 1), SHF_SC_R_SHF);
	wrp_immed(nfp_prog, reg_both(dst + 1), 0);
}

static int shr_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 umin, umax;
	u8 dst, src;

	dst = insn->dst_reg * 2;
	umin = meta->umin;
	umax = meta->umax;
	if (umin == umax)
		return __shr_imm64(nfp_prog, dst, umin);

	src = insn->src_reg * 2;
	if (umax < 32) {
		shr_reg64_lt32(nfp_prog, dst, src);
	} else if (umin >= 32) {
		shr_reg64_ge32(nfp_prog, dst, src);
	} else {
		/* Generate different instruction sequences depending on runtime
		 * value of shift amount.
		 */
		u16 label_ge32, label_end;

		label_ge32 = nfp_prog_current_offset(nfp_prog) + 6;
		emit_br_bset(nfp_prog, reg_a(src), 5, label_ge32, 0);
		shr_reg64_lt32_low(nfp_prog, dst, src);
		label_end = nfp_prog_current_offset(nfp_prog) + 6;
		emit_br(nfp_prog, BR_UNC, label_end, 2);
		/* shr_reg64_lt32_high packed in delay slot. */
		shr_reg64_lt32_high(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_ge32))
			return -EINVAL;
		shr_reg64_ge32(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_end))
			return -EINVAL;
	}

	return 0;
}

/* Code logic is the same as __shr_imm64 except ashr requires signedness bit
 * told through PREV_ALU result.
 */
static int __ashr_imm64(struct nfp_prog *nfp_prog, u8 dst, u8 shift_amt)
{
	if (shift_amt < 32) {
		emit_shf(nfp_prog, reg_both(dst), reg_a(dst + 1), SHF_OP_NONE,
			 reg_b(dst), SHF_SC_R_DSHF, shift_amt);
		/* Set signedness bit. */
		emit_alu(nfp_prog, reg_none(), reg_a(dst + 1), ALU_OP_OR,
			 reg_imm(0));
		emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_ASHR,
			 reg_b(dst + 1), SHF_SC_R_SHF, shift_amt);
	} else if (shift_amt == 32) {
		/* NOTE: this also helps setting signedness bit. */
		wrp_reg_mov(nfp_prog, dst, dst + 1);
		emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_ASHR,
			 reg_b(dst + 1), SHF_SC_R_SHF, 31);
	} else if (shift_amt > 32) {
		emit_alu(nfp_prog, reg_none(), reg_a(dst + 1), ALU_OP_OR,
			 reg_imm(0));
		emit_shf(nfp_prog, reg_both(dst), reg_none(), SHF_OP_ASHR,
			 reg_b(dst + 1), SHF_SC_R_SHF, shift_amt - 32);
		emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_ASHR,
			 reg_b(dst + 1), SHF_SC_R_SHF, 31);
	}

	return 0;
}

static int ashr_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;

	return __ashr_imm64(nfp_prog, dst, insn->imm);
}

static void ashr_reg64_lt32_high(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	/* NOTE: the first insn will set both indirect shift amount (source A)
	 * and signedness bit (MSB of result).
	 */
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_b(dst + 1));
	emit_shf_indir(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_ASHR,
		       reg_b(dst + 1), SHF_SC_R_SHF);
}

static void ashr_reg64_lt32_low(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	/* NOTE: it is the same as logic shift because we don't need to shift in
	 * signedness bit when the shift amount is less than 32.
	 */
	return shr_reg64_lt32_low(nfp_prog, dst, src);
}

static void ashr_reg64_lt32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	ashr_reg64_lt32_low(nfp_prog, dst, src);
	ashr_reg64_lt32_high(nfp_prog, dst, src);
}

static void ashr_reg64_ge32(struct nfp_prog *nfp_prog, u8 dst, u8 src)
{
	emit_alu(nfp_prog, reg_none(), reg_a(src), ALU_OP_OR, reg_b(dst + 1));
	emit_shf_indir(nfp_prog, reg_both(dst), reg_none(), SHF_OP_ASHR,
		       reg_b(dst + 1), SHF_SC_R_SHF);
	emit_shf(nfp_prog, reg_both(dst + 1), reg_none(), SHF_OP_ASHR,
		 reg_b(dst + 1), SHF_SC_R_SHF, 31);
}

/* Like ashr_imm64, but need to use indirect shift. */
static int ashr_reg64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 umin, umax;
	u8 dst, src;

	dst = insn->dst_reg * 2;
	umin = meta->umin;
	umax = meta->umax;
	if (umin == umax)
		return __ashr_imm64(nfp_prog, dst, umin);

	src = insn->src_reg * 2;
	if (umax < 32) {
		ashr_reg64_lt32(nfp_prog, dst, src);
	} else if (umin >= 32) {
		ashr_reg64_ge32(nfp_prog, dst, src);
	} else {
		u16 label_ge32, label_end;

		label_ge32 = nfp_prog_current_offset(nfp_prog) + 6;
		emit_br_bset(nfp_prog, reg_a(src), 5, label_ge32, 0);
		ashr_reg64_lt32_low(nfp_prog, dst, src);
		label_end = nfp_prog_current_offset(nfp_prog) + 6;
		emit_br(nfp_prog, BR_UNC, label_end, 2);
		/* ashr_reg64_lt32_high packed in delay slot. */
		ashr_reg64_lt32_high(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_ge32))
			return -EINVAL;
		ashr_reg64_ge32(nfp_prog, dst, src);

		if (!nfp_prog_confirm_current_offset(nfp_prog, label_end))
			return -EINVAL;
	}

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

static int neg_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	u8 dst = meta->insn.dst_reg * 2;

	emit_alu(nfp_prog, reg_both(dst), reg_imm(0), ALU_OP_SUB, reg_b(dst));
	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return 0;
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

static int end_reg32(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 gpr = insn->dst_reg * 2;

	switch (insn->imm) {
	case 16:
		emit_ld_field(nfp_prog, reg_both(gpr), 0x9, reg_b(gpr),
			      SHF_SC_R_ROT, 8);
		emit_ld_field(nfp_prog, reg_both(gpr), 0xe, reg_a(gpr),
			      SHF_SC_R_SHF, 16);

		wrp_immed(nfp_prog, reg_both(gpr + 1), 0);
		break;
	case 32:
		wrp_end32(nfp_prog, reg_a(gpr), gpr);
		wrp_immed(nfp_prog, reg_both(gpr + 1), 0);
		break;
	case 64:
		wrp_mov(nfp_prog, imm_a(nfp_prog), reg_b(gpr + 1));

		wrp_end32(nfp_prog, reg_a(gpr), gpr + 1);
		wrp_end32(nfp_prog, imm_a(nfp_prog), gpr);
		break;
	}

	return 0;
}

static int imm_ld8_part2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	struct nfp_insn_meta *prev = nfp_meta_prev(meta);
	u32 imm_lo, imm_hi;
	u8 dst;

	dst = prev->insn.dst_reg * 2;
	imm_lo = prev->insn.imm;
	imm_hi = meta->insn.imm;

	wrp_immed(nfp_prog, reg_both(dst), imm_lo);

	/* mov is always 1 insn, load imm may be two, so try to use mov */
	if (imm_hi == imm_lo)
		wrp_mov(nfp_prog, reg_both(dst + 1), reg_a(dst));
	else
		wrp_immed(nfp_prog, reg_both(dst + 1), imm_hi);

	return 0;
}

static int imm_ld8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	meta->double_cb = imm_ld8_part2;
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
				     meta->insn.src_reg * 2, 1);
}

static int data_ind_ld2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ind_ld(nfp_prog, meta->insn.imm,
				     meta->insn.src_reg * 2, 2);
}

static int data_ind_ld4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return construct_data_ind_ld(nfp_prog, meta->insn.imm,
				     meta->insn.src_reg * 2, 4);
}

static int
mem_ldx_stack(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      unsigned int size, unsigned int ptr_off)
{
	return mem_op_stack(nfp_prog, meta, size, ptr_off,
			    meta->insn.dst_reg * 2, meta->insn.src_reg * 2,
			    true, wrp_lmem_load);
}

static int mem_ldx_skb(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		       u8 size)
{
	swreg dst = reg_both(meta->insn.dst_reg * 2);

	switch (meta->insn.off) {
	case offsetof(struct __sk_buff, len):
		if (size != FIELD_SIZEOF(struct __sk_buff, len))
			return -EOPNOTSUPP;
		wrp_mov(nfp_prog, dst, plen_reg(nfp_prog));
		break;
	case offsetof(struct __sk_buff, data):
		if (size != FIELD_SIZEOF(struct __sk_buff, data))
			return -EOPNOTSUPP;
		wrp_mov(nfp_prog, dst, pptr_reg(nfp_prog));
		break;
	case offsetof(struct __sk_buff, data_end):
		if (size != FIELD_SIZEOF(struct __sk_buff, data_end))
			return -EOPNOTSUPP;
		emit_alu(nfp_prog, dst,
			 plen_reg(nfp_prog), ALU_OP_ADD, pptr_reg(nfp_prog));
		break;
	default:
		return -EOPNOTSUPP;
	}

	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return 0;
}

static int mem_ldx_xdp(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		       u8 size)
{
	swreg dst = reg_both(meta->insn.dst_reg * 2);

	switch (meta->insn.off) {
	case offsetof(struct xdp_md, data):
		if (size != FIELD_SIZEOF(struct xdp_md, data))
			return -EOPNOTSUPP;
		wrp_mov(nfp_prog, dst, pptr_reg(nfp_prog));
		break;
	case offsetof(struct xdp_md, data_end):
		if (size != FIELD_SIZEOF(struct xdp_md, data_end))
			return -EOPNOTSUPP;
		emit_alu(nfp_prog, dst,
			 plen_reg(nfp_prog), ALU_OP_ADD, pptr_reg(nfp_prog));
		break;
	default:
		return -EOPNOTSUPP;
	}

	wrp_immed(nfp_prog, reg_both(meta->insn.dst_reg * 2 + 1), 0);

	return 0;
}

static int
mem_ldx_data(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	     unsigned int size)
{
	swreg tmp_reg;

	tmp_reg = re_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));

	return data_ld_host_order_addr32(nfp_prog, meta->insn.src_reg * 2,
					 tmp_reg, meta->insn.dst_reg * 2, size);
}

static int
mem_ldx_emem(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	     unsigned int size)
{
	swreg tmp_reg;

	tmp_reg = re_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));

	return data_ld_host_order_addr40(nfp_prog, meta->insn.src_reg * 2,
					 tmp_reg, meta->insn.dst_reg * 2, size);
}

static void
mem_ldx_data_init_pktcache(struct nfp_prog *nfp_prog,
			   struct nfp_insn_meta *meta)
{
	s16 range_start = meta->pkt_cache.range_start;
	s16 range_end = meta->pkt_cache.range_end;
	swreg src_base, off;
	u8 xfer_num, len;
	bool indir;

	off = re_load_imm_any(nfp_prog, range_start, imm_b(nfp_prog));
	src_base = reg_a(meta->insn.src_reg * 2);
	len = range_end - range_start;
	xfer_num = round_up(len, REG_WIDTH) / REG_WIDTH;

	indir = len > 8 * REG_WIDTH;
	/* Setup PREV_ALU for indirect mode. */
	if (indir)
		wrp_immed(nfp_prog, reg_none(),
			  CMD_OVE_LEN | FIELD_PREP(CMD_OV_LEN, xfer_num - 1));

	/* Cache memory into transfer-in registers. */
	emit_cmd_any(nfp_prog, CMD_TGT_READ32_SWAP, CMD_MODE_32b, 0, src_base,
		     off, xfer_num - 1, CMD_CTX_SWAP, indir);
}

static int
mem_ldx_data_from_pktcache_unaligned(struct nfp_prog *nfp_prog,
				     struct nfp_insn_meta *meta,
				     unsigned int size)
{
	s16 range_start = meta->pkt_cache.range_start;
	s16 insn_off = meta->insn.off - range_start;
	swreg dst_lo, dst_hi, src_lo, src_mid;
	u8 dst_gpr = meta->insn.dst_reg * 2;
	u8 len_lo = size, len_mid = 0;
	u8 idx = insn_off / REG_WIDTH;
	u8 off = insn_off % REG_WIDTH;

	dst_hi = reg_both(dst_gpr + 1);
	dst_lo = reg_both(dst_gpr);
	src_lo = reg_xfer(idx);

	/* The read length could involve as many as three registers. */
	if (size > REG_WIDTH - off) {
		/* Calculate the part in the second register. */
		len_lo = REG_WIDTH - off;
		len_mid = size - len_lo;

		/* Calculate the part in the third register. */
		if (size > 2 * REG_WIDTH - off)
			len_mid = REG_WIDTH;
	}

	wrp_reg_subpart(nfp_prog, dst_lo, src_lo, len_lo, off);

	if (!len_mid) {
		wrp_immed(nfp_prog, dst_hi, 0);
		return 0;
	}

	src_mid = reg_xfer(idx + 1);

	if (size <= REG_WIDTH) {
		wrp_reg_or_subpart(nfp_prog, dst_lo, src_mid, len_mid, len_lo);
		wrp_immed(nfp_prog, dst_hi, 0);
	} else {
		swreg src_hi = reg_xfer(idx + 2);

		wrp_reg_or_subpart(nfp_prog, dst_lo, src_mid,
				   REG_WIDTH - len_lo, len_lo);
		wrp_reg_subpart(nfp_prog, dst_hi, src_mid, len_lo,
				REG_WIDTH - len_lo);
		wrp_reg_or_subpart(nfp_prog, dst_hi, src_hi, REG_WIDTH - len_lo,
				   len_lo);
	}

	return 0;
}

static int
mem_ldx_data_from_pktcache_aligned(struct nfp_prog *nfp_prog,
				   struct nfp_insn_meta *meta,
				   unsigned int size)
{
	swreg dst_lo, dst_hi, src_lo;
	u8 dst_gpr, idx;

	idx = (meta->insn.off - meta->pkt_cache.range_start) / REG_WIDTH;
	dst_gpr = meta->insn.dst_reg * 2;
	dst_hi = reg_both(dst_gpr + 1);
	dst_lo = reg_both(dst_gpr);
	src_lo = reg_xfer(idx);

	if (size < REG_WIDTH) {
		wrp_reg_subpart(nfp_prog, dst_lo, src_lo, size, 0);
		wrp_immed(nfp_prog, dst_hi, 0);
	} else if (size == REG_WIDTH) {
		wrp_mov(nfp_prog, dst_lo, src_lo);
		wrp_immed(nfp_prog, dst_hi, 0);
	} else {
		swreg src_hi = reg_xfer(idx + 1);

		wrp_mov(nfp_prog, dst_lo, src_lo);
		wrp_mov(nfp_prog, dst_hi, src_hi);
	}

	return 0;
}

static int
mem_ldx_data_from_pktcache(struct nfp_prog *nfp_prog,
			   struct nfp_insn_meta *meta, unsigned int size)
{
	u8 off = meta->insn.off - meta->pkt_cache.range_start;

	if (IS_ALIGNED(off, REG_WIDTH))
		return mem_ldx_data_from_pktcache_aligned(nfp_prog, meta, size);

	return mem_ldx_data_from_pktcache_unaligned(nfp_prog, meta, size);
}

static int
mem_ldx(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	unsigned int size)
{
	if (meta->ldst_gather_len)
		return nfp_cpp_memcpy(nfp_prog, meta);

	if (meta->ptr.type == PTR_TO_CTX) {
		if (nfp_prog->type == BPF_PROG_TYPE_XDP)
			return mem_ldx_xdp(nfp_prog, meta, size);
		else
			return mem_ldx_skb(nfp_prog, meta, size);
	}

	if (meta->ptr.type == PTR_TO_PACKET) {
		if (meta->pkt_cache.range_end) {
			if (meta->pkt_cache.do_init)
				mem_ldx_data_init_pktcache(nfp_prog, meta);

			return mem_ldx_data_from_pktcache(nfp_prog, meta, size);
		} else {
			return mem_ldx_data(nfp_prog, meta, size);
		}
	}

	if (meta->ptr.type == PTR_TO_STACK)
		return mem_ldx_stack(nfp_prog, meta, size,
				     meta->ptr.off + meta->ptr.var_off.value);

	if (meta->ptr.type == PTR_TO_MAP_VALUE)
		return mem_ldx_emem(nfp_prog, meta, size);

	return -EOPNOTSUPP;
}

static int mem_ldx1(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_ldx(nfp_prog, meta, 1);
}

static int mem_ldx2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_ldx(nfp_prog, meta, 2);
}

static int mem_ldx4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_ldx(nfp_prog, meta, 4);
}

static int mem_ldx8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_ldx(nfp_prog, meta, 8);
}

static int
mem_st_data(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	    unsigned int size)
{
	u64 imm = meta->insn.imm; /* sign extend */
	swreg off_reg;

	off_reg = re_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));

	return data_st_host_order(nfp_prog, meta->insn.dst_reg * 2, off_reg,
				  imm, size);
}

static int mem_st(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  unsigned int size)
{
	if (meta->ptr.type == PTR_TO_PACKET)
		return mem_st_data(nfp_prog, meta, size);

	return -EOPNOTSUPP;
}

static int mem_st1(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_st(nfp_prog, meta, 1);
}

static int mem_st2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_st(nfp_prog, meta, 2);
}

static int mem_st4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_st(nfp_prog, meta, 4);
}

static int mem_st8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_st(nfp_prog, meta, 8);
}

static int
mem_stx_data(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	     unsigned int size)
{
	swreg off_reg;

	off_reg = re_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));

	return data_stx_host_order(nfp_prog, meta->insn.dst_reg * 2, off_reg,
				   meta->insn.src_reg * 2, size);
}

static int
mem_stx_stack(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	      unsigned int size, unsigned int ptr_off)
{
	return mem_op_stack(nfp_prog, meta, size, ptr_off,
			    meta->insn.src_reg * 2, meta->insn.dst_reg * 2,
			    false, wrp_lmem_store);
}

static int mem_stx_xdp(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	switch (meta->insn.off) {
	case offsetof(struct xdp_md, rx_queue_index):
		return nfp_queue_select(nfp_prog, meta);
	}

	WARN_ON_ONCE(1); /* verifier should have rejected bad accesses */
	return -EOPNOTSUPP;
}

static int
mem_stx(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	unsigned int size)
{
	if (meta->ptr.type == PTR_TO_PACKET)
		return mem_stx_data(nfp_prog, meta, size);

	if (meta->ptr.type == PTR_TO_STACK)
		return mem_stx_stack(nfp_prog, meta, size,
				     meta->ptr.off + meta->ptr.var_off.value);

	return -EOPNOTSUPP;
}

static int mem_stx1(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_stx(nfp_prog, meta, 1);
}

static int mem_stx2(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_stx(nfp_prog, meta, 2);
}

static int mem_stx4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	if (meta->ptr.type == PTR_TO_CTX)
		if (nfp_prog->type == BPF_PROG_TYPE_XDP)
			return mem_stx_xdp(nfp_prog, meta);
	return mem_stx(nfp_prog, meta, 4);
}

static int mem_stx8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_stx(nfp_prog, meta, 8);
}

static int
mem_xadd(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta, bool is64)
{
	u8 dst_gpr = meta->insn.dst_reg * 2;
	u8 src_gpr = meta->insn.src_reg * 2;
	unsigned int full_add, out;
	swreg addra, addrb, off;

	off = ur_load_imm_any(nfp_prog, meta->insn.off, imm_b(nfp_prog));

	/* We can fit 16 bits into command immediate, if we know the immediate
	 * is guaranteed to either always or never fit into 16 bit we only
	 * generate code to handle that particular case, otherwise generate
	 * code for both.
	 */
	out = nfp_prog_current_offset(nfp_prog);
	full_add = nfp_prog_current_offset(nfp_prog);

	if (meta->insn.off) {
		out += 2;
		full_add += 2;
	}
	if (meta->xadd_maybe_16bit) {
		out += 3;
		full_add += 3;
	}
	if (meta->xadd_over_16bit)
		out += 2 + is64;
	if (meta->xadd_maybe_16bit && meta->xadd_over_16bit) {
		out += 5;
		full_add += 5;
	}

	/* Generate the branch for choosing add_imm vs add */
	if (meta->xadd_maybe_16bit && meta->xadd_over_16bit) {
		swreg max_imm = imm_a(nfp_prog);

		wrp_immed(nfp_prog, max_imm, 0xffff);
		emit_alu(nfp_prog, reg_none(),
			 max_imm, ALU_OP_SUB, reg_b(src_gpr));
		emit_alu(nfp_prog, reg_none(),
			 reg_imm(0), ALU_OP_SUB_C, reg_b(src_gpr + 1));
		emit_br(nfp_prog, BR_BLO, full_add, meta->insn.off ? 2 : 0);
		/* defer for add */
	}

	/* If insn has an offset add to the address */
	if (!meta->insn.off) {
		addra = reg_a(dst_gpr);
		addrb = reg_b(dst_gpr + 1);
	} else {
		emit_alu(nfp_prog, imma_a(nfp_prog),
			 reg_a(dst_gpr), ALU_OP_ADD, off);
		emit_alu(nfp_prog, imma_b(nfp_prog),
			 reg_a(dst_gpr + 1), ALU_OP_ADD_C, reg_imm(0));
		addra = imma_a(nfp_prog);
		addrb = imma_b(nfp_prog);
	}

	/* Generate the add_imm if 16 bits are possible */
	if (meta->xadd_maybe_16bit) {
		swreg prev_alu = imm_a(nfp_prog);

		wrp_immed(nfp_prog, prev_alu,
			  FIELD_PREP(CMD_OVE_DATA, 2) |
			  CMD_OVE_LEN |
			  FIELD_PREP(CMD_OV_LEN, 0x8 | is64 << 2));
		wrp_reg_or_subpart(nfp_prog, prev_alu, reg_b(src_gpr), 2, 2);
		emit_cmd_indir(nfp_prog, CMD_TGT_ADD_IMM, CMD_MODE_40b_BA, 0,
			       addra, addrb, 0, CMD_CTX_NO_SWAP);

		if (meta->xadd_over_16bit)
			emit_br(nfp_prog, BR_UNC, out, 0);
	}

	if (!nfp_prog_confirm_current_offset(nfp_prog, full_add))
		return -EINVAL;

	/* Generate the add if 16 bits are not guaranteed */
	if (meta->xadd_over_16bit) {
		emit_cmd(nfp_prog, CMD_TGT_ADD, CMD_MODE_40b_BA, 0,
			 addra, addrb, is64 << 2,
			 is64 ? CMD_CTX_SWAP_DEFER2 : CMD_CTX_SWAP_DEFER1);

		wrp_mov(nfp_prog, reg_xfer(0), reg_a(src_gpr));
		if (is64)
			wrp_mov(nfp_prog, reg_xfer(1), reg_a(src_gpr + 1));
	}

	if (!nfp_prog_confirm_current_offset(nfp_prog, out))
		return -EINVAL;

	return 0;
}

static int mem_xadd4(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_xadd(nfp_prog, meta, false);
}

static int mem_xadd8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_xadd(nfp_prog, meta, true);
}

static int jump(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	emit_br(nfp_prog, BR_UNC, meta->insn.off, 0);

	return 0;
}

static int jeq_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	swreg or1, or2, tmp_reg;

	or1 = reg_a(insn->dst_reg * 2);
	or2 = reg_b(insn->dst_reg * 2 + 1);

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

static int jset_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	swreg tmp_reg;

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
	swreg tmp_reg;

	if (!imm) {
		emit_alu(nfp_prog, reg_none(), reg_a(insn->dst_reg * 2),
			 ALU_OP_OR, reg_b(insn->dst_reg * 2 + 1));
		emit_br(nfp_prog, BR_BNE, insn->off, 0);
		return 0;
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

	emit_alu(nfp_prog, imm_a(nfp_prog), reg_a(insn->dst_reg * 2),
		 ALU_OP_XOR, reg_b(insn->src_reg * 2));
	emit_alu(nfp_prog, imm_b(nfp_prog), reg_a(insn->dst_reg * 2 + 1),
		 ALU_OP_XOR, reg_b(insn->src_reg * 2 + 1));
	emit_alu(nfp_prog, reg_none(),
		 imm_a(nfp_prog), ALU_OP_OR, imm_b(nfp_prog));
	emit_br(nfp_prog, BR_BEQ, insn->off, 0);

	return 0;
}

static int jset_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_test_reg(nfp_prog, meta, ALU_OP_AND, BR_BNE);
}

static int jne_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_test_reg(nfp_prog, meta, ALU_OP_XOR, BR_BNE);
}

static int call(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	switch (meta->insn.imm) {
	case BPF_FUNC_xdp_adjust_head:
		return adjust_head(nfp_prog, meta);
	case BPF_FUNC_map_lookup_elem:
	case BPF_FUNC_map_update_elem:
	case BPF_FUNC_map_delete_elem:
		return map_call_stack_common(nfp_prog, meta);
	case BPF_FUNC_get_prandom_u32:
		return nfp_get_prandom_u32(nfp_prog, meta);
	case BPF_FUNC_perf_event_output:
		return nfp_perf_event_output(nfp_prog, meta);
	default:
		WARN_ONCE(1, "verifier allowed unsupported function\n");
		return -EOPNOTSUPP;
	}
}

static int goto_out(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO, 0, RELO_BR_GO_OUT);

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
	[BPF_ALU64 | BPF_NEG] =		neg_reg64,
	[BPF_ALU64 | BPF_LSH | BPF_X] =	shl_reg64,
	[BPF_ALU64 | BPF_LSH | BPF_K] =	shl_imm64,
	[BPF_ALU64 | BPF_RSH | BPF_X] =	shr_reg64,
	[BPF_ALU64 | BPF_RSH | BPF_K] =	shr_imm64,
	[BPF_ALU64 | BPF_ARSH | BPF_X] = ashr_reg64,
	[BPF_ALU64 | BPF_ARSH | BPF_K] = ashr_imm64,
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
	[BPF_ALU | BPF_NEG] =		neg_reg,
	[BPF_ALU | BPF_LSH | BPF_K] =	shl_imm,
	[BPF_ALU | BPF_END | BPF_X] =	end_reg32,
	[BPF_LD | BPF_IMM | BPF_DW] =	imm_ld8,
	[BPF_LD | BPF_ABS | BPF_B] =	data_ld1,
	[BPF_LD | BPF_ABS | BPF_H] =	data_ld2,
	[BPF_LD | BPF_ABS | BPF_W] =	data_ld4,
	[BPF_LD | BPF_IND | BPF_B] =	data_ind_ld1,
	[BPF_LD | BPF_IND | BPF_H] =	data_ind_ld2,
	[BPF_LD | BPF_IND | BPF_W] =	data_ind_ld4,
	[BPF_LDX | BPF_MEM | BPF_B] =	mem_ldx1,
	[BPF_LDX | BPF_MEM | BPF_H] =	mem_ldx2,
	[BPF_LDX | BPF_MEM | BPF_W] =	mem_ldx4,
	[BPF_LDX | BPF_MEM | BPF_DW] =	mem_ldx8,
	[BPF_STX | BPF_MEM | BPF_B] =	mem_stx1,
	[BPF_STX | BPF_MEM | BPF_H] =	mem_stx2,
	[BPF_STX | BPF_MEM | BPF_W] =	mem_stx4,
	[BPF_STX | BPF_MEM | BPF_DW] =	mem_stx8,
	[BPF_STX | BPF_XADD | BPF_W] =	mem_xadd4,
	[BPF_STX | BPF_XADD | BPF_DW] =	mem_xadd8,
	[BPF_ST | BPF_MEM | BPF_B] =	mem_st1,
	[BPF_ST | BPF_MEM | BPF_H] =	mem_st2,
	[BPF_ST | BPF_MEM | BPF_W] =	mem_st4,
	[BPF_ST | BPF_MEM | BPF_DW] =	mem_st8,
	[BPF_JMP | BPF_JA | BPF_K] =	jump,
	[BPF_JMP | BPF_JEQ | BPF_K] =	jeq_imm,
	[BPF_JMP | BPF_JGT | BPF_K] =	cmp_imm,
	[BPF_JMP | BPF_JGE | BPF_K] =	cmp_imm,
	[BPF_JMP | BPF_JLT | BPF_K] =	cmp_imm,
	[BPF_JMP | BPF_JLE | BPF_K] =	cmp_imm,
	[BPF_JMP | BPF_JSGT | BPF_K] =  cmp_imm,
	[BPF_JMP | BPF_JSGE | BPF_K] =  cmp_imm,
	[BPF_JMP | BPF_JSLT | BPF_K] =  cmp_imm,
	[BPF_JMP | BPF_JSLE | BPF_K] =  cmp_imm,
	[BPF_JMP | BPF_JSET | BPF_K] =	jset_imm,
	[BPF_JMP | BPF_JNE | BPF_K] =	jne_imm,
	[BPF_JMP | BPF_JEQ | BPF_X] =	jeq_reg,
	[BPF_JMP | BPF_JGT | BPF_X] =	cmp_reg,
	[BPF_JMP | BPF_JGE | BPF_X] =	cmp_reg,
	[BPF_JMP | BPF_JLT | BPF_X] =	cmp_reg,
	[BPF_JMP | BPF_JLE | BPF_X] =	cmp_reg,
	[BPF_JMP | BPF_JSGT | BPF_X] =  cmp_reg,
	[BPF_JMP | BPF_JSGE | BPF_X] =  cmp_reg,
	[BPF_JMP | BPF_JSLT | BPF_X] =  cmp_reg,
	[BPF_JMP | BPF_JSLE | BPF_X] =  cmp_reg,
	[BPF_JMP | BPF_JSET | BPF_X] =	jset_reg,
	[BPF_JMP | BPF_JNE | BPF_X] =	jne_reg,
	[BPF_JMP | BPF_CALL] =		call,
	[BPF_JMP | BPF_EXIT] =		goto_out,
};

/* --- Assembler logic --- */
static int nfp_fixup_branches(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *jmp_dst;
	u32 idx, br_idx;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (meta->skip)
			continue;
		if (meta->insn.code == (BPF_JMP | BPF_CALL))
			continue;
		if (BPF_CLASS(meta->insn.code) != BPF_JMP)
			continue;

		if (list_is_last(&meta->l, &nfp_prog->insns))
			br_idx = nfp_prog->last_bpf_off;
		else
			br_idx = list_next_entry(meta, l)->off - 1;

		if (!nfp_is_br(nfp_prog->prog[br_idx])) {
			pr_err("Fixup found block not ending in branch %d %02x %016llx!!\n",
			       br_idx, meta->insn.code, nfp_prog->prog[br_idx]);
			return -ELOOP;
		}
		/* Leave special branches for later */
		if (FIELD_GET(OP_RELO_TYPE, nfp_prog->prog[br_idx]) !=
		    RELO_BR_REL)
			continue;

		if (!meta->jmp_dst) {
			pr_err("Non-exit jump doesn't have destination info recorded!!\n");
			return -ELOOP;
		}

		jmp_dst = meta->jmp_dst;

		if (jmp_dst->skip) {
			pr_err("Branch landing on removed instruction!!\n");
			return -ELOOP;
		}

		for (idx = meta->off; idx <= br_idx; idx++) {
			if (!nfp_is_br(nfp_prog->prog[idx]))
				continue;
			br_set_offset(&nfp_prog->prog[idx], jmp_dst->off);
		}
	}

	return 0;
}

static void nfp_intro(struct nfp_prog *nfp_prog)
{
	wrp_immed(nfp_prog, plen_reg(nfp_prog), GENMASK(13, 0));
	emit_alu(nfp_prog, plen_reg(nfp_prog),
		 plen_reg(nfp_prog), ALU_OP_AND, pv_len(nfp_prog));
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

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO, 2, RELO_BR_NEXT_PKT);

	wrp_mov(nfp_prog, reg_a(0), NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_imm(0x11), SHF_SC_L_SHF, 16);

	/* Target for normal exits */
	nfp_prog->tgt_out = nfp_prog_current_offset(nfp_prog);

	/* if R0 > 7 jump to abort */
	emit_alu(nfp_prog, reg_none(), reg_imm(7), ALU_OP_SUB, reg_b(0));
	emit_br(nfp_prog, BR_BLO, nfp_prog->tgt_abort, 0);
	wrp_mov(nfp_prog, reg_a(0), NFP_BPF_ABI_FLAGS);

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

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO, 2, RELO_BR_NEXT_PKT);

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

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO, 2, RELO_BR_NEXT_PKT);

	wrp_mov(nfp_prog, reg_a(0), NFP_BPF_ABI_FLAGS);
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

	emit_br_relo(nfp_prog, BR_UNC, BR_OFF_RELO, 2, RELO_BR_NEXT_PKT);

	wrp_mov(nfp_prog, reg_a(0), NFP_BPF_ABI_FLAGS);
	emit_ld_field(nfp_prog, reg_a(0), 0xc, reg_b(2), SHF_SC_L_SHF, 16);
}

static void nfp_outro(struct nfp_prog *nfp_prog)
{
	switch (nfp_prog->type) {
	case BPF_PROG_TYPE_SCHED_CLS:
		nfp_outro_tc_da(nfp_prog);
		break;
	case BPF_PROG_TYPE_XDP:
		nfp_outro_xdp(nfp_prog);
		break;
	default:
		WARN_ON(1);
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
		if (nfp_prog->error)
			return nfp_prog->error;

		nfp_prog->n_translated++;
	}

	nfp_prog->last_bpf_off = nfp_prog_current_offset(nfp_prog) - 1;

	nfp_outro(nfp_prog);
	if (nfp_prog->error)
		return nfp_prog->error;

	wrp_nops(nfp_prog, NFP_USTORE_PREFETCH_WINDOW);
	if (nfp_prog->error)
		return nfp_prog->error;

	return nfp_fixup_branches(nfp_prog);
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

/* abs(insn.imm) will fit better into unrestricted reg immediate -
 * convert add/sub of a negative number into a sub/add of a positive one.
 */
static void nfp_bpf_opt_neg_add_sub(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		struct bpf_insn insn = meta->insn;

		if (meta->skip)
			continue;

		if (BPF_CLASS(insn.code) != BPF_ALU &&
		    BPF_CLASS(insn.code) != BPF_ALU64 &&
		    BPF_CLASS(insn.code) != BPF_JMP)
			continue;
		if (BPF_SRC(insn.code) != BPF_K)
			continue;
		if (insn.imm >= 0)
			continue;

		if (BPF_CLASS(insn.code) == BPF_JMP) {
			switch (BPF_OP(insn.code)) {
			case BPF_JGE:
			case BPF_JSGE:
			case BPF_JLT:
			case BPF_JSLT:
				meta->jump_neg_op = true;
				break;
			default:
				continue;
			}
		} else {
			if (BPF_OP(insn.code) == BPF_ADD)
				insn.code = BPF_CLASS(insn.code) | BPF_SUB;
			else if (BPF_OP(insn.code) == BPF_SUB)
				insn.code = BPF_CLASS(insn.code) | BPF_ADD;
			else
				continue;

			meta->insn.code = insn.code | BPF_K;
		}

		meta->insn.imm = -insn.imm;
	}
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

		if (meta2->flags & FLAG_INSN_IS_JUMP_DST)
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

		if (meta2->flags & FLAG_INSN_IS_JUMP_DST ||
		    meta3->flags & FLAG_INSN_IS_JUMP_DST)
			continue;

		meta2->skip = true;
		meta3->skip = true;
	}
}

/* load/store pair that forms memory copy sould look like the following:
 *
 *   ld_width R, [addr_src + offset_src]
 *   st_width [addr_dest + offset_dest], R
 *
 * The destination register of load and source register of store should
 * be the same, load and store should also perform at the same width.
 * If either of addr_src or addr_dest is stack pointer, we don't do the
 * CPP optimization as stack is modelled by registers on NFP.
 */
static bool
curr_pair_is_memcpy(struct nfp_insn_meta *ld_meta,
		    struct nfp_insn_meta *st_meta)
{
	struct bpf_insn *ld = &ld_meta->insn;
	struct bpf_insn *st = &st_meta->insn;

	if (!is_mbpf_load(ld_meta) || !is_mbpf_store(st_meta))
		return false;

	if (ld_meta->ptr.type != PTR_TO_PACKET)
		return false;

	if (st_meta->ptr.type != PTR_TO_PACKET)
		return false;

	if (BPF_SIZE(ld->code) != BPF_SIZE(st->code))
		return false;

	if (ld->dst_reg != st->src_reg)
		return false;

	/* There is jump to the store insn in this pair. */
	if (st_meta->flags & FLAG_INSN_IS_JUMP_DST)
		return false;

	return true;
}

/* Currently, we only support chaining load/store pairs if:
 *
 *  - Their address base registers are the same.
 *  - Their address offsets are in the same order.
 *  - They operate at the same memory width.
 *  - There is no jump into the middle of them.
 */
static bool
curr_pair_chain_with_previous(struct nfp_insn_meta *ld_meta,
			      struct nfp_insn_meta *st_meta,
			      struct bpf_insn *prev_ld,
			      struct bpf_insn *prev_st)
{
	u8 prev_size, curr_size, prev_ld_base, prev_st_base, prev_ld_dst;
	struct bpf_insn *ld = &ld_meta->insn;
	struct bpf_insn *st = &st_meta->insn;
	s16 prev_ld_off, prev_st_off;

	/* This pair is the start pair. */
	if (!prev_ld)
		return true;

	prev_size = BPF_LDST_BYTES(prev_ld);
	curr_size = BPF_LDST_BYTES(ld);
	prev_ld_base = prev_ld->src_reg;
	prev_st_base = prev_st->dst_reg;
	prev_ld_dst = prev_ld->dst_reg;
	prev_ld_off = prev_ld->off;
	prev_st_off = prev_st->off;

	if (ld->dst_reg != prev_ld_dst)
		return false;

	if (ld->src_reg != prev_ld_base || st->dst_reg != prev_st_base)
		return false;

	if (curr_size != prev_size)
		return false;

	/* There is jump to the head of this pair. */
	if (ld_meta->flags & FLAG_INSN_IS_JUMP_DST)
		return false;

	/* Both in ascending order. */
	if (prev_ld_off + prev_size == ld->off &&
	    prev_st_off + prev_size == st->off)
		return true;

	/* Both in descending order. */
	if (ld->off + curr_size == prev_ld_off &&
	    st->off + curr_size == prev_st_off)
		return true;

	return false;
}

/* Return TRUE if cross memory access happens. Cross memory access means
 * store area is overlapping with load area that a later load might load
 * the value from previous store, for this case we can't treat the sequence
 * as an memory copy.
 */
static bool
cross_mem_access(struct bpf_insn *ld, struct nfp_insn_meta *head_ld_meta,
		 struct nfp_insn_meta *head_st_meta)
{
	s16 head_ld_off, head_st_off, ld_off;

	/* Different pointer types does not overlap. */
	if (head_ld_meta->ptr.type != head_st_meta->ptr.type)
		return false;

	/* load and store are both PTR_TO_PACKET, check ID info.  */
	if (head_ld_meta->ptr.id != head_st_meta->ptr.id)
		return true;

	/* Canonicalize the offsets. Turn all of them against the original
	 * base register.
	 */
	head_ld_off = head_ld_meta->insn.off + head_ld_meta->ptr.off;
	head_st_off = head_st_meta->insn.off + head_st_meta->ptr.off;
	ld_off = ld->off + head_ld_meta->ptr.off;

	/* Ascending order cross. */
	if (ld_off > head_ld_off &&
	    head_ld_off < head_st_off && ld_off >= head_st_off)
		return true;

	/* Descending order cross. */
	if (ld_off < head_ld_off &&
	    head_ld_off > head_st_off && ld_off <= head_st_off)
		return true;

	return false;
}

/* This pass try to identify the following instructoin sequences.
 *
 *   load R, [regA + offA]
 *   store [regB + offB], R
 *   load R, [regA + offA + const_imm_A]
 *   store [regB + offB + const_imm_A], R
 *   load R, [regA + offA + 2 * const_imm_A]
 *   store [regB + offB + 2 * const_imm_A], R
 *   ...
 *
 * Above sequence is typically generated by compiler when lowering
 * memcpy. NFP prefer using CPP instructions to accelerate it.
 */
static void nfp_bpf_opt_ldst_gather(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *head_ld_meta = NULL;
	struct nfp_insn_meta *head_st_meta = NULL;
	struct nfp_insn_meta *meta1, *meta2;
	struct bpf_insn *prev_ld = NULL;
	struct bpf_insn *prev_st = NULL;
	u8 count = 0;

	nfp_for_each_insn_walk2(nfp_prog, meta1, meta2) {
		struct bpf_insn *ld = &meta1->insn;
		struct bpf_insn *st = &meta2->insn;

		/* Reset record status if any of the following if true:
		 *   - The current insn pair is not load/store.
		 *   - The load/store pair doesn't chain with previous one.
		 *   - The chained load/store pair crossed with previous pair.
		 *   - The chained load/store pair has a total size of memory
		 *     copy beyond 128 bytes which is the maximum length a
		 *     single NFP CPP command can transfer.
		 */
		if (!curr_pair_is_memcpy(meta1, meta2) ||
		    !curr_pair_chain_with_previous(meta1, meta2, prev_ld,
						   prev_st) ||
		    (head_ld_meta && (cross_mem_access(ld, head_ld_meta,
						       head_st_meta) ||
				      head_ld_meta->ldst_gather_len >= 128))) {
			if (!count)
				continue;

			if (count > 1) {
				s16 prev_ld_off = prev_ld->off;
				s16 prev_st_off = prev_st->off;
				s16 head_ld_off = head_ld_meta->insn.off;

				if (prev_ld_off < head_ld_off) {
					head_ld_meta->insn.off = prev_ld_off;
					head_st_meta->insn.off = prev_st_off;
					head_ld_meta->ldst_gather_len =
						-head_ld_meta->ldst_gather_len;
				}

				head_ld_meta->paired_st = &head_st_meta->insn;
				head_st_meta->skip = true;
			} else {
				head_ld_meta->ldst_gather_len = 0;
			}

			/* If the chain is ended by an load/store pair then this
			 * could serve as the new head of the the next chain.
			 */
			if (curr_pair_is_memcpy(meta1, meta2)) {
				head_ld_meta = meta1;
				head_st_meta = meta2;
				head_ld_meta->ldst_gather_len =
					BPF_LDST_BYTES(ld);
				meta1 = nfp_meta_next(meta1);
				meta2 = nfp_meta_next(meta2);
				prev_ld = ld;
				prev_st = st;
				count = 1;
			} else {
				head_ld_meta = NULL;
				head_st_meta = NULL;
				prev_ld = NULL;
				prev_st = NULL;
				count = 0;
			}

			continue;
		}

		if (!head_ld_meta) {
			head_ld_meta = meta1;
			head_st_meta = meta2;
		} else {
			meta1->skip = true;
			meta2->skip = true;
		}

		head_ld_meta->ldst_gather_len += BPF_LDST_BYTES(ld);
		meta1 = nfp_meta_next(meta1);
		meta2 = nfp_meta_next(meta2);
		prev_ld = ld;
		prev_st = st;
		count++;
	}
}

static void nfp_bpf_opt_pkt_cache(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *range_node = NULL;
	s16 range_start = 0, range_end = 0;
	bool cache_avail = false;
	struct bpf_insn *insn;
	s32 range_ptr_off = 0;
	u32 range_ptr_id = 0;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (meta->flags & FLAG_INSN_IS_JUMP_DST)
			cache_avail = false;

		if (meta->skip)
			continue;

		insn = &meta->insn;

		if (is_mbpf_store_pkt(meta) ||
		    insn->code == (BPF_JMP | BPF_CALL) ||
		    is_mbpf_classic_store_pkt(meta) ||
		    is_mbpf_classic_load(meta)) {
			cache_avail = false;
			continue;
		}

		if (!is_mbpf_load(meta))
			continue;

		if (meta->ptr.type != PTR_TO_PACKET || meta->ldst_gather_len) {
			cache_avail = false;
			continue;
		}

		if (!cache_avail) {
			cache_avail = true;
			if (range_node)
				goto end_current_then_start_new;
			goto start_new;
		}

		/* Check ID to make sure two reads share the same
		 * variable offset against PTR_TO_PACKET, and check OFF
		 * to make sure they also share the same constant
		 * offset.
		 *
		 * OFFs don't really need to be the same, because they
		 * are the constant offsets against PTR_TO_PACKET, so
		 * for different OFFs, we could canonicalize them to
		 * offsets against original packet pointer. We don't
		 * support this.
		 */
		if (meta->ptr.id == range_ptr_id &&
		    meta->ptr.off == range_ptr_off) {
			s16 new_start = range_start;
			s16 end, off = insn->off;
			s16 new_end = range_end;
			bool changed = false;

			if (off < range_start) {
				new_start = off;
				changed = true;
			}

			end = off + BPF_LDST_BYTES(insn);
			if (end > range_end) {
				new_end = end;
				changed = true;
			}

			if (!changed)
				continue;

			if (new_end - new_start <= 64) {
				/* Install new range. */
				range_start = new_start;
				range_end = new_end;
				continue;
			}
		}

end_current_then_start_new:
		range_node->pkt_cache.range_start = range_start;
		range_node->pkt_cache.range_end = range_end;
start_new:
		range_node = meta;
		range_node->pkt_cache.do_init = true;
		range_ptr_id = range_node->ptr.id;
		range_ptr_off = range_node->ptr.off;
		range_start = insn->off;
		range_end = insn->off + BPF_LDST_BYTES(insn);
	}

	if (range_node) {
		range_node->pkt_cache.range_start = range_start;
		range_node->pkt_cache.range_end = range_end;
	}

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (meta->skip)
			continue;

		if (is_mbpf_load_pkt(meta) && !meta->ldst_gather_len) {
			if (meta->pkt_cache.do_init) {
				range_start = meta->pkt_cache.range_start;
				range_end = meta->pkt_cache.range_end;
			} else {
				meta->pkt_cache.range_start = range_start;
				meta->pkt_cache.range_end = range_end;
			}
		}
	}
}

static int nfp_bpf_optimize(struct nfp_prog *nfp_prog)
{
	nfp_bpf_opt_reg_init(nfp_prog);

	nfp_bpf_opt_neg_add_sub(nfp_prog);
	nfp_bpf_opt_ld_mask(nfp_prog);
	nfp_bpf_opt_ld_shift(nfp_prog);
	nfp_bpf_opt_ldst_gather(nfp_prog);
	nfp_bpf_opt_pkt_cache(nfp_prog);

	return 0;
}

static int nfp_bpf_replace_map_ptrs(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta1, *meta2;
	struct nfp_bpf_map *nfp_map;
	struct bpf_map *map;

	nfp_for_each_insn_walk2(nfp_prog, meta1, meta2) {
		if (meta1->skip || meta2->skip)
			continue;

		if (meta1->insn.code != (BPF_LD | BPF_IMM | BPF_DW) ||
		    meta1->insn.src_reg != BPF_PSEUDO_MAP_FD)
			continue;

		map = (void *)(unsigned long)((u32)meta1->insn.imm |
					      (u64)meta2->insn.imm << 32);
		if (bpf_map_offload_neutral(map))
			continue;
		nfp_map = map_to_offmap(map)->dev_priv;

		meta1->insn.imm = nfp_map->tid;
		meta2->insn.imm = 0;
	}

	return 0;
}

static int nfp_bpf_ustore_calc(u64 *prog, unsigned int len)
{
	__le64 *ustore = (__force __le64 *)prog;
	int i;

	for (i = 0; i < len; i++) {
		int err;

		err = nfp_ustore_check_valid_no_ecc(prog[i]);
		if (err)
			return err;

		ustore[i] = cpu_to_le64(nfp_ustore_calc_ecc_insn(prog[i]));
	}

	return 0;
}

static void nfp_bpf_prog_trim(struct nfp_prog *nfp_prog)
{
	void *prog;

	prog = kvmalloc_array(nfp_prog->prog_len, sizeof(u64), GFP_KERNEL);
	if (!prog)
		return;

	nfp_prog->__prog_alloc_len = nfp_prog->prog_len * sizeof(u64);
	memcpy(prog, nfp_prog->prog, nfp_prog->__prog_alloc_len);
	kvfree(nfp_prog->prog);
	nfp_prog->prog = prog;
}

int nfp_bpf_jit(struct nfp_prog *nfp_prog)
{
	int ret;

	ret = nfp_bpf_replace_map_ptrs(nfp_prog);
	if (ret)
		return ret;

	ret = nfp_bpf_optimize(nfp_prog);
	if (ret)
		return ret;

	ret = nfp_translate(nfp_prog);
	if (ret) {
		pr_err("Translation failed with error %d (translated: %u)\n",
		       ret, nfp_prog->n_translated);
		return -EINVAL;
	}

	nfp_bpf_prog_trim(nfp_prog);

	return ret;
}

void nfp_bpf_jit_prepare(struct nfp_prog *nfp_prog, unsigned int cnt)
{
	struct nfp_insn_meta *meta;

	/* Another pass to record jump information. */
	list_for_each_entry(meta, &nfp_prog->insns, l) {
		u64 code = meta->insn.code;

		if (BPF_CLASS(code) == BPF_JMP && BPF_OP(code) != BPF_EXIT &&
		    BPF_OP(code) != BPF_CALL) {
			struct nfp_insn_meta *dst_meta;
			unsigned short dst_indx;

			dst_indx = meta->n + 1 + meta->insn.off;
			dst_meta = nfp_bpf_goto_meta(nfp_prog, meta, dst_indx,
						     cnt);

			meta->jmp_dst = dst_meta;
			dst_meta->flags |= FLAG_INSN_IS_JUMP_DST;
		}
	}
}

bool nfp_bpf_supported_opcode(u8 code)
{
	return !!instr_cb[code];
}

void *nfp_bpf_relo_for_vnic(struct nfp_prog *nfp_prog, struct nfp_bpf_vnic *bv)
{
	unsigned int i;
	u64 *prog;
	int err;

	prog = kmemdup(nfp_prog->prog, nfp_prog->prog_len * sizeof(u64),
		       GFP_KERNEL);
	if (!prog)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nfp_prog->prog_len; i++) {
		enum nfp_relo_type special;
		u32 val;

		special = FIELD_GET(OP_RELO_TYPE, prog[i]);
		switch (special) {
		case RELO_NONE:
			continue;
		case RELO_BR_REL:
			br_add_offset(&prog[i], bv->start_off);
			break;
		case RELO_BR_GO_OUT:
			br_set_offset(&prog[i],
				      nfp_prog->tgt_out + bv->start_off);
			break;
		case RELO_BR_GO_ABORT:
			br_set_offset(&prog[i],
				      nfp_prog->tgt_abort + bv->start_off);
			break;
		case RELO_BR_NEXT_PKT:
			br_set_offset(&prog[i], bv->tgt_done);
			break;
		case RELO_BR_HELPER:
			val = br_get_offset(prog[i]);
			val -= BR_OFF_RELO;
			switch (val) {
			case BPF_FUNC_map_lookup_elem:
				val = nfp_prog->bpf->helpers.map_lookup;
				break;
			case BPF_FUNC_map_update_elem:
				val = nfp_prog->bpf->helpers.map_update;
				break;
			case BPF_FUNC_map_delete_elem:
				val = nfp_prog->bpf->helpers.map_delete;
				break;
			case BPF_FUNC_perf_event_output:
				val = nfp_prog->bpf->helpers.perf_event_output;
				break;
			default:
				pr_err("relocation of unknown helper %d\n",
				       val);
				err = -EINVAL;
				goto err_free_prog;
			}
			br_set_offset(&prog[i], val);
			break;
		case RELO_IMMED_REL:
			immed_add_value(&prog[i], bv->start_off);
			break;
		}

		prog[i] &= ~OP_RELO_TYPE;
	}

	err = nfp_bpf_ustore_calc(prog, nfp_prog->prog_len);
	if (err)
		goto err_free_prog;

	return prog;

err_free_prog:
	kfree(prog);
	return ERR_PTR(err);
}
