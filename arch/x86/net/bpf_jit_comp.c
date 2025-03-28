// SPDX-License-Identifier: GPL-2.0-only
/*
 * BPF JIT compiler
 *
 * Copyright (C) 2011-2013 Eric Dumazet (eric.dumazet@gmail.com)
 * Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <linux/bpf.h>
#include <linux/memory.h>
#include <linux/sort.h>
#include <asm/extable.h>
#include <asm/ftrace.h>
#include <asm/set_memory.h>
#include <asm/nospec-branch.h>
#include <asm/text-patching.h>
#include <asm/unwind.h>
#include <asm/cfi.h>

static bool all_callee_regs_used[4] = {true, true, true, true};

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
	do { prog = emit_code(prog, bytes, len); } while (0)

#define EMIT1(b1)		EMIT(b1, 1)
#define EMIT2(b1, b2)		EMIT((b1) + ((b2) << 8), 2)
#define EMIT3(b1, b2, b3)	EMIT((b1) + ((b2) << 8) + ((b3) << 16), 3)
#define EMIT4(b1, b2, b3, b4)   EMIT((b1) + ((b2) << 8) + ((b3) << 16) + ((b4) << 24), 4)

#define EMIT1_off32(b1, off) \
	do { EMIT1(b1); EMIT(off, 4); } while (0)
#define EMIT2_off32(b1, b2, off) \
	do { EMIT2(b1, b2); EMIT(off, 4); } while (0)
#define EMIT3_off32(b1, b2, b3, off) \
	do { EMIT3(b1, b2, b3); EMIT(off, 4); } while (0)
#define EMIT4_off32(b1, b2, b3, b4, off) \
	do { EMIT4(b1, b2, b3, b4); EMIT(off, 4); } while (0)

#ifdef CONFIG_X86_KERNEL_IBT
#define EMIT_ENDBR()		EMIT(gen_endbr(), 4)
#define EMIT_ENDBR_POISON()	EMIT(gen_endbr_poison(), 4)
#else
#define EMIT_ENDBR()
#define EMIT_ENDBR_POISON()
#endif

static bool is_imm8(int value)
{
	return value <= 127 && value >= -128;
}

/*
 * Let us limit the positive offset to be <= 123.
 * This is to ensure eventual jit convergence For the following patterns:
 * ...
 * pass4, final_proglen=4391:
 *   ...
 *   20e:    48 85 ff                test   rdi,rdi
 *   211:    74 7d                   je     0x290
 *   213:    48 8b 77 00             mov    rsi,QWORD PTR [rdi+0x0]
 *   ...
 *   289:    48 85 ff                test   rdi,rdi
 *   28c:    74 17                   je     0x2a5
 *   28e:    e9 7f ff ff ff          jmp    0x212
 *   293:    bf 03 00 00 00          mov    edi,0x3
 * Note that insn at 0x211 is 2-byte cond jump insn for offset 0x7d (-125)
 * and insn at 0x28e is 5-byte jmp insn with offset -129.
 *
 * pass5, final_proglen=4392:
 *   ...
 *   20e:    48 85 ff                test   rdi,rdi
 *   211:    0f 84 80 00 00 00       je     0x297
 *   217:    48 8b 77 00             mov    rsi,QWORD PTR [rdi+0x0]
 *   ...
 *   28d:    48 85 ff                test   rdi,rdi
 *   290:    74 1a                   je     0x2ac
 *   292:    eb 84                   jmp    0x218
 *   294:    bf 03 00 00 00          mov    edi,0x3
 * Note that insn at 0x211 is 6-byte cond jump insn now since its offset
 * becomes 0x80 based on previous round (0x293 - 0x213 = 0x80).
 * At the same time, insn at 0x292 is a 2-byte insn since its offset is
 * -124.
 *
 * pass6 will repeat the same code as in pass4 and this will prevent
 * eventual convergence.
 *
 * To fix this issue, we need to break je (2->6 bytes) <-> jmp (5->2 bytes)
 * cycle in the above. In the above example je offset <= 0x7c should work.
 *
 * For other cases, je <-> je needs offset <= 0x7b to avoid no convergence
 * issue. For jmp <-> je and jmp <-> jmp cases, jmp offset <= 0x7c should
 * avoid no convergence issue.
 *
 * Overall, let us limit the positive offset for 8bit cond/uncond jmp insn
 * to maximum 123 (0x7b). This way, the jit pass can eventually converge.
 */
static bool is_imm8_jmp_offset(int value)
{
	return value <= 123 && value >= -128;
}

static bool is_simm32(s64 value)
{
	return value == (s64)(s32)value;
}

static bool is_uimm32(u64 value)
{
	return value == (u64)(u32)value;
}

/* mov dst, src */
#define EMIT_mov(DST, SRC)								 \
	do {										 \
		if (DST != SRC)								 \
			EMIT3(add_2mod(0x48, DST, SRC), 0x89, add_2reg(0xC0, DST, SRC)); \
	} while (0)

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

/*
 * List of x86 cond jumps opcodes (. + s8)
 * Add 0x10 (and an extra 0x0f) to generate far jumps (. + s32)
 */
#define X86_JB  0x72
#define X86_JAE 0x73
#define X86_JE  0x74
#define X86_JNE 0x75
#define X86_JBE 0x76
#define X86_JA  0x77
#define X86_JL  0x7C
#define X86_JGE 0x7D
#define X86_JLE 0x7E
#define X86_JG  0x7F

/* Pick a register outside of BPF range for JIT internal work */
#define AUX_REG (MAX_BPF_JIT_REG + 1)
#define X86_REG_R9 (MAX_BPF_JIT_REG + 2)
#define X86_REG_R12 (MAX_BPF_JIT_REG + 3)

/*
 * The following table maps BPF registers to x86-64 registers.
 *
 * x86-64 register R12 is unused, since if used as base address
 * register in load/store instructions, it always needs an
 * extra byte of encoding and is callee saved.
 *
 * x86-64 register R9 is not used by BPF programs, but can be used by BPF
 * trampoline. x86-64 register R10 is used for blinding (if enabled).
 */
static const int reg2hex[] = {
	[BPF_REG_0] = 0,  /* RAX */
	[BPF_REG_1] = 7,  /* RDI */
	[BPF_REG_2] = 6,  /* RSI */
	[BPF_REG_3] = 2,  /* RDX */
	[BPF_REG_4] = 1,  /* RCX */
	[BPF_REG_5] = 0,  /* R8  */
	[BPF_REG_6] = 3,  /* RBX callee saved */
	[BPF_REG_7] = 5,  /* R13 callee saved */
	[BPF_REG_8] = 6,  /* R14 callee saved */
	[BPF_REG_9] = 7,  /* R15 callee saved */
	[BPF_REG_FP] = 5, /* RBP readonly */
	[BPF_REG_AX] = 2, /* R10 temp register */
	[AUX_REG] = 3,    /* R11 temp register */
	[X86_REG_R9] = 1, /* R9 register, 6th function argument */
	[X86_REG_R12] = 4, /* R12 callee saved */
};

static const int reg2pt_regs[] = {
	[BPF_REG_0] = offsetof(struct pt_regs, ax),
	[BPF_REG_1] = offsetof(struct pt_regs, di),
	[BPF_REG_2] = offsetof(struct pt_regs, si),
	[BPF_REG_3] = offsetof(struct pt_regs, dx),
	[BPF_REG_4] = offsetof(struct pt_regs, cx),
	[BPF_REG_5] = offsetof(struct pt_regs, r8),
	[BPF_REG_6] = offsetof(struct pt_regs, bx),
	[BPF_REG_7] = offsetof(struct pt_regs, r13),
	[BPF_REG_8] = offsetof(struct pt_regs, r14),
	[BPF_REG_9] = offsetof(struct pt_regs, r15),
};

/*
 * is_ereg() == true if BPF register 'reg' maps to x86-64 r8..r15
 * which need extra byte of encoding.
 * rax,rcx,...,rbp have simpler encoding
 */
static bool is_ereg(u32 reg)
{
	return (1 << reg) & (BIT(BPF_REG_5) |
			     BIT(AUX_REG) |
			     BIT(BPF_REG_7) |
			     BIT(BPF_REG_8) |
			     BIT(BPF_REG_9) |
			     BIT(X86_REG_R9) |
			     BIT(X86_REG_R12) |
			     BIT(BPF_REG_AX));
}

/*
 * is_ereg_8l() == true if BPF register 'reg' is mapped to access x86-64
 * lower 8-bit registers dil,sil,bpl,spl,r8b..r15b, which need extra byte
 * of encoding. al,cl,dl,bl have simpler encoding.
 */
static bool is_ereg_8l(u32 reg)
{
	return is_ereg(reg) ||
	    (1 << reg) & (BIT(BPF_REG_1) |
			  BIT(BPF_REG_2) |
			  BIT(BPF_REG_FP));
}

static bool is_axreg(u32 reg)
{
	return reg == BPF_REG_0;
}

/* Add modifiers if 'reg' maps to x86-64 registers R8..R15 */
static u8 add_1mod(u8 byte, u32 reg)
{
	if (is_ereg(reg))
		byte |= 1;
	return byte;
}

static u8 add_2mod(u8 byte, u32 r1, u32 r2)
{
	if (is_ereg(r1))
		byte |= 1;
	if (is_ereg(r2))
		byte |= 4;
	return byte;
}

static u8 add_3mod(u8 byte, u32 r1, u32 r2, u32 index)
{
	if (is_ereg(r1))
		byte |= 1;
	if (is_ereg(index))
		byte |= 2;
	if (is_ereg(r2))
		byte |= 4;
	return byte;
}

/* Encode 'dst_reg' register into x86-64 opcode 'byte' */
static u8 add_1reg(u8 byte, u32 dst_reg)
{
	return byte + reg2hex[dst_reg];
}

/* Encode 'dst_reg' and 'src_reg' registers into x86-64 opcode 'byte' */
static u8 add_2reg(u8 byte, u32 dst_reg, u32 src_reg)
{
	return byte + reg2hex[dst_reg] + (reg2hex[src_reg] << 3);
}

/* Some 1-byte opcodes for binary ALU operations */
static u8 simple_alu_opcodes[] = {
	[BPF_ADD] = 0x01,
	[BPF_SUB] = 0x29,
	[BPF_AND] = 0x21,
	[BPF_OR] = 0x09,
	[BPF_XOR] = 0x31,
	[BPF_LSH] = 0xE0,
	[BPF_RSH] = 0xE8,
	[BPF_ARSH] = 0xF8,
};

static void jit_fill_hole(void *area, unsigned int size)
{
	/* Fill whole space with INT3 instructions */
	memset(area, 0xcc, size);
}

int bpf_arch_text_invalidate(void *dst, size_t len)
{
	return IS_ERR_OR_NULL(text_poke_set(dst, 0xcc, len));
}

struct jit_context {
	int cleanup_addr; /* Epilogue code offset */

	/*
	 * Program specific offsets of labels in the code; these rely on the
	 * JIT doing at least 2 passes, recording the position on the first
	 * pass, only to generate the correct offset on the second pass.
	 */
	int tail_call_direct_label;
	int tail_call_indirect_label;
};

/* Maximum number of bytes emitted while JITing one eBPF insn */
#define BPF_MAX_INSN_SIZE	128
#define BPF_INSN_SAFETY		64

/* Number of bytes emit_patch() needs to generate instructions */
#define X86_PATCH_SIZE		5
/* Number of bytes that will be skipped on tailcall */
#define X86_TAIL_CALL_OFFSET	(12 + ENDBR_INSN_SIZE)

static void push_r9(u8 **pprog)
{
	u8 *prog = *pprog;

	EMIT2(0x41, 0x51);   /* push r9 */
	*pprog = prog;
}

static void pop_r9(u8 **pprog)
{
	u8 *prog = *pprog;

	EMIT2(0x41, 0x59);   /* pop r9 */
	*pprog = prog;
}

static void push_r12(u8 **pprog)
{
	u8 *prog = *pprog;

	EMIT2(0x41, 0x54);   /* push r12 */
	*pprog = prog;
}

static void push_callee_regs(u8 **pprog, bool *callee_regs_used)
{
	u8 *prog = *pprog;

	if (callee_regs_used[0])
		EMIT1(0x53);         /* push rbx */
	if (callee_regs_used[1])
		EMIT2(0x41, 0x55);   /* push r13 */
	if (callee_regs_used[2])
		EMIT2(0x41, 0x56);   /* push r14 */
	if (callee_regs_used[3])
		EMIT2(0x41, 0x57);   /* push r15 */
	*pprog = prog;
}

static void pop_r12(u8 **pprog)
{
	u8 *prog = *pprog;

	EMIT2(0x41, 0x5C);   /* pop r12 */
	*pprog = prog;
}

static void pop_callee_regs(u8 **pprog, bool *callee_regs_used)
{
	u8 *prog = *pprog;

	if (callee_regs_used[3])
		EMIT2(0x41, 0x5F);   /* pop r15 */
	if (callee_regs_used[2])
		EMIT2(0x41, 0x5E);   /* pop r14 */
	if (callee_regs_used[1])
		EMIT2(0x41, 0x5D);   /* pop r13 */
	if (callee_regs_used[0])
		EMIT1(0x5B);         /* pop rbx */
	*pprog = prog;
}

static void emit_nops(u8 **pprog, int len)
{
	u8 *prog = *pprog;
	int i, noplen;

	while (len > 0) {
		noplen = len;

		if (noplen > ASM_NOP_MAX)
			noplen = ASM_NOP_MAX;

		for (i = 0; i < noplen; i++)
			EMIT1(x86_nops[noplen][i]);
		len -= noplen;
	}

	*pprog = prog;
}

/*
 * Emit the various CFI preambles, see asm/cfi.h and the comments about FineIBT
 * in arch/x86/kernel/alternative.c
 */
static int emit_call(u8 **prog, void *func, void *ip);

static void emit_fineibt(u8 **pprog, u8 *ip, u32 hash, int arity)
{
	u8 *prog = *pprog;

	EMIT_ENDBR();
	EMIT3_off32(0x41, 0x81, 0xea, hash);		/* subl $hash, %r10d	*/
	if (cfi_bhi) {
		emit_call(&prog, __bhi_args[arity], ip + 11);
	} else {
		EMIT2(0x75, 0xf9);			/* jne.d8 .-7		*/
		EMIT3(0x0f, 0x1f, 0x00);		/* nop3			*/
	}
	EMIT_ENDBR_POISON();

	*pprog = prog;
}

static void emit_kcfi(u8 **pprog, u32 hash)
{
	u8 *prog = *pprog;

	EMIT1_off32(0xb8, hash);			/* movl $hash, %eax	*/
#ifdef CONFIG_CALL_PADDING
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
	EMIT1(0x90);
#endif
	EMIT_ENDBR();

	*pprog = prog;
}

static void emit_cfi(u8 **pprog, u8 *ip, u32 hash, int arity)
{
	u8 *prog = *pprog;

	switch (cfi_mode) {
	case CFI_FINEIBT:
		emit_fineibt(&prog, ip, hash, arity);
		break;

	case CFI_KCFI:
		emit_kcfi(&prog, hash);
		break;

	default:
		EMIT_ENDBR();
		break;
	}

	*pprog = prog;
}

static void emit_prologue_tail_call(u8 **pprog, bool is_subprog)
{
	u8 *prog = *pprog;

	if (!is_subprog) {
		/* cmp rax, MAX_TAIL_CALL_CNT */
		EMIT4(0x48, 0x83, 0xF8, MAX_TAIL_CALL_CNT);
		EMIT2(X86_JA, 6);        /* ja 6 */
		/* rax is tail_call_cnt if <= MAX_TAIL_CALL_CNT.
		 * case1: entry of main prog.
		 * case2: tail callee of main prog.
		 */
		EMIT1(0x50);             /* push rax */
		/* Make rax as tail_call_cnt_ptr. */
		EMIT3(0x48, 0x89, 0xE0); /* mov rax, rsp */
		EMIT2(0xEB, 1);          /* jmp 1 */
		/* rax is tail_call_cnt_ptr if > MAX_TAIL_CALL_CNT.
		 * case: tail callee of subprog.
		 */
		EMIT1(0x50);             /* push rax */
		/* push tail_call_cnt_ptr */
		EMIT1(0x50);             /* push rax */
	} else { /* is_subprog */
		/* rax is tail_call_cnt_ptr. */
		EMIT1(0x50);             /* push rax */
		EMIT1(0x50);             /* push rax */
	}

	*pprog = prog;
}

/*
 * Emit x86-64 prologue code for BPF program.
 * bpf_tail_call helper will skip the first X86_TAIL_CALL_OFFSET bytes
 * while jumping to another program
 */
static void emit_prologue(u8 **pprog, u8 *ip, u32 stack_depth, bool ebpf_from_cbpf,
			  bool tail_call_reachable, bool is_subprog,
			  bool is_exception_cb)
{
	u8 *prog = *pprog;

	if (is_subprog) {
		emit_cfi(&prog, ip, cfi_bpf_subprog_hash, 5);
	} else {
		emit_cfi(&prog, ip, cfi_bpf_hash, 1);
	}
	/* BPF trampoline can be made to work without these nops,
	 * but let's waste 5 bytes for now and optimize later
	 */
	emit_nops(&prog, X86_PATCH_SIZE);
	if (!ebpf_from_cbpf) {
		if (tail_call_reachable && !is_subprog)
			/* When it's the entry of the whole tailcall context,
			 * zeroing rax means initialising tail_call_cnt.
			 */
			EMIT3(0x48, 0x31, 0xC0); /* xor rax, rax */
		else
			/* Keep the same instruction layout. */
			emit_nops(&prog, 3);     /* nop3 */
	}
	/* Exception callback receives FP as third parameter */
	if (is_exception_cb) {
		EMIT3(0x48, 0x89, 0xF4); /* mov rsp, rsi */
		EMIT3(0x48, 0x89, 0xD5); /* mov rbp, rdx */
		/* The main frame must have exception_boundary as true, so we
		 * first restore those callee-saved regs from stack, before
		 * reusing the stack frame.
		 */
		pop_callee_regs(&prog, all_callee_regs_used);
		pop_r12(&prog);
		/* Reset the stack frame. */
		EMIT3(0x48, 0x89, 0xEC); /* mov rsp, rbp */
	} else {
		EMIT1(0x55);             /* push rbp */
		EMIT3(0x48, 0x89, 0xE5); /* mov rbp, rsp */
	}

	/* X86_TAIL_CALL_OFFSET is here */
	EMIT_ENDBR();

	/* sub rsp, rounded_stack_depth */
	if (stack_depth)
		EMIT3_off32(0x48, 0x81, 0xEC, round_up(stack_depth, 8));
	if (tail_call_reachable)
		emit_prologue_tail_call(&prog, is_subprog);
	*pprog = prog;
}

static int emit_patch(u8 **pprog, void *func, void *ip, u8 opcode)
{
	u8 *prog = *pprog;
	s64 offset;

	offset = func - (ip + X86_PATCH_SIZE);
	if (!is_simm32(offset)) {
		pr_err("Target call %p is out of range\n", func);
		return -ERANGE;
	}
	EMIT1_off32(opcode, offset);
	*pprog = prog;
	return 0;
}

static int emit_call(u8 **pprog, void *func, void *ip)
{
	return emit_patch(pprog, func, ip, 0xE8);
}

static int emit_rsb_call(u8 **pprog, void *func, void *ip)
{
	OPTIMIZER_HIDE_VAR(func);
	ip += x86_call_depth_emit_accounting(pprog, func, ip);
	return emit_patch(pprog, func, ip, 0xE8);
}

static int emit_jump(u8 **pprog, void *func, void *ip)
{
	return emit_patch(pprog, func, ip, 0xE9);
}

static int __bpf_arch_text_poke(void *ip, enum bpf_text_poke_type t,
				void *old_addr, void *new_addr)
{
	const u8 *nop_insn = x86_nops[5];
	u8 old_insn[X86_PATCH_SIZE];
	u8 new_insn[X86_PATCH_SIZE];
	u8 *prog;
	int ret;

	memcpy(old_insn, nop_insn, X86_PATCH_SIZE);
	if (old_addr) {
		prog = old_insn;
		ret = t == BPF_MOD_CALL ?
		      emit_call(&prog, old_addr, ip) :
		      emit_jump(&prog, old_addr, ip);
		if (ret)
			return ret;
	}

	memcpy(new_insn, nop_insn, X86_PATCH_SIZE);
	if (new_addr) {
		prog = new_insn;
		ret = t == BPF_MOD_CALL ?
		      emit_call(&prog, new_addr, ip) :
		      emit_jump(&prog, new_addr, ip);
		if (ret)
			return ret;
	}

	ret = -EBUSY;
	mutex_lock(&text_mutex);
	if (memcmp(ip, old_insn, X86_PATCH_SIZE))
		goto out;
	ret = 1;
	if (memcmp(ip, new_insn, X86_PATCH_SIZE)) {
		text_poke_bp(ip, new_insn, X86_PATCH_SIZE, NULL);
		ret = 0;
	}
out:
	mutex_unlock(&text_mutex);
	return ret;
}

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type t,
		       void *old_addr, void *new_addr)
{
	if (!is_kernel_text((long)ip) &&
	    !is_bpf_text_address((long)ip))
		/* BPF poking in modules is not supported */
		return -EINVAL;

	/*
	 * See emit_prologue(), for IBT builds the trampoline hook is preceded
	 * with an ENDBR instruction.
	 */
	if (is_endbr(ip))
		ip += ENDBR_INSN_SIZE;

	return __bpf_arch_text_poke(ip, t, old_addr, new_addr);
}

#define EMIT_LFENCE()	EMIT3(0x0F, 0xAE, 0xE8)

static void emit_indirect_jump(u8 **pprog, int reg, u8 *ip)
{
	u8 *prog = *pprog;

	if (cpu_feature_enabled(X86_FEATURE_RETPOLINE_LFENCE)) {
		EMIT_LFENCE();
		EMIT2(0xFF, 0xE0 + reg);
	} else if (cpu_feature_enabled(X86_FEATURE_RETPOLINE)) {
		OPTIMIZER_HIDE_VAR(reg);
		if (cpu_feature_enabled(X86_FEATURE_CALL_DEPTH))
			emit_jump(&prog, &__x86_indirect_jump_thunk_array[reg], ip);
		else
			emit_jump(&prog, &__x86_indirect_thunk_array[reg], ip);
	} else {
		EMIT2(0xFF, 0xE0 + reg);	/* jmp *%\reg */
		if (IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) || IS_ENABLED(CONFIG_MITIGATION_SLS))
			EMIT1(0xCC);		/* int3 */
	}

	*pprog = prog;
}

static void emit_return(u8 **pprog, u8 *ip)
{
	u8 *prog = *pprog;

	if (cpu_feature_enabled(X86_FEATURE_RETHUNK)) {
		emit_jump(&prog, x86_return_thunk, ip);
	} else {
		EMIT1(0xC3);		/* ret */
		if (IS_ENABLED(CONFIG_MITIGATION_SLS))
			EMIT1(0xCC);	/* int3 */
	}

	*pprog = prog;
}

#define BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack)	(-16 - round_up(stack, 8))

/*
 * Generate the following code:
 *
 * ... bpf_tail_call(void *ctx, struct bpf_array *array, u64 index) ...
 *   if (index >= array->map.max_entries)
 *     goto out;
 *   if ((*tcc_ptr)++ >= MAX_TAIL_CALL_CNT)
 *     goto out;
 *   prog = array->ptrs[index];
 *   if (prog == NULL)
 *     goto out;
 *   goto *(prog->bpf_func + prologue_size);
 * out:
 */
static void emit_bpf_tail_call_indirect(struct bpf_prog *bpf_prog,
					u8 **pprog, bool *callee_regs_used,
					u32 stack_depth, u8 *ip,
					struct jit_context *ctx)
{
	int tcc_ptr_off = BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack_depth);
	u8 *prog = *pprog, *start = *pprog;
	int offset;

	/*
	 * rdi - pointer to ctx
	 * rsi - pointer to bpf_array
	 * rdx - index in bpf_array
	 */

	/*
	 * if (index >= array->map.max_entries)
	 *	goto out;
	 */
	EMIT2(0x89, 0xD2);                        /* mov edx, edx */
	EMIT3(0x39, 0x56,                         /* cmp dword ptr [rsi + 16], edx */
	      offsetof(struct bpf_array, map.max_entries));

	offset = ctx->tail_call_indirect_label - (prog + 2 - start);
	EMIT2(X86_JBE, offset);                   /* jbe out */

	/*
	 * if ((*tcc_ptr)++ >= MAX_TAIL_CALL_CNT)
	 *	goto out;
	 */
	EMIT3_off32(0x48, 0x8B, 0x85, tcc_ptr_off); /* mov rax, qword ptr [rbp - tcc_ptr_off] */
	EMIT4(0x48, 0x83, 0x38, MAX_TAIL_CALL_CNT); /* cmp qword ptr [rax], MAX_TAIL_CALL_CNT */

	offset = ctx->tail_call_indirect_label - (prog + 2 - start);
	EMIT2(X86_JAE, offset);                   /* jae out */

	/* prog = array->ptrs[index]; */
	EMIT4_off32(0x48, 0x8B, 0x8C, 0xD6,       /* mov rcx, [rsi + rdx * 8 + offsetof(...)] */
		    offsetof(struct bpf_array, ptrs));

	/*
	 * if (prog == NULL)
	 *	goto out;
	 */
	EMIT3(0x48, 0x85, 0xC9);                  /* test rcx,rcx */

	offset = ctx->tail_call_indirect_label - (prog + 2 - start);
	EMIT2(X86_JE, offset);                    /* je out */

	/* Inc tail_call_cnt if the slot is populated. */
	EMIT4(0x48, 0x83, 0x00, 0x01);            /* add qword ptr [rax], 1 */

	if (bpf_prog->aux->exception_boundary) {
		pop_callee_regs(&prog, all_callee_regs_used);
		pop_r12(&prog);
	} else {
		pop_callee_regs(&prog, callee_regs_used);
		if (bpf_arena_get_kern_vm_start(bpf_prog->aux->arena))
			pop_r12(&prog);
	}

	/* Pop tail_call_cnt_ptr. */
	EMIT1(0x58);                              /* pop rax */
	/* Pop tail_call_cnt, if it's main prog.
	 * Pop tail_call_cnt_ptr, if it's subprog.
	 */
	EMIT1(0x58);                              /* pop rax */
	if (stack_depth)
		EMIT3_off32(0x48, 0x81, 0xC4,     /* add rsp, sd */
			    round_up(stack_depth, 8));

	/* goto *(prog->bpf_func + X86_TAIL_CALL_OFFSET); */
	EMIT4(0x48, 0x8B, 0x49,                   /* mov rcx, qword ptr [rcx + 32] */
	      offsetof(struct bpf_prog, bpf_func));
	EMIT4(0x48, 0x83, 0xC1,                   /* add rcx, X86_TAIL_CALL_OFFSET */
	      X86_TAIL_CALL_OFFSET);
	/*
	 * Now we're ready to jump into next BPF program
	 * rdi == ctx (1st arg)
	 * rcx == prog->bpf_func + X86_TAIL_CALL_OFFSET
	 */
	emit_indirect_jump(&prog, 1 /* rcx */, ip + (prog - start));

	/* out: */
	ctx->tail_call_indirect_label = prog - start;
	*pprog = prog;
}

static void emit_bpf_tail_call_direct(struct bpf_prog *bpf_prog,
				      struct bpf_jit_poke_descriptor *poke,
				      u8 **pprog, u8 *ip,
				      bool *callee_regs_used, u32 stack_depth,
				      struct jit_context *ctx)
{
	int tcc_ptr_off = BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack_depth);
	u8 *prog = *pprog, *start = *pprog;
	int offset;

	/*
	 * if ((*tcc_ptr)++ >= MAX_TAIL_CALL_CNT)
	 *	goto out;
	 */
	EMIT3_off32(0x48, 0x8B, 0x85, tcc_ptr_off);   /* mov rax, qword ptr [rbp - tcc_ptr_off] */
	EMIT4(0x48, 0x83, 0x38, MAX_TAIL_CALL_CNT);   /* cmp qword ptr [rax], MAX_TAIL_CALL_CNT */

	offset = ctx->tail_call_direct_label - (prog + 2 - start);
	EMIT2(X86_JAE, offset);                       /* jae out */

	poke->tailcall_bypass = ip + (prog - start);
	poke->adj_off = X86_TAIL_CALL_OFFSET;
	poke->tailcall_target = ip + ctx->tail_call_direct_label - X86_PATCH_SIZE;
	poke->bypass_addr = (u8 *)poke->tailcall_target + X86_PATCH_SIZE;

	emit_jump(&prog, (u8 *)poke->tailcall_target + X86_PATCH_SIZE,
		  poke->tailcall_bypass);

	/* Inc tail_call_cnt if the slot is populated. */
	EMIT4(0x48, 0x83, 0x00, 0x01);                /* add qword ptr [rax], 1 */

	if (bpf_prog->aux->exception_boundary) {
		pop_callee_regs(&prog, all_callee_regs_used);
		pop_r12(&prog);
	} else {
		pop_callee_regs(&prog, callee_regs_used);
		if (bpf_arena_get_kern_vm_start(bpf_prog->aux->arena))
			pop_r12(&prog);
	}

	/* Pop tail_call_cnt_ptr. */
	EMIT1(0x58);                                  /* pop rax */
	/* Pop tail_call_cnt, if it's main prog.
	 * Pop tail_call_cnt_ptr, if it's subprog.
	 */
	EMIT1(0x58);                                  /* pop rax */
	if (stack_depth)
		EMIT3_off32(0x48, 0x81, 0xC4, round_up(stack_depth, 8));

	emit_nops(&prog, X86_PATCH_SIZE);

	/* out: */
	ctx->tail_call_direct_label = prog - start;

	*pprog = prog;
}

static void bpf_tail_call_direct_fixup(struct bpf_prog *prog)
{
	struct bpf_jit_poke_descriptor *poke;
	struct bpf_array *array;
	struct bpf_prog *target;
	int i, ret;

	for (i = 0; i < prog->aux->size_poke_tab; i++) {
		poke = &prog->aux->poke_tab[i];
		if (poke->aux && poke->aux != prog->aux)
			continue;

		WARN_ON_ONCE(READ_ONCE(poke->tailcall_target_stable));

		if (poke->reason != BPF_POKE_REASON_TAIL_CALL)
			continue;

		array = container_of(poke->tail_call.map, struct bpf_array, map);
		mutex_lock(&array->aux->poke_mutex);
		target = array->ptrs[poke->tail_call.key];
		if (target) {
			ret = __bpf_arch_text_poke(poke->tailcall_target,
						   BPF_MOD_JUMP, NULL,
						   (u8 *)target->bpf_func +
						   poke->adj_off);
			BUG_ON(ret < 0);
			ret = __bpf_arch_text_poke(poke->tailcall_bypass,
						   BPF_MOD_JUMP,
						   (u8 *)poke->tailcall_target +
						   X86_PATCH_SIZE, NULL);
			BUG_ON(ret < 0);
		}
		WRITE_ONCE(poke->tailcall_target_stable, true);
		mutex_unlock(&array->aux->poke_mutex);
	}
}

static void emit_mov_imm32(u8 **pprog, bool sign_propagate,
			   u32 dst_reg, const u32 imm32)
{
	u8 *prog = *pprog;
	u8 b1, b2, b3;

	/*
	 * Optimization: if imm32 is positive, use 'mov %eax, imm32'
	 * (which zero-extends imm32) to save 2 bytes.
	 */
	if (sign_propagate && (s32)imm32 < 0) {
		/* 'mov %rax, imm32' sign extends imm32 */
		b1 = add_1mod(0x48, dst_reg);
		b2 = 0xC7;
		b3 = 0xC0;
		EMIT3_off32(b1, b2, add_1reg(b3, dst_reg), imm32);
		goto done;
	}

	/*
	 * Optimization: if imm32 is zero, use 'xor %eax, %eax'
	 * to save 3 bytes.
	 */
	if (imm32 == 0) {
		if (is_ereg(dst_reg))
			EMIT1(add_2mod(0x40, dst_reg, dst_reg));
		b2 = 0x31; /* xor */
		b3 = 0xC0;
		EMIT2(b2, add_2reg(b3, dst_reg, dst_reg));
		goto done;
	}

	/* mov %eax, imm32 */
	if (is_ereg(dst_reg))
		EMIT1(add_1mod(0x40, dst_reg));
	EMIT1_off32(add_1reg(0xB8, dst_reg), imm32);
done:
	*pprog = prog;
}

static void emit_mov_imm64(u8 **pprog, u32 dst_reg,
			   const u32 imm32_hi, const u32 imm32_lo)
{
	u64 imm64 = ((u64)imm32_hi << 32) | (u32)imm32_lo;
	u8 *prog = *pprog;

	if (is_uimm32(imm64)) {
		/*
		 * For emitting plain u32, where sign bit must not be
		 * propagated LLVM tends to load imm64 over mov32
		 * directly, so save couple of bytes by just doing
		 * 'mov %eax, imm32' instead.
		 */
		emit_mov_imm32(&prog, false, dst_reg, imm32_lo);
	} else if (is_simm32(imm64)) {
		emit_mov_imm32(&prog, true, dst_reg, imm32_lo);
	} else {
		/* movabsq rax, imm64 */
		EMIT2(add_1mod(0x48, dst_reg), add_1reg(0xB8, dst_reg));
		EMIT(imm32_lo, 4);
		EMIT(imm32_hi, 4);
	}

	*pprog = prog;
}

static void emit_mov_reg(u8 **pprog, bool is64, u32 dst_reg, u32 src_reg)
{
	u8 *prog = *pprog;

	if (is64) {
		/* mov dst, src */
		EMIT_mov(dst_reg, src_reg);
	} else {
		/* mov32 dst, src */
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT1(add_2mod(0x40, dst_reg, src_reg));
		EMIT2(0x89, add_2reg(0xC0, dst_reg, src_reg));
	}

	*pprog = prog;
}

static void emit_movsx_reg(u8 **pprog, int num_bits, bool is64, u32 dst_reg,
			   u32 src_reg)
{
	u8 *prog = *pprog;

	if (is64) {
		/* movs[b,w,l]q dst, src */
		if (num_bits == 8)
			EMIT4(add_2mod(0x48, src_reg, dst_reg), 0x0f, 0xbe,
			      add_2reg(0xC0, src_reg, dst_reg));
		else if (num_bits == 16)
			EMIT4(add_2mod(0x48, src_reg, dst_reg), 0x0f, 0xbf,
			      add_2reg(0xC0, src_reg, dst_reg));
		else if (num_bits == 32)
			EMIT3(add_2mod(0x48, src_reg, dst_reg), 0x63,
			      add_2reg(0xC0, src_reg, dst_reg));
	} else {
		/* movs[b,w]l dst, src */
		if (num_bits == 8) {
			EMIT4(add_2mod(0x40, src_reg, dst_reg), 0x0f, 0xbe,
			      add_2reg(0xC0, src_reg, dst_reg));
		} else if (num_bits == 16) {
			if (is_ereg(dst_reg) || is_ereg(src_reg))
				EMIT1(add_2mod(0x40, src_reg, dst_reg));
			EMIT3(add_2mod(0x0f, src_reg, dst_reg), 0xbf,
			      add_2reg(0xC0, src_reg, dst_reg));
		}
	}

	*pprog = prog;
}

/* Emit the suffix (ModR/M etc) for addressing *(ptr_reg + off) and val_reg */
static void emit_insn_suffix(u8 **pprog, u32 ptr_reg, u32 val_reg, int off)
{
	u8 *prog = *pprog;

	if (is_imm8(off)) {
		/* 1-byte signed displacement.
		 *
		 * If off == 0 we could skip this and save one extra byte, but
		 * special case of x86 R13 which always needs an offset is not
		 * worth the hassle
		 */
		EMIT2(add_2reg(0x40, ptr_reg, val_reg), off);
	} else {
		/* 4-byte signed displacement */
		EMIT1_off32(add_2reg(0x80, ptr_reg, val_reg), off);
	}
	*pprog = prog;
}

static void emit_insn_suffix_SIB(u8 **pprog, u32 ptr_reg, u32 val_reg, u32 index_reg, int off)
{
	u8 *prog = *pprog;

	if (is_imm8(off)) {
		EMIT3(add_2reg(0x44, BPF_REG_0, val_reg), add_2reg(0, ptr_reg, index_reg) /* SIB */, off);
	} else {
		EMIT2_off32(add_2reg(0x84, BPF_REG_0, val_reg), add_2reg(0, ptr_reg, index_reg) /* SIB */, off);
	}
	*pprog = prog;
}

/*
 * Emit a REX byte if it will be necessary to address these registers
 */
static void maybe_emit_mod(u8 **pprog, u32 dst_reg, u32 src_reg, bool is64)
{
	u8 *prog = *pprog;

	if (is64)
		EMIT1(add_2mod(0x48, dst_reg, src_reg));
	else if (is_ereg(dst_reg) || is_ereg(src_reg))
		EMIT1(add_2mod(0x40, dst_reg, src_reg));
	*pprog = prog;
}

/*
 * Similar version of maybe_emit_mod() for a single register
 */
static void maybe_emit_1mod(u8 **pprog, u32 reg, bool is64)
{
	u8 *prog = *pprog;

	if (is64)
		EMIT1(add_1mod(0x48, reg));
	else if (is_ereg(reg))
		EMIT1(add_1mod(0x40, reg));
	*pprog = prog;
}

/* LDX: dst_reg = *(u8*)(src_reg + off) */
static void emit_ldx(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* Emit 'movzx rax, byte ptr [rax + off]' */
		EMIT3(add_2mod(0x48, src_reg, dst_reg), 0x0F, 0xB6);
		break;
	case BPF_H:
		/* Emit 'movzx rax, word ptr [rax + off]' */
		EMIT3(add_2mod(0x48, src_reg, dst_reg), 0x0F, 0xB7);
		break;
	case BPF_W:
		/* Emit 'mov eax, dword ptr [rax+0x14]' */
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT2(add_2mod(0x40, src_reg, dst_reg), 0x8B);
		else
			EMIT1(0x8B);
		break;
	case BPF_DW:
		/* Emit 'mov rax, qword ptr [rax+0x14]' */
		EMIT2(add_2mod(0x48, src_reg, dst_reg), 0x8B);
		break;
	}
	emit_insn_suffix(&prog, src_reg, dst_reg, off);
	*pprog = prog;
}

/* LDSX: dst_reg = *(s8*)(src_reg + off) */
static void emit_ldsx(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* Emit 'movsx rax, byte ptr [rax + off]' */
		EMIT3(add_2mod(0x48, src_reg, dst_reg), 0x0F, 0xBE);
		break;
	case BPF_H:
		/* Emit 'movsx rax, word ptr [rax + off]' */
		EMIT3(add_2mod(0x48, src_reg, dst_reg), 0x0F, 0xBF);
		break;
	case BPF_W:
		/* Emit 'movsx rax, dword ptr [rax+0x14]' */
		EMIT2(add_2mod(0x48, src_reg, dst_reg), 0x63);
		break;
	}
	emit_insn_suffix(&prog, src_reg, dst_reg, off);
	*pprog = prog;
}

static void emit_ldx_index(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, u32 index_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* movzx rax, byte ptr [rax + r12 + off] */
		EMIT3(add_3mod(0x40, src_reg, dst_reg, index_reg), 0x0F, 0xB6);
		break;
	case BPF_H:
		/* movzx rax, word ptr [rax + r12 + off] */
		EMIT3(add_3mod(0x40, src_reg, dst_reg, index_reg), 0x0F, 0xB7);
		break;
	case BPF_W:
		/* mov eax, dword ptr [rax + r12 + off] */
		EMIT2(add_3mod(0x40, src_reg, dst_reg, index_reg), 0x8B);
		break;
	case BPF_DW:
		/* mov rax, qword ptr [rax + r12 + off] */
		EMIT2(add_3mod(0x48, src_reg, dst_reg, index_reg), 0x8B);
		break;
	}
	emit_insn_suffix_SIB(&prog, src_reg, dst_reg, index_reg, off);
	*pprog = prog;
}

static void emit_ldx_r12(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	emit_ldx_index(pprog, size, dst_reg, src_reg, X86_REG_R12, off);
}

/* STX: *(u8*)(dst_reg + off) = src_reg */
static void emit_stx(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* Emit 'mov byte ptr [rax + off], al' */
		if (is_ereg(dst_reg) || is_ereg_8l(src_reg))
			/* Add extra byte for eregs or SIL,DIL,BPL in src_reg */
			EMIT2(add_2mod(0x40, dst_reg, src_reg), 0x88);
		else
			EMIT1(0x88);
		break;
	case BPF_H:
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT3(0x66, add_2mod(0x40, dst_reg, src_reg), 0x89);
		else
			EMIT2(0x66, 0x89);
		break;
	case BPF_W:
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT2(add_2mod(0x40, dst_reg, src_reg), 0x89);
		else
			EMIT1(0x89);
		break;
	case BPF_DW:
		EMIT2(add_2mod(0x48, dst_reg, src_reg), 0x89);
		break;
	}
	emit_insn_suffix(&prog, dst_reg, src_reg, off);
	*pprog = prog;
}

/* STX: *(u8*)(dst_reg + index_reg + off) = src_reg */
static void emit_stx_index(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, u32 index_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* mov byte ptr [rax + r12 + off], al */
		EMIT2(add_3mod(0x40, dst_reg, src_reg, index_reg), 0x88);
		break;
	case BPF_H:
		/* mov word ptr [rax + r12 + off], ax */
		EMIT3(0x66, add_3mod(0x40, dst_reg, src_reg, index_reg), 0x89);
		break;
	case BPF_W:
		/* mov dword ptr [rax + r12 + 1], eax */
		EMIT2(add_3mod(0x40, dst_reg, src_reg, index_reg), 0x89);
		break;
	case BPF_DW:
		/* mov qword ptr [rax + r12 + 1], rax */
		EMIT2(add_3mod(0x48, dst_reg, src_reg, index_reg), 0x89);
		break;
	}
	emit_insn_suffix_SIB(&prog, dst_reg, src_reg, index_reg, off);
	*pprog = prog;
}

static void emit_stx_r12(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	emit_stx_index(pprog, size, dst_reg, src_reg, X86_REG_R12, off);
}

/* ST: *(u8*)(dst_reg + index_reg + off) = imm32 */
static void emit_st_index(u8 **pprog, u32 size, u32 dst_reg, u32 index_reg, int off, int imm)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* mov byte ptr [rax + r12 + off], imm8 */
		EMIT2(add_3mod(0x40, dst_reg, 0, index_reg), 0xC6);
		break;
	case BPF_H:
		/* mov word ptr [rax + r12 + off], imm16 */
		EMIT3(0x66, add_3mod(0x40, dst_reg, 0, index_reg), 0xC7);
		break;
	case BPF_W:
		/* mov dword ptr [rax + r12 + 1], imm32 */
		EMIT2(add_3mod(0x40, dst_reg, 0, index_reg), 0xC7);
		break;
	case BPF_DW:
		/* mov qword ptr [rax + r12 + 1], imm32 */
		EMIT2(add_3mod(0x48, dst_reg, 0, index_reg), 0xC7);
		break;
	}
	emit_insn_suffix_SIB(&prog, dst_reg, 0, index_reg, off);
	EMIT(imm, bpf_size_to_x86_bytes(size));
	*pprog = prog;
}

static void emit_st_r12(u8 **pprog, u32 size, u32 dst_reg, int off, int imm)
{
	emit_st_index(pprog, size, dst_reg, X86_REG_R12, off, imm);
}

static int emit_atomic(u8 **pprog, u8 atomic_op,
		       u32 dst_reg, u32 src_reg, s16 off, u8 bpf_size)
{
	u8 *prog = *pprog;

	EMIT1(0xF0); /* lock prefix */

	maybe_emit_mod(&prog, dst_reg, src_reg, bpf_size == BPF_DW);

	/* emit opcode */
	switch (atomic_op) {
	case BPF_ADD:
	case BPF_AND:
	case BPF_OR:
	case BPF_XOR:
		/* lock *(u32/u64*)(dst_reg + off) <op>= src_reg */
		EMIT1(simple_alu_opcodes[atomic_op]);
		break;
	case BPF_ADD | BPF_FETCH:
		/* src_reg = atomic_fetch_add(dst_reg + off, src_reg); */
		EMIT2(0x0F, 0xC1);
		break;
	case BPF_XCHG:
		/* src_reg = atomic_xchg(dst_reg + off, src_reg); */
		EMIT1(0x87);
		break;
	case BPF_CMPXCHG:
		/* r0 = atomic_cmpxchg(dst_reg + off, r0, src_reg); */
		EMIT2(0x0F, 0xB1);
		break;
	default:
		pr_err("bpf_jit: unknown atomic opcode %02x\n", atomic_op);
		return -EFAULT;
	}

	emit_insn_suffix(&prog, dst_reg, src_reg, off);

	*pprog = prog;
	return 0;
}

static int emit_atomic_index(u8 **pprog, u8 atomic_op, u32 size,
			     u32 dst_reg, u32 src_reg, u32 index_reg, int off)
{
	u8 *prog = *pprog;

	EMIT1(0xF0); /* lock prefix */
	switch (size) {
	case BPF_W:
		EMIT1(add_3mod(0x40, dst_reg, src_reg, index_reg));
		break;
	case BPF_DW:
		EMIT1(add_3mod(0x48, dst_reg, src_reg, index_reg));
		break;
	default:
		pr_err("bpf_jit: 1 and 2 byte atomics are not supported\n");
		return -EFAULT;
	}

	/* emit opcode */
	switch (atomic_op) {
	case BPF_ADD:
	case BPF_AND:
	case BPF_OR:
	case BPF_XOR:
		/* lock *(u32/u64*)(dst_reg + idx_reg + off) <op>= src_reg */
		EMIT1(simple_alu_opcodes[atomic_op]);
		break;
	case BPF_ADD | BPF_FETCH:
		/* src_reg = atomic_fetch_add(dst_reg + idx_reg + off, src_reg); */
		EMIT2(0x0F, 0xC1);
		break;
	case BPF_XCHG:
		/* src_reg = atomic_xchg(dst_reg + idx_reg + off, src_reg); */
		EMIT1(0x87);
		break;
	case BPF_CMPXCHG:
		/* r0 = atomic_cmpxchg(dst_reg + idx_reg + off, r0, src_reg); */
		EMIT2(0x0F, 0xB1);
		break;
	default:
		pr_err("bpf_jit: unknown atomic opcode %02x\n", atomic_op);
		return -EFAULT;
	}
	emit_insn_suffix_SIB(&prog, dst_reg, src_reg, index_reg, off);
	*pprog = prog;
	return 0;
}

#define DONT_CLEAR 1

bool ex_handler_bpf(const struct exception_table_entry *x, struct pt_regs *regs)
{
	u32 reg = x->fixup >> 8;

	/* jump over faulting load and clear dest register */
	if (reg != DONT_CLEAR)
		*(unsigned long *)((void *)regs + reg) = 0;
	regs->ip += x->fixup & 0xff;
	return true;
}

static void detect_reg_usage(struct bpf_insn *insn, int insn_cnt,
			     bool *regs_used)
{
	int i;

	for (i = 1; i <= insn_cnt; i++, insn++) {
		if (insn->dst_reg == BPF_REG_6 || insn->src_reg == BPF_REG_6)
			regs_used[0] = true;
		if (insn->dst_reg == BPF_REG_7 || insn->src_reg == BPF_REG_7)
			regs_used[1] = true;
		if (insn->dst_reg == BPF_REG_8 || insn->src_reg == BPF_REG_8)
			regs_used[2] = true;
		if (insn->dst_reg == BPF_REG_9 || insn->src_reg == BPF_REG_9)
			regs_used[3] = true;
	}
}

/* emit the 3-byte VEX prefix
 *
 * r: same as rex.r, extra bit for ModRM reg field
 * x: same as rex.x, extra bit for SIB index field
 * b: same as rex.b, extra bit for ModRM r/m, or SIB base
 * m: opcode map select, encoding escape bytes e.g. 0x0f38
 * w: same as rex.w (32 bit or 64 bit) or opcode specific
 * src_reg2: additional source reg (encoded as BPF reg)
 * l: vector length (128 bit or 256 bit) or reserved
 * pp: opcode prefix (none, 0x66, 0xf2 or 0xf3)
 */
static void emit_3vex(u8 **pprog, bool r, bool x, bool b, u8 m,
		      bool w, u8 src_reg2, bool l, u8 pp)
{
	u8 *prog = *pprog;
	const u8 b0 = 0xc4; /* first byte of 3-byte VEX prefix */
	u8 b1, b2;
	u8 vvvv = reg2hex[src_reg2];

	/* reg2hex gives only the lower 3 bit of vvvv */
	if (is_ereg(src_reg2))
		vvvv |= 1 << 3;

	/*
	 * 2nd byte of 3-byte VEX prefix
	 * ~ means bit inverted encoding
	 *
	 *    7                           0
	 *  +---+---+---+---+---+---+---+---+
	 *  |~R |~X |~B |         m         |
	 *  +---+---+---+---+---+---+---+---+
	 */
	b1 = (!r << 7) | (!x << 6) | (!b << 5) | (m & 0x1f);
	/*
	 * 3rd byte of 3-byte VEX prefix
	 *
	 *    7                           0
	 *  +---+---+---+---+---+---+---+---+
	 *  | W |     ~vvvv     | L |   pp  |
	 *  +---+---+---+---+---+---+---+---+
	 */
	b2 = (w << 7) | ((~vvvv & 0xf) << 3) | (l << 2) | (pp & 3);

	EMIT3(b0, b1, b2);
	*pprog = prog;
}

/* emit BMI2 shift instruction */
static void emit_shiftx(u8 **pprog, u32 dst_reg, u8 src_reg, bool is64, u8 op)
{
	u8 *prog = *pprog;
	bool r = is_ereg(dst_reg);
	u8 m = 2; /* escape code 0f38 */

	emit_3vex(&prog, r, false, r, m, is64, src_reg, false, op);
	EMIT2(0xf7, add_2reg(0xC0, dst_reg, dst_reg));
	*pprog = prog;
}

static void emit_priv_frame_ptr(u8 **pprog, void __percpu *priv_frame_ptr)
{
	u8 *prog = *pprog;

	/* movabs r9, priv_frame_ptr */
	emit_mov_imm64(&prog, X86_REG_R9, (__force long) priv_frame_ptr >> 32,
		       (u32) (__force long) priv_frame_ptr);

#ifdef CONFIG_SMP
	/* add <r9>, gs:[<off>] */
	EMIT2(0x65, 0x4c);
	EMIT3(0x03, 0x0c, 0x25);
	EMIT((u32)(unsigned long)&this_cpu_off, 4);
#endif

	*pprog = prog;
}

#define INSN_SZ_DIFF (((addrs[i] - addrs[i - 1]) - (prog - temp)))

#define __LOAD_TCC_PTR(off)			\
	EMIT3_off32(0x48, 0x8B, 0x85, off)
/* mov rax, qword ptr [rbp - rounded_stack_depth - 16] */
#define LOAD_TAIL_CALL_CNT_PTR(stack)				\
	__LOAD_TCC_PTR(BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack))

/* Memory size/value to protect private stack overflow/underflow */
#define PRIV_STACK_GUARD_SZ    8
#define PRIV_STACK_GUARD_VAL   0xEB9F12345678eb9fULL

static int do_jit(struct bpf_prog *bpf_prog, int *addrs, u8 *image, u8 *rw_image,
		  int oldproglen, struct jit_context *ctx, bool jmp_padding)
{
	bool tail_call_reachable = bpf_prog->aux->tail_call_reachable;
	struct bpf_insn *insn = bpf_prog->insnsi;
	bool callee_regs_used[4] = {};
	int insn_cnt = bpf_prog->len;
	bool seen_exit = false;
	u8 temp[BPF_MAX_INSN_SIZE + BPF_INSN_SAFETY];
	void __percpu *priv_frame_ptr = NULL;
	u64 arena_vm_start, user_vm_start;
	void __percpu *priv_stack_ptr;
	int i, excnt = 0;
	int ilen, proglen = 0;
	u8 *prog = temp;
	u32 stack_depth;
	int err;

	stack_depth = bpf_prog->aux->stack_depth;
	priv_stack_ptr = bpf_prog->aux->priv_stack_ptr;
	if (priv_stack_ptr) {
		priv_frame_ptr = priv_stack_ptr + PRIV_STACK_GUARD_SZ + round_up(stack_depth, 8);
		stack_depth = 0;
	}

	arena_vm_start = bpf_arena_get_kern_vm_start(bpf_prog->aux->arena);
	user_vm_start = bpf_arena_get_user_vm_start(bpf_prog->aux->arena);

	detect_reg_usage(insn, insn_cnt, callee_regs_used);

	emit_prologue(&prog, image, stack_depth,
		      bpf_prog_was_classic(bpf_prog), tail_call_reachable,
		      bpf_is_subprog(bpf_prog), bpf_prog->aux->exception_cb);
	/* Exception callback will clobber callee regs for its own use, and
	 * restore the original callee regs from main prog's stack frame.
	 */
	if (bpf_prog->aux->exception_boundary) {
		/* We also need to save r12, which is not mapped to any BPF
		 * register, as we throw after entry into the kernel, which may
		 * overwrite r12.
		 */
		push_r12(&prog);
		push_callee_regs(&prog, all_callee_regs_used);
	} else {
		if (arena_vm_start)
			push_r12(&prog);
		push_callee_regs(&prog, callee_regs_used);
	}
	if (arena_vm_start)
		emit_mov_imm64(&prog, X86_REG_R12,
			       arena_vm_start >> 32, (u32) arena_vm_start);

	if (priv_frame_ptr)
		emit_priv_frame_ptr(&prog, priv_frame_ptr);

	ilen = prog - temp;
	if (rw_image)
		memcpy(rw_image + proglen, temp, ilen);
	proglen += ilen;
	addrs[0] = proglen;
	prog = temp;

	for (i = 1; i <= insn_cnt; i++, insn++) {
		const s32 imm32 = insn->imm;
		u32 dst_reg = insn->dst_reg;
		u32 src_reg = insn->src_reg;
		u8 b2 = 0, b3 = 0;
		u8 *start_of_ldx;
		s64 jmp_offset;
		s16 insn_off;
		u8 jmp_cond;
		u8 *func;
		int nops;

		if (priv_frame_ptr) {
			if (src_reg == BPF_REG_FP)
				src_reg = X86_REG_R9;

			if (dst_reg == BPF_REG_FP)
				dst_reg = X86_REG_R9;
		}

		switch (insn->code) {
			/* ALU */
		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_OR | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU64 | BPF_ADD | BPF_X:
		case BPF_ALU64 | BPF_SUB | BPF_X:
		case BPF_ALU64 | BPF_AND | BPF_X:
		case BPF_ALU64 | BPF_OR | BPF_X:
		case BPF_ALU64 | BPF_XOR | BPF_X:
			maybe_emit_mod(&prog, dst_reg, src_reg,
				       BPF_CLASS(insn->code) == BPF_ALU64);
			b2 = simple_alu_opcodes[BPF_OP(insn->code)];
			EMIT2(b2, add_2reg(0xC0, dst_reg, src_reg));
			break;

		case BPF_ALU64 | BPF_MOV | BPF_X:
			if (insn_is_cast_user(insn)) {
				if (dst_reg != src_reg)
					/* 32-bit mov */
					emit_mov_reg(&prog, false, dst_reg, src_reg);
				/* shl dst_reg, 32 */
				maybe_emit_1mod(&prog, dst_reg, true);
				EMIT3(0xC1, add_1reg(0xE0, dst_reg), 32);

				/* or dst_reg, user_vm_start */
				maybe_emit_1mod(&prog, dst_reg, true);
				if (is_axreg(dst_reg))
					EMIT1_off32(0x0D,  user_vm_start >> 32);
				else
					EMIT2_off32(0x81, add_1reg(0xC8, dst_reg),  user_vm_start >> 32);

				/* rol dst_reg, 32 */
				maybe_emit_1mod(&prog, dst_reg, true);
				EMIT3(0xC1, add_1reg(0xC0, dst_reg), 32);

				/* xor r11, r11 */
				EMIT3(0x4D, 0x31, 0xDB);

				/* test dst_reg32, dst_reg32; check if lower 32-bit are zero */
				maybe_emit_mod(&prog, dst_reg, dst_reg, false);
				EMIT2(0x85, add_2reg(0xC0, dst_reg, dst_reg));

				/* cmove r11, dst_reg; if so, set dst_reg to zero */
				/* WARNING: Intel swapped src/dst register encoding in CMOVcc !!! */
				maybe_emit_mod(&prog, AUX_REG, dst_reg, true);
				EMIT3(0x0F, 0x44, add_2reg(0xC0, AUX_REG, dst_reg));
				break;
			} else if (insn_is_mov_percpu_addr(insn)) {
				/* mov <dst>, <src> (if necessary) */
				EMIT_mov(dst_reg, src_reg);
#ifdef CONFIG_SMP
				/* add <dst>, gs:[<off>] */
				EMIT2(0x65, add_1mod(0x48, dst_reg));
				EMIT3(0x03, add_2reg(0x04, 0, dst_reg), 0x25);
				EMIT((u32)(unsigned long)&this_cpu_off, 4);
#endif
				break;
			}
			fallthrough;
		case BPF_ALU | BPF_MOV | BPF_X:
			if (insn->off == 0)
				emit_mov_reg(&prog,
					     BPF_CLASS(insn->code) == BPF_ALU64,
					     dst_reg, src_reg);
			else
				emit_movsx_reg(&prog, insn->off,
					       BPF_CLASS(insn->code) == BPF_ALU64,
					       dst_reg, src_reg);
			break;

			/* neg dst */
		case BPF_ALU | BPF_NEG:
		case BPF_ALU64 | BPF_NEG:
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_ALU64);
			EMIT2(0xF7, add_1reg(0xD8, dst_reg));
			break;

		case BPF_ALU | BPF_ADD | BPF_K:
		case BPF_ALU | BPF_SUB | BPF_K:
		case BPF_ALU | BPF_AND | BPF_K:
		case BPF_ALU | BPF_OR | BPF_K:
		case BPF_ALU | BPF_XOR | BPF_K:
		case BPF_ALU64 | BPF_ADD | BPF_K:
		case BPF_ALU64 | BPF_SUB | BPF_K:
		case BPF_ALU64 | BPF_AND | BPF_K:
		case BPF_ALU64 | BPF_OR | BPF_K:
		case BPF_ALU64 | BPF_XOR | BPF_K:
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_ALU64);

			/*
			 * b3 holds 'normal' opcode, b2 short form only valid
			 * in case dst is eax/rax.
			 */
			switch (BPF_OP(insn->code)) {
			case BPF_ADD:
				b3 = 0xC0;
				b2 = 0x05;
				break;
			case BPF_SUB:
				b3 = 0xE8;
				b2 = 0x2D;
				break;
			case BPF_AND:
				b3 = 0xE0;
				b2 = 0x25;
				break;
			case BPF_OR:
				b3 = 0xC8;
				b2 = 0x0D;
				break;
			case BPF_XOR:
				b3 = 0xF0;
				b2 = 0x35;
				break;
			}

			if (is_imm8(imm32))
				EMIT3(0x83, add_1reg(b3, dst_reg), imm32);
			else if (is_axreg(dst_reg))
				EMIT1_off32(b2, imm32);
			else
				EMIT2_off32(0x81, add_1reg(b3, dst_reg), imm32);
			break;

		case BPF_ALU64 | BPF_MOV | BPF_K:
		case BPF_ALU | BPF_MOV | BPF_K:
			emit_mov_imm32(&prog, BPF_CLASS(insn->code) == BPF_ALU64,
				       dst_reg, imm32);
			break;

		case BPF_LD | BPF_IMM | BPF_DW:
			emit_mov_imm64(&prog, dst_reg, insn[1].imm, insn[0].imm);
			insn++;
			i++;
			break;

			/* dst %= src, dst /= src, dst %= imm32, dst /= imm32 */
		case BPF_ALU | BPF_MOD | BPF_X:
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
		case BPF_ALU | BPF_DIV | BPF_K:
		case BPF_ALU64 | BPF_MOD | BPF_X:
		case BPF_ALU64 | BPF_DIV | BPF_X:
		case BPF_ALU64 | BPF_MOD | BPF_K:
		case BPF_ALU64 | BPF_DIV | BPF_K: {
			bool is64 = BPF_CLASS(insn->code) == BPF_ALU64;

			if (dst_reg != BPF_REG_0)
				EMIT1(0x50); /* push rax */
			if (dst_reg != BPF_REG_3)
				EMIT1(0x52); /* push rdx */

			if (BPF_SRC(insn->code) == BPF_X) {
				if (src_reg == BPF_REG_0 ||
				    src_reg == BPF_REG_3) {
					/* mov r11, src_reg */
					EMIT_mov(AUX_REG, src_reg);
					src_reg = AUX_REG;
				}
			} else {
				/* mov r11, imm32 */
				EMIT3_off32(0x49, 0xC7, 0xC3, imm32);
				src_reg = AUX_REG;
			}

			if (dst_reg != BPF_REG_0)
				/* mov rax, dst_reg */
				emit_mov_reg(&prog, is64, BPF_REG_0, dst_reg);

			if (insn->off == 0) {
				/*
				 * xor edx, edx
				 * equivalent to 'xor rdx, rdx', but one byte less
				 */
				EMIT2(0x31, 0xd2);

				/* div src_reg */
				maybe_emit_1mod(&prog, src_reg, is64);
				EMIT2(0xF7, add_1reg(0xF0, src_reg));
			} else {
				if (BPF_CLASS(insn->code) == BPF_ALU)
					EMIT1(0x99); /* cdq */
				else
					EMIT2(0x48, 0x99); /* cqo */

				/* idiv src_reg */
				maybe_emit_1mod(&prog, src_reg, is64);
				EMIT2(0xF7, add_1reg(0xF8, src_reg));
			}

			if (BPF_OP(insn->code) == BPF_MOD &&
			    dst_reg != BPF_REG_3)
				/* mov dst_reg, rdx */
				emit_mov_reg(&prog, is64, dst_reg, BPF_REG_3);
			else if (BPF_OP(insn->code) == BPF_DIV &&
				 dst_reg != BPF_REG_0)
				/* mov dst_reg, rax */
				emit_mov_reg(&prog, is64, dst_reg, BPF_REG_0);

			if (dst_reg != BPF_REG_3)
				EMIT1(0x5A); /* pop rdx */
			if (dst_reg != BPF_REG_0)
				EMIT1(0x58); /* pop rax */
			break;
		}

		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU64 | BPF_MUL | BPF_K:
			maybe_emit_mod(&prog, dst_reg, dst_reg,
				       BPF_CLASS(insn->code) == BPF_ALU64);

			if (is_imm8(imm32))
				/* imul dst_reg, dst_reg, imm8 */
				EMIT3(0x6B, add_2reg(0xC0, dst_reg, dst_reg),
				      imm32);
			else
				/* imul dst_reg, dst_reg, imm32 */
				EMIT2_off32(0x69,
					    add_2reg(0xC0, dst_reg, dst_reg),
					    imm32);
			break;

		case BPF_ALU | BPF_MUL | BPF_X:
		case BPF_ALU64 | BPF_MUL | BPF_X:
			maybe_emit_mod(&prog, src_reg, dst_reg,
				       BPF_CLASS(insn->code) == BPF_ALU64);

			/* imul dst_reg, src_reg */
			EMIT3(0x0F, 0xAF, add_2reg(0xC0, src_reg, dst_reg));
			break;

			/* Shifts */
		case BPF_ALU | BPF_LSH | BPF_K:
		case BPF_ALU | BPF_RSH | BPF_K:
		case BPF_ALU | BPF_ARSH | BPF_K:
		case BPF_ALU64 | BPF_LSH | BPF_K:
		case BPF_ALU64 | BPF_RSH | BPF_K:
		case BPF_ALU64 | BPF_ARSH | BPF_K:
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_ALU64);

			b3 = simple_alu_opcodes[BPF_OP(insn->code)];
			if (imm32 == 1)
				EMIT2(0xD1, add_1reg(b3, dst_reg));
			else
				EMIT3(0xC1, add_1reg(b3, dst_reg), imm32);
			break;

		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_ARSH | BPF_X:
		case BPF_ALU64 | BPF_LSH | BPF_X:
		case BPF_ALU64 | BPF_RSH | BPF_X:
		case BPF_ALU64 | BPF_ARSH | BPF_X:
			/* BMI2 shifts aren't better when shift count is already in rcx */
			if (boot_cpu_has(X86_FEATURE_BMI2) && src_reg != BPF_REG_4) {
				/* shrx/sarx/shlx dst_reg, dst_reg, src_reg */
				bool w = (BPF_CLASS(insn->code) == BPF_ALU64);
				u8 op;

				switch (BPF_OP(insn->code)) {
				case BPF_LSH:
					op = 1; /* prefix 0x66 */
					break;
				case BPF_RSH:
					op = 3; /* prefix 0xf2 */
					break;
				case BPF_ARSH:
					op = 2; /* prefix 0xf3 */
					break;
				}

				emit_shiftx(&prog, dst_reg, src_reg, w, op);

				break;
			}

			if (src_reg != BPF_REG_4) { /* common case */
				/* Check for bad case when dst_reg == rcx */
				if (dst_reg == BPF_REG_4) {
					/* mov r11, dst_reg */
					EMIT_mov(AUX_REG, dst_reg);
					dst_reg = AUX_REG;
				} else {
					EMIT1(0x51); /* push rcx */
				}
				/* mov rcx, src_reg */
				EMIT_mov(BPF_REG_4, src_reg);
			}

			/* shl %rax, %cl | shr %rax, %cl | sar %rax, %cl */
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_ALU64);

			b3 = simple_alu_opcodes[BPF_OP(insn->code)];
			EMIT2(0xD3, add_1reg(b3, dst_reg));

			if (src_reg != BPF_REG_4) {
				if (insn->dst_reg == BPF_REG_4)
					/* mov dst_reg, r11 */
					EMIT_mov(insn->dst_reg, AUX_REG);
				else
					EMIT1(0x59); /* pop rcx */
			}

			break;

		case BPF_ALU | BPF_END | BPF_FROM_BE:
		case BPF_ALU64 | BPF_END | BPF_FROM_LE:
			switch (imm32) {
			case 16:
				/* Emit 'ror %ax, 8' to swap lower 2 bytes */
				EMIT1(0x66);
				if (is_ereg(dst_reg))
					EMIT1(0x41);
				EMIT3(0xC1, add_1reg(0xC8, dst_reg), 8);

				/* Emit 'movzwl eax, ax' */
				if (is_ereg(dst_reg))
					EMIT3(0x45, 0x0F, 0xB7);
				else
					EMIT2(0x0F, 0xB7);
				EMIT1(add_2reg(0xC0, dst_reg, dst_reg));
				break;
			case 32:
				/* Emit 'bswap eax' to swap lower 4 bytes */
				if (is_ereg(dst_reg))
					EMIT2(0x41, 0x0F);
				else
					EMIT1(0x0F);
				EMIT1(add_1reg(0xC8, dst_reg));
				break;
			case 64:
				/* Emit 'bswap rax' to swap 8 bytes */
				EMIT3(add_1mod(0x48, dst_reg), 0x0F,
				      add_1reg(0xC8, dst_reg));
				break;
			}
			break;

		case BPF_ALU | BPF_END | BPF_FROM_LE:
			switch (imm32) {
			case 16:
				/*
				 * Emit 'movzwl eax, ax' to zero extend 16-bit
				 * into 64 bit
				 */
				if (is_ereg(dst_reg))
					EMIT3(0x45, 0x0F, 0xB7);
				else
					EMIT2(0x0F, 0xB7);
				EMIT1(add_2reg(0xC0, dst_reg, dst_reg));
				break;
			case 32:
				/* Emit 'mov eax, eax' to clear upper 32-bits */
				if (is_ereg(dst_reg))
					EMIT1(0x45);
				EMIT2(0x89, add_2reg(0xC0, dst_reg, dst_reg));
				break;
			case 64:
				/* nop */
				break;
			}
			break;

			/* speculation barrier */
		case BPF_ST | BPF_NOSPEC:
			EMIT_LFENCE();
			break;

			/* ST: *(u8*)(dst_reg + off) = imm */
		case BPF_ST | BPF_MEM | BPF_B:
			if (is_ereg(dst_reg))
				EMIT2(0x41, 0xC6);
			else
				EMIT1(0xC6);
			goto st;
		case BPF_ST | BPF_MEM | BPF_H:
			if (is_ereg(dst_reg))
				EMIT3(0x66, 0x41, 0xC7);
			else
				EMIT2(0x66, 0xC7);
			goto st;
		case BPF_ST | BPF_MEM | BPF_W:
			if (is_ereg(dst_reg))
				EMIT2(0x41, 0xC7);
			else
				EMIT1(0xC7);
			goto st;
		case BPF_ST | BPF_MEM | BPF_DW:
			EMIT2(add_1mod(0x48, dst_reg), 0xC7);

st:			if (is_imm8(insn->off))
				EMIT2(add_1reg(0x40, dst_reg), insn->off);
			else
				EMIT1_off32(add_1reg(0x80, dst_reg), insn->off);

			EMIT(imm32, bpf_size_to_x86_bytes(BPF_SIZE(insn->code)));
			break;

			/* STX: *(u8*)(dst_reg + off) = src_reg */
		case BPF_STX | BPF_MEM | BPF_B:
		case BPF_STX | BPF_MEM | BPF_H:
		case BPF_STX | BPF_MEM | BPF_W:
		case BPF_STX | BPF_MEM | BPF_DW:
			emit_stx(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
			break;

		case BPF_ST | BPF_PROBE_MEM32 | BPF_B:
		case BPF_ST | BPF_PROBE_MEM32 | BPF_H:
		case BPF_ST | BPF_PROBE_MEM32 | BPF_W:
		case BPF_ST | BPF_PROBE_MEM32 | BPF_DW:
			start_of_ldx = prog;
			emit_st_r12(&prog, BPF_SIZE(insn->code), dst_reg, insn->off, insn->imm);
			goto populate_extable;

			/* LDX: dst_reg = *(u8*)(src_reg + r12 + off) */
		case BPF_LDX | BPF_PROBE_MEM32 | BPF_B:
		case BPF_LDX | BPF_PROBE_MEM32 | BPF_H:
		case BPF_LDX | BPF_PROBE_MEM32 | BPF_W:
		case BPF_LDX | BPF_PROBE_MEM32 | BPF_DW:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_B:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_H:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_W:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_DW:
			start_of_ldx = prog;
			if (BPF_CLASS(insn->code) == BPF_LDX)
				emit_ldx_r12(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
			else
				emit_stx_r12(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
populate_extable:
			{
				struct exception_table_entry *ex;
				u8 *_insn = image + proglen + (start_of_ldx - temp);
				s64 delta;

				if (!bpf_prog->aux->extable)
					break;

				if (excnt >= bpf_prog->aux->num_exentries) {
					pr_err("mem32 extable bug\n");
					return -EFAULT;
				}
				ex = &bpf_prog->aux->extable[excnt++];

				delta = _insn - (u8 *)&ex->insn;
				/* switch ex to rw buffer for writes */
				ex = (void *)rw_image + ((void *)ex - (void *)image);

				ex->insn = delta;

				ex->data = EX_TYPE_BPF;

				ex->fixup = (prog - start_of_ldx) |
					((BPF_CLASS(insn->code) == BPF_LDX ? reg2pt_regs[dst_reg] : DONT_CLEAR) << 8);
			}
			break;

			/* LDX: dst_reg = *(u8*)(src_reg + off) */
		case BPF_LDX | BPF_MEM | BPF_B:
		case BPF_LDX | BPF_PROBE_MEM | BPF_B:
		case BPF_LDX | BPF_MEM | BPF_H:
		case BPF_LDX | BPF_PROBE_MEM | BPF_H:
		case BPF_LDX | BPF_MEM | BPF_W:
		case BPF_LDX | BPF_PROBE_MEM | BPF_W:
		case BPF_LDX | BPF_MEM | BPF_DW:
		case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
			/* LDXS: dst_reg = *(s8*)(src_reg + off) */
		case BPF_LDX | BPF_MEMSX | BPF_B:
		case BPF_LDX | BPF_MEMSX | BPF_H:
		case BPF_LDX | BPF_MEMSX | BPF_W:
		case BPF_LDX | BPF_PROBE_MEMSX | BPF_B:
		case BPF_LDX | BPF_PROBE_MEMSX | BPF_H:
		case BPF_LDX | BPF_PROBE_MEMSX | BPF_W:
			insn_off = insn->off;

			if (BPF_MODE(insn->code) == BPF_PROBE_MEM ||
			    BPF_MODE(insn->code) == BPF_PROBE_MEMSX) {
				/* Conservatively check that src_reg + insn->off is a kernel address:
				 *   src_reg + insn->off > TASK_SIZE_MAX + PAGE_SIZE
				 *   and
				 *   src_reg + insn->off < VSYSCALL_ADDR
				 */

				u64 limit = TASK_SIZE_MAX + PAGE_SIZE - VSYSCALL_ADDR;
				u8 *end_of_jmp;

				/* movabsq r10, VSYSCALL_ADDR */
				emit_mov_imm64(&prog, BPF_REG_AX, (long)VSYSCALL_ADDR >> 32,
					       (u32)(long)VSYSCALL_ADDR);

				/* mov src_reg, r11 */
				EMIT_mov(AUX_REG, src_reg);

				if (insn->off) {
					/* add r11, insn->off */
					maybe_emit_1mod(&prog, AUX_REG, true);
					EMIT2_off32(0x81, add_1reg(0xC0, AUX_REG), insn->off);
				}

				/* sub r11, r10 */
				maybe_emit_mod(&prog, AUX_REG, BPF_REG_AX, true);
				EMIT2(0x29, add_2reg(0xC0, AUX_REG, BPF_REG_AX));

				/* movabsq r10, limit */
				emit_mov_imm64(&prog, BPF_REG_AX, (long)limit >> 32,
					       (u32)(long)limit);

				/* cmp r10, r11 */
				maybe_emit_mod(&prog, AUX_REG, BPF_REG_AX, true);
				EMIT2(0x39, add_2reg(0xC0, AUX_REG, BPF_REG_AX));

				/* if unsigned '>', goto load */
				EMIT2(X86_JA, 0);
				end_of_jmp = prog;

				/* xor dst_reg, dst_reg */
				emit_mov_imm32(&prog, false, dst_reg, 0);
				/* jmp byte_after_ldx */
				EMIT2(0xEB, 0);

				/* populate jmp_offset for JAE above to jump to start_of_ldx */
				start_of_ldx = prog;
				end_of_jmp[-1] = start_of_ldx - end_of_jmp;
			}
			if (BPF_MODE(insn->code) == BPF_PROBE_MEMSX ||
			    BPF_MODE(insn->code) == BPF_MEMSX)
				emit_ldsx(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn_off);
			else
				emit_ldx(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn_off);
			if (BPF_MODE(insn->code) == BPF_PROBE_MEM ||
			    BPF_MODE(insn->code) == BPF_PROBE_MEMSX) {
				struct exception_table_entry *ex;
				u8 *_insn = image + proglen + (start_of_ldx - temp);
				s64 delta;

				/* populate jmp_offset for JMP above */
				start_of_ldx[-1] = prog - start_of_ldx;

				if (!bpf_prog->aux->extable)
					break;

				if (excnt >= bpf_prog->aux->num_exentries) {
					pr_err("ex gen bug\n");
					return -EFAULT;
				}
				ex = &bpf_prog->aux->extable[excnt++];

				delta = _insn - (u8 *)&ex->insn;
				if (!is_simm32(delta)) {
					pr_err("extable->insn doesn't fit into 32-bit\n");
					return -EFAULT;
				}
				/* switch ex to rw buffer for writes */
				ex = (void *)rw_image + ((void *)ex - (void *)image);

				ex->insn = delta;

				ex->data = EX_TYPE_BPF;

				if (dst_reg > BPF_REG_9) {
					pr_err("verifier error\n");
					return -EFAULT;
				}
				/*
				 * Compute size of x86 insn and its target dest x86 register.
				 * ex_handler_bpf() will use lower 8 bits to adjust
				 * pt_regs->ip to jump over this x86 instruction
				 * and upper bits to figure out which pt_regs to zero out.
				 * End result: x86 insn "mov rbx, qword ptr [rax+0x14]"
				 * of 4 bytes will be ignored and rbx will be zero inited.
				 */
				ex->fixup = (prog - start_of_ldx) | (reg2pt_regs[dst_reg] << 8);
			}
			break;

		case BPF_STX | BPF_ATOMIC | BPF_W:
		case BPF_STX | BPF_ATOMIC | BPF_DW:
			if (insn->imm == (BPF_AND | BPF_FETCH) ||
			    insn->imm == (BPF_OR | BPF_FETCH) ||
			    insn->imm == (BPF_XOR | BPF_FETCH)) {
				bool is64 = BPF_SIZE(insn->code) == BPF_DW;
				u32 real_src_reg = src_reg;
				u32 real_dst_reg = dst_reg;
				u8 *branch_target;

				/*
				 * Can't be implemented with a single x86 insn.
				 * Need to do a CMPXCHG loop.
				 */

				/* Will need RAX as a CMPXCHG operand so save R0 */
				emit_mov_reg(&prog, true, BPF_REG_AX, BPF_REG_0);
				if (src_reg == BPF_REG_0)
					real_src_reg = BPF_REG_AX;
				if (dst_reg == BPF_REG_0)
					real_dst_reg = BPF_REG_AX;

				branch_target = prog;
				/* Load old value */
				emit_ldx(&prog, BPF_SIZE(insn->code),
					 BPF_REG_0, real_dst_reg, insn->off);
				/*
				 * Perform the (commutative) operation locally,
				 * put the result in the AUX_REG.
				 */
				emit_mov_reg(&prog, is64, AUX_REG, BPF_REG_0);
				maybe_emit_mod(&prog, AUX_REG, real_src_reg, is64);
				EMIT2(simple_alu_opcodes[BPF_OP(insn->imm)],
				      add_2reg(0xC0, AUX_REG, real_src_reg));
				/* Attempt to swap in new value */
				err = emit_atomic(&prog, BPF_CMPXCHG,
						  real_dst_reg, AUX_REG,
						  insn->off,
						  BPF_SIZE(insn->code));
				if (WARN_ON(err))
					return err;
				/*
				 * ZF tells us whether we won the race. If it's
				 * cleared we need to try again.
				 */
				EMIT2(X86_JNE, -(prog - branch_target) - 2);
				/* Return the pre-modification value */
				emit_mov_reg(&prog, is64, real_src_reg, BPF_REG_0);
				/* Restore R0 after clobbering RAX */
				emit_mov_reg(&prog, true, BPF_REG_0, BPF_REG_AX);
				break;
			}

			err = emit_atomic(&prog, insn->imm, dst_reg, src_reg,
					  insn->off, BPF_SIZE(insn->code));
			if (err)
				return err;
			break;

		case BPF_STX | BPF_PROBE_ATOMIC | BPF_W:
		case BPF_STX | BPF_PROBE_ATOMIC | BPF_DW:
			start_of_ldx = prog;
			err = emit_atomic_index(&prog, insn->imm, BPF_SIZE(insn->code),
						dst_reg, src_reg, X86_REG_R12, insn->off);
			if (err)
				return err;
			goto populate_extable;

			/* call */
		case BPF_JMP | BPF_CALL: {
			u8 *ip = image + addrs[i - 1];

			func = (u8 *) __bpf_call_base + imm32;
			if (src_reg == BPF_PSEUDO_CALL && tail_call_reachable) {
				LOAD_TAIL_CALL_CNT_PTR(stack_depth);
				ip += 7;
			}
			if (!imm32)
				return -EINVAL;
			if (priv_frame_ptr) {
				push_r9(&prog);
				ip += 2;
			}
			ip += x86_call_depth_emit_accounting(&prog, func, ip);
			if (emit_call(&prog, func, ip))
				return -EINVAL;
			if (priv_frame_ptr)
				pop_r9(&prog);
			break;
		}

		case BPF_JMP | BPF_TAIL_CALL:
			if (imm32)
				emit_bpf_tail_call_direct(bpf_prog,
							  &bpf_prog->aux->poke_tab[imm32 - 1],
							  &prog, image + addrs[i - 1],
							  callee_regs_used,
							  stack_depth,
							  ctx);
			else
				emit_bpf_tail_call_indirect(bpf_prog,
							    &prog,
							    callee_regs_used,
							    stack_depth,
							    image + addrs[i - 1],
							    ctx);
			break;

			/* cond jump */
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JNE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JLT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JLE | BPF_X:
		case BPF_JMP | BPF_JSGT | BPF_X:
		case BPF_JMP | BPF_JSLT | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_X:
		case BPF_JMP | BPF_JSLE | BPF_X:
		case BPF_JMP32 | BPF_JEQ | BPF_X:
		case BPF_JMP32 | BPF_JNE | BPF_X:
		case BPF_JMP32 | BPF_JGT | BPF_X:
		case BPF_JMP32 | BPF_JLT | BPF_X:
		case BPF_JMP32 | BPF_JGE | BPF_X:
		case BPF_JMP32 | BPF_JLE | BPF_X:
		case BPF_JMP32 | BPF_JSGT | BPF_X:
		case BPF_JMP32 | BPF_JSLT | BPF_X:
		case BPF_JMP32 | BPF_JSGE | BPF_X:
		case BPF_JMP32 | BPF_JSLE | BPF_X:
			/* cmp dst_reg, src_reg */
			maybe_emit_mod(&prog, dst_reg, src_reg,
				       BPF_CLASS(insn->code) == BPF_JMP);
			EMIT2(0x39, add_2reg(0xC0, dst_reg, src_reg));
			goto emit_cond_jmp;

		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP32 | BPF_JSET | BPF_X:
			/* test dst_reg, src_reg */
			maybe_emit_mod(&prog, dst_reg, src_reg,
				       BPF_CLASS(insn->code) == BPF_JMP);
			EMIT2(0x85, add_2reg(0xC0, dst_reg, src_reg));
			goto emit_cond_jmp;

		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP32 | BPF_JSET | BPF_K:
			/* test dst_reg, imm32 */
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_JMP);
			EMIT2_off32(0xF7, add_1reg(0xC0, dst_reg), imm32);
			goto emit_cond_jmp;

		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JNE | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JLT | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JLE | BPF_K:
		case BPF_JMP | BPF_JSGT | BPF_K:
		case BPF_JMP | BPF_JSLT | BPF_K:
		case BPF_JMP | BPF_JSGE | BPF_K:
		case BPF_JMP | BPF_JSLE | BPF_K:
		case BPF_JMP32 | BPF_JEQ | BPF_K:
		case BPF_JMP32 | BPF_JNE | BPF_K:
		case BPF_JMP32 | BPF_JGT | BPF_K:
		case BPF_JMP32 | BPF_JLT | BPF_K:
		case BPF_JMP32 | BPF_JGE | BPF_K:
		case BPF_JMP32 | BPF_JLE | BPF_K:
		case BPF_JMP32 | BPF_JSGT | BPF_K:
		case BPF_JMP32 | BPF_JSLT | BPF_K:
		case BPF_JMP32 | BPF_JSGE | BPF_K:
		case BPF_JMP32 | BPF_JSLE | BPF_K:
			/* test dst_reg, dst_reg to save one extra byte */
			if (imm32 == 0) {
				maybe_emit_mod(&prog, dst_reg, dst_reg,
					       BPF_CLASS(insn->code) == BPF_JMP);
				EMIT2(0x85, add_2reg(0xC0, dst_reg, dst_reg));
				goto emit_cond_jmp;
			}

			/* cmp dst_reg, imm8/32 */
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_JMP);

			if (is_imm8(imm32))
				EMIT3(0x83, add_1reg(0xF8, dst_reg), imm32);
			else
				EMIT2_off32(0x81, add_1reg(0xF8, dst_reg), imm32);

emit_cond_jmp:		/* Convert BPF opcode to x86 */
			switch (BPF_OP(insn->code)) {
			case BPF_JEQ:
				jmp_cond = X86_JE;
				break;
			case BPF_JSET:
			case BPF_JNE:
				jmp_cond = X86_JNE;
				break;
			case BPF_JGT:
				/* GT is unsigned '>', JA in x86 */
				jmp_cond = X86_JA;
				break;
			case BPF_JLT:
				/* LT is unsigned '<', JB in x86 */
				jmp_cond = X86_JB;
				break;
			case BPF_JGE:
				/* GE is unsigned '>=', JAE in x86 */
				jmp_cond = X86_JAE;
				break;
			case BPF_JLE:
				/* LE is unsigned '<=', JBE in x86 */
				jmp_cond = X86_JBE;
				break;
			case BPF_JSGT:
				/* Signed '>', GT in x86 */
				jmp_cond = X86_JG;
				break;
			case BPF_JSLT:
				/* Signed '<', LT in x86 */
				jmp_cond = X86_JL;
				break;
			case BPF_JSGE:
				/* Signed '>=', GE in x86 */
				jmp_cond = X86_JGE;
				break;
			case BPF_JSLE:
				/* Signed '<=', LE in x86 */
				jmp_cond = X86_JLE;
				break;
			default: /* to silence GCC warning */
				return -EFAULT;
			}
			jmp_offset = addrs[i + insn->off] - addrs[i];
			if (is_imm8_jmp_offset(jmp_offset)) {
				if (jmp_padding) {
					/* To keep the jmp_offset valid, the extra bytes are
					 * padded before the jump insn, so we subtract the
					 * 2 bytes of jmp_cond insn from INSN_SZ_DIFF.
					 *
					 * If the previous pass already emits an imm8
					 * jmp_cond, then this BPF insn won't shrink, so
					 * "nops" is 0.
					 *
					 * On the other hand, if the previous pass emits an
					 * imm32 jmp_cond, the extra 4 bytes(*) is padded to
					 * keep the image from shrinking further.
					 *
					 * (*) imm32 jmp_cond is 6 bytes, and imm8 jmp_cond
					 *     is 2 bytes, so the size difference is 4 bytes.
					 */
					nops = INSN_SZ_DIFF - 2;
					if (nops != 0 && nops != 4) {
						pr_err("unexpected jmp_cond padding: %d bytes\n",
						       nops);
						return -EFAULT;
					}
					emit_nops(&prog, nops);
				}
				EMIT2(jmp_cond, jmp_offset);
			} else if (is_simm32(jmp_offset)) {
				EMIT2_off32(0x0F, jmp_cond + 0x10, jmp_offset);
			} else {
				pr_err("cond_jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}

			break;

		case BPF_JMP | BPF_JA:
		case BPF_JMP32 | BPF_JA:
			if (BPF_CLASS(insn->code) == BPF_JMP) {
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
			} else {
				if (insn->imm == -1)
					jmp_offset = -2;
				else
					jmp_offset = addrs[i + insn->imm] - addrs[i];
			}

			if (!jmp_offset) {
				/*
				 * If jmp_padding is enabled, the extra nops will
				 * be inserted. Otherwise, optimize out nop jumps.
				 */
				if (jmp_padding) {
					/* There are 3 possible conditions.
					 * (1) This BPF_JA is already optimized out in
					 *     the previous run, so there is no need
					 *     to pad any extra byte (0 byte).
					 * (2) The previous pass emits an imm8 jmp,
					 *     so we pad 2 bytes to match the previous
					 *     insn size.
					 * (3) Similarly, the previous pass emits an
					 *     imm32 jmp, and 5 bytes is padded.
					 */
					nops = INSN_SZ_DIFF;
					if (nops != 0 && nops != 2 && nops != 5) {
						pr_err("unexpected nop jump padding: %d bytes\n",
						       nops);
						return -EFAULT;
					}
					emit_nops(&prog, nops);
				}
				break;
			}
emit_jmp:
			if (is_imm8_jmp_offset(jmp_offset)) {
				if (jmp_padding) {
					/* To avoid breaking jmp_offset, the extra bytes
					 * are padded before the actual jmp insn, so
					 * 2 bytes is subtracted from INSN_SZ_DIFF.
					 *
					 * If the previous pass already emits an imm8
					 * jmp, there is nothing to pad (0 byte).
					 *
					 * If it emits an imm32 jmp (5 bytes) previously
					 * and now an imm8 jmp (2 bytes), then we pad
					 * (5 - 2 = 3) bytes to stop the image from
					 * shrinking further.
					 */
					nops = INSN_SZ_DIFF - 2;
					if (nops != 0 && nops != 3) {
						pr_err("unexpected jump padding: %d bytes\n",
						       nops);
						return -EFAULT;
					}
					emit_nops(&prog, INSN_SZ_DIFF - 2);
				}
				EMIT2(0xEB, jmp_offset);
			} else if (is_simm32(jmp_offset)) {
				EMIT1_off32(0xE9, jmp_offset);
			} else {
				pr_err("jmp gen bug %llx\n", jmp_offset);
				return -EFAULT;
			}
			break;

		case BPF_JMP | BPF_EXIT:
			if (seen_exit) {
				jmp_offset = ctx->cleanup_addr - addrs[i];
				goto emit_jmp;
			}
			seen_exit = true;
			/* Update cleanup_addr */
			ctx->cleanup_addr = proglen;
			if (bpf_prog->aux->exception_boundary) {
				pop_callee_regs(&prog, all_callee_regs_used);
				pop_r12(&prog);
			} else {
				pop_callee_regs(&prog, callee_regs_used);
				if (arena_vm_start)
					pop_r12(&prog);
			}
			EMIT1(0xC9);         /* leave */
			emit_return(&prog, image + addrs[i - 1] + (prog - temp));
			break;

		default:
			/*
			 * By design x86-64 JIT should support all BPF instructions.
			 * This error will be seen if new instruction was added
			 * to the interpreter, but not to the JIT, or if there is
			 * junk in bpf_prog.
			 */
			pr_err("bpf_jit: unknown opcode %02x\n", insn->code);
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
			memcpy(rw_image + proglen, temp, ilen);
		}
		proglen += ilen;
		addrs[i] = proglen;
		prog = temp;
	}

	if (image && excnt != bpf_prog->aux->num_exentries) {
		pr_err("extable is not populated\n");
		return -EFAULT;
	}
	return proglen;
}

static void clean_stack_garbage(const struct btf_func_model *m,
				u8 **pprog, int nr_stack_slots,
				int stack_size)
{
	int arg_size, off;
	u8 *prog;

	/* Generally speaking, the compiler will pass the arguments
	 * on-stack with "push" instruction, which will take 8-byte
	 * on the stack. In this case, there won't be garbage values
	 * while we copy the arguments from origin stack frame to current
	 * in BPF_DW.
	 *
	 * However, sometimes the compiler will only allocate 4-byte on
	 * the stack for the arguments. For now, this case will only
	 * happen if there is only one argument on-stack and its size
	 * not more than 4 byte. In this case, there will be garbage
	 * values on the upper 4-byte where we store the argument on
	 * current stack frame.
	 *
	 * arguments on origin stack:
	 *
	 * stack_arg_1(4-byte) xxx(4-byte)
	 *
	 * what we copy:
	 *
	 * stack_arg_1(8-byte): stack_arg_1(origin) xxx
	 *
	 * and the xxx is the garbage values which we should clean here.
	 */
	if (nr_stack_slots != 1)
		return;

	/* the size of the last argument */
	arg_size = m->arg_size[m->nr_args - 1];
	if (arg_size <= 4) {
		off = -(stack_size - 4);
		prog = *pprog;
		/* mov DWORD PTR [rbp + off], 0 */
		if (!is_imm8(off))
			EMIT2_off32(0xC7, 0x85, off);
		else
			EMIT3(0xC7, 0x45, off);
		EMIT(0, 4);
		*pprog = prog;
	}
}

/* get the count of the regs that are used to pass arguments */
static int get_nr_used_regs(const struct btf_func_model *m)
{
	int i, arg_regs, nr_used_regs = 0;

	for (i = 0; i < min_t(int, m->nr_args, MAX_BPF_FUNC_ARGS); i++) {
		arg_regs = (m->arg_size[i] + 7) / 8;
		if (nr_used_regs + arg_regs <= 6)
			nr_used_regs += arg_regs;

		if (nr_used_regs >= 6)
			break;
	}

	return nr_used_regs;
}

static void save_args(const struct btf_func_model *m, u8 **prog,
		      int stack_size, bool for_call_origin)
{
	int arg_regs, first_off = 0, nr_regs = 0, nr_stack_slots = 0;
	int i, j;

	/* Store function arguments to stack.
	 * For a function that accepts two pointers the sequence will be:
	 * mov QWORD PTR [rbp-0x10],rdi
	 * mov QWORD PTR [rbp-0x8],rsi
	 */
	for (i = 0; i < min_t(int, m->nr_args, MAX_BPF_FUNC_ARGS); i++) {
		arg_regs = (m->arg_size[i] + 7) / 8;

		/* According to the research of Yonghong, struct members
		 * should be all in register or all on the stack.
		 * Meanwhile, the compiler will pass the argument on regs
		 * if the remaining regs can hold the argument.
		 *
		 * Disorder of the args can happen. For example:
		 *
		 * struct foo_struct {
		 *     long a;
		 *     int b;
		 * };
		 * int foo(char, char, char, char, char, struct foo_struct,
		 *         char);
		 *
		 * the arg1-5,arg7 will be passed by regs, and arg6 will
		 * by stack.
		 */
		if (nr_regs + arg_regs > 6) {
			/* copy function arguments from origin stack frame
			 * into current stack frame.
			 *
			 * The starting address of the arguments on-stack
			 * is:
			 *   rbp + 8(push rbp) +
			 *   8(return addr of origin call) +
			 *   8(return addr of the caller)
			 * which means: rbp + 24
			 */
			for (j = 0; j < arg_regs; j++) {
				emit_ldx(prog, BPF_DW, BPF_REG_0, BPF_REG_FP,
					 nr_stack_slots * 8 + 0x18);
				emit_stx(prog, BPF_DW, BPF_REG_FP, BPF_REG_0,
					 -stack_size);

				if (!nr_stack_slots)
					first_off = stack_size;
				stack_size -= 8;
				nr_stack_slots++;
			}
		} else {
			/* Only copy the arguments on-stack to current
			 * 'stack_size' and ignore the regs, used to
			 * prepare the arguments on-stack for origin call.
			 */
			if (for_call_origin) {
				nr_regs += arg_regs;
				continue;
			}

			/* copy the arguments from regs into stack */
			for (j = 0; j < arg_regs; j++) {
				emit_stx(prog, BPF_DW, BPF_REG_FP,
					 nr_regs == 5 ? X86_REG_R9 : BPF_REG_1 + nr_regs,
					 -stack_size);
				stack_size -= 8;
				nr_regs++;
			}
		}
	}

	clean_stack_garbage(m, prog, nr_stack_slots, first_off);
}

static void restore_regs(const struct btf_func_model *m, u8 **prog,
			 int stack_size)
{
	int i, j, arg_regs, nr_regs = 0;

	/* Restore function arguments from stack.
	 * For a function that accepts two pointers the sequence will be:
	 * EMIT4(0x48, 0x8B, 0x7D, 0xF0); mov rdi,QWORD PTR [rbp-0x10]
	 * EMIT4(0x48, 0x8B, 0x75, 0xF8); mov rsi,QWORD PTR [rbp-0x8]
	 *
	 * The logic here is similar to what we do in save_args()
	 */
	for (i = 0; i < min_t(int, m->nr_args, MAX_BPF_FUNC_ARGS); i++) {
		arg_regs = (m->arg_size[i] + 7) / 8;
		if (nr_regs + arg_regs <= 6) {
			for (j = 0; j < arg_regs; j++) {
				emit_ldx(prog, BPF_DW,
					 nr_regs == 5 ? X86_REG_R9 : BPF_REG_1 + nr_regs,
					 BPF_REG_FP,
					 -stack_size);
				stack_size -= 8;
				nr_regs++;
			}
		} else {
			stack_size -= 8 * arg_regs;
		}

		if (nr_regs >= 6)
			break;
	}
}

static int invoke_bpf_prog(const struct btf_func_model *m, u8 **pprog,
			   struct bpf_tramp_link *l, int stack_size,
			   int run_ctx_off, bool save_ret,
			   void *image, void *rw_image)
{
	u8 *prog = *pprog;
	u8 *jmp_insn;
	int ctx_cookie_off = offsetof(struct bpf_tramp_run_ctx, bpf_cookie);
	struct bpf_prog *p = l->link.prog;
	u64 cookie = l->cookie;

	/* mov rdi, cookie */
	emit_mov_imm64(&prog, BPF_REG_1, (long) cookie >> 32, (u32) (long) cookie);

	/* Prepare struct bpf_tramp_run_ctx.
	 *
	 * bpf_tramp_run_ctx is already preserved by
	 * arch_prepare_bpf_trampoline().
	 *
	 * mov QWORD PTR [rbp - run_ctx_off + ctx_cookie_off], rdi
	 */
	emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_1, -run_ctx_off + ctx_cookie_off);

	/* arg1: mov rdi, progs[i] */
	emit_mov_imm64(&prog, BPF_REG_1, (long) p >> 32, (u32) (long) p);
	/* arg2: lea rsi, [rbp - ctx_cookie_off] */
	if (!is_imm8(-run_ctx_off))
		EMIT3_off32(0x48, 0x8D, 0xB5, -run_ctx_off);
	else
		EMIT4(0x48, 0x8D, 0x75, -run_ctx_off);

	if (emit_rsb_call(&prog, bpf_trampoline_enter(p), image + (prog - (u8 *)rw_image)))
		return -EINVAL;
	/* remember prog start time returned by __bpf_prog_enter */
	emit_mov_reg(&prog, true, BPF_REG_6, BPF_REG_0);

	/* if (__bpf_prog_enter*(prog) == 0)
	 *	goto skip_exec_of_prog;
	 */
	EMIT3(0x48, 0x85, 0xC0);  /* test rax,rax */
	/* emit 2 nops that will be replaced with JE insn */
	jmp_insn = prog;
	emit_nops(&prog, 2);

	/* arg1: lea rdi, [rbp - stack_size] */
	if (!is_imm8(-stack_size))
		EMIT3_off32(0x48, 0x8D, 0xBD, -stack_size);
	else
		EMIT4(0x48, 0x8D, 0x7D, -stack_size);
	/* arg2: progs[i]->insnsi for interpreter */
	if (!p->jited)
		emit_mov_imm64(&prog, BPF_REG_2,
			       (long) p->insnsi >> 32,
			       (u32) (long) p->insnsi);
	/* call JITed bpf program or interpreter */
	if (emit_rsb_call(&prog, p->bpf_func, image + (prog - (u8 *)rw_image)))
		return -EINVAL;

	/*
	 * BPF_TRAMP_MODIFY_RETURN trampolines can modify the return
	 * of the previous call which is then passed on the stack to
	 * the next BPF program.
	 *
	 * BPF_TRAMP_FENTRY trampoline may need to return the return
	 * value of BPF_PROG_TYPE_STRUCT_OPS prog.
	 */
	if (save_ret)
		emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -8);

	/* replace 2 nops with JE insn, since jmp target is known */
	jmp_insn[0] = X86_JE;
	jmp_insn[1] = prog - jmp_insn - 2;

	/* arg1: mov rdi, progs[i] */
	emit_mov_imm64(&prog, BPF_REG_1, (long) p >> 32, (u32) (long) p);
	/* arg2: mov rsi, rbx <- start time in nsec */
	emit_mov_reg(&prog, true, BPF_REG_2, BPF_REG_6);
	/* arg3: lea rdx, [rbp - run_ctx_off] */
	if (!is_imm8(-run_ctx_off))
		EMIT3_off32(0x48, 0x8D, 0x95, -run_ctx_off);
	else
		EMIT4(0x48, 0x8D, 0x55, -run_ctx_off);
	if (emit_rsb_call(&prog, bpf_trampoline_exit(p), image + (prog - (u8 *)rw_image)))
		return -EINVAL;

	*pprog = prog;
	return 0;
}

static void emit_align(u8 **pprog, u32 align)
{
	u8 *target, *prog = *pprog;

	target = PTR_ALIGN(prog, align);
	if (target != prog)
		emit_nops(&prog, target - prog);

	*pprog = prog;
}

static int emit_cond_near_jump(u8 **pprog, void *func, void *ip, u8 jmp_cond)
{
	u8 *prog = *pprog;
	s64 offset;

	offset = func - (ip + 2 + 4);
	if (!is_simm32(offset)) {
		pr_err("Target %p is out of range\n", func);
		return -EINVAL;
	}
	EMIT2_off32(0x0F, jmp_cond + 0x10, offset);
	*pprog = prog;
	return 0;
}

static int invoke_bpf(const struct btf_func_model *m, u8 **pprog,
		      struct bpf_tramp_links *tl, int stack_size,
		      int run_ctx_off, bool save_ret,
		      void *image, void *rw_image)
{
	int i;
	u8 *prog = *pprog;

	for (i = 0; i < tl->nr_links; i++) {
		if (invoke_bpf_prog(m, &prog, tl->links[i], stack_size,
				    run_ctx_off, save_ret, image, rw_image))
			return -EINVAL;
	}
	*pprog = prog;
	return 0;
}

static int invoke_bpf_mod_ret(const struct btf_func_model *m, u8 **pprog,
			      struct bpf_tramp_links *tl, int stack_size,
			      int run_ctx_off, u8 **branches,
			      void *image, void *rw_image)
{
	u8 *prog = *pprog;
	int i;

	/* The first fmod_ret program will receive a garbage return value.
	 * Set this to 0 to avoid confusing the program.
	 */
	emit_mov_imm32(&prog, false, BPF_REG_0, 0);
	emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -8);
	for (i = 0; i < tl->nr_links; i++) {
		if (invoke_bpf_prog(m, &prog, tl->links[i], stack_size, run_ctx_off, true,
				    image, rw_image))
			return -EINVAL;

		/* mod_ret prog stored return value into [rbp - 8]. Emit:
		 * if (*(u64 *)(rbp - 8) !=  0)
		 *	goto do_fexit;
		 */
		/* cmp QWORD PTR [rbp - 0x8], 0x0 */
		EMIT4(0x48, 0x83, 0x7d, 0xf8); EMIT1(0x00);

		/* Save the location of the branch and Generate 6 nops
		 * (4 bytes for an offset and 2 bytes for the jump) These nops
		 * are replaced with a conditional jump once do_fexit (i.e. the
		 * start of the fexit invocation) is finalized.
		 */
		branches[i] = prog;
		emit_nops(&prog, 4 + 2);
	}

	*pprog = prog;
	return 0;
}

/* mov rax, qword ptr [rbp - rounded_stack_depth - 8] */
#define LOAD_TRAMP_TAIL_CALL_CNT_PTR(stack)	\
	__LOAD_TCC_PTR(-round_up(stack, 8) - 8)

/* Example:
 * __be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev);
 * its 'struct btf_func_model' will be nr_args=2
 * The assembly code when eth_type_trans is executing after trampoline:
 *
 * push rbp
 * mov rbp, rsp
 * sub rsp, 16                     // space for skb and dev
 * push rbx                        // temp regs to pass start time
 * mov qword ptr [rbp - 16], rdi   // save skb pointer to stack
 * mov qword ptr [rbp - 8], rsi    // save dev pointer to stack
 * call __bpf_prog_enter           // rcu_read_lock and preempt_disable
 * mov rbx, rax                    // remember start time in bpf stats are enabled
 * lea rdi, [rbp - 16]             // R1==ctx of bpf prog
 * call addr_of_jited_FENTRY_prog
 * movabsq rdi, 64bit_addr_of_struct_bpf_prog  // unused if bpf stats are off
 * mov rsi, rbx                    // prog start time
 * call __bpf_prog_exit            // rcu_read_unlock, preempt_enable and stats math
 * mov rdi, qword ptr [rbp - 16]   // restore skb pointer from stack
 * mov rsi, qword ptr [rbp - 8]    // restore dev pointer from stack
 * pop rbx
 * leave
 * ret
 *
 * eth_type_trans has 5 byte nop at the beginning. These 5 bytes will be
 * replaced with 'call generated_bpf_trampoline'. When it returns
 * eth_type_trans will continue executing with original skb and dev pointers.
 *
 * The assembly code when eth_type_trans is called from trampoline:
 *
 * push rbp
 * mov rbp, rsp
 * sub rsp, 24                     // space for skb, dev, return value
 * push rbx                        // temp regs to pass start time
 * mov qword ptr [rbp - 24], rdi   // save skb pointer to stack
 * mov qword ptr [rbp - 16], rsi   // save dev pointer to stack
 * call __bpf_prog_enter           // rcu_read_lock and preempt_disable
 * mov rbx, rax                    // remember start time if bpf stats are enabled
 * lea rdi, [rbp - 24]             // R1==ctx of bpf prog
 * call addr_of_jited_FENTRY_prog  // bpf prog can access skb and dev
 * movabsq rdi, 64bit_addr_of_struct_bpf_prog  // unused if bpf stats are off
 * mov rsi, rbx                    // prog start time
 * call __bpf_prog_exit            // rcu_read_unlock, preempt_enable and stats math
 * mov rdi, qword ptr [rbp - 24]   // restore skb pointer from stack
 * mov rsi, qword ptr [rbp - 16]   // restore dev pointer from stack
 * call eth_type_trans+5           // execute body of eth_type_trans
 * mov qword ptr [rbp - 8], rax    // save return value
 * call __bpf_prog_enter           // rcu_read_lock and preempt_disable
 * mov rbx, rax                    // remember start time in bpf stats are enabled
 * lea rdi, [rbp - 24]             // R1==ctx of bpf prog
 * call addr_of_jited_FEXIT_prog   // bpf prog can access skb, dev, return value
 * movabsq rdi, 64bit_addr_of_struct_bpf_prog  // unused if bpf stats are off
 * mov rsi, rbx                    // prog start time
 * call __bpf_prog_exit            // rcu_read_unlock, preempt_enable and stats math
 * mov rax, qword ptr [rbp - 8]    // restore eth_type_trans's return value
 * pop rbx
 * leave
 * add rsp, 8                      // skip eth_type_trans's frame
 * ret                             // return to its caller
 */
static int __arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *rw_image,
					 void *rw_image_end, void *image,
					 const struct btf_func_model *m, u32 flags,
					 struct bpf_tramp_links *tlinks,
					 void *func_addr)
{
	int i, ret, nr_regs = m->nr_args, stack_size = 0;
	int regs_off, nregs_off, ip_off, run_ctx_off, arg_stack_off, rbx_off;
	struct bpf_tramp_links *fentry = &tlinks[BPF_TRAMP_FENTRY];
	struct bpf_tramp_links *fexit = &tlinks[BPF_TRAMP_FEXIT];
	struct bpf_tramp_links *fmod_ret = &tlinks[BPF_TRAMP_MODIFY_RETURN];
	void *orig_call = func_addr;
	u8 **branches = NULL;
	u8 *prog;
	bool save_ret;

	/*
	 * F_INDIRECT is only compatible with F_RET_FENTRY_RET, it is
	 * explicitly incompatible with F_CALL_ORIG | F_SKIP_FRAME | F_IP_ARG
	 * because @func_addr.
	 */
	WARN_ON_ONCE((flags & BPF_TRAMP_F_INDIRECT) &&
		     (flags & ~(BPF_TRAMP_F_INDIRECT | BPF_TRAMP_F_RET_FENTRY_RET)));

	/* extra registers for struct arguments */
	for (i = 0; i < m->nr_args; i++) {
		if (m->arg_flags[i] & BTF_FMODEL_STRUCT_ARG)
			nr_regs += (m->arg_size[i] + 7) / 8 - 1;
	}

	/* x86-64 supports up to MAX_BPF_FUNC_ARGS arguments. 1-6
	 * are passed through regs, the remains are through stack.
	 */
	if (nr_regs > MAX_BPF_FUNC_ARGS)
		return -ENOTSUPP;

	/* Generated trampoline stack layout:
	 *
	 * RBP + 8         [ return address  ]
	 * RBP + 0         [ RBP             ]
	 *
	 * RBP - 8         [ return value    ]  BPF_TRAMP_F_CALL_ORIG or
	 *                                      BPF_TRAMP_F_RET_FENTRY_RET flags
	 *
	 *                 [ reg_argN        ]  always
	 *                 [ ...             ]
	 * RBP - regs_off  [ reg_arg1        ]  program's ctx pointer
	 *
	 * RBP - nregs_off [ regs count	     ]  always
	 *
	 * RBP - ip_off    [ traced function ]  BPF_TRAMP_F_IP_ARG flag
	 *
	 * RBP - rbx_off   [ rbx value       ]  always
	 *
	 * RBP - run_ctx_off [ bpf_tramp_run_ctx ]
	 *
	 *                     [ stack_argN ]  BPF_TRAMP_F_CALL_ORIG
	 *                     [ ...        ]
	 *                     [ stack_arg2 ]
	 * RBP - arg_stack_off [ stack_arg1 ]
	 * RSP                 [ tail_call_cnt_ptr ] BPF_TRAMP_F_TAIL_CALL_CTX
	 */

	/* room for return value of orig_call or fentry prog */
	save_ret = flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET);
	if (save_ret)
		stack_size += 8;

	stack_size += nr_regs * 8;
	regs_off = stack_size;

	/* regs count  */
	stack_size += 8;
	nregs_off = stack_size;

	if (flags & BPF_TRAMP_F_IP_ARG)
		stack_size += 8; /* room for IP address argument */

	ip_off = stack_size;

	stack_size += 8;
	rbx_off = stack_size;

	stack_size += (sizeof(struct bpf_tramp_run_ctx) + 7) & ~0x7;
	run_ctx_off = stack_size;

	if (nr_regs > 6 && (flags & BPF_TRAMP_F_CALL_ORIG)) {
		/* the space that used to pass arguments on-stack */
		stack_size += (nr_regs - get_nr_used_regs(m)) * 8;
		/* make sure the stack pointer is 16-byte aligned if we
		 * need pass arguments on stack, which means
		 *  [stack_size + 8(rbp) + 8(rip) + 8(origin rip)]
		 * should be 16-byte aligned. Following code depend on
		 * that stack_size is already 8-byte aligned.
		 */
		stack_size += (stack_size % 16) ? 0 : 8;
	}

	arg_stack_off = stack_size;

	if (flags & BPF_TRAMP_F_SKIP_FRAME) {
		/* skip patched call instruction and point orig_call to actual
		 * body of the kernel function.
		 */
		if (is_endbr(orig_call))
			orig_call += ENDBR_INSN_SIZE;
		orig_call += X86_PATCH_SIZE;
	}

	prog = rw_image;

	if (flags & BPF_TRAMP_F_INDIRECT) {
		/*
		 * Indirect call for bpf_struct_ops
		 */
		emit_cfi(&prog, image,
			 cfi_get_func_hash(func_addr),
			 cfi_get_func_arity(func_addr));
	} else {
		/*
		 * Direct-call fentry stub, as such it needs accounting for the
		 * __fentry__ call.
		 */
		x86_call_depth_emit_accounting(&prog, NULL, image);
	}
	EMIT1(0x55);		 /* push rbp */
	EMIT3(0x48, 0x89, 0xE5); /* mov rbp, rsp */
	if (!is_imm8(stack_size)) {
		/* sub rsp, stack_size */
		EMIT3_off32(0x48, 0x81, 0xEC, stack_size);
	} else {
		/* sub rsp, stack_size */
		EMIT4(0x48, 0x83, 0xEC, stack_size);
	}
	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
		EMIT1(0x50);		/* push rax */
	/* mov QWORD PTR [rbp - rbx_off], rbx */
	emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_6, -rbx_off);

	/* Store number of argument registers of the traced function:
	 *   mov rax, nr_regs
	 *   mov QWORD PTR [rbp - nregs_off], rax
	 */
	emit_mov_imm64(&prog, BPF_REG_0, 0, (u32) nr_regs);
	emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -nregs_off);

	if (flags & BPF_TRAMP_F_IP_ARG) {
		/* Store IP address of the traced function:
		 * movabsq rax, func_addr
		 * mov QWORD PTR [rbp - ip_off], rax
		 */
		emit_mov_imm64(&prog, BPF_REG_0, (long) func_addr >> 32, (u32) (long) func_addr);
		emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -ip_off);
	}

	save_args(m, &prog, regs_off, false);

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		/* arg1: mov rdi, im */
		emit_mov_imm64(&prog, BPF_REG_1, (long) im >> 32, (u32) (long) im);
		if (emit_rsb_call(&prog, __bpf_tramp_enter,
				  image + (prog - (u8 *)rw_image))) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (fentry->nr_links) {
		if (invoke_bpf(m, &prog, fentry, regs_off, run_ctx_off,
			       flags & BPF_TRAMP_F_RET_FENTRY_RET, image, rw_image))
			return -EINVAL;
	}

	if (fmod_ret->nr_links) {
		branches = kcalloc(fmod_ret->nr_links, sizeof(u8 *),
				   GFP_KERNEL);
		if (!branches)
			return -ENOMEM;

		if (invoke_bpf_mod_ret(m, &prog, fmod_ret, regs_off,
				       run_ctx_off, branches, image, rw_image)) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		restore_regs(m, &prog, regs_off);
		save_args(m, &prog, arg_stack_off, true);

		if (flags & BPF_TRAMP_F_TAIL_CALL_CTX) {
			/* Before calling the original function, load the
			 * tail_call_cnt_ptr from stack to rax.
			 */
			LOAD_TRAMP_TAIL_CALL_CNT_PTR(stack_size);
		}

		if (flags & BPF_TRAMP_F_ORIG_STACK) {
			emit_ldx(&prog, BPF_DW, BPF_REG_6, BPF_REG_FP, 8);
			EMIT2(0xff, 0xd3); /* call *rbx */
		} else {
			/* call original function */
			if (emit_rsb_call(&prog, orig_call, image + (prog - (u8 *)rw_image))) {
				ret = -EINVAL;
				goto cleanup;
			}
		}
		/* remember return value in a stack for bpf prog to access */
		emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -8);
		im->ip_after_call = image + (prog - (u8 *)rw_image);
		emit_nops(&prog, X86_PATCH_SIZE);
	}

	if (fmod_ret->nr_links) {
		/* From Intel 64 and IA-32 Architectures Optimization
		 * Reference Manual, 3.4.1.4 Code Alignment, Assembly/Compiler
		 * Coding Rule 11: All branch targets should be 16-byte
		 * aligned.
		 */
		emit_align(&prog, 16);
		/* Update the branches saved in invoke_bpf_mod_ret with the
		 * aligned address of do_fexit.
		 */
		for (i = 0; i < fmod_ret->nr_links; i++) {
			emit_cond_near_jump(&branches[i], image + (prog - (u8 *)rw_image),
					    image + (branches[i] - (u8 *)rw_image), X86_JNE);
		}
	}

	if (fexit->nr_links) {
		if (invoke_bpf(m, &prog, fexit, regs_off, run_ctx_off,
			       false, image, rw_image)) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (flags & BPF_TRAMP_F_RESTORE_REGS)
		restore_regs(m, &prog, regs_off);

	/* This needs to be done regardless. If there were fmod_ret programs,
	 * the return value is only updated on the stack and still needs to be
	 * restored to R0.
	 */
	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		im->ip_epilogue = image + (prog - (u8 *)rw_image);
		/* arg1: mov rdi, im */
		emit_mov_imm64(&prog, BPF_REG_1, (long) im >> 32, (u32) (long) im);
		if (emit_rsb_call(&prog, __bpf_tramp_exit, image + (prog - (u8 *)rw_image))) {
			ret = -EINVAL;
			goto cleanup;
		}
	} else if (flags & BPF_TRAMP_F_TAIL_CALL_CTX) {
		/* Before running the original function, load the
		 * tail_call_cnt_ptr from stack to rax.
		 */
		LOAD_TRAMP_TAIL_CALL_CNT_PTR(stack_size);
	}

	/* restore return value of orig_call or fentry prog back into RAX */
	if (save_ret)
		emit_ldx(&prog, BPF_DW, BPF_REG_0, BPF_REG_FP, -8);

	emit_ldx(&prog, BPF_DW, BPF_REG_6, BPF_REG_FP, -rbx_off);
	EMIT1(0xC9); /* leave */
	if (flags & BPF_TRAMP_F_SKIP_FRAME) {
		/* skip our return address and return to parent */
		EMIT4(0x48, 0x83, 0xC4, 8); /* add rsp, 8 */
	}
	emit_return(&prog, image + (prog - (u8 *)rw_image));
	/* Make sure the trampoline generation logic doesn't overflow */
	if (WARN_ON_ONCE(prog > (u8 *)rw_image_end - BPF_INSN_SAFETY)) {
		ret = -EFAULT;
		goto cleanup;
	}
	ret = prog - (u8 *)rw_image + BPF_INSN_SAFETY;

cleanup:
	kfree(branches);
	return ret;
}

void *arch_alloc_bpf_trampoline(unsigned int size)
{
	return bpf_prog_pack_alloc(size, jit_fill_hole);
}

void arch_free_bpf_trampoline(void *image, unsigned int size)
{
	bpf_prog_pack_free(image, size);
}

int arch_protect_bpf_trampoline(void *image, unsigned int size)
{
	return 0;
}

int arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *image, void *image_end,
				const struct btf_func_model *m, u32 flags,
				struct bpf_tramp_links *tlinks,
				void *func_addr)
{
	void *rw_image, *tmp;
	int ret;
	u32 size = image_end - image;

	/* rw_image doesn't need to be in module memory range, so we can
	 * use kvmalloc.
	 */
	rw_image = kvmalloc(size, GFP_KERNEL);
	if (!rw_image)
		return -ENOMEM;

	ret = __arch_prepare_bpf_trampoline(im, rw_image, rw_image + size, image, m,
					    flags, tlinks, func_addr);
	if (ret < 0)
		goto out;

	tmp = bpf_arch_text_copy(image, rw_image, size);
	if (IS_ERR(tmp))
		ret = PTR_ERR(tmp);
out:
	kvfree(rw_image);
	return ret;
}

int arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
			     struct bpf_tramp_links *tlinks, void *func_addr)
{
	struct bpf_tramp_image im;
	void *image;
	int ret;

	/* Allocate a temporary buffer for __arch_prepare_bpf_trampoline().
	 * This will NOT cause fragmentation in direct map, as we do not
	 * call set_memory_*() on this buffer.
	 *
	 * We cannot use kvmalloc here, because we need image to be in
	 * module memory range.
	 */
	image = bpf_jit_alloc_exec(PAGE_SIZE);
	if (!image)
		return -ENOMEM;

	ret = __arch_prepare_bpf_trampoline(&im, image, image + PAGE_SIZE, image,
					    m, flags, tlinks, func_addr);
	bpf_jit_free_exec(image);
	return ret;
}

static int emit_bpf_dispatcher(u8 **pprog, int a, int b, s64 *progs, u8 *image, u8 *buf)
{
	u8 *jg_reloc, *prog = *pprog;
	int pivot, err, jg_bytes = 1;
	s64 jg_offset;

	if (a == b) {
		/* Leaf node of recursion, i.e. not a range of indices
		 * anymore.
		 */
		EMIT1(add_1mod(0x48, BPF_REG_3));	/* cmp rdx,func */
		if (!is_simm32(progs[a]))
			return -1;
		EMIT2_off32(0x81, add_1reg(0xF8, BPF_REG_3),
			    progs[a]);
		err = emit_cond_near_jump(&prog,	/* je func */
					  (void *)progs[a], image + (prog - buf),
					  X86_JE);
		if (err)
			return err;

		emit_indirect_jump(&prog, 2 /* rdx */, image + (prog - buf));

		*pprog = prog;
		return 0;
	}

	/* Not a leaf node, so we pivot, and recursively descend into
	 * the lower and upper ranges.
	 */
	pivot = (b - a) / 2;
	EMIT1(add_1mod(0x48, BPF_REG_3));		/* cmp rdx,func */
	if (!is_simm32(progs[a + pivot]))
		return -1;
	EMIT2_off32(0x81, add_1reg(0xF8, BPF_REG_3), progs[a + pivot]);

	if (pivot > 2) {				/* jg upper_part */
		/* Require near jump. */
		jg_bytes = 4;
		EMIT2_off32(0x0F, X86_JG + 0x10, 0);
	} else {
		EMIT2(X86_JG, 0);
	}
	jg_reloc = prog;

	err = emit_bpf_dispatcher(&prog, a, a + pivot,	/* emit lower_part */
				  progs, image, buf);
	if (err)
		return err;

	/* From Intel 64 and IA-32 Architectures Optimization
	 * Reference Manual, 3.4.1.4 Code Alignment, Assembly/Compiler
	 * Coding Rule 11: All branch targets should be 16-byte
	 * aligned.
	 */
	emit_align(&prog, 16);
	jg_offset = prog - jg_reloc;
	emit_code(jg_reloc - jg_bytes, jg_offset, jg_bytes);

	err = emit_bpf_dispatcher(&prog, a + pivot + 1,	/* emit upper_part */
				  b, progs, image, buf);
	if (err)
		return err;

	*pprog = prog;
	return 0;
}

static int cmp_ips(const void *a, const void *b)
{
	const s64 *ipa = a;
	const s64 *ipb = b;

	if (*ipa > *ipb)
		return 1;
	if (*ipa < *ipb)
		return -1;
	return 0;
}

int arch_prepare_bpf_dispatcher(void *image, void *buf, s64 *funcs, int num_funcs)
{
	u8 *prog = buf;

	sort(funcs, num_funcs, sizeof(funcs[0]), cmp_ips, NULL);
	return emit_bpf_dispatcher(&prog, 0, num_funcs - 1, funcs, image, buf);
}

static const char *bpf_get_prog_name(struct bpf_prog *prog)
{
	if (prog->aux->ksym.prog)
		return prog->aux->ksym.name;
	return prog->aux->name;
}

static void priv_stack_init_guard(void __percpu *priv_stack_ptr, int alloc_size)
{
	int cpu, underflow_idx = (alloc_size - PRIV_STACK_GUARD_SZ) >> 3;
	u64 *stack_ptr;

	for_each_possible_cpu(cpu) {
		stack_ptr = per_cpu_ptr(priv_stack_ptr, cpu);
		stack_ptr[0] = PRIV_STACK_GUARD_VAL;
		stack_ptr[underflow_idx] = PRIV_STACK_GUARD_VAL;
	}
}

static void priv_stack_check_guard(void __percpu *priv_stack_ptr, int alloc_size,
				   struct bpf_prog *prog)
{
	int cpu, underflow_idx = (alloc_size - PRIV_STACK_GUARD_SZ) >> 3;
	u64 *stack_ptr;

	for_each_possible_cpu(cpu) {
		stack_ptr = per_cpu_ptr(priv_stack_ptr, cpu);
		if (stack_ptr[0] != PRIV_STACK_GUARD_VAL ||
		    stack_ptr[underflow_idx] != PRIV_STACK_GUARD_VAL) {
			pr_err("BPF private stack overflow/underflow detected for prog %sx\n",
			       bpf_get_prog_name(prog));
			break;
		}
	}
}

struct x64_jit_data {
	struct bpf_binary_header *rw_header;
	struct bpf_binary_header *header;
	int *addrs;
	u8 *image;
	int proglen;
	struct jit_context ctx;
};

#define MAX_PASSES 20
#define PADDING_PASSES (MAX_PASSES - 5)

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_binary_header *rw_header = NULL;
	struct bpf_binary_header *header = NULL;
	struct bpf_prog *tmp, *orig_prog = prog;
	void __percpu *priv_stack_ptr = NULL;
	struct x64_jit_data *jit_data;
	int priv_stack_alloc_sz;
	int proglen, oldproglen = 0;
	struct jit_context ctx = {};
	bool tmp_blinded = false;
	bool extra_pass = false;
	bool padding = false;
	u8 *rw_image = NULL;
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

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}
	priv_stack_ptr = prog->aux->priv_stack_ptr;
	if (!priv_stack_ptr && prog->aux->jits_use_priv_stack) {
		/* Allocate actual private stack size with verifier-calculated
		 * stack size plus two memory guards to protect overflow and
		 * underflow.
		 */
		priv_stack_alloc_sz = round_up(prog->aux->stack_depth, 8) +
				      2 * PRIV_STACK_GUARD_SZ;
		priv_stack_ptr = __alloc_percpu_gfp(priv_stack_alloc_sz, 8, GFP_KERNEL);
		if (!priv_stack_ptr) {
			prog = orig_prog;
			goto out_priv_stack;
		}

		priv_stack_init_guard(priv_stack_ptr, priv_stack_alloc_sz);
		prog->aux->priv_stack_ptr = priv_stack_ptr;
	}
	addrs = jit_data->addrs;
	if (addrs) {
		ctx = jit_data->ctx;
		oldproglen = jit_data->proglen;
		image = jit_data->image;
		header = jit_data->header;
		rw_header = jit_data->rw_header;
		rw_image = (void *)rw_header + ((void *)image - (void *)header);
		extra_pass = true;
		padding = true;
		goto skip_init_addrs;
	}
	addrs = kvmalloc_array(prog->len + 1, sizeof(*addrs), GFP_KERNEL);
	if (!addrs) {
		prog = orig_prog;
		goto out_addrs;
	}

	/*
	 * Before first pass, make a rough estimation of addrs[]
	 * each BPF instruction is translated to less than 64 bytes
	 */
	for (proglen = 0, i = 0; i <= prog->len; i++) {
		proglen += 64;
		addrs[i] = proglen;
	}
	ctx.cleanup_addr = proglen;
skip_init_addrs:

	/*
	 * JITed image shrinks with every pass and the loop iterates
	 * until the image stops shrinking. Very large BPF programs
	 * may converge on the last pass. In such case do one more
	 * pass to emit the final image.
	 */
	for (pass = 0; pass < MAX_PASSES || image; pass++) {
		if (!padding && pass >= PADDING_PASSES)
			padding = true;
		proglen = do_jit(prog, addrs, image, rw_image, oldproglen, &ctx, padding);
		if (proglen <= 0) {
out_image:
			image = NULL;
			if (header) {
				bpf_arch_text_copy(&header->size, &rw_header->size,
						   sizeof(rw_header->size));
				bpf_jit_binary_pack_free(header, rw_header);
			}
			/* Fall back to interpreter mode */
			prog = orig_prog;
			if (extra_pass) {
				prog->bpf_func = NULL;
				prog->jited = 0;
				prog->jited_len = 0;
			}
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
			/*
			 * The number of entries in extable is the number of BPF_LDX
			 * insns that access kernel memory via "pointer to BTF type".
			 * The verifier changed their opcode from LDX|MEM|size
			 * to LDX|PROBE_MEM|size to make JITing easier.
			 */
			u32 align = __alignof__(struct exception_table_entry);
			u32 extable_size = prog->aux->num_exentries *
				sizeof(struct exception_table_entry);

			/* allocate module memory for x86 insns and extable */
			header = bpf_jit_binary_pack_alloc(roundup(proglen, align) + extable_size,
							   &image, align, &rw_header, &rw_image,
							   jit_fill_hole);
			if (!header) {
				prog = orig_prog;
				goto out_addrs;
			}
			prog->aux->extable = (void *) image + roundup(proglen, align);
		}
		oldproglen = proglen;
		cond_resched();
	}

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, proglen, pass + 1, rw_image);

	if (image) {
		if (!prog->is_func || extra_pass) {
			/*
			 * bpf_jit_binary_pack_finalize fails in two scenarios:
			 *   1) header is not pointing to proper module memory;
			 *   2) the arch doesn't support bpf_arch_text_copy().
			 *
			 * Both cases are serious bugs and justify WARN_ON.
			 */
			if (WARN_ON(bpf_jit_binary_pack_finalize(header, rw_header))) {
				/* header has been freed */
				header = NULL;
				goto out_image;
			}

			bpf_tail_call_direct_fixup(prog);
		} else {
			jit_data->addrs = addrs;
			jit_data->ctx = ctx;
			jit_data->proglen = proglen;
			jit_data->image = image;
			jit_data->header = header;
			jit_data->rw_header = rw_header;
		}
		/*
		 * ctx.prog_offset is used when CFI preambles put code *before*
		 * the function. See emit_cfi(). For FineIBT specifically this code
		 * can also be executed and bpf_prog_kallsyms_add() will
		 * generate an additional symbol to cover this, hence also
		 * decrement proglen.
		 */
		prog->bpf_func = (void *)image + cfi_get_offset();
		prog->jited = 1;
		prog->jited_len = proglen - cfi_get_offset();
	} else {
		prog = orig_prog;
	}

	if (!image || !prog->is_func || extra_pass) {
		if (image)
			bpf_prog_fill_jited_linfo(prog, addrs + 1);
out_addrs:
		kvfree(addrs);
		if (!image && priv_stack_ptr) {
			free_percpu(priv_stack_ptr);
			prog->aux->priv_stack_ptr = NULL;
		}
out_priv_stack:
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
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

void *bpf_arch_text_copy(void *dst, void *src, size_t len)
{
	if (text_poke_copy(dst, src, len) == NULL)
		return ERR_PTR(-EINVAL);
	return dst;
}

/* Indicate the JIT backend supports mixing bpf2bpf and tailcalls. */
bool bpf_jit_supports_subprog_tailcalls(void)
{
	return true;
}

bool bpf_jit_supports_percpu_insn(void)
{
	return true;
}

void bpf_jit_free(struct bpf_prog *prog)
{
	if (prog->jited) {
		struct x64_jit_data *jit_data = prog->aux->jit_data;
		struct bpf_binary_header *hdr;
		void __percpu *priv_stack_ptr;
		int priv_stack_alloc_sz;

		/*
		 * If we fail the final pass of JIT (from jit_subprogs),
		 * the program may not be finalized yet. Call finalize here
		 * before freeing it.
		 */
		if (jit_data) {
			bpf_jit_binary_pack_finalize(jit_data->header,
						     jit_data->rw_header);
			kvfree(jit_data->addrs);
			kfree(jit_data);
		}
		prog->bpf_func = (void *)prog->bpf_func - cfi_get_offset();
		hdr = bpf_jit_binary_pack_hdr(prog);
		bpf_jit_binary_pack_free(hdr, NULL);
		priv_stack_ptr = prog->aux->priv_stack_ptr;
		if (priv_stack_ptr) {
			priv_stack_alloc_sz = round_up(prog->aux->stack_depth, 8) +
					      2 * PRIV_STACK_GUARD_SZ;
			priv_stack_check_guard(priv_stack_ptr, priv_stack_alloc_sz, prog);
			free_percpu(prog->aux->priv_stack_ptr);
		}
		WARN_ON_ONCE(!bpf_prog_kallsyms_verify_off(prog));
	}

	bpf_prog_unlock_free(prog);
}

bool bpf_jit_supports_exceptions(void)
{
	/* We unwind through both kernel frames (starting from within bpf_throw
	 * call) and BPF frames. Therefore we require ORC unwinder to be enabled
	 * to walk kernel frames and reach BPF frames in the stack trace.
	 */
	return IS_ENABLED(CONFIG_UNWINDER_ORC);
}

bool bpf_jit_supports_private_stack(void)
{
	return true;
}

void arch_bpf_stack_walk(bool (*consume_fn)(void *cookie, u64 ip, u64 sp, u64 bp), void *cookie)
{
#if defined(CONFIG_UNWINDER_ORC)
	struct unwind_state state;
	unsigned long addr;

	for (unwind_start(&state, current, NULL, NULL); !unwind_done(&state);
	     unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		if (!addr || !consume_fn(cookie, (u64)addr, (u64)state.sp, (u64)state.bp))
			break;
	}
	return;
#endif
	WARN(1, "verification of programs using bpf_throw should have failed\n");
}

void bpf_arch_poke_desc_update(struct bpf_jit_poke_descriptor *poke,
			       struct bpf_prog *new, struct bpf_prog *old)
{
	u8 *old_addr, *new_addr, *old_bypass_addr;
	int ret;

	old_bypass_addr = old ? NULL : poke->bypass_addr;
	old_addr = old ? (u8 *)old->bpf_func + poke->adj_off : NULL;
	new_addr = new ? (u8 *)new->bpf_func + poke->adj_off : NULL;

	/*
	 * On program loading or teardown, the program's kallsym entry
	 * might not be in place, so we use __bpf_arch_text_poke to skip
	 * the kallsyms check.
	 */
	if (new) {
		ret = __bpf_arch_text_poke(poke->tailcall_target,
					   BPF_MOD_JUMP,
					   old_addr, new_addr);
		BUG_ON(ret < 0);
		if (!old) {
			ret = __bpf_arch_text_poke(poke->tailcall_bypass,
						   BPF_MOD_JUMP,
						   poke->bypass_addr,
						   NULL);
			BUG_ON(ret < 0);
		}
	} else {
		ret = __bpf_arch_text_poke(poke->tailcall_bypass,
					   BPF_MOD_JUMP,
					   old_bypass_addr,
					   poke->bypass_addr);
		BUG_ON(ret < 0);
		/* let other CPUs finish the execution of program
		 * so that it will not possible to expose them
		 * to invalid nop, stack unwind, nop state
		 */
		if (!ret)
			synchronize_rcu();
		ret = __bpf_arch_text_poke(poke->tailcall_target,
					   BPF_MOD_JUMP,
					   old_addr, NULL);
		BUG_ON(ret < 0);
	}
}

bool bpf_jit_supports_arena(void)
{
	return true;
}

bool bpf_jit_supports_insn(struct bpf_insn *insn, bool in_arena)
{
	if (!in_arena)
		return true;
	switch (insn->code) {
	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		if (insn->imm == (BPF_AND | BPF_FETCH) ||
		    insn->imm == (BPF_OR | BPF_FETCH) ||
		    insn->imm == (BPF_XOR | BPF_FETCH))
			return false;
	}
	return true;
}

bool bpf_jit_supports_ptr_xchg(void)
{
	return true;
}

/* x86-64 JIT emits its own code to filter user addresses so return 0 here */
u64 bpf_arch_uaddress_limit(void)
{
	return 0;
}
