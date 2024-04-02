/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common functionality for RV32 and RV64 BPF JIT compilers
 *
 * Copyright (c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#include <linux/bpf.h>
#include <linux/filter.h>
#include <asm/cacheflush.h>

static inline bool rvc_enabled(void)
{
	return IS_ENABLED(CONFIG_RISCV_ISA_C);
}

static inline bool rvzbb_enabled(void)
{
	return IS_ENABLED(CONFIG_RISCV_ISA_ZBB) && riscv_has_extension_likely(RISCV_ISA_EXT_ZBB);
}

enum {
	RV_REG_ZERO =	0,	/* The constant value 0 */
	RV_REG_RA =	1,	/* Return address */
	RV_REG_SP =	2,	/* Stack pointer */
	RV_REG_GP =	3,	/* Global pointer */
	RV_REG_TP =	4,	/* Thread pointer */
	RV_REG_T0 =	5,	/* Temporaries */
	RV_REG_T1 =	6,
	RV_REG_T2 =	7,
	RV_REG_FP =	8,	/* Saved register/frame pointer */
	RV_REG_S1 =	9,	/* Saved register */
	RV_REG_A0 =	10,	/* Function argument/return values */
	RV_REG_A1 =	11,	/* Function arguments */
	RV_REG_A2 =	12,
	RV_REG_A3 =	13,
	RV_REG_A4 =	14,
	RV_REG_A5 =	15,
	RV_REG_A6 =	16,
	RV_REG_A7 =	17,
	RV_REG_S2 =	18,	/* Saved registers */
	RV_REG_S3 =	19,
	RV_REG_S4 =	20,
	RV_REG_S5 =	21,
	RV_REG_S6 =	22,
	RV_REG_S7 =	23,
	RV_REG_S8 =	24,
	RV_REG_S9 =	25,
	RV_REG_S10 =	26,
	RV_REG_S11 =	27,
	RV_REG_T3 =	28,	/* Temporaries */
	RV_REG_T4 =	29,
	RV_REG_T5 =	30,
	RV_REG_T6 =	31,
};

static inline bool is_creg(u8 reg)
{
	return (1 << reg) & (BIT(RV_REG_FP) |
			     BIT(RV_REG_S1) |
			     BIT(RV_REG_A0) |
			     BIT(RV_REG_A1) |
			     BIT(RV_REG_A2) |
			     BIT(RV_REG_A3) |
			     BIT(RV_REG_A4) |
			     BIT(RV_REG_A5));
}

struct rv_jit_context {
	struct bpf_prog *prog;
	u16 *insns;		/* RV insns */
	u16 *ro_insns;
	int ninsns;
	int prologue_len;
	int epilogue_offset;
	int *offset;		/* BPF to RV */
	int nexentries;
	unsigned long flags;
	int stack_size;
};

/* Convert from ninsns to bytes. */
static inline int ninsns_rvoff(int ninsns)
{
	return ninsns << 1;
}

struct rv_jit_data {
	struct bpf_binary_header *header;
	struct bpf_binary_header *ro_header;
	u8 *image;
	u8 *ro_image;
	struct rv_jit_context ctx;
};

static inline void bpf_fill_ill_insns(void *area, unsigned int size)
{
	memset(area, 0, size);
}

static inline void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

/* Emit a 4-byte riscv instruction. */
static inline void emit(const u32 insn, struct rv_jit_context *ctx)
{
	if (ctx->insns) {
		ctx->insns[ctx->ninsns] = insn;
		ctx->insns[ctx->ninsns + 1] = (insn >> 16);
	}

	ctx->ninsns += 2;
}

/* Emit a 2-byte riscv compressed instruction. */
static inline void emitc(const u16 insn, struct rv_jit_context *ctx)
{
	BUILD_BUG_ON(!rvc_enabled());

	if (ctx->insns)
		ctx->insns[ctx->ninsns] = insn;

	ctx->ninsns++;
}

static inline int epilogue_offset(struct rv_jit_context *ctx)
{
	int to = ctx->epilogue_offset, from = ctx->ninsns;

	return ninsns_rvoff(to - from);
}

/* Return -1 or inverted cond. */
static inline int invert_bpf_cond(u8 cond)
{
	switch (cond) {
	case BPF_JEQ:
		return BPF_JNE;
	case BPF_JGT:
		return BPF_JLE;
	case BPF_JLT:
		return BPF_JGE;
	case BPF_JGE:
		return BPF_JLT;
	case BPF_JLE:
		return BPF_JGT;
	case BPF_JNE:
		return BPF_JEQ;
	case BPF_JSGT:
		return BPF_JSLE;
	case BPF_JSLT:
		return BPF_JSGE;
	case BPF_JSGE:
		return BPF_JSLT;
	case BPF_JSLE:
		return BPF_JSGT;
	}
	return -1;
}

static inline bool is_6b_int(long val)
{
	return -(1L << 5) <= val && val < (1L << 5);
}

static inline bool is_7b_uint(unsigned long val)
{
	return val < (1UL << 7);
}

static inline bool is_8b_uint(unsigned long val)
{
	return val < (1UL << 8);
}

static inline bool is_9b_uint(unsigned long val)
{
	return val < (1UL << 9);
}

static inline bool is_10b_int(long val)
{
	return -(1L << 9) <= val && val < (1L << 9);
}

static inline bool is_10b_uint(unsigned long val)
{
	return val < (1UL << 10);
}

static inline bool is_12b_int(long val)
{
	return -(1L << 11) <= val && val < (1L << 11);
}

static inline int is_12b_check(int off, int insn)
{
	if (!is_12b_int(off)) {
		pr_err("bpf-jit: insn=%d 12b < offset=%d not supported yet!\n",
		       insn, (int)off);
		return -1;
	}
	return 0;
}

static inline bool is_13b_int(long val)
{
	return -(1L << 12) <= val && val < (1L << 12);
}

static inline bool is_21b_int(long val)
{
	return -(1L << 20) <= val && val < (1L << 20);
}

static inline int rv_offset(int insn, int off, struct rv_jit_context *ctx)
{
	int from, to;

	off++; /* BPF branch is from PC+1, RV is from PC */
	from = (insn > 0) ? ctx->offset[insn - 1] : ctx->prologue_len;
	to = (insn + off > 0) ? ctx->offset[insn + off - 1] : ctx->prologue_len;
	return ninsns_rvoff(to - from);
}

/* Instruction formats. */

static inline u32 rv_r_insn(u8 funct7, u8 rs2, u8 rs1, u8 funct3, u8 rd,
			    u8 opcode)
{
	return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(rd << 7) | opcode;
}

static inline u32 rv_i_insn(u16 imm11_0, u8 rs1, u8 funct3, u8 rd, u8 opcode)
{
	return (imm11_0 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) |
		opcode;
}

static inline u32 rv_s_insn(u16 imm11_0, u8 rs2, u8 rs1, u8 funct3, u8 opcode)
{
	u8 imm11_5 = imm11_0 >> 5, imm4_0 = imm11_0 & 0x1f;

	return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(imm4_0 << 7) | opcode;
}

static inline u32 rv_b_insn(u16 imm12_1, u8 rs2, u8 rs1, u8 funct3, u8 opcode)
{
	u8 imm12 = ((imm12_1 & 0x800) >> 5) | ((imm12_1 & 0x3f0) >> 4);
	u8 imm4_1 = ((imm12_1 & 0xf) << 1) | ((imm12_1 & 0x400) >> 10);

	return (imm12 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(imm4_1 << 7) | opcode;
}

static inline u32 rv_u_insn(u32 imm31_12, u8 rd, u8 opcode)
{
	return (imm31_12 << 12) | (rd << 7) | opcode;
}

static inline u32 rv_j_insn(u32 imm20_1, u8 rd, u8 opcode)
{
	u32 imm;

	imm = (imm20_1 & 0x80000) | ((imm20_1 & 0x3ff) << 9) |
		((imm20_1 & 0x400) >> 2) | ((imm20_1 & 0x7f800) >> 11);

	return (imm << 12) | (rd << 7) | opcode;
}

static inline u32 rv_amo_insn(u8 funct5, u8 aq, u8 rl, u8 rs2, u8 rs1,
			      u8 funct3, u8 rd, u8 opcode)
{
	u8 funct7 = (funct5 << 2) | (aq << 1) | rl;

	return rv_r_insn(funct7, rs2, rs1, funct3, rd, opcode);
}

/* RISC-V compressed instruction formats. */

static inline u16 rv_cr_insn(u8 funct4, u8 rd, u8 rs2, u8 op)
{
	return (funct4 << 12) | (rd << 7) | (rs2 << 2) | op;
}

static inline u16 rv_ci_insn(u8 funct3, u32 imm6, u8 rd, u8 op)
{
	u32 imm;

	imm = ((imm6 & 0x20) << 7) | ((imm6 & 0x1f) << 2);
	return (funct3 << 13) | (rd << 7) | op | imm;
}

static inline u16 rv_css_insn(u8 funct3, u32 uimm, u8 rs2, u8 op)
{
	return (funct3 << 13) | (uimm << 7) | (rs2 << 2) | op;
}

static inline u16 rv_ciw_insn(u8 funct3, u32 uimm, u8 rd, u8 op)
{
	return (funct3 << 13) | (uimm << 5) | ((rd & 0x7) << 2) | op;
}

static inline u16 rv_cl_insn(u8 funct3, u32 imm_hi, u8 rs1, u32 imm_lo, u8 rd,
			     u8 op)
{
	return (funct3 << 13) | (imm_hi << 10) | ((rs1 & 0x7) << 7) |
		(imm_lo << 5) | ((rd & 0x7) << 2) | op;
}

static inline u16 rv_cs_insn(u8 funct3, u32 imm_hi, u8 rs1, u32 imm_lo, u8 rs2,
			     u8 op)
{
	return (funct3 << 13) | (imm_hi << 10) | ((rs1 & 0x7) << 7) |
		(imm_lo << 5) | ((rs2 & 0x7) << 2) | op;
}

static inline u16 rv_ca_insn(u8 funct6, u8 rd, u8 funct2, u8 rs2, u8 op)
{
	return (funct6 << 10) | ((rd & 0x7) << 7) | (funct2 << 5) |
		((rs2 & 0x7) << 2) | op;
}

static inline u16 rv_cb_insn(u8 funct3, u32 imm6, u8 funct2, u8 rd, u8 op)
{
	u32 imm;

	imm = ((imm6 & 0x20) << 7) | ((imm6 & 0x1f) << 2);
	return (funct3 << 13) | (funct2 << 10) | ((rd & 0x7) << 7) | op | imm;
}

/* Instructions shared by both RV32 and RV64. */

static inline u32 rv_addi(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x13);
}

static inline u32 rv_andi(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 7, rd, 0x13);
}

static inline u32 rv_ori(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 6, rd, 0x13);
}

static inline u32 rv_xori(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 4, rd, 0x13);
}

static inline u32 rv_slli(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 1, rd, 0x13);
}

static inline u32 rv_srli(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x13);
}

static inline u32 rv_srai(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(0x400 | imm11_0, rs1, 5, rd, 0x13);
}

static inline u32 rv_lui(u8 rd, u32 imm31_12)
{
	return rv_u_insn(imm31_12, rd, 0x37);
}

static inline u32 rv_auipc(u8 rd, u32 imm31_12)
{
	return rv_u_insn(imm31_12, rd, 0x17);
}

static inline u32 rv_add(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 0, rd, 0x33);
}

static inline u32 rv_sub(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 0, rd, 0x33);
}

static inline u32 rv_sltu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 3, rd, 0x33);
}

static inline u32 rv_and(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 7, rd, 0x33);
}

static inline u32 rv_or(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 6, rd, 0x33);
}

static inline u32 rv_xor(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 4, rd, 0x33);
}

static inline u32 rv_sll(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 1, rd, 0x33);
}

static inline u32 rv_srl(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 5, rd, 0x33);
}

static inline u32 rv_sra(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 5, rd, 0x33);
}

static inline u32 rv_mul(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 0, rd, 0x33);
}

static inline u32 rv_mulhu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 3, rd, 0x33);
}

static inline u32 rv_div(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 4, rd, 0x33);
}

static inline u32 rv_divu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 5, rd, 0x33);
}

static inline u32 rv_rem(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 6, rd, 0x33);
}

static inline u32 rv_remu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 7, rd, 0x33);
}

static inline u32 rv_jal(u8 rd, u32 imm20_1)
{
	return rv_j_insn(imm20_1, rd, 0x6f);
}

static inline u32 rv_jalr(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x67);
}

static inline u32 rv_beq(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 0, 0x63);
}

static inline u32 rv_bne(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 1, 0x63);
}

static inline u32 rv_bltu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 6, 0x63);
}

static inline u32 rv_bgtu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_bltu(rs2, rs1, imm12_1);
}

static inline u32 rv_bgeu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 7, 0x63);
}

static inline u32 rv_bleu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_bgeu(rs2, rs1, imm12_1);
}

static inline u32 rv_blt(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 4, 0x63);
}

static inline u32 rv_bgt(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_blt(rs2, rs1, imm12_1);
}

static inline u32 rv_bge(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_b_insn(imm12_1, rs2, rs1, 5, 0x63);
}

static inline u32 rv_ble(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_bge(rs2, rs1, imm12_1);
}

static inline u32 rv_lb(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x03);
}

static inline u32 rv_lh(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 1, rd, 0x03);
}

static inline u32 rv_lw(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 2, rd, 0x03);
}

static inline u32 rv_lbu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 4, rd, 0x03);
}

static inline u32 rv_lhu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x03);
}

static inline u32 rv_sb(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 0, 0x23);
}

static inline u32 rv_sh(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 1, 0x23);
}

static inline u32 rv_sw(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 2, 0x23);
}

static inline u32 rv_amoadd_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_amoand_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0xc, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_amoor_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x8, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_amoxor_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x4, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_amoswap_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x1, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_lr_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x2, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_sc_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x3, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static inline u32 rv_fence(u8 pred, u8 succ)
{
	u16 imm11_0 = pred << 4 | succ;

	return rv_i_insn(imm11_0, 0, 0, 0, 0xf);
}

static inline u32 rv_nop(void)
{
	return rv_i_insn(0, 0, 0, 0, 0x13);
}

/* RVC instrutions. */

static inline u16 rvc_addi4spn(u8 rd, u32 imm10)
{
	u32 imm;

	imm = ((imm10 & 0x30) << 2) | ((imm10 & 0x3c0) >> 4) |
		((imm10 & 0x4) >> 1) | ((imm10 & 0x8) >> 3);
	return rv_ciw_insn(0x0, imm, rd, 0x0);
}

static inline u16 rvc_lw(u8 rd, u32 imm7, u8 rs1)
{
	u32 imm_hi, imm_lo;

	imm_hi = (imm7 & 0x38) >> 3;
	imm_lo = ((imm7 & 0x4) >> 1) | ((imm7 & 0x40) >> 6);
	return rv_cl_insn(0x2, imm_hi, rs1, imm_lo, rd, 0x0);
}

static inline u16 rvc_sw(u8 rs1, u32 imm7, u8 rs2)
{
	u32 imm_hi, imm_lo;

	imm_hi = (imm7 & 0x38) >> 3;
	imm_lo = ((imm7 & 0x4) >> 1) | ((imm7 & 0x40) >> 6);
	return rv_cs_insn(0x6, imm_hi, rs1, imm_lo, rs2, 0x0);
}

static inline u16 rvc_addi(u8 rd, u32 imm6)
{
	return rv_ci_insn(0, imm6, rd, 0x1);
}

static inline u16 rvc_li(u8 rd, u32 imm6)
{
	return rv_ci_insn(0x2, imm6, rd, 0x1);
}

static inline u16 rvc_addi16sp(u32 imm10)
{
	u32 imm;

	imm = ((imm10 & 0x200) >> 4) | (imm10 & 0x10) | ((imm10 & 0x40) >> 3) |
		((imm10 & 0x180) >> 6) | ((imm10 & 0x20) >> 5);
	return rv_ci_insn(0x3, imm, RV_REG_SP, 0x1);
}

static inline u16 rvc_lui(u8 rd, u32 imm6)
{
	return rv_ci_insn(0x3, imm6, rd, 0x1);
}

static inline u16 rvc_srli(u8 rd, u32 imm6)
{
	return rv_cb_insn(0x4, imm6, 0, rd, 0x1);
}

static inline u16 rvc_srai(u8 rd, u32 imm6)
{
	return rv_cb_insn(0x4, imm6, 0x1, rd, 0x1);
}

static inline u16 rvc_andi(u8 rd, u32 imm6)
{
	return rv_cb_insn(0x4, imm6, 0x2, rd, 0x1);
}

static inline u16 rvc_sub(u8 rd, u8 rs)
{
	return rv_ca_insn(0x23, rd, 0, rs, 0x1);
}

static inline u16 rvc_xor(u8 rd, u8 rs)
{
	return rv_ca_insn(0x23, rd, 0x1, rs, 0x1);
}

static inline u16 rvc_or(u8 rd, u8 rs)
{
	return rv_ca_insn(0x23, rd, 0x2, rs, 0x1);
}

static inline u16 rvc_and(u8 rd, u8 rs)
{
	return rv_ca_insn(0x23, rd, 0x3, rs, 0x1);
}

static inline u16 rvc_slli(u8 rd, u32 imm6)
{
	return rv_ci_insn(0, imm6, rd, 0x2);
}

static inline u16 rvc_lwsp(u8 rd, u32 imm8)
{
	u32 imm;

	imm = ((imm8 & 0xc0) >> 6) | (imm8 & 0x3c);
	return rv_ci_insn(0x2, imm, rd, 0x2);
}

static inline u16 rvc_jr(u8 rs1)
{
	return rv_cr_insn(0x8, rs1, RV_REG_ZERO, 0x2);
}

static inline u16 rvc_mv(u8 rd, u8 rs)
{
	return rv_cr_insn(0x8, rd, rs, 0x2);
}

static inline u16 rvc_jalr(u8 rs1)
{
	return rv_cr_insn(0x9, rs1, RV_REG_ZERO, 0x2);
}

static inline u16 rvc_add(u8 rd, u8 rs)
{
	return rv_cr_insn(0x9, rd, rs, 0x2);
}

static inline u16 rvc_swsp(u32 imm8, u8 rs2)
{
	u32 imm;

	imm = (imm8 & 0x3c) | ((imm8 & 0xc0) >> 6);
	return rv_css_insn(0x6, imm, rs2, 0x2);
}

/* RVZBB instrutions. */
static inline u32 rvzbb_sextb(u8 rd, u8 rs1)
{
	return rv_i_insn(0x604, rs1, 1, rd, 0x13);
}

static inline u32 rvzbb_sexth(u8 rd, u8 rs1)
{
	return rv_i_insn(0x605, rs1, 1, rd, 0x13);
}

static inline u32 rvzbb_zexth(u8 rd, u8 rs)
{
	if (IS_ENABLED(CONFIG_64BIT))
		return rv_i_insn(0x80, rs, 4, rd, 0x3b);

	return rv_i_insn(0x80, rs, 4, rd, 0x33);
}

static inline u32 rvzbb_rev8(u8 rd, u8 rs)
{
	if (IS_ENABLED(CONFIG_64BIT))
		return rv_i_insn(0x6b8, rs, 5, rd, 0x13);

	return rv_i_insn(0x698, rs, 5, rd, 0x13);
}

/*
 * RV64-only instructions.
 *
 * These instructions are not available on RV32.  Wrap them below a #if to
 * ensure that the RV32 JIT doesn't emit any of these instructions.
 */

#if __riscv_xlen == 64

static inline u32 rv_addiw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x1b);
}

static inline u32 rv_slliw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 1, rd, 0x1b);
}

static inline u32 rv_srliw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x1b);
}

static inline u32 rv_sraiw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(0x400 | imm11_0, rs1, 5, rd, 0x1b);
}

static inline u32 rv_addw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 0, rd, 0x3b);
}

static inline u32 rv_subw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 0, rd, 0x3b);
}

static inline u32 rv_sllw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 1, rd, 0x3b);
}

static inline u32 rv_srlw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 5, rd, 0x3b);
}

static inline u32 rv_sraw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 5, rd, 0x3b);
}

static inline u32 rv_mulw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 0, rd, 0x3b);
}

static inline u32 rv_divw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 4, rd, 0x3b);
}

static inline u32 rv_divuw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 5, rd, 0x3b);
}

static inline u32 rv_remw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 6, rd, 0x3b);
}

static inline u32 rv_remuw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 7, rd, 0x3b);
}

static inline u32 rv_ld(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 3, rd, 0x03);
}

static inline u32 rv_lwu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 6, rd, 0x03);
}

static inline u32 rv_sd(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 3, 0x23);
}

static inline u32 rv_amoadd_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_amoand_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0xc, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_amoor_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x8, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_amoxor_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x4, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_amoswap_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x1, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_lr_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x2, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static inline u32 rv_sc_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0x3, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

/* RV64-only RVC instructions. */

static inline u16 rvc_ld(u8 rd, u32 imm8, u8 rs1)
{
	u32 imm_hi, imm_lo;

	imm_hi = (imm8 & 0x38) >> 3;
	imm_lo = (imm8 & 0xc0) >> 6;
	return rv_cl_insn(0x3, imm_hi, rs1, imm_lo, rd, 0x0);
}

static inline u16 rvc_sd(u8 rs1, u32 imm8, u8 rs2)
{
	u32 imm_hi, imm_lo;

	imm_hi = (imm8 & 0x38) >> 3;
	imm_lo = (imm8 & 0xc0) >> 6;
	return rv_cs_insn(0x7, imm_hi, rs1, imm_lo, rs2, 0x0);
}

static inline u16 rvc_subw(u8 rd, u8 rs)
{
	return rv_ca_insn(0x27, rd, 0, rs, 0x1);
}

static inline u16 rvc_addiw(u8 rd, u32 imm6)
{
	return rv_ci_insn(0x1, imm6, rd, 0x1);
}

static inline u16 rvc_ldsp(u8 rd, u32 imm9)
{
	u32 imm;

	imm = ((imm9 & 0x1c0) >> 6) | (imm9 & 0x38);
	return rv_ci_insn(0x3, imm, rd, 0x2);
}

static inline u16 rvc_sdsp(u32 imm9, u8 rs2)
{
	u32 imm;

	imm = (imm9 & 0x38) | ((imm9 & 0x1c0) >> 6);
	return rv_css_insn(0x7, imm, rs2, 0x2);
}

#endif /* __riscv_xlen == 64 */

/* Helper functions that emit RVC instructions when possible. */

static inline void emit_jalr(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd == RV_REG_RA && rs && !imm)
		emitc(rvc_jalr(rs), ctx);
	else if (rvc_enabled() && !rd && rs && !imm)
		emitc(rvc_jr(rs), ctx);
	else
		emit(rv_jalr(rd, rs, imm), ctx);
}

static inline void emit_mv(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && rs)
		emitc(rvc_mv(rd, rs), ctx);
	else
		emit(rv_addi(rd, rs, 0), ctx);
}

static inline void emit_add(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && rd == rs1 && rs2)
		emitc(rvc_add(rd, rs2), ctx);
	else
		emit(rv_add(rd, rs1, rs2), ctx);
}

static inline void emit_addi(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd == RV_REG_SP && rd == rs && is_10b_int(imm) && imm && !(imm & 0xf))
		emitc(rvc_addi16sp(imm), ctx);
	else if (rvc_enabled() && is_creg(rd) && rs == RV_REG_SP && is_10b_uint(imm) &&
		 !(imm & 0x3) && imm)
		emitc(rvc_addi4spn(rd, imm), ctx);
	else if (rvc_enabled() && rd && rd == rs && imm && is_6b_int(imm))
		emitc(rvc_addi(rd, imm), ctx);
	else
		emit(rv_addi(rd, rs, imm), ctx);
}

static inline void emit_li(u8 rd, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && is_6b_int(imm))
		emitc(rvc_li(rd, imm), ctx);
	else
		emit(rv_addi(rd, RV_REG_ZERO, imm), ctx);
}

static inline void emit_lui(u8 rd, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && rd != RV_REG_SP && is_6b_int(imm) && imm)
		emitc(rvc_lui(rd, imm), ctx);
	else
		emit(rv_lui(rd, imm), ctx);
}

static inline void emit_slli(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && rd == rs && imm && (u32)imm < __riscv_xlen)
		emitc(rvc_slli(rd, imm), ctx);
	else
		emit(rv_slli(rd, rs, imm), ctx);
}

static inline void emit_andi(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs && is_6b_int(imm))
		emitc(rvc_andi(rd, imm), ctx);
	else
		emit(rv_andi(rd, rs, imm), ctx);
}

static inline void emit_srli(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs && imm && (u32)imm < __riscv_xlen)
		emitc(rvc_srli(rd, imm), ctx);
	else
		emit(rv_srli(rd, rs, imm), ctx);
}

static inline void emit_srai(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs && imm && (u32)imm < __riscv_xlen)
		emitc(rvc_srai(rd, imm), ctx);
	else
		emit(rv_srai(rd, rs, imm), ctx);
}

static inline void emit_sub(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs1 && is_creg(rs2))
		emitc(rvc_sub(rd, rs2), ctx);
	else
		emit(rv_sub(rd, rs1, rs2), ctx);
}

static inline void emit_or(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs1 && is_creg(rs2))
		emitc(rvc_or(rd, rs2), ctx);
	else
		emit(rv_or(rd, rs1, rs2), ctx);
}

static inline void emit_and(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs1 && is_creg(rs2))
		emitc(rvc_and(rd, rs2), ctx);
	else
		emit(rv_and(rd, rs1, rs2), ctx);
}

static inline void emit_xor(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs1 && is_creg(rs2))
		emitc(rvc_xor(rd, rs2), ctx);
	else
		emit(rv_xor(rd, rs1, rs2), ctx);
}

static inline void emit_lw(u8 rd, s32 off, u8 rs1, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rs1 == RV_REG_SP && rd && is_8b_uint(off) && !(off & 0x3))
		emitc(rvc_lwsp(rd, off), ctx);
	else if (rvc_enabled() && is_creg(rd) && is_creg(rs1) && is_7b_uint(off) && !(off & 0x3))
		emitc(rvc_lw(rd, off, rs1), ctx);
	else
		emit(rv_lw(rd, off, rs1), ctx);
}

static inline void emit_sw(u8 rs1, s32 off, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rs1 == RV_REG_SP && is_8b_uint(off) && !(off & 0x3))
		emitc(rvc_swsp(off, rs2), ctx);
	else if (rvc_enabled() && is_creg(rs1) && is_creg(rs2) && is_7b_uint(off) && !(off & 0x3))
		emitc(rvc_sw(rs1, off, rs2), ctx);
	else
		emit(rv_sw(rs1, off, rs2), ctx);
}

/* RV64-only helper functions. */
#if __riscv_xlen == 64

static inline void emit_addiw(u8 rd, u8 rs, s32 imm, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rd && rd == rs && is_6b_int(imm))
		emitc(rvc_addiw(rd, imm), ctx);
	else
		emit(rv_addiw(rd, rs, imm), ctx);
}

static inline void emit_ld(u8 rd, s32 off, u8 rs1, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rs1 == RV_REG_SP && rd && is_9b_uint(off) && !(off & 0x7))
		emitc(rvc_ldsp(rd, off), ctx);
	else if (rvc_enabled() && is_creg(rd) && is_creg(rs1) && is_8b_uint(off) && !(off & 0x7))
		emitc(rvc_ld(rd, off, rs1), ctx);
	else
		emit(rv_ld(rd, off, rs1), ctx);
}

static inline void emit_sd(u8 rs1, s32 off, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && rs1 == RV_REG_SP && is_9b_uint(off) && !(off & 0x7))
		emitc(rvc_sdsp(off, rs2), ctx);
	else if (rvc_enabled() && is_creg(rs1) && is_creg(rs2) && is_8b_uint(off) && !(off & 0x7))
		emitc(rvc_sd(rs1, off, rs2), ctx);
	else
		emit(rv_sd(rs1, off, rs2), ctx);
}

static inline void emit_subw(u8 rd, u8 rs1, u8 rs2, struct rv_jit_context *ctx)
{
	if (rvc_enabled() && is_creg(rd) && rd == rs1 && is_creg(rs2))
		emitc(rvc_subw(rd, rs2), ctx);
	else
		emit(rv_subw(rd, rs1, rs2), ctx);
}

static inline void emit_sextb(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	if (rvzbb_enabled()) {
		emit(rvzbb_sextb(rd, rs), ctx);
		return;
	}

	emit_slli(rd, rs, 56, ctx);
	emit_srai(rd, rd, 56, ctx);
}

static inline void emit_sexth(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	if (rvzbb_enabled()) {
		emit(rvzbb_sexth(rd, rs), ctx);
		return;
	}

	emit_slli(rd, rs, 48, ctx);
	emit_srai(rd, rd, 48, ctx);
}

static inline void emit_sextw(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	emit_addiw(rd, rs, 0, ctx);
}

static inline void emit_zexth(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	if (rvzbb_enabled()) {
		emit(rvzbb_zexth(rd, rs), ctx);
		return;
	}

	emit_slli(rd, rs, 48, ctx);
	emit_srli(rd, rd, 48, ctx);
}

static inline void emit_zextw(u8 rd, u8 rs, struct rv_jit_context *ctx)
{
	emit_slli(rd, rs, 32, ctx);
	emit_srli(rd, rd, 32, ctx);
}

static inline void emit_bswap(u8 rd, s32 imm, struct rv_jit_context *ctx)
{
	if (rvzbb_enabled()) {
		int bits = 64 - imm;

		emit(rvzbb_rev8(rd, rd), ctx);
		if (bits)
			emit_srli(rd, rd, bits, ctx);
		return;
	}

	emit_li(RV_REG_T2, 0, ctx);

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);
	if (imm == 16)
		goto out_be;

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);
	if (imm == 32)
		goto out_be;

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);

	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
	emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
	emit_srli(rd, rd, 8, ctx);
out_be:
	emit_andi(RV_REG_T1, rd, 0xff, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);

	emit_mv(rd, RV_REG_T2, ctx);
}

#endif /* __riscv_xlen == 64 */

void bpf_jit_build_prologue(struct rv_jit_context *ctx, bool is_subprog);
void bpf_jit_build_epilogue(struct rv_jit_context *ctx);

int bpf_jit_emit_insn(const struct bpf_insn *insn, struct rv_jit_context *ctx,
		      bool extra_pass);

#endif /* _BPF_JIT_H */
