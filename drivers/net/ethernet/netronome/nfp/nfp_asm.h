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

#ifndef __NFP_ASM_H__
#define __NFP_ASM_H__ 1

#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/types.h>

#define REG_NONE	0
#define REG_WIDTH	4

#define RE_REG_NO_DST	0x020
#define RE_REG_IMM	0x020
#define RE_REG_IMM_encode(x)					\
	(RE_REG_IMM | ((x) & 0x1f) | (((x) & 0x60) << 1))
#define RE_REG_IMM_MAX	 0x07fULL
#define RE_REG_LM	0x050
#define RE_REG_LM_IDX	0x008
#define RE_REG_LM_IDX_MAX	0x7
#define RE_REG_XFR	0x080

#define UR_REG_XFR	0x180
#define UR_REG_LM	0x200
#define UR_REG_LM_IDX	0x020
#define UR_REG_LM_POST_MOD	0x010
#define UR_REG_LM_POST_MOD_DEC	0x001
#define UR_REG_LM_IDX_MAX	0xf
#define UR_REG_NN	0x280
#define UR_REG_NO_DST	0x300
#define UR_REG_IMM	UR_REG_NO_DST
#define UR_REG_IMM_encode(x) (UR_REG_IMM | (x))
#define UR_REG_IMM_MAX	 0x0ffULL

#define OP_BR_BASE		0x0d800000020ULL
#define OP_BR_BASE_MASK		0x0f8000c3ce0ULL
#define OP_BR_MASK		0x0000000001fULL
#define OP_BR_EV_PIP		0x00000000300ULL
#define OP_BR_CSS		0x0000003c000ULL
#define OP_BR_DEFBR		0x00000300000ULL
#define OP_BR_ADDR_LO		0x007ffc00000ULL
#define OP_BR_ADDR_HI		0x10000000000ULL

#define nfp_is_br(_insn)				\
	(((_insn) & OP_BR_BASE_MASK) == OP_BR_BASE)

enum br_mask {
	BR_BEQ = 0x00,
	BR_BNE = 0x01,
	BR_BMI = 0x02,
	BR_BHS = 0x04,
	BR_BLO = 0x05,
	BR_BGE = 0x08,
	BR_BLT = 0x09,
	BR_UNC = 0x18,
};

enum br_ev_pip {
	BR_EV_PIP_UNCOND = 0,
	BR_EV_PIP_COND = 1,
};

enum br_ctx_signal_state {
	BR_CSS_NONE = 2,
};

u16 br_get_offset(u64 instr);
void br_set_offset(u64 *instr, u16 offset);
void br_add_offset(u64 *instr, u16 offset);

#define OP_BBYTE_BASE		0x0c800000000ULL
#define OP_BB_A_SRC		0x000000000ffULL
#define OP_BB_BYTE		0x00000000300ULL
#define OP_BB_B_SRC		0x0000003fc00ULL
#define OP_BB_I8		0x00000040000ULL
#define OP_BB_EQ		0x00000080000ULL
#define OP_BB_DEFBR		0x00000300000ULL
#define OP_BB_ADDR_LO		0x007ffc00000ULL
#define OP_BB_ADDR_HI		0x10000000000ULL
#define OP_BB_SRC_LMEXTN	0x40000000000ULL

#define OP_BALU_BASE		0x0e800000000ULL
#define OP_BA_A_SRC		0x000000003ffULL
#define OP_BA_B_SRC		0x000000ffc00ULL
#define OP_BA_DEFBR		0x00000300000ULL
#define OP_BA_ADDR_HI		0x0007fc00000ULL

#define OP_IMMED_A_SRC		0x000000003ffULL
#define OP_IMMED_B_SRC		0x000000ffc00ULL
#define OP_IMMED_IMM		0x0000ff00000ULL
#define OP_IMMED_WIDTH		0x00060000000ULL
#define OP_IMMED_INV		0x00080000000ULL
#define OP_IMMED_SHIFT		0x00600000000ULL
#define OP_IMMED_BASE		0x0f000000000ULL
#define OP_IMMED_WR_AB		0x20000000000ULL
#define OP_IMMED_SRC_LMEXTN	0x40000000000ULL
#define OP_IMMED_DST_LMEXTN	0x80000000000ULL

enum immed_width {
	IMMED_WIDTH_ALL = 0,
	IMMED_WIDTH_BYTE = 1,
	IMMED_WIDTH_WORD = 2,
};

enum immed_shift {
	IMMED_SHIFT_0B = 0,
	IMMED_SHIFT_1B = 1,
	IMMED_SHIFT_2B = 2,
};

u16 immed_get_value(u64 instr);
void immed_set_value(u64 *instr, u16 immed);
void immed_add_value(u64 *instr, u16 offset);

#define OP_SHF_BASE		0x08000000000ULL
#define OP_SHF_A_SRC		0x000000000ffULL
#define OP_SHF_SC		0x00000000300ULL
#define OP_SHF_B_SRC		0x0000003fc00ULL
#define OP_SHF_I8		0x00000040000ULL
#define OP_SHF_SW		0x00000080000ULL
#define OP_SHF_DST		0x0000ff00000ULL
#define OP_SHF_SHIFT		0x001f0000000ULL
#define OP_SHF_OP		0x00e00000000ULL
#define OP_SHF_DST_AB		0x01000000000ULL
#define OP_SHF_WR_AB		0x20000000000ULL
#define OP_SHF_SRC_LMEXTN	0x40000000000ULL
#define OP_SHF_DST_LMEXTN	0x80000000000ULL

enum shf_op {
	SHF_OP_NONE = 0,
	SHF_OP_AND = 2,
	SHF_OP_OR = 5,
};

enum shf_sc {
	SHF_SC_R_ROT = 0,
	SHF_SC_NONE = SHF_SC_R_ROT,
	SHF_SC_R_SHF = 1,
	SHF_SC_L_SHF = 2,
	SHF_SC_R_DSHF = 3,
};

#define OP_ALU_A_SRC		0x000000003ffULL
#define OP_ALU_B_SRC		0x000000ffc00ULL
#define OP_ALU_DST		0x0003ff00000ULL
#define OP_ALU_SW		0x00040000000ULL
#define OP_ALU_OP		0x00f80000000ULL
#define OP_ALU_DST_AB		0x01000000000ULL
#define OP_ALU_BASE		0x0a000000000ULL
#define OP_ALU_WR_AB		0x20000000000ULL
#define OP_ALU_SRC_LMEXTN	0x40000000000ULL
#define OP_ALU_DST_LMEXTN	0x80000000000ULL

enum alu_op {
	ALU_OP_NONE	= 0x00,
	ALU_OP_ADD	= 0x01,
	ALU_OP_NOT	= 0x04,
	ALU_OP_ADD_2B	= 0x05,
	ALU_OP_AND	= 0x08,
	ALU_OP_SUB_C	= 0x0d,
	ALU_OP_ADD_C	= 0x11,
	ALU_OP_OR	= 0x14,
	ALU_OP_SUB	= 0x15,
	ALU_OP_XOR	= 0x18,
};

enum alu_dst_ab {
	ALU_DST_A = 0,
	ALU_DST_B = 1,
};

#define OP_LDF_BASE		0x0c000000000ULL
#define OP_LDF_A_SRC		0x000000000ffULL
#define OP_LDF_SC		0x00000000300ULL
#define OP_LDF_B_SRC		0x0000003fc00ULL
#define OP_LDF_I8		0x00000040000ULL
#define OP_LDF_SW		0x00000080000ULL
#define OP_LDF_ZF		0x00000100000ULL
#define OP_LDF_BMASK		0x0000f000000ULL
#define OP_LDF_SHF		0x001f0000000ULL
#define OP_LDF_WR_AB		0x20000000000ULL
#define OP_LDF_SRC_LMEXTN	0x40000000000ULL
#define OP_LDF_DST_LMEXTN	0x80000000000ULL

#define OP_CMD_A_SRC		0x000000000ffULL
#define OP_CMD_CTX		0x00000000300ULL
#define OP_CMD_B_SRC		0x0000003fc00ULL
#define OP_CMD_TOKEN		0x000000c0000ULL
#define OP_CMD_XFER		0x00001f00000ULL
#define OP_CMD_CNT		0x0000e000000ULL
#define OP_CMD_SIG		0x000f0000000ULL
#define OP_CMD_TGT_CMD		0x07f00000000ULL
#define OP_CMD_INDIR		0x20000000000ULL
#define OP_CMD_MODE	       0x1c0000000000ULL

struct cmd_tgt_act {
	u8 token;
	u8 tgt_cmd;
};

enum cmd_tgt_map {
	CMD_TGT_READ8,
	CMD_TGT_WRITE8_SWAP,
	CMD_TGT_WRITE32_SWAP,
	CMD_TGT_READ32,
	CMD_TGT_READ32_LE,
	CMD_TGT_READ32_SWAP,
	CMD_TGT_READ_LE,
	CMD_TGT_READ_SWAP_LE,
	__CMD_TGT_MAP_SIZE,
};

extern const struct cmd_tgt_act cmd_tgt_act[__CMD_TGT_MAP_SIZE];

enum cmd_mode {
	CMD_MODE_40b_AB	= 0,
	CMD_MODE_40b_BA	= 1,
	CMD_MODE_32b	= 4,
};

enum cmd_ctx_swap {
	CMD_CTX_SWAP = 0,
	CMD_CTX_NO_SWAP = 3,
};

#define CMD_OVE_LEN	BIT(7)
#define CMD_OV_LEN	GENMASK(12, 8)

#define OP_LCSR_BASE		0x0fc00000000ULL
#define OP_LCSR_A_SRC		0x000000003ffULL
#define OP_LCSR_B_SRC		0x000000ffc00ULL
#define OP_LCSR_WRITE		0x00000200000ULL
#define OP_LCSR_ADDR		0x001ffc00000ULL
#define OP_LCSR_SRC_LMEXTN	0x40000000000ULL
#define OP_LCSR_DST_LMEXTN	0x80000000000ULL

enum lcsr_wr_src {
	LCSR_WR_AREG,
	LCSR_WR_BREG,
	LCSR_WR_IMM,
};

#define OP_CARB_BASE		0x0e000000000ULL
#define OP_CARB_OR		0x00000010000ULL

#define NFP_CSR_CTX_PTR		0x20
#define NFP_CSR_ACT_LM_ADDR0	0x64
#define NFP_CSR_ACT_LM_ADDR1	0x6c
#define NFP_CSR_ACT_LM_ADDR2	0x94
#define NFP_CSR_ACT_LM_ADDR3	0x9c

/* Software register representation, independent of operand type */
#define NN_REG_TYPE	GENMASK(31, 24)
#define NN_REG_LM_IDX	GENMASK(23, 22)
#define NN_REG_LM_IDX_HI	BIT(23)
#define NN_REG_LM_IDX_LO	BIT(22)
#define NN_REG_LM_MOD	GENMASK(21, 20)
#define NN_REG_VAL	GENMASK(7, 0)

enum nfp_bpf_reg_type {
	NN_REG_GPR_A =	BIT(0),
	NN_REG_GPR_B =	BIT(1),
	NN_REG_GPR_BOTH = NN_REG_GPR_A | NN_REG_GPR_B,
	NN_REG_NNR =	BIT(2),
	NN_REG_XFER =	BIT(3),
	NN_REG_IMM =	BIT(4),
	NN_REG_NONE =	BIT(5),
	NN_REG_LMEM =	BIT(6),
};

enum nfp_bpf_lm_mode {
	NN_LM_MOD_NONE = 0,
	NN_LM_MOD_INC,
	NN_LM_MOD_DEC,
};

#define reg_both(x)	__enc_swreg((x), NN_REG_GPR_BOTH)
#define reg_a(x)	__enc_swreg((x), NN_REG_GPR_A)
#define reg_b(x)	__enc_swreg((x), NN_REG_GPR_B)
#define reg_nnr(x)	__enc_swreg((x), NN_REG_NNR)
#define reg_xfer(x)	__enc_swreg((x), NN_REG_XFER)
#define reg_imm(x)	__enc_swreg((x), NN_REG_IMM)
#define reg_none()	__enc_swreg(0, NN_REG_NONE)
#define reg_lm(x, off)	__enc_swreg_lm((x), NN_LM_MOD_NONE, (off))
#define reg_lm_inc(x)	__enc_swreg_lm((x), NN_LM_MOD_INC, 0)
#define reg_lm_dec(x)	__enc_swreg_lm((x), NN_LM_MOD_DEC, 0)
#define __reg_lm(x, mod, off)	__enc_swreg_lm((x), (mod), (off))

typedef __u32 __bitwise swreg;

static inline swreg __enc_swreg(u16 id, u8 type)
{
	return (__force swreg)(id | FIELD_PREP(NN_REG_TYPE, type));
}

static inline swreg __enc_swreg_lm(u8 id, enum nfp_bpf_lm_mode mode, u8 off)
{
	WARN_ON(id > 3 || (off && mode != NN_LM_MOD_NONE));

	return (__force swreg)(FIELD_PREP(NN_REG_TYPE, NN_REG_LMEM) |
			       FIELD_PREP(NN_REG_LM_IDX, id) |
			       FIELD_PREP(NN_REG_LM_MOD, mode) |
			       off);
}

static inline u32 swreg_raw(swreg reg)
{
	return (__force u32)reg;
}

static inline enum nfp_bpf_reg_type swreg_type(swreg reg)
{
	return FIELD_GET(NN_REG_TYPE, swreg_raw(reg));
}

static inline u16 swreg_value(swreg reg)
{
	return FIELD_GET(NN_REG_VAL, swreg_raw(reg));
}

static inline bool swreg_lm_idx(swreg reg)
{
	return FIELD_GET(NN_REG_LM_IDX_LO, swreg_raw(reg));
}

static inline bool swreg_lmextn(swreg reg)
{
	return FIELD_GET(NN_REG_LM_IDX_HI, swreg_raw(reg));
}

static inline enum nfp_bpf_lm_mode swreg_lm_mode(swreg reg)
{
	return FIELD_GET(NN_REG_LM_MOD, swreg_raw(reg));
}

struct nfp_insn_ur_regs {
	enum alu_dst_ab dst_ab;
	u16 dst;
	u16 areg, breg;
	bool swap;
	bool wr_both;
	bool dst_lmextn;
	bool src_lmextn;
};

struct nfp_insn_re_regs {
	enum alu_dst_ab dst_ab;
	u8 dst;
	u8 areg, breg;
	bool swap;
	bool wr_both;
	bool i8;
	bool dst_lmextn;
	bool src_lmextn;
};

int swreg_to_unrestricted(swreg dst, swreg lreg, swreg rreg,
			  struct nfp_insn_ur_regs *reg);
int swreg_to_restricted(swreg dst, swreg lreg, swreg rreg,
			struct nfp_insn_re_regs *reg, bool has_imm8);

#define NFP_USTORE_PREFETCH_WINDOW	8

int nfp_ustore_check_valid_no_ecc(u64 insn);
u64 nfp_ustore_calc_ecc_insn(u64 insn);

#define NFP_IND_ME_REFL_WR_SIG_INIT	3
#define NFP_IND_ME_CTX_PTR_BASE_MASK	GENMASK(9, 0)
#define NFP_IND_NUM_CONTEXTS		8

static inline u32 nfp_get_ind_csr_ctx_ptr_offs(u32 read_offset)
{
	return (read_offset & ~NFP_IND_ME_CTX_PTR_BASE_MASK) | NFP_CSR_CTX_PTR;
}

#endif
