/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common functionality for PARISC32 and PARISC64 BPF JIT compilers
 *
 * Copyright (c) 2023 Helge Deller <deller@gmx.de>
 *
 */

#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#include <linux/bpf.h>
#include <linux/filter.h>
#include <asm/cacheflush.h>

#define HPPA_JIT_DEBUG	0
#define HPPA_JIT_REBOOT	0
#define HPPA_JIT_DUMP	0

#define OPTIMIZE_HPPA	1	/* enable some asm optimizations */
// echo 1 > /proc/sys/net/core/bpf_jit_enable

#define HPPA_R(nr)	nr	/* use HPPA register #nr */

enum {
	HPPA_REG_ZERO =	0,	/* The constant value 0 */
	HPPA_REG_R1 =	1,	/* used for addil */
	HPPA_REG_RP =	2,	/* Return address */

	HPPA_REG_ARG7 =	19,	/* ARG4-7 used in 64-bit ABI */
	HPPA_REG_ARG6 =	20,
	HPPA_REG_ARG5 =	21,
	HPPA_REG_ARG4 =	22,

	HPPA_REG_ARG3 =	23,	/* ARG0-3 in 32- and 64-bit ABI */
	HPPA_REG_ARG2 =	24,
	HPPA_REG_ARG1 =	25,
	HPPA_REG_ARG0 =	26,
	HPPA_REG_GP =	27,	/* Global pointer */
	HPPA_REG_RET0 =	28,	/* Return value, HI in 32-bit */
	HPPA_REG_RET1 =	29,	/* Return value, LOW in 32-bit */
	HPPA_REG_SP =	30,	/* Stack pointer */
	HPPA_REG_R31 =	31,

#ifdef CONFIG_64BIT
	HPPA_REG_TCC	     = 3,
	HPPA_REG_TCC_SAVED   = 4,
	HPPA_REG_TCC_IN_INIT = HPPA_REG_R31,
#else
	HPPA_REG_TCC	     = 18,
	HPPA_REG_TCC_SAVED   = 17,
	HPPA_REG_TCC_IN_INIT = HPPA_REG_R31,
#endif

	HPPA_REG_T0 =	HPPA_REG_R1,	/* Temporaries */
	HPPA_REG_T1 =	HPPA_REG_R31,
	HPPA_REG_T2 =	HPPA_REG_ARG4,
#ifndef CONFIG_64BIT
	HPPA_REG_T3 =	HPPA_REG_ARG5,	/* not used in 64-bit */
	HPPA_REG_T4 =	HPPA_REG_ARG6,
	HPPA_REG_T5 =	HPPA_REG_ARG7,
#endif
};

struct hppa_jit_context {
	struct bpf_prog *prog;
	u32 *insns;		/* HPPA insns */
	int ninsns;
	int reg_seen_collect;
	int reg_seen;
	int body_len;
	int epilogue_offset;
	int prologue_len;
	int *offset;		/* BPF to HPPA */
};

#define REG_SET_SEEN(ctx, nr)	{ if (ctx->reg_seen_collect) ctx->reg_seen |= BIT(nr); }
#define REG_SET_SEEN_ALL(ctx)	{ if (ctx->reg_seen_collect) ctx->reg_seen = -1; }
#define REG_FORCE_SEEN(ctx, nr)	{ ctx->reg_seen |= BIT(nr); }
#define REG_WAS_SEEN(ctx, nr)	(ctx->reg_seen & BIT(nr))
#define REG_ALL_SEEN(ctx)	(ctx->reg_seen == -1)

#define HPPA_INSN_SIZE		4	/* bytes per HPPA asm instruction */
#define REG_SIZE		REG_SZ	/* bytes per native "long" word */

/* subtract hppa displacement on branches which is .+8 */
#define HPPA_BRANCH_DISPLACEMENT  2	/* instructions */

/* asm statement indicator to execute delay slot */
#define EXEC_NEXT_INSTR	0
#define NOP_NEXT_INSTR	1

#define im11(val)	(((u32)(val)) & 0x07ff)

#define hppa_ldil(addr, reg) \
	hppa_t5_insn(0x08, reg, ((u32)(addr)) >> 11)		/* ldil im21,reg */
#define hppa_addil(addr, reg) \
	hppa_t5_insn(0x0a, reg, ((u32)(addr)) >> 11)		/* addil im21,reg -> result in gr1 */
#define hppa_ldo(im14, reg, target) \
	hppa_t1_insn(0x0d, reg, target, im14)			/* ldo val14(reg),target */
#define hppa_ldi(im14, reg) \
	hppa_ldo(im14, HPPA_REG_ZERO, reg)			/* ldi val14,reg */
#define hppa_or(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x09, target)	/* or reg1,reg2,target */
#define hppa_or_cond(reg1, reg2, cond, f, target) \
	hppa_t6_insn(0x02, reg2, reg1, cond, f, 0x09, target)
#define hppa_and(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x08, target)	/* and reg1,reg2,target */
#define hppa_and_cond(reg1, reg2, cond, f, target) \
	hppa_t6_insn(0x02, reg2, reg1, cond, f, 0x08, target)
#define hppa_xor(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x0a, target)	/* xor reg1,reg2,target */
#define hppa_add(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x18, target)	/* add reg1,reg2,target */
#define hppa_addc(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x1c, target)	/* add,c reg1,reg2,target */
#define hppa_sub(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x10, target)	/* sub reg1,reg2,target */
#define hppa_subb(reg1, reg2, target) \
	hppa_t6_insn(0x02, reg2, reg1, 0, 0, 0x14, target)	/* sub,b reg1,reg2,target */
#define hppa_nop() \
	hppa_or(0,0,0)						/* nop: or 0,0,0 */
#define hppa_addi(val11, reg, target) \
	hppa_t7_insn(0x2d, reg, target, val11)			/* addi im11,reg,target */
#define hppa_subi(val11, reg, target) \
	hppa_t7_insn(0x25, reg, target, val11)			/* subi im11,reg,target */
#define hppa_copy(reg, target) \
	hppa_or(reg, HPPA_REG_ZERO, target)			/* copy reg,target */
#define hppa_ldw(val14, reg, target) \
	hppa_t1_insn(0x12, reg, target, val14)			/* ldw im14(reg),target */
#define hppa_ldb(val14, reg, target) \
	hppa_t1_insn(0x10, reg, target, val14)			/* ldb im14(reg),target */
#define hppa_ldh(val14, reg, target) \
	hppa_t1_insn(0x11, reg, target, val14)			/* ldh im14(reg),target */
#define hppa_stw(reg, val14, base) \
	hppa_t1_insn(0x1a, base, reg, val14)			/* stw reg,im14(base) */
#define hppa_stb(reg, val14, base) \
	hppa_t1_insn(0x18, base, reg, val14)			/* stb reg,im14(base) */
#define hppa_sth(reg, val14, base) \
	hppa_t1_insn(0x19, base, reg, val14)			/* sth reg,im14(base) */
#define hppa_stwma(reg, val14, base) \
	hppa_t1_insn(0x1b, base, reg, val14)			/* stw,ma reg,im14(base) */
#define hppa_bv(reg, base, nop) \
	hppa_t11_insn(0x3a, base, reg, 0x06, 0, nop)		/* bv(,n) reg(base) */
#define hppa_be(offset, base) \
	hppa_t12_insn(0x38, base, offset, 0x00, 1)		/* be,n offset(0,base) */
#define hppa_be_l(offset, base, nop) \
	hppa_t12_insn(0x39, base, offset, 0x00, nop)		/* ble(,nop) offset(0,base) */
#define hppa_mtctl(reg, cr) \
	hppa_t21_insn(0x00, cr, reg, 0xc2, 0)			/* mtctl reg,cr */
#define hppa_mtsar(reg) \
	hppa_mtctl(reg, 11)					/* mtsar reg */
#define hppa_zdep(r, p, len, target) \
	hppa_t10_insn(0x35, target, r, 0, 2, p, len)		/* zdep r,a,b,t */
#define hppa_shl(r, len, target) \
	hppa_zdep(r, len, len, lo(rd))
#define hppa_depwz(r, p, len, target) \
	hppa_t10_insn(0x35, target, r, 0, 3, 31-(p), 32-(len))	/* depw,z r,p,len,ret1 */
#define hppa_depwz_sar(reg, target) \
	hppa_t1_insn(0x35, target, reg, 0)			/* depw,z reg,sar,32,target */
#define hppa_shrpw_sar(reg, target) \
	hppa_t10_insn(0x34, reg, 0, 0, 0, 0, target)		/* shrpw r0,reg,sar,target */
#define hppa_shrpw(r1, r2, p, target) \
	hppa_t10_insn(0x34, r2, r1, 0, 2, 31-(p), target)	/* shrpw r1,r2,p,target */
#define hppa_shd(r1, r2, p, target) \
	hppa_t10_insn(0x34, r2, r1, 0, 2, 31-(p), target)	/* shrpw r1,r2,p,tarfer */
#define hppa_extrws_sar(reg, target) \
	hppa_t10_insn(0x34, reg, target, 0, 5, 0, 0)		/* extrw,s reg,sar,32,ret0 */
#define hppa_extrws(reg, p, len, target) \
	hppa_t10_insn(0x34, reg, target, 0, 7, p, len)		/* extrw,s reg,p,len,target */
#define hppa_extru(r, p, len, target) \
	hppa_t10_insn(0x34, r, target, 0, 6, p, 32-(len))
#define hppa_shr(r, len, target) \
	hppa_extru(r, 31-(len), 32-(len), target)
#define hppa_bl(imm17, rp) \
	hppa_t12_insn(0x3a, rp, imm17, 0x00, 1)			/* bl,n target_addr,rp */
#define hppa_sh2add(r1, r2, target) \
	hppa_t6_insn(0x02, r2, r1, 0, 0, 0x1a, target)		/* sh2add r1,r2,target */

#define hppa_combt(r1, r2, target_addr, condition, nop) \
	hppa_t11_insn(IS_ENABLED(CONFIG_64BIT) ? 0x27 : 0x20, \
		r2, r1, condition, target_addr, nop)		/* combt,cond,n r1,r2,addr */
#define hppa_beq(r1, r2, target_addr) \
	hppa_combt(r1, r2, target_addr, 1, NOP_NEXT_INSTR)
#define hppa_blt(r1, r2, target_addr) \
	hppa_combt(r1, r2, target_addr, 2, NOP_NEXT_INSTR)
#define hppa_ble(r1, r2, target_addr) \
	hppa_combt(r1, r2, target_addr, 3, NOP_NEXT_INSTR)
#define hppa_bltu(r1, r2, target_addr) \
	hppa_combt(r1, r2, target_addr, 4, NOP_NEXT_INSTR)
#define hppa_bleu(r1, r2, target_addr) \
	hppa_combt(r1, r2, target_addr, 5, NOP_NEXT_INSTR)

#define hppa_combf(r1, r2, target_addr, condition, nop) \
	hppa_t11_insn(IS_ENABLED(CONFIG_64BIT) ? 0x2f : 0x22, \
		r2, r1, condition, target_addr, nop)		/* combf,cond,n r1,r2,addr */
#define hppa_bne(r1, r2, target_addr) \
	hppa_combf(r1, r2, target_addr, 1, NOP_NEXT_INSTR)
#define hppa_bge(r1, r2, target_addr) \
	hppa_combf(r1, r2, target_addr, 2, NOP_NEXT_INSTR)
#define hppa_bgt(r1, r2, target_addr) \
	hppa_combf(r1, r2, target_addr, 3, NOP_NEXT_INSTR)
#define hppa_bgeu(r1, r2, target_addr) \
	hppa_combf(r1, r2, target_addr, 4, NOP_NEXT_INSTR)
#define hppa_bgtu(r1, r2, target_addr) \
	hppa_combf(r1, r2, target_addr, 5, NOP_NEXT_INSTR)

/* 64-bit instructions */
#ifdef CONFIG_64BIT
#define hppa64_ldd_reg(reg, b, target) \
	hppa_t10_insn(0x03, b, reg, 0, 0, 3<<1, target)
#define hppa64_ldd_im5(im5, b, target) \
	hppa_t10_insn(0x03, b, low_sign_unext(im5,5), 0, 1<<2, 3<<1, target)
#define hppa64_ldd_im16(im16, b, target) \
	hppa_t10_insn(0x14, b, target, 0, 0, 0, 0) | re_assemble_16(im16)
#define hppa64_std_im5(src, im5, b) \
	hppa_t10_insn(0x03, b, src, 0, 1<<2, 0xB<<1, low_sign_unext(im5,5))
#define hppa64_std_im16(src, im16, b) \
	hppa_t10_insn(0x1c, b, src, 0, 0, 0, 0) | re_assemble_16(im16)
#define hppa64_bl_long(offs22) \
	hppa_t12_L_insn(0x3a, offs22, 1)
#define hppa64_mtsarcm(reg) \
	hppa_t21_insn(0x00, 11, reg, 0xc6, 0)
#define hppa64_shrpd_sar(reg, target) \
	hppa_t10_insn(0x34, reg, 0, 0, 0, 1<<4, target)
#define hppa64_shladd(r1, sa, r2, target) \
	hppa_t6_insn(0x02, r2, r1, 0, 0, 1<<4|1<<3|sa, target)
#define hppa64_depdz_sar(reg, target) \
	hppa_t21_insn(0x35, target, reg, 3<<3, 0)
#define hppa_extrd_sar(reg, target, se) \
	hppa_t10_insn(0x34, reg, target, 0, 0, 0, 0) | 2<<11 | (se&1)<<10 | 1<<9 | 1<<8
#define hppa64_bve_l_rp(base) \
	(0x3a << 26) | (base << 21) | 0xf000
#define hppa64_permh_3210(r, target) \
	(0x3e << 26) | (r << 21) | (r << 16) | (target) | 0x00006900
#define hppa64_hshl(r, sa, target) \
	(0x3e << 26) | (0 << 21) | (r << 16) | (sa << 6) | (target) | 0x00008800
#define hppa64_hshr_u(r, sa, target) \
	(0x3e << 26) | (r << 21) | (0 << 16) | (sa << 6) | (target) | 0x0000c800
#endif

struct hppa_jit_data {
	struct bpf_binary_header *header;
	u8 *image;
	struct hppa_jit_context ctx;
};

static inline void bpf_fill_ill_insns(void *area, unsigned int size)
{
	memset(area, 0, size);
}

static inline void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

/* Emit a 4-byte HPPA instruction. */
static inline void emit(const u32 insn, struct hppa_jit_context *ctx)
{
	if (ctx->insns) {
		ctx->insns[ctx->ninsns] = insn;
	}

	ctx->ninsns++;
}

static inline int epilogue_offset(struct hppa_jit_context *ctx)
{
	int to = ctx->epilogue_offset, from = ctx->ninsns;

	return (to - from);
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


static inline signed long hppa_offset(int insn, int off, struct hppa_jit_context *ctx)
{
	signed long from, to;

	off++; /* BPF branch is from PC+1 */
	from = (insn > 0) ? ctx->offset[insn - 1] : 0;
	to = (insn + off > 0) ? ctx->offset[insn + off - 1] : 0;
	return (to - from);
}

/* does the signed value fits into a given number of bits ? */
static inline int check_bits_int(signed long val, int bits)
{
	return	((val >= 0) && ((val >> bits) == 0)) ||
		 ((val < 0) && (((~((u32)val)) >> (bits-1)) == 0));
}

/* can the signed value be used in relative code ? */
static inline int relative_bits_ok(signed long val, int bits)
{
	return	((val >= 0) && (val < (1UL << (bits-1)))) || /* XXX */
		 ((val < 0) && (((~((unsigned long)val)) >> (bits-1)) == 0)
			    && (val & (1UL << (bits-1))));
}

/* can the signed value be used in relative branches ? */
static inline int relative_branch_ok(signed long val, int bits)
{
	return	((val >= 0) && (val < (1UL << (bits-2)))) || /* XXX */
		 ((val < 0) && (((~((unsigned long)val)) < (1UL << (bits-2))))
			    && (val & (1UL << (bits-1))));
}


#define is_5b_int(val)		check_bits_int(val, 5)

static inline unsigned sign_unext(unsigned x, unsigned len)
{
	unsigned len_ones;

	len_ones = (1 << len) - 1;
	return x & len_ones;
}

static inline unsigned low_sign_unext(unsigned x, unsigned len)
{
	unsigned temp;
	unsigned sign;

	sign = (x >> (len-1)) & 1;
	temp = sign_unext (x, len-1);
	return (temp << 1) | sign;
}

static inline unsigned re_assemble_12(unsigned as12)
{
	return ((  (as12 & 0x800) >> 11)
		| ((as12 & 0x400) >> (10 - 2))
		| ((as12 & 0x3ff) << (1 + 2)));
}

static inline unsigned re_assemble_14(unsigned as14)
{
	return ((  (as14 & 0x1fff) << 1)
		| ((as14 & 0x2000) >> 13));
}

#ifdef CONFIG_64BIT
static inline unsigned re_assemble_16(unsigned as16)
{
	unsigned s, t;

	/* Unusual 16-bit encoding, for wide mode only.  */
	t = (as16 << 1) & 0xffff;
	s = (as16 & 0x8000);
	return (t ^ s ^ (s >> 1)) | (s >> 15);
}
#endif

static inline unsigned re_assemble_17(unsigned as17)
{
	return ((  (as17 & 0x10000) >> 16)
		| ((as17 & 0x0f800) << (16 - 11))
		| ((as17 & 0x00400) >> (10 - 2))
		| ((as17 & 0x003ff) << (1 + 2)));
}

static inline unsigned re_assemble_21(unsigned as21)
{
	return ((  (as21 & 0x100000) >> 20)
		| ((as21 & 0x0ffe00) >> 8)
		| ((as21 & 0x000180) << 7)
		| ((as21 & 0x00007c) << 14)
		| ((as21 & 0x000003) << 12));
}

static inline unsigned re_assemble_22(unsigned as22)
{
	return ((  (as22 & 0x200000) >> 21)
		| ((as22 & 0x1f0000) << (21 - 16))
		| ((as22 & 0x00f800) << (16 - 11))
		| ((as22 & 0x000400) >> (10 - 2))
		| ((as22 & 0x0003ff) << (1 + 2)));
}

/* Various HPPA instruction formats. */
/* see https://parisc.wiki.kernel.org/images-parisc/6/68/Pa11_acd.pdf, appendix C */

static inline u32 hppa_t1_insn(u8 opcode, u8 b, u8 r, s16 im14)
{
	return ((opcode << 26) | (b << 21) | (r << 16) | re_assemble_14(im14));
}

static inline u32 hppa_t5_insn(u8 opcode, u8 tr, u32 val21)
{
	return ((opcode << 26) | (tr << 21) | re_assemble_21(val21));
}

static inline u32 hppa_t6_insn(u8 opcode, u8 r2, u8 r1, u8 c, u8 f, u8 ext6, u16 t)
{
	return ((opcode << 26) | (r2 << 21) | (r1 << 16) | (c << 13) | (f << 12) |
		(ext6 << 6) | t);
}

/* 7. Arithmetic immediate */
static inline u32 hppa_t7_insn(u8 opcode, u8 r, u8 t, u32 im11)
{
	return ((opcode << 26) | (r << 21) | (t << 16) | low_sign_unext(im11, 11));
}

/* 10. Shift instructions */
static inline u32 hppa_t10_insn(u8 opcode, u8 r2, u8 r1, u8 c, u8 ext3, u8 cp, u8 t)
{
	return ((opcode << 26) | (r2 << 21) | (r1 << 16) | (c << 13) |
		(ext3 << 10) | (cp << 5) | t);
}

/* 11. Conditional branch instructions */
static inline u32 hppa_t11_insn(u8 opcode, u8 r2, u8 r1, u8 c, u32 w, u8 nop)
{
	u32 ra = re_assemble_12(w);
	// ra = low_sign_unext(w,11) | (w & (1<<10)
	return ((opcode << 26) | (r2 << 21) | (r1 << 16) | (c << 13) | (nop << 1) | ra);
}

/* 12. Branch instructions */
static inline u32 hppa_t12_insn(u8 opcode, u8 rp, u32 w, u8 ext3, u8 nop)
{
	return ((opcode << 26) | (rp << 21) | (ext3 << 13) | (nop << 1) | re_assemble_17(w));
}

static inline u32 hppa_t12_L_insn(u8 opcode, u32 w, u8 nop)
{
	return ((opcode << 26) | (0x05 << 13) | (nop << 1) | re_assemble_22(w));
}

/* 21. Move to control register */
static inline u32 hppa_t21_insn(u8 opcode, u8 r2, u8 r1, u8 ext8, u8 t)
{
	return ((opcode << 26) | (r2 << 21) | (r1 << 16) | (ext8 << 5) | t);
}

/* Helper functions called by jit code on HPPA32 and HPPA64. */

u64 hppa_div64(u64 div, u64 divisor);
u64 hppa_div64_rem(u64 div, u64 divisor);

/* Helper functions that emit HPPA instructions when possible. */

void bpf_jit_build_prologue(struct hppa_jit_context *ctx);
void bpf_jit_build_epilogue(struct hppa_jit_context *ctx);

int bpf_jit_emit_insn(const struct bpf_insn *insn, struct hppa_jit_context *ctx,
		      bool extra_pass);

#endif /* _BPF_JIT_H */
