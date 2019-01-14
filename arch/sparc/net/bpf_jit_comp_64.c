// SPDX-License-Identifier: GPL-2.0
#include <linux/moduleloader.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/cache.h>
#include <linux/if_vlan.h>

#include <asm/cacheflush.h>
#include <asm/ptrace.h>

#include "bpf_jit_64.h"

static inline bool is_simm13(unsigned int value)
{
	return value + 0x1000 < 0x2000;
}

static inline bool is_simm10(unsigned int value)
{
	return value + 0x200 < 0x400;
}

static inline bool is_simm5(unsigned int value)
{
	return value + 0x10 < 0x20;
}

static inline bool is_sethi(unsigned int value)
{
	return (value & ~0x3fffff) == 0;
}

static void bpf_flush_icache(void *start_, void *end_)
{
	/* Cheetah's I-cache is fully coherent.  */
	if (tlb_type == spitfire) {
		unsigned long start = (unsigned long) start_;
		unsigned long end = (unsigned long) end_;

		start &= ~7UL;
		end = (end + 7UL) & ~7UL;
		while (start < end) {
			flushi(start);
			start += 32;
		}
	}
}

#define S13(X)		((X) & 0x1fff)
#define S5(X)		((X) & 0x1f)
#define IMMED		0x00002000
#define RD(X)		((X) << 25)
#define RS1(X)		((X) << 14)
#define RS2(X)		((X))
#define OP(X)		((X) << 30)
#define OP2(X)		((X) << 22)
#define OP3(X)		((X) << 19)
#define COND(X)		(((X) & 0xf) << 25)
#define CBCOND(X)	(((X) & 0x1f) << 25)
#define F1(X)		OP(X)
#define F2(X, Y)	(OP(X) | OP2(Y))
#define F3(X, Y)	(OP(X) | OP3(Y))
#define ASI(X)		(((X) & 0xff) << 5)

#define CONDN		COND(0x0)
#define CONDE		COND(0x1)
#define CONDLE		COND(0x2)
#define CONDL		COND(0x3)
#define CONDLEU		COND(0x4)
#define CONDCS		COND(0x5)
#define CONDNEG		COND(0x6)
#define CONDVC		COND(0x7)
#define CONDA		COND(0x8)
#define CONDNE		COND(0x9)
#define CONDG		COND(0xa)
#define CONDGE		COND(0xb)
#define CONDGU		COND(0xc)
#define CONDCC		COND(0xd)
#define CONDPOS		COND(0xe)
#define CONDVS		COND(0xf)

#define CONDGEU		CONDCC
#define CONDLU		CONDCS

#define WDISP22(X)	(((X) >> 2) & 0x3fffff)
#define WDISP19(X)	(((X) >> 2) & 0x7ffff)

/* The 10-bit branch displacement for CBCOND is split into two fields */
static u32 WDISP10(u32 off)
{
	u32 ret = ((off >> 2) & 0xff) << 5;

	ret |= ((off >> (2 + 8)) & 0x03) << 19;

	return ret;
}

#define CBCONDE		CBCOND(0x09)
#define CBCONDLE	CBCOND(0x0a)
#define CBCONDL		CBCOND(0x0b)
#define CBCONDLEU	CBCOND(0x0c)
#define CBCONDCS	CBCOND(0x0d)
#define CBCONDN		CBCOND(0x0e)
#define CBCONDVS	CBCOND(0x0f)
#define CBCONDNE	CBCOND(0x19)
#define CBCONDG		CBCOND(0x1a)
#define CBCONDGE	CBCOND(0x1b)
#define CBCONDGU	CBCOND(0x1c)
#define CBCONDCC	CBCOND(0x1d)
#define CBCONDPOS	CBCOND(0x1e)
#define CBCONDVC	CBCOND(0x1f)

#define CBCONDGEU	CBCONDCC
#define CBCONDLU	CBCONDCS

#define ANNUL		(1 << 29)
#define XCC		(1 << 21)

#define BRANCH		(F2(0, 1) | XCC)
#define CBCOND_OP	(F2(0, 3) | XCC)

#define BA		(BRANCH | CONDA)
#define BG		(BRANCH | CONDG)
#define BL		(BRANCH | CONDL)
#define BLE		(BRANCH | CONDLE)
#define BGU		(BRANCH | CONDGU)
#define BLEU		(BRANCH | CONDLEU)
#define BGE		(BRANCH | CONDGE)
#define BGEU		(BRANCH | CONDGEU)
#define BLU		(BRANCH | CONDLU)
#define BE		(BRANCH | CONDE)
#define BNE		(BRANCH | CONDNE)

#define SETHI(K, REG)	\
	(F2(0, 0x4) | RD(REG) | (((K) >> 10) & 0x3fffff))
#define OR_LO(K, REG)	\
	(F3(2, 0x02) | IMMED | RS1(REG) | ((K) & 0x3ff) | RD(REG))

#define ADD		F3(2, 0x00)
#define AND		F3(2, 0x01)
#define ANDCC		F3(2, 0x11)
#define OR		F3(2, 0x02)
#define XOR		F3(2, 0x03)
#define SUB		F3(2, 0x04)
#define SUBCC		F3(2, 0x14)
#define MUL		F3(2, 0x0a)
#define MULX		F3(2, 0x09)
#define UDIVX		F3(2, 0x0d)
#define DIV		F3(2, 0x0e)
#define SLL		F3(2, 0x25)
#define SLLX		(F3(2, 0x25)|(1<<12))
#define SRA		F3(2, 0x27)
#define SRAX		(F3(2, 0x27)|(1<<12))
#define SRL		F3(2, 0x26)
#define SRLX		(F3(2, 0x26)|(1<<12))
#define JMPL		F3(2, 0x38)
#define SAVE		F3(2, 0x3c)
#define RESTORE		F3(2, 0x3d)
#define CALL		F1(1)
#define BR		F2(0, 0x01)
#define RD_Y		F3(2, 0x28)
#define WR_Y		F3(2, 0x30)

#define LD32		F3(3, 0x00)
#define LD8		F3(3, 0x01)
#define LD16		F3(3, 0x02)
#define LD64		F3(3, 0x0b)
#define LD64A		F3(3, 0x1b)
#define ST8		F3(3, 0x05)
#define ST16		F3(3, 0x06)
#define ST32		F3(3, 0x04)
#define ST64		F3(3, 0x0e)

#define CAS		F3(3, 0x3c)
#define CASX		F3(3, 0x3e)

#define LDPTR		LD64
#define BASE_STACKFRAME	176

#define LD32I		(LD32 | IMMED)
#define LD8I		(LD8 | IMMED)
#define LD16I		(LD16 | IMMED)
#define LD64I		(LD64 | IMMED)
#define LDPTRI		(LDPTR | IMMED)
#define ST32I		(ST32 | IMMED)

struct jit_ctx {
	struct bpf_prog		*prog;
	unsigned int		*offset;
	int			idx;
	int			epilogue_offset;
	bool 			tmp_1_used;
	bool 			tmp_2_used;
	bool 			tmp_3_used;
	bool			saw_frame_pointer;
	bool			saw_call;
	bool			saw_tail_call;
	u32			*image;
};

#define TMP_REG_1	(MAX_BPF_JIT_REG + 0)
#define TMP_REG_2	(MAX_BPF_JIT_REG + 1)
#define TMP_REG_3	(MAX_BPF_JIT_REG + 2)

/* Map BPF registers to SPARC registers */
static const int bpf2sparc[] = {
	/* return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = O5,

	/* arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = O0,
	[BPF_REG_2] = O1,
	[BPF_REG_3] = O2,
	[BPF_REG_4] = O3,
	[BPF_REG_5] = O4,

	/* callee saved registers that in-kernel function will preserve */
	[BPF_REG_6] = L0,
	[BPF_REG_7] = L1,
	[BPF_REG_8] = L2,
	[BPF_REG_9] = L3,

	/* read-only frame pointer to access stack */
	[BPF_REG_FP] = L6,

	[BPF_REG_AX] = G7,

	/* temporary register for internal BPF JIT */
	[TMP_REG_1] = G1,
	[TMP_REG_2] = G2,
	[TMP_REG_3] = G3,
};

static void emit(const u32 insn, struct jit_ctx *ctx)
{
	if (ctx->image != NULL)
		ctx->image[ctx->idx] = insn;

	ctx->idx++;
}

static void emit_call(u32 *func, struct jit_ctx *ctx)
{
	if (ctx->image != NULL) {
		void *here = &ctx->image[ctx->idx];
		unsigned int off;

		off = (void *)func - here;
		ctx->image[ctx->idx] = CALL | ((off >> 2) & 0x3fffffff);
	}
	ctx->idx++;
}

static void emit_nop(struct jit_ctx *ctx)
{
	emit(SETHI(0, G0), ctx);
}

static void emit_reg_move(u32 from, u32 to, struct jit_ctx *ctx)
{
	emit(OR | RS1(G0) | RS2(from) | RD(to), ctx);
}

/* Emit 32-bit constant, zero extended. */
static void emit_set_const(s32 K, u32 reg, struct jit_ctx *ctx)
{
	emit(SETHI(K, reg), ctx);
	emit(OR_LO(K, reg), ctx);
}

/* Emit 32-bit constant, sign extended. */
static void emit_set_const_sext(s32 K, u32 reg, struct jit_ctx *ctx)
{
	if (K >= 0) {
		emit(SETHI(K, reg), ctx);
		emit(OR_LO(K, reg), ctx);
	} else {
		u32 hbits = ~(u32) K;
		u32 lbits = -0x400 | (u32) K;

		emit(SETHI(hbits, reg), ctx);
		emit(XOR | IMMED | RS1(reg) | S13(lbits) | RD(reg), ctx);
	}
}

static void emit_alu(u32 opcode, u32 src, u32 dst, struct jit_ctx *ctx)
{
	emit(opcode | RS1(dst) | RS2(src) | RD(dst), ctx);
}

static void emit_alu3(u32 opcode, u32 a, u32 b, u32 c, struct jit_ctx *ctx)
{
	emit(opcode | RS1(a) | RS2(b) | RD(c), ctx);
}

static void emit_alu_K(unsigned int opcode, unsigned int dst, unsigned int imm,
		       struct jit_ctx *ctx)
{
	bool small_immed = is_simm13(imm);
	unsigned int insn = opcode;

	insn |= RS1(dst) | RD(dst);
	if (small_immed) {
		emit(insn | IMMED | S13(imm), ctx);
	} else {
		unsigned int tmp = bpf2sparc[TMP_REG_1];

		ctx->tmp_1_used = true;

		emit_set_const_sext(imm, tmp, ctx);
		emit(insn | RS2(tmp), ctx);
	}
}

static void emit_alu3_K(unsigned int opcode, unsigned int src, unsigned int imm,
			unsigned int dst, struct jit_ctx *ctx)
{
	bool small_immed = is_simm13(imm);
	unsigned int insn = opcode;

	insn |= RS1(src) | RD(dst);
	if (small_immed) {
		emit(insn | IMMED | S13(imm), ctx);
	} else {
		unsigned int tmp = bpf2sparc[TMP_REG_1];

		ctx->tmp_1_used = true;

		emit_set_const_sext(imm, tmp, ctx);
		emit(insn | RS2(tmp), ctx);
	}
}

static void emit_loadimm32(s32 K, unsigned int dest, struct jit_ctx *ctx)
{
	if (K >= 0 && is_simm13(K)) {
		/* or %g0, K, DEST */
		emit(OR | IMMED | RS1(G0) | S13(K) | RD(dest), ctx);
	} else {
		emit_set_const(K, dest, ctx);
	}
}

static void emit_loadimm(s32 K, unsigned int dest, struct jit_ctx *ctx)
{
	if (is_simm13(K)) {
		/* or %g0, K, DEST */
		emit(OR | IMMED | RS1(G0) | S13(K) | RD(dest), ctx);
	} else {
		emit_set_const(K, dest, ctx);
	}
}

static void emit_loadimm_sext(s32 K, unsigned int dest, struct jit_ctx *ctx)
{
	if (is_simm13(K)) {
		/* or %g0, K, DEST */
		emit(OR | IMMED | RS1(G0) | S13(K) | RD(dest), ctx);
	} else {
		emit_set_const_sext(K, dest, ctx);
	}
}

static void analyze_64bit_constant(u32 high_bits, u32 low_bits,
				   int *hbsp, int *lbsp, int *abbasp)
{
	int lowest_bit_set, highest_bit_set, all_bits_between_are_set;
	int i;

	lowest_bit_set = highest_bit_set = -1;
	i = 0;
	do {
		if ((lowest_bit_set == -1) && ((low_bits >> i) & 1))
			lowest_bit_set = i;
		if ((highest_bit_set == -1) && ((high_bits >> (32 - i - 1)) & 1))
			highest_bit_set = (64 - i - 1);
	}  while (++i < 32 && (highest_bit_set == -1 ||
			       lowest_bit_set == -1));
	if (i == 32) {
		i = 0;
		do {
			if (lowest_bit_set == -1 && ((high_bits >> i) & 1))
				lowest_bit_set = i + 32;
			if (highest_bit_set == -1 &&
			    ((low_bits >> (32 - i - 1)) & 1))
				highest_bit_set = 32 - i - 1;
		} while (++i < 32 && (highest_bit_set == -1 ||
				      lowest_bit_set == -1));
	}

	all_bits_between_are_set = 1;
	for (i = lowest_bit_set; i <= highest_bit_set; i++) {
		if (i < 32) {
			if ((low_bits & (1 << i)) != 0)
				continue;
		} else {
			if ((high_bits & (1 << (i - 32))) != 0)
				continue;
		}
		all_bits_between_are_set = 0;
		break;
	}
	*hbsp = highest_bit_set;
	*lbsp = lowest_bit_set;
	*abbasp = all_bits_between_are_set;
}

static unsigned long create_simple_focus_bits(unsigned long high_bits,
					      unsigned long low_bits,
					      int lowest_bit_set, int shift)
{
	long hi, lo;

	if (lowest_bit_set < 32) {
		lo = (low_bits >> lowest_bit_set) << shift;
		hi = ((high_bits << (32 - lowest_bit_set)) << shift);
	} else {
		lo = 0;
		hi = ((high_bits >> (lowest_bit_set - 32)) << shift);
	}
	return hi | lo;
}

static bool const64_is_2insns(unsigned long high_bits,
			      unsigned long low_bits)
{
	int highest_bit_set, lowest_bit_set, all_bits_between_are_set;

	if (high_bits == 0 || high_bits == 0xffffffff)
		return true;

	analyze_64bit_constant(high_bits, low_bits,
			       &highest_bit_set, &lowest_bit_set,
			       &all_bits_between_are_set);

	if ((highest_bit_set == 63 || lowest_bit_set == 0) &&
	    all_bits_between_are_set != 0)
		return true;

	if (highest_bit_set - lowest_bit_set < 21)
		return true;

	return false;
}

static void sparc_emit_set_const64_quick2(unsigned long high_bits,
					  unsigned long low_imm,
					  unsigned int dest,
					  int shift_count, struct jit_ctx *ctx)
{
	emit_loadimm32(high_bits, dest, ctx);

	/* Now shift it up into place.  */
	emit_alu_K(SLLX, dest, shift_count, ctx);

	/* If there is a low immediate part piece, finish up by
	 * putting that in as well.
	 */
	if (low_imm != 0)
		emit(OR | IMMED | RS1(dest) | S13(low_imm) | RD(dest), ctx);
}

static void emit_loadimm64(u64 K, unsigned int dest, struct jit_ctx *ctx)
{
	int all_bits_between_are_set, lowest_bit_set, highest_bit_set;
	unsigned int tmp = bpf2sparc[TMP_REG_1];
	u32 low_bits = (K & 0xffffffff);
	u32 high_bits = (K >> 32);

	/* These two tests also take care of all of the one
	 * instruction cases.
	 */
	if (high_bits == 0xffffffff && (low_bits & 0x80000000))
		return emit_loadimm_sext(K, dest, ctx);
	if (high_bits == 0x00000000)
		return emit_loadimm32(K, dest, ctx);

	analyze_64bit_constant(high_bits, low_bits, &highest_bit_set,
			       &lowest_bit_set, &all_bits_between_are_set);

	/* 1) mov	-1, %reg
	 *    sllx	%reg, shift, %reg
	 * 2) mov	-1, %reg
	 *    srlx	%reg, shift, %reg
	 * 3) mov	some_small_const, %reg
	 *    sllx	%reg, shift, %reg
	 */
	if (((highest_bit_set == 63 || lowest_bit_set == 0) &&
	     all_bits_between_are_set != 0) ||
	    ((highest_bit_set - lowest_bit_set) < 12)) {
		int shift = lowest_bit_set;
		long the_const = -1;

		if ((highest_bit_set != 63 && lowest_bit_set != 0) ||
		    all_bits_between_are_set == 0) {
			the_const =
				create_simple_focus_bits(high_bits, low_bits,
							 lowest_bit_set, 0);
		} else if (lowest_bit_set == 0)
			shift = -(63 - highest_bit_set);

		emit(OR | IMMED | RS1(G0) | S13(the_const) | RD(dest), ctx);
		if (shift > 0)
			emit_alu_K(SLLX, dest, shift, ctx);
		else if (shift < 0)
			emit_alu_K(SRLX, dest, -shift, ctx);

		return;
	}

	/* Now a range of 22 or less bits set somewhere.
	 * 1) sethi	%hi(focus_bits), %reg
	 *    sllx	%reg, shift, %reg
	 * 2) sethi	%hi(focus_bits), %reg
	 *    srlx	%reg, shift, %reg
	 */
	if ((highest_bit_set - lowest_bit_set) < 21) {
		unsigned long focus_bits =
			create_simple_focus_bits(high_bits, low_bits,
						 lowest_bit_set, 10);

		emit(SETHI(focus_bits, dest), ctx);

		/* If lowest_bit_set == 10 then a sethi alone could
		 * have done it.
		 */
		if (lowest_bit_set < 10)
			emit_alu_K(SRLX, dest, 10 - lowest_bit_set, ctx);
		else if (lowest_bit_set > 10)
			emit_alu_K(SLLX, dest, lowest_bit_set - 10, ctx);
		return;
	}

	/* Ok, now 3 instruction sequences.  */
	if (low_bits == 0) {
		emit_loadimm32(high_bits, dest, ctx);
		emit_alu_K(SLLX, dest, 32, ctx);
		return;
	}

	/* We may be able to do something quick
	 * when the constant is negated, so try that.
	 */
	if (const64_is_2insns((~high_bits) & 0xffffffff,
			      (~low_bits) & 0xfffffc00)) {
		/* NOTE: The trailing bits get XOR'd so we need the
		 * non-negated bits, not the negated ones.
		 */
		unsigned long trailing_bits = low_bits & 0x3ff;

		if ((((~high_bits) & 0xffffffff) == 0 &&
		     ((~low_bits) & 0x80000000) == 0) ||
		    (((~high_bits) & 0xffffffff) == 0xffffffff &&
		     ((~low_bits) & 0x80000000) != 0)) {
			unsigned long fast_int = (~low_bits & 0xffffffff);

			if ((is_sethi(fast_int) &&
			     (~high_bits & 0xffffffff) == 0)) {
				emit(SETHI(fast_int, dest), ctx);
			} else if (is_simm13(fast_int)) {
				emit(OR | IMMED | RS1(G0) | S13(fast_int) | RD(dest), ctx);
			} else {
				emit_loadimm64(fast_int, dest, ctx);
			}
		} else {
			u64 n = ((~low_bits) & 0xfffffc00) |
				(((unsigned long)((~high_bits) & 0xffffffff))<<32);
			emit_loadimm64(n, dest, ctx);
		}

		low_bits = -0x400 | trailing_bits;

		emit(XOR | IMMED | RS1(dest) | S13(low_bits) | RD(dest), ctx);
		return;
	}

	/* 1) sethi	%hi(xxx), %reg
	 *    or	%reg, %lo(xxx), %reg
	 *    sllx	%reg, yyy, %reg
	 */
	if ((highest_bit_set - lowest_bit_set) < 32) {
		unsigned long focus_bits =
			create_simple_focus_bits(high_bits, low_bits,
						 lowest_bit_set, 0);

		/* So what we know is that the set bits straddle the
		 * middle of the 64-bit word.
		 */
		sparc_emit_set_const64_quick2(focus_bits, 0, dest,
					      lowest_bit_set, ctx);
		return;
	}

	/* 1) sethi	%hi(high_bits), %reg
	 *    or	%reg, %lo(high_bits), %reg
	 *    sllx	%reg, 32, %reg
	 *    or	%reg, low_bits, %reg
	 */
	if (is_simm13(low_bits) && ((int)low_bits > 0)) {
		sparc_emit_set_const64_quick2(high_bits, low_bits,
					      dest, 32, ctx);
		return;
	}

	/* Oh well, we tried... Do a full 64-bit decomposition.  */
	ctx->tmp_1_used = true;

	emit_loadimm32(high_bits, tmp, ctx);
	emit_loadimm32(low_bits, dest, ctx);
	emit_alu_K(SLLX, tmp, 32, ctx);
	emit(OR | RS1(dest) | RS2(tmp) | RD(dest), ctx);
}

static void emit_branch(unsigned int br_opc, unsigned int from_idx, unsigned int to_idx,
			struct jit_ctx *ctx)
{
	unsigned int off = to_idx - from_idx;

	if (br_opc & XCC)
		emit(br_opc | WDISP19(off << 2), ctx);
	else
		emit(br_opc | WDISP22(off << 2), ctx);
}

static void emit_cbcond(unsigned int cb_opc, unsigned int from_idx, unsigned int to_idx,
			const u8 dst, const u8 src, struct jit_ctx *ctx)
{
	unsigned int off = to_idx - from_idx;

	emit(cb_opc | WDISP10(off << 2) | RS1(dst) | RS2(src), ctx);
}

static void emit_cbcondi(unsigned int cb_opc, unsigned int from_idx, unsigned int to_idx,
			 const u8 dst, s32 imm, struct jit_ctx *ctx)
{
	unsigned int off = to_idx - from_idx;

	emit(cb_opc | IMMED | WDISP10(off << 2) | RS1(dst) | S5(imm), ctx);
}

#define emit_read_y(REG, CTX)	emit(RD_Y | RD(REG), CTX)
#define emit_write_y(REG, CTX)	emit(WR_Y | IMMED | RS1(REG) | S13(0), CTX)

#define emit_cmp(R1, R2, CTX)				\
	emit(SUBCC | RS1(R1) | RS2(R2) | RD(G0), CTX)

#define emit_cmpi(R1, IMM, CTX)				\
	emit(SUBCC | IMMED | RS1(R1) | S13(IMM) | RD(G0), CTX)

#define emit_btst(R1, R2, CTX)				\
	emit(ANDCC | RS1(R1) | RS2(R2) | RD(G0), CTX)

#define emit_btsti(R1, IMM, CTX)			\
	emit(ANDCC | IMMED | RS1(R1) | S13(IMM) | RD(G0), CTX)

static int emit_compare_and_branch(const u8 code, const u8 dst, u8 src,
				   const s32 imm, bool is_imm, int branch_dst,
				   struct jit_ctx *ctx)
{
	bool use_cbcond = (sparc64_elf_hwcap & AV_SPARC_CBCOND) != 0;
	const u8 tmp = bpf2sparc[TMP_REG_1];

	branch_dst = ctx->offset[branch_dst];

	if (!is_simm10(branch_dst - ctx->idx) ||
	    BPF_OP(code) == BPF_JSET)
		use_cbcond = false;

	if (is_imm) {
		bool fits = true;

		if (use_cbcond) {
			if (!is_simm5(imm))
				fits = false;
		} else if (!is_simm13(imm)) {
			fits = false;
		}
		if (!fits) {
			ctx->tmp_1_used = true;
			emit_loadimm_sext(imm, tmp, ctx);
			src = tmp;
			is_imm = false;
		}
	}

	if (!use_cbcond) {
		u32 br_opcode;

		if (BPF_OP(code) == BPF_JSET) {
			if (is_imm)
				emit_btsti(dst, imm, ctx);
			else
				emit_btst(dst, src, ctx);
		} else {
			if (is_imm)
				emit_cmpi(dst, imm, ctx);
			else
				emit_cmp(dst, src, ctx);
		}
		switch (BPF_OP(code)) {
		case BPF_JEQ:
			br_opcode = BE;
			break;
		case BPF_JGT:
			br_opcode = BGU;
			break;
		case BPF_JLT:
			br_opcode = BLU;
			break;
		case BPF_JGE:
			br_opcode = BGEU;
			break;
		case BPF_JLE:
			br_opcode = BLEU;
			break;
		case BPF_JSET:
		case BPF_JNE:
			br_opcode = BNE;
			break;
		case BPF_JSGT:
			br_opcode = BG;
			break;
		case BPF_JSLT:
			br_opcode = BL;
			break;
		case BPF_JSGE:
			br_opcode = BGE;
			break;
		case BPF_JSLE:
			br_opcode = BLE;
			break;
		default:
			/* Make sure we dont leak kernel information to the
			 * user.
			 */
			return -EFAULT;
		}
		emit_branch(br_opcode, ctx->idx, branch_dst, ctx);
		emit_nop(ctx);
	} else {
		u32 cbcond_opcode;

		switch (BPF_OP(code)) {
		case BPF_JEQ:
			cbcond_opcode = CBCONDE;
			break;
		case BPF_JGT:
			cbcond_opcode = CBCONDGU;
			break;
		case BPF_JLT:
			cbcond_opcode = CBCONDLU;
			break;
		case BPF_JGE:
			cbcond_opcode = CBCONDGEU;
			break;
		case BPF_JLE:
			cbcond_opcode = CBCONDLEU;
			break;
		case BPF_JNE:
			cbcond_opcode = CBCONDNE;
			break;
		case BPF_JSGT:
			cbcond_opcode = CBCONDG;
			break;
		case BPF_JSLT:
			cbcond_opcode = CBCONDL;
			break;
		case BPF_JSGE:
			cbcond_opcode = CBCONDGE;
			break;
		case BPF_JSLE:
			cbcond_opcode = CBCONDLE;
			break;
		default:
			/* Make sure we dont leak kernel information to the
			 * user.
			 */
			return -EFAULT;
		}
		cbcond_opcode |= CBCOND_OP;
		if (is_imm)
			emit_cbcondi(cbcond_opcode, ctx->idx, branch_dst,
				     dst, imm, ctx);
		else
			emit_cbcond(cbcond_opcode, ctx->idx, branch_dst,
				    dst, src, ctx);
	}
	return 0;
}

/* Just skip the save instruction and the ctx register move.  */
#define BPF_TAILCALL_PROLOGUE_SKIP	32
#define BPF_TAILCALL_CNT_SP_OFF		(STACK_BIAS + 128)

static void build_prologue(struct jit_ctx *ctx)
{
	s32 stack_needed = BASE_STACKFRAME;

	if (ctx->saw_frame_pointer || ctx->saw_tail_call) {
		struct bpf_prog *prog = ctx->prog;
		u32 stack_depth;

		stack_depth = prog->aux->stack_depth;
		stack_needed += round_up(stack_depth, 16);
	}

	if (ctx->saw_tail_call)
		stack_needed += 8;

	/* save %sp, -176, %sp */
	emit(SAVE | IMMED | RS1(SP) | S13(-stack_needed) | RD(SP), ctx);

	/* tail_call_cnt = 0 */
	if (ctx->saw_tail_call) {
		u32 off = BPF_TAILCALL_CNT_SP_OFF;

		emit(ST32 | IMMED | RS1(SP) | S13(off) | RD(G0), ctx);
	} else {
		emit_nop(ctx);
	}
	if (ctx->saw_frame_pointer) {
		const u8 vfp = bpf2sparc[BPF_REG_FP];

		emit(ADD | IMMED | RS1(FP) | S13(STACK_BIAS) | RD(vfp), ctx);
	} else {
		emit_nop(ctx);
	}

	emit_reg_move(I0, O0, ctx);
	emit_reg_move(I1, O1, ctx);
	emit_reg_move(I2, O2, ctx);
	emit_reg_move(I3, O3, ctx);
	emit_reg_move(I4, O4, ctx);
	/* If you add anything here, adjust BPF_TAILCALL_PROLOGUE_SKIP above. */
}

static void build_epilogue(struct jit_ctx *ctx)
{
	ctx->epilogue_offset = ctx->idx;

	/* ret (jmpl %i7 + 8, %g0) */
	emit(JMPL | IMMED | RS1(I7) | S13(8) | RD(G0), ctx);

	/* restore %i5, %g0, %o0 */
	emit(RESTORE | RS1(bpf2sparc[BPF_REG_0]) | RS2(G0) | RD(O0), ctx);
}

static void emit_tail_call(struct jit_ctx *ctx)
{
	const u8 bpf_array = bpf2sparc[BPF_REG_2];
	const u8 bpf_index = bpf2sparc[BPF_REG_3];
	const u8 tmp = bpf2sparc[TMP_REG_1];
	u32 off;

	ctx->saw_tail_call = true;

	off = offsetof(struct bpf_array, map.max_entries);
	emit(LD32 | IMMED | RS1(bpf_array) | S13(off) | RD(tmp), ctx);
	emit_cmp(bpf_index, tmp, ctx);
#define OFFSET1 17
	emit_branch(BGEU, ctx->idx, ctx->idx + OFFSET1, ctx);
	emit_nop(ctx);

	off = BPF_TAILCALL_CNT_SP_OFF;
	emit(LD32 | IMMED | RS1(SP) | S13(off) | RD(tmp), ctx);
	emit_cmpi(tmp, MAX_TAIL_CALL_CNT, ctx);
#define OFFSET2 13
	emit_branch(BGU, ctx->idx, ctx->idx + OFFSET2, ctx);
	emit_nop(ctx);

	emit_alu_K(ADD, tmp, 1, ctx);
	off = BPF_TAILCALL_CNT_SP_OFF;
	emit(ST32 | IMMED | RS1(SP) | S13(off) | RD(tmp), ctx);

	emit_alu3_K(SLL, bpf_index, 3, tmp, ctx);
	emit_alu(ADD, bpf_array, tmp, ctx);
	off = offsetof(struct bpf_array, ptrs);
	emit(LD64 | IMMED | RS1(tmp) | S13(off) | RD(tmp), ctx);

	emit_cmpi(tmp, 0, ctx);
#define OFFSET3 5
	emit_branch(BE, ctx->idx, ctx->idx + OFFSET3, ctx);
	emit_nop(ctx);

	off = offsetof(struct bpf_prog, bpf_func);
	emit(LD64 | IMMED | RS1(tmp) | S13(off) | RD(tmp), ctx);

	off = BPF_TAILCALL_PROLOGUE_SKIP;
	emit(JMPL | IMMED | RS1(tmp) | S13(off) | RD(G0), ctx);
	emit_nop(ctx);
}

static int build_insn(const struct bpf_insn *insn, struct jit_ctx *ctx)
{
	const u8 code = insn->code;
	const u8 dst = bpf2sparc[insn->dst_reg];
	const u8 src = bpf2sparc[insn->src_reg];
	const int i = insn - ctx->prog->insnsi;
	const s16 off = insn->off;
	const s32 imm = insn->imm;

	if (insn->src_reg == BPF_REG_FP)
		ctx->saw_frame_pointer = true;

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
		emit_alu3_K(SRL, src, 0, dst, ctx);
		break;
	case BPF_ALU64 | BPF_MOV | BPF_X:
		emit_reg_move(src, dst, ctx);
		break;
	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit_alu(ADD, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit_alu(SUB, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit_alu(AND, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit_alu(OR, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit_alu(XOR, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_MUL | BPF_X:
		emit_alu(MUL, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit_alu(MULX, src, dst, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
		emit_write_y(G0, ctx);
		emit_alu(DIV, src, dst, ctx);
		break;
	case BPF_ALU64 | BPF_DIV | BPF_X:
		emit_alu(UDIVX, src, dst, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_X: {
		const u8 tmp = bpf2sparc[TMP_REG_1];

		ctx->tmp_1_used = true;

		emit_write_y(G0, ctx);
		emit_alu3(DIV, dst, src, tmp, ctx);
		emit_alu3(MULX, tmp, src, tmp, ctx);
		emit_alu3(SUB, dst, tmp, dst, ctx);
		goto do_alu32_trunc;
	}
	case BPF_ALU64 | BPF_MOD | BPF_X: {
		const u8 tmp = bpf2sparc[TMP_REG_1];

		ctx->tmp_1_used = true;

		emit_alu3(UDIVX, dst, src, tmp, ctx);
		emit_alu3(MULX, tmp, src, tmp, ctx);
		emit_alu3(SUB, dst, tmp, dst, ctx);
		break;
	}
	case BPF_ALU | BPF_LSH | BPF_X:
		emit_alu(SLL, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit_alu(SLLX, src, dst, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
		emit_alu(SRL, src, dst, ctx);
		break;
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit_alu(SRLX, src, dst, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
		emit_alu(SRA, src, dst, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit_alu(SRAX, src, dst, ctx);
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit(SUB | RS1(0) | RS2(dst) | RD(dst), ctx);
		goto do_alu32_trunc;

	case BPF_ALU | BPF_END | BPF_FROM_BE:
		switch (imm) {
		case 16:
			emit_alu_K(SLL, dst, 16, ctx);
			emit_alu_K(SRL, dst, 16, ctx);
			break;
		case 32:
			emit_alu_K(SRL, dst, 0, ctx);
			break;
		case 64:
			/* nop */
			break;

		}
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		const u8 tmp2 = bpf2sparc[TMP_REG_2];

		ctx->tmp_1_used = true;
		switch (imm) {
		case 16:
			emit_alu3_K(AND, dst, 0xff, tmp, ctx);
			emit_alu3_K(SRL, dst, 8, dst, ctx);
			emit_alu3_K(AND, dst, 0xff, dst, ctx);
			emit_alu3_K(SLL, tmp, 8, tmp, ctx);
			emit_alu(OR, tmp, dst, ctx);
			break;

		case 32:
			ctx->tmp_2_used = true;
			emit_alu3_K(SRL, dst, 24, tmp, ctx);	/* tmp  = dst >> 24 */
			emit_alu3_K(SRL, dst, 16, tmp2, ctx);	/* tmp2 = dst >> 16 */
			emit_alu3_K(AND, tmp2, 0xff, tmp2, ctx);/* tmp2 = tmp2 & 0xff */
			emit_alu3_K(SLL, tmp2, 8, tmp2, ctx);	/* tmp2 = tmp2 << 8 */
			emit_alu(OR, tmp2, tmp, ctx);		/* tmp  = tmp | tmp2 */
			emit_alu3_K(SRL, dst, 8, tmp2, ctx);	/* tmp2 = dst >> 8 */
			emit_alu3_K(AND, tmp2, 0xff, tmp2, ctx);/* tmp2 = tmp2 & 0xff */
			emit_alu3_K(SLL, tmp2, 16, tmp2, ctx);	/* tmp2 = tmp2 << 16 */
			emit_alu(OR, tmp2, tmp, ctx);		/* tmp  = tmp | tmp2 */
			emit_alu3_K(AND, dst, 0xff, dst, ctx);	/* dst	= dst & 0xff */
			emit_alu3_K(SLL, dst, 24, dst, ctx);	/* dst  = dst << 24 */
			emit_alu(OR, tmp, dst, ctx);		/* dst  = dst | tmp */
			break;

		case 64:
			emit_alu3_K(ADD, SP, STACK_BIAS + 128, tmp, ctx);
			emit(ST64 | RS1(tmp) | RS2(G0) | RD(dst), ctx);
			emit(LD64A | ASI(ASI_PL) | RS1(tmp) | RS2(G0) | RD(dst), ctx);
			break;
		}
		break;
	}
	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
		emit_loadimm32(imm, dst, ctx);
		break;
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_loadimm_sext(imm, dst, ctx);
		break;
	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		emit_alu_K(ADD, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		emit_alu_K(SUB, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		emit_alu_K(AND, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		emit_alu_K(OR, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		emit_alu_K(XOR, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU | BPF_MUL | BPF_K:
		emit_alu_K(MUL, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_alu_K(MULX, dst, imm, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
		if (imm == 0)
			return -EINVAL;

		emit_write_y(G0, ctx);
		emit_alu_K(DIV, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_DIV | BPF_K:
		if (imm == 0)
			return -EINVAL;

		emit_alu_K(UDIVX, dst, imm, ctx);
		break;
	case BPF_ALU64 | BPF_MOD | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K: {
		const u8 tmp = bpf2sparc[TMP_REG_2];
		unsigned int div;

		if (imm == 0)
			return -EINVAL;

		div = (BPF_CLASS(code) == BPF_ALU64) ? UDIVX : DIV;

		ctx->tmp_2_used = true;

		if (BPF_CLASS(code) != BPF_ALU64)
			emit_write_y(G0, ctx);
		if (is_simm13(imm)) {
			emit(div | IMMED | RS1(dst) | S13(imm) | RD(tmp), ctx);
			emit(MULX | IMMED | RS1(tmp) | S13(imm) | RD(tmp), ctx);
			emit(SUB | RS1(dst) | RS2(tmp) | RD(dst), ctx);
		} else {
			const u8 tmp1 = bpf2sparc[TMP_REG_1];

			ctx->tmp_1_used = true;

			emit_set_const_sext(imm, tmp1, ctx);
			emit(div | RS1(dst) | RS2(tmp1) | RD(tmp), ctx);
			emit(MULX | RS1(tmp) | RS2(tmp1) | RD(tmp), ctx);
			emit(SUB | RS1(dst) | RS2(tmp) | RD(dst), ctx);
		}
		goto do_alu32_trunc;
	}
	case BPF_ALU | BPF_LSH | BPF_K:
		emit_alu_K(SLL, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit_alu_K(SLLX, dst, imm, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
		emit_alu_K(SRL, dst, imm, ctx);
		break;
	case BPF_ALU64 | BPF_RSH | BPF_K:
		emit_alu_K(SRLX, dst, imm, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
		emit_alu_K(SRA, dst, imm, ctx);
		goto do_alu32_trunc;
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit_alu_K(SRAX, dst, imm, ctx);
		break;

	do_alu32_trunc:
		if (BPF_CLASS(code) == BPF_ALU)
			emit_alu_K(SRL, dst, 0, ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		emit_branch(BA, ctx->idx, ctx->offset[i + off], ctx);
		emit_nop(ctx);
		break;
	/* IF (dst COND src) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
	case BPF_JMP | BPF_JSET | BPF_X: {
		int err;

		err = emit_compare_and_branch(code, dst, src, 0, false, i + off, ctx);
		if (err)
			return err;
		break;
	}
	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
	case BPF_JMP | BPF_JSET | BPF_K: {
		int err;

		err = emit_compare_and_branch(code, dst, 0, imm, true, i + off, ctx);
		if (err)
			return err;
		break;
	}

	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		u8 *func = ((u8 *)__bpf_call_base) + imm;

		ctx->saw_call = true;

		emit_call((u32 *)func, ctx);
		emit_nop(ctx);

		emit_reg_move(O0, bpf2sparc[BPF_REG_0], ctx);
		break;
	}

	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		emit_tail_call(ctx);
		break;

	/* function return */
	case BPF_JMP | BPF_EXIT:
		/* Optimization: when last instruction is EXIT,
		   simply fallthrough to epilogue. */
		if (i == ctx->prog->len - 1)
			break;
		emit_branch(BA, ctx->idx, ctx->epilogue_offset, ctx);
		emit_nop(ctx);
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		const struct bpf_insn insn1 = insn[1];
		u64 imm64;

		imm64 = (u64)insn1.imm << 32 | (u32)imm;
		emit_loadimm64(imm64, dst, ctx);

		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		u32 opcode = 0, rs2;

		ctx->tmp_1_used = true;
		switch (BPF_SIZE(code)) {
		case BPF_W:
			opcode = LD32;
			break;
		case BPF_H:
			opcode = LD16;
			break;
		case BPF_B:
			opcode = LD8;
			break;
		case BPF_DW:
			opcode = LD64;
			break;
		}

		if (is_simm13(off)) {
			opcode |= IMMED;
			rs2 = S13(off);
		} else {
			emit_loadimm(off, tmp, ctx);
			rs2 = RS2(tmp);
		}
		emit(opcode | RS1(src) | rs2 | RD(dst), ctx);
		break;
	}
	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		const u8 tmp2 = bpf2sparc[TMP_REG_2];
		u32 opcode = 0, rs2;

		if (insn->dst_reg == BPF_REG_FP)
			ctx->saw_frame_pointer = true;

		ctx->tmp_2_used = true;
		emit_loadimm(imm, tmp2, ctx);

		switch (BPF_SIZE(code)) {
		case BPF_W:
			opcode = ST32;
			break;
		case BPF_H:
			opcode = ST16;
			break;
		case BPF_B:
			opcode = ST8;
			break;
		case BPF_DW:
			opcode = ST64;
			break;
		}

		if (is_simm13(off)) {
			opcode |= IMMED;
			rs2 = S13(off);
		} else {
			ctx->tmp_1_used = true;
			emit_loadimm(off, tmp, ctx);
			rs2 = RS2(tmp);
		}
		emit(opcode | RS1(dst) | rs2 | RD(tmp2), ctx);
		break;
	}

	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		u32 opcode = 0, rs2;

		if (insn->dst_reg == BPF_REG_FP)
			ctx->saw_frame_pointer = true;

		switch (BPF_SIZE(code)) {
		case BPF_W:
			opcode = ST32;
			break;
		case BPF_H:
			opcode = ST16;
			break;
		case BPF_B:
			opcode = ST8;
			break;
		case BPF_DW:
			opcode = ST64;
			break;
		}
		if (is_simm13(off)) {
			opcode |= IMMED;
			rs2 = S13(off);
		} else {
			ctx->tmp_1_used = true;
			emit_loadimm(off, tmp, ctx);
			rs2 = RS2(tmp);
		}
		emit(opcode | RS1(dst) | rs2 | RD(src), ctx);
		break;
	}

	/* STX XADD: lock *(u32 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_W: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		const u8 tmp2 = bpf2sparc[TMP_REG_2];
		const u8 tmp3 = bpf2sparc[TMP_REG_3];

		if (insn->dst_reg == BPF_REG_FP)
			ctx->saw_frame_pointer = true;

		ctx->tmp_1_used = true;
		ctx->tmp_2_used = true;
		ctx->tmp_3_used = true;
		emit_loadimm(off, tmp, ctx);
		emit_alu3(ADD, dst, tmp, tmp, ctx);

		emit(LD32 | RS1(tmp) | RS2(G0) | RD(tmp2), ctx);
		emit_alu3(ADD, tmp2, src, tmp3, ctx);
		emit(CAS | ASI(ASI_P) | RS1(tmp) | RS2(tmp2) | RD(tmp3), ctx);
		emit_cmp(tmp2, tmp3, ctx);
		emit_branch(BNE, 4, 0, ctx);
		emit_nop(ctx);
		break;
	}
	/* STX XADD: lock *(u64 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_DW: {
		const u8 tmp = bpf2sparc[TMP_REG_1];
		const u8 tmp2 = bpf2sparc[TMP_REG_2];
		const u8 tmp3 = bpf2sparc[TMP_REG_3];

		if (insn->dst_reg == BPF_REG_FP)
			ctx->saw_frame_pointer = true;

		ctx->tmp_1_used = true;
		ctx->tmp_2_used = true;
		ctx->tmp_3_used = true;
		emit_loadimm(off, tmp, ctx);
		emit_alu3(ADD, dst, tmp, tmp, ctx);

		emit(LD64 | RS1(tmp) | RS2(G0) | RD(tmp2), ctx);
		emit_alu3(ADD, tmp2, src, tmp3, ctx);
		emit(CASX | ASI(ASI_P) | RS1(tmp) | RS2(tmp2) | RD(tmp3), ctx);
		emit_cmp(tmp2, tmp3, ctx);
		emit_branch(BNE, 4, 0, ctx);
		emit_nop(ctx);
		break;
	}

	default:
		pr_err_once("unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

static int build_body(struct jit_ctx *ctx)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		ret = build_insn(insn, ctx);

		if (ret > 0) {
			i++;
			ctx->offset[i] = ctx->idx;
			continue;
		}
		ctx->offset[i] = ctx->idx;
		if (ret)
			return ret;
	}
	return 0;
}

static void jit_fill_hole(void *area, unsigned int size)
{
	u32 *ptr;
	/* We are guaranteed to have aligned memory. */
	for (ptr = area; size >= sizeof(u32); size -= sizeof(u32))
		*ptr++ = 0x91d02005; /* ta 5 */
}

struct sparc64_jit_data {
	struct bpf_binary_header *header;
	u8 *image;
	struct jit_ctx ctx;
};

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_prog *tmp, *orig_prog = prog;
	struct sparc64_jit_data *jit_data;
	struct bpf_binary_header *header;
	u32 prev_image_size, image_size;
	bool tmp_blinded = false;
	bool extra_pass = false;
	struct jit_ctx ctx;
	u8 *image_ptr;
	int pass, i;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	/* If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter.
	 */
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
	if (jit_data->ctx.offset) {
		ctx = jit_data->ctx;
		image_ptr = jit_data->image;
		header = jit_data->header;
		extra_pass = true;
		image_size = sizeof(u32) * ctx.idx;
		prev_image_size = image_size;
		pass = 1;
		goto skip_init_ctx;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.prog = prog;

	ctx.offset = kmalloc_array(prog->len, sizeof(unsigned int), GFP_KERNEL);
	if (ctx.offset == NULL) {
		prog = orig_prog;
		goto out_off;
	}

	/* Longest sequence emitted is for bswap32, 12 instructions.  Pre-cook
	 * the offset array so that we converge faster.
	 */
	for (i = 0; i < prog->len; i++)
		ctx.offset[i] = i * (12 * 4);

	prev_image_size = ~0U;
	for (pass = 1; pass < 40; pass++) {
		ctx.idx = 0;

		build_prologue(&ctx);
		if (build_body(&ctx)) {
			prog = orig_prog;
			goto out_off;
		}
		build_epilogue(&ctx);

		if (bpf_jit_enable > 1)
			pr_info("Pass %d: size = %u, seen = [%c%c%c%c%c%c]\n", pass,
				ctx.idx * 4,
				ctx.tmp_1_used ? '1' : ' ',
				ctx.tmp_2_used ? '2' : ' ',
				ctx.tmp_3_used ? '3' : ' ',
				ctx.saw_frame_pointer ? 'F' : ' ',
				ctx.saw_call ? 'C' : ' ',
				ctx.saw_tail_call ? 'T' : ' ');

		if (ctx.idx * 4 == prev_image_size)
			break;
		prev_image_size = ctx.idx * 4;
		cond_resched();
	}

	/* Now we know the actual image size. */
	image_size = sizeof(u32) * ctx.idx;
	header = bpf_jit_binary_alloc(image_size, &image_ptr,
				      sizeof(u32), jit_fill_hole);
	if (header == NULL) {
		prog = orig_prog;
		goto out_off;
	}

	ctx.image = (u32 *)image_ptr;
skip_init_ctx:
	ctx.idx = 0;

	build_prologue(&ctx);

	if (build_body(&ctx)) {
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_off;
	}

	build_epilogue(&ctx);

	if (ctx.idx * 4 != prev_image_size) {
		pr_err("bpf_jit: Failed to converge, prev_size=%u size=%d\n",
		       prev_image_size, ctx.idx * 4);
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_off;
	}

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, pass, ctx.image);

	bpf_flush_icache(header, (u8 *)header + (header->pages * PAGE_SIZE));

	if (!prog->is_func || extra_pass) {
		bpf_jit_binary_lock_ro(header);
	} else {
		jit_data->ctx = ctx;
		jit_data->image = image_ptr;
		jit_data->header = header;
	}

	prog->bpf_func = (void *)ctx.image;
	prog->jited = 1;
	prog->jited_len = image_size;

	if (!prog->is_func || extra_pass) {
out_off:
		kfree(ctx.offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	return prog;
}
