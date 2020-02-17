// SPDX-License-Identifier: GPL-2.0
/* BPF JIT compiler for RV64G
 *
 * Copyright(c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <asm/cacheflush.h>

enum {
	RV_REG_ZERO =	0,	/* The constant value 0 */
	RV_REG_RA =	1,	/* Return address */
	RV_REG_SP =	2,	/* Stack pointer */
	RV_REG_GP =	3,	/* Global pointer */
	RV_REG_TP =	4,	/* Thread pointer */
	RV_REG_T0 =	5,	/* Temporaries */
	RV_REG_T1 =	6,
	RV_REG_T2 =	7,
	RV_REG_FP =	8,
	RV_REG_S1 =	9,	/* Saved registers */
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

#define RV_REG_TCC RV_REG_A6
#define RV_REG_TCC_SAVED RV_REG_S6 /* Store A6 in S6 if program do calls */

static const int regmap[] = {
	[BPF_REG_0] =	RV_REG_A5,
	[BPF_REG_1] =	RV_REG_A0,
	[BPF_REG_2] =	RV_REG_A1,
	[BPF_REG_3] =	RV_REG_A2,
	[BPF_REG_4] =	RV_REG_A3,
	[BPF_REG_5] =	RV_REG_A4,
	[BPF_REG_6] =	RV_REG_S1,
	[BPF_REG_7] =	RV_REG_S2,
	[BPF_REG_8] =	RV_REG_S3,
	[BPF_REG_9] =	RV_REG_S4,
	[BPF_REG_FP] =	RV_REG_S5,
	[BPF_REG_AX] =	RV_REG_T0,
};

enum {
	RV_CTX_F_SEEN_TAIL_CALL =	0,
	RV_CTX_F_SEEN_CALL =		RV_REG_RA,
	RV_CTX_F_SEEN_S1 =		RV_REG_S1,
	RV_CTX_F_SEEN_S2 =		RV_REG_S2,
	RV_CTX_F_SEEN_S3 =		RV_REG_S3,
	RV_CTX_F_SEEN_S4 =		RV_REG_S4,
	RV_CTX_F_SEEN_S5 =		RV_REG_S5,
	RV_CTX_F_SEEN_S6 =		RV_REG_S6,
};

struct rv_jit_context {
	struct bpf_prog *prog;
	u32 *insns; /* RV insns */
	int ninsns;
	int epilogue_offset;
	int *offset; /* BPF to RV */
	unsigned long flags;
	int stack_size;
};

struct rv_jit_data {
	struct bpf_binary_header *header;
	u8 *image;
	struct rv_jit_context ctx;
};

static u8 bpf_to_rv_reg(int bpf_reg, struct rv_jit_context *ctx)
{
	u8 reg = regmap[bpf_reg];

	switch (reg) {
	case RV_CTX_F_SEEN_S1:
	case RV_CTX_F_SEEN_S2:
	case RV_CTX_F_SEEN_S3:
	case RV_CTX_F_SEEN_S4:
	case RV_CTX_F_SEEN_S5:
	case RV_CTX_F_SEEN_S6:
		__set_bit(reg, &ctx->flags);
	}
	return reg;
};

static bool seen_reg(int reg, struct rv_jit_context *ctx)
{
	switch (reg) {
	case RV_CTX_F_SEEN_CALL:
	case RV_CTX_F_SEEN_S1:
	case RV_CTX_F_SEEN_S2:
	case RV_CTX_F_SEEN_S3:
	case RV_CTX_F_SEEN_S4:
	case RV_CTX_F_SEEN_S5:
	case RV_CTX_F_SEEN_S6:
		return test_bit(reg, &ctx->flags);
	}
	return false;
}

static void mark_fp(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_S5, &ctx->flags);
}

static void mark_call(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_CALL, &ctx->flags);
}

static bool seen_call(struct rv_jit_context *ctx)
{
	return test_bit(RV_CTX_F_SEEN_CALL, &ctx->flags);
}

static void mark_tail_call(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_TAIL_CALL, &ctx->flags);
}

static bool seen_tail_call(struct rv_jit_context *ctx)
{
	return test_bit(RV_CTX_F_SEEN_TAIL_CALL, &ctx->flags);
}

static u8 rv_tail_call_reg(struct rv_jit_context *ctx)
{
	mark_tail_call(ctx);

	if (seen_call(ctx)) {
		__set_bit(RV_CTX_F_SEEN_S6, &ctx->flags);
		return RV_REG_S6;
	}
	return RV_REG_A6;
}

static void emit(const u32 insn, struct rv_jit_context *ctx)
{
	if (ctx->insns)
		ctx->insns[ctx->ninsns] = insn;

	ctx->ninsns++;
}

static u32 rv_r_insn(u8 funct7, u8 rs2, u8 rs1, u8 funct3, u8 rd, u8 opcode)
{
	return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(rd << 7) | opcode;
}

static u32 rv_i_insn(u16 imm11_0, u8 rs1, u8 funct3, u8 rd, u8 opcode)
{
	return (imm11_0 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) |
		opcode;
}

static u32 rv_s_insn(u16 imm11_0, u8 rs2, u8 rs1, u8 funct3, u8 opcode)
{
	u8 imm11_5 = imm11_0 >> 5, imm4_0 = imm11_0 & 0x1f;

	return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(imm4_0 << 7) | opcode;
}

static u32 rv_sb_insn(u16 imm12_1, u8 rs2, u8 rs1, u8 funct3, u8 opcode)
{
	u8 imm12 = ((imm12_1 & 0x800) >> 5) | ((imm12_1 & 0x3f0) >> 4);
	u8 imm4_1 = ((imm12_1 & 0xf) << 1) | ((imm12_1 & 0x400) >> 10);

	return (imm12 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
		(imm4_1 << 7) | opcode;
}

static u32 rv_u_insn(u32 imm31_12, u8 rd, u8 opcode)
{
	return (imm31_12 << 12) | (rd << 7) | opcode;
}

static u32 rv_uj_insn(u32 imm20_1, u8 rd, u8 opcode)
{
	u32 imm;

	imm = (imm20_1 & 0x80000) |  ((imm20_1 & 0x3ff) << 9) |
	      ((imm20_1 & 0x400) >> 2) | ((imm20_1 & 0x7f800) >> 11);

	return (imm << 12) | (rd << 7) | opcode;
}

static u32 rv_amo_insn(u8 funct5, u8 aq, u8 rl, u8 rs2, u8 rs1,
		       u8 funct3, u8 rd, u8 opcode)
{
	u8 funct7 = (funct5 << 2) | (aq << 1) | rl;

	return rv_r_insn(funct7, rs2, rs1, funct3, rd, opcode);
}

static u32 rv_addiw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x1b);
}

static u32 rv_addi(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x13);
}

static u32 rv_addw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 0, rd, 0x3b);
}

static u32 rv_add(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 0, rd, 0x33);
}

static u32 rv_subw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 0, rd, 0x3b);
}

static u32 rv_sub(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 0, rd, 0x33);
}

static u32 rv_and(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 7, rd, 0x33);
}

static u32 rv_or(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 6, rd, 0x33);
}

static u32 rv_xor(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 4, rd, 0x33);
}

static u32 rv_mulw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 0, rd, 0x3b);
}

static u32 rv_mul(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 0, rd, 0x33);
}

static u32 rv_divuw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 5, rd, 0x3b);
}

static u32 rv_divu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 5, rd, 0x33);
}

static u32 rv_remuw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 7, rd, 0x3b);
}

static u32 rv_remu(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(1, rs2, rs1, 7, rd, 0x33);
}

static u32 rv_sllw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 1, rd, 0x3b);
}

static u32 rv_sll(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 1, rd, 0x33);
}

static u32 rv_srlw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 5, rd, 0x3b);
}

static u32 rv_srl(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0, rs2, rs1, 5, rd, 0x33);
}

static u32 rv_sraw(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 5, rd, 0x3b);
}

static u32 rv_sra(u8 rd, u8 rs1, u8 rs2)
{
	return rv_r_insn(0x20, rs2, rs1, 5, rd, 0x33);
}

static u32 rv_lui(u8 rd, u32 imm31_12)
{
	return rv_u_insn(imm31_12, rd, 0x37);
}

static u32 rv_slli(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 1, rd, 0x13);
}

static u32 rv_andi(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 7, rd, 0x13);
}

static u32 rv_ori(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 6, rd, 0x13);
}

static u32 rv_xori(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 4, rd, 0x13);
}

static u32 rv_slliw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 1, rd, 0x1b);
}

static u32 rv_srliw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x1b);
}

static u32 rv_srli(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x13);
}

static u32 rv_sraiw(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(0x400 | imm11_0, rs1, 5, rd, 0x1b);
}

static u32 rv_srai(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(0x400 | imm11_0, rs1, 5, rd, 0x13);
}

static u32 rv_jal(u8 rd, u32 imm20_1)
{
	return rv_uj_insn(imm20_1, rd, 0x6f);
}

static u32 rv_jalr(u8 rd, u8 rs1, u16 imm11_0)
{
	return rv_i_insn(imm11_0, rs1, 0, rd, 0x67);
}

static u32 rv_beq(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 0, 0x63);
}

static u32 rv_bltu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 6, 0x63);
}

static u32 rv_bgeu(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 7, 0x63);
}

static u32 rv_bne(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 1, 0x63);
}

static u32 rv_blt(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 4, 0x63);
}

static u32 rv_bge(u8 rs1, u8 rs2, u16 imm12_1)
{
	return rv_sb_insn(imm12_1, rs2, rs1, 5, 0x63);
}

static u32 rv_sb(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 0, 0x23);
}

static u32 rv_sh(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 1, 0x23);
}

static u32 rv_sw(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 2, 0x23);
}

static u32 rv_sd(u8 rs1, u16 imm11_0, u8 rs2)
{
	return rv_s_insn(imm11_0, rs2, rs1, 3, 0x23);
}

static u32 rv_lbu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 4, rd, 0x03);
}

static u32 rv_lhu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 5, rd, 0x03);
}

static u32 rv_lwu(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 6, rd, 0x03);
}

static u32 rv_ld(u8 rd, u16 imm11_0, u8 rs1)
{
	return rv_i_insn(imm11_0, rs1, 3, rd, 0x03);
}

static u32 rv_amoadd_w(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0, aq, rl, rs2, rs1, 2, rd, 0x2f);
}

static u32 rv_amoadd_d(u8 rd, u8 rs2, u8 rs1, u8 aq, u8 rl)
{
	return rv_amo_insn(0, aq, rl, rs2, rs1, 3, rd, 0x2f);
}

static u32 rv_auipc(u8 rd, u32 imm31_12)
{
	return rv_u_insn(imm31_12, rd, 0x17);
}

static bool is_12b_int(s64 val)
{
	return -(1 << 11) <= val && val < (1 << 11);
}

static bool is_13b_int(s64 val)
{
	return -(1 << 12) <= val && val < (1 << 12);
}

static bool is_21b_int(s64 val)
{
	return -(1L << 20) <= val && val < (1L << 20);
}

static bool is_32b_int(s64 val)
{
	return -(1L << 31) <= val && val < (1L << 31);
}

static int is_12b_check(int off, int insn)
{
	if (!is_12b_int(off)) {
		pr_err("bpf-jit: insn=%d 12b < offset=%d not supported yet!\n",
		       insn, (int)off);
		return -1;
	}
	return 0;
}

static void emit_imm(u8 rd, s64 val, struct rv_jit_context *ctx)
{
	/* Note that the immediate from the add is sign-extended,
	 * which means that we need to compensate this by adding 2^12,
	 * when the 12th bit is set. A simpler way of doing this, and
	 * getting rid of the check, is to just add 2**11 before the
	 * shift. The "Loading a 32-Bit constant" example from the
	 * "Computer Organization and Design, RISC-V edition" book by
	 * Patterson/Hennessy highlights this fact.
	 *
	 * This also means that we need to process LSB to MSB.
	 */
	s64 upper = (val + (1 << 11)) >> 12, lower = val & 0xfff;
	int shift;

	if (is_32b_int(val)) {
		if (upper)
			emit(rv_lui(rd, upper), ctx);

		if (!upper) {
			emit(rv_addi(rd, RV_REG_ZERO, lower), ctx);
			return;
		}

		emit(rv_addiw(rd, rd, lower), ctx);
		return;
	}

	shift = __ffs(upper);
	upper >>= shift;
	shift += 12;

	emit_imm(rd, upper, ctx);

	emit(rv_slli(rd, rd, shift), ctx);
	if (lower)
		emit(rv_addi(rd, rd, lower), ctx);
}

static int rv_offset(int insn, int off, struct rv_jit_context *ctx)
{
	int from, to;

	off++; /* BPF branch is from PC+1, RV is from PC */
	from = (insn > 0) ? ctx->offset[insn - 1] : 0;
	to = (insn + off > 0) ? ctx->offset[insn + off - 1] : 0;
	return (to - from) << 2;
}

static int epilogue_offset(struct rv_jit_context *ctx)
{
	int to = ctx->epilogue_offset, from = ctx->ninsns;

	return (to - from) << 2;
}

static void __build_epilogue(bool is_tail_call, struct rv_jit_context *ctx)
{
	int stack_adjust = ctx->stack_size, store_offset = stack_adjust - 8;

	if (seen_reg(RV_REG_RA, ctx)) {
		emit(rv_ld(RV_REG_RA, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	emit(rv_ld(RV_REG_FP, store_offset, RV_REG_SP), ctx);
	store_offset -= 8;
	if (seen_reg(RV_REG_S1, ctx)) {
		emit(rv_ld(RV_REG_S1, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S2, ctx)) {
		emit(rv_ld(RV_REG_S2, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S3, ctx)) {
		emit(rv_ld(RV_REG_S3, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S4, ctx)) {
		emit(rv_ld(RV_REG_S4, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S5, ctx)) {
		emit(rv_ld(RV_REG_S5, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S6, ctx)) {
		emit(rv_ld(RV_REG_S6, store_offset, RV_REG_SP), ctx);
		store_offset -= 8;
	}

	emit(rv_addi(RV_REG_SP, RV_REG_SP, stack_adjust), ctx);
	/* Set return value. */
	if (!is_tail_call)
		emit(rv_addi(RV_REG_A0, RV_REG_A5, 0), ctx);
	emit(rv_jalr(RV_REG_ZERO, is_tail_call ? RV_REG_T3 : RV_REG_RA,
		     is_tail_call ? 4 : 0), /* skip TCC init */
	     ctx);
}

/* return -1 or inverted cond */
static int invert_bpf_cond(u8 cond)
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

static void emit_bcc(u8 cond, u8 rd, u8 rs, int rvoff,
		     struct rv_jit_context *ctx)
{
	switch (cond) {
	case BPF_JEQ:
		emit(rv_beq(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JGT:
		emit(rv_bltu(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JLT:
		emit(rv_bltu(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JGE:
		emit(rv_bgeu(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JLE:
		emit(rv_bgeu(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JNE:
		emit(rv_bne(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSGT:
		emit(rv_blt(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JSLT:
		emit(rv_blt(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSGE:
		emit(rv_bge(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSLE:
		emit(rv_bge(rs, rd, rvoff >> 1), ctx);
	}
}

static void emit_branch(u8 cond, u8 rd, u8 rs, int rvoff,
			struct rv_jit_context *ctx)
{
	s64 upper, lower;

	if (is_13b_int(rvoff)) {
		emit_bcc(cond, rd, rs, rvoff, ctx);
		return;
	}

	/* Adjust for jal */
	rvoff -= 4;

	/* Transform, e.g.:
	 *   bne rd,rs,foo
	 * to
	 *   beq rd,rs,<.L1>
	 *   (auipc foo)
	 *   jal(r) foo
	 * .L1
	 */
	cond = invert_bpf_cond(cond);
	if (is_21b_int(rvoff)) {
		emit_bcc(cond, rd, rs, 8, ctx);
		emit(rv_jal(RV_REG_ZERO, rvoff >> 1), ctx);
		return;
	}

	/* 32b No need for an additional rvoff adjustment, since we
	 * get that from the auipc at PC', where PC = PC' + 4.
	 */
	upper = (rvoff + (1 << 11)) >> 12;
	lower = rvoff & 0xfff;

	emit_bcc(cond, rd, rs, 12, ctx);
	emit(rv_auipc(RV_REG_T1, upper), ctx);
	emit(rv_jalr(RV_REG_ZERO, RV_REG_T1, lower), ctx);
}

static void emit_zext_32(u8 reg, struct rv_jit_context *ctx)
{
	emit(rv_slli(reg, reg, 32), ctx);
	emit(rv_srli(reg, reg, 32), ctx);
}

static int emit_bpf_tail_call(int insn, struct rv_jit_context *ctx)
{
	int tc_ninsn, off, start_insn = ctx->ninsns;
	u8 tcc = rv_tail_call_reg(ctx);

	/* a0: &ctx
	 * a1: &array
	 * a2: index
	 *
	 * if (index >= array->map.max_entries)
	 *	goto out;
	 */
	tc_ninsn = insn ? ctx->offset[insn] - ctx->offset[insn - 1] :
		   ctx->offset[0];
	emit_zext_32(RV_REG_A2, ctx);

	off = offsetof(struct bpf_array, map.max_entries);
	if (is_12b_check(off, insn))
		return -1;
	emit(rv_lwu(RV_REG_T1, off, RV_REG_A1), ctx);
	off = (tc_ninsn - (ctx->ninsns - start_insn)) << 2;
	emit_branch(BPF_JGE, RV_REG_A2, RV_REG_T1, off, ctx);

	/* if (TCC-- < 0)
	 *     goto out;
	 */
	emit(rv_addi(RV_REG_T1, tcc, -1), ctx);
	off = (tc_ninsn - (ctx->ninsns - start_insn)) << 2;
	emit_branch(BPF_JSLT, tcc, RV_REG_ZERO, off, ctx);

	/* prog = array->ptrs[index];
	 * if (!prog)
	 *     goto out;
	 */
	emit(rv_slli(RV_REG_T2, RV_REG_A2, 3), ctx);
	emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_A1), ctx);
	off = offsetof(struct bpf_array, ptrs);
	if (is_12b_check(off, insn))
		return -1;
	emit(rv_ld(RV_REG_T2, off, RV_REG_T2), ctx);
	off = (tc_ninsn - (ctx->ninsns - start_insn)) << 2;
	emit_branch(BPF_JEQ, RV_REG_T2, RV_REG_ZERO, off, ctx);

	/* goto *(prog->bpf_func + 4); */
	off = offsetof(struct bpf_prog, bpf_func);
	if (is_12b_check(off, insn))
		return -1;
	emit(rv_ld(RV_REG_T3, off, RV_REG_T2), ctx);
	emit(rv_addi(RV_REG_TCC, RV_REG_T1, 0), ctx);
	__build_epilogue(true, ctx);
	return 0;
}

static void init_regs(u8 *rd, u8 *rs, const struct bpf_insn *insn,
		      struct rv_jit_context *ctx)
{
	u8 code = insn->code;

	switch (code) {
	case BPF_JMP | BPF_JA:
	case BPF_JMP | BPF_CALL:
	case BPF_JMP | BPF_EXIT:
	case BPF_JMP | BPF_TAIL_CALL:
		break;
	default:
		*rd = bpf_to_rv_reg(insn->dst_reg, ctx);
	}

	if (code & (BPF_ALU | BPF_X) || code & (BPF_ALU64 | BPF_X) ||
	    code & (BPF_JMP | BPF_X) || code & (BPF_JMP32 | BPF_X) ||
	    code & BPF_LDX || code & BPF_STX)
		*rs = bpf_to_rv_reg(insn->src_reg, ctx);
}

static void emit_zext_32_rd_rs(u8 *rd, u8 *rs, struct rv_jit_context *ctx)
{
	emit(rv_addi(RV_REG_T2, *rd, 0), ctx);
	emit_zext_32(RV_REG_T2, ctx);
	emit(rv_addi(RV_REG_T1, *rs, 0), ctx);
	emit_zext_32(RV_REG_T1, ctx);
	*rd = RV_REG_T2;
	*rs = RV_REG_T1;
}

static void emit_sext_32_rd_rs(u8 *rd, u8 *rs, struct rv_jit_context *ctx)
{
	emit(rv_addiw(RV_REG_T2, *rd, 0), ctx);
	emit(rv_addiw(RV_REG_T1, *rs, 0), ctx);
	*rd = RV_REG_T2;
	*rs = RV_REG_T1;
}

static void emit_zext_32_rd_t1(u8 *rd, struct rv_jit_context *ctx)
{
	emit(rv_addi(RV_REG_T2, *rd, 0), ctx);
	emit_zext_32(RV_REG_T2, ctx);
	emit_zext_32(RV_REG_T1, ctx);
	*rd = RV_REG_T2;
}

static void emit_sext_32_rd(u8 *rd, struct rv_jit_context *ctx)
{
	emit(rv_addiw(RV_REG_T2, *rd, 0), ctx);
	*rd = RV_REG_T2;
}

static void emit_jump_and_link(u8 rd, s64 rvoff, bool force_jalr,
			       struct rv_jit_context *ctx)
{
	s64 upper, lower;

	if (rvoff && is_21b_int(rvoff) && !force_jalr) {
		emit(rv_jal(rd, rvoff >> 1), ctx);
		return;
	}

	upper = (rvoff + (1 << 11)) >> 12;
	lower = rvoff & 0xfff;
	emit(rv_auipc(RV_REG_T1, upper), ctx);
	emit(rv_jalr(rd, RV_REG_T1, lower), ctx);
}

static bool is_signed_bpf_cond(u8 cond)
{
	return cond == BPF_JSGT || cond == BPF_JSLT ||
		cond == BPF_JSGE || cond == BPF_JSLE;
}

static int emit_call(bool fixed, u64 addr, struct rv_jit_context *ctx)
{
	s64 off = 0;
	u64 ip;
	u8 rd;

	if (addr && ctx->insns) {
		ip = (u64)(long)(ctx->insns + ctx->ninsns);
		off = addr - ip;
		if (!is_32b_int(off)) {
			pr_err("bpf-jit: target call addr %pK is out of range\n",
			       (void *)addr);
			return -ERANGE;
		}
	}

	emit_jump_and_link(RV_REG_RA, off, !fixed, ctx);
	rd = bpf_to_rv_reg(BPF_REG_0, ctx);
	emit(rv_addi(rd, RV_REG_A0, 0), ctx);
	return 0;
}

static int emit_insn(const struct bpf_insn *insn, struct rv_jit_context *ctx,
		     bool extra_pass)
{
	bool is64 = BPF_CLASS(insn->code) == BPF_ALU64 ||
		    BPF_CLASS(insn->code) == BPF_JMP;
	int s, e, rvoff, i = insn - ctx->prog->insnsi;
	struct bpf_prog_aux *aux = ctx->prog->aux;
	u8 rd = -1, rs = -1, code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;

	init_regs(&rd, &rs, insn, ctx);

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
	case BPF_ALU64 | BPF_MOV | BPF_X:
		if (imm == 1) {
			/* Special mov32 for zext */
			emit_zext_32(rd, ctx);
			break;
		}
		emit(is64 ? rv_addi(rd, rs, 0) : rv_addiw(rd, rs, 0), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit(is64 ? rv_add(rd, rd, rs) : rv_addw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit(is64 ? rv_sub(rd, rd, rs) : rv_subw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit(rv_and(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit(rv_or(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit(rv_xor(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit(is64 ? rv_mul(rd, rd, rs) : rv_mulw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		emit(is64 ? rv_divu(rd, rd, rs) : rv_divuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit(is64 ? rv_remu(rd, rd, rs) : rv_remuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit(is64 ? rv_sll(rd, rd, rs) : rv_sllw(rd, rd, rs), ctx);
		if (!is64)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(is64 ? rv_srl(rd, rd, rs) : rv_srlw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit(is64 ? rv_sra(rd, rd, rs) : rv_sraw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit(is64 ? rv_sub(rd, RV_REG_ZERO, rd) :
		     rv_subw(rd, RV_REG_ZERO, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
	{
		int shift = 64 - imm;

		emit(rv_slli(rd, rd, shift), ctx);
		emit(rv_srli(rd, rd, shift), ctx);
		break;
	}
	case BPF_ALU | BPF_END | BPF_FROM_BE:
		emit(rv_addi(RV_REG_T2, RV_REG_ZERO, 0), ctx);

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);
		if (imm == 16)
			goto out_be;

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);
		if (imm == 32)
			goto out_be;

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);

		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);
		emit(rv_slli(RV_REG_T2, RV_REG_T2, 8), ctx);
		emit(rv_srli(rd, rd, 8), ctx);
out_be:
		emit(rv_andi(RV_REG_T1, rd, 0xff), ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, RV_REG_T1), ctx);

		emit(rv_addi(rd, RV_REG_T2, 0), ctx);
		break;

	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_imm(rd, imm, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		if (is_12b_int(imm)) {
			emit(is64 ? rv_addi(rd, rd, imm) :
			     rv_addiw(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit(is64 ? rv_add(rd, rd, RV_REG_T1) :
			     rv_addw(rd, rd, RV_REG_T1), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		if (is_12b_int(-imm)) {
			emit(is64 ? rv_addi(rd, rd, -imm) :
			     rv_addiw(rd, rd, -imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit(is64 ? rv_sub(rd, rd, RV_REG_T1) :
			     rv_subw(rd, rd, RV_REG_T1), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		if (is_12b_int(imm)) {
			emit(rv_andi(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit(rv_and(rd, rd, RV_REG_T1), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		if (is_12b_int(imm)) {
			emit(rv_ori(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit(rv_or(rd, rd, RV_REG_T1), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		if (is_12b_int(imm)) {
			emit(rv_xori(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit(rv_xor(rd, rd, RV_REG_T1), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_mul(rd, rd, RV_REG_T1) :
		     rv_mulw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_divu(rd, rd, RV_REG_T1) :
		     rv_divuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_remu(rd, rd, RV_REG_T1) :
		     rv_remuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit(is64 ? rv_slli(rd, rd, imm) : rv_slliw(rd, rd, imm), ctx);
		if (!is64)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		emit(is64 ? rv_srli(rd, rd, imm) : rv_srliw(rd, rd, imm), ctx);
		if (!is64)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit(is64 ? rv_srai(rd, rd, imm) : rv_sraiw(rd, rd, imm), ctx);
		if (!is64)
			emit_zext_32(rd, ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		rvoff = rv_offset(i, off, ctx);
		emit_jump_and_link(RV_REG_ZERO, rvoff, false, ctx);
		break;

	/* IF (dst COND src) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP32 | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP32 | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP32 | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP32 | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP32 | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP32 | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
	case BPF_JMP | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_X:
		rvoff = rv_offset(i, off, ctx);
		if (!is64) {
			s = ctx->ninsns;
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd_rs(&rd, &rs, ctx);
			else
				emit_zext_32_rd_rs(&rd, &rs, ctx);
			e = ctx->ninsns;

			/* Adjust for extra insns */
			rvoff -= (e - s) << 2;
		}

		if (BPF_OP(code) == BPF_JSET) {
			/* Adjust for and */
			rvoff -= 4;
			emit(rv_and(RV_REG_T1, rd, rs), ctx);
			emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff,
				    ctx);
		} else {
			emit_branch(BPF_OP(code), rd, rs, rvoff, ctx);
		}
		break;

	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP32 | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP32 | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP32 | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP32 | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP32 | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP32 | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		rvoff = rv_offset(i, off, ctx);
		s = ctx->ninsns;
		emit_imm(RV_REG_T1, imm, ctx);
		if (!is64) {
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd(&rd, ctx);
			else
				emit_zext_32_rd_t1(&rd, ctx);
		}
		e = ctx->ninsns;

		/* Adjust for extra insns */
		rvoff -= (e - s) << 2;

		if (BPF_OP(code) == BPF_JSET) {
			/* Adjust for and */
			rvoff -= 4;
			emit(rv_and(RV_REG_T1, rd, RV_REG_T1), ctx);
			emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff,
				    ctx);
		} else {
			emit_branch(BPF_OP(code), rd, RV_REG_T1, rvoff, ctx);
		}
		break;

	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		bool fixed;
		int ret;
		u64 addr;

		mark_call(ctx);
		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass, &addr,
					    &fixed);
		if (ret < 0)
			return ret;
		ret = emit_call(fixed, addr, ctx);
		if (ret)
			return ret;
		break;
	}
	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_bpf_tail_call(i, ctx))
			return -1;
		break;

	/* function return */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;

		rvoff = epilogue_offset(ctx);
		emit_jump_and_link(RV_REG_ZERO, rvoff, false, ctx);
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		u64 imm64;

		imm64 = (u64)insn1.imm << 32 | (u32)imm;
		emit_imm(rd, imm64, ctx);
		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
		if (is_12b_int(off)) {
			emit(rv_lbu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rs), ctx);
		emit(rv_lbu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_H:
		if (is_12b_int(off)) {
			emit(rv_lhu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rs), ctx);
		emit(rv_lhu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_W:
		if (is_12b_int(off)) {
			emit(rv_lwu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rs), ctx);
		emit(rv_lwu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_DW:
		if (is_12b_int(off)) {
			emit(rv_ld(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rs), ctx);
		emit(rv_ld(rd, 0, RV_REG_T1), ctx);
		break;

	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_B:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sb(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, rd), ctx);
		emit(rv_sb(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;

	case BPF_ST | BPF_MEM | BPF_H:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sh(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, rd), ctx);
		emit(rv_sh(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;
	case BPF_ST | BPF_MEM | BPF_W:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sw(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, rd), ctx);
		emit(rv_sw(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;
	case BPF_ST | BPF_MEM | BPF_DW:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sd(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit(rv_add(RV_REG_T2, RV_REG_T2, rd), ctx);
		emit(rv_sd(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;

	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_B:
		if (is_12b_int(off)) {
			emit(rv_sb(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rd), ctx);
		emit(rv_sb(RV_REG_T1, 0, rs), ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_H:
		if (is_12b_int(off)) {
			emit(rv_sh(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rd), ctx);
		emit(rv_sh(RV_REG_T1, 0, rs), ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_W:
		if (is_12b_int(off)) {
			emit(rv_sw(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rd), ctx);
		emit(rv_sw(RV_REG_T1, 0, rs), ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_DW:
		if (is_12b_int(off)) {
			emit(rv_sd(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit(rv_add(RV_REG_T1, RV_REG_T1, rd), ctx);
		emit(rv_sd(RV_REG_T1, 0, rs), ctx);
		break;
	/* STX XADD: lock *(u32 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_W:
	/* STX XADD: lock *(u64 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_DW:
		if (off) {
			if (is_12b_int(off)) {
				emit(rv_addi(RV_REG_T1, rd, off), ctx);
			} else {
				emit_imm(RV_REG_T1, off, ctx);
				emit(rv_add(RV_REG_T1, RV_REG_T1, rd), ctx);
			}

			rd = RV_REG_T1;
		}

		emit(BPF_SIZE(code) == BPF_W ?
		     rv_amoadd_w(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoadd_d(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	default:
		pr_err("bpf-jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

static void build_prologue(struct rv_jit_context *ctx)
{
	int stack_adjust = 0, store_offset, bpf_stack_adjust;

	bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, 16);
	if (bpf_stack_adjust)
		mark_fp(ctx);

	if (seen_reg(RV_REG_RA, ctx))
		stack_adjust += 8;
	stack_adjust += 8; /* RV_REG_FP */
	if (seen_reg(RV_REG_S1, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S2, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S3, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S4, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S5, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S6, ctx))
		stack_adjust += 8;

	stack_adjust = round_up(stack_adjust, 16);
	stack_adjust += bpf_stack_adjust;

	store_offset = stack_adjust - 8;

	/* First instruction is always setting the tail-call-counter
	 * (TCC) register. This instruction is skipped for tail calls.
	 */
	emit(rv_addi(RV_REG_TCC, RV_REG_ZERO, MAX_TAIL_CALL_CNT), ctx);

	emit(rv_addi(RV_REG_SP, RV_REG_SP, -stack_adjust), ctx);

	if (seen_reg(RV_REG_RA, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_RA), ctx);
		store_offset -= 8;
	}
	emit(rv_sd(RV_REG_SP, store_offset, RV_REG_FP), ctx);
	store_offset -= 8;
	if (seen_reg(RV_REG_S1, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S1), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S2, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S2), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S3, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S3), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S4, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S4), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S5, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S5), ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S6, ctx)) {
		emit(rv_sd(RV_REG_SP, store_offset, RV_REG_S6), ctx);
		store_offset -= 8;
	}

	emit(rv_addi(RV_REG_FP, RV_REG_SP, stack_adjust), ctx);

	if (bpf_stack_adjust)
		emit(rv_addi(RV_REG_S5, RV_REG_SP, bpf_stack_adjust), ctx);

	/* Program contains calls and tail calls, so RV_REG_TCC need
	 * to be saved across calls.
	 */
	if (seen_tail_call(ctx) && seen_call(ctx))
		emit(rv_addi(RV_REG_TCC_SAVED, RV_REG_TCC, 0), ctx);

	ctx->stack_size = stack_adjust;
}

static void build_epilogue(struct rv_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}

static int build_body(struct rv_jit_context *ctx, bool extra_pass, int *offset)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		ret = emit_insn(insn, ctx, extra_pass);
		if (ret > 0) {
			i++;
			if (offset)
				offset[i] = ctx->ninsns;
			continue;
		}
		if (offset)
			offset[i] = ctx->ninsns;
		if (ret)
			return ret;
	}
	return 0;
}

static void bpf_fill_ill_insns(void *area, unsigned int size)
{
	memset(area, 0, size);
}

static void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	bool tmp_blinded = false, extra_pass = false;
	struct bpf_prog *tmp, *orig_prog = prog;
	int pass = 0, prev_ninsns = 0, i;
	struct rv_jit_data *jit_data;
	unsigned int image_size = 0;
	struct rv_jit_context *ctx;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	if (IS_ERR(tmp))
		return orig_prog;
	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}

	ctx = &jit_data->ctx;

	if (ctx->offset) {
		extra_pass = true;
		image_size = sizeof(u32) * ctx->ninsns;
		goto skip_init_ctx;
	}

	ctx->prog = prog;
	ctx->offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (!ctx->offset) {
		prog = orig_prog;
		goto out_offset;
	}
	for (i = 0; i < prog->len; i++) {
		prev_ninsns += 32;
		ctx->offset[i] = prev_ninsns;
	}

	for (i = 0; i < 16; i++) {
		pass++;
		ctx->ninsns = 0;
		if (build_body(ctx, extra_pass, ctx->offset)) {
			prog = orig_prog;
			goto out_offset;
		}
		build_prologue(ctx);
		ctx->epilogue_offset = ctx->ninsns;
		build_epilogue(ctx);

		if (ctx->ninsns == prev_ninsns) {
			if (jit_data->header)
				break;

			image_size = sizeof(u32) * ctx->ninsns;
			jit_data->header =
				bpf_jit_binary_alloc(image_size,
						     &jit_data->image,
						     sizeof(u32),
						     bpf_fill_ill_insns);
			if (!jit_data->header) {
				prog = orig_prog;
				goto out_offset;
			}

			ctx->insns = (u32 *)jit_data->image;
			/* Now, when the image is allocated, the image
			 * can potentially shrink more (auipc/jalr ->
			 * jal).
			 */
		}
		prev_ninsns = ctx->ninsns;
	}

	if (i == 16) {
		pr_err("bpf-jit: image did not converge in <%d passes!\n", i);
		bpf_jit_binary_free(jit_data->header);
		prog = orig_prog;
		goto out_offset;
	}

skip_init_ctx:
	pass++;
	ctx->ninsns = 0;

	build_prologue(ctx);
	if (build_body(ctx, extra_pass, NULL)) {
		bpf_jit_binary_free(jit_data->header);
		prog = orig_prog;
		goto out_offset;
	}
	build_epilogue(ctx);

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, pass, ctx->insns);

	prog->bpf_func = (void *)ctx->insns;
	prog->jited = 1;
	prog->jited_len = image_size;

	bpf_flush_icache(jit_data->header, ctx->insns + ctx->ninsns);

	if (!prog->is_func || extra_pass) {
out_offset:
		kfree(ctx->offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	return prog;
}

void *bpf_jit_alloc_exec(unsigned long size)
{
	return __vmalloc_node_range(size, PAGE_SIZE, BPF_JIT_REGION_START,
				    BPF_JIT_REGION_END, GFP_KERNEL,
				    PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

void bpf_jit_free_exec(void *addr)
{
	return vfree(addr);
}
