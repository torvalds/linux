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

#ifndef __NFP_ASM_H__
#define __NFP_ASM_H__ 1

#include <linux/types.h>

#define REG_NONE	0

#define RE_REG_NO_DST	0x020
#define RE_REG_IMM	0x020
#define RE_REG_IMM_encode(x)					\
	(RE_REG_IMM | ((x) & 0x1f) | (((x) & 0x60) << 1))
#define RE_REG_IMM_MAX	 0x07fULL
#define RE_REG_XFR	0x080

#define UR_REG_XFR	0x180
#define UR_REG_NN	0x280
#define UR_REG_NO_DST	0x300
#define UR_REG_IMM	UR_REG_NO_DST
#define UR_REG_IMM_encode(x) (UR_REG_IMM | (x))
#define UR_REG_IMM_MAX	 0x0ffULL

#define OP_BR_BASE	0x0d800000020ULL
#define OP_BR_BASE_MASK	0x0f8000c3ce0ULL
#define OP_BR_MASK	0x0000000001fULL
#define OP_BR_EV_PIP	0x00000000300ULL
#define OP_BR_CSS	0x0000003c000ULL
#define OP_BR_DEFBR	0x00000300000ULL
#define OP_BR_ADDR_LO	0x007ffc00000ULL
#define OP_BR_ADDR_HI	0x10000000000ULL

#define nfp_is_br(_insn)				\
	(((_insn) & OP_BR_BASE_MASK) == OP_BR_BASE)

enum br_mask {
	BR_BEQ = 0x00,
	BR_BNE = 0x01,
	BR_BHS = 0x04,
	BR_BLO = 0x05,
	BR_BGE = 0x08,
	BR_UNC = 0x18,
};

enum br_ev_pip {
	BR_EV_PIP_UNCOND = 0,
	BR_EV_PIP_COND = 1,
};

enum br_ctx_signal_state {
	BR_CSS_NONE = 2,
};

#define OP_BBYTE_BASE	0x0c800000000ULL
#define OP_BB_A_SRC	0x000000000ffULL
#define OP_BB_BYTE	0x00000000300ULL
#define OP_BB_B_SRC	0x0000003fc00ULL
#define OP_BB_I8	0x00000040000ULL
#define OP_BB_EQ	0x00000080000ULL
#define OP_BB_DEFBR	0x00000300000ULL
#define OP_BB_ADDR_LO	0x007ffc00000ULL
#define OP_BB_ADDR_HI	0x10000000000ULL

#define OP_BALU_BASE	0x0e800000000ULL
#define OP_BA_A_SRC	0x000000003ffULL
#define OP_BA_B_SRC	0x000000ffc00ULL
#define OP_BA_DEFBR	0x00000300000ULL
#define OP_BA_ADDR_HI	0x0007fc00000ULL

#define OP_IMMED_A_SRC	0x000000003ffULL
#define OP_IMMED_B_SRC	0x000000ffc00ULL
#define OP_IMMED_IMM	0x0000ff00000ULL
#define OP_IMMED_WIDTH	0x00060000000ULL
#define OP_IMMED_INV	0x00080000000ULL
#define OP_IMMED_SHIFT	0x00600000000ULL
#define OP_IMMED_BASE	0x0f000000000ULL
#define OP_IMMED_WR_AB	0x20000000000ULL

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

#define OP_SHF_BASE	0x08000000000ULL
#define OP_SHF_A_SRC	0x000000000ffULL
#define OP_SHF_SC	0x00000000300ULL
#define OP_SHF_B_SRC	0x0000003fc00ULL
#define OP_SHF_I8	0x00000040000ULL
#define OP_SHF_SW	0x00000080000ULL
#define OP_SHF_DST	0x0000ff00000ULL
#define OP_SHF_SHIFT	0x001f0000000ULL
#define OP_SHF_OP	0x00e00000000ULL
#define OP_SHF_DST_AB	0x01000000000ULL
#define OP_SHF_WR_AB	0x20000000000ULL

enum shf_op {
	SHF_OP_NONE = 0,
	SHF_OP_AND = 2,
	SHF_OP_OR = 5,
};

enum shf_sc {
	SHF_SC_R_ROT = 0,
	SHF_SC_R_SHF = 1,
	SHF_SC_L_SHF = 2,
	SHF_SC_R_DSHF = 3,
};

#define OP_ALU_A_SRC	0x000000003ffULL
#define OP_ALU_B_SRC	0x000000ffc00ULL
#define OP_ALU_DST	0x0003ff00000ULL
#define OP_ALU_SW	0x00040000000ULL
#define OP_ALU_OP	0x00f80000000ULL
#define OP_ALU_DST_AB	0x01000000000ULL
#define OP_ALU_BASE	0x0a000000000ULL
#define OP_ALU_WR_AB	0x20000000000ULL

enum alu_op {
	ALU_OP_NONE	= 0x00,
	ALU_OP_ADD	= 0x01,
	ALU_OP_NEG	= 0x04,
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

#define OP_LDF_BASE	0x0c000000000ULL
#define OP_LDF_A_SRC	0x000000000ffULL
#define OP_LDF_SC	0x00000000300ULL
#define OP_LDF_B_SRC	0x0000003fc00ULL
#define OP_LDF_I8	0x00000040000ULL
#define OP_LDF_SW	0x00000080000ULL
#define OP_LDF_ZF	0x00000100000ULL
#define OP_LDF_BMASK	0x0000f000000ULL
#define OP_LDF_SHF	0x001f0000000ULL
#define OP_LDF_WR_AB	0x20000000000ULL

#define OP_CMD_A_SRC	 0x000000000ffULL
#define OP_CMD_CTX	 0x00000000300ULL
#define OP_CMD_B_SRC	 0x0000003fc00ULL
#define OP_CMD_TOKEN	 0x000000c0000ULL
#define OP_CMD_XFER	 0x00001f00000ULL
#define OP_CMD_CNT	 0x0000e000000ULL
#define OP_CMD_SIG	 0x000f0000000ULL
#define OP_CMD_TGT_CMD	 0x07f00000000ULL
#define OP_CMD_MODE	0x1c0000000000ULL

struct cmd_tgt_act {
	u8 token;
	u8 tgt_cmd;
};

enum cmd_tgt_map {
	CMD_TGT_READ8,
	CMD_TGT_WRITE8,
	CMD_TGT_READ_LE,
	CMD_TGT_READ_SWAP_LE,
	__CMD_TGT_MAP_SIZE,
};

enum cmd_mode {
	CMD_MODE_40b_AB	= 0,
	CMD_MODE_40b_BA	= 1,
	CMD_MODE_32b	= 4,
};

enum cmd_ctx_swap {
	CMD_CTX_SWAP = 0,
	CMD_CTX_NO_SWAP = 3,
};

#define OP_LCSR_BASE	0x0fc00000000ULL
#define OP_LCSR_A_SRC	0x000000003ffULL
#define OP_LCSR_B_SRC	0x000000ffc00ULL
#define OP_LCSR_WRITE	0x00000200000ULL
#define OP_LCSR_ADDR	0x001ffc00000ULL

enum lcsr_wr_src {
	LCSR_WR_AREG,
	LCSR_WR_BREG,
	LCSR_WR_IMM,
};

#define OP_CARB_BASE	0x0e000000000ULL
#define OP_CARB_OR	0x00000010000ULL

#endif
