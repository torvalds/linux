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

#define pr_fmt(fmt)	"NFP net bpf: " fmt

#include <linux/kernel.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/pkt_cls.h>
#include <linux/unistd.h>

#include "main.h"
#include "../nfp_asm.h"

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

/* --- Emitters --- */
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
	 u8 mode, u8 xfer, swreg lreg, swreg rreg, u8 size, bool sync)
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
		FIELD_PREP(OP_LCSR_ADDR, addr) |
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

	__emit_lcsr(nfp_prog, reg.areg, reg.breg, true, addr / 4,
		    false, reg.src_lmextn);
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

static void
wrp_br_special(struct nfp_prog *nfp_prog, enum br_mask mask,
	       enum br_special special)
{
	emit_br(nfp_prog, mask, 0, 0);

	nfp_prog->prog[nfp_prog->prog_len - 1] |=
		FIELD_PREP(OP_BR_SPECIAL, special);
}

static void wrp_mov(struct nfp_prog *nfp_prog, swreg dst, swreg src)
{
	emit_alu(nfp_prog, dst, reg_none(), ALU_OP_NONE, src);
}

static void wrp_reg_mov(struct nfp_prog *nfp_prog, u16 dst, u16 src)
{
	wrp_mov(nfp_prog, reg_both(dst), reg_b(src));
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
		 pptr_reg(nfp_prog), offset, sz - 1, true);

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
data_ld_host_order(struct nfp_prog *nfp_prog, u8 src_gpr, swreg offset,
		   u8 dst_gpr, int size)
{
	unsigned int i;
	u8 mask, sz;

	/* We load the value from the address indicated in @offset and then
	 * mask out the data we don't need.  Note: this is little endian!
	 */
	sz = max(size, 4);
	mask = size < 4 ? GENMASK(size - 1, 0) : 0;

	emit_cmd(nfp_prog, CMD_TGT_READ32_SWAP, CMD_MODE_32b, 0,
		 reg_a(src_gpr), offset, sz / 4 - 1, true);

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
	wrp_br_special(nfp_prog, BR_BLO, OP_BR_GO_ABORT);

	/* Load data */
	return data_ld(nfp_prog, imm_b(nfp_prog), 0, size);
}

static int construct_data_ld(struct nfp_prog *nfp_prog, u16 offset, u8 size)
{
	swreg tmp_reg;

	/* Check packet length */
	tmp_reg = ur_load_imm_any(nfp_prog, offset + size, imm_a(nfp_prog));
	emit_alu(nfp_prog, reg_none(), plen_reg(nfp_prog), ALU_OP_SUB, tmp_reg);
	wrp_br_special(nfp_prog, BR_BLO, OP_BR_GO_ABORT);

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
		 reg_a(dst_gpr), offset, size - 1, true);

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
		 reg_a(dst_gpr), offset, size - 1, true);

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

static int
wrp_cmp_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	    enum br_mask br_mask, bool swap)
{
	const struct bpf_insn *insn = &meta->insn;
	u64 imm = insn->imm; /* sign extend */
	u8 reg = insn->dst_reg * 2;
	swreg tmp_reg;

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
	u8 areg, breg;

	areg = insn->dst_reg * 2;
	breg = insn->src_reg * 2;

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

static void wrp_end32(struct nfp_prog *nfp_prog, swreg reg_in, u8 gpr_out)
{
	emit_ld_field(nfp_prog, reg_both(gpr_out), 0xf, reg_in,
		      SHF_SC_R_ROT, 8);
	emit_ld_field(nfp_prog, reg_both(gpr_out), 0x5, reg_a(gpr_out),
		      SHF_SC_R_ROT, 16);
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

static int shl_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;

	if (insn->imm < 32) {
		emit_shf(nfp_prog, reg_both(dst + 1),
			 reg_a(dst + 1), SHF_OP_NONE, reg_b(dst),
			 SHF_SC_R_DSHF, 32 - insn->imm);
		emit_shf(nfp_prog, reg_both(dst),
			 reg_none(), SHF_OP_NONE, reg_b(dst),
			 SHF_SC_L_SHF, insn->imm);
	} else if (insn->imm == 32) {
		wrp_reg_mov(nfp_prog, dst + 1, dst);
		wrp_immed(nfp_prog, reg_both(dst), 0);
	} else if (insn->imm > 32) {
		emit_shf(nfp_prog, reg_both(dst + 1),
			 reg_none(), SHF_OP_NONE, reg_b(dst),
			 SHF_SC_L_SHF, insn->imm - 32);
		wrp_immed(nfp_prog, reg_both(dst), 0);
	}

	return 0;
}

static int shr_imm64(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	const struct bpf_insn *insn = &meta->insn;
	u8 dst = insn->dst_reg * 2;

	if (insn->imm < 32) {
		emit_shf(nfp_prog, reg_both(dst),
			 reg_a(dst + 1), SHF_OP_NONE, reg_b(dst),
			 SHF_SC_R_DSHF, insn->imm);
		emit_shf(nfp_prog, reg_both(dst + 1),
			 reg_none(), SHF_OP_NONE, reg_b(dst + 1),
			 SHF_SC_R_SHF, insn->imm);
	} else if (insn->imm == 32) {
		wrp_reg_mov(nfp_prog, dst, dst + 1);
		wrp_immed(nfp_prog, reg_both(dst + 1), 0);
	} else if (insn->imm > 32) {
		emit_shf(nfp_prog, reg_both(dst),
			 reg_none(), SHF_OP_NONE, reg_b(dst + 1),
			 SHF_SC_R_SHF, insn->imm - 32);
		wrp_immed(nfp_prog, reg_both(dst + 1), 0);
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

	return data_ld_host_order(nfp_prog, meta->insn.src_reg * 2, tmp_reg,
				  meta->insn.dst_reg * 2, size);
}

static int
mem_ldx(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
	unsigned int size)
{
	if (meta->ptr.type == PTR_TO_CTX) {
		if (nfp_prog->type == BPF_PROG_TYPE_XDP)
			return mem_ldx_xdp(nfp_prog, meta, size);
		else
			return mem_ldx_skb(nfp_prog, meta, size);
	}

	if (meta->ptr.type == PTR_TO_PACKET)
		return mem_ldx_data(nfp_prog, meta, size);

	if (meta->ptr.type == PTR_TO_STACK)
		return mem_ldx_stack(nfp_prog, meta, size,
				     meta->ptr.off + meta->ptr.var_off.value);

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
	return mem_stx(nfp_prog, meta, 4);
}

static int mem_stx8(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return mem_stx(nfp_prog, meta, 8);
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

static int jgt_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BLO, true);
}

static int jge_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BHS, false);
}

static int jlt_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BLO, false);
}

static int jle_imm(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_imm(nfp_prog, meta, BR_BHS, true);
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

static int jgt_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_reg(nfp_prog, meta, BR_BLO, true);
}

static int jge_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_reg(nfp_prog, meta, BR_BHS, false);
}

static int jlt_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
{
	return wrp_cmp_reg(nfp_prog, meta, BR_BLO, false);
}

static int jle_reg(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta)
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
	[BPF_ALU64 | BPF_NEG] =		neg_reg64,
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
	[BPF_ST | BPF_MEM | BPF_B] =	mem_st1,
	[BPF_ST | BPF_MEM | BPF_H] =	mem_st2,
	[BPF_ST | BPF_MEM | BPF_W] =	mem_st4,
	[BPF_ST | BPF_MEM | BPF_DW] =	mem_st8,
	[BPF_JMP | BPF_JA | BPF_K] =	jump,
	[BPF_JMP | BPF_JEQ | BPF_K] =	jeq_imm,
	[BPF_JMP | BPF_JGT | BPF_K] =	jgt_imm,
	[BPF_JMP | BPF_JGE | BPF_K] =	jge_imm,
	[BPF_JMP | BPF_JLT | BPF_K] =	jlt_imm,
	[BPF_JMP | BPF_JLE | BPF_K] =	jle_imm,
	[BPF_JMP | BPF_JSET | BPF_K] =	jset_imm,
	[BPF_JMP | BPF_JNE | BPF_K] =	jne_imm,
	[BPF_JMP | BPF_JEQ | BPF_X] =	jeq_reg,
	[BPF_JMP | BPF_JGT | BPF_X] =	jgt_reg,
	[BPF_JMP | BPF_JGE | BPF_X] =	jge_reg,
	[BPF_JMP | BPF_JLT | BPF_X] =	jlt_reg,
	[BPF_JMP | BPF_JLE | BPF_X] =	jle_reg,
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
	struct nfp_insn_meta *meta, *jmp_dst;
	u32 idx, br_idx;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (meta->skip)
			continue;
		if (BPF_CLASS(meta->insn.code) != BPF_JMP)
			continue;

		if (list_is_last(&meta->l, &nfp_prog->insns))
			idx = nfp_prog->last_bpf_off;
		else
			idx = list_next_entry(meta, l)->off - 1;

		br_idx = nfp_prog_offset_to_index(nfp_prog, idx);

		if (!nfp_is_br(nfp_prog->prog[br_idx])) {
			pr_err("Fixup found block not ending in branch %d %02x %016llx!!\n",
			       br_idx, meta->insn.code, nfp_prog->prog[br_idx]);
			return -ELOOP;
		}
		/* Leave special branches for later */
		if (FIELD_GET(OP_BR_SPECIAL, nfp_prog->prog[br_idx]))
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

		for (idx = nfp_prog_offset_to_index(nfp_prog, meta->off);
		     idx <= br_idx; idx++) {
			if (!nfp_is_br(nfp_prog->prog[idx]))
				continue;
			br_set_offset(&nfp_prog->prog[idx], jmp_dst->off);
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

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

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

	emit_br_def(nfp_prog, nfp_prog->tgt_done, 2);

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

static int nfp_bpf_optimize(struct nfp_prog *nfp_prog)
{
	nfp_bpf_opt_reg_init(nfp_prog);

	nfp_bpf_opt_ld_mask(nfp_prog);
	nfp_bpf_opt_ld_shift(nfp_prog);

	return 0;
}

static int nfp_bpf_ustore_calc(struct nfp_prog *nfp_prog, __le64 *ustore)
{
	int i;

	for (i = 0; i < nfp_prog->prog_len; i++) {
		int err;

		err = nfp_ustore_check_valid_no_ecc(nfp_prog->prog[i]);
		if (err)
			return err;

		nfp_prog->prog[i] = nfp_ustore_calc_ecc_insn(nfp_prog->prog[i]);

		ustore[i] = cpu_to_le64(nfp_prog->prog[i]);
	}

	return 0;
}

int nfp_bpf_jit(struct nfp_prog *nfp_prog)
{
	int ret;

	ret = nfp_bpf_optimize(nfp_prog);
	if (ret)
		return ret;

	ret = nfp_translate(nfp_prog);
	if (ret) {
		pr_err("Translation failed with error %d (translated: %u)\n",
		       ret, nfp_prog->n_translated);
		return -EINVAL;
	}

	return nfp_bpf_ustore_calc(nfp_prog, (__force __le64 *)nfp_prog->prog);
}
