// SPDX-License-Identifier: GPL-2.0
/*
 * BPF Jit compiler for s390.
 *
 * Minimum build requirements:
 *
 *  - HAVE_MARCH_Z196_FEATURES: laal, laalg
 *  - HAVE_MARCH_Z10_FEATURES: msfi, cgrj, clgrj
 *  - HAVE_MARCH_Z9_109_FEATURES: alfi, llilf, clfi, oilf, nilf
 *  - PACK_STACK
 *  - 64BIT
 *
 * Copyright IBM Corp. 2012,2015
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "bpf_jit"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/init.h>
#include <linux/bpf.h>
#include <asm/cacheflush.h>
#include <asm/dis.h>
#include <asm/facility.h>
#include <asm/nospec-branch.h>
#include <asm/set_memory.h>
#include "bpf_jit.h"

struct bpf_jit {
	u32 seen;		/* Flags to remember seen eBPF instructions */
	u32 seen_reg[16];	/* Array to remember which registers are used */
	u32 *addrs;		/* Array with relative instruction addresses */
	u8 *prg_buf;		/* Start of program */
	int size;		/* Size of program and literal pool */
	int size_prg;		/* Size of program */
	int prg;		/* Current position in program */
	int lit_start;		/* Start of literal pool */
	int lit;		/* Current position in literal pool */
	int base_ip;		/* Base address for literal pool */
	int ret0_ip;		/* Address of return 0 */
	int exit_ip;		/* Address of exit */
	int r1_thunk_ip;	/* Address of expoline thunk for 'br %r1' */
	int r14_thunk_ip;	/* Address of expoline thunk for 'br %r14' */
	int tail_call_start;	/* Tail call start offset */
	int labels[1];		/* Labels for local jumps */
};

#define BPF_SIZE_MAX	0xffff	/* Max size for program (16 bit branches) */

#define SEEN_MEM	(1 << 0)	/* use mem[] for temporary storage */
#define SEEN_RET0	(1 << 1)	/* ret0_ip points to a valid return 0 */
#define SEEN_LITERAL	(1 << 2)	/* code uses literals */
#define SEEN_FUNC	(1 << 3)	/* calls C functions */
#define SEEN_TAIL_CALL	(1 << 4)	/* code uses tail calls */
#define SEEN_REG_AX	(1 << 5)	/* code uses constant blinding */
#define SEEN_STACK	(SEEN_FUNC | SEEN_MEM)

/*
 * s390 registers
 */
#define REG_W0		(MAX_BPF_JIT_REG + 0)	/* Work register 1 (even) */
#define REG_W1		(MAX_BPF_JIT_REG + 1)	/* Work register 2 (odd) */
#define REG_L		(MAX_BPF_JIT_REG + 2)	/* Literal pool register */
#define REG_15		(MAX_BPF_JIT_REG + 3)	/* Register 15 */
#define REG_0		REG_W0			/* Register 0 */
#define REG_1		REG_W1			/* Register 1 */
#define REG_2		BPF_REG_1		/* Register 2 */
#define REG_14		BPF_REG_0		/* Register 14 */

/*
 * Mapping of BPF registers to s390 registers
 */
static const int reg2hex[] = {
	/* Return code */
	[BPF_REG_0]	= 14,
	/* Function parameters */
	[BPF_REG_1]	= 2,
	[BPF_REG_2]	= 3,
	[BPF_REG_3]	= 4,
	[BPF_REG_4]	= 5,
	[BPF_REG_5]	= 6,
	/* Call saved registers */
	[BPF_REG_6]	= 7,
	[BPF_REG_7]	= 8,
	[BPF_REG_8]	= 9,
	[BPF_REG_9]	= 10,
	/* BPF stack pointer */
	[BPF_REG_FP]	= 13,
	/* Register for blinding */
	[BPF_REG_AX]	= 12,
	/* Work registers for s390x backend */
	[REG_W0]	= 0,
	[REG_W1]	= 1,
	[REG_L]		= 11,
	[REG_15]	= 15,
};

static inline u32 reg(u32 dst_reg, u32 src_reg)
{
	return reg2hex[dst_reg] << 4 | reg2hex[src_reg];
}

static inline u32 reg_high(u32 reg)
{
	return reg2hex[reg] << 4;
}

static inline void reg_set_seen(struct bpf_jit *jit, u32 b1)
{
	u32 r1 = reg2hex[b1];

	if (!jit->seen_reg[r1] && r1 >= 6 && r1 <= 15)
		jit->seen_reg[r1] = 1;
}

#define REG_SET_SEEN(b1)					\
({								\
	reg_set_seen(jit, b1);					\
})

#define REG_SEEN(b1) jit->seen_reg[reg2hex[(b1)]]

/*
 * EMIT macros for code generation
 */

#define _EMIT2(op)						\
({								\
	if (jit->prg_buf)					\
		*(u16 *) (jit->prg_buf + jit->prg) = op;	\
	jit->prg += 2;						\
})

#define EMIT2(op, b1, b2)					\
({								\
	_EMIT2(op | reg(b1, b2));				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define _EMIT4(op)						\
({								\
	if (jit->prg_buf)					\
		*(u32 *) (jit->prg_buf + jit->prg) = op;	\
	jit->prg += 4;						\
})

#define EMIT4(op, b1, b2)					\
({								\
	_EMIT4(op | reg(b1, b2));				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT4_RRF(op, b1, b2, b3)				\
({								\
	_EMIT4(op | reg_high(b3) << 8 | reg(b1, b2));		\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
	REG_SET_SEEN(b3);					\
})

#define _EMIT4_DISP(op, disp)					\
({								\
	unsigned int __disp = (disp) & 0xfff;			\
	_EMIT4(op | __disp);					\
})

#define EMIT4_DISP(op, b1, b2, disp)				\
({								\
	_EMIT4_DISP(op | reg_high(b1) << 16 |			\
		    reg_high(b2) << 8, disp);			\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT4_IMM(op, b1, imm)					\
({								\
	unsigned int __imm = (imm) & 0xffff;			\
	_EMIT4(op | reg_high(b1) << 16 | __imm);		\
	REG_SET_SEEN(b1);					\
})

#define EMIT4_PCREL(op, pcrel)					\
({								\
	long __pcrel = ((pcrel) >> 1) & 0xffff;			\
	_EMIT4(op | __pcrel);					\
})

#define _EMIT6(op1, op2)					\
({								\
	if (jit->prg_buf) {					\
		*(u32 *) (jit->prg_buf + jit->prg) = op1;	\
		*(u16 *) (jit->prg_buf + jit->prg + 4) = op2;	\
	}							\
	jit->prg += 6;						\
})

#define _EMIT6_DISP(op1, op2, disp)				\
({								\
	unsigned int __disp = (disp) & 0xfff;			\
	_EMIT6(op1 | __disp, op2);				\
})

#define _EMIT6_DISP_LH(op1, op2, disp)				\
({								\
	u32 _disp = (u32) disp;					\
	unsigned int __disp_h = _disp & 0xff000;		\
	unsigned int __disp_l = _disp & 0x00fff;		\
	_EMIT6(op1 | __disp_l, op2 | __disp_h >> 4);		\
})

#define EMIT6_DISP_LH(op1, op2, b1, b2, b3, disp)		\
({								\
	_EMIT6_DISP_LH(op1 | reg(b1, b2) << 16 |		\
		       reg_high(b3) << 8, op2, disp);		\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
	REG_SET_SEEN(b3);					\
})

#define EMIT6_PCREL_LABEL(op1, op2, b1, b2, label, mask)	\
({								\
	int rel = (jit->labels[label] - jit->prg) >> 1;		\
	_EMIT6(op1 | reg(b1, b2) << 16 | (rel & 0xffff),	\
	       op2 | mask << 12);				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT6_PCREL_IMM_LABEL(op1, op2, b1, imm, label, mask)	\
({								\
	int rel = (jit->labels[label] - jit->prg) >> 1;		\
	_EMIT6(op1 | (reg_high(b1) | mask) << 16 |		\
		(rel & 0xffff), op2 | (imm & 0xff) << 8);	\
	REG_SET_SEEN(b1);					\
	BUILD_BUG_ON(((unsigned long) imm) > 0xff);		\
})

#define EMIT6_PCREL(op1, op2, b1, b2, i, off, mask)		\
({								\
	/* Branch instruction needs 6 bytes */			\
	int rel = (addrs[i + off + 1] - (addrs[i + 1] - 6)) / 2;\
	_EMIT6(op1 | reg(b1, b2) << 16 | (rel & 0xffff), op2 | mask);	\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT6_PCREL_RILB(op, b, target)				\
({								\
	int rel = (target - jit->prg) / 2;			\
	_EMIT6(op | reg_high(b) << 16 | rel >> 16, rel & 0xffff);	\
	REG_SET_SEEN(b);					\
})

#define EMIT6_PCREL_RIL(op, target)				\
({								\
	int rel = (target - jit->prg) / 2;			\
	_EMIT6(op | rel >> 16, rel & 0xffff);			\
})

#define _EMIT6_IMM(op, imm)					\
({								\
	unsigned int __imm = (imm);				\
	_EMIT6(op | (__imm >> 16), __imm & 0xffff);		\
})

#define EMIT6_IMM(op, b1, imm)					\
({								\
	_EMIT6_IMM(op | reg_high(b1) << 16, imm);		\
	REG_SET_SEEN(b1);					\
})

#define EMIT_CONST_U32(val)					\
({								\
	unsigned int ret;					\
	ret = jit->lit - jit->base_ip;				\
	jit->seen |= SEEN_LITERAL;				\
	if (jit->prg_buf)					\
		*(u32 *) (jit->prg_buf + jit->lit) = (u32) val;	\
	jit->lit += 4;						\
	ret;							\
})

#define EMIT_CONST_U64(val)					\
({								\
	unsigned int ret;					\
	ret = jit->lit - jit->base_ip;				\
	jit->seen |= SEEN_LITERAL;				\
	if (jit->prg_buf)					\
		*(u64 *) (jit->prg_buf + jit->lit) = (u64) val;	\
	jit->lit += 8;						\
	ret;							\
})

#define EMIT_ZERO(b1)						\
({								\
	/* llgfr %dst,%dst (zero extend to 64 bit) */		\
	EMIT4(0xb9160000, b1, b1);				\
	REG_SET_SEEN(b1);					\
})

/*
 * Fill whole space with illegal instructions
 */
static void jit_fill_hole(void *area, unsigned int size)
{
	memset(area, 0, size);
}

/*
 * Save registers from "rs" (register start) to "re" (register end) on stack
 */
static void save_regs(struct bpf_jit *jit, u32 rs, u32 re)
{
	u32 off = STK_OFF_R6 + (rs - 6) * 8;

	if (rs == re)
		/* stg %rs,off(%r15) */
		_EMIT6(0xe300f000 | rs << 20 | off, 0x0024);
	else
		/* stmg %rs,%re,off(%r15) */
		_EMIT6_DISP(0xeb00f000 | rs << 20 | re << 16, 0x0024, off);
}

/*
 * Restore registers from "rs" (register start) to "re" (register end) on stack
 */
static void restore_regs(struct bpf_jit *jit, u32 rs, u32 re, u32 stack_depth)
{
	u32 off = STK_OFF_R6 + (rs - 6) * 8;

	if (jit->seen & SEEN_STACK)
		off += STK_OFF + stack_depth;

	if (rs == re)
		/* lg %rs,off(%r15) */
		_EMIT6(0xe300f000 | rs << 20 | off, 0x0004);
	else
		/* lmg %rs,%re,off(%r15) */
		_EMIT6_DISP(0xeb00f000 | rs << 20 | re << 16, 0x0004, off);
}

/*
 * Return first seen register (from start)
 */
static int get_start(struct bpf_jit *jit, int start)
{
	int i;

	for (i = start; i <= 15; i++) {
		if (jit->seen_reg[i])
			return i;
	}
	return 0;
}

/*
 * Return last seen register (from start) (gap >= 2)
 */
static int get_end(struct bpf_jit *jit, int start)
{
	int i;

	for (i = start; i < 15; i++) {
		if (!jit->seen_reg[i] && !jit->seen_reg[i + 1])
			return i - 1;
	}
	return jit->seen_reg[15] ? 15 : 14;
}

#define REGS_SAVE	1
#define REGS_RESTORE	0
/*
 * Save and restore clobbered registers (6-15) on stack.
 * We save/restore registers in chunks with gap >= 2 registers.
 */
static void save_restore_regs(struct bpf_jit *jit, int op, u32 stack_depth)
{

	int re = 6, rs;

	do {
		rs = get_start(jit, re);
		if (!rs)
			break;
		re = get_end(jit, rs + 1);
		if (op == REGS_SAVE)
			save_regs(jit, rs, re);
		else
			restore_regs(jit, rs, re, stack_depth);
		re++;
	} while (re <= 15);
}

/*
 * Emit function prologue
 *
 * Save registers and create stack frame if necessary.
 * See stack frame layout desription in "bpf_jit.h"!
 */
static void bpf_jit_prologue(struct bpf_jit *jit, u32 stack_depth)
{
	if (jit->seen & SEEN_TAIL_CALL) {
		/* xc STK_OFF_TCCNT(4,%r15),STK_OFF_TCCNT(%r15) */
		_EMIT6(0xd703f000 | STK_OFF_TCCNT, 0xf000 | STK_OFF_TCCNT);
	} else {
		/* j tail_call_start: NOP if no tail calls are used */
		EMIT4_PCREL(0xa7f40000, 6);
		_EMIT2(0);
	}
	/* Tail calls have to skip above initialization */
	jit->tail_call_start = jit->prg;
	/* Save registers */
	save_restore_regs(jit, REGS_SAVE, stack_depth);
	/* Setup literal pool */
	if (jit->seen & SEEN_LITERAL) {
		/* basr %r13,0 */
		EMIT2(0x0d00, REG_L, REG_0);
		jit->base_ip = jit->prg;
	}
	/* Setup stack and backchain */
	if (jit->seen & SEEN_STACK) {
		if (jit->seen & SEEN_FUNC)
			/* lgr %w1,%r15 (backchain) */
			EMIT4(0xb9040000, REG_W1, REG_15);
		/* la %bfp,STK_160_UNUSED(%r15) (BPF frame pointer) */
		EMIT4_DISP(0x41000000, BPF_REG_FP, REG_15, STK_160_UNUSED);
		/* aghi %r15,-STK_OFF */
		EMIT4_IMM(0xa70b0000, REG_15, -(STK_OFF + stack_depth));
		if (jit->seen & SEEN_FUNC)
			/* stg %w1,152(%r15) (backchain) */
			EMIT6_DISP_LH(0xe3000000, 0x0024, REG_W1, REG_0,
				      REG_15, 152);
	}
}

/*
 * Function epilogue
 */
static void bpf_jit_epilogue(struct bpf_jit *jit, u32 stack_depth)
{
	/* Return 0 */
	if (jit->seen & SEEN_RET0) {
		jit->ret0_ip = jit->prg;
		/* lghi %b0,0 */
		EMIT4_IMM(0xa7090000, BPF_REG_0, 0);
	}
	jit->exit_ip = jit->prg;
	/* Load exit code: lgr %r2,%b0 */
	EMIT4(0xb9040000, REG_2, BPF_REG_0);
	/* Restore registers */
	save_restore_regs(jit, REGS_RESTORE, stack_depth);
	if (IS_ENABLED(CC_USING_EXPOLINE) && !nospec_disable) {
		jit->r14_thunk_ip = jit->prg;
		/* Generate __s390_indirect_jump_r14 thunk */
		if (test_facility(35)) {
			/* exrl %r0,.+10 */
			EMIT6_PCREL_RIL(0xc6000000, jit->prg + 10);
		} else {
			/* larl %r1,.+14 */
			EMIT6_PCREL_RILB(0xc0000000, REG_1, jit->prg + 14);
			/* ex 0,0(%r1) */
			EMIT4_DISP(0x44000000, REG_0, REG_1, 0);
		}
		/* j . */
		EMIT4_PCREL(0xa7f40000, 0);
	}
	/* br %r14 */
	_EMIT2(0x07fe);

	if (IS_ENABLED(CC_USING_EXPOLINE) && !nospec_disable &&
	    (jit->seen & SEEN_FUNC)) {
		jit->r1_thunk_ip = jit->prg;
		/* Generate __s390_indirect_jump_r1 thunk */
		if (test_facility(35)) {
			/* exrl %r0,.+10 */
			EMIT6_PCREL_RIL(0xc6000000, jit->prg + 10);
			/* j . */
			EMIT4_PCREL(0xa7f40000, 0);
			/* br %r1 */
			_EMIT2(0x07f1);
		} else {
			/* ex 0,S390_lowcore.br_r1_tampoline */
			EMIT4_DISP(0x44000000, REG_0, REG_0,
				   offsetof(struct lowcore, br_r1_trampoline));
			/* j . */
			EMIT4_PCREL(0xa7f40000, 0);
		}
	}
}

/*
 * Compile one eBPF instruction into s390x code
 *
 * NOTE: Use noinline because for gcov (-fprofile-arcs) gcc allocates a lot of
 * stack space for the large switch statement.
 */
static noinline int bpf_jit_insn(struct bpf_jit *jit, struct bpf_prog *fp, int i)
{
	struct bpf_insn *insn = &fp->insnsi[i];
	int jmp_off, last, insn_count = 1;
	u32 dst_reg = insn->dst_reg;
	u32 src_reg = insn->src_reg;
	u32 *addrs = jit->addrs;
	s32 imm = insn->imm;
	s16 off = insn->off;
	unsigned int mask;

	if (dst_reg == BPF_REG_AX || src_reg == BPF_REG_AX)
		jit->seen |= SEEN_REG_AX;
	switch (insn->code) {
	/*
	 * BPF_MOV
	 */
	case BPF_ALU | BPF_MOV | BPF_X: /* dst = (u32) src */
		/* llgfr %dst,%src */
		EMIT4(0xb9160000, dst_reg, src_reg);
		break;
	case BPF_ALU64 | BPF_MOV | BPF_X: /* dst = src */
		/* lgr %dst,%src */
		EMIT4(0xb9040000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_MOV | BPF_K: /* dst = (u32) imm */
		/* llilf %dst,imm */
		EMIT6_IMM(0xc00f0000, dst_reg, imm);
		break;
	case BPF_ALU64 | BPF_MOV | BPF_K: /* dst = imm */
		/* lgfi %dst,imm */
		EMIT6_IMM(0xc0010000, dst_reg, imm);
		break;
	/*
	 * BPF_LD 64
	 */
	case BPF_LD | BPF_IMM | BPF_DW: /* dst = (u64) imm */
	{
		/* 16 byte instruction that uses two 'struct bpf_insn' */
		u64 imm64;

		imm64 = (u64)(u32) insn[0].imm | ((u64)(u32) insn[1].imm) << 32;
		/* lg %dst,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, dst_reg, REG_0, REG_L,
			      EMIT_CONST_U64(imm64));
		insn_count = 2;
		break;
	}
	/*
	 * BPF_ADD
	 */
	case BPF_ALU | BPF_ADD | BPF_X: /* dst = (u32) dst + (u32) src */
		/* ar %dst,%src */
		EMIT2(0x1a00, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_ADD | BPF_X: /* dst = dst + src */
		/* agr %dst,%src */
		EMIT4(0xb9080000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_ADD | BPF_K: /* dst = (u32) dst + (u32) imm */
		if (!imm)
			break;
		/* alfi %dst,imm */
		EMIT6_IMM(0xc20b0000, dst_reg, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_ADD | BPF_K: /* dst = dst + imm */
		if (!imm)
			break;
		/* agfi %dst,imm */
		EMIT6_IMM(0xc2080000, dst_reg, imm);
		break;
	/*
	 * BPF_SUB
	 */
	case BPF_ALU | BPF_SUB | BPF_X: /* dst = (u32) dst - (u32) src */
		/* sr %dst,%src */
		EMIT2(0x1b00, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_SUB | BPF_X: /* dst = dst - src */
		/* sgr %dst,%src */
		EMIT4(0xb9090000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_SUB | BPF_K: /* dst = (u32) dst - (u32) imm */
		if (!imm)
			break;
		/* alfi %dst,-imm */
		EMIT6_IMM(0xc20b0000, dst_reg, -imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_SUB | BPF_K: /* dst = dst - imm */
		if (!imm)
			break;
		/* agfi %dst,-imm */
		EMIT6_IMM(0xc2080000, dst_reg, -imm);
		break;
	/*
	 * BPF_MUL
	 */
	case BPF_ALU | BPF_MUL | BPF_X: /* dst = (u32) dst * (u32) src */
		/* msr %dst,%src */
		EMIT4(0xb2520000, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_MUL | BPF_X: /* dst = dst * src */
		/* msgr %dst,%src */
		EMIT4(0xb90c0000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_MUL | BPF_K: /* dst = (u32) dst * (u32) imm */
		if (imm == 1)
			break;
		/* msfi %r5,imm */
		EMIT6_IMM(0xc2010000, dst_reg, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_MUL | BPF_K: /* dst = dst * imm */
		if (imm == 1)
			break;
		/* msgfi %dst,imm */
		EMIT6_IMM(0xc2000000, dst_reg, imm);
		break;
	/*
	 * BPF_DIV / BPF_MOD
	 */
	case BPF_ALU | BPF_DIV | BPF_X: /* dst = (u32) dst / (u32) src */
	case BPF_ALU | BPF_MOD | BPF_X: /* dst = (u32) dst % (u32) src */
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		/* lhi %w0,0 */
		EMIT4_IMM(0xa7080000, REG_W0, 0);
		/* lr %w1,%dst */
		EMIT2(0x1800, REG_W1, dst_reg);
		/* dlr %w0,%src */
		EMIT4(0xb9970000, REG_W0, src_reg);
		/* llgfr %dst,%rc */
		EMIT4(0xb9160000, dst_reg, rc_reg);
		break;
	}
	case BPF_ALU64 | BPF_DIV | BPF_X: /* dst = dst / src */
	case BPF_ALU64 | BPF_MOD | BPF_X: /* dst = dst % src */
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		/* lghi %w0,0 */
		EMIT4_IMM(0xa7090000, REG_W0, 0);
		/* lgr %w1,%dst */
		EMIT4(0xb9040000, REG_W1, dst_reg);
		/* dlgr %w0,%dst */
		EMIT4(0xb9870000, REG_W0, src_reg);
		/* lgr %dst,%rc */
		EMIT4(0xb9040000, dst_reg, rc_reg);
		break;
	}
	case BPF_ALU | BPF_DIV | BPF_K: /* dst = (u32) dst / (u32) imm */
	case BPF_ALU | BPF_MOD | BPF_K: /* dst = (u32) dst % (u32) imm */
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		if (imm == 1) {
			if (BPF_OP(insn->code) == BPF_MOD)
				/* lhgi %dst,0 */
				EMIT4_IMM(0xa7090000, dst_reg, 0);
			break;
		}
		/* lhi %w0,0 */
		EMIT4_IMM(0xa7080000, REG_W0, 0);
		/* lr %w1,%dst */
		EMIT2(0x1800, REG_W1, dst_reg);
		/* dl %w0,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0097, REG_W0, REG_0, REG_L,
			      EMIT_CONST_U32(imm));
		/* llgfr %dst,%rc */
		EMIT4(0xb9160000, dst_reg, rc_reg);
		break;
	}
	case BPF_ALU64 | BPF_DIV | BPF_K: /* dst = dst / imm */
	case BPF_ALU64 | BPF_MOD | BPF_K: /* dst = dst % imm */
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		if (imm == 1) {
			if (BPF_OP(insn->code) == BPF_MOD)
				/* lhgi %dst,0 */
				EMIT4_IMM(0xa7090000, dst_reg, 0);
			break;
		}
		/* lghi %w0,0 */
		EMIT4_IMM(0xa7090000, REG_W0, 0);
		/* lgr %w1,%dst */
		EMIT4(0xb9040000, REG_W1, dst_reg);
		/* dlg %w0,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0087, REG_W0, REG_0, REG_L,
			      EMIT_CONST_U64(imm));
		/* lgr %dst,%rc */
		EMIT4(0xb9040000, dst_reg, rc_reg);
		break;
	}
	/*
	 * BPF_AND
	 */
	case BPF_ALU | BPF_AND | BPF_X: /* dst = (u32) dst & (u32) src */
		/* nr %dst,%src */
		EMIT2(0x1400, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_AND | BPF_X: /* dst = dst & src */
		/* ngr %dst,%src */
		EMIT4(0xb9800000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_AND | BPF_K: /* dst = (u32) dst & (u32) imm */
		/* nilf %dst,imm */
		EMIT6_IMM(0xc00b0000, dst_reg, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_AND | BPF_K: /* dst = dst & imm */
		/* ng %dst,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0080, dst_reg, REG_0, REG_L,
			      EMIT_CONST_U64(imm));
		break;
	/*
	 * BPF_OR
	 */
	case BPF_ALU | BPF_OR | BPF_X: /* dst = (u32) dst | (u32) src */
		/* or %dst,%src */
		EMIT2(0x1600, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_OR | BPF_X: /* dst = dst | src */
		/* ogr %dst,%src */
		EMIT4(0xb9810000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_OR | BPF_K: /* dst = (u32) dst | (u32) imm */
		/* oilf %dst,imm */
		EMIT6_IMM(0xc00d0000, dst_reg, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_OR | BPF_K: /* dst = dst | imm */
		/* og %dst,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0081, dst_reg, REG_0, REG_L,
			      EMIT_CONST_U64(imm));
		break;
	/*
	 * BPF_XOR
	 */
	case BPF_ALU | BPF_XOR | BPF_X: /* dst = (u32) dst ^ (u32) src */
		/* xr %dst,%src */
		EMIT2(0x1700, dst_reg, src_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_XOR | BPF_X: /* dst = dst ^ src */
		/* xgr %dst,%src */
		EMIT4(0xb9820000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_XOR | BPF_K: /* dst = (u32) dst ^ (u32) imm */
		if (!imm)
			break;
		/* xilf %dst,imm */
		EMIT6_IMM(0xc0070000, dst_reg, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_XOR | BPF_K: /* dst = dst ^ imm */
		/* xg %dst,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0082, dst_reg, REG_0, REG_L,
			      EMIT_CONST_U64(imm));
		break;
	/*
	 * BPF_LSH
	 */
	case BPF_ALU | BPF_LSH | BPF_X: /* dst = (u32) dst << (u32) src */
		/* sll %dst,0(%src) */
		EMIT4_DISP(0x89000000, dst_reg, src_reg, 0);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_LSH | BPF_X: /* dst = dst << src */
		/* sllg %dst,%dst,0(%src) */
		EMIT6_DISP_LH(0xeb000000, 0x000d, dst_reg, dst_reg, src_reg, 0);
		break;
	case BPF_ALU | BPF_LSH | BPF_K: /* dst = (u32) dst << (u32) imm */
		if (imm == 0)
			break;
		/* sll %dst,imm(%r0) */
		EMIT4_DISP(0x89000000, dst_reg, REG_0, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_LSH | BPF_K: /* dst = dst << imm */
		if (imm == 0)
			break;
		/* sllg %dst,%dst,imm(%r0) */
		EMIT6_DISP_LH(0xeb000000, 0x000d, dst_reg, dst_reg, REG_0, imm);
		break;
	/*
	 * BPF_RSH
	 */
	case BPF_ALU | BPF_RSH | BPF_X: /* dst = (u32) dst >> (u32) src */
		/* srl %dst,0(%src) */
		EMIT4_DISP(0x88000000, dst_reg, src_reg, 0);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_RSH | BPF_X: /* dst = dst >> src */
		/* srlg %dst,%dst,0(%src) */
		EMIT6_DISP_LH(0xeb000000, 0x000c, dst_reg, dst_reg, src_reg, 0);
		break;
	case BPF_ALU | BPF_RSH | BPF_K: /* dst = (u32) dst >> (u32) imm */
		if (imm == 0)
			break;
		/* srl %dst,imm(%r0) */
		EMIT4_DISP(0x88000000, dst_reg, REG_0, imm);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_RSH | BPF_K: /* dst = dst >> imm */
		if (imm == 0)
			break;
		/* srlg %dst,%dst,imm(%r0) */
		EMIT6_DISP_LH(0xeb000000, 0x000c, dst_reg, dst_reg, REG_0, imm);
		break;
	/*
	 * BPF_ARSH
	 */
	case BPF_ALU64 | BPF_ARSH | BPF_X: /* ((s64) dst) >>= src */
		/* srag %dst,%dst,0(%src) */
		EMIT6_DISP_LH(0xeb000000, 0x000a, dst_reg, dst_reg, src_reg, 0);
		break;
	case BPF_ALU64 | BPF_ARSH | BPF_K: /* ((s64) dst) >>= imm */
		if (imm == 0)
			break;
		/* srag %dst,%dst,imm(%r0) */
		EMIT6_DISP_LH(0xeb000000, 0x000a, dst_reg, dst_reg, REG_0, imm);
		break;
	/*
	 * BPF_NEG
	 */
	case BPF_ALU | BPF_NEG: /* dst = (u32) -dst */
		/* lcr %dst,%dst */
		EMIT2(0x1300, dst_reg, dst_reg);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_NEG: /* dst = -dst */
		/* lcgr %dst,%dst */
		EMIT4(0xb9030000, dst_reg, dst_reg);
		break;
	/*
	 * BPF_FROM_BE/LE
	 */
	case BPF_ALU | BPF_END | BPF_FROM_BE:
		/* s390 is big endian, therefore only clear high order bytes */
		switch (imm) {
		case 16: /* dst = (u16) cpu_to_be16(dst) */
			/* llghr %dst,%dst */
			EMIT4(0xb9850000, dst_reg, dst_reg);
			break;
		case 32: /* dst = (u32) cpu_to_be32(dst) */
			/* llgfr %dst,%dst */
			EMIT4(0xb9160000, dst_reg, dst_reg);
			break;
		case 64: /* dst = (u64) cpu_to_be64(dst) */
			break;
		}
		break;
	case BPF_ALU | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16: /* dst = (u16) cpu_to_le16(dst) */
			/* lrvr %dst,%dst */
			EMIT4(0xb91f0000, dst_reg, dst_reg);
			/* srl %dst,16(%r0) */
			EMIT4_DISP(0x88000000, dst_reg, REG_0, 16);
			/* llghr %dst,%dst */
			EMIT4(0xb9850000, dst_reg, dst_reg);
			break;
		case 32: /* dst = (u32) cpu_to_le32(dst) */
			/* lrvr %dst,%dst */
			EMIT4(0xb91f0000, dst_reg, dst_reg);
			/* llgfr %dst,%dst */
			EMIT4(0xb9160000, dst_reg, dst_reg);
			break;
		case 64: /* dst = (u64) cpu_to_le64(dst) */
			/* lrvgr %dst,%dst */
			EMIT4(0xb90f0000, dst_reg, dst_reg);
			break;
		}
		break;
	/*
	 * BPF_ST(X)
	 */
	case BPF_STX | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = src_reg */
		/* stcy %src,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0072, src_reg, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = src */
		/* sthy %src,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0070, src_reg, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = src */
		/* sty %src,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0050, src_reg, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_DW: /* (u64 *)(dst + off) = src */
		/* stg %src,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, src_reg, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = imm */
		/* lhi %w0,imm */
		EMIT4_IMM(0xa7080000, REG_W0, (u8) imm);
		/* stcy %w0,off(dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0072, REG_W0, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = imm */
		/* lhi %w0,imm */
		EMIT4_IMM(0xa7080000, REG_W0, (u16) imm);
		/* sthy %w0,off(dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0070, REG_W0, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = imm */
		/* llilf %w0,imm  */
		EMIT6_IMM(0xc00f0000, REG_W0, (u32) imm);
		/* sty %w0,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0050, REG_W0, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_DW: /* *(u64 *)(dst + off) = imm */
		/* lgfi %w0,imm */
		EMIT6_IMM(0xc0010000, REG_W0, imm);
		/* stg %w0,off(%dst) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, REG_W0, dst_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	/*
	 * BPF_STX XADD (atomic_add)
	 */
	case BPF_STX | BPF_XADD | BPF_W: /* *(u32 *)(dst + off) += src */
		/* laal %w0,%src,off(%dst) */
		EMIT6_DISP_LH(0xeb000000, 0x00fa, REG_W0, src_reg,
			      dst_reg, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_XADD | BPF_DW: /* *(u64 *)(dst + off) += src */
		/* laalg %w0,%src,off(%dst) */
		EMIT6_DISP_LH(0xeb000000, 0x00ea, REG_W0, src_reg,
			      dst_reg, off);
		jit->seen |= SEEN_MEM;
		break;
	/*
	 * BPF_LDX
	 */
	case BPF_LDX | BPF_MEM | BPF_B: /* dst = *(u8 *)(ul) (src + off) */
		/* llgc %dst,0(off,%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0090, dst_reg, src_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_LDX | BPF_MEM | BPF_H: /* dst = *(u16 *)(ul) (src + off) */
		/* llgh %dst,0(off,%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0091, dst_reg, src_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		break;
	case BPF_LDX | BPF_MEM | BPF_W: /* dst = *(u32 *)(ul) (src + off) */
		/* llgf %dst,off(%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0016, dst_reg, src_reg, REG_0, off);
		break;
	case BPF_LDX | BPF_MEM | BPF_DW: /* dst = *(u64 *)(ul) (src + off) */
		/* lg %dst,0(off,%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0004, dst_reg, src_reg, REG_0, off);
		break;
	/*
	 * BPF_JMP / CALL
	 */
	case BPF_JMP | BPF_CALL:
	{
		/*
		 * b0 = (__bpf_call_base + imm)(b1, b2, b3, b4, b5)
		 */
		const u64 func = (u64)__bpf_call_base + imm;

		REG_SET_SEEN(BPF_REG_5);
		jit->seen |= SEEN_FUNC;
		/* lg %w1,<d(imm)>(%l) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_W1, REG_0, REG_L,
			      EMIT_CONST_U64(func));
		if (IS_ENABLED(CC_USING_EXPOLINE) && !nospec_disable) {
			/* brasl %r14,__s390_indirect_jump_r1 */
			EMIT6_PCREL_RILB(0xc0050000, REG_14, jit->r1_thunk_ip);
		} else {
			/* basr %r14,%w1 */
			EMIT2(0x0d00, REG_14, REG_W1);
		}
		/* lgr %b0,%r2: load return value into %b0 */
		EMIT4(0xb9040000, BPF_REG_0, REG_2);
		break;
	}
	case BPF_JMP | BPF_TAIL_CALL:
		/*
		 * Implicit input:
		 *  B1: pointer to ctx
		 *  B2: pointer to bpf_array
		 *  B3: index in bpf_array
		 */
		jit->seen |= SEEN_TAIL_CALL;

		/*
		 * if (index >= array->map.max_entries)
		 *         goto out;
		 */

		/* llgf %w1,map.max_entries(%b2) */
		EMIT6_DISP_LH(0xe3000000, 0x0016, REG_W1, REG_0, BPF_REG_2,
			      offsetof(struct bpf_array, map.max_entries));
		/* clrj %b3,%w1,0xa,label0: if (u32)%b3 >= (u32)%w1 goto out */
		EMIT6_PCREL_LABEL(0xec000000, 0x0077, BPF_REG_3,
				  REG_W1, 0, 0xa);

		/*
		 * if (tail_call_cnt++ > MAX_TAIL_CALL_CNT)
		 *         goto out;
		 */

		if (jit->seen & SEEN_STACK)
			off = STK_OFF_TCCNT + STK_OFF + fp->aux->stack_depth;
		else
			off = STK_OFF_TCCNT;
		/* lhi %w0,1 */
		EMIT4_IMM(0xa7080000, REG_W0, 1);
		/* laal %w1,%w0,off(%r15) */
		EMIT6_DISP_LH(0xeb000000, 0x00fa, REG_W1, REG_W0, REG_15, off);
		/* clij %w1,MAX_TAIL_CALL_CNT,0x2,label0 */
		EMIT6_PCREL_IMM_LABEL(0xec000000, 0x007f, REG_W1,
				      MAX_TAIL_CALL_CNT, 0, 0x2);

		/*
		 * prog = array->ptrs[index];
		 * if (prog == NULL)
		 *         goto out;
		 */

		/* llgfr %r1,%b3: %r1 = (u32) index */
		EMIT4(0xb9160000, REG_1, BPF_REG_3);
		/* sllg %r1,%r1,3: %r1 *= 8 */
		EMIT6_DISP_LH(0xeb000000, 0x000d, REG_1, REG_1, REG_0, 3);
		/* lg %r1,prog(%b2,%r1) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_1, BPF_REG_2,
			      REG_1, offsetof(struct bpf_array, ptrs));
		/* clgij %r1,0,0x8,label0 */
		EMIT6_PCREL_IMM_LABEL(0xec000000, 0x007d, REG_1, 0, 0, 0x8);

		/*
		 * Restore registers before calling function
		 */
		save_restore_regs(jit, REGS_RESTORE, fp->aux->stack_depth);

		/*
		 * goto *(prog->bpf_func + tail_call_start);
		 */

		/* lg %r1,bpf_func(%r1) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_1, REG_1, REG_0,
			      offsetof(struct bpf_prog, bpf_func));
		/* bc 0xf,tail_call_start(%r1) */
		_EMIT4(0x47f01000 + jit->tail_call_start);
		/* out: */
		jit->labels[0] = jit->prg;
		break;
	case BPF_JMP | BPF_EXIT: /* return b0 */
		last = (i == fp->len - 1) ? 1 : 0;
		if (last && !(jit->seen & SEEN_RET0))
			break;
		/* j <exit> */
		EMIT4_PCREL(0xa7f40000, jit->exit_ip - jit->prg);
		break;
	/*
	 * Branch relative (number of skipped instructions) to offset on
	 * condition.
	 *
	 * Condition code to mask mapping:
	 *
	 * CC | Description	   | Mask
	 * ------------------------------
	 * 0  | Operands equal	   |	8
	 * 1  | First operand low  |	4
	 * 2  | First operand high |	2
	 * 3  | Unused		   |	1
	 *
	 * For s390x relative branches: ip = ip + off_bytes
	 * For BPF relative branches:	insn = insn + off_insns + 1
	 *
	 * For example for s390x with offset 0 we jump to the branch
	 * instruction itself (loop) and for BPF with offset 0 we
	 * branch to the instruction behind the branch.
	 */
	case BPF_JMP | BPF_JA: /* if (true) */
		mask = 0xf000; /* j */
		goto branch_oc;
	case BPF_JMP | BPF_JSGT | BPF_K: /* ((s64) dst > (s64) imm) */
		mask = 0x2000; /* jh */
		goto branch_ks;
	case BPF_JMP | BPF_JSLT | BPF_K: /* ((s64) dst < (s64) imm) */
		mask = 0x4000; /* jl */
		goto branch_ks;
	case BPF_JMP | BPF_JSGE | BPF_K: /* ((s64) dst >= (s64) imm) */
		mask = 0xa000; /* jhe */
		goto branch_ks;
	case BPF_JMP | BPF_JSLE | BPF_K: /* ((s64) dst <= (s64) imm) */
		mask = 0xc000; /* jle */
		goto branch_ks;
	case BPF_JMP | BPF_JGT | BPF_K: /* (dst_reg > imm) */
		mask = 0x2000; /* jh */
		goto branch_ku;
	case BPF_JMP | BPF_JLT | BPF_K: /* (dst_reg < imm) */
		mask = 0x4000; /* jl */
		goto branch_ku;
	case BPF_JMP | BPF_JGE | BPF_K: /* (dst_reg >= imm) */
		mask = 0xa000; /* jhe */
		goto branch_ku;
	case BPF_JMP | BPF_JLE | BPF_K: /* (dst_reg <= imm) */
		mask = 0xc000; /* jle */
		goto branch_ku;
	case BPF_JMP | BPF_JNE | BPF_K: /* (dst_reg != imm) */
		mask = 0x7000; /* jne */
		goto branch_ku;
	case BPF_JMP | BPF_JEQ | BPF_K: /* (dst_reg == imm) */
		mask = 0x8000; /* je */
		goto branch_ku;
	case BPF_JMP | BPF_JSET | BPF_K: /* (dst_reg & imm) */
		mask = 0x7000; /* jnz */
		/* lgfi %w1,imm (load sign extend imm) */
		EMIT6_IMM(0xc0010000, REG_W1, imm);
		/* ngr %w1,%dst */
		EMIT4(0xb9800000, REG_W1, dst_reg);
		goto branch_oc;

	case BPF_JMP | BPF_JSGT | BPF_X: /* ((s64) dst > (s64) src) */
		mask = 0x2000; /* jh */
		goto branch_xs;
	case BPF_JMP | BPF_JSLT | BPF_X: /* ((s64) dst < (s64) src) */
		mask = 0x4000; /* jl */
		goto branch_xs;
	case BPF_JMP | BPF_JSGE | BPF_X: /* ((s64) dst >= (s64) src) */
		mask = 0xa000; /* jhe */
		goto branch_xs;
	case BPF_JMP | BPF_JSLE | BPF_X: /* ((s64) dst <= (s64) src) */
		mask = 0xc000; /* jle */
		goto branch_xs;
	case BPF_JMP | BPF_JGT | BPF_X: /* (dst > src) */
		mask = 0x2000; /* jh */
		goto branch_xu;
	case BPF_JMP | BPF_JLT | BPF_X: /* (dst < src) */
		mask = 0x4000; /* jl */
		goto branch_xu;
	case BPF_JMP | BPF_JGE | BPF_X: /* (dst >= src) */
		mask = 0xa000; /* jhe */
		goto branch_xu;
	case BPF_JMP | BPF_JLE | BPF_X: /* (dst <= src) */
		mask = 0xc000; /* jle */
		goto branch_xu;
	case BPF_JMP | BPF_JNE | BPF_X: /* (dst != src) */
		mask = 0x7000; /* jne */
		goto branch_xu;
	case BPF_JMP | BPF_JEQ | BPF_X: /* (dst == src) */
		mask = 0x8000; /* je */
		goto branch_xu;
	case BPF_JMP | BPF_JSET | BPF_X: /* (dst & src) */
		mask = 0x7000; /* jnz */
		/* ngrk %w1,%dst,%src */
		EMIT4_RRF(0xb9e40000, REG_W1, dst_reg, src_reg);
		goto branch_oc;
branch_ks:
		/* lgfi %w1,imm (load sign extend imm) */
		EMIT6_IMM(0xc0010000, REG_W1, imm);
		/* cgrj %dst,%w1,mask,off */
		EMIT6_PCREL(0xec000000, 0x0064, dst_reg, REG_W1, i, off, mask);
		break;
branch_ku:
		/* lgfi %w1,imm (load sign extend imm) */
		EMIT6_IMM(0xc0010000, REG_W1, imm);
		/* clgrj %dst,%w1,mask,off */
		EMIT6_PCREL(0xec000000, 0x0065, dst_reg, REG_W1, i, off, mask);
		break;
branch_xs:
		/* cgrj %dst,%src,mask,off */
		EMIT6_PCREL(0xec000000, 0x0064, dst_reg, src_reg, i, off, mask);
		break;
branch_xu:
		/* clgrj %dst,%src,mask,off */
		EMIT6_PCREL(0xec000000, 0x0065, dst_reg, src_reg, i, off, mask);
		break;
branch_oc:
		/* brc mask,jmp_off (branch instruction needs 4 bytes) */
		jmp_off = addrs[i + off + 1] - (addrs[i + 1] - 4);
		EMIT4_PCREL(0xa7040000 | mask << 8, jmp_off);
		break;
	default: /* too complex, give up */
		pr_err("Unknown opcode %02x\n", insn->code);
		return -1;
	}
	return insn_count;
}

/*
 * Compile eBPF program into s390x code
 */
static int bpf_jit_prog(struct bpf_jit *jit, struct bpf_prog *fp)
{
	int i, insn_count;

	jit->lit = jit->lit_start;
	jit->prg = 0;

	bpf_jit_prologue(jit, fp->aux->stack_depth);
	for (i = 0; i < fp->len; i += insn_count) {
		insn_count = bpf_jit_insn(jit, fp, i);
		if (insn_count < 0)
			return -1;
		/* Next instruction address */
		jit->addrs[i + insn_count] = jit->prg;
	}
	bpf_jit_epilogue(jit, fp->aux->stack_depth);

	jit->lit_start = jit->prg;
	jit->size = jit->lit;
	jit->size_prg = jit->prg;
	return 0;
}

/*
 * Compile eBPF program "fp"
 */
struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *fp)
{
	struct bpf_prog *tmp, *orig_fp = fp;
	struct bpf_binary_header *header;
	bool tmp_blinded = false;
	struct bpf_jit jit;
	int pass;

	if (!fp->jit_requested)
		return orig_fp;

	tmp = bpf_jit_blind_constants(fp);
	/*
	 * If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter.
	 */
	if (IS_ERR(tmp))
		return orig_fp;
	if (tmp != fp) {
		tmp_blinded = true;
		fp = tmp;
	}

	memset(&jit, 0, sizeof(jit));
	jit.addrs = kcalloc(fp->len + 1, sizeof(*jit.addrs), GFP_KERNEL);
	if (jit.addrs == NULL) {
		fp = orig_fp;
		goto out;
	}
	/*
	 * Three initial passes:
	 *   - 1/2: Determine clobbered registers
	 *   - 3:   Calculate program size and addrs arrray
	 */
	for (pass = 1; pass <= 3; pass++) {
		if (bpf_jit_prog(&jit, fp)) {
			fp = orig_fp;
			goto free_addrs;
		}
	}
	/*
	 * Final pass: Allocate and generate program
	 */
	if (jit.size >= BPF_SIZE_MAX) {
		fp = orig_fp;
		goto free_addrs;
	}
	header = bpf_jit_binary_alloc(jit.size, &jit.prg_buf, 2, jit_fill_hole);
	if (!header) {
		fp = orig_fp;
		goto free_addrs;
	}
	if (bpf_jit_prog(&jit, fp)) {
		bpf_jit_binary_free(header);
		fp = orig_fp;
		goto free_addrs;
	}
	if (bpf_jit_enable > 1) {
		bpf_jit_dump(fp->len, jit.size, pass, jit.prg_buf);
		print_fn_code(jit.prg_buf, jit.size_prg);
	}
	bpf_jit_binary_lock_ro(header);
	fp->bpf_func = (void *) jit.prg_buf;
	fp->jited = 1;
	fp->jited_len = jit.size;
free_addrs:
	kfree(jit.addrs);
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(fp, fp == orig_fp ?
					   tmp : orig_fp);
	return fp;
}
