// SPDX-License-Identifier: GPL-2.0
/*
 * Just-In-Time compiler for eBPF filters on IA32 (32bit x86)
 *
 * Author: Wang YanQing (udknight@gmail.com)
 * The code based on code and ideas from:
 * Eric Dumazet (eric.dumazet@gmail.com)
 * and from:
 * Shubham Bansal <illusionist.neo@gmail.com>
 */

#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <asm/cacheflush.h>
#include <asm/set_memory.h>
#include <asm/nospec-branch.h>
#include <asm/asm-prototypes.h>
#include <linux/bpf.h>

/*
 * eBPF prog stack layout:
 *
 *                         high
 * original ESP =>        +-----+
 *                        |     | callee saved registers
 *                        +-----+
 *                        | ... | eBPF JIT scratch space
 * BPF_FP,IA32_EBP  =>    +-----+
 *                        | ... | eBPF prog stack
 *                        +-----+
 *                        |RSVD | JIT scratchpad
 * current ESP =>         +-----+
 *                        |     |
 *                        | ... | Function call stack
 *                        |     |
 *                        +-----+
 *                          low
 *
 * The callee saved registers:
 *
 *                                high
 * original ESP =>        +------------------+ \
 *                        |        ebp       | |
 * current EBP =>         +------------------+ } callee saved registers
 *                        |    ebx,esi,edi   | |
 *                        +------------------+ /
 *                                low
 */

static u8 *emit_code(u8 *ptr, u32 bytes, unsigned int len)
{
	if (len == 1)
		*ptr = bytes;
	else if (len == 2)
		*(u16 *)ptr = bytes;
	else {
		*(u32 *)ptr = bytes;
		barrier();
	}
	return ptr + len;
}

#define EMIT(bytes, len) \
	do { prog = emit_code(prog, bytes, len); cnt += len; } while (0)

#define EMIT1(b1)		EMIT(b1, 1)
#define EMIT2(b1, b2)		EMIT((b1) + ((b2) << 8), 2)
#define EMIT3(b1, b2, b3)	EMIT((b1) + ((b2) << 8) + ((b3) << 16), 3)
#define EMIT4(b1, b2, b3, b4)   \
	EMIT((b1) + ((b2) << 8) + ((b3) << 16) + ((b4) << 24), 4)

#define EMIT1_off32(b1, off) \
	do { EMIT1(b1); EMIT(off, 4); } while (0)
#define EMIT2_off32(b1, b2, off) \
	do { EMIT2(b1, b2); EMIT(off, 4); } while (0)
#define EMIT3_off32(b1, b2, b3, off) \
	do { EMIT3(b1, b2, b3); EMIT(off, 4); } while (0)
#define EMIT4_off32(b1, b2, b3, b4, off) \
	do { EMIT4(b1, b2, b3, b4); EMIT(off, 4); } while (0)

#define jmp_label(label, jmp_insn_len) (label - cnt - jmp_insn_len)

static bool is_imm8(int value)
{
	return value <= 127 && value >= -128;
}

static bool is_simm32(s64 value)
{
	return value == (s64) (s32) value;
}

#define STACK_OFFSET(k)	(k)
#define TCALL_CNT	(MAX_BPF_JIT_REG + 0)	/* Tail Call Count */

#define IA32_EAX	(0x0)
#define IA32_EBX	(0x3)
#define IA32_ECX	(0x1)
#define IA32_EDX	(0x2)
#define IA32_ESI	(0x6)
#define IA32_EDI	(0x7)
#define IA32_EBP	(0x5)
#define IA32_ESP	(0x4)

/*
 * List of x86 cond jumps opcodes (. + s8)
 * Add 0x10 (and an extra 0x0f) to generate far jumps (. + s32)
 */
#define IA32_JB  0x72
#define IA32_JAE 0x73
#define IA32_JE  0x74
#define IA32_JNE 0x75
#define IA32_JBE 0x76
#define IA32_JA  0x77
#define IA32_JL  0x7C
#define IA32_JGE 0x7D
#define IA32_JLE 0x7E
#define IA32_JG  0x7F

#define COND_JMP_OPCODE_INVALID	(0xFF)

/*
 * Map eBPF registers to IA32 32bit registers or stack scratch space.
 *
 * 1. All the registers, R0-R10, are mapped to scratch space on stack.
 * 2. We need two 64 bit temp registers to do complex operations on eBPF
 *    registers.
 * 3. For performance reason, the BPF_REG_AX for blinding constant, is
 *    mapped to real hardware register pair, IA32_ESI and IA32_EDI.
 *
 * As the eBPF registers are all 64 bit registers and IA32 has only 32 bit
 * registers, we have to map each eBPF registers with two IA32 32 bit regs
 * or scratch memory space and we have to build eBPF 64 bit register from those.
 *
 * We use IA32_EAX, IA32_EDX, IA32_ECX, IA32_EBX as temporary registers.
 */
static const u8 bpf2ia32[][2] = {
	/* Return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = {STACK_OFFSET(0), STACK_OFFSET(4)},

	/* The arguments from eBPF program to in-kernel function */
	/* Stored on stack scratch space */
	[BPF_REG_1] = {STACK_OFFSET(8), STACK_OFFSET(12)},
	[BPF_REG_2] = {STACK_OFFSET(16), STACK_OFFSET(20)},
	[BPF_REG_3] = {STACK_OFFSET(24), STACK_OFFSET(28)},
	[BPF_REG_4] = {STACK_OFFSET(32), STACK_OFFSET(36)},
	[BPF_REG_5] = {STACK_OFFSET(40), STACK_OFFSET(44)},

	/* Callee saved registers that in-kernel function will preserve */
	/* Stored on stack scratch space */
	[BPF_REG_6] = {STACK_OFFSET(48), STACK_OFFSET(52)},
	[BPF_REG_7] = {STACK_OFFSET(56), STACK_OFFSET(60)},
	[BPF_REG_8] = {STACK_OFFSET(64), STACK_OFFSET(68)},
	[BPF_REG_9] = {STACK_OFFSET(72), STACK_OFFSET(76)},

	/* Read only Frame Pointer to access Stack */
	[BPF_REG_FP] = {STACK_OFFSET(80), STACK_OFFSET(84)},

	/* Temporary register for blinding constants. */
	[BPF_REG_AX] = {IA32_ESI, IA32_EDI},

	/* Tail call count. Stored on stack scratch space. */
	[TCALL_CNT] = {STACK_OFFSET(88), STACK_OFFSET(92)},
};

#define dst_lo	dst[0]
#define dst_hi	dst[1]
#define src_lo	src[0]
#define src_hi	src[1]

#define STACK_ALIGNMENT	8
/*
 * Stack space for BPF_REG_1, BPF_REG_2, BPF_REG_3, BPF_REG_4,
 * BPF_REG_5, BPF_REG_6, BPF_REG_7, BPF_REG_8, BPF_REG_9,
 * BPF_REG_FP, BPF_REG_AX and Tail call counts.
 */
#define SCRATCH_SIZE 96

/* Total stack size used in JITed code */
#define _STACK_SIZE	(stack_depth + SCRATCH_SIZE)

#define STACK_SIZE ALIGN(_STACK_SIZE, STACK_ALIGNMENT)

/* Get the offset of eBPF REGISTERs stored on scratch space. */
#define STACK_VAR(off) (off)

/* Encode 'dst_reg' register into IA32 opcode 'byte' */
static u8 add_1reg(u8 byte, u32 dst_reg)
{
	return byte + dst_reg;
}

/* Encode 'dst_reg' and 'src_reg' registers into IA32 opcode 'byte' */
static u8 add_2reg(u8 byte, u32 dst_reg, u32 src_reg)
{
	return byte + dst_reg + (src_reg << 3);
}

static void jit_fill_hole(void *area, unsigned int size)
{
	/* Fill whole space with int3 instructions */
	memset(area, 0xcc, size);
}

static inline void emit_ia32_mov_i(const u8 dst, const u32 val, bool dstk,
				   u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;

	if (dstk) {
		if (val == 0) {
			/* xor eax,eax */
			EMIT2(0x33, add_2reg(0xC0, IA32_EAX, IA32_EAX));
			/* mov dword ptr [ebp+off],eax */
			EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
			      STACK_VAR(dst));
		} else {
			EMIT3_off32(0xC7, add_1reg(0x40, IA32_EBP),
				    STACK_VAR(dst), val);
		}
	} else {
		if (val == 0)
			EMIT2(0x33, add_2reg(0xC0, dst, dst));
		else
			EMIT2_off32(0xC7, add_1reg(0xC0, dst),
				    val);
	}
	*pprog = prog;
}

/* dst = imm (4 bytes)*/
static inline void emit_ia32_mov_r(const u8 dst, const u8 src, bool dstk,
				   bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 sreg = sstk ? IA32_EAX : src;

	if (sstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(src));
	if (dstk)
		/* mov dword ptr [ebp+off],eax */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, sreg), STACK_VAR(dst));
	else
		/* mov dst,sreg */
		EMIT2(0x89, add_2reg(0xC0, dst, sreg));

	*pprog = prog;
}

/* dst = src */
static inline void emit_ia32_mov_r64(const bool is64, const u8 dst[],
				     const u8 src[], bool dstk,
				     bool sstk, u8 **pprog,
				     const struct bpf_prog_aux *aux)
{
	emit_ia32_mov_r(dst_lo, src_lo, dstk, sstk, pprog);
	if (is64)
		/* complete 8 byte move */
		emit_ia32_mov_r(dst_hi, src_hi, dstk, sstk, pprog);
	else if (!aux->verifier_zext)
		/* zero out high 4 bytes */
		emit_ia32_mov_i(dst_hi, 0, dstk, pprog);
}

/* Sign extended move */
static inline void emit_ia32_mov_i64(const bool is64, const u8 dst[],
				     const u32 val, bool dstk, u8 **pprog)
{
	u32 hi = 0;

	if (is64 && (val & (1<<31)))
		hi = (u32)~0;
	emit_ia32_mov_i(dst_lo, val, dstk, pprog);
	emit_ia32_mov_i(dst_hi, hi, dstk, pprog);
}

/*
 * ALU operation (32 bit)
 * dst = dst * src
 */
static inline void emit_ia32_mul_r(const u8 dst, const u8 src, bool dstk,
				   bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 sreg = sstk ? IA32_ECX : src;

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(src));

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(dst));
	else
		/* mov eax,dst */
		EMIT2(0x8B, add_2reg(0xC0, dst, IA32_EAX));


	EMIT2(0xF7, add_1reg(0xE0, sreg));

	if (dstk)
		/* mov dword ptr [ebp+off],eax */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst));
	else
		/* mov dst,eax */
		EMIT2(0x89, add_2reg(0xC0, dst, IA32_EAX));

	*pprog = prog;
}

static inline void emit_ia32_to_le_r64(const u8 dst[], s32 val,
					 bool dstk, u8 **pprog,
					 const struct bpf_prog_aux *aux)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk && val != 64) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}
	switch (val) {
	case 16:
		/*
		 * Emit 'movzwl eax,ax' to zero extend 16-bit
		 * into 64 bit
		 */
		EMIT2(0x0F, 0xB7);
		EMIT1(add_2reg(0xC0, dreg_lo, dreg_lo));
		if (!aux->verifier_zext)
			/* xor dreg_hi,dreg_hi */
			EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
		break;
	case 32:
		if (!aux->verifier_zext)
			/* xor dreg_hi,dreg_hi */
			EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
		break;
	case 64:
		/* nop */
		break;
	}

	if (dstk && val != 64) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

static inline void emit_ia32_to_be_r64(const u8 dst[], s32 val,
				       bool dstk, u8 **pprog,
				       const struct bpf_prog_aux *aux)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}
	switch (val) {
	case 16:
		/* Emit 'ror %ax, 8' to swap lower 2 bytes */
		EMIT1(0x66);
		EMIT3(0xC1, add_1reg(0xC8, dreg_lo), 8);

		EMIT2(0x0F, 0xB7);
		EMIT1(add_2reg(0xC0, dreg_lo, dreg_lo));

		if (!aux->verifier_zext)
			/* xor dreg_hi,dreg_hi */
			EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
		break;
	case 32:
		/* Emit 'bswap eax' to swap lower 4 bytes */
		EMIT1(0x0F);
		EMIT1(add_1reg(0xC8, dreg_lo));

		if (!aux->verifier_zext)
			/* xor dreg_hi,dreg_hi */
			EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
		break;
	case 64:
		/* Emit 'bswap eax' to swap lower 4 bytes */
		EMIT1(0x0F);
		EMIT1(add_1reg(0xC8, dreg_lo));

		/* Emit 'bswap edx' to swap lower 4 bytes */
		EMIT1(0x0F);
		EMIT1(add_1reg(0xC8, dreg_hi));

		/* mov ecx,dreg_hi */
		EMIT2(0x89, add_2reg(0xC0, IA32_ECX, dreg_hi));
		/* mov dreg_hi,dreg_lo */
		EMIT2(0x89, add_2reg(0xC0, dreg_hi, dreg_lo));
		/* mov dreg_lo,ecx */
		EMIT2(0x89, add_2reg(0xC0, dreg_lo, IA32_ECX));

		break;
	}
	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

/*
 * ALU operation (32 bit)
 * dst = dst (div|mod) src
 */
static inline void emit_ia32_div_mod_r(const u8 op, const u8 dst, const u8 src,
				       bool dstk, bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(src));
	else if (src != IA32_ECX)
		/* mov ecx,src */
		EMIT2(0x8B, add_2reg(0xC0, src, IA32_ECX));

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst));
	else
		/* mov eax,dst */
		EMIT2(0x8B, add_2reg(0xC0, dst, IA32_EAX));

	/* xor edx,edx */
	EMIT2(0x31, add_2reg(0xC0, IA32_EDX, IA32_EDX));
	/* div ecx */
	EMIT2(0xF7, add_1reg(0xF0, IA32_ECX));

	if (op == BPF_MOD) {
		if (dstk)
			EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EDX),
			      STACK_VAR(dst));
		else
			EMIT2(0x89, add_2reg(0xC0, dst, IA32_EDX));
	} else {
		if (dstk)
			EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
			      STACK_VAR(dst));
		else
			EMIT2(0x89, add_2reg(0xC0, dst, IA32_EAX));
	}
	*pprog = prog;
}

/*
 * ALU operation (32 bit)
 * dst = dst (shift) src
 */
static inline void emit_ia32_shift_r(const u8 op, const u8 dst, const u8 src,
				     bool dstk, bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg = dstk ? IA32_EAX : dst;
	u8 b2;

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(dst));

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(src));
	else if (src != IA32_ECX)
		/* mov ecx,src */
		EMIT2(0x8B, add_2reg(0xC0, src, IA32_ECX));

	switch (op) {
	case BPF_LSH:
		b2 = 0xE0; break;
	case BPF_RSH:
		b2 = 0xE8; break;
	case BPF_ARSH:
		b2 = 0xF8; break;
	default:
		return;
	}
	EMIT2(0xD3, add_1reg(b2, dreg));

	if (dstk)
		/* mov dword ptr [ebp+off],dreg */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg), STACK_VAR(dst));
	*pprog = prog;
}

/*
 * ALU operation (32 bit)
 * dst = dst (op) src
 */
static inline void emit_ia32_alu_r(const bool is64, const bool hi, const u8 op,
				   const u8 dst, const u8 src, bool dstk,
				   bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 sreg = sstk ? IA32_EAX : src;
	u8 dreg = dstk ? IA32_EDX : dst;

	if (sstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(src));

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX), STACK_VAR(dst));

	switch (BPF_OP(op)) {
	/* dst = dst + src */
	case BPF_ADD:
		if (hi && is64)
			EMIT2(0x11, add_2reg(0xC0, dreg, sreg));
		else
			EMIT2(0x01, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst - src */
	case BPF_SUB:
		if (hi && is64)
			EMIT2(0x19, add_2reg(0xC0, dreg, sreg));
		else
			EMIT2(0x29, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst | src */
	case BPF_OR:
		EMIT2(0x09, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst & src */
	case BPF_AND:
		EMIT2(0x21, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst ^ src */
	case BPF_XOR:
		EMIT2(0x31, add_2reg(0xC0, dreg, sreg));
		break;
	}

	if (dstk)
		/* mov dword ptr [ebp+off],dreg */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg),
		      STACK_VAR(dst));
	*pprog = prog;
}

/* ALU operation (64 bit) */
static inline void emit_ia32_alu_r64(const bool is64, const u8 op,
				     const u8 dst[], const u8 src[],
				     bool dstk,  bool sstk,
				     u8 **pprog, const struct bpf_prog_aux *aux)
{
	u8 *prog = *pprog;

	emit_ia32_alu_r(is64, false, op, dst_lo, src_lo, dstk, sstk, &prog);
	if (is64)
		emit_ia32_alu_r(is64, true, op, dst_hi, src_hi, dstk, sstk,
				&prog);
	else if (!aux->verifier_zext)
		emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
	*pprog = prog;
}

/*
 * ALU operation (32 bit)
 * dst = dst (op) val
 */
static inline void emit_ia32_alu_i(const bool is64, const bool hi, const u8 op,
				   const u8 dst, const s32 val, bool dstk,
				   u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg = dstk ? IA32_EAX : dst;
	u8 sreg = IA32_EDX;

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(dst));

	if (!is_imm8(val))
		/* mov edx,imm32*/
		EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EDX), val);

	switch (op) {
	/* dst = dst + val */
	case BPF_ADD:
		if (hi && is64) {
			if (is_imm8(val))
				EMIT3(0x83, add_1reg(0xD0, dreg), val);
			else
				EMIT2(0x11, add_2reg(0xC0, dreg, sreg));
		} else {
			if (is_imm8(val))
				EMIT3(0x83, add_1reg(0xC0, dreg), val);
			else
				EMIT2(0x01, add_2reg(0xC0, dreg, sreg));
		}
		break;
	/* dst = dst - val */
	case BPF_SUB:
		if (hi && is64) {
			if (is_imm8(val))
				EMIT3(0x83, add_1reg(0xD8, dreg), val);
			else
				EMIT2(0x19, add_2reg(0xC0, dreg, sreg));
		} else {
			if (is_imm8(val))
				EMIT3(0x83, add_1reg(0xE8, dreg), val);
			else
				EMIT2(0x29, add_2reg(0xC0, dreg, sreg));
		}
		break;
	/* dst = dst | val */
	case BPF_OR:
		if (is_imm8(val))
			EMIT3(0x83, add_1reg(0xC8, dreg), val);
		else
			EMIT2(0x09, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst & val */
	case BPF_AND:
		if (is_imm8(val))
			EMIT3(0x83, add_1reg(0xE0, dreg), val);
		else
			EMIT2(0x21, add_2reg(0xC0, dreg, sreg));
		break;
	/* dst = dst ^ val */
	case BPF_XOR:
		if (is_imm8(val))
			EMIT3(0x83, add_1reg(0xF0, dreg), val);
		else
			EMIT2(0x31, add_2reg(0xC0, dreg, sreg));
		break;
	case BPF_NEG:
		EMIT2(0xF7, add_1reg(0xD8, dreg));
		break;
	}

	if (dstk)
		/* mov dword ptr [ebp+off],dreg */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg),
		      STACK_VAR(dst));
	*pprog = prog;
}

/* ALU operation (64 bit) */
static inline void emit_ia32_alu_i64(const bool is64, const u8 op,
				     const u8 dst[], const u32 val,
				     bool dstk, u8 **pprog,
				     const struct bpf_prog_aux *aux)
{
	u8 *prog = *pprog;
	u32 hi = 0;

	if (is64 && (val & (1<<31)))
		hi = (u32)~0;

	emit_ia32_alu_i(is64, false, op, dst_lo, val, dstk, &prog);
	if (is64)
		emit_ia32_alu_i(is64, true, op, dst_hi, hi, dstk, &prog);
	else if (!aux->verifier_zext)
		emit_ia32_mov_i(dst_hi, 0, dstk, &prog);

	*pprog = prog;
}

/* dst = ~dst (64 bit) */
static inline void emit_ia32_neg64(const u8 dst[], bool dstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}

	/* neg dreg_lo */
	EMIT2(0xF7, add_1reg(0xD8, dreg_lo));
	/* adc dreg_hi,0x0 */
	EMIT3(0x83, add_1reg(0xD0, dreg_hi), 0x00);
	/* neg dreg_hi */
	EMIT2(0xF7, add_1reg(0xD8, dreg_hi));

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

/* dst = dst << src */
static inline void emit_ia32_lsh_r64(const u8 dst[], const u8 src[],
				     bool dstk, bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(src_lo));
	else
		/* mov ecx,src_lo */
		EMIT2(0x8B, add_2reg(0xC0, src_lo, IA32_ECX));

	/* shld dreg_hi,dreg_lo,cl */
	EMIT3(0x0F, 0xA5, add_2reg(0xC0, dreg_hi, dreg_lo));
	/* shl dreg_lo,cl */
	EMIT2(0xD3, add_1reg(0xE0, dreg_lo));

	/* if ecx >= 32, mov dreg_lo into dreg_hi and clear dreg_lo */

	/* cmp ecx,32 */
	EMIT3(0x83, add_1reg(0xF8, IA32_ECX), 32);
	/* skip the next two instructions (4 bytes) when < 32 */
	EMIT2(IA32_JB, 4);

	/* mov dreg_hi,dreg_lo */
	EMIT2(0x89, add_2reg(0xC0, dreg_hi, dreg_lo));
	/* xor dreg_lo,dreg_lo */
	EMIT2(0x33, add_2reg(0xC0, dreg_lo, dreg_lo));

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	/* out: */
	*pprog = prog;
}

/* dst = dst >> src (signed)*/
static inline void emit_ia32_arsh_r64(const u8 dst[], const u8 src[],
				      bool dstk, bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(src_lo));
	else
		/* mov ecx,src_lo */
		EMIT2(0x8B, add_2reg(0xC0, src_lo, IA32_ECX));

	/* shrd dreg_lo,dreg_hi,cl */
	EMIT3(0x0F, 0xAD, add_2reg(0xC0, dreg_lo, dreg_hi));
	/* sar dreg_hi,cl */
	EMIT2(0xD3, add_1reg(0xF8, dreg_hi));

	/* if ecx >= 32, mov dreg_hi to dreg_lo and set/clear dreg_hi depending on sign */

	/* cmp ecx,32 */
	EMIT3(0x83, add_1reg(0xF8, IA32_ECX), 32);
	/* skip the next two instructions (5 bytes) when < 32 */
	EMIT2(IA32_JB, 5);

	/* mov dreg_lo,dreg_hi */
	EMIT2(0x89, add_2reg(0xC0, dreg_lo, dreg_hi));
	/* sar dreg_hi,31 */
	EMIT3(0xC1, add_1reg(0xF8, dreg_hi), 31);

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	/* out: */
	*pprog = prog;
}

/* dst = dst >> src */
static inline void emit_ia32_rsh_r64(const u8 dst[], const u8 src[], bool dstk,
				     bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}

	if (sstk)
		/* mov ecx,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(src_lo));
	else
		/* mov ecx,src_lo */
		EMIT2(0x8B, add_2reg(0xC0, src_lo, IA32_ECX));

	/* shrd dreg_lo,dreg_hi,cl */
	EMIT3(0x0F, 0xAD, add_2reg(0xC0, dreg_lo, dreg_hi));
	/* shr dreg_hi,cl */
	EMIT2(0xD3, add_1reg(0xE8, dreg_hi));

	/* if ecx >= 32, mov dreg_hi to dreg_lo and clear dreg_hi */

	/* cmp ecx,32 */
	EMIT3(0x83, add_1reg(0xF8, IA32_ECX), 32);
	/* skip the next two instructions (4 bytes) when < 32 */
	EMIT2(IA32_JB, 4);

	/* mov dreg_lo,dreg_hi */
	EMIT2(0x89, add_2reg(0xC0, dreg_lo, dreg_hi));
	/* xor dreg_hi,dreg_hi */
	EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	/* out: */
	*pprog = prog;
}

/* dst = dst << val */
static inline void emit_ia32_lsh_i64(const u8 dst[], const u32 val,
				     bool dstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}
	/* Do LSH operation */
	if (val < 32) {
		/* shld dreg_hi,dreg_lo,imm8 */
		EMIT4(0x0F, 0xA4, add_2reg(0xC0, dreg_hi, dreg_lo), val);
		/* shl dreg_lo,imm8 */
		EMIT3(0xC1, add_1reg(0xE0, dreg_lo), val);
	} else if (val >= 32 && val < 64) {
		u32 value = val - 32;

		/* shl dreg_lo,imm8 */
		EMIT3(0xC1, add_1reg(0xE0, dreg_lo), value);
		/* mov dreg_hi,dreg_lo */
		EMIT2(0x89, add_2reg(0xC0, dreg_hi, dreg_lo));
		/* xor dreg_lo,dreg_lo */
		EMIT2(0x33, add_2reg(0xC0, dreg_lo, dreg_lo));
	} else {
		/* xor dreg_lo,dreg_lo */
		EMIT2(0x33, add_2reg(0xC0, dreg_lo, dreg_lo));
		/* xor dreg_hi,dreg_hi */
		EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
	}

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

/* dst = dst >> val */
static inline void emit_ia32_rsh_i64(const u8 dst[], const u32 val,
				     bool dstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}

	/* Do RSH operation */
	if (val < 32) {
		/* shrd dreg_lo,dreg_hi,imm8 */
		EMIT4(0x0F, 0xAC, add_2reg(0xC0, dreg_lo, dreg_hi), val);
		/* shr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xE8, dreg_hi), val);
	} else if (val >= 32 && val < 64) {
		u32 value = val - 32;

		/* shr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xE8, dreg_hi), value);
		/* mov dreg_lo,dreg_hi */
		EMIT2(0x89, add_2reg(0xC0, dreg_lo, dreg_hi));
		/* xor dreg_hi,dreg_hi */
		EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
	} else {
		/* xor dreg_lo,dreg_lo */
		EMIT2(0x33, add_2reg(0xC0, dreg_lo, dreg_lo));
		/* xor dreg_hi,dreg_hi */
		EMIT2(0x33, add_2reg(0xC0, dreg_hi, dreg_hi));
	}

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

/* dst = dst >> val (signed) */
static inline void emit_ia32_arsh_i64(const u8 dst[], const u32 val,
				      bool dstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
	u8 dreg_hi = dstk ? IA32_EDX : dst_hi;

	if (dstk) {
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(dst_hi));
	}
	/* Do RSH operation */
	if (val < 32) {
		/* shrd dreg_lo,dreg_hi,imm8 */
		EMIT4(0x0F, 0xAC, add_2reg(0xC0, dreg_lo, dreg_hi), val);
		/* ashr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xF8, dreg_hi), val);
	} else if (val >= 32 && val < 64) {
		u32 value = val - 32;

		/* ashr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xF8, dreg_hi), value);
		/* mov dreg_lo,dreg_hi */
		EMIT2(0x89, add_2reg(0xC0, dreg_lo, dreg_hi));

		/* ashr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xF8, dreg_hi), 31);
	} else {
		/* ashr dreg_hi,imm8 */
		EMIT3(0xC1, add_1reg(0xF8, dreg_hi), 31);
		/* mov dreg_lo,dreg_hi */
		EMIT2(0x89, add_2reg(0xC0, dreg_lo, dreg_hi));
	}

	if (dstk) {
		/* mov dword ptr [ebp+off],dreg_lo */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_lo),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],dreg_hi */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, dreg_hi),
		      STACK_VAR(dst_hi));
	}
	*pprog = prog;
}

static inline void emit_ia32_mul_r64(const u8 dst[], const u8 src[], bool dstk,
				     bool sstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_hi));
	else
		/* mov eax,dst_hi */
		EMIT2(0x8B, add_2reg(0xC0, dst_hi, IA32_EAX));

	if (sstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(src_lo));
	else
		/* mul src_lo */
		EMIT2(0xF7, add_1reg(0xE0, src_lo));

	/* mov ecx,eax */
	EMIT2(0x89, add_2reg(0xC0, IA32_ECX, IA32_EAX));

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
	else
		/* mov eax,dst_lo */
		EMIT2(0x8B, add_2reg(0xC0, dst_lo, IA32_EAX));

	if (sstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(src_hi));
	else
		/* mul src_hi */
		EMIT2(0xF7, add_1reg(0xE0, src_hi));

	/* add eax,eax */
	EMIT2(0x01, add_2reg(0xC0, IA32_ECX, IA32_EAX));

	if (dstk)
		/* mov eax,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
	else
		/* mov eax,dst_lo */
		EMIT2(0x8B, add_2reg(0xC0, dst_lo, IA32_EAX));

	if (sstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(src_lo));
	else
		/* mul src_lo */
		EMIT2(0xF7, add_1reg(0xE0, src_lo));

	/* add ecx,edx */
	EMIT2(0x01, add_2reg(0xC0, IA32_ECX, IA32_EDX));

	if (dstk) {
		/* mov dword ptr [ebp+off],eax */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],ecx */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(dst_hi));
	} else {
		/* mov dst_lo,eax */
		EMIT2(0x89, add_2reg(0xC0, dst_lo, IA32_EAX));
		/* mov dst_hi,ecx */
		EMIT2(0x89, add_2reg(0xC0, dst_hi, IA32_ECX));
	}

	*pprog = prog;
}

static inline void emit_ia32_mul_i64(const u8 dst[], const u32 val,
				     bool dstk, u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;
	u32 hi;

	hi = val & (1<<31) ? (u32)~0 : 0;
	/* movl eax,imm32 */
	EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EAX), val);
	if (dstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(dst_hi));
	else
		/* mul dst_hi */
		EMIT2(0xF7, add_1reg(0xE0, dst_hi));

	/* mov ecx,eax */
	EMIT2(0x89, add_2reg(0xC0, IA32_ECX, IA32_EAX));

	/* movl eax,imm32 */
	EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EAX), hi);
	if (dstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(dst_lo));
	else
		/* mul dst_lo */
		EMIT2(0xF7, add_1reg(0xE0, dst_lo));
	/* add ecx,eax */
	EMIT2(0x01, add_2reg(0xC0, IA32_ECX, IA32_EAX));

	/* movl eax,imm32 */
	EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EAX), val);
	if (dstk)
		/* mul dword ptr [ebp+off] */
		EMIT3(0xF7, add_1reg(0x60, IA32_EBP), STACK_VAR(dst_lo));
	else
		/* mul dst_lo */
		EMIT2(0xF7, add_1reg(0xE0, dst_lo));

	/* add ecx,edx */
	EMIT2(0x01, add_2reg(0xC0, IA32_ECX, IA32_EDX));

	if (dstk) {
		/* mov dword ptr [ebp+off],eax */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(dst_lo));
		/* mov dword ptr [ebp+off],ecx */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_ECX),
		      STACK_VAR(dst_hi));
	} else {
		/* mov dword ptr [ebp+off],eax */
		EMIT2(0x89, add_2reg(0xC0, dst_lo, IA32_EAX));
		/* mov dword ptr [ebp+off],ecx */
		EMIT2(0x89, add_2reg(0xC0, dst_hi, IA32_ECX));
	}

	*pprog = prog;
}

static int bpf_size_to_x86_bytes(int bpf_size)
{
	if (bpf_size == BPF_W)
		return 4;
	else if (bpf_size == BPF_H)
		return 2;
	else if (bpf_size == BPF_B)
		return 1;
	else if (bpf_size == BPF_DW)
		return 4; /* imm32 */
	else
		return 0;
}

struct jit_context {
	int cleanup_addr; /* Epilogue code offset */
};

/* Maximum number of bytes emitted while JITing one eBPF insn */
#define BPF_MAX_INSN_SIZE	128
#define BPF_INSN_SAFETY		64

#define PROLOGUE_SIZE 35

/*
 * Emit prologue code for BPF program and check it's size.
 * bpf_tail_call helper will skip it while jumping into another program.
 */
static void emit_prologue(u8 **pprog, u32 stack_depth)
{
	u8 *prog = *pprog;
	int cnt = 0;
	const u8 *r1 = bpf2ia32[BPF_REG_1];
	const u8 fplo = bpf2ia32[BPF_REG_FP][0];
	const u8 fphi = bpf2ia32[BPF_REG_FP][1];
	const u8 *tcc = bpf2ia32[TCALL_CNT];

	/* push ebp */
	EMIT1(0x55);
	/* mov ebp,esp */
	EMIT2(0x89, 0xE5);
	/* push edi */
	EMIT1(0x57);
	/* push esi */
	EMIT1(0x56);
	/* push ebx */
	EMIT1(0x53);

	/* sub esp,STACK_SIZE */
	EMIT2_off32(0x81, 0xEC, STACK_SIZE);
	/* sub ebp,SCRATCH_SIZE+12*/
	EMIT3(0x83, add_1reg(0xE8, IA32_EBP), SCRATCH_SIZE + 12);
	/* xor ebx,ebx */
	EMIT2(0x31, add_2reg(0xC0, IA32_EBX, IA32_EBX));

	/* Set up BPF prog stack base register */
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBP), STACK_VAR(fplo));
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(fphi));

	/* Move BPF_CTX (EAX) to BPF_REG_R1 */
	/* mov dword ptr [ebp+off],eax */
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(r1[0]));
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(r1[1]));

	/* Initialize Tail Count */
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(tcc[0]));
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(tcc[1]));

	BUILD_BUG_ON(cnt != PROLOGUE_SIZE);
	*pprog = prog;
}

/* Emit epilogue code for BPF program */
static void emit_epilogue(u8 **pprog, u32 stack_depth)
{
	u8 *prog = *pprog;
	const u8 *r0 = bpf2ia32[BPF_REG_0];
	int cnt = 0;

	/* mov eax,dword ptr [ebp+off]*/
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(r0[0]));
	/* mov edx,dword ptr [ebp+off]*/
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX), STACK_VAR(r0[1]));

	/* add ebp,SCRATCH_SIZE+12*/
	EMIT3(0x83, add_1reg(0xC0, IA32_EBP), SCRATCH_SIZE + 12);

	/* mov ebx,dword ptr [ebp-12]*/
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EBX), -12);
	/* mov esi,dword ptr [ebp-8]*/
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ESI), -8);
	/* mov edi,dword ptr [ebp-4]*/
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDI), -4);

	EMIT1(0xC9); /* leave */
	EMIT1(0xC3); /* ret */
	*pprog = prog;
}

static int emit_jmp_edx(u8 **pprog, u8 *ip)
{
	u8 *prog = *pprog;
	int cnt = 0;

#ifdef CONFIG_RETPOLINE
	EMIT1_off32(0xE9, (u8 *)__x86_indirect_thunk_edx - (ip + 5));
#else
	EMIT2(0xFF, 0xE2);
#endif
	*pprog = prog;

	return cnt;
}

/*
 * Generate the following code:
 * ... bpf_tail_call(void *ctx, struct bpf_array *array, u64 index) ...
 *   if (index >= array->map.max_entries)
 *     goto out;
 *   if (++tail_call_cnt > MAX_TAIL_CALL_CNT)
 *     goto out;
 *   prog = array->ptrs[index];
 *   if (prog == NULL)
 *     goto out;
 *   goto *(prog->bpf_func + prologue_size);
 * out:
 */
static void emit_bpf_tail_call(u8 **pprog, u8 *ip)
{
	u8 *prog = *pprog;
	int cnt = 0;
	const u8 *r1 = bpf2ia32[BPF_REG_1];
	const u8 *r2 = bpf2ia32[BPF_REG_2];
	const u8 *r3 = bpf2ia32[BPF_REG_3];
	const u8 *tcc = bpf2ia32[TCALL_CNT];
	u32 lo, hi;
	static int jmp_label1 = -1;

	/*
	 * if (index >= array->map.max_entries)
	 *     goto out;
	 */
	/* mov eax,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(r2[0]));
	/* mov edx,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX), STACK_VAR(r3[0]));

	/* cmp dword ptr [eax+off],edx */
	EMIT3(0x39, add_2reg(0x40, IA32_EAX, IA32_EDX),
	      offsetof(struct bpf_array, map.max_entries));
	/* jbe out */
	EMIT2(IA32_JBE, jmp_label(jmp_label1, 2));

	/*
	 * if (tail_call_cnt > MAX_TAIL_CALL_CNT)
	 *     goto out;
	 */
	lo = (u32)MAX_TAIL_CALL_CNT;
	hi = (u32)((u64)MAX_TAIL_CALL_CNT >> 32);
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(tcc[0]));
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(tcc[1]));

	/* cmp edx,hi */
	EMIT3(0x83, add_1reg(0xF8, IA32_EBX), hi);
	EMIT2(IA32_JNE, 3);
	/* cmp ecx,lo */
	EMIT3(0x83, add_1reg(0xF8, IA32_ECX), lo);

	/* ja out */
	EMIT2(IA32_JAE, jmp_label(jmp_label1, 2));

	/* add eax,0x1 */
	EMIT3(0x83, add_1reg(0xC0, IA32_ECX), 0x01);
	/* adc ebx,0x0 */
	EMIT3(0x83, add_1reg(0xD0, IA32_EBX), 0x00);

	/* mov dword ptr [ebp+off],eax */
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(tcc[0]));
	/* mov dword ptr [ebp+off],edx */
	EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EBX), STACK_VAR(tcc[1]));

	/* prog = array->ptrs[index]; */
	/* mov edx, [eax + edx * 4 + offsetof(...)] */
	EMIT3_off32(0x8B, 0x94, 0x90, offsetof(struct bpf_array, ptrs));

	/*
	 * if (prog == NULL)
	 *     goto out;
	 */
	/* test edx,edx */
	EMIT2(0x85, add_2reg(0xC0, IA32_EDX, IA32_EDX));
	/* je out */
	EMIT2(IA32_JE, jmp_label(jmp_label1, 2));

	/* goto *(prog->bpf_func + prologue_size); */
	/* mov edx, dword ptr [edx + 32] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EDX, IA32_EDX),
	      offsetof(struct bpf_prog, bpf_func));
	/* add edx,prologue_size */
	EMIT3(0x83, add_1reg(0xC0, IA32_EDX), PROLOGUE_SIZE);

	/* mov eax,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX), STACK_VAR(r1[0]));

	/*
	 * Now we're ready to jump into next BPF program:
	 * eax == ctx (1st arg)
	 * edx == prog->bpf_func + prologue_size
	 */
	cnt += emit_jmp_edx(&prog, ip + cnt);

	if (jmp_label1 == -1)
		jmp_label1 = cnt;

	/* out: */
	*pprog = prog;
}

/* Push the scratch stack register on top of the stack. */
static inline void emit_push_r64(const u8 src[], u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;

	/* mov ecx,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(src_hi));
	/* push ecx */
	EMIT1(0x51);

	/* mov ecx,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(src_lo));
	/* push ecx */
	EMIT1(0x51);

	*pprog = prog;
}

static void emit_push_r32(const u8 src[], u8 **pprog)
{
	u8 *prog = *pprog;
	int cnt = 0;

	/* mov ecx,dword ptr [ebp+off] */
	EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX), STACK_VAR(src_lo));
	/* push ecx */
	EMIT1(0x51);

	*pprog = prog;
}

static u8 get_cond_jmp_opcode(const u8 op, bool is_cmp_lo)
{
	u8 jmp_cond;

	/* Convert BPF opcode to x86 */
	switch (op) {
	case BPF_JEQ:
		jmp_cond = IA32_JE;
		break;
	case BPF_JSET:
	case BPF_JNE:
		jmp_cond = IA32_JNE;
		break;
	case BPF_JGT:
		/* GT is unsigned '>', JA in x86 */
		jmp_cond = IA32_JA;
		break;
	case BPF_JLT:
		/* LT is unsigned '<', JB in x86 */
		jmp_cond = IA32_JB;
		break;
	case BPF_JGE:
		/* GE is unsigned '>=', JAE in x86 */
		jmp_cond = IA32_JAE;
		break;
	case BPF_JLE:
		/* LE is unsigned '<=', JBE in x86 */
		jmp_cond = IA32_JBE;
		break;
	case BPF_JSGT:
		if (!is_cmp_lo)
			/* Signed '>', GT in x86 */
			jmp_cond = IA32_JG;
		else
			/* GT is unsigned '>', JA in x86 */
			jmp_cond = IA32_JA;
		break;
	case BPF_JSLT:
		if (!is_cmp_lo)
			/* Signed '<', LT in x86 */
			jmp_cond = IA32_JL;
		else
			/* LT is unsigned '<', JB in x86 */
			jmp_cond = IA32_JB;
		break;
	case BPF_JSGE:
		if (!is_cmp_lo)
			/* Signed '>=', GE in x86 */
			jmp_cond = IA32_JGE;
		else
			/* GE is unsigned '>=', JAE in x86 */
			jmp_cond = IA32_JAE;
		break;
	case BPF_JSLE:
		if (!is_cmp_lo)
			/* Signed '<=', LE in x86 */
			jmp_cond = IA32_JLE;
		else
			/* LE is unsigned '<=', JBE in x86 */
			jmp_cond = IA32_JBE;
		break;
	default: /* to silence GCC warning */
		jmp_cond = COND_JMP_OPCODE_INVALID;
		break;
	}

	return jmp_cond;
}

/* i386 kernel compiles with "-mregparm=3".  From gcc document:
 *
 * ==== snippet ====
 * regparm (number)
 *	On x86-32 targets, the regparm attribute causes the compiler
 *	to pass arguments number one to (number) if they are of integral
 *	type in registers EAX, EDX, and ECX instead of on the stack.
 *	Functions that take a variable number of arguments continue
 *	to be passed all of their arguments on the stack.
 * ==== snippet ====
 *
 * The first three args of a function will be considered for
 * putting into the 32bit register EAX, EDX, and ECX.
 *
 * Two 32bit registers are used to pass a 64bit arg.
 *
 * For example,
 * void foo(u32 a, u32 b, u32 c, u32 d):
 *	u32 a: EAX
 *	u32 b: EDX
 *	u32 c: ECX
 *	u32 d: stack
 *
 * void foo(u64 a, u32 b, u32 c):
 *	u64 a: EAX (lo32) EDX (hi32)
 *	u32 b: ECX
 *	u32 c: stack
 *
 * void foo(u32 a, u64 b, u32 c):
 *	u32 a: EAX
 *	u64 b: EDX (lo32) ECX (hi32)
 *	u32 c: stack
 *
 * void foo(u32 a, u32 b, u64 c):
 *	u32 a: EAX
 *	u32 b: EDX
 *	u64 c: stack
 *
 * The return value will be stored in the EAX (and EDX for 64bit value).
 *
 * For example,
 * u32 foo(u32 a, u32 b, u32 c):
 *	return value: EAX
 *
 * u64 foo(u32 a, u32 b, u32 c):
 *	return value: EAX (lo32) EDX (hi32)
 *
 * Notes:
 *	The verifier only accepts function having integer and pointers
 *	as its args and return value, so it does not have
 *	struct-by-value.
 *
 * emit_kfunc_call() finds out the btf_func_model by calling
 * bpf_jit_find_kfunc_model().  A btf_func_model
 * has the details about the number of args, size of each arg,
 * and the size of the return value.
 *
 * It first decides how many args can be passed by EAX, EDX, and ECX.
 * That will decide what args should be pushed to the stack:
 * [first_stack_regno, last_stack_regno] are the bpf regnos
 * that should be pushed to the stack.
 *
 * It will first push all args to the stack because the push
 * will need to use ECX.  Then, it moves
 * [BPF_REG_1, first_stack_regno) to EAX, EDX, and ECX.
 *
 * When emitting a call (0xE8), it needs to figure out
 * the jmp_offset relative to the jit-insn address immediately
 * following the call (0xE8) instruction.  At this point, it knows
 * the end of the jit-insn address after completely translated the
 * current (BPF_JMP | BPF_CALL) bpf-insn.  It is passed as "end_addr"
 * to the emit_kfunc_call().  Thus, it can learn the "immediate-follow-call"
 * address by figuring out how many jit-insn is generated between
 * the call (0xE8) and the end_addr:
 *	- 0-1 jit-insn (3 bytes each) to restore the esp pointer if there
 *	  is arg pushed to the stack.
 *	- 0-2 jit-insns (3 bytes each) to handle the return value.
 */
static int emit_kfunc_call(const struct bpf_prog *bpf_prog, u8 *end_addr,
			   const struct bpf_insn *insn, u8 **pprog)
{
	const u8 arg_regs[] = { IA32_EAX, IA32_EDX, IA32_ECX };
	int i, cnt = 0, first_stack_regno, last_stack_regno;
	int free_arg_regs = ARRAY_SIZE(arg_regs);
	const struct btf_func_model *fm;
	int bytes_in_stack = 0;
	const u8 *cur_arg_reg;
	u8 *prog = *pprog;
	s64 jmp_offset;

	fm = bpf_jit_find_kfunc_model(bpf_prog, insn);
	if (!fm)
		return -EINVAL;

	first_stack_regno = BPF_REG_1;
	for (i = 0; i < fm->nr_args; i++) {
		int regs_needed = fm->arg_size[i] > sizeof(u32) ? 2 : 1;

		if (regs_needed > free_arg_regs)
			break;

		free_arg_regs -= regs_needed;
		first_stack_regno++;
	}

	/* Push the args to the stack */
	last_stack_regno = BPF_REG_0 + fm->nr_args;
	for (i = last_stack_regno; i >= first_stack_regno; i--) {
		if (fm->arg_size[i - 1] > sizeof(u32)) {
			emit_push_r64(bpf2ia32[i], &prog);
			bytes_in_stack += 8;
		} else {
			emit_push_r32(bpf2ia32[i], &prog);
			bytes_in_stack += 4;
		}
	}

	cur_arg_reg = &arg_regs[0];
	for (i = BPF_REG_1; i < first_stack_regno; i++) {
		/* mov e[adc]x,dword ptr [ebp+off] */
		EMIT3(0x8B, add_2reg(0x40, IA32_EBP, *cur_arg_reg++),
		      STACK_VAR(bpf2ia32[i][0]));
		if (fm->arg_size[i - 1] > sizeof(u32))
			/* mov e[adc]x,dword ptr [ebp+off] */
			EMIT3(0x8B, add_2reg(0x40, IA32_EBP, *cur_arg_reg++),
			      STACK_VAR(bpf2ia32[i][1]));
	}

	if (bytes_in_stack)
		/* add esp,"bytes_in_stack" */
		end_addr -= 3;

	/* mov dword ptr [ebp+off],edx */
	if (fm->ret_size > sizeof(u32))
		end_addr -= 3;

	/* mov dword ptr [ebp+off],eax */
	if (fm->ret_size)
		end_addr -= 3;

	jmp_offset = (u8 *)__bpf_call_base + insn->imm - end_addr;
	if (!is_simm32(jmp_offset)) {
		pr_err("unsupported BPF kernel function jmp_offset:%lld\n",
		       jmp_offset);
		return -EINVAL;
	}

	EMIT1_off32(0xE8, jmp_offset);

	if (fm->ret_size)
		/* mov dword ptr [ebp+off],eax */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
		      STACK_VAR(bpf2ia32[BPF_REG_0][0]));

	if (fm->ret_size > sizeof(u32))
		/* mov dword ptr [ebp+off],edx */
		EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EDX),
		      STACK_VAR(bpf2ia32[BPF_REG_0][1]));

	if (bytes_in_stack)
		/* add esp,"bytes_in_stack" */
		EMIT3(0x83, add_1reg(0xC0, IA32_ESP), bytes_in_stack);

	*pprog = prog;

	return 0;
}

static int do_jit(struct bpf_prog *bpf_prog, int *addrs, u8 *image,
		  int oldproglen, struct jit_context *ctx)
{
	struct bpf_insn *insn = bpf_prog->insnsi;
	int insn_cnt = bpf_prog->len;
	bool seen_exit = false;
	u8 temp[BPF_MAX_INSN_SIZE + BPF_INSN_SAFETY];
	int i, cnt = 0;
	int proglen = 0;
	u8 *prog = temp;

	emit_prologue(&prog, bpf_prog->aux->stack_depth);

	for (i = 0; i < insn_cnt; i++, insn++) {
		const s32 imm32 = insn->imm;
		const bool is64 = BPF_CLASS(insn->code) == BPF_ALU64;
		const bool dstk = insn->dst_reg != BPF_REG_AX;
		const bool sstk = insn->src_reg != BPF_REG_AX;
		const u8 code = insn->code;
		const u8 *dst = bpf2ia32[insn->dst_reg];
		const u8 *src = bpf2ia32[insn->src_reg];
		const u8 *r0 = bpf2ia32[BPF_REG_0];
		s64 jmp_offset;
		u8 jmp_cond;
		int ilen;
		u8 *func;

		switch (code) {
		/* ALU operations */
		/* dst = src */
		case BPF_ALU | BPF_MOV | BPF_K:
		case BPF_ALU | BPF_MOV | BPF_X:
		case BPF_ALU64 | BPF_MOV | BPF_K:
		case BPF_ALU64 | BPF_MOV | BPF_X:
			switch (BPF_SRC(code)) {
			case BPF_X:
				if (imm32 == 1) {
					/* Special mov32 for zext. */
					emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
					break;
				}
				emit_ia32_mov_r64(is64, dst, src, dstk, sstk,
						  &prog, bpf_prog->aux);
				break;
			case BPF_K:
				/* Sign-extend immediate value to dst reg */
				emit_ia32_mov_i64(is64, dst, imm32,
						  dstk, &prog);
				break;
			}
			break;
		/* dst = dst + src/imm */
		/* dst = dst - src/imm */
		/* dst = dst | src/imm */
		/* dst = dst & src/imm */
		/* dst = dst ^ src/imm */
		/* dst = dst * src/imm */
		/* dst = dst << src */
		/* dst = dst >> src */
		case BPF_ALU | BPF_ADD | BPF_K:
		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_K:
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_OR | BPF_K:
		case BPF_ALU | BPF_OR | BPF_X:
		case BPF_ALU | BPF_AND | BPF_K:
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_K:
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU64 | BPF_ADD | BPF_K:
		case BPF_ALU64 | BPF_ADD | BPF_X:
		case BPF_ALU64 | BPF_SUB | BPF_K:
		case BPF_ALU64 | BPF_SUB | BPF_X:
		case BPF_ALU64 | BPF_OR | BPF_K:
		case BPF_ALU64 | BPF_OR | BPF_X:
		case BPF_ALU64 | BPF_AND | BPF_K:
		case BPF_ALU64 | BPF_AND | BPF_X:
		case BPF_ALU64 | BPF_XOR | BPF_K:
		case BPF_ALU64 | BPF_XOR | BPF_X:
			switch (BPF_SRC(code)) {
			case BPF_X:
				emit_ia32_alu_r64(is64, BPF_OP(code), dst,
						  src, dstk, sstk, &prog,
						  bpf_prog->aux);
				break;
			case BPF_K:
				emit_ia32_alu_i64(is64, BPF_OP(code), dst,
						  imm32, dstk, &prog,
						  bpf_prog->aux);
				break;
			}
			break;
		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU | BPF_MUL | BPF_X:
			switch (BPF_SRC(code)) {
			case BPF_X:
				emit_ia32_mul_r(dst_lo, src_lo, dstk,
						sstk, &prog);
				break;
			case BPF_K:
				/* mov ecx,imm32*/
				EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX),
					    imm32);
				emit_ia32_mul_r(dst_lo, IA32_ECX, dstk,
						false, &prog);
				break;
			}
			if (!bpf_prog->aux->verifier_zext)
				emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
			break;
		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_ARSH | BPF_K:
		case BPF_ALU | BPF_ARSH | BPF_X:
			switch (BPF_SRC(code)) {
			case BPF_X:
				emit_ia32_shift_r(BPF_OP(code), dst_lo, src_lo,
						  dstk, sstk, &prog);
				break;
			case BPF_K:
				/* mov ecx,imm32*/
				EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX),
					    imm32);
				emit_ia32_shift_r(BPF_OP(code), dst_lo,
						  IA32_ECX, dstk, false,
						  &prog);
				break;
			}
			if (!bpf_prog->aux->verifier_zext)
				emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
			break;
		/* dst = dst / src(imm) */
		/* dst = dst % src(imm) */
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
		case BPF_ALU | BPF_MOD | BPF_X:
			switch (BPF_SRC(code)) {
			case BPF_X:
				emit_ia32_div_mod_r(BPF_OP(code), dst_lo,
						    src_lo, dstk, sstk, &prog);
				break;
			case BPF_K:
				/* mov ecx,imm32*/
				EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX),
					    imm32);
				emit_ia32_div_mod_r(BPF_OP(code), dst_lo,
						    IA32_ECX, dstk, false,
						    &prog);
				break;
			}
			if (!bpf_prog->aux->verifier_zext)
				emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
			break;
		case BPF_ALU64 | BPF_DIV | BPF_K:
		case BPF_ALU64 | BPF_DIV | BPF_X:
		case BPF_ALU64 | BPF_MOD | BPF_K:
		case BPF_ALU64 | BPF_MOD | BPF_X:
			goto notyet;
		/* dst = dst >> imm */
		/* dst = dst << imm */
		case BPF_ALU | BPF_RSH | BPF_K:
		case BPF_ALU | BPF_LSH | BPF_K:
			if (unlikely(imm32 > 31))
				return -EINVAL;
			/* mov ecx,imm32*/
			EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX), imm32);
			emit_ia32_shift_r(BPF_OP(code), dst_lo, IA32_ECX, dstk,
					  false, &prog);
			if (!bpf_prog->aux->verifier_zext)
				emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
			break;
		/* dst = dst << imm */
		case BPF_ALU64 | BPF_LSH | BPF_K:
			if (unlikely(imm32 > 63))
				return -EINVAL;
			emit_ia32_lsh_i64(dst, imm32, dstk, &prog);
			break;
		/* dst = dst >> imm */
		case BPF_ALU64 | BPF_RSH | BPF_K:
			if (unlikely(imm32 > 63))
				return -EINVAL;
			emit_ia32_rsh_i64(dst, imm32, dstk, &prog);
			break;
		/* dst = dst << src */
		case BPF_ALU64 | BPF_LSH | BPF_X:
			emit_ia32_lsh_r64(dst, src, dstk, sstk, &prog);
			break;
		/* dst = dst >> src */
		case BPF_ALU64 | BPF_RSH | BPF_X:
			emit_ia32_rsh_r64(dst, src, dstk, sstk, &prog);
			break;
		/* dst = dst >> src (signed) */
		case BPF_ALU64 | BPF_ARSH | BPF_X:
			emit_ia32_arsh_r64(dst, src, dstk, sstk, &prog);
			break;
		/* dst = dst >> imm (signed) */
		case BPF_ALU64 | BPF_ARSH | BPF_K:
			if (unlikely(imm32 > 63))
				return -EINVAL;
			emit_ia32_arsh_i64(dst, imm32, dstk, &prog);
			break;
		/* dst = ~dst */
		case BPF_ALU | BPF_NEG:
			emit_ia32_alu_i(is64, false, BPF_OP(code),
					dst_lo, 0, dstk, &prog);
			if (!bpf_prog->aux->verifier_zext)
				emit_ia32_mov_i(dst_hi, 0, dstk, &prog);
			break;
		/* dst = ~dst (64 bit) */
		case BPF_ALU64 | BPF_NEG:
			emit_ia32_neg64(dst, dstk, &prog);
			break;
		/* dst = dst * src/imm */
		case BPF_ALU64 | BPF_MUL | BPF_X:
		case BPF_ALU64 | BPF_MUL | BPF_K:
			switch (BPF_SRC(code)) {
			case BPF_X:
				emit_ia32_mul_r64(dst, src, dstk, sstk, &prog);
				break;
			case BPF_K:
				emit_ia32_mul_i64(dst, imm32, dstk, &prog);
				break;
			}
			break;
		/* dst = htole(dst) */
		case BPF_ALU | BPF_END | BPF_FROM_LE:
			emit_ia32_to_le_r64(dst, imm32, dstk, &prog,
					    bpf_prog->aux);
			break;
		/* dst = htobe(dst) */
		case BPF_ALU | BPF_END | BPF_FROM_BE:
			emit_ia32_to_be_r64(dst, imm32, dstk, &prog,
					    bpf_prog->aux);
			break;
		/* dst = imm64 */
		case BPF_LD | BPF_IMM | BPF_DW: {
			s32 hi, lo = imm32;

			hi = insn[1].imm;
			emit_ia32_mov_i(dst_lo, lo, dstk, &prog);
			emit_ia32_mov_i(dst_hi, hi, dstk, &prog);
			insn++;
			i++;
			break;
		}
		/* speculation barrier */
		case BPF_ST | BPF_NOSPEC:
			if (boot_cpu_has(X86_FEATURE_XMM2))
				/* Emit 'lfence' */
				EMIT3(0x0F, 0xAE, 0xE8);
			break;
		/* ST: *(u8*)(dst_reg + off) = imm */
		case BPF_ST | BPF_MEM | BPF_H:
		case BPF_ST | BPF_MEM | BPF_B:
		case BPF_ST | BPF_MEM | BPF_W:
		case BPF_ST | BPF_MEM | BPF_DW:
			if (dstk)
				/* mov eax,dword ptr [ebp+off] */
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
			else
				/* mov eax,dst_lo */
				EMIT2(0x8B, add_2reg(0xC0, dst_lo, IA32_EAX));

			switch (BPF_SIZE(code)) {
			case BPF_B:
				EMIT(0xC6, 1); break;
			case BPF_H:
				EMIT2(0x66, 0xC7); break;
			case BPF_W:
			case BPF_DW:
				EMIT(0xC7, 1); break;
			}

			if (is_imm8(insn->off))
				EMIT2(add_1reg(0x40, IA32_EAX), insn->off);
			else
				EMIT1_off32(add_1reg(0x80, IA32_EAX),
					    insn->off);
			EMIT(imm32, bpf_size_to_x86_bytes(BPF_SIZE(code)));

			if (BPF_SIZE(code) == BPF_DW) {
				u32 hi;

				hi = imm32 & (1<<31) ? (u32)~0 : 0;
				EMIT2_off32(0xC7, add_1reg(0x80, IA32_EAX),
					    insn->off + 4);
				EMIT(hi, 4);
			}
			break;

		/* STX: *(u8*)(dst_reg + off) = src_reg */
		case BPF_STX | BPF_MEM | BPF_B:
		case BPF_STX | BPF_MEM | BPF_H:
		case BPF_STX | BPF_MEM | BPF_W:
		case BPF_STX | BPF_MEM | BPF_DW:
			if (dstk)
				/* mov eax,dword ptr [ebp+off] */
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
			else
				/* mov eax,dst_lo */
				EMIT2(0x8B, add_2reg(0xC0, dst_lo, IA32_EAX));

			if (sstk)
				/* mov edx,dword ptr [ebp+off] */
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
				      STACK_VAR(src_lo));
			else
				/* mov edx,src_lo */
				EMIT2(0x8B, add_2reg(0xC0, src_lo, IA32_EDX));

			switch (BPF_SIZE(code)) {
			case BPF_B:
				EMIT(0x88, 1); break;
			case BPF_H:
				EMIT2(0x66, 0x89); break;
			case BPF_W:
			case BPF_DW:
				EMIT(0x89, 1); break;
			}

			if (is_imm8(insn->off))
				EMIT2(add_2reg(0x40, IA32_EAX, IA32_EDX),
				      insn->off);
			else
				EMIT1_off32(add_2reg(0x80, IA32_EAX, IA32_EDX),
					    insn->off);

			if (BPF_SIZE(code) == BPF_DW) {
				if (sstk)
					/* mov edi,dword ptr [ebp+off] */
					EMIT3(0x8B, add_2reg(0x40, IA32_EBP,
							     IA32_EDX),
					      STACK_VAR(src_hi));
				else
					/* mov edi,src_hi */
					EMIT2(0x8B, add_2reg(0xC0, src_hi,
							     IA32_EDX));
				EMIT1(0x89);
				if (is_imm8(insn->off + 4)) {
					EMIT2(add_2reg(0x40, IA32_EAX,
						       IA32_EDX),
					      insn->off + 4);
				} else {
					EMIT1(add_2reg(0x80, IA32_EAX,
						       IA32_EDX));
					EMIT(insn->off + 4, 4);
				}
			}
			break;

		/* LDX: dst_reg = *(u8*)(src_reg + off) */
		case BPF_LDX | BPF_MEM | BPF_B:
		case BPF_LDX | BPF_MEM | BPF_H:
		case BPF_LDX | BPF_MEM | BPF_W:
		case BPF_LDX | BPF_MEM | BPF_DW:
			if (sstk)
				/* mov eax,dword ptr [ebp+off] */
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(src_lo));
			else
				/* mov eax,dword ptr [ebp+off] */
				EMIT2(0x8B, add_2reg(0xC0, src_lo, IA32_EAX));

			switch (BPF_SIZE(code)) {
			case BPF_B:
				EMIT2(0x0F, 0xB6); break;
			case BPF_H:
				EMIT2(0x0F, 0xB7); break;
			case BPF_W:
			case BPF_DW:
				EMIT(0x8B, 1); break;
			}

			if (is_imm8(insn->off))
				EMIT2(add_2reg(0x40, IA32_EAX, IA32_EDX),
				      insn->off);
			else
				EMIT1_off32(add_2reg(0x80, IA32_EAX, IA32_EDX),
					    insn->off);

			if (dstk)
				/* mov dword ptr [ebp+off],edx */
				EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EDX),
				      STACK_VAR(dst_lo));
			else
				/* mov dst_lo,edx */
				EMIT2(0x89, add_2reg(0xC0, dst_lo, IA32_EDX));
			switch (BPF_SIZE(code)) {
			case BPF_B:
			case BPF_H:
			case BPF_W:
				if (bpf_prog->aux->verifier_zext)
					break;
				if (dstk) {
					EMIT3(0xC7, add_1reg(0x40, IA32_EBP),
					      STACK_VAR(dst_hi));
					EMIT(0x0, 4);
				} else {
					/* xor dst_hi,dst_hi */
					EMIT2(0x33,
					      add_2reg(0xC0, dst_hi, dst_hi));
				}
				break;
			case BPF_DW:
				EMIT2_off32(0x8B,
					    add_2reg(0x80, IA32_EAX, IA32_EDX),
					    insn->off + 4);
				if (dstk)
					EMIT3(0x89,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EDX),
					      STACK_VAR(dst_hi));
				else
					EMIT2(0x89,
					      add_2reg(0xC0, dst_hi, IA32_EDX));
				break;
			default:
				break;
			}
			break;
		/* call */
		case BPF_JMP | BPF_CALL:
		{
			const u8 *r1 = bpf2ia32[BPF_REG_1];
			const u8 *r2 = bpf2ia32[BPF_REG_2];
			const u8 *r3 = bpf2ia32[BPF_REG_3];
			const u8 *r4 = bpf2ia32[BPF_REG_4];
			const u8 *r5 = bpf2ia32[BPF_REG_5];

			if (insn->src_reg == BPF_PSEUDO_CALL)
				goto notyet;

			if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
				int err;

				err = emit_kfunc_call(bpf_prog,
						      image + addrs[i],
						      insn, &prog);

				if (err)
					return err;
				break;
			}

			func = (u8 *) __bpf_call_base + imm32;
			jmp_offset = func - (image + addrs[i]);

			if (!imm32 || !is_simm32(jmp_offset)) {
				pr_err("unsupported BPF func %d addr %p image %p\n",
				       imm32, func, image);
				return -EINVAL;
			}

			/* mov eax,dword ptr [ebp+off] */
			EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
			      STACK_VAR(r1[0]));
			/* mov edx,dword ptr [ebp+off] */
			EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EDX),
			      STACK_VAR(r1[1]));

			emit_push_r64(r5, &prog);
			emit_push_r64(r4, &prog);
			emit_push_r64(r3, &prog);
			emit_push_r64(r2, &prog);

			EMIT1_off32(0xE8, jmp_offset + 9);

			/* mov dword ptr [ebp+off],eax */
			EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EAX),
			      STACK_VAR(r0[0]));
			/* mov dword ptr [ebp+off],edx */
			EMIT3(0x89, add_2reg(0x40, IA32_EBP, IA32_EDX),
			      STACK_VAR(r0[1]));

			/* add esp,32 */
			EMIT3(0x83, add_1reg(0xC0, IA32_ESP), 32);
			break;
		}
		case BPF_JMP | BPF_TAIL_CALL:
			emit_bpf_tail_call(&prog, image + addrs[i - 1]);
			break;

		/* cond jump */
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JNE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JLT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JLE | BPF_X:
		case BPF_JMP32 | BPF_JEQ | BPF_X:
		case BPF_JMP32 | BPF_JNE | BPF_X:
		case BPF_JMP32 | BPF_JGT | BPF_X:
		case BPF_JMP32 | BPF_JLT | BPF_X:
		case BPF_JMP32 | BPF_JGE | BPF_X:
		case BPF_JMP32 | BPF_JLE | BPF_X:
		case BPF_JMP32 | BPF_JSGT | BPF_X:
		case BPF_JMP32 | BPF_JSLE | BPF_X:
		case BPF_JMP32 | BPF_JSLT | BPF_X:
		case BPF_JMP32 | BPF_JSGE | BPF_X: {
			bool is_jmp64 = BPF_CLASS(insn->code) == BPF_JMP;
			u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
			u8 dreg_hi = dstk ? IA32_EDX : dst_hi;
			u8 sreg_lo = sstk ? IA32_ECX : src_lo;
			u8 sreg_hi = sstk ? IA32_EBX : src_hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EDX),
					      STACK_VAR(dst_hi));
			}

			if (sstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
				      STACK_VAR(src_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EBX),
					      STACK_VAR(src_hi));
			}

			if (is_jmp64) {
				/* cmp dreg_hi,sreg_hi */
				EMIT2(0x39, add_2reg(0xC0, dreg_hi, sreg_hi));
				EMIT2(IA32_JNE, 2);
			}
			/* cmp dreg_lo,sreg_lo */
			EMIT2(0x39, add_2reg(0xC0, dreg_lo, sreg_lo));
			goto emit_cond_jmp;
		}
		case BPF_JMP | BPF_JSGT | BPF_X:
		case BPF_JMP | BPF_JSLE | BPF_X:
		case BPF_JMP | BPF_JSLT | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_X: {
			u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
			u8 dreg_hi = dstk ? IA32_EDX : dst_hi;
			u8 sreg_lo = sstk ? IA32_ECX : src_lo;
			u8 sreg_hi = sstk ? IA32_EBX : src_hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				EMIT3(0x8B,
				      add_2reg(0x40, IA32_EBP,
					       IA32_EDX),
				      STACK_VAR(dst_hi));
			}

			if (sstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
				      STACK_VAR(src_lo));
				EMIT3(0x8B,
				      add_2reg(0x40, IA32_EBP,
					       IA32_EBX),
				      STACK_VAR(src_hi));
			}

			/* cmp dreg_hi,sreg_hi */
			EMIT2(0x39, add_2reg(0xC0, dreg_hi, sreg_hi));
			EMIT2(IA32_JNE, 10);
			/* cmp dreg_lo,sreg_lo */
			EMIT2(0x39, add_2reg(0xC0, dreg_lo, sreg_lo));
			goto emit_cond_jmp_signed;
		}
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP32 | BPF_JSET | BPF_X: {
			bool is_jmp64 = BPF_CLASS(insn->code) == BPF_JMP;
			u8 dreg_lo = IA32_EAX;
			u8 dreg_hi = IA32_EDX;
			u8 sreg_lo = sstk ? IA32_ECX : src_lo;
			u8 sreg_hi = sstk ? IA32_EBX : src_hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EDX),
					      STACK_VAR(dst_hi));
			} else {
				/* mov dreg_lo,dst_lo */
				EMIT2(0x89, add_2reg(0xC0, dreg_lo, dst_lo));
				if (is_jmp64)
					/* mov dreg_hi,dst_hi */
					EMIT2(0x89,
					      add_2reg(0xC0, dreg_hi, dst_hi));
			}

			if (sstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_ECX),
				      STACK_VAR(src_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EBX),
					      STACK_VAR(src_hi));
			}
			/* and dreg_lo,sreg_lo */
			EMIT2(0x23, add_2reg(0xC0, sreg_lo, dreg_lo));
			if (is_jmp64) {
				/* and dreg_hi,sreg_hi */
				EMIT2(0x23, add_2reg(0xC0, sreg_hi, dreg_hi));
				/* or dreg_lo,dreg_hi */
				EMIT2(0x09, add_2reg(0xC0, dreg_lo, dreg_hi));
			}
			goto emit_cond_jmp;
		}
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP32 | BPF_JSET | BPF_K: {
			bool is_jmp64 = BPF_CLASS(insn->code) == BPF_JMP;
			u8 dreg_lo = IA32_EAX;
			u8 dreg_hi = IA32_EDX;
			u8 sreg_lo = IA32_ECX;
			u8 sreg_hi = IA32_EBX;
			u32 hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EDX),
					      STACK_VAR(dst_hi));
			} else {
				/* mov dreg_lo,dst_lo */
				EMIT2(0x89, add_2reg(0xC0, dreg_lo, dst_lo));
				if (is_jmp64)
					/* mov dreg_hi,dst_hi */
					EMIT2(0x89,
					      add_2reg(0xC0, dreg_hi, dst_hi));
			}

			/* mov ecx,imm32 */
			EMIT2_off32(0xC7, add_1reg(0xC0, sreg_lo), imm32);

			/* and dreg_lo,sreg_lo */
			EMIT2(0x23, add_2reg(0xC0, sreg_lo, dreg_lo));
			if (is_jmp64) {
				hi = imm32 & (1 << 31) ? (u32)~0 : 0;
				/* mov ebx,imm32 */
				EMIT2_off32(0xC7, add_1reg(0xC0, sreg_hi), hi);
				/* and dreg_hi,sreg_hi */
				EMIT2(0x23, add_2reg(0xC0, sreg_hi, dreg_hi));
				/* or dreg_lo,dreg_hi */
				EMIT2(0x09, add_2reg(0xC0, dreg_lo, dreg_hi));
			}
			goto emit_cond_jmp;
		}
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JNE | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JLT | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JLE | BPF_K:
		case BPF_JMP32 | BPF_JEQ | BPF_K:
		case BPF_JMP32 | BPF_JNE | BPF_K:
		case BPF_JMP32 | BPF_JGT | BPF_K:
		case BPF_JMP32 | BPF_JLT | BPF_K:
		case BPF_JMP32 | BPF_JGE | BPF_K:
		case BPF_JMP32 | BPF_JLE | BPF_K:
		case BPF_JMP32 | BPF_JSGT | BPF_K:
		case BPF_JMP32 | BPF_JSLE | BPF_K:
		case BPF_JMP32 | BPF_JSLT | BPF_K:
		case BPF_JMP32 | BPF_JSGE | BPF_K: {
			bool is_jmp64 = BPF_CLASS(insn->code) == BPF_JMP;
			u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
			u8 dreg_hi = dstk ? IA32_EDX : dst_hi;
			u8 sreg_lo = IA32_ECX;
			u8 sreg_hi = IA32_EBX;
			u32 hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				if (is_jmp64)
					EMIT3(0x8B,
					      add_2reg(0x40, IA32_EBP,
						       IA32_EDX),
					      STACK_VAR(dst_hi));
			}

			/* mov ecx,imm32 */
			EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX), imm32);
			if (is_jmp64) {
				hi = imm32 & (1 << 31) ? (u32)~0 : 0;
				/* mov ebx,imm32 */
				EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EBX), hi);
				/* cmp dreg_hi,sreg_hi */
				EMIT2(0x39, add_2reg(0xC0, dreg_hi, sreg_hi));
				EMIT2(IA32_JNE, 2);
			}
			/* cmp dreg_lo,sreg_lo */
			EMIT2(0x39, add_2reg(0xC0, dreg_lo, sreg_lo));

emit_cond_jmp:		jmp_cond = get_cond_jmp_opcode(BPF_OP(code), false);
			if (jmp_cond == COND_JMP_OPCODE_INVALID)
				return -EFAULT;
			jmp_offset = addrs[i + insn->off] - addrs[i];
			if (is_imm8(jmp_offset)) {
				EMIT2(jmp_cond, jmp_offset);
			} else if (is_simm32(jmp_offset)) {
				EMIT2_off32(0x0F, jmp_cond + 0x10, jmp_offset);
			} else {
				pr_err("cond_jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}
			break;
		}
		case BPF_JMP | BPF_JSGT | BPF_K:
		case BPF_JMP | BPF_JSLE | BPF_K:
		case BPF_JMP | BPF_JSLT | BPF_K:
		case BPF_JMP | BPF_JSGE | BPF_K: {
			u8 dreg_lo = dstk ? IA32_EAX : dst_lo;
			u8 dreg_hi = dstk ? IA32_EDX : dst_hi;
			u8 sreg_lo = IA32_ECX;
			u8 sreg_hi = IA32_EBX;
			u32 hi;

			if (dstk) {
				EMIT3(0x8B, add_2reg(0x40, IA32_EBP, IA32_EAX),
				      STACK_VAR(dst_lo));
				EMIT3(0x8B,
				      add_2reg(0x40, IA32_EBP,
					       IA32_EDX),
				      STACK_VAR(dst_hi));
			}

			/* mov ecx,imm32 */
			EMIT2_off32(0xC7, add_1reg(0xC0, IA32_ECX), imm32);
			hi = imm32 & (1 << 31) ? (u32)~0 : 0;
			/* mov ebx,imm32 */
			EMIT2_off32(0xC7, add_1reg(0xC0, IA32_EBX), hi);
			/* cmp dreg_hi,sreg_hi */
			EMIT2(0x39, add_2reg(0xC0, dreg_hi, sreg_hi));
			EMIT2(IA32_JNE, 10);
			/* cmp dreg_lo,sreg_lo */
			EMIT2(0x39, add_2reg(0xC0, dreg_lo, sreg_lo));

			/*
			 * For simplicity of branch offset computation,
			 * let's use fixed jump coding here.
			 */
emit_cond_jmp_signed:	/* Check the condition for low 32-bit comparison */
			jmp_cond = get_cond_jmp_opcode(BPF_OP(code), true);
			if (jmp_cond == COND_JMP_OPCODE_INVALID)
				return -EFAULT;
			jmp_offset = addrs[i + insn->off] - addrs[i] + 8;
			if (is_simm32(jmp_offset)) {
				EMIT2_off32(0x0F, jmp_cond + 0x10, jmp_offset);
			} else {
				pr_err("cond_jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}
			EMIT2(0xEB, 6);

			/* Check the condition for high 32-bit comparison */
			jmp_cond = get_cond_jmp_opcode(BPF_OP(code), false);
			if (jmp_cond == COND_JMP_OPCODE_INVALID)
				return -EFAULT;
			jmp_offset = addrs[i + insn->off] - addrs[i];
			if (is_simm32(jmp_offset)) {
				EMIT2_off32(0x0F, jmp_cond + 0x10, jmp_offset);
			} else {
				pr_err("cond_jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}
			break;
		}
		case BPF_JMP | BPF_JA:
			if (insn->off == -1)
				/* -1 jmp instructions will always jump
				 * backwards two bytes. Explicitly handling
				 * this case avoids wasting too many passes
				 * when there are long sequences of replaced
				 * dead code.
				 */
				jmp_offset = -2;
			else
				jmp_offset = addrs[i + insn->off] - addrs[i];

			if (!jmp_offset)
				/* Optimize out nop jumps */
				break;
emit_jmp:
			if (is_imm8(jmp_offset)) {
				EMIT2(0xEB, jmp_offset);
			} else if (is_simm32(jmp_offset)) {
				EMIT1_off32(0xE9, jmp_offset);
			} else {
				pr_err("jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}
			break;
		case BPF_STX | BPF_ATOMIC | BPF_W:
		case BPF_STX | BPF_ATOMIC | BPF_DW:
			goto notyet;
		case BPF_JMP | BPF_EXIT:
			if (seen_exit) {
				jmp_offset = ctx->cleanup_addr - addrs[i];
				goto emit_jmp;
			}
			seen_exit = true;
			/* Update cleanup_addr */
			ctx->cleanup_addr = proglen;
			emit_epilogue(&prog, bpf_prog->aux->stack_depth);
			break;
notyet:
			pr_info_once("*** NOT YET: opcode %02x ***\n", code);
			return -EFAULT;
		default:
			/*
			 * This error will be seen if new instruction was added
			 * to interpreter, but not to JIT or if there is junk in
			 * bpf_prog
			 */
			pr_err("bpf_jit: unknown opcode %02x\n", code);
			return -EINVAL;
		}

		ilen = prog - temp;
		if (ilen > BPF_MAX_INSN_SIZE) {
			pr_err("bpf_jit: fatal insn size error\n");
			return -EFAULT;
		}

		if (image) {
			/*
			 * When populating the image, assert that:
			 *
			 *  i) We do not write beyond the allocated space, and
			 * ii) addrs[i] did not change from the prior run, in order
			 *     to validate assumptions made for computing branch
			 *     displacements.
			 */
			if (unlikely(proglen + ilen > oldproglen ||
				     proglen + ilen != addrs[i])) {
				pr_err("bpf_jit: fatal error\n");
				return -EFAULT;
			}
			memcpy(image + proglen, temp, ilen);
		}
		proglen += ilen;
		addrs[i] = proglen;
		prog = temp;
	}
	return proglen;
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_binary_header *header = NULL;
	struct bpf_prog *tmp, *orig_prog = prog;
	int proglen, oldproglen = 0;
	struct jit_context ctx = {};
	bool tmp_blinded = false;
	u8 *image = NULL;
	int *addrs;
	int pass;
	int i;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	/*
	 * If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter.
	 */
	if (IS_ERR(tmp))
		return orig_prog;
	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	addrs = kmalloc_array(prog->len, sizeof(*addrs), GFP_KERNEL);
	if (!addrs) {
		prog = orig_prog;
		goto out;
	}

	/*
	 * Before first pass, make a rough estimation of addrs[]
	 * each BPF instruction is translated to less than 64 bytes
	 */
	for (proglen = 0, i = 0; i < prog->len; i++) {
		proglen += 64;
		addrs[i] = proglen;
	}
	ctx.cleanup_addr = proglen;

	/*
	 * JITed image shrinks with every pass and the loop iterates
	 * until the image stops shrinking. Very large BPF programs
	 * may converge on the last pass. In such case do one more
	 * pass to emit the final image.
	 */
	for (pass = 0; pass < 20 || image; pass++) {
		proglen = do_jit(prog, addrs, image, oldproglen, &ctx);
		if (proglen <= 0) {
out_image:
			image = NULL;
			if (header)
				bpf_jit_binary_free(header);
			prog = orig_prog;
			goto out_addrs;
		}
		if (image) {
			if (proglen != oldproglen) {
				pr_err("bpf_jit: proglen=%d != oldproglen=%d\n",
				       proglen, oldproglen);
				goto out_image;
			}
			break;
		}
		if (proglen == oldproglen) {
			header = bpf_jit_binary_alloc(proglen, &image,
						      1, jit_fill_hole);
			if (!header) {
				prog = orig_prog;
				goto out_addrs;
			}
		}
		oldproglen = proglen;
		cond_resched();
	}

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, proglen, pass + 1, image);

	if (image) {
		bpf_jit_binary_lock_ro(header);
		prog->bpf_func = (void *)image;
		prog->jited = 1;
		prog->jited_len = proglen;
	} else {
		prog = orig_prog;
	}

out_addrs:
	kfree(addrs);
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	return prog;
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}
