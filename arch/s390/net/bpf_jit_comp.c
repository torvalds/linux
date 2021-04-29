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
#include <linux/mm.h>
#include <linux/kernel.h>
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
	int lit32_start;	/* Start of 32-bit literal pool */
	int lit32;		/* Current position in 32-bit literal pool */
	int lit64_start;	/* Start of 64-bit literal pool */
	int lit64;		/* Current position in 64-bit literal pool */
	int base_ip;		/* Base address for literal pool */
	int exit_ip;		/* Address of exit */
	int r1_thunk_ip;	/* Address of expoline thunk for 'br %r1' */
	int r14_thunk_ip;	/* Address of expoline thunk for 'br %r14' */
	int tail_call_start;	/* Tail call start offset */
	int excnt;		/* Number of exception table entries */
};

#define SEEN_MEM	BIT(0)		/* use mem[] for temporary storage */
#define SEEN_LITERAL	BIT(1)		/* code uses literals */
#define SEEN_FUNC	BIT(2)		/* calls C functions */
#define SEEN_TAIL_CALL	BIT(3)		/* code uses tail calls */
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
		*(u16 *) (jit->prg_buf + jit->prg) = (op);	\
	jit->prg += 2;						\
})

#define EMIT2(op, b1, b2)					\
({								\
	_EMIT2((op) | reg(b1, b2));				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define _EMIT4(op)						\
({								\
	if (jit->prg_buf)					\
		*(u32 *) (jit->prg_buf + jit->prg) = (op);	\
	jit->prg += 4;						\
})

#define EMIT4(op, b1, b2)					\
({								\
	_EMIT4((op) | reg(b1, b2));				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT4_RRF(op, b1, b2, b3)				\
({								\
	_EMIT4((op) | reg_high(b3) << 8 | reg(b1, b2));		\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
	REG_SET_SEEN(b3);					\
})

#define _EMIT4_DISP(op, disp)					\
({								\
	unsigned int __disp = (disp) & 0xfff;			\
	_EMIT4((op) | __disp);					\
})

#define EMIT4_DISP(op, b1, b2, disp)				\
({								\
	_EMIT4_DISP((op) | reg_high(b1) << 16 |			\
		    reg_high(b2) << 8, (disp));			\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT4_IMM(op, b1, imm)					\
({								\
	unsigned int __imm = (imm) & 0xffff;			\
	_EMIT4((op) | reg_high(b1) << 16 | __imm);		\
	REG_SET_SEEN(b1);					\
})

#define EMIT4_PCREL(op, pcrel)					\
({								\
	long __pcrel = ((pcrel) >> 1) & 0xffff;			\
	_EMIT4((op) | __pcrel);					\
})

#define EMIT4_PCREL_RIC(op, mask, target)			\
({								\
	int __rel = ((target) - jit->prg) / 2;			\
	_EMIT4((op) | (mask) << 20 | (__rel & 0xffff));		\
})

#define _EMIT6(op1, op2)					\
({								\
	if (jit->prg_buf) {					\
		*(u32 *) (jit->prg_buf + jit->prg) = (op1);	\
		*(u16 *) (jit->prg_buf + jit->prg + 4) = (op2);	\
	}							\
	jit->prg += 6;						\
})

#define _EMIT6_DISP(op1, op2, disp)				\
({								\
	unsigned int __disp = (disp) & 0xfff;			\
	_EMIT6((op1) | __disp, op2);				\
})

#define _EMIT6_DISP_LH(op1, op2, disp)				\
({								\
	u32 _disp = (u32) (disp);				\
	unsigned int __disp_h = _disp & 0xff000;		\
	unsigned int __disp_l = _disp & 0x00fff;		\
	_EMIT6((op1) | __disp_l, (op2) | __disp_h >> 4);	\
})

#define EMIT6_DISP_LH(op1, op2, b1, b2, b3, disp)		\
({								\
	_EMIT6_DISP_LH((op1) | reg(b1, b2) << 16 |		\
		       reg_high(b3) << 8, op2, disp);		\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
	REG_SET_SEEN(b3);					\
})

#define EMIT6_PCREL_RIEB(op1, op2, b1, b2, mask, target)	\
({								\
	unsigned int rel = (int)((target) - jit->prg) / 2;	\
	_EMIT6((op1) | reg(b1, b2) << 16 | (rel & 0xffff),	\
	       (op2) | (mask) << 12);				\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT6_PCREL_RIEC(op1, op2, b1, imm, mask, target)	\
({								\
	unsigned int rel = (int)((target) - jit->prg) / 2;	\
	_EMIT6((op1) | (reg_high(b1) | (mask)) << 16 |		\
		(rel & 0xffff), (op2) | ((imm) & 0xff) << 8);	\
	REG_SET_SEEN(b1);					\
	BUILD_BUG_ON(((unsigned long) (imm)) > 0xff);		\
})

#define EMIT6_PCREL(op1, op2, b1, b2, i, off, mask)		\
({								\
	/* Branch instruction needs 6 bytes */			\
	int rel = (addrs[(i) + (off) + 1] - (addrs[(i) + 1] - 6)) / 2;\
	_EMIT6((op1) | reg(b1, b2) << 16 | (rel & 0xffff), (op2) | (mask));\
	REG_SET_SEEN(b1);					\
	REG_SET_SEEN(b2);					\
})

#define EMIT6_PCREL_RILB(op, b, target)				\
({								\
	unsigned int rel = (int)((target) - jit->prg) / 2;	\
	_EMIT6((op) | reg_high(b) << 16 | rel >> 16, rel & 0xffff);\
	REG_SET_SEEN(b);					\
})

#define EMIT6_PCREL_RIL(op, target)				\
({								\
	unsigned int rel = (int)((target) - jit->prg) / 2;	\
	_EMIT6((op) | rel >> 16, rel & 0xffff);			\
})

#define EMIT6_PCREL_RILC(op, mask, target)			\
({								\
	EMIT6_PCREL_RIL((op) | (mask) << 20, (target));		\
})

#define _EMIT6_IMM(op, imm)					\
({								\
	unsigned int __imm = (imm);				\
	_EMIT6((op) | (__imm >> 16), __imm & 0xffff);		\
})

#define EMIT6_IMM(op, b1, imm)					\
({								\
	_EMIT6_IMM((op) | reg_high(b1) << 16, imm);		\
	REG_SET_SEEN(b1);					\
})

#define _EMIT_CONST_U32(val)					\
({								\
	unsigned int ret;					\
	ret = jit->lit32;					\
	if (jit->prg_buf)					\
		*(u32 *)(jit->prg_buf + jit->lit32) = (u32)(val);\
	jit->lit32 += 4;					\
	ret;							\
})

#define EMIT_CONST_U32(val)					\
({								\
	jit->seen |= SEEN_LITERAL;				\
	_EMIT_CONST_U32(val) - jit->base_ip;			\
})

#define _EMIT_CONST_U64(val)					\
({								\
	unsigned int ret;					\
	ret = jit->lit64;					\
	if (jit->prg_buf)					\
		*(u64 *)(jit->prg_buf + jit->lit64) = (u64)(val);\
	jit->lit64 += 8;					\
	ret;							\
})

#define EMIT_CONST_U64(val)					\
({								\
	jit->seen |= SEEN_LITERAL;				\
	_EMIT_CONST_U64(val) - jit->base_ip;			\
})

#define EMIT_ZERO(b1)						\
({								\
	if (!fp->aux->verifier_zext) {				\
		/* llgfr %dst,%dst (zero extend to 64 bit) */	\
		EMIT4(0xb9160000, b1, b1);			\
		REG_SET_SEEN(b1);				\
	}							\
})

/*
 * Return whether this is the first pass. The first pass is special, since we
 * don't know any sizes yet, and thus must be conservative.
 */
static bool is_first_pass(struct bpf_jit *jit)
{
	return jit->size == 0;
}

/*
 * Return whether this is the code generation pass. The code generation pass is
 * special, since we should change as little as possible.
 */
static bool is_codegen_pass(struct bpf_jit *jit)
{
	return jit->prg_buf;
}

/*
 * Return whether "rel" can be encoded as a short PC-relative offset
 */
static bool is_valid_rel(int rel)
{
	return rel >= -65536 && rel <= 65534;
}

/*
 * Return whether "off" can be reached using a short PC-relative offset
 */
static bool can_use_rel(struct bpf_jit *jit, int off)
{
	return is_valid_rel(off - jit->prg);
}

/*
 * Return whether given displacement can be encoded using
 * Long-Displacement Facility
 */
static bool is_valid_ldisp(int disp)
{
	return disp >= -524288 && disp <= 524287;
}

/*
 * Return whether the next 32-bit literal pool entry can be referenced using
 * Long-Displacement Facility
 */
static bool can_use_ldisp_for_lit32(struct bpf_jit *jit)
{
	return is_valid_ldisp(jit->lit32 - jit->base_ip);
}

/*
 * Return whether the next 64-bit literal pool entry can be referenced using
 * Long-Displacement Facility
 */
static bool can_use_ldisp_for_lit64(struct bpf_jit *jit)
{
	return is_valid_ldisp(jit->lit64 - jit->base_ip);
}

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
	const int last = 15, save_restore_size = 6;
	int re = 6, rs;

	if (is_first_pass(jit)) {
		/*
		 * We don't know yet which registers are used. Reserve space
		 * conservatively.
		 */
		jit->prg += (last - re + 1) * save_restore_size;
		return;
	}

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
	} while (re <= last);
}

static void bpf_skip(struct bpf_jit *jit, int size)
{
	if (size >= 6 && !is_valid_rel(size)) {
		/* brcl 0xf,size */
		EMIT6_PCREL_RIL(0xc0f4000000, size);
		size -= 6;
	} else if (size >= 4 && is_valid_rel(size)) {
		/* brc 0xf,size */
		EMIT4_PCREL(0xa7f40000, size);
		size -= 4;
	}
	while (size >= 2) {
		/* bcr 0,%0 */
		_EMIT2(0x0700);
		size -= 2;
	}
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
		/*
		 * There are no tail calls. Insert nops in order to have
		 * tail_call_start at a predictable offset.
		 */
		bpf_skip(jit, 6);
	}
	/* Tail calls have to skip above initialization */
	jit->tail_call_start = jit->prg;
	/* Save registers */
	save_restore_regs(jit, REGS_SAVE, stack_depth);
	/* Setup literal pool */
	if (is_first_pass(jit) || (jit->seen & SEEN_LITERAL)) {
		if (!is_first_pass(jit) &&
		    is_valid_ldisp(jit->size - (jit->prg + 2))) {
			/* basr %l,0 */
			EMIT2(0x0d00, REG_L, REG_0);
			jit->base_ip = jit->prg;
		} else {
			/* larl %l,lit32_start */
			EMIT6_PCREL_RILB(0xc0000000, REG_L, jit->lit32_start);
			jit->base_ip = jit->lit32_start;
		}
	}
	/* Setup stack and backchain */
	if (is_first_pass(jit) || (jit->seen & SEEN_STACK)) {
		if (is_first_pass(jit) || (jit->seen & SEEN_FUNC))
			/* lgr %w1,%r15 (backchain) */
			EMIT4(0xb9040000, REG_W1, REG_15);
		/* la %bfp,STK_160_UNUSED(%r15) (BPF frame pointer) */
		EMIT4_DISP(0x41000000, BPF_REG_FP, REG_15, STK_160_UNUSED);
		/* aghi %r15,-STK_OFF */
		EMIT4_IMM(0xa70b0000, REG_15, -(STK_OFF + stack_depth));
		if (is_first_pass(jit) || (jit->seen & SEEN_FUNC))
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
	jit->exit_ip = jit->prg;
	/* Load exit code: lgr %r2,%b0 */
	EMIT4(0xb9040000, REG_2, BPF_REG_0);
	/* Restore registers */
	save_restore_regs(jit, REGS_RESTORE, stack_depth);
	if (__is_defined(CC_USING_EXPOLINE) && !nospec_disable) {
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

	if (__is_defined(CC_USING_EXPOLINE) && !nospec_disable &&
	    (is_first_pass(jit) || (jit->seen & SEEN_FUNC))) {
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

static int get_probe_mem_regno(const u8 *insn)
{
	/*
	 * insn must point to llgc, llgh, llgf or lg, which have destination
	 * register at the same position.
	 */
	if (insn[0] != 0xe3) /* common llgc, llgh, llgf and lg prefix */
		return -1;
	if (insn[5] != 0x90 && /* llgc */
	    insn[5] != 0x91 && /* llgh */
	    insn[5] != 0x16 && /* llgf */
	    insn[5] != 0x04) /* lg */
		return -1;
	return insn[1] >> 4;
}

static bool ex_handler_bpf(const struct exception_table_entry *x,
			   struct pt_regs *regs)
{
	int regno;
	u8 *insn;

	regs->psw.addr = extable_fixup(x);
	insn = (u8 *)__rewind_psw(regs->psw, regs->int_code >> 16);
	regno = get_probe_mem_regno(insn);
	if (WARN_ON_ONCE(regno < 0))
		/* JIT bug - unexpected instruction. */
		return false;
	regs->gprs[regno] = 0;
	return true;
}

static int bpf_jit_probe_mem(struct bpf_jit *jit, struct bpf_prog *fp,
			     int probe_prg, int nop_prg)
{
	struct exception_table_entry *ex;
	s64 delta;
	u8 *insn;
	int prg;
	int i;

	if (!fp->aux->extable)
		/* Do nothing during early JIT passes. */
		return 0;
	insn = jit->prg_buf + probe_prg;
	if (WARN_ON_ONCE(get_probe_mem_regno(insn) < 0))
		/* JIT bug - unexpected probe instruction. */
		return -1;
	if (WARN_ON_ONCE(probe_prg + insn_length(*insn) != nop_prg))
		/* JIT bug - gap between probe and nop instructions. */
		return -1;
	for (i = 0; i < 2; i++) {
		if (WARN_ON_ONCE(jit->excnt >= fp->aux->num_exentries))
			/* Verifier bug - not enough entries. */
			return -1;
		ex = &fp->aux->extable[jit->excnt];
		/* Add extable entries for probe and nop instructions. */
		prg = i == 0 ? probe_prg : nop_prg;
		delta = jit->prg_buf + prg - (u8 *)&ex->insn;
		if (WARN_ON_ONCE(delta < INT_MIN || delta > INT_MAX))
			/* JIT bug - code and extable must be close. */
			return -1;
		ex->insn = delta;
		/*
		 * Always land on the nop. Note that extable infrastructure
		 * ignores fixup field, it is handled by ex_handler_bpf().
		 */
		delta = jit->prg_buf + nop_prg - (u8 *)&ex->fixup;
		if (WARN_ON_ONCE(delta < INT_MIN || delta > INT_MAX))
			/* JIT bug - landing pad and extable must be close. */
			return -1;
		ex->fixup = delta;
		ex->handler = (u8 *)ex_handler_bpf - (u8 *)&ex->handler;
		jit->excnt++;
	}
	return 0;
}

/*
 * Compile one eBPF instruction into s390x code
 *
 * NOTE: Use noinline because for gcov (-fprofile-arcs) gcc allocates a lot of
 * stack space for the large switch statement.
 */
static noinline int bpf_jit_insn(struct bpf_jit *jit, struct bpf_prog *fp,
				 int i, bool extra_pass, u32 stack_depth)
{
	struct bpf_insn *insn = &fp->insnsi[i];
	u32 dst_reg = insn->dst_reg;
	u32 src_reg = insn->src_reg;
	int last, insn_count = 1;
	u32 *addrs = jit->addrs;
	s32 imm = insn->imm;
	s16 off = insn->off;
	int probe_prg = -1;
	unsigned int mask;
	int nop_prg;
	int err;

	if (BPF_CLASS(insn->code) == BPF_LDX &&
	    BPF_MODE(insn->code) == BPF_PROBE_MEM)
		probe_prg = jit->prg;

	switch (insn->code) {
	/*
	 * BPF_MOV
	 */
	case BPF_ALU | BPF_MOV | BPF_X: /* dst = (u32) src */
		/* llgfr %dst,%src */
		EMIT4(0xb9160000, dst_reg, src_reg);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_ALU64 | BPF_MOV | BPF_X: /* dst = src */
		/* lgr %dst,%src */
		EMIT4(0xb9040000, dst_reg, src_reg);
		break;
	case BPF_ALU | BPF_MOV | BPF_K: /* dst = (u32) imm */
		/* llilf %dst,imm */
		EMIT6_IMM(0xc00f0000, dst_reg, imm);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
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
		/* lgrl %dst,imm */
		EMIT6_PCREL_RILB(0xc4080000, dst_reg, _EMIT_CONST_U64(imm64));
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
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
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
		if (!is_first_pass(jit) && can_use_ldisp_for_lit32(jit)) {
			/* dl %w0,<d(imm)>(%l) */
			EMIT6_DISP_LH(0xe3000000, 0x0097, REG_W0, REG_0, REG_L,
				      EMIT_CONST_U32(imm));
		} else {
			/* lgfrl %dst,imm */
			EMIT6_PCREL_RILB(0xc40c0000, dst_reg,
					 _EMIT_CONST_U32(imm));
			jit->seen |= SEEN_LITERAL;
			/* dlr %w0,%dst */
			EMIT4(0xb9970000, REG_W0, dst_reg);
		}
		/* llgfr %dst,%rc */
		EMIT4(0xb9160000, dst_reg, rc_reg);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
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
		if (!is_first_pass(jit) && can_use_ldisp_for_lit64(jit)) {
			/* dlg %w0,<d(imm)>(%l) */
			EMIT6_DISP_LH(0xe3000000, 0x0087, REG_W0, REG_0, REG_L,
				      EMIT_CONST_U64(imm));
		} else {
			/* lgrl %dst,imm */
			EMIT6_PCREL_RILB(0xc4080000, dst_reg,
					 _EMIT_CONST_U64(imm));
			jit->seen |= SEEN_LITERAL;
			/* dlgr %w0,%dst */
			EMIT4(0xb9870000, REG_W0, dst_reg);
		}
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
		if (!is_first_pass(jit) && can_use_ldisp_for_lit64(jit)) {
			/* ng %dst,<d(imm)>(%l) */
			EMIT6_DISP_LH(0xe3000000, 0x0080,
				      dst_reg, REG_0, REG_L,
				      EMIT_CONST_U64(imm));
		} else {
			/* lgrl %w0,imm */
			EMIT6_PCREL_RILB(0xc4080000, REG_W0,
					 _EMIT_CONST_U64(imm));
			jit->seen |= SEEN_LITERAL;
			/* ngr %dst,%w0 */
			EMIT4(0xb9800000, dst_reg, REG_W0);
		}
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
		if (!is_first_pass(jit) && can_use_ldisp_for_lit64(jit)) {
			/* og %dst,<d(imm)>(%l) */
			EMIT6_DISP_LH(0xe3000000, 0x0081,
				      dst_reg, REG_0, REG_L,
				      EMIT_CONST_U64(imm));
		} else {
			/* lgrl %w0,imm */
			EMIT6_PCREL_RILB(0xc4080000, REG_W0,
					 _EMIT_CONST_U64(imm));
			jit->seen |= SEEN_LITERAL;
			/* ogr %dst,%w0 */
			EMIT4(0xb9810000, dst_reg, REG_W0);
		}
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
		if (!is_first_pass(jit) && can_use_ldisp_for_lit64(jit)) {
			/* xg %dst,<d(imm)>(%l) */
			EMIT6_DISP_LH(0xe3000000, 0x0082,
				      dst_reg, REG_0, REG_L,
				      EMIT_CONST_U64(imm));
		} else {
			/* lgrl %w0,imm */
			EMIT6_PCREL_RILB(0xc4080000, REG_W0,
					 _EMIT_CONST_U64(imm));
			jit->seen |= SEEN_LITERAL;
			/* xgr %dst,%w0 */
			EMIT4(0xb9820000, dst_reg, REG_W0);
		}
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
	case BPF_ALU | BPF_ARSH | BPF_X: /* ((s32) dst) >>= src */
		/* sra %dst,%dst,0(%src) */
		EMIT4_DISP(0x8a000000, dst_reg, src_reg, 0);
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_ARSH | BPF_X: /* ((s64) dst) >>= src */
		/* srag %dst,%dst,0(%src) */
		EMIT6_DISP_LH(0xeb000000, 0x000a, dst_reg, dst_reg, src_reg, 0);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K: /* ((s32) dst >> imm */
		if (imm == 0)
			break;
		/* sra %dst,imm(%r0) */
		EMIT4_DISP(0x8a000000, dst_reg, REG_0, imm);
		EMIT_ZERO(dst_reg);
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
			if (insn_is_zext(&insn[1]))
				insn_count = 2;
			break;
		case 32: /* dst = (u32) cpu_to_be32(dst) */
			if (!fp->aux->verifier_zext)
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
			if (insn_is_zext(&insn[1]))
				insn_count = 2;
			break;
		case 32: /* dst = (u32) cpu_to_le32(dst) */
			/* lrvr %dst,%dst */
			EMIT4(0xb91f0000, dst_reg, dst_reg);
			if (!fp->aux->verifier_zext)
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
	 * BPF_ATOMIC
	 */
	case BPF_STX | BPF_ATOMIC | BPF_DW:
	case BPF_STX | BPF_ATOMIC | BPF_W:
	{
		bool is32 = BPF_SIZE(insn->code) == BPF_W;

		switch (insn->imm) {
/* {op32|op64} {%w0|%src},%src,off(%dst) */
#define EMIT_ATOMIC(op32, op64) do {					\
	EMIT6_DISP_LH(0xeb000000, is32 ? (op32) : (op64),		\
		      (insn->imm & BPF_FETCH) ? src_reg : REG_W0,	\
		      src_reg, dst_reg, off);				\
	if (is32 && (insn->imm & BPF_FETCH))				\
		EMIT_ZERO(src_reg);					\
} while (0)
		case BPF_ADD:
		case BPF_ADD | BPF_FETCH:
			/* {laal|laalg} */
			EMIT_ATOMIC(0x00fa, 0x00ea);
			break;
		case BPF_AND:
		case BPF_AND | BPF_FETCH:
			/* {lan|lang} */
			EMIT_ATOMIC(0x00f4, 0x00e4);
			break;
		case BPF_OR:
		case BPF_OR | BPF_FETCH:
			/* {lao|laog} */
			EMIT_ATOMIC(0x00f6, 0x00e6);
			break;
		case BPF_XOR:
		case BPF_XOR | BPF_FETCH:
			/* {lax|laxg} */
			EMIT_ATOMIC(0x00f7, 0x00e7);
			break;
#undef EMIT_ATOMIC
		case BPF_XCHG:
			/* {ly|lg} %w0,off(%dst) */
			EMIT6_DISP_LH(0xe3000000,
				      is32 ? 0x0058 : 0x0004, REG_W0, REG_0,
				      dst_reg, off);
			/* 0: {csy|csg} %w0,%src,off(%dst) */
			EMIT6_DISP_LH(0xeb000000, is32 ? 0x0014 : 0x0030,
				      REG_W0, src_reg, dst_reg, off);
			/* brc 4,0b */
			EMIT4_PCREL_RIC(0xa7040000, 4, jit->prg - 6);
			/* {llgfr|lgr} %src,%w0 */
			EMIT4(is32 ? 0xb9160000 : 0xb9040000, src_reg, REG_W0);
			if (is32 && insn_is_zext(&insn[1]))
				insn_count = 2;
			break;
		case BPF_CMPXCHG:
			/* 0: {csy|csg} %b0,%src,off(%dst) */
			EMIT6_DISP_LH(0xeb000000, is32 ? 0x0014 : 0x0030,
				      BPF_REG_0, src_reg, dst_reg, off);
			break;
		default:
			pr_err("Unknown atomic operation %02x\n", insn->imm);
			return -1;
		}

		jit->seen |= SEEN_MEM;
		break;
	}
	/*
	 * BPF_LDX
	 */
	case BPF_LDX | BPF_MEM | BPF_B: /* dst = *(u8 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_B:
		/* llgc %dst,0(off,%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0090, dst_reg, src_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEM | BPF_H: /* dst = *(u16 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_H:
		/* llgh %dst,0(off,%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0091, dst_reg, src_reg, REG_0, off);
		jit->seen |= SEEN_MEM;
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEM | BPF_W: /* dst = *(u32 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_W:
		/* llgf %dst,off(%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0016, dst_reg, src_reg, REG_0, off);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEM | BPF_DW: /* dst = *(u64 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
		/* lg %dst,0(off,%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0004, dst_reg, src_reg, REG_0, off);
		break;
	/*
	 * BPF_JMP / CALL
	 */
	case BPF_JMP | BPF_CALL:
	{
		u64 func;
		bool func_addr_fixed;
		int ret;

		ret = bpf_jit_get_func_addr(fp, insn, extra_pass,
					    &func, &func_addr_fixed);
		if (ret < 0)
			return -1;

		REG_SET_SEEN(BPF_REG_5);
		jit->seen |= SEEN_FUNC;
		/* lgrl %w1,func */
		EMIT6_PCREL_RILB(0xc4080000, REG_W1, _EMIT_CONST_U64(func));
		if (__is_defined(CC_USING_EXPOLINE) && !nospec_disable) {
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
	case BPF_JMP | BPF_TAIL_CALL: {
		int patch_1_clrj, patch_2_clij, patch_3_brc;

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
		/* if ((u32)%b3 >= (u32)%w1) goto out; */
		/* clrj %b3,%w1,0xa,out */
		patch_1_clrj = jit->prg;
		EMIT6_PCREL_RIEB(0xec000000, 0x0077, BPF_REG_3, REG_W1, 0xa,
				 jit->prg);

		/*
		 * if (tail_call_cnt++ > MAX_TAIL_CALL_CNT)
		 *         goto out;
		 */

		if (jit->seen & SEEN_STACK)
			off = STK_OFF_TCCNT + STK_OFF + stack_depth;
		else
			off = STK_OFF_TCCNT;
		/* lhi %w0,1 */
		EMIT4_IMM(0xa7080000, REG_W0, 1);
		/* laal %w1,%w0,off(%r15) */
		EMIT6_DISP_LH(0xeb000000, 0x00fa, REG_W1, REG_W0, REG_15, off);
		/* clij %w1,MAX_TAIL_CALL_CNT,0x2,out */
		patch_2_clij = jit->prg;
		EMIT6_PCREL_RIEC(0xec000000, 0x007f, REG_W1, MAX_TAIL_CALL_CNT,
				 2, jit->prg);

		/*
		 * prog = array->ptrs[index];
		 * if (prog == NULL)
		 *         goto out;
		 */

		/* llgfr %r1,%b3: %r1 = (u32) index */
		EMIT4(0xb9160000, REG_1, BPF_REG_3);
		/* sllg %r1,%r1,3: %r1 *= 8 */
		EMIT6_DISP_LH(0xeb000000, 0x000d, REG_1, REG_1, REG_0, 3);
		/* ltg %r1,prog(%b2,%r1) */
		EMIT6_DISP_LH(0xe3000000, 0x0002, REG_1, BPF_REG_2,
			      REG_1, offsetof(struct bpf_array, ptrs));
		/* brc 0x8,out */
		patch_3_brc = jit->prg;
		EMIT4_PCREL_RIC(0xa7040000, 8, jit->prg);

		/*
		 * Restore registers before calling function
		 */
		save_restore_regs(jit, REGS_RESTORE, stack_depth);

		/*
		 * goto *(prog->bpf_func + tail_call_start);
		 */

		/* lg %r1,bpf_func(%r1) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_1, REG_1, REG_0,
			      offsetof(struct bpf_prog, bpf_func));
		/* bc 0xf,tail_call_start(%r1) */
		_EMIT4(0x47f01000 + jit->tail_call_start);
		/* out: */
		if (jit->prg_buf) {
			*(u16 *)(jit->prg_buf + patch_1_clrj + 2) =
				(jit->prg - patch_1_clrj) >> 1;
			*(u16 *)(jit->prg_buf + patch_2_clij + 2) =
				(jit->prg - patch_2_clij) >> 1;
			*(u16 *)(jit->prg_buf + patch_3_brc + 2) =
				(jit->prg - patch_3_brc) >> 1;
		}
		break;
	}
	case BPF_JMP | BPF_EXIT: /* return b0 */
		last = (i == fp->len - 1) ? 1 : 0;
		if (last)
			break;
		if (!is_first_pass(jit) && can_use_rel(jit, jit->exit_ip))
			/* brc 0xf, <exit> */
			EMIT4_PCREL_RIC(0xa7040000, 0xf, jit->exit_ip);
		else
			/* brcl 0xf, <exit> */
			EMIT6_PCREL_RILC(0xc0040000, 0xf, jit->exit_ip);
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
	case BPF_JMP32 | BPF_JSGT | BPF_K: /* ((s32) dst > (s32) imm) */
		mask = 0x2000; /* jh */
		goto branch_ks;
	case BPF_JMP | BPF_JSLT | BPF_K: /* ((s64) dst < (s64) imm) */
	case BPF_JMP32 | BPF_JSLT | BPF_K: /* ((s32) dst < (s32) imm) */
		mask = 0x4000; /* jl */
		goto branch_ks;
	case BPF_JMP | BPF_JSGE | BPF_K: /* ((s64) dst >= (s64) imm) */
	case BPF_JMP32 | BPF_JSGE | BPF_K: /* ((s32) dst >= (s32) imm) */
		mask = 0xa000; /* jhe */
		goto branch_ks;
	case BPF_JMP | BPF_JSLE | BPF_K: /* ((s64) dst <= (s64) imm) */
	case BPF_JMP32 | BPF_JSLE | BPF_K: /* ((s32) dst <= (s32) imm) */
		mask = 0xc000; /* jle */
		goto branch_ks;
	case BPF_JMP | BPF_JGT | BPF_K: /* (dst_reg > imm) */
	case BPF_JMP32 | BPF_JGT | BPF_K: /* ((u32) dst_reg > (u32) imm) */
		mask = 0x2000; /* jh */
		goto branch_ku;
	case BPF_JMP | BPF_JLT | BPF_K: /* (dst_reg < imm) */
	case BPF_JMP32 | BPF_JLT | BPF_K: /* ((u32) dst_reg < (u32) imm) */
		mask = 0x4000; /* jl */
		goto branch_ku;
	case BPF_JMP | BPF_JGE | BPF_K: /* (dst_reg >= imm) */
	case BPF_JMP32 | BPF_JGE | BPF_K: /* ((u32) dst_reg >= (u32) imm) */
		mask = 0xa000; /* jhe */
		goto branch_ku;
	case BPF_JMP | BPF_JLE | BPF_K: /* (dst_reg <= imm) */
	case BPF_JMP32 | BPF_JLE | BPF_K: /* ((u32) dst_reg <= (u32) imm) */
		mask = 0xc000; /* jle */
		goto branch_ku;
	case BPF_JMP | BPF_JNE | BPF_K: /* (dst_reg != imm) */
	case BPF_JMP32 | BPF_JNE | BPF_K: /* ((u32) dst_reg != (u32) imm) */
		mask = 0x7000; /* jne */
		goto branch_ku;
	case BPF_JMP | BPF_JEQ | BPF_K: /* (dst_reg == imm) */
	case BPF_JMP32 | BPF_JEQ | BPF_K: /* ((u32) dst_reg == (u32) imm) */
		mask = 0x8000; /* je */
		goto branch_ku;
	case BPF_JMP | BPF_JSET | BPF_K: /* (dst_reg & imm) */
	case BPF_JMP32 | BPF_JSET | BPF_K: /* ((u32) dst_reg & (u32) imm) */
		mask = 0x7000; /* jnz */
		if (BPF_CLASS(insn->code) == BPF_JMP32) {
			/* llilf %w1,imm (load zero extend imm) */
			EMIT6_IMM(0xc00f0000, REG_W1, imm);
			/* nr %w1,%dst */
			EMIT2(0x1400, REG_W1, dst_reg);
		} else {
			/* lgfi %w1,imm (load sign extend imm) */
			EMIT6_IMM(0xc0010000, REG_W1, imm);
			/* ngr %w1,%dst */
			EMIT4(0xb9800000, REG_W1, dst_reg);
		}
		goto branch_oc;

	case BPF_JMP | BPF_JSGT | BPF_X: /* ((s64) dst > (s64) src) */
	case BPF_JMP32 | BPF_JSGT | BPF_X: /* ((s32) dst > (s32) src) */
		mask = 0x2000; /* jh */
		goto branch_xs;
	case BPF_JMP | BPF_JSLT | BPF_X: /* ((s64) dst < (s64) src) */
	case BPF_JMP32 | BPF_JSLT | BPF_X: /* ((s32) dst < (s32) src) */
		mask = 0x4000; /* jl */
		goto branch_xs;
	case BPF_JMP | BPF_JSGE | BPF_X: /* ((s64) dst >= (s64) src) */
	case BPF_JMP32 | BPF_JSGE | BPF_X: /* ((s32) dst >= (s32) src) */
		mask = 0xa000; /* jhe */
		goto branch_xs;
	case BPF_JMP | BPF_JSLE | BPF_X: /* ((s64) dst <= (s64) src) */
	case BPF_JMP32 | BPF_JSLE | BPF_X: /* ((s32) dst <= (s32) src) */
		mask = 0xc000; /* jle */
		goto branch_xs;
	case BPF_JMP | BPF_JGT | BPF_X: /* (dst > src) */
	case BPF_JMP32 | BPF_JGT | BPF_X: /* ((u32) dst > (u32) src) */
		mask = 0x2000; /* jh */
		goto branch_xu;
	case BPF_JMP | BPF_JLT | BPF_X: /* (dst < src) */
	case BPF_JMP32 | BPF_JLT | BPF_X: /* ((u32) dst < (u32) src) */
		mask = 0x4000; /* jl */
		goto branch_xu;
	case BPF_JMP | BPF_JGE | BPF_X: /* (dst >= src) */
	case BPF_JMP32 | BPF_JGE | BPF_X: /* ((u32) dst >= (u32) src) */
		mask = 0xa000; /* jhe */
		goto branch_xu;
	case BPF_JMP | BPF_JLE | BPF_X: /* (dst <= src) */
	case BPF_JMP32 | BPF_JLE | BPF_X: /* ((u32) dst <= (u32) src) */
		mask = 0xc000; /* jle */
		goto branch_xu;
	case BPF_JMP | BPF_JNE | BPF_X: /* (dst != src) */
	case BPF_JMP32 | BPF_JNE | BPF_X: /* ((u32) dst != (u32) src) */
		mask = 0x7000; /* jne */
		goto branch_xu;
	case BPF_JMP | BPF_JEQ | BPF_X: /* (dst == src) */
	case BPF_JMP32 | BPF_JEQ | BPF_X: /* ((u32) dst == (u32) src) */
		mask = 0x8000; /* je */
		goto branch_xu;
	case BPF_JMP | BPF_JSET | BPF_X: /* (dst & src) */
	case BPF_JMP32 | BPF_JSET | BPF_X: /* ((u32) dst & (u32) src) */
	{
		bool is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;

		mask = 0x7000; /* jnz */
		/* nrk or ngrk %w1,%dst,%src */
		EMIT4_RRF((is_jmp32 ? 0xb9f40000 : 0xb9e40000),
			  REG_W1, dst_reg, src_reg);
		goto branch_oc;
branch_ks:
		is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;
		/* cfi or cgfi %dst,imm */
		EMIT6_IMM(is_jmp32 ? 0xc20d0000 : 0xc20c0000,
			  dst_reg, imm);
		if (!is_first_pass(jit) &&
		    can_use_rel(jit, addrs[i + off + 1])) {
			/* brc mask,off */
			EMIT4_PCREL_RIC(0xa7040000,
					mask >> 12, addrs[i + off + 1]);
		} else {
			/* brcl mask,off */
			EMIT6_PCREL_RILC(0xc0040000,
					 mask >> 12, addrs[i + off + 1]);
		}
		break;
branch_ku:
		/* lgfi %w1,imm (load sign extend imm) */
		src_reg = REG_1;
		EMIT6_IMM(0xc0010000, src_reg, imm);
		goto branch_xu;
branch_xs:
		is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;
		if (!is_first_pass(jit) &&
		    can_use_rel(jit, addrs[i + off + 1])) {
			/* crj or cgrj %dst,%src,mask,off */
			EMIT6_PCREL(0xec000000, (is_jmp32 ? 0x0076 : 0x0064),
				    dst_reg, src_reg, i, off, mask);
		} else {
			/* cr or cgr %dst,%src */
			if (is_jmp32)
				EMIT2(0x1900, dst_reg, src_reg);
			else
				EMIT4(0xb9200000, dst_reg, src_reg);
			/* brcl mask,off */
			EMIT6_PCREL_RILC(0xc0040000,
					 mask >> 12, addrs[i + off + 1]);
		}
		break;
branch_xu:
		is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;
		if (!is_first_pass(jit) &&
		    can_use_rel(jit, addrs[i + off + 1])) {
			/* clrj or clgrj %dst,%src,mask,off */
			EMIT6_PCREL(0xec000000, (is_jmp32 ? 0x0077 : 0x0065),
				    dst_reg, src_reg, i, off, mask);
		} else {
			/* clr or clgr %dst,%src */
			if (is_jmp32)
				EMIT2(0x1500, dst_reg, src_reg);
			else
				EMIT4(0xb9210000, dst_reg, src_reg);
			/* brcl mask,off */
			EMIT6_PCREL_RILC(0xc0040000,
					 mask >> 12, addrs[i + off + 1]);
		}
		break;
branch_oc:
		if (!is_first_pass(jit) &&
		    can_use_rel(jit, addrs[i + off + 1])) {
			/* brc mask,off */
			EMIT4_PCREL_RIC(0xa7040000,
					mask >> 12, addrs[i + off + 1]);
		} else {
			/* brcl mask,off */
			EMIT6_PCREL_RILC(0xc0040000,
					 mask >> 12, addrs[i + off + 1]);
		}
		break;
	}
	default: /* too complex, give up */
		pr_err("Unknown opcode %02x\n", insn->code);
		return -1;
	}

	if (probe_prg != -1) {
		/*
		 * Handlers of certain exceptions leave psw.addr pointing to
		 * the instruction directly after the failing one. Therefore,
		 * create two exception table entries and also add a nop in
		 * case two probing instructions come directly after each
		 * other.
		 */
		nop_prg = jit->prg;
		/* bcr 0,%0 */
		_EMIT2(0x0700);
		err = bpf_jit_probe_mem(jit, fp, probe_prg, nop_prg);
		if (err < 0)
			return err;
	}

	return insn_count;
}

/*
 * Return whether new i-th instruction address does not violate any invariant
 */
static bool bpf_is_new_addr_sane(struct bpf_jit *jit, int i)
{
	/* On the first pass anything goes */
	if (is_first_pass(jit))
		return true;

	/* The codegen pass must not change anything */
	if (is_codegen_pass(jit))
		return jit->addrs[i] == jit->prg;

	/* Passes in between must not increase code size */
	return jit->addrs[i] >= jit->prg;
}

/*
 * Update the address of i-th instruction
 */
static int bpf_set_addr(struct bpf_jit *jit, int i)
{
	int delta;

	if (is_codegen_pass(jit)) {
		delta = jit->prg - jit->addrs[i];
		if (delta < 0)
			bpf_skip(jit, -delta);
	}
	if (WARN_ON_ONCE(!bpf_is_new_addr_sane(jit, i)))
		return -1;
	jit->addrs[i] = jit->prg;
	return 0;
}

/*
 * Compile eBPF program into s390x code
 */
static int bpf_jit_prog(struct bpf_jit *jit, struct bpf_prog *fp,
			bool extra_pass, u32 stack_depth)
{
	int i, insn_count, lit32_size, lit64_size;

	jit->lit32 = jit->lit32_start;
	jit->lit64 = jit->lit64_start;
	jit->prg = 0;
	jit->excnt = 0;

	bpf_jit_prologue(jit, stack_depth);
	if (bpf_set_addr(jit, 0) < 0)
		return -1;
	for (i = 0; i < fp->len; i += insn_count) {
		insn_count = bpf_jit_insn(jit, fp, i, extra_pass, stack_depth);
		if (insn_count < 0)
			return -1;
		/* Next instruction address */
		if (bpf_set_addr(jit, i + insn_count) < 0)
			return -1;
	}
	bpf_jit_epilogue(jit, stack_depth);

	lit32_size = jit->lit32 - jit->lit32_start;
	lit64_size = jit->lit64 - jit->lit64_start;
	jit->lit32_start = jit->prg;
	if (lit32_size)
		jit->lit32_start = ALIGN(jit->lit32_start, 4);
	jit->lit64_start = jit->lit32_start + lit32_size;
	if (lit64_size)
		jit->lit64_start = ALIGN(jit->lit64_start, 8);
	jit->size = jit->lit64_start + lit64_size;
	jit->size_prg = jit->prg;

	if (WARN_ON_ONCE(fp->aux->extable &&
			 jit->excnt != fp->aux->num_exentries))
		/* Verifier bug - too many entries. */
		return -1;

	return 0;
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct s390_jit_data {
	struct bpf_binary_header *header;
	struct bpf_jit ctx;
	int pass;
};

static struct bpf_binary_header *bpf_jit_alloc(struct bpf_jit *jit,
					       struct bpf_prog *fp)
{
	struct bpf_binary_header *header;
	u32 extable_size;
	u32 code_size;

	/* We need two entries per insn. */
	fp->aux->num_exentries *= 2;

	code_size = roundup(jit->size,
			    __alignof__(struct exception_table_entry));
	extable_size = fp->aux->num_exentries *
		sizeof(struct exception_table_entry);
	header = bpf_jit_binary_alloc(code_size + extable_size, &jit->prg_buf,
				      8, jit_fill_hole);
	if (!header)
		return NULL;
	fp->aux->extable = (struct exception_table_entry *)
		(jit->prg_buf + code_size);
	return header;
}

/*
 * Compile eBPF program "fp"
 */
struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *fp)
{
	u32 stack_depth = round_up(fp->aux->stack_depth, 8);
	struct bpf_prog *tmp, *orig_fp = fp;
	struct bpf_binary_header *header;
	struct s390_jit_data *jit_data;
	bool tmp_blinded = false;
	bool extra_pass = false;
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

	jit_data = fp->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			fp = orig_fp;
			goto out;
		}
		fp->aux->jit_data = jit_data;
	}
	if (jit_data->ctx.addrs) {
		jit = jit_data->ctx;
		header = jit_data->header;
		extra_pass = true;
		pass = jit_data->pass + 1;
		goto skip_init_ctx;
	}

	memset(&jit, 0, sizeof(jit));
	jit.addrs = kvcalloc(fp->len + 1, sizeof(*jit.addrs), GFP_KERNEL);
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
		if (bpf_jit_prog(&jit, fp, extra_pass, stack_depth)) {
			fp = orig_fp;
			goto free_addrs;
		}
	}
	/*
	 * Final pass: Allocate and generate program
	 */
	header = bpf_jit_alloc(&jit, fp);
	if (!header) {
		fp = orig_fp;
		goto free_addrs;
	}
skip_init_ctx:
	if (bpf_jit_prog(&jit, fp, extra_pass, stack_depth)) {
		bpf_jit_binary_free(header);
		fp = orig_fp;
		goto free_addrs;
	}
	if (bpf_jit_enable > 1) {
		bpf_jit_dump(fp->len, jit.size, pass, jit.prg_buf);
		print_fn_code(jit.prg_buf, jit.size_prg);
	}
	if (!fp->is_func || extra_pass) {
		bpf_jit_binary_lock_ro(header);
	} else {
		jit_data->header = header;
		jit_data->ctx = jit;
		jit_data->pass = pass;
	}
	fp->bpf_func = (void *) jit.prg_buf;
	fp->jited = 1;
	fp->jited_len = jit.size;

	if (!fp->is_func || extra_pass) {
		bpf_prog_fill_jited_linfo(fp, jit.addrs + 1);
free_addrs:
		kvfree(jit.addrs);
		kfree(jit_data);
		fp->aux->jit_data = NULL;
	}
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(fp, fp == orig_fp ?
					   tmp : orig_fp);
	return fp;
}
