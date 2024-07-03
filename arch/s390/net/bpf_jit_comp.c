// SPDX-License-Identifier: GPL-2.0
/*
 * BPF Jit compiler for s390.
 *
 * Minimum build requirements:
 *
 *  - HAVE_MARCH_Z196_FEATURES: laal, laalg
 *  - HAVE_MARCH_Z10_FEATURES: msfi, cgrj, clgrj
 *  - HAVE_MARCH_Z9_109_FEATURES: alfi, llilf, clfi, oilf, nilf
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
#include <asm/extable.h>
#include <asm/dis.h>
#include <asm/facility.h>
#include <asm/nospec-branch.h>
#include <asm/set_memory.h>
#include <asm/text-patching.h>
#include <asm/unwind.h>
#include "bpf_jit.h"

struct bpf_jit {
	u32 seen;		/* Flags to remember seen eBPF instructions */
	u16 seen_regs;		/* Mask to remember which registers are used */
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
	int prologue_plt_ret;	/* Return address for prologue hotpatch PLT */
	int prologue_plt;	/* Start of prologue hotpatch PLT */
	int kern_arena;		/* Pool offset of kernel arena address */
	u64 user_arena;		/* User arena address */
};

#define SEEN_MEM	BIT(0)		/* use mem[] for temporary storage */
#define SEEN_LITERAL	BIT(1)		/* code uses literals */
#define SEEN_FUNC	BIT(2)		/* calls C functions */
#define SEEN_STACK	(SEEN_FUNC | SEEN_MEM)

#define NVREGS		0xffc0		/* %r6-%r15 */

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
#define REG_3		BPF_REG_2		/* Register 3 */
#define REG_4		BPF_REG_3		/* Register 4 */
#define REG_7		BPF_REG_6		/* Register 7 */
#define REG_8		BPF_REG_7		/* Register 8 */
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

	if (r1 >= 6 && r1 <= 15)
		jit->seen_regs |= (1 << r1);
}

#define REG_SET_SEEN(b1)					\
({								\
	reg_set_seen(jit, b1);					\
})

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
	int rel = (addrs[(i) + (off) + 1] - jit->prg) / 2;	\
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
static int get_start(u16 seen_regs, int start)
{
	int i;

	for (i = start; i <= 15; i++) {
		if (seen_regs & (1 << i))
			return i;
	}
	return 0;
}

/*
 * Return last seen register (from start) (gap >= 2)
 */
static int get_end(u16 seen_regs, int start)
{
	int i;

	for (i = start; i < 15; i++) {
		if (!(seen_regs & (3 << i)))
			return i - 1;
	}
	return (seen_regs & (1 << 15)) ? 15 : 14;
}

#define REGS_SAVE	1
#define REGS_RESTORE	0
/*
 * Save and restore clobbered registers (6-15) on stack.
 * We save/restore registers in chunks with gap >= 2 registers.
 */
static void save_restore_regs(struct bpf_jit *jit, int op, u32 stack_depth,
			      u16 extra_regs)
{
	u16 seen_regs = jit->seen_regs | extra_regs;
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
		rs = get_start(seen_regs, re);
		if (!rs)
			break;
		re = get_end(seen_regs, rs + 1);
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
 * PLT for hotpatchable calls. The calling convention is the same as for the
 * ftrace hotpatch trampolines: %r0 is return address, %r1 is clobbered.
 */
struct bpf_plt {
	char code[16];
	void *ret;
	void *target;
} __packed;
extern const struct bpf_plt bpf_plt;
asm(
	".pushsection .rodata\n"
	"	.balign 8\n"
	"bpf_plt:\n"
	"	lgrl %r0,bpf_plt_ret\n"
	"	lgrl %r1,bpf_plt_target\n"
	"	br %r1\n"
	"	.balign 8\n"
	"bpf_plt_ret: .quad 0\n"
	"bpf_plt_target: .quad 0\n"
	"	.popsection\n"
);

static void bpf_jit_plt(struct bpf_plt *plt, void *ret, void *target)
{
	memcpy(plt, &bpf_plt, sizeof(*plt));
	plt->ret = ret;
	plt->target = target;
}

/*
 * Emit function prologue
 *
 * Save registers and create stack frame if necessary.
 * See stack frame layout description in "bpf_jit.h"!
 */
static void bpf_jit_prologue(struct bpf_jit *jit, struct bpf_prog *fp,
			     u32 stack_depth)
{
	/* No-op for hotpatching */
	/* brcl 0,prologue_plt */
	EMIT6_PCREL_RILC(0xc0040000, 0, jit->prologue_plt);
	jit->prologue_plt_ret = jit->prg;

	if (!bpf_is_subprog(fp)) {
		/* Initialize the tail call counter in the main program. */
		/* xc STK_OFF_TCCNT(4,%r15),STK_OFF_TCCNT(%r15) */
		_EMIT6(0xd703f000 | STK_OFF_TCCNT, 0xf000 | STK_OFF_TCCNT);
	} else {
		/*
		 * Skip the tail call counter initialization in subprograms.
		 * Insert nops in order to have tail_call_start at a
		 * predictable offset.
		 */
		bpf_skip(jit, 6);
	}
	/* Tail calls have to skip above initialization */
	jit->tail_call_start = jit->prg;
	if (fp->aux->exception_cb) {
		/*
		 * Switch stack, the new address is in the 2nd parameter.
		 *
		 * Arrange the restoration of %r6-%r15 in the epilogue.
		 * Do not restore them now, the prog does not need them.
		 */
		/* lgr %r15,%r3 */
		EMIT4(0xb9040000, REG_15, REG_3);
		jit->seen_regs |= NVREGS;
	} else {
		/* Save registers */
		save_restore_regs(jit, REGS_SAVE, stack_depth,
				  fp->aux->exception_boundary ? NVREGS : 0);
	}
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
 * Emit an expoline for a jump that follows
 */
static void emit_expoline(struct bpf_jit *jit)
{
	/* exrl %r0,.+10 */
	EMIT6_PCREL_RIL(0xc6000000, jit->prg + 10);
	/* j . */
	EMIT4_PCREL(0xa7f40000, 0);
}

/*
 * Emit __s390_indirect_jump_r1 thunk if necessary
 */
static void emit_r1_thunk(struct bpf_jit *jit)
{
	if (nospec_uses_trampoline()) {
		jit->r1_thunk_ip = jit->prg;
		emit_expoline(jit);
		/* br %r1 */
		_EMIT2(0x07f1);
	}
}

/*
 * Call r1 either directly or via __s390_indirect_jump_r1 thunk
 */
static void call_r1(struct bpf_jit *jit)
{
	if (nospec_uses_trampoline())
		/* brasl %r14,__s390_indirect_jump_r1 */
		EMIT6_PCREL_RILB(0xc0050000, REG_14, jit->r1_thunk_ip);
	else
		/* basr %r14,%r1 */
		EMIT2(0x0d00, REG_14, REG_1);
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
	save_restore_regs(jit, REGS_RESTORE, stack_depth, 0);
	if (nospec_uses_trampoline()) {
		jit->r14_thunk_ip = jit->prg;
		/* Generate __s390_indirect_jump_r14 thunk */
		emit_expoline(jit);
	}
	/* br %r14 */
	_EMIT2(0x07fe);

	if (is_first_pass(jit) || (jit->seen & SEEN_FUNC))
		emit_r1_thunk(jit);

	jit->prg = ALIGN(jit->prg, 8);
	jit->prologue_plt = jit->prg;
	if (jit->prg_buf)
		bpf_jit_plt((struct bpf_plt *)(jit->prg_buf + jit->prg),
			    jit->prg_buf + jit->prologue_plt_ret, NULL);
	jit->prg += sizeof(struct bpf_plt);
}

bool ex_handler_bpf(const struct exception_table_entry *x, struct pt_regs *regs)
{
	regs->psw.addr = extable_fixup(x);
	if (x->data != -1)
		regs->gprs[x->data] = 0;
	return true;
}

/*
 * A single BPF probe instruction
 */
struct bpf_jit_probe {
	int prg;	/* JITed instruction offset */
	int nop_prg;	/* JITed nop offset */
	int reg;	/* Register to clear on exception */
	int arena_reg;	/* Register to use for arena addressing */
};

static void bpf_jit_probe_init(struct bpf_jit_probe *probe)
{
	probe->prg = -1;
	probe->nop_prg = -1;
	probe->reg = -1;
	probe->arena_reg = REG_0;
}

/*
 * Handlers of certain exceptions leave psw.addr pointing to the instruction
 * directly after the failing one. Therefore, create two exception table
 * entries and also add a nop in case two probing instructions come directly
 * after each other.
 */
static void bpf_jit_probe_emit_nop(struct bpf_jit *jit,
				   struct bpf_jit_probe *probe)
{
	if (probe->prg == -1 || probe->nop_prg != -1)
		/* The probe is not armed or nop is already emitted. */
		return;

	probe->nop_prg = jit->prg;
	/* bcr 0,%0 */
	_EMIT2(0x0700);
}

static void bpf_jit_probe_load_pre(struct bpf_jit *jit, struct bpf_insn *insn,
				   struct bpf_jit_probe *probe)
{
	if (BPF_MODE(insn->code) != BPF_PROBE_MEM &&
	    BPF_MODE(insn->code) != BPF_PROBE_MEMSX &&
	    BPF_MODE(insn->code) != BPF_PROBE_MEM32)
		return;

	if (BPF_MODE(insn->code) == BPF_PROBE_MEM32) {
		/* lgrl %r1,kern_arena */
		EMIT6_PCREL_RILB(0xc4080000, REG_W1, jit->kern_arena);
		probe->arena_reg = REG_W1;
	}
	probe->prg = jit->prg;
	probe->reg = reg2hex[insn->dst_reg];
}

static void bpf_jit_probe_store_pre(struct bpf_jit *jit, struct bpf_insn *insn,
				    struct bpf_jit_probe *probe)
{
	if (BPF_MODE(insn->code) != BPF_PROBE_MEM32)
		return;

	/* lgrl %r1,kern_arena */
	EMIT6_PCREL_RILB(0xc4080000, REG_W1, jit->kern_arena);
	probe->arena_reg = REG_W1;
	probe->prg = jit->prg;
}

static void bpf_jit_probe_atomic_pre(struct bpf_jit *jit,
				     struct bpf_insn *insn,
				     struct bpf_jit_probe *probe)
{
	if (BPF_MODE(insn->code) != BPF_PROBE_ATOMIC)
		return;

	/* lgrl %r1,kern_arena */
	EMIT6_PCREL_RILB(0xc4080000, REG_W1, jit->kern_arena);
	/* agr %r1,%dst */
	EMIT4(0xb9080000, REG_W1, insn->dst_reg);
	probe->arena_reg = REG_W1;
	probe->prg = jit->prg;
}

static int bpf_jit_probe_post(struct bpf_jit *jit, struct bpf_prog *fp,
			      struct bpf_jit_probe *probe)
{
	struct exception_table_entry *ex;
	int i, prg;
	s64 delta;
	u8 *insn;

	if (probe->prg == -1)
		/* The probe is not armed. */
		return 0;
	bpf_jit_probe_emit_nop(jit, probe);
	if (!fp->aux->extable)
		/* Do nothing during early JIT passes. */
		return 0;
	insn = jit->prg_buf + probe->prg;
	if (WARN_ON_ONCE(probe->prg + insn_length(*insn) != probe->nop_prg))
		/* JIT bug - gap between probe and nop instructions. */
		return -1;
	for (i = 0; i < 2; i++) {
		if (WARN_ON_ONCE(jit->excnt >= fp->aux->num_exentries))
			/* Verifier bug - not enough entries. */
			return -1;
		ex = &fp->aux->extable[jit->excnt];
		/* Add extable entries for probe and nop instructions. */
		prg = i == 0 ? probe->prg : probe->nop_prg;
		delta = jit->prg_buf + prg - (u8 *)&ex->insn;
		if (WARN_ON_ONCE(delta < INT_MIN || delta > INT_MAX))
			/* JIT bug - code and extable must be close. */
			return -1;
		ex->insn = delta;
		/*
		 * Land on the current instruction. Note that the extable
		 * infrastructure ignores the fixup field; it is handled by
		 * ex_handler_bpf().
		 */
		delta = jit->prg_buf + jit->prg - (u8 *)&ex->fixup;
		if (WARN_ON_ONCE(delta < INT_MIN || delta > INT_MAX))
			/* JIT bug - landing pad and extable must be close. */
			return -1;
		ex->fixup = delta;
		ex->type = EX_TYPE_BPF;
		ex->data = probe->reg;
		jit->excnt++;
	}
	return 0;
}

/*
 * Sign-extend the register if necessary
 */
static int sign_extend(struct bpf_jit *jit, int r, u8 size, u8 flags)
{
	if (!(flags & BTF_FMODEL_SIGNED_ARG))
		return 0;

	switch (size) {
	case 1:
		/* lgbr %r,%r */
		EMIT4(0xb9060000, r, r);
		return 0;
	case 2:
		/* lghr %r,%r */
		EMIT4(0xb9070000, r, r);
		return 0;
	case 4:
		/* lgfr %r,%r */
		EMIT4(0xb9140000, r, r);
		return 0;
	case 8:
		return 0;
	default:
		return -1;
	}
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
	s32 branch_oc_off = insn->off;
	u32 dst_reg = insn->dst_reg;
	u32 src_reg = insn->src_reg;
	struct bpf_jit_probe probe;
	int last, insn_count = 1;
	u32 *addrs = jit->addrs;
	s32 imm = insn->imm;
	s16 off = insn->off;
	unsigned int mask;
	int err;

	bpf_jit_probe_init(&probe);

	switch (insn->code) {
	/*
	 * BPF_MOV
	 */
	case BPF_ALU | BPF_MOV | BPF_X:
		switch (insn->off) {
		case 0: /* DST = (u32) SRC */
			/* llgfr %dst,%src */
			EMIT4(0xb9160000, dst_reg, src_reg);
			if (insn_is_zext(&insn[1]))
				insn_count = 2;
			break;
		case 8: /* DST = (u32)(s8) SRC */
			/* lbr %dst,%src */
			EMIT4(0xb9260000, dst_reg, src_reg);
			/* llgfr %dst,%dst */
			EMIT4(0xb9160000, dst_reg, dst_reg);
			break;
		case 16: /* DST = (u32)(s16) SRC */
			/* lhr %dst,%src */
			EMIT4(0xb9270000, dst_reg, src_reg);
			/* llgfr %dst,%dst */
			EMIT4(0xb9160000, dst_reg, dst_reg);
			break;
		}
		break;
	case BPF_ALU64 | BPF_MOV | BPF_X:
		if (insn_is_cast_user(insn)) {
			int patch_brc;

			/* ltgr %dst,%src */
			EMIT4(0xb9020000, dst_reg, src_reg);
			/* brc 8,0f */
			patch_brc = jit->prg;
			EMIT4_PCREL_RIC(0xa7040000, 8, 0);
			/* iihf %dst,user_arena>>32 */
			EMIT6_IMM(0xc0080000, dst_reg, jit->user_arena >> 32);
			/* 0: */
			if (jit->prg_buf)
				*(u16 *)(jit->prg_buf + patch_brc + 2) =
					(jit->prg - patch_brc) >> 1;
			break;
		}
		switch (insn->off) {
		case 0: /* DST = SRC */
			/* lgr %dst,%src */
			EMIT4(0xb9040000, dst_reg, src_reg);
			break;
		case 8: /* DST = (s8) SRC */
			/* lgbr %dst,%src */
			EMIT4(0xb9060000, dst_reg, src_reg);
			break;
		case 16: /* DST = (s16) SRC */
			/* lghr %dst,%src */
			EMIT4(0xb9070000, dst_reg, src_reg);
			break;
		case 32: /* DST = (s32) SRC */
			/* lgfr %dst,%src */
			EMIT4(0xb9140000, dst_reg, src_reg);
			break;
		}
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
		if (imm != 0) {
			/* alfi %dst,imm */
			EMIT6_IMM(0xc20b0000, dst_reg, imm);
		}
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
		if (imm != 0) {
			/* alfi %dst,-imm */
			EMIT6_IMM(0xc20b0000, dst_reg, -imm);
		}
		EMIT_ZERO(dst_reg);
		break;
	case BPF_ALU64 | BPF_SUB | BPF_K: /* dst = dst - imm */
		if (!imm)
			break;
		if (imm == -0x80000000) {
			/* algfi %dst,0x80000000 */
			EMIT6_IMM(0xc20a0000, dst_reg, 0x80000000);
		} else {
			/* agfi %dst,-imm */
			EMIT6_IMM(0xc2080000, dst_reg, -imm);
		}
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
		if (imm != 1) {
			/* msfi %r5,imm */
			EMIT6_IMM(0xc2010000, dst_reg, imm);
		}
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
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_X:
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		switch (off) {
		case 0: /* dst = (u32) dst {/,%} (u32) src */
			/* xr %w0,%w0 */
			EMIT2(0x1700, REG_W0, REG_W0);
			/* lr %w1,%dst */
			EMIT2(0x1800, REG_W1, dst_reg);
			/* dlr %w0,%src */
			EMIT4(0xb9970000, REG_W0, src_reg);
			break;
		case 1: /* dst = (u32) ((s32) dst {/,%} (s32) src) */
			/* lgfr %r1,%dst */
			EMIT4(0xb9140000, REG_W1, dst_reg);
			/* dsgfr %r0,%src */
			EMIT4(0xb91d0000, REG_W0, src_reg);
			break;
		}
		/* llgfr %dst,%rc */
		EMIT4(0xb9160000, dst_reg, rc_reg);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	}
	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		switch (off) {
		case 0: /* dst = dst {/,%} src */
			/* lghi %w0,0 */
			EMIT4_IMM(0xa7090000, REG_W0, 0);
			/* lgr %w1,%dst */
			EMIT4(0xb9040000, REG_W1, dst_reg);
			/* dlgr %w0,%src */
			EMIT4(0xb9870000, REG_W0, src_reg);
			break;
		case 1: /* dst = (s64) dst {/,%} (s64) src */
			/* lgr %w1,%dst */
			EMIT4(0xb9040000, REG_W1, dst_reg);
			/* dsgr %w0,%src */
			EMIT4(0xb90d0000, REG_W0, src_reg);
			break;
		}
		/* lgr %dst,%rc */
		EMIT4(0xb9040000, dst_reg, rc_reg);
		break;
	}
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		if (imm == 1) {
			if (BPF_OP(insn->code) == BPF_MOD)
				/* lghi %dst,0 */
				EMIT4_IMM(0xa7090000, dst_reg, 0);
			else
				EMIT_ZERO(dst_reg);
			break;
		}
		if (!is_first_pass(jit) && can_use_ldisp_for_lit32(jit)) {
			switch (off) {
			case 0: /* dst = (u32) dst {/,%} (u32) imm */
				/* xr %w0,%w0 */
				EMIT2(0x1700, REG_W0, REG_W0);
				/* lr %w1,%dst */
				EMIT2(0x1800, REG_W1, dst_reg);
				/* dl %w0,<d(imm)>(%l) */
				EMIT6_DISP_LH(0xe3000000, 0x0097, REG_W0, REG_0,
					      REG_L, EMIT_CONST_U32(imm));
				break;
			case 1: /* dst = (s32) dst {/,%} (s32) imm */
				/* lgfr %r1,%dst */
				EMIT4(0xb9140000, REG_W1, dst_reg);
				/* dsgf %r0,<d(imm)>(%l) */
				EMIT6_DISP_LH(0xe3000000, 0x001d, REG_W0, REG_0,
					      REG_L, EMIT_CONST_U32(imm));
				break;
			}
		} else {
			switch (off) {
			case 0: /* dst = (u32) dst {/,%} (u32) imm */
				/* xr %w0,%w0 */
				EMIT2(0x1700, REG_W0, REG_W0);
				/* lr %w1,%dst */
				EMIT2(0x1800, REG_W1, dst_reg);
				/* lrl %dst,imm */
				EMIT6_PCREL_RILB(0xc40d0000, dst_reg,
						 _EMIT_CONST_U32(imm));
				jit->seen |= SEEN_LITERAL;
				/* dlr %w0,%dst */
				EMIT4(0xb9970000, REG_W0, dst_reg);
				break;
			case 1: /* dst = (s32) dst {/,%} (s32) imm */
				/* lgfr %w1,%dst */
				EMIT4(0xb9140000, REG_W1, dst_reg);
				/* lgfrl %dst,imm */
				EMIT6_PCREL_RILB(0xc40c0000, dst_reg,
						 _EMIT_CONST_U32(imm));
				jit->seen |= SEEN_LITERAL;
				/* dsgr %w0,%dst */
				EMIT4(0xb90d0000, REG_W0, dst_reg);
				break;
			}
		}
		/* llgfr %dst,%rc */
		EMIT4(0xb9160000, dst_reg, rc_reg);
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	}
	case BPF_ALU64 | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
	{
		int rc_reg = BPF_OP(insn->code) == BPF_DIV ? REG_W1 : REG_W0;

		if (imm == 1) {
			if (BPF_OP(insn->code) == BPF_MOD)
				/* lhgi %dst,0 */
				EMIT4_IMM(0xa7090000, dst_reg, 0);
			break;
		}
		if (!is_first_pass(jit) && can_use_ldisp_for_lit64(jit)) {
			switch (off) {
			case 0: /* dst = dst {/,%} imm */
				/* lghi %w0,0 */
				EMIT4_IMM(0xa7090000, REG_W0, 0);
				/* lgr %w1,%dst */
				EMIT4(0xb9040000, REG_W1, dst_reg);
				/* dlg %w0,<d(imm)>(%l) */
				EMIT6_DISP_LH(0xe3000000, 0x0087, REG_W0, REG_0,
					      REG_L, EMIT_CONST_U64(imm));
				break;
			case 1: /* dst = (s64) dst {/,%} (s64) imm */
				/* lgr %w1,%dst */
				EMIT4(0xb9040000, REG_W1, dst_reg);
				/* dsg %w0,<d(imm)>(%l) */
				EMIT6_DISP_LH(0xe3000000, 0x000d, REG_W0, REG_0,
					      REG_L, EMIT_CONST_U64(imm));
				break;
			}
		} else {
			switch (off) {
			case 0: /* dst = dst {/,%} imm */
				/* lghi %w0,0 */
				EMIT4_IMM(0xa7090000, REG_W0, 0);
				/* lgr %w1,%dst */
				EMIT4(0xb9040000, REG_W1, dst_reg);
				/* lgrl %dst,imm */
				EMIT6_PCREL_RILB(0xc4080000, dst_reg,
						 _EMIT_CONST_U64(imm));
				jit->seen |= SEEN_LITERAL;
				/* dlgr %w0,%dst */
				EMIT4(0xb9870000, REG_W0, dst_reg);
				break;
			case 1: /* dst = (s64) dst {/,%} (s64) imm */
				/* lgr %w1,%dst */
				EMIT4(0xb9040000, REG_W1, dst_reg);
				/* lgrl %dst,imm */
				EMIT6_PCREL_RILB(0xc4080000, dst_reg,
						 _EMIT_CONST_U64(imm));
				jit->seen |= SEEN_LITERAL;
				/* dsgr %w0,%dst */
				EMIT4(0xb90d0000, REG_W0, dst_reg);
				break;
			}
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
		if (imm != 0) {
			/* xilf %dst,imm */
			EMIT6_IMM(0xc0070000, dst_reg, imm);
		}
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
		if (imm != 0) {
			/* sll %dst,imm(%r0) */
			EMIT4_DISP(0x89000000, dst_reg, REG_0, imm);
		}
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
		if (imm != 0) {
			/* srl %dst,imm(%r0) */
			EMIT4_DISP(0x88000000, dst_reg, REG_0, imm);
		}
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
		if (imm != 0) {
			/* sra %dst,imm(%r0) */
			EMIT4_DISP(0x8a000000, dst_reg, REG_0, imm);
		}
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
	case BPF_ALU64 | BPF_END | BPF_FROM_LE:
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
	 * BPF_NOSPEC (speculation barrier)
	 */
	case BPF_ST | BPF_NOSPEC:
		break;
	/*
	 * BPF_ST(X)
	 */
	case BPF_STX | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = src_reg */
	case BPF_STX | BPF_PROBE_MEM32 | BPF_B:
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* stcy %src,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0072, src_reg, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = src */
	case BPF_STX | BPF_PROBE_MEM32 | BPF_H:
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* sthy %src,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0070, src_reg, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = src */
	case BPF_STX | BPF_PROBE_MEM32 | BPF_W:
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* sty %src,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0050, src_reg, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_STX | BPF_MEM | BPF_DW: /* (u64 *)(dst + off) = src */
	case BPF_STX | BPF_PROBE_MEM32 | BPF_DW:
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* stg %src,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, src_reg, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = imm */
	case BPF_ST | BPF_PROBE_MEM32 | BPF_B:
		/* lhi %w0,imm */
		EMIT4_IMM(0xa7080000, REG_W0, (u8) imm);
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* stcy %w0,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0072, REG_W0, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = imm */
	case BPF_ST | BPF_PROBE_MEM32 | BPF_H:
		/* lhi %w0,imm */
		EMIT4_IMM(0xa7080000, REG_W0, (u16) imm);
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* sthy %w0,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0070, REG_W0, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = imm */
	case BPF_ST | BPF_PROBE_MEM32 | BPF_W:
		/* llilf %w0,imm  */
		EMIT6_IMM(0xc00f0000, REG_W0, (u32) imm);
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* sty %w0,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0050, REG_W0, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_ST | BPF_MEM | BPF_DW: /* *(u64 *)(dst + off) = imm */
	case BPF_ST | BPF_PROBE_MEM32 | BPF_DW:
		/* lgfi %w0,imm */
		EMIT6_IMM(0xc0010000, REG_W0, imm);
		bpf_jit_probe_store_pre(jit, insn, &probe);
		/* stg %w0,off(%dst,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, REG_W0, dst_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	/*
	 * BPF_ATOMIC
	 */
	case BPF_STX | BPF_ATOMIC | BPF_DW:
	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_PROBE_ATOMIC | BPF_DW:
	case BPF_STX | BPF_PROBE_ATOMIC | BPF_W:
	{
		bool is32 = BPF_SIZE(insn->code) == BPF_W;

		/*
		 * Unlike loads and stores, atomics have only a base register,
		 * but no index register. For the non-arena case, simply use
		 * %dst as a base. For the arena case, use the work register
		 * %r1: first, load the arena base into it, and then add %dst
		 * to it.
		 */
		probe.arena_reg = dst_reg;

		switch (insn->imm) {
#define EMIT_ATOMIC(op32, op64) do {					\
	bpf_jit_probe_atomic_pre(jit, insn, &probe);			\
	/* {op32|op64} {%w0|%src},%src,off(%arena) */			\
	EMIT6_DISP_LH(0xeb000000, is32 ? (op32) : (op64),		\
		      (insn->imm & BPF_FETCH) ? src_reg : REG_W0,	\
		      src_reg, probe.arena_reg, off);			\
	err = bpf_jit_probe_post(jit, fp, &probe);			\
	if (err < 0)							\
		return err;						\
	if (insn->imm & BPF_FETCH) {					\
		/* bcr 14,0 - see atomic_fetch_{add,and,or,xor}() */	\
		_EMIT2(0x07e0);						\
		if (is32)                                               \
			EMIT_ZERO(src_reg);				\
	}								\
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
		case BPF_XCHG: {
			struct bpf_jit_probe load_probe = probe;
			int loop_start;

			bpf_jit_probe_atomic_pre(jit, insn, &load_probe);
			/* {ly|lg} %w0,off(%arena) */
			EMIT6_DISP_LH(0xe3000000,
				      is32 ? 0x0058 : 0x0004, REG_W0, REG_0,
				      load_probe.arena_reg, off);
			bpf_jit_probe_emit_nop(jit, &load_probe);
			/* Reuse {ly|lg}'s arena_reg for {csy|csg}. */
			if (load_probe.prg != -1) {
				probe.prg = jit->prg;
				probe.arena_reg = load_probe.arena_reg;
			}
			loop_start = jit->prg;
			/* 0: {csy|csg} %w0,%src,off(%arena) */
			EMIT6_DISP_LH(0xeb000000, is32 ? 0x0014 : 0x0030,
				      REG_W0, src_reg, probe.arena_reg, off);
			bpf_jit_probe_emit_nop(jit, &probe);
			/* brc 4,0b */
			EMIT4_PCREL_RIC(0xa7040000, 4, loop_start);
			/* {llgfr|lgr} %src,%w0 */
			EMIT4(is32 ? 0xb9160000 : 0xb9040000, src_reg, REG_W0);
			/* Both probes should land here on exception. */
			err = bpf_jit_probe_post(jit, fp, &load_probe);
			if (err < 0)
				return err;
			err = bpf_jit_probe_post(jit, fp, &probe);
			if (err < 0)
				return err;
			if (is32 && insn_is_zext(&insn[1]))
				insn_count = 2;
			break;
		}
		case BPF_CMPXCHG:
			bpf_jit_probe_atomic_pre(jit, insn, &probe);
			/* 0: {csy|csg} %b0,%src,off(%arena) */
			EMIT6_DISP_LH(0xeb000000, is32 ? 0x0014 : 0x0030,
				      BPF_REG_0, src_reg,
				      probe.arena_reg, off);
			err = bpf_jit_probe_post(jit, fp, &probe);
			if (err < 0)
				return err;
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
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_B:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* llgc %dst,off(%src,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0090, dst_reg, src_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEMSX | BPF_B: /* dst = *(s8 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_B:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* lgb %dst,off(%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0077, dst_reg, src_reg, REG_0, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_LDX | BPF_MEM | BPF_H: /* dst = *(u16 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_H:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_H:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* llgh %dst,off(%src,%arena) */
		EMIT6_DISP_LH(0xe3000000, 0x0091, dst_reg, src_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEMSX | BPF_H: /* dst = *(s16 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_H:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* lgh %dst,off(%src) */
		EMIT6_DISP_LH(0xe3000000, 0x0015, dst_reg, src_reg, REG_0, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		jit->seen |= SEEN_MEM;
		break;
	case BPF_LDX | BPF_MEM | BPF_W: /* dst = *(u32 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_W:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_W:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* llgf %dst,off(%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0016, dst_reg, src_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		if (insn_is_zext(&insn[1]))
			insn_count = 2;
		break;
	case BPF_LDX | BPF_MEMSX | BPF_W: /* dst = *(s32 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_W:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* lgf %dst,off(%src) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0014, dst_reg, src_reg, REG_0, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		break;
	case BPF_LDX | BPF_MEM | BPF_DW: /* dst = *(u64 *)(ul) (src + off) */
	case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_DW:
		bpf_jit_probe_load_pre(jit, insn, &probe);
		/* lg %dst,off(%src,%arena) */
		jit->seen |= SEEN_MEM;
		EMIT6_DISP_LH(0xe3000000, 0x0004, dst_reg, src_reg,
			      probe.arena_reg, off);
		err = bpf_jit_probe_post(jit, fp, &probe);
		if (err < 0)
			return err;
		break;
	/*
	 * BPF_JMP / CALL
	 */
	case BPF_JMP | BPF_CALL:
	{
		const struct btf_func_model *m;
		bool func_addr_fixed;
		int j, ret;
		u64 func;

		ret = bpf_jit_get_func_addr(fp, insn, extra_pass,
					    &func, &func_addr_fixed);
		if (ret < 0)
			return -1;

		REG_SET_SEEN(BPF_REG_5);
		jit->seen |= SEEN_FUNC;
		/*
		 * Copy the tail call counter to where the callee expects it.
		 *
		 * Note 1: The callee can increment the tail call counter, but
		 * we do not load it back, since the x86 JIT does not do this
		 * either.
		 *
		 * Note 2: We assume that the verifier does not let us call the
		 * main program, which clears the tail call counter on entry.
		 */
		/* mvc STK_OFF_TCCNT(4,%r15),N(%r15) */
		_EMIT6(0xd203f000 | STK_OFF_TCCNT,
		       0xf000 | (STK_OFF_TCCNT + STK_OFF + stack_depth));

		/* Sign-extend the kfunc arguments. */
		if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
			m = bpf_jit_find_kfunc_model(fp, insn);
			if (!m)
				return -1;

			for (j = 0; j < m->nr_args; j++) {
				if (sign_extend(jit, BPF_REG_1 + j,
						m->arg_size[j],
						m->arg_flags[j]))
					return -1;
			}
		}

		/* lgrl %w1,func */
		EMIT6_PCREL_RILB(0xc4080000, REG_W1, _EMIT_CONST_U64(func));
		/* %r1() */
		call_r1(jit);
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
		 *
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
		 * if (tail_call_cnt++ >= MAX_TAIL_CALL_CNT)
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
		/* clij %w1,MAX_TAIL_CALL_CNT-1,0x2,out */
		patch_2_clij = jit->prg;
		EMIT6_PCREL_RIEC(0xec000000, 0x007f, REG_W1, MAX_TAIL_CALL_CNT - 1,
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
		save_restore_regs(jit, REGS_RESTORE, stack_depth, 0);

		/*
		 * goto *(prog->bpf_func + tail_call_start);
		 */

		/* lg %r1,bpf_func(%r1) */
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_1, REG_1, REG_0,
			      offsetof(struct bpf_prog, bpf_func));
		if (nospec_uses_trampoline()) {
			jit->seen |= SEEN_FUNC;
			/* aghi %r1,tail_call_start */
			EMIT4_IMM(0xa70b0000, REG_1, jit->tail_call_start);
			/* brcl 0xf,__s390_indirect_jump_r1 */
			EMIT6_PCREL_RILC(0xc0040000, 0xf, jit->r1_thunk_ip);
		} else {
			/* bc 0xf,tail_call_start(%r1) */
			_EMIT4(0x47f01000 + jit->tail_call_start);
		}
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
	case BPF_JMP32 | BPF_JA: /* if (true) */
		branch_oc_off = imm;
		fallthrough;
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
		    can_use_rel(jit, addrs[i + branch_oc_off + 1])) {
			/* brc mask,off */
			EMIT4_PCREL_RIC(0xa7040000,
					mask >> 12,
					addrs[i + branch_oc_off + 1]);
		} else {
			/* brcl mask,off */
			EMIT6_PCREL_RILC(0xc0040000,
					 mask >> 12,
					 addrs[i + branch_oc_off + 1]);
		}
		break;
	}
	default: /* too complex, give up */
		pr_err("Unknown opcode %02x\n", insn->code);
		return -1;
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
	u64 kern_arena;

	jit->lit32 = jit->lit32_start;
	jit->lit64 = jit->lit64_start;
	jit->prg = 0;
	jit->excnt = 0;

	kern_arena = bpf_arena_get_kern_vm_start(fp->aux->arena);
	if (kern_arena)
		jit->kern_arena = _EMIT_CONST_U64(kern_arena);
	jit->user_arena = bpf_arena_get_user_vm_start(fp->aux->arena);

	bpf_jit_prologue(jit, fp, stack_depth);
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
	struct bpf_insn *insn;
	u32 extable_size;
	u32 code_size;
	int i;

	for (i = 0; i < fp->len; i++) {
		insn = &fp->insnsi[i];

		if (BPF_CLASS(insn->code) == BPF_STX &&
		    BPF_MODE(insn->code) == BPF_PROBE_ATOMIC &&
		    (BPF_SIZE(insn->code) == BPF_DW ||
		     BPF_SIZE(insn->code) == BPF_W) &&
		    insn->imm == BPF_XCHG)
			/*
			 * bpf_jit_insn() emits a load and a compare-and-swap,
			 * both of which need to be probed.
			 */
			fp->aux->num_exentries += 1;
	}
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
		goto free_addrs;
	}
	/*
	 * Three initial passes:
	 *   - 1/2: Determine clobbered registers
	 *   - 3:   Calculate program size and addrs array
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
		if (bpf_jit_binary_lock_ro(header)) {
			bpf_jit_binary_free(header);
			fp = orig_fp;
			goto free_addrs;
		}
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

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}

bool bpf_jit_supports_far_kfunc_call(void)
{
	return true;
}

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type t,
		       void *old_addr, void *new_addr)
{
	struct bpf_plt expected_plt, current_plt, new_plt, *plt;
	struct {
		u16 opc;
		s32 disp;
	} __packed insn;
	char *ret;
	int err;

	/* Verify the branch to be patched. */
	err = copy_from_kernel_nofault(&insn, ip, sizeof(insn));
	if (err < 0)
		return err;
	if (insn.opc != (0xc004 | (old_addr ? 0xf0 : 0)))
		return -EINVAL;

	if (t == BPF_MOD_JUMP &&
	    insn.disp == ((char *)new_addr - (char *)ip) >> 1) {
		/*
		 * The branch already points to the destination,
		 * there is no PLT.
		 */
	} else {
		/* Verify the PLT. */
		plt = ip + (insn.disp << 1);
		err = copy_from_kernel_nofault(&current_plt, plt,
					       sizeof(current_plt));
		if (err < 0)
			return err;
		ret = (char *)ip + 6;
		bpf_jit_plt(&expected_plt, ret, old_addr);
		if (memcmp(&current_plt, &expected_plt, sizeof(current_plt)))
			return -EINVAL;
		/* Adjust the call address. */
		bpf_jit_plt(&new_plt, ret, new_addr);
		s390_kernel_write(&plt->target, &new_plt.target,
				  sizeof(void *));
	}

	/* Adjust the mask of the branch. */
	insn.opc = 0xc004 | (new_addr ? 0xf0 : 0);
	s390_kernel_write((char *)ip + 1, (char *)&insn.opc + 1, 1);

	/* Make the new code visible to the other CPUs. */
	text_poke_sync_lock();

	return 0;
}

struct bpf_tramp_jit {
	struct bpf_jit common;
	int orig_stack_args_off;/* Offset of arguments placed on stack by the
				 * func_addr's original caller
				 */
	int stack_size;		/* Trampoline stack size */
	int backchain_off;	/* Offset of backchain */
	int stack_args_off;	/* Offset of stack arguments for calling
				 * func_addr, has to be at the top
				 */
	int reg_args_off;	/* Offset of register arguments for calling
				 * func_addr
				 */
	int ip_off;		/* For bpf_get_func_ip(), has to be at
				 * (ctx - 16)
				 */
	int arg_cnt_off;	/* For bpf_get_func_arg_cnt(), has to be at
				 * (ctx - 8)
				 */
	int bpf_args_off;	/* Offset of BPF_PROG context, which consists
				 * of BPF arguments followed by return value
				 */
	int retval_off;		/* Offset of return value (see above) */
	int r7_r8_off;		/* Offset of saved %r7 and %r8, which are used
				 * for __bpf_prog_enter() return value and
				 * func_addr respectively
				 */
	int run_ctx_off;	/* Offset of struct bpf_tramp_run_ctx */
	int tccnt_off;		/* Offset of saved tailcall counter */
	int r14_off;		/* Offset of saved %r14, has to be at the
				 * bottom */
	int do_fexit;		/* do_fexit: label */
};

static void load_imm64(struct bpf_jit *jit, int dst_reg, u64 val)
{
	/* llihf %dst_reg,val_hi */
	EMIT6_IMM(0xc00e0000, dst_reg, (val >> 32));
	/* oilf %rdst_reg,val_lo */
	EMIT6_IMM(0xc00d0000, dst_reg, val);
}

static int invoke_bpf_prog(struct bpf_tramp_jit *tjit,
			   const struct btf_func_model *m,
			   struct bpf_tramp_link *tlink, bool save_ret)
{
	struct bpf_jit *jit = &tjit->common;
	int cookie_off = tjit->run_ctx_off +
			 offsetof(struct bpf_tramp_run_ctx, bpf_cookie);
	struct bpf_prog *p = tlink->link.prog;
	int patch;

	/*
	 * run_ctx.cookie = tlink->cookie;
	 */

	/* %r0 = tlink->cookie */
	load_imm64(jit, REG_W0, tlink->cookie);
	/* stg %r0,cookie_off(%r15) */
	EMIT6_DISP_LH(0xe3000000, 0x0024, REG_W0, REG_0, REG_15, cookie_off);

	/*
	 * if ((start = __bpf_prog_enter(p, &run_ctx)) == 0)
	 *         goto skip;
	 */

	/* %r1 = __bpf_prog_enter */
	load_imm64(jit, REG_1, (u64)bpf_trampoline_enter(p));
	/* %r2 = p */
	load_imm64(jit, REG_2, (u64)p);
	/* la %r3,run_ctx_off(%r15) */
	EMIT4_DISP(0x41000000, REG_3, REG_15, tjit->run_ctx_off);
	/* %r1() */
	call_r1(jit);
	/* ltgr %r7,%r2 */
	EMIT4(0xb9020000, REG_7, REG_2);
	/* brcl 8,skip */
	patch = jit->prg;
	EMIT6_PCREL_RILC(0xc0040000, 8, 0);

	/*
	 * retval = bpf_func(args, p->insnsi);
	 */

	/* %r1 = p->bpf_func */
	load_imm64(jit, REG_1, (u64)p->bpf_func);
	/* la %r2,bpf_args_off(%r15) */
	EMIT4_DISP(0x41000000, REG_2, REG_15, tjit->bpf_args_off);
	/* %r3 = p->insnsi */
	if (!p->jited)
		load_imm64(jit, REG_3, (u64)p->insnsi);
	/* %r1() */
	call_r1(jit);
	/* stg %r2,retval_off(%r15) */
	if (save_ret) {
		if (sign_extend(jit, REG_2, m->ret_size, m->ret_flags))
			return -1;
		EMIT6_DISP_LH(0xe3000000, 0x0024, REG_2, REG_0, REG_15,
			      tjit->retval_off);
	}

	/* skip: */
	if (jit->prg_buf)
		*(u32 *)&jit->prg_buf[patch + 2] = (jit->prg - patch) >> 1;

	/*
	 * __bpf_prog_exit(p, start, &run_ctx);
	 */

	/* %r1 = __bpf_prog_exit */
	load_imm64(jit, REG_1, (u64)bpf_trampoline_exit(p));
	/* %r2 = p */
	load_imm64(jit, REG_2, (u64)p);
	/* lgr %r3,%r7 */
	EMIT4(0xb9040000, REG_3, REG_7);
	/* la %r4,run_ctx_off(%r15) */
	EMIT4_DISP(0x41000000, REG_4, REG_15, tjit->run_ctx_off);
	/* %r1() */
	call_r1(jit);

	return 0;
}

static int alloc_stack(struct bpf_tramp_jit *tjit, size_t size)
{
	int stack_offset = tjit->stack_size;

	tjit->stack_size += size;
	return stack_offset;
}

/* ABI uses %r2 - %r6 for parameter passing. */
#define MAX_NR_REG_ARGS 5

/* The "L" field of the "mvc" instruction is 8 bits. */
#define MAX_MVC_SIZE 256
#define MAX_NR_STACK_ARGS (MAX_MVC_SIZE / sizeof(u64))

/* -mfentry generates a 6-byte nop on s390x. */
#define S390X_PATCH_SIZE 6

static int __arch_prepare_bpf_trampoline(struct bpf_tramp_image *im,
					 struct bpf_tramp_jit *tjit,
					 const struct btf_func_model *m,
					 u32 flags,
					 struct bpf_tramp_links *tlinks,
					 void *func_addr)
{
	struct bpf_tramp_links *fmod_ret = &tlinks[BPF_TRAMP_MODIFY_RETURN];
	struct bpf_tramp_links *fentry = &tlinks[BPF_TRAMP_FENTRY];
	struct bpf_tramp_links *fexit = &tlinks[BPF_TRAMP_FEXIT];
	int nr_bpf_args, nr_reg_args, nr_stack_args;
	struct bpf_jit *jit = &tjit->common;
	int arg, bpf_arg_off;
	int i, j;

	/* Support as many stack arguments as "mvc" instruction can handle. */
	nr_reg_args = min_t(int, m->nr_args, MAX_NR_REG_ARGS);
	nr_stack_args = m->nr_args - nr_reg_args;
	if (nr_stack_args > MAX_NR_STACK_ARGS)
		return -ENOTSUPP;

	/* Return to %r14, since func_addr and %r0 are not available. */
	if ((!func_addr && !(flags & BPF_TRAMP_F_ORIG_STACK)) ||
	    (flags & BPF_TRAMP_F_INDIRECT))
		flags |= BPF_TRAMP_F_SKIP_FRAME;

	/*
	 * Compute how many arguments we need to pass to BPF programs.
	 * BPF ABI mirrors that of x86_64: arguments that are 16 bytes or
	 * smaller are packed into 1 or 2 registers; larger arguments are
	 * passed via pointers.
	 * In s390x ABI, arguments that are 8 bytes or smaller are packed into
	 * a register; larger arguments are passed via pointers.
	 * We need to deal with this difference.
	 */
	nr_bpf_args = 0;
	for (i = 0; i < m->nr_args; i++) {
		if (m->arg_size[i] <= 8)
			nr_bpf_args += 1;
		else if (m->arg_size[i] <= 16)
			nr_bpf_args += 2;
		else
			return -ENOTSUPP;
	}

	/*
	 * Calculate the stack layout.
	 */

	/*
	 * Allocate STACK_FRAME_OVERHEAD bytes for the callees. As the s390x
	 * ABI requires, put our backchain at the end of the allocated memory.
	 */
	tjit->stack_size = STACK_FRAME_OVERHEAD;
	tjit->backchain_off = tjit->stack_size - sizeof(u64);
	tjit->stack_args_off = alloc_stack(tjit, nr_stack_args * sizeof(u64));
	tjit->reg_args_off = alloc_stack(tjit, nr_reg_args * sizeof(u64));
	tjit->ip_off = alloc_stack(tjit, sizeof(u64));
	tjit->arg_cnt_off = alloc_stack(tjit, sizeof(u64));
	tjit->bpf_args_off = alloc_stack(tjit, nr_bpf_args * sizeof(u64));
	tjit->retval_off = alloc_stack(tjit, sizeof(u64));
	tjit->r7_r8_off = alloc_stack(tjit, 2 * sizeof(u64));
	tjit->run_ctx_off = alloc_stack(tjit,
					sizeof(struct bpf_tramp_run_ctx));
	tjit->tccnt_off = alloc_stack(tjit, sizeof(u64));
	tjit->r14_off = alloc_stack(tjit, sizeof(u64) * 2);
	/*
	 * In accordance with the s390x ABI, the caller has allocated
	 * STACK_FRAME_OVERHEAD bytes for us. 8 of them contain the caller's
	 * backchain, and the rest we can use.
	 */
	tjit->stack_size -= STACK_FRAME_OVERHEAD - sizeof(u64);
	tjit->orig_stack_args_off = tjit->stack_size + STACK_FRAME_OVERHEAD;

	/* lgr %r1,%r15 */
	EMIT4(0xb9040000, REG_1, REG_15);
	/* aghi %r15,-stack_size */
	EMIT4_IMM(0xa70b0000, REG_15, -tjit->stack_size);
	/* stg %r1,backchain_off(%r15) */
	EMIT6_DISP_LH(0xe3000000, 0x0024, REG_1, REG_0, REG_15,
		      tjit->backchain_off);
	/* mvc tccnt_off(4,%r15),stack_size+STK_OFF_TCCNT(%r15) */
	_EMIT6(0xd203f000 | tjit->tccnt_off,
	       0xf000 | (tjit->stack_size + STK_OFF_TCCNT));
	/* stmg %r2,%rN,fwd_reg_args_off(%r15) */
	if (nr_reg_args)
		EMIT6_DISP_LH(0xeb000000, 0x0024, REG_2,
			      REG_2 + (nr_reg_args - 1), REG_15,
			      tjit->reg_args_off);
	for (i = 0, j = 0; i < m->nr_args; i++) {
		if (i < MAX_NR_REG_ARGS)
			arg = REG_2 + i;
		else
			arg = tjit->orig_stack_args_off +
			      (i - MAX_NR_REG_ARGS) * sizeof(u64);
		bpf_arg_off = tjit->bpf_args_off + j * sizeof(u64);
		if (m->arg_size[i] <= 8) {
			if (i < MAX_NR_REG_ARGS)
				/* stg %arg,bpf_arg_off(%r15) */
				EMIT6_DISP_LH(0xe3000000, 0x0024, arg,
					      REG_0, REG_15, bpf_arg_off);
			else
				/* mvc bpf_arg_off(8,%r15),arg(%r15) */
				_EMIT6(0xd207f000 | bpf_arg_off,
				       0xf000 | arg);
			j += 1;
		} else {
			if (i < MAX_NR_REG_ARGS) {
				/* mvc bpf_arg_off(16,%r15),0(%arg) */
				_EMIT6(0xd20ff000 | bpf_arg_off,
				       reg2hex[arg] << 12);
			} else {
				/* lg %r1,arg(%r15) */
				EMIT6_DISP_LH(0xe3000000, 0x0004, REG_1, REG_0,
					      REG_15, arg);
				/* mvc bpf_arg_off(16,%r15),0(%r1) */
				_EMIT6(0xd20ff000 | bpf_arg_off, 0x1000);
			}
			j += 2;
		}
	}
	/* stmg %r7,%r8,r7_r8_off(%r15) */
	EMIT6_DISP_LH(0xeb000000, 0x0024, REG_7, REG_8, REG_15,
		      tjit->r7_r8_off);
	/* stg %r14,r14_off(%r15) */
	EMIT6_DISP_LH(0xe3000000, 0x0024, REG_14, REG_0, REG_15, tjit->r14_off);

	if (flags & BPF_TRAMP_F_ORIG_STACK) {
		/*
		 * The ftrace trampoline puts the return address (which is the
		 * address of the original function + S390X_PATCH_SIZE) into
		 * %r0; see ftrace_shared_hotpatch_trampoline_br and
		 * ftrace_init_nop() for details.
		 */

		/* lgr %r8,%r0 */
		EMIT4(0xb9040000, REG_8, REG_0);
	} else {
		/* %r8 = func_addr + S390X_PATCH_SIZE */
		load_imm64(jit, REG_8, (u64)func_addr + S390X_PATCH_SIZE);
	}

	/*
	 * ip = func_addr;
	 * arg_cnt = m->nr_args;
	 */

	if (flags & BPF_TRAMP_F_IP_ARG) {
		/* %r0 = func_addr */
		load_imm64(jit, REG_0, (u64)func_addr);
		/* stg %r0,ip_off(%r15) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, REG_0, REG_0, REG_15,
			      tjit->ip_off);
	}
	/* lghi %r0,nr_bpf_args */
	EMIT4_IMM(0xa7090000, REG_0, nr_bpf_args);
	/* stg %r0,arg_cnt_off(%r15) */
	EMIT6_DISP_LH(0xe3000000, 0x0024, REG_0, REG_0, REG_15,
		      tjit->arg_cnt_off);

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		/*
		 * __bpf_tramp_enter(im);
		 */

		/* %r1 = __bpf_tramp_enter */
		load_imm64(jit, REG_1, (u64)__bpf_tramp_enter);
		/* %r2 = im */
		load_imm64(jit, REG_2, (u64)im);
		/* %r1() */
		call_r1(jit);
	}

	for (i = 0; i < fentry->nr_links; i++)
		if (invoke_bpf_prog(tjit, m, fentry->links[i],
				    flags & BPF_TRAMP_F_RET_FENTRY_RET))
			return -EINVAL;

	if (fmod_ret->nr_links) {
		/*
		 * retval = 0;
		 */

		/* xc retval_off(8,%r15),retval_off(%r15) */
		_EMIT6(0xd707f000 | tjit->retval_off,
		       0xf000 | tjit->retval_off);

		for (i = 0; i < fmod_ret->nr_links; i++) {
			if (invoke_bpf_prog(tjit, m, fmod_ret->links[i], true))
				return -EINVAL;

			/*
			 * if (retval)
			 *         goto do_fexit;
			 */

			/* ltg %r0,retval_off(%r15) */
			EMIT6_DISP_LH(0xe3000000, 0x0002, REG_0, REG_0, REG_15,
				      tjit->retval_off);
			/* brcl 7,do_fexit */
			EMIT6_PCREL_RILC(0xc0040000, 7, tjit->do_fexit);
		}
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		/*
		 * retval = func_addr(args);
		 */

		/* lmg %r2,%rN,reg_args_off(%r15) */
		if (nr_reg_args)
			EMIT6_DISP_LH(0xeb000000, 0x0004, REG_2,
				      REG_2 + (nr_reg_args - 1), REG_15,
				      tjit->reg_args_off);
		/* mvc stack_args_off(N,%r15),orig_stack_args_off(%r15) */
		if (nr_stack_args)
			_EMIT6(0xd200f000 |
				       (nr_stack_args * sizeof(u64) - 1) << 16 |
				       tjit->stack_args_off,
			       0xf000 | tjit->orig_stack_args_off);
		/* mvc STK_OFF_TCCNT(4,%r15),tccnt_off(%r15) */
		_EMIT6(0xd203f000 | STK_OFF_TCCNT, 0xf000 | tjit->tccnt_off);
		/* lgr %r1,%r8 */
		EMIT4(0xb9040000, REG_1, REG_8);
		/* %r1() */
		call_r1(jit);
		/* stg %r2,retval_off(%r15) */
		EMIT6_DISP_LH(0xe3000000, 0x0024, REG_2, REG_0, REG_15,
			      tjit->retval_off);

		im->ip_after_call = jit->prg_buf + jit->prg;

		/*
		 * The following nop will be patched by bpf_tramp_image_put().
		 */

		/* brcl 0,im->ip_epilogue */
		EMIT6_PCREL_RILC(0xc0040000, 0, (u64)im->ip_epilogue);
	}

	/* do_fexit: */
	tjit->do_fexit = jit->prg;
	for (i = 0; i < fexit->nr_links; i++)
		if (invoke_bpf_prog(tjit, m, fexit->links[i], false))
			return -EINVAL;

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		im->ip_epilogue = jit->prg_buf + jit->prg;

		/*
		 * __bpf_tramp_exit(im);
		 */

		/* %r1 = __bpf_tramp_exit */
		load_imm64(jit, REG_1, (u64)__bpf_tramp_exit);
		/* %r2 = im */
		load_imm64(jit, REG_2, (u64)im);
		/* %r1() */
		call_r1(jit);
	}

	/* lmg %r2,%rN,reg_args_off(%r15) */
	if ((flags & BPF_TRAMP_F_RESTORE_REGS) && nr_reg_args)
		EMIT6_DISP_LH(0xeb000000, 0x0004, REG_2,
			      REG_2 + (nr_reg_args - 1), REG_15,
			      tjit->reg_args_off);
	/* lgr %r1,%r8 */
	if (!(flags & BPF_TRAMP_F_SKIP_FRAME))
		EMIT4(0xb9040000, REG_1, REG_8);
	/* lmg %r7,%r8,r7_r8_off(%r15) */
	EMIT6_DISP_LH(0xeb000000, 0x0004, REG_7, REG_8, REG_15,
		      tjit->r7_r8_off);
	/* lg %r14,r14_off(%r15) */
	EMIT6_DISP_LH(0xe3000000, 0x0004, REG_14, REG_0, REG_15, tjit->r14_off);
	/* lg %r2,retval_off(%r15) */
	if (flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET))
		EMIT6_DISP_LH(0xe3000000, 0x0004, REG_2, REG_0, REG_15,
			      tjit->retval_off);
	/* mvc stack_size+STK_OFF_TCCNT(4,%r15),tccnt_off(%r15) */
	_EMIT6(0xd203f000 | (tjit->stack_size + STK_OFF_TCCNT),
	       0xf000 | tjit->tccnt_off);
	/* aghi %r15,stack_size */
	EMIT4_IMM(0xa70b0000, REG_15, tjit->stack_size);
	/* Emit an expoline for the following indirect jump. */
	if (nospec_uses_trampoline())
		emit_expoline(jit);
	if (flags & BPF_TRAMP_F_SKIP_FRAME)
		/* br %r14 */
		_EMIT2(0x07fe);
	else
		/* br %r1 */
		_EMIT2(0x07f1);

	emit_r1_thunk(jit);

	return 0;
}

int arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
			     struct bpf_tramp_links *tlinks, void *orig_call)
{
	struct bpf_tramp_image im;
	struct bpf_tramp_jit tjit;
	int ret;

	memset(&tjit, 0, sizeof(tjit));

	ret = __arch_prepare_bpf_trampoline(&im, &tjit, m, flags,
					    tlinks, orig_call);

	return ret < 0 ? ret : tjit.common.prg;
}

int arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *image,
				void *image_end, const struct btf_func_model *m,
				u32 flags, struct bpf_tramp_links *tlinks,
				void *func_addr)
{
	struct bpf_tramp_jit tjit;
	int ret;

	/* Compute offsets, check whether the code fits. */
	memset(&tjit, 0, sizeof(tjit));
	ret = __arch_prepare_bpf_trampoline(im, &tjit, m, flags,
					    tlinks, func_addr);

	if (ret < 0)
		return ret;
	if (tjit.common.prg > (char *)image_end - (char *)image)
		/*
		 * Use the same error code as for exceeding
		 * BPF_MAX_TRAMP_LINKS.
		 */
		return -E2BIG;

	tjit.common.prg = 0;
	tjit.common.prg_buf = image;
	ret = __arch_prepare_bpf_trampoline(im, &tjit, m, flags,
					    tlinks, func_addr);

	return ret < 0 ? ret : tjit.common.prg;
}

bool bpf_jit_supports_subprog_tailcalls(void)
{
	return true;
}

bool bpf_jit_supports_arena(void)
{
	return true;
}

bool bpf_jit_supports_insn(struct bpf_insn *insn, bool in_arena)
{
	/*
	 * Currently the verifier uses this function only to check which
	 * atomic stores to arena are supported, and they all are.
	 */
	return true;
}

bool bpf_jit_supports_exceptions(void)
{
	/*
	 * Exceptions require unwinding support, which is always available,
	 * because the kernel is always built with backchain.
	 */
	return true;
}

void arch_bpf_stack_walk(bool (*consume_fn)(void *, u64, u64, u64),
			 void *cookie)
{
	unsigned long addr, prev_addr = 0;
	struct unwind_state state;

	unwind_for_each_frame(&state, NULL, NULL, 0) {
		addr = unwind_get_return_address(&state);
		if (!addr)
			break;
		/*
		 * addr is a return address and state.sp is the value of %r15
		 * at this address. exception_cb needs %r15 at entry to the
		 * function containing addr, so take the next state.sp.
		 *
		 * There is no bp, and the exception_cb prog does not need one
		 * to perform a quasi-longjmp. The common code requires a
		 * non-zero bp, so pass sp there as well.
		 */
		if (prev_addr && !consume_fn(cookie, prev_addr, state.sp,
					     state.sp))
			break;
		prev_addr = addr;
	}
}
