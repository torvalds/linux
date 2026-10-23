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
#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/memory.h>
#include <linux/sort.h>
#include <linux/execmem.h>
#include <linux/extable.h>
#include <asm/extable.h>
#include <asm/ftrace.h>
#include <asm/set_memory.h>
#include <asm/nospec-branch.h>
#include <asm/text-patching.h>
#include <asm/unwind.h>
#include <asm/cfi.h>

#if IS_ENABLED(CONFIG_BPF_JIT_KASAN)
void __asan_load1(void *p);
void __asan_store1(void *p);
void __asan_load2(void *p);
void __asan_store2(void *p);
void __asan_load4(void *p);
void __asan_store4(void *p);
void __asan_load8(void *p);
void __asan_store8(void *p);
#endif

static bool all_callee_regs_used[4] = {true, true, true, true};

static void *trampoline_size_image;

static int __init init_trampoline_size_image(void)
{
	/*
	 * The generated trampoline contains calls and jumps with 32bit relative
	 * offsets, so the scratch image must be in the execmem range.
	 * On x86, module data and executable memory share the same address range,
	 * so using EXECMEM_MODULE_DATA to get writable memory.
	 */
	trampoline_size_image = execmem_alloc(EXECMEM_MODULE_DATA, PAGE_SIZE);
	return trampoline_size_image ? 0 : -ENOMEM;
}
late_initcall(init_trampoline_size_image);

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
#define EMIT5(b1, b2, b3, b4, b5) \
	do { EMIT1(b1); EMIT4(b2, b3, b4, b5); } while (0)

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
#define EMIT_ENDBR()		do { } while (0)
#define EMIT_ENDBR_POISON()	do { } while (0)
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
	[BPF_REG_AX] = offsetof(struct pt_regs, r10),
};

static bool is_atomic_fetch_op(const struct bpf_insn *insn)
{
	return insn->imm == (BPF_AND | BPF_FETCH) ||
	       insn->imm == (BPF_OR | BPF_FETCH) ||
	       insn->imm == (BPF_XOR | BPF_FETCH);
}

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

/* add rsp, depth */
static void emit_add_rsp(u8 **pprog, u32 depth)
{
	u8 *prog = *pprog;

	if (!depth)
		return;
	if (is_imm8(depth))
		EMIT4(0x48, 0x83, 0xC4, depth); /* add rsp, imm8 */
	else
		EMIT3_off32(0x48, 0x81, 0xC4, depth); /* add rsp, imm32 */
	*pprog = prog;
}

/* sub rsp, depth */
static void emit_sub_rsp(u8 **pprog, u32 depth)
{
	u8 *prog = *pprog;

	if (!depth)
		return;
	if (is_imm8(depth))
		EMIT4(0x48, 0x83, 0xEC, depth); /* sub rsp, imm8 */
	else
		EMIT3_off32(0x48, 0x81, 0xEC, depth); /* sub rsp, imm32 */
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
static void emit_ldx(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off);
static void emit_mov_reg(u8 **pprog, bool is64, u32 dst_reg, u32 src_reg);
static void emit_alu_imm(u8 **pprog, u32 op, u32 dst_reg, s32 imm, bool is64);

static void emit_fineibt(u8 **pprog, u8 *ip, u32 hash, int arity)
{
	u8 *prog = *pprog;

	EMIT_ENDBR();
	EMIT1_off32(0x2d, hash);			/* subl $hash, %eax	*/
	if (cfi_bhi) {
		EMIT2(0x2e, 0x2e);			/* cs cs */
		emit_call(&prog, __bhi_args[arity], ip + 11);
	} else {
		EMIT3_off32(0x2e, 0x0f, 0x85, 3);	/* jne.d32,pn 3		*/
	}
	EMIT_ENDBR_POISON();

	*pprog = prog;
}

static void emit_kcfi(u8 **pprog, u32 hash)
{
	u8 *prog = *pprog;

	EMIT1_off32(0xb8, hash);			/* movl $hash, %eax	*/
#ifdef CONFIG_CALL_PADDING
	for (int i = 0; i < CONFIG_FUNCTION_PADDING_CFI; i++)
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
	emit_sub_rsp(&prog, round_up(stack_depth, 8));
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

static int __bpf_arch_text_poke(void *ip, enum bpf_text_poke_type old_t,
				enum bpf_text_poke_type new_t,
				void *old_addr, void *new_addr)
{
	const u8 *nop_insn = x86_nops[5];
	u8 old_insn[X86_PATCH_SIZE];
	u8 new_insn[X86_PATCH_SIZE];
	u8 *prog;
	int ret;

	memcpy(old_insn, nop_insn, X86_PATCH_SIZE);
	if (old_t != BPF_MOD_NOP && old_addr) {
		prog = old_insn;
		ret = old_t == BPF_MOD_CALL ?
		      emit_call(&prog, old_addr, ip) :
		      emit_jump(&prog, old_addr, ip);
		if (ret)
			return ret;
	}

	memcpy(new_insn, nop_insn, X86_PATCH_SIZE);
	if (new_t != BPF_MOD_NOP && new_addr) {
		prog = new_insn;
		ret = new_t == BPF_MOD_CALL ?
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
		smp_text_poke_single(ip, new_insn, X86_PATCH_SIZE, NULL);
		ret = 0;
	}
out:
	mutex_unlock(&text_mutex);
	return ret;
}

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type old_t,
		       enum bpf_text_poke_type new_t, void *old_addr,
		       void *new_addr)
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

	return __bpf_arch_text_poke(ip, old_t, new_t, old_addr, new_addr);
}

#define EMIT_LFENCE()	EMIT3(0x0F, 0xAE, 0xE8)

static void __emit_indirect_jump(u8 **pprog, int reg, bool ereg)
{
	u8 *prog = *pprog;

	if (ereg)
		EMIT1(0x41);

	EMIT2(0xFF, 0xE0 + reg);

	*pprog = prog;
}

static void emit_indirect_jump(u8 **pprog, int bpf_reg, u8 *ip)
{
	u8 *prog = *pprog;
	int reg = reg2hex[bpf_reg];
	bool ereg = is_ereg(bpf_reg);

	if (cpu_feature_enabled(X86_FEATURE_INDIRECT_THUNK_ITS)) {
		OPTIMIZER_HIDE_VAR(reg);
		emit_jump(&prog, its_static_thunk(reg + 8*ereg), ip);
	} else if (cpu_feature_enabled(X86_FEATURE_RETPOLINE_LFENCE)) {
		EMIT_LFENCE();
		__emit_indirect_jump(&prog, reg, ereg);
	} else if (cpu_feature_enabled(X86_FEATURE_RETPOLINE)) {
		OPTIMIZER_HIDE_VAR(reg);
		if (cpu_feature_enabled(X86_FEATURE_CALL_DEPTH))
			emit_jump(&prog, &__x86_indirect_jump_thunk_array[reg + 8*ereg], ip);
		else
			emit_jump(&prog, &__x86_indirect_thunk_array[reg + 8*ereg], ip);
	} else {
		__emit_indirect_jump(&prog, reg, ereg);
		if (IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) || IS_ENABLED(CONFIG_MITIGATION_SLS))
			EMIT1(0xCC);		/* int3 */
	}

	*pprog = prog;
}

static void __emit_indirect_call(u8 **pprog, int reg, bool ereg)
{
	u8 *prog = *pprog;

	if (ereg)
		EMIT1(0x41);

	EMIT2(0xFF, 0xD0 + reg);

	*pprog = prog;
}

/* call *bpf_reg */
static int emit_indirect_call(u8 **pprog, int bpf_reg, u8 *ip)
{
	u8 *prog = *pprog;
	int reg = reg2hex[bpf_reg];
	bool ereg = is_ereg(bpf_reg);
	int err = 0;

	if (cpu_feature_enabled(X86_FEATURE_INDIRECT_THUNK_ITS)) {
		OPTIMIZER_HIDE_VAR(reg);
		err = emit_call(&prog, its_static_thunk(reg + 8*ereg), ip);
	} else if (cpu_feature_enabled(X86_FEATURE_RETPOLINE_LFENCE)) {
		EMIT_LFENCE();
		__emit_indirect_call(&prog, reg, ereg);
	} else if (cpu_feature_enabled(X86_FEATURE_RETPOLINE)) {
		OPTIMIZER_HIDE_VAR(reg);
		if (cpu_feature_enabled(X86_FEATURE_CALL_DEPTH))
			err = emit_call(&prog, &__x86_indirect_call_thunk_array[reg + 8*ereg], ip);
		else
			err = emit_call(&prog, &__x86_indirect_thunk_array[reg + 8*ereg], ip);
	} else {
		__emit_indirect_call(&prog, reg, ereg);
	}

	*pprog = prog;
	return err;
}

static void emit_return(u8 **pprog, u8 *ip)
{
	u8 *prog = *pprog;

	if (cpu_wants_rethunk()) {
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
	emit_mov_reg(&prog, false, BPF_REG_3, BPF_REG_3); /* mov edx, edx */
	EMIT3(0x39, 0x56,                         /* cmp dword ptr [rsi + 16], edx */
	      offsetof(struct bpf_array, map.max_entries));

	offset = ctx->tail_call_indirect_label - (prog + 2 - start);
	EMIT2(X86_JBE, offset);                   /* jbe out */

	/*
	 * if ((*tcc_ptr)++ >= MAX_TAIL_CALL_CNT)
	 *	goto out;
	 */
	/* mov rax, qword ptr [rbp - tcc_ptr_off] */
	emit_ldx(&prog, BPF_DW, BPF_REG_0, BPF_REG_FP, tcc_ptr_off);
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
		if (bpf_prog_uses_arena_insns(bpf_prog))
			pop_r12(&prog);
	}

	/* Pop tail_call_cnt_ptr. */
	EMIT1(0x58);                              /* pop rax */
	/* Pop tail_call_cnt, if it's main prog.
	 * Pop tail_call_cnt_ptr, if it's subprog.
	 */
	EMIT1(0x58);                              /* pop rax */
	emit_add_rsp(&prog, round_up(stack_depth, 8)); /* add rsp, sd */

	/* goto *(prog->bpf_func + X86_TAIL_CALL_OFFSET); */
	EMIT4(0x48, 0x8B, 0x49,                   /* mov rcx, qword ptr [rcx + 32] */
	      offsetof(struct bpf_prog, bpf_func));
	/* add rcx, X86_TAIL_CALL_OFFSET */
	emit_alu_imm(&prog, BPF_ADD, BPF_REG_4, X86_TAIL_CALL_OFFSET, true);
	/*
	 * Now we're ready to jump into next BPF program
	 * rdi == ctx (1st arg)
	 * rcx == prog->bpf_func + X86_TAIL_CALL_OFFSET
	 */
	emit_indirect_jump(&prog, BPF_REG_4 /* R4 -> rcx */, ip + (prog - start));

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
	/* mov rax, qword ptr [rbp - tcc_ptr_off] */
	emit_ldx(&prog, BPF_DW, BPF_REG_0, BPF_REG_FP, tcc_ptr_off);
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
		if (bpf_prog_uses_arena_insns(bpf_prog))
			pop_r12(&prog);
	}

	/* Pop tail_call_cnt_ptr. */
	EMIT1(0x58);                                  /* pop rax */
	/* Pop tail_call_cnt, if it's main prog.
	 * Pop tail_call_cnt_ptr, if it's subprog.
	 */
	EMIT1(0x58);                                  /* pop rax */
	emit_add_rsp(&prog, round_up(stack_depth, 8));

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
						   BPF_MOD_NOP, BPF_MOD_JUMP,
						   NULL,
						   (u8 *)target->bpf_func +
						   poke->adj_off);
			BUG_ON(ret < 0);
			ret = __bpf_arch_text_poke(poke->tailcall_bypass,
						   BPF_MOD_JUMP, BPF_MOD_NOP,
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
			if (is_ereg(dst_reg) || is_ereg_8l(src_reg))
				EMIT1(add_2mod(0x40, src_reg, dst_reg));
			EMIT3(0x0f, 0xbe, add_2reg(0xC0, src_reg, dst_reg));
		} else if (num_bits == 16) {
			if (is_ereg(dst_reg) || is_ereg(src_reg))
				EMIT1(add_2mod(0x40, src_reg, dst_reg));
			EMIT3(add_2mod(0x0f, src_reg, dst_reg), 0xbf,
			      add_2reg(0xC0, src_reg, dst_reg));
		}
	}

	*pprog = prog;
}

/*
 * rbp and r13 as base register always need a displacement, since mod 00
 * with r/m 101 encodes a RIP-relative disp32 and with SIB base 101 a
 * disp32 without base instead.
 */
static bool base_needs_disp(u32 reg)
{
	return reg2hex[reg] == 5;
}

static void emit_movzx_reg(u8 **pprog, int num_bits, u32 dst_reg, u32 src_reg)
{
	u8 *prog = *pprog;

	/* movz[b,w]l dst, src */
	if (is_ereg(dst_reg) ||
	    (num_bits == 8 ? is_ereg_8l(src_reg) : is_ereg(src_reg)))
		EMIT1(add_2mod(0x40, src_reg, dst_reg));
	EMIT3(0x0F, num_bits == 8 ? 0xB6 : 0xB7,
	      add_2reg(0xC0, src_reg, dst_reg));

	*pprog = prog;
}

/* Emit the suffix (ModR/M etc) for addressing *(ptr_reg + off) and val_reg */
static void emit_insn_suffix(u8 **pprog, u32 ptr_reg, u32 val_reg, int off)
{
	u8 *prog = *pprog;

	if (!off && !base_needs_disp(ptr_reg)) {
		/* No displacement */
		EMIT1(add_2reg(0x00, ptr_reg, val_reg));
	} else if (is_imm8(off)) {
		/* 1-byte signed displacement */
		EMIT2(add_2reg(0x40, ptr_reg, val_reg), off);
	} else {
		/* 4-byte signed displacement */
		EMIT1_off32(add_2reg(0x80, ptr_reg, val_reg), off);
	}
	*pprog = prog;
}

/*
 * Emit the suffix for addressing *(ptr_reg + (index_reg << scale) + off)
 * and val_reg, with a SIB byte
 */
static void emit_insn_suffix_SIB(u8 **pprog, u32 ptr_reg, u32 val_reg, u32 index_reg, int off,
				 u8 scale)
{
	u8 sib = (scale << 6) | add_2reg(0, ptr_reg, index_reg);
	u8 *prog = *pprog;

	if (!off && !base_needs_disp(ptr_reg)) {
		EMIT2(add_2reg(0x04, BPF_REG_0, val_reg), sib);
	} else if (is_imm8(off)) {
		EMIT3(add_2reg(0x44, BPF_REG_0, val_reg), sib, off);
	} else {
		EMIT2_off32(add_2reg(0x84, BPF_REG_0, val_reg), sib, off);
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

/* add/sub/and/or/xor dst_reg, imm with the imm8 and the rax short forms */
static void emit_alu_imm(u8 **pprog, u32 op, u32 dst_reg, s32 imm, bool is64)
{
	/* ModR/M opcode extension and rax short form per operation */
	static const u8 modrm[] = {
		[BPF_ADD] = 0xC0, [BPF_SUB] = 0xE8, [BPF_AND] = 0xE0,
		[BPF_OR] = 0xC8, [BPF_XOR] = 0xF0,
	};
	static const u8 short_op[] = {
		[BPF_ADD] = 0x05, [BPF_SUB] = 0x2D, [BPF_AND] = 0x25,
		[BPF_OR] = 0x0D, [BPF_XOR] = 0x35,
	};
	u8 *prog = *pprog;
	bool inc;

	/* A non-negative mask clears the upper half like the 32-bit and does */
	if (op == BPF_AND && imm >= 0)
		is64 = false;
	/* add 128 is sub -128 and the other way round, which fit into an imm8 */
	if ((op == BPF_ADD || op == BPF_SUB) && imm == 128) {
		op = op == BPF_ADD ? BPF_SUB : BPF_ADD;
		imm = -128;
	}
	inc = op == BPF_ADD ? imm == 1 : imm == -1;
	maybe_emit_1mod(&prog, dst_reg, is64);
	/* inc or dec for +-1, the flags are not read across instructions */
	if ((op == BPF_ADD || op == BPF_SUB) && (imm == 1 || imm == -1))
		EMIT2(0xFF, add_1reg(inc ? 0xC0 : 0xC8, dst_reg));
	else if (is_imm8(imm))
		EMIT3(0x83, add_1reg(modrm[op], dst_reg), imm);
	else if (is_axreg(dst_reg))
		EMIT1_off32(short_op[op], imm);
	else
		EMIT2_off32(0x81, add_1reg(modrm[op], dst_reg), imm);

	*pprog = prog;
}

/* add/sub/and/or/xor dst_reg, src_reg */
static void emit_alu_reg(u8 **pprog, u32 op, u32 dst_reg, u32 src_reg, bool is64)
{
	u8 *prog = *pprog;

	maybe_emit_mod(&prog, dst_reg, src_reg, is64);
	EMIT2(simple_alu_opcodes[op], add_2reg(0xC0, dst_reg, src_reg));

	*pprog = prog;
}

/* shl/shr/sar reg, imm */
static void emit_shift_imm(u8 **pprog, u32 op, u32 reg, u8 imm, bool is64)
{
	u8 *prog = *pprog;

	/* The shift left by one is an add of the register to itself */
	if (op == BPF_LSH && imm == 1) {
		emit_alu_reg(&prog, BPF_ADD, reg, reg, is64);
	} else {
		maybe_emit_1mod(&prog, reg, is64);
		if (imm == 1)
			EMIT2(0xD1, add_1reg(simple_alu_opcodes[op], reg));
		else
			EMIT3(0xC1, add_1reg(simple_alu_opcodes[op], reg), imm);
	}

	*pprog = prog;
}

/* neg dst_reg */
static void emit_neg(u8 **pprog, u32 dst_reg, bool is64)
{
	u8 *prog = *pprog;

	maybe_emit_1mod(&prog, dst_reg, is64);
	EMIT2(0xF7, add_1reg(0xD8, dst_reg));

	*pprog = prog;
}

static int emit_kasan_check(struct bpf_verifier_env *env, u8 **pprog,
			    u32 addr_reg, s32 off, u32 bpf_size, u8 *ip,
			    bool is_write)
{
#ifdef CONFIG_BPF_JIT_KASAN
	u8 *prog = *pprog;
	void *kasan_func;

	if (!env)
		return 0;

	if (addr_reg == BPF_REG_FP || addr_reg == BPF_REG_PARAMS)
		return 0;

	/* Derive KASAN check function from access type and size */
	switch (bpf_size) {
	case BPF_B:
		kasan_func = is_write ? __asan_store1 : __asan_load1;
		break;
	case BPF_H:
		kasan_func = is_write ? __asan_store2 : __asan_load2;
		break;
	case BPF_W:
		kasan_func = is_write ? __asan_store4 : __asan_load4;
		break;
	case BPF_DW:
		kasan_func = is_write ? __asan_store8 : __asan_load8;
		break;
	default:
		return -EINVAL;
	}

	/* Save rax */
	EMIT1(0x50);
	/* Save rcx */
	EMIT1(0x51);
	/* Save rdx */
	EMIT1(0x52);
	/* Save rsi */
	EMIT1(0x56);
	/* Save rdi */
	EMIT1(0x57);
	/* Save r8 */
	EMIT2(0x41, 0x50);
	/* Save r9 */
	EMIT2(0x41, 0x51);
	/* Save r10 */
	EMIT2(0x41, 0x52);
	/* Save r11 */
	EMIT2(0x41, 0x53);
	/* mov rdi, addr_reg */
	EMIT_mov(BPF_REG_1, addr_reg);

	/* add rdi, off (if offset is non-zero) */
	if (off)
		emit_alu_imm(&prog, BPF_ADD, BPF_REG_1, off, true);

	/* Adjust ip to account for the instrumentation generated so far */
	ip += (prog - *pprog);
	/* We emit a call, so update call depth counting */
	ip += x86_call_depth_emit_accounting(&prog, kasan_func, ip);
	/* call kasan_func */
	if (emit_call(&prog, kasan_func, ip))
		return -ERANGE;

	EMIT2(0x41, 0x5B);
	EMIT2(0x41, 0x5A);
	EMIT2(0x41, 0x59);
	EMIT2(0x41, 0x58);
	EMIT1(0x5F);
	EMIT1(0x5E);
	EMIT1(0x5A);
	EMIT1(0x59);
	EMIT1(0x58);

	*pprog = prog;
#endif /* CONFIG_BPF_JIT_KASAN */
	return 0;
}

/* LDX: dst_reg = *(u8*)(src_reg + off) */
static void emit_ldx(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* Emit 'movzx eax, byte ptr [rax + off]' */
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT1(add_2mod(0x40, src_reg, dst_reg));
		EMIT2(0x0F, 0xB6);
		break;
	case BPF_H:
		/* Emit 'movzx eax, word ptr [rax + off]' */
		if (is_ereg(dst_reg) || is_ereg(src_reg))
			EMIT1(add_2mod(0x40, src_reg, dst_reg));
		EMIT2(0x0F, 0xB7);
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
	emit_insn_suffix_SIB(&prog, src_reg, dst_reg, index_reg, off, 0);
	*pprog = prog;
}

static void emit_ldsx_index(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, u32 index_reg, int off)
{
	u8 *prog = *pprog;

	switch (size) {
	case BPF_B:
		/* movsx rax, byte ptr [rax + r12 + off] */
		EMIT3(add_3mod(0x48, src_reg, dst_reg, index_reg), 0x0F, 0xBE);
		break;
	case BPF_H:
		/* movsx rax, word ptr [rax + r12 + off] */
		EMIT3(add_3mod(0x48, src_reg, dst_reg, index_reg), 0x0F, 0xBF);
		break;
	case BPF_W:
		/* movsx rax, dword ptr [rax + r12 + off] */
		EMIT2(add_3mod(0x48, src_reg, dst_reg, index_reg), 0x63);
		break;
	}
	emit_insn_suffix_SIB(&prog, src_reg, dst_reg, index_reg, off, 0);
	*pprog = prog;
}

static void emit_ldx_r12(u8 **pprog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	emit_ldx_index(pprog, size, dst_reg, src_reg, X86_REG_R12, off);
}

static void emit_ldsx_r12(u8 **prog, u32 size, u32 dst_reg, u32 src_reg, int off)
{
	emit_ldsx_index(prog, size, dst_reg, src_reg, X86_REG_R12, off);
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
	emit_insn_suffix_SIB(&prog, dst_reg, src_reg, index_reg, off, 0);
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
	emit_insn_suffix_SIB(&prog, dst_reg, 0, index_reg, off, 0);
	EMIT(imm, bpf_size_to_x86_bytes(size));
	*pprog = prog;
}

/* ST: *(u8*)(dst_reg + off) = imm */
static void emit_st(u8 **pprog, struct bpf_insn *insn, u32 dst_reg,
		    s32 insn_off)
{
	s32 imm32 = insn->imm;
	u8 *prog = *pprog;

	switch (BPF_SIZE(insn->code)) {
	case BPF_B:
		if (is_ereg(dst_reg))
			EMIT2(0x41, 0xC6);
		else
			EMIT1(0xC6);
		break;
	case BPF_H:
		if (is_ereg(dst_reg))
			EMIT3(0x66, 0x41, 0xC7);
		else
			EMIT2(0x66, 0xC7);
		break;
	case BPF_W:
		if (is_ereg(dst_reg))
			EMIT2(0x41, 0xC7);
		else
			EMIT1(0xC7);
		break;
	case BPF_DW:
		EMIT2(add_1mod(0x48, dst_reg), 0xC7);
		break;
	}

	emit_insn_suffix(&prog, dst_reg, BPF_REG_0, insn_off);
	EMIT(imm32, bpf_size_to_x86_bytes(BPF_SIZE(insn->code)));
	*pprog = prog;
}

static void emit_st_r12(u8 **pprog, u32 size, u32 dst_reg, int off, int imm)
{
	emit_st_index(pprog, size, dst_reg, X86_REG_R12, off, imm);
}

static void emit_store_stack_imm64(u8 **pprog, int reg, int stack_off, u64 imm64)
{
	/*
	 * mov reg, imm64
	 * mov QWORD PTR [rbp + stack_off], reg
	 */
	emit_mov_imm64(pprog, reg, imm64 >> 32, (u32) imm64);
	emit_stx(pprog, BPF_DW, BPF_REG_FP, reg, stack_off);
}

static int emit_atomic_rmw(u8 **pprog, u32 atomic_op,
			   u32 dst_reg, u32 src_reg, s16 off, u8 bpf_size)
{
	u8 *prog = *pprog;

	if (atomic_op != BPF_XCHG)
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

static int emit_atomic_rmw_index(u8 **pprog, u32 atomic_op, u32 size,
				 u32 dst_reg, u32 src_reg, u32 index_reg,
				 int off)
{
	u8 *prog = *pprog;

	if (atomic_op != BPF_XCHG)
		EMIT1(0xF0); /* lock prefix */

	switch (size) {
	case BPF_W:
		EMIT1(add_3mod(0x40, dst_reg, src_reg, index_reg));
		break;
	case BPF_DW:
		EMIT1(add_3mod(0x48, dst_reg, src_reg, index_reg));
		break;
	default:
		pr_err("bpf_jit: 1- and 2-byte RMW atomics are not supported\n");
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
	emit_insn_suffix_SIB(&prog, dst_reg, src_reg, index_reg, off, 0);
	*pprog = prog;
	return 0;
}

static int emit_atomic_ld_st(struct bpf_verifier_env *env, u8 **pprog,
			     struct bpf_insn *insn, u8 *ip, u32 dst_reg,
			     u32 src_reg, bool accesses_stack_only)
{
	u32 atomic_op = insn->imm;
	int err;

	switch (atomic_op) {
	case BPF_LOAD_ACQ:
		if (!accesses_stack_only) {
			err = emit_kasan_check(env, pprog, src_reg, insn->off,
					       BPF_SIZE(insn->code), ip, false);
			if (err)
				return err;
		}
		/* dst_reg = smp_load_acquire(src_reg + off16) */
		emit_ldx(pprog, BPF_SIZE(insn->code), dst_reg, src_reg,
			 insn->off);
		break;
	case BPF_STORE_REL:
		if (!accesses_stack_only) {
			err = emit_kasan_check(env, pprog, dst_reg, insn->off,
					       BPF_SIZE(insn->code), ip, true);
			if (err)
				return err;
		}
		/* smp_store_release(dst_reg + off16, src_reg) */
		emit_stx(pprog, BPF_SIZE(insn->code), dst_reg, src_reg,
			 insn->off);
		break;
	default:
		pr_err("bpf_jit: unknown atomic load/store opcode %02x\n",
		       atomic_op);
		return -EFAULT;
	}

	return 0;
}

static int emit_atomic_ld_st_index(u8 **pprog, u32 atomic_op, u32 size,
				   u32 dst_reg, u32 src_reg, u32 index_reg,
				   int off)
{
	switch (atomic_op) {
	case BPF_LOAD_ACQ:
		/* dst_reg = smp_load_acquire(src_reg + idx_reg + off16) */
		emit_ldx_index(pprog, size, dst_reg, src_reg, index_reg, off);
		break;
	case BPF_STORE_REL:
		/* smp_store_release(dst_reg + idx_reg + off16, src_reg) */
		emit_stx_index(pprog, size, dst_reg, src_reg, index_reg, off);
		break;
	default:
		pr_err("bpf_jit: unknown atomic load/store opcode %02x\n",
		       atomic_op);
		return -EFAULT;
	}

	return 0;
}

/*
 * A fetching AND/OR/XOR can't be implemented with a single x86 insn, so do a
 * CMPXCHG loop. @index_reg is X86_REG_R12 for an arena access or -1 otherwise.
 * Unless @r0_on_stack, the caller has substituted BPF_REG_AX for R0 in
 * @dst_reg and @src_reg.
 *
 * Both the load and the CMPXCHG can fault when accessing the arena, so their
 * addresses are handed back in @fault[0] and @fault[1]. A fault has to resume
 * at @resume, which is past the loop and past the move that delivers the old
 * value, but before R0 is restored. Both are NULL for the non-arena case,
 * which needs no fixup.
 *
 * R0 is expected to be saved in BPF_REG_AX by the caller, or pushed on the
 * stack if @r0_on_stack. In that case an R0 operand stays in the stack slot,
 * RAX ends up holding the old value that R0 is to return, and the slot is
 * dropped rather than popped.
 */
static int emit_atomic_fetch_rmw(u8 **pprog, struct bpf_insn *insn, u32 dst_reg,
				 u32 src_reg, int index_reg, bool r0_on_stack,
				 u8 **fault, u8 **resume)
{
	bool is64 = BPF_SIZE(insn->code) == BPF_DW;
	bool operand_on_stack = r0_on_stack && src_reg == BPF_REG_0;
	u8 *branch_target, *prog = *pprog;
	int err;

	branch_target = prog;

	/* Load old value */
	if (fault)
		fault[0] = prog;
	if (index_reg < 0)
		emit_ldx(&prog, BPF_SIZE(insn->code), BPF_REG_0, dst_reg, insn->off);
	else
		emit_ldx_index(&prog, BPF_SIZE(insn->code), BPF_REG_0, dst_reg,
			       index_reg, insn->off);

	/*
	 * Perform the (commutative) operation locally, put the result in
	 * the AUX_REG.
	 */
	if (operand_on_stack) {
		/* mov r11, [rsp]: the operand is the R0 saved there, RAX is the old value */
		if (is64)
			EMIT4(0x4C, 0x8B, 0x1C, 0x24);
		else
			EMIT4(0x44, 0x8B, 0x1C, 0x24);
	} else {
		emit_mov_reg(&prog, is64, AUX_REG, BPF_REG_0);
	}
	maybe_emit_mod(&prog, AUX_REG, src_reg, is64);
	EMIT2(simple_alu_opcodes[BPF_OP(insn->imm)],
	      add_2reg(0xC0, AUX_REG, src_reg));

	/* Attempt to swap in new value */
	if (fault)
		fault[1] = prog;
	if (index_reg < 0)
		err = emit_atomic_rmw(&prog, BPF_CMPXCHG, dst_reg, AUX_REG,
				      insn->off, BPF_SIZE(insn->code));
	else
		err = emit_atomic_rmw_index(&prog, BPF_CMPXCHG, BPF_SIZE(insn->code),
					    dst_reg, AUX_REG, index_reg, insn->off);
	if (WARN_ON(err))
		return err;

	/* ZF tells us whether we won the race. If it's cleared we need to try again. */
	EMIT2(X86_JNE, -(prog - branch_target) - 2);
	/* Return the pre-modification value, which RAX already is for R0 */
	if (!operand_on_stack)
		emit_mov_reg(&prog, is64, src_reg, BPF_REG_0);

	if (resume)
		*resume = prog;

	/* Restore R0 after clobbering RAX */
	if (!r0_on_stack)
		emit_mov_reg(&prog, true, BPF_REG_0, BPF_REG_AX);
	else if (operand_on_stack)
		EMIT2(0x41, 0x5B); /* pop r11, dropping the operand */
	else
		EMIT1(0x58); /* pop rax */

	*pprog = prog;
	return 0;
}

/*
 * Metadata encoding for exception handling in JITed code.
 *
 * Format of `fixup` and `data` fields in `struct exception_table_entry`:
 *
 * Bit layout of `fixup` (32-bit):
 *
 * +-----------+-------------+--------+-----------+---------+----------+
 * | 31        | 30          | 29-24  |   23-16   |   15-8  |    7-0   |
 * |           |             |        |           |         |          |
 * | ARENA_ACC | ARENA_WRITE | Unused | ARENA_REG | DST_REG | INSN_LEN |
 * +-----------+-------------+--------+-----------+---------+----------+
 *
 * - INSN_LEN (8 bits): How far past the faulting insn to resume. That is its own length
 *                      for a single-insn access, but the distance to the end of the whole
 *                      sequence where one BPF insn became several, as for the CMPXCHG loop
 *                      of a fetching AND/OR/XOR. A sequence long enough to overflow this
 *                      field would first have to exceed BPF_MAX_INSN_SIZE, which do_jit()
 *                      rejects with -EFAULT before the image is used.
 * - DST_REG  (8 bits): Offset of dst_reg from reg2pt_regs[] (max offset = 112 (fits in 8 bits)).
 *                      This is set to DONT_CLEAR if the insn does not read into a register.
 * - ARENA_REG (8 bits): Offset of the register that is used to calculate the
 *                       address for load/store when accessing the arena region.
 * - ARENA_WRITE (1 bit): This bit is set when the faulting instruction wrote to the arena region.
 *                        It is independent of DST_REG, since a read-modify-write both writes to
 *                        memory and reads the old value into a register.
 * - ARENA_ACCESS (1 bit): This bit is set when the faulting instruction accessed the arena region.
 *
 * Bit layout of `data` (32-bit):
 *
 * +--------------+--------+--------------+
 * |	31-16	  |  15-8  |     7-0      |
 * |              |	   |              |
 * | ARENA_OFFSET | Unused |  EX_TYPE_BPF |
 * +--------------+--------+--------------+
 *
 * - ARENA_OFFSET (16 bits): Offset used to calculate the address for load/store when
 *                           accessing the arena region.
 */

#define DONT_CLEAR 1
#define FIXUP_INSN_LEN_MASK	GENMASK(7, 0)
#define FIXUP_REG_MASK		GENMASK(15, 8)
#define FIXUP_ARENA_REG_MASK	GENMASK(23, 16)
#define FIXUP_ARENA_WRITE	BIT(30)
#define FIXUP_ARENA_ACCESS	BIT(31)
#define DATA_ARENA_OFFSET_MASK	GENMASK(31, 16)

bool ex_handler_bpf(const struct exception_table_entry *x, struct pt_regs *regs)
{
	u32 reg = FIELD_GET(FIXUP_REG_MASK, x->fixup);
	u32 insn_len = FIELD_GET(FIXUP_INSN_LEN_MASK, x->fixup);
	bool is_arena = !!(x->fixup & FIXUP_ARENA_ACCESS);
	bool is_write = !!(x->fixup & FIXUP_ARENA_WRITE);
	unsigned long addr;
	s16 off;
	u32 arena_reg;

	if (is_arena) {
		arena_reg = FIELD_GET(FIXUP_ARENA_REG_MASK, x->fixup);
		off = FIELD_GET(DATA_ARENA_OFFSET_MASK, x->data);
		addr = *(unsigned long *)((void *)regs + arena_reg) + off;
		bpf_prog_report_arena_violation(is_write, addr, regs->ip);
	}

	/* jump over the faulting access, or the sequence it is part of, and clear dest register */
	if (reg != DONT_CLEAR)
		*(unsigned long *)((void *)regs + reg) = 0;
	regs->ip += insn_len;

	return true;
}

/*
 * Record an arena access that may fault. @fault_ip is the address of the
 * faulting insn in the RO image, @resume_off how far past it execution has to
 * resume: for a multi-insn lowering that is the end of the whole sequence, not
 * the end of the one insn.
 */
static int emit_arena_exentry(struct bpf_prog *bpf_prog, u8 *image, u8 *rw_image,
			      int *excnt, u8 *fault_ip, u32 resume_off,
			      u32 fixup_reg, u32 arena_reg, bool is_write, s16 off)
{
	struct exception_table_entry *ex;
	s64 delta;

	if (!bpf_prog->aux->extable)
		return 0;

	if (*excnt >= bpf_prog->aux->num_exentries) {
		pr_err("arena extable bug\n");
		return -EFAULT;
	}
	ex = &bpf_prog->aux->extable[(*excnt)++];

	delta = fault_ip - (u8 *)&ex->insn;
	/* switch ex to rw buffer for writes */
	ex = (void *)rw_image + ((void *)ex - (void *)image);

	ex->insn = delta;
	ex->data = EX_TYPE_BPF | FIELD_PREP(DATA_ARENA_OFFSET_MASK, off);
	ex->fixup = FIELD_PREP(FIXUP_INSN_LEN_MASK, resume_off) |
		    FIELD_PREP(FIXUP_ARENA_REG_MASK, arena_reg) |
		    FIELD_PREP(FIXUP_REG_MASK, fixup_reg) |
		    FIXUP_ARENA_ACCESS;
	if (is_write)
		ex->fixup |= FIXUP_ARENA_WRITE;

	return 0;
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

struct bpf_div_magic {
	u64 m;		/* multiplier, sign extended for signed division */
	int s;		/* shift of the high half of the product */
	bool add;	/* multiplier has 65 bits, use the add sequence */
};

/*
 * Multiplier and shift for an unsigned division by the constant d, with
 * 2 <= d < 2^64 and a dividend below 2^(64 - lz): n / d == (mulhu(n, m)
 * >> s), or with the add sequence when m needs 65 bits, (((n - t) >> 1)
 * + t) >> (s - 1) with t = mulhu(n, m). Hacker's Delight, 2nd ed., figure
 * 10-2.
 */
static void bpf_magic_udiv(u64 d, int lz, struct bpf_div_magic *mag)
{
	u64 mask = ~0ULL >> (lz & 63);
	u64 nc, delta, q1, r1, q2, r2;
	int p;

	mag->add = false;
	/* Largest value with nc mod d == d - 1 */
	nc = mask - (mask % d + 1) % d;
	p = 63;
	q1 = (1ULL << 63) / nc;			/* 2^p / nc */
	r1 = (1ULL << 63) - q1 * nc;		/* 2^p mod nc */
	q2 = (~0ULL >> 1) / d;			/* (2^p - 1) / d */
	r2 = (~0ULL >> 1) - q2 * d;		/* (2^p - 1) mod d */
	do {
		p++;
		if (r1 >= nc - r1) {
			q1 = 2 * q1 + 1;
			r1 = 2 * r1 - nc;
		} else {
			q1 = 2 * q1;
			r1 = 2 * r1;
		}
		if (r2 + 1 >= d - r2) {
			if (q2 >= (~0ULL >> 1))
				mag->add = true;
			q2 = 2 * q2 + 1;
			r2 = 2 * r2 + 1 - d;
		} else {
			if (q2 >= (1ULL << 63))
				mag->add = true;
			q2 = 2 * q2;
			r2 = 2 * r2 + 1;
		}
		delta = d - 1 - r2;
	} while (p < 128 && (q1 < delta || (q1 == delta && r1 == 0)));

	mag->m = q2 + 1;
	mag->s = p - 64;
}

/*
 * Multiplier and shift for a signed division by the constant d, with
 * 2 <= |d| < 2^63: q = mulhs(n, m), q += n if d > 0 and m < 0, q -= n
 * if d < 0 and m > 0, q >>= s (arithmetic), n / d == q + (q >>> 63).
 * Hacker's Delight, 2nd ed., figure 10-1.
 */
static void bpf_magic_sdiv(s64 d, struct bpf_div_magic *mag)
{
	u64 ad = d < 0 ? -(u64)d : (u64)d;
	u64 t, anc, delta, q1, r1, q2, r2;
	int p;

	t = (1ULL << 63) + (d < 0);
	anc = t - 1 - t % ad;			/* |nc| */
	p = 63;
	q1 = (1ULL << 63) / anc;		/* 2^p / |nc| */
	r1 = (1ULL << 63) - q1 * anc;		/* 2^p mod |nc| */
	q2 = (1ULL << 63) / ad;			/* 2^p / |d| */
	r2 = (1ULL << 63) - q2 * ad;		/* 2^p mod |d| */
	do {
		p++;
		q1 = 2 * q1;
		r1 = 2 * r1;
		if (r1 >= anc) {
			q1++;
			r1 -= anc;
		}
		q2 = 2 * q2;
		r2 = 2 * r2;
		if (r2 >= ad) {
			q2++;
			r2 -= ad;
		}
		delta = ad - r2;
	} while (q1 < delta || (q1 == delta && r1 == 0));

	mag->m = q2 + 1;
	if (d < 0)
		mag->m = -mag->m;
	mag->s = p - 64;
	mag->add = false;
}

/*
 * Divide dst_reg by the constant in insn->imm of a 64-bit division through
 * a multiplication with the magic number from bpf_magic_udiv() or
 * bpf_magic_sdiv(). The dividend goes through rax and the multiplier
 * through r11, the caller saves rax and rdx unless they are dst_reg, and
 * r10 is scratch. Returns false for the divisors left to div/idiv.
 */
static bool emit_div_const(u8 **pprog, const struct bpf_insn *insn, u32 dst_reg)
{
	bool is_mod = BPF_OP(insn->code) == BPF_MOD;
	bool is_signed = insn->off == 1;
	u32 n_reg = dst_reg, q_reg;
	s32 imm32 = insn->imm;
	struct bpf_div_magic mag;
	u8 *prog = *pprog;
	int corr = 0, pre = 0, s;

	if (is_signed) {
		/* |d| >= 2, the verifier already rewrote d == -1 */
		if (imm32 >= -1 && imm32 <= 1)
			return false;
		bpf_magic_sdiv(imm32, &mag);
		/* q += n for d > 0 and m < 0, q -= n for d < 0 and m > 0 */
		if (imm32 > 0 && (s64)mag.m < 0)
			corr = 1;
		else if (imm32 < 0 && (s64)mag.m > 0)
			corr = -1;
	} else {
		/* d >= 2, a negative imm32 is a divisor above 2^63 */
		if (imm32 < 2)
			return false;
		bpf_magic_udiv(imm32, 0, &mag);
		/*
		 * Shift the dividend by the trailing zeros of an even divisor
		 * first, the magic number of the odd part then fits without
		 * the add sequence.
		 */
		if (mag.add && !(imm32 & 1)) {
			pre = __ffs(imm32) & 31;
			bpf_magic_udiv(imm32 >> pre, pre, &mag);
			if (mag.add)
				return false;
		}
	}
	s = mag.s;
	/*
	 * The shift is at most 63, or 64 with the add sequence which shifts
	 * by s - 1 and needs s >= 1. Never hit for d >= 2, kept as a guard
	 * against an unencodable count.
	 */
	if (s > 64 - !mag.add || (mag.add && !s))
		return false;

	/* Keep the dividend when the multiplication clobbers it */
	if ((is_mod || mag.add || corr) &&
	    (dst_reg == BPF_REG_0 || dst_reg == BPF_REG_3)) {
		emit_mov_reg(&prog, true, BPF_REG_AX, dst_reg);
		n_reg = BPF_REG_AX;
	}

	/* mov rax, dst_reg; shr rax, pre; mov r11, m; mul r11 or imul r11 */
	if (dst_reg != BPF_REG_0)
		emit_mov_reg(&prog, true, BPF_REG_0, dst_reg);
	if (pre)
		emit_shift_imm(&prog, BPF_RSH, BPF_REG_0, pre, true);
	emit_mov_imm64(&prog, AUX_REG, mag.m >> 32, (u32)mag.m);
	EMIT3(0x49, 0xF7, is_signed ? 0xEB : 0xE3);
	if (is_signed) {
		/* add rdx, n_reg or sub rdx, n_reg */
		if (corr)
			emit_alu_reg(&prog, corr > 0 ? BPF_ADD : BPF_SUB,
				     BPF_REG_3, n_reg, true);
		/* sar rdx, s */
		if (s)
			emit_shift_imm(&prog, BPF_ARSH, BPF_REG_3, s, true);
		/* mov rax, rdx; shr rax, 63; add rdx, rax */
		emit_mov_reg(&prog, true, BPF_REG_0, BPF_REG_3);
		emit_shift_imm(&prog, BPF_RSH, BPF_REG_0, 63, true);
		emit_alu_reg(&prog, BPF_ADD, BPF_REG_3, BPF_REG_0, true);
		q_reg = BPF_REG_3;
	} else if (!mag.add) {
		/* shr rdx, s */
		if (s)
			emit_shift_imm(&prog, BPF_RSH, BPF_REG_3, s, true);
		q_reg = BPF_REG_3;
	} else {
		/* mov rax, n_reg; sub rax, rdx; shr rax, 1 */
		emit_mov_reg(&prog, true, BPF_REG_0, n_reg);
		emit_alu_reg(&prog, BPF_SUB, BPF_REG_0, BPF_REG_3, true);
		emit_shift_imm(&prog, BPF_RSH, BPF_REG_0, 1, true);
		/* add rax, rdx; shr rax, s - 1 */
		emit_alu_reg(&prog, BPF_ADD, BPF_REG_0, BPF_REG_3, true);
		if (s > 1)
			emit_shift_imm(&prog, BPF_RSH, BPF_REG_0, s - 1, true);
		q_reg = BPF_REG_0;
	}

	if (is_mod) {
		/* imul q_reg, q_reg, imm32 */
		maybe_emit_mod(&prog, q_reg, q_reg, true);
		if (is_imm8(imm32))
			EMIT3(0x6B, add_2reg(0xC0, q_reg, q_reg), imm32);
		else
			EMIT2_off32(0x69, add_2reg(0xC0, q_reg, q_reg), imm32);
		/* sub n_reg, q_reg */
		emit_alu_reg(&prog, BPF_SUB, n_reg, q_reg, true);
		q_reg = n_reg;
	}
	if (q_reg != dst_reg)
		emit_mov_reg(&prog, true, dst_reg, q_reg);

	*pprog = prog;
	return true;
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
	emit_ldx(&prog, BPF_DW, BPF_REG_0, BPF_REG_FP, off)
/* mov rax, qword ptr [rbp - rounded_stack_depth - 16] */
#define LOAD_TAIL_CALL_CNT_PTR(stack)				\
	__LOAD_TCC_PTR(BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack))

/* Memory size/value to protect private stack overflow/underflow */
#define PRIV_STACK_GUARD_SZ    8
#define PRIV_STACK_GUARD_VAL   0xEB9F12345678eb9fULL

static int emit_spectre_bhb_barrier(u8 **pprog, u8 *ip,
				    struct bpf_prog *bpf_prog)
{
	u8 *prog = *pprog;
	u8 *func;

	if (cpu_feature_enabled(X86_FEATURE_CLEAR_BHB_LOOP)) {
		/* The clearing sequence clobbers eax and ecx. */
		EMIT1(0x50); /* push rax */
		EMIT1(0x51); /* push rcx */
		ip += 2;

		func = (u8 *)clear_bhb_loop;
		ip += x86_call_depth_emit_accounting(&prog, func, ip);

		if (emit_call(&prog, func, ip))
			return -EINVAL;
		EMIT1(0x59); /* pop rcx */
		EMIT1(0x58); /* pop rax */
	}
	/* Insert IBHF instruction */
	if ((cpu_feature_enabled(X86_FEATURE_CLEAR_BHB_LOOP) &&
	     cpu_feature_enabled(X86_FEATURE_HYPERVISOR)) ||
	    cpu_feature_enabled(X86_FEATURE_CLEAR_BHB_HW)) {
		/*
		 * Add an Indirect Branch History Fence (IBHF). IBHF acts as a
		 * fence preventing branch history from before the fence from
		 * affecting indirect branches after the fence. This is
		 * specifically used in cBPF jitted code to prevent Intra-mode
		 * BHI attacks. The IBHF instruction is designed to be a NOP on
		 * hardware that doesn't need or support it.  The REP and REX.W
		 * prefixes are required by the microcode, and they also ensure
		 * that the NOP is unlikely to be used in existing code.
		 *
		 * IBHF is not a valid instruction in 32-bit mode.
		 */
		EMIT5(0xF3, 0x48, 0x0F, 0x1E, 0xF8); /* ibhf */
	}
	*pprog = prog;
	return 0;
}

static const struct bpf_jit_arg_abi x86_arg_abi = {
	.nr_arg_regs		= 6,
	.backfill_after_stack	= true,
	.even_stack_align	= true,
};

static const u8 x86_arg_reg[] = {
	BPF_REG_1, BPF_REG_2, BPF_REG_3, BPF_REG_4, BPF_REG_5, X86_REG_R9,
};

static bool x86_arg_on_stack(u8 pos)
{
	return pos >= x86_arg_abi.nr_arg_regs;
}

static s32 x86_arg_stack_off(u8 pos)
{
	return (pos - x86_arg_abi.nr_arg_regs) * 8;
}

/*
 * Move the arguments the x86-64 ABI places somewhere other than the argument
 * slot the BPF calling convention gave them. @stack_base addresses the
 * outgoing stack argument area from RBP. Return the number of emitted bytes.
 */
static int emit_kfunc_arg_moves(const struct btf_func_model *fm, s32 stack_base, u8 **pprog)
{
	struct bpf_jit_arg_move moves[BPF_JIT_MAX_ARG_MOVES];
	const u8 nreg = x86_arg_abi.nr_arg_regs;
	u8 *prog = *pprog, *start = prog;
	u32 i, n;

	n = bpf_jit_plan_arg_moves(&x86_arg_abi, fm, moves);

	for (i = 0; i < n; i++) {
		u8 dst = moves[i].dst, src = moves[i].src, reg;
		bool dst_mem = dst != BPF_JIT_ARG_TMP && dst >= nreg;
		bool src_mem = src != BPF_JIT_ARG_TMP && src >= nreg;

		/*
		 * Take the value into a register: the one it belongs in, the
		 * scratch when it is carried past its own destination, and
		 * BPF_REG_AX only to pass one stack slot to another.
		 */
		if (src == BPF_JIT_ARG_TMP) {
			reg = AUX_REG;
		} else if (src_mem) {
			reg = dst == BPF_JIT_ARG_TMP ? AUX_REG :
			      dst_mem ? BPF_REG_AX : x86_arg_reg[dst];
			emit_ldx(&prog, BPF_DW, reg, BPF_REG_FP,
				 stack_base + (src - nreg) * 8);
		} else {
			reg = x86_arg_reg[src];
		}

		/* And leave it where the argument belongs. */
		if (dst == BPF_JIT_ARG_TMP)
			emit_mov_reg(&prog, true, AUX_REG, reg);
		else if (dst_mem)
			emit_stx(&prog, BPF_DW, BPF_REG_FP, reg,
				 stack_base + (dst - nreg) * 8);
		else if (reg != x86_arg_reg[dst])
			emit_mov_reg(&prog, true, x86_arg_reg[dst], reg);
	}

	*pprog = prog;
	return prog - start;
}

/*
 * Rebase the __arena args of a kfunc call to arena kernel addresses,
 * rN = kern_vm_start + (u32)rN, with R12 holding kern_vm_start. A nullable
 * arg preserves NULL by skipping the add, tested on the truncated value as
 * arena NULL is offset 0. Return the number of emitted bytes.
 */
static int emit_kfunc_arena_args(struct bpf_prog *bpf_prog,
				 const struct btf_func_model *fm, u8 **pprog)
{
	u8 *prog = *pprog;
	u8 *start = prog;
	int i, slot;

	for (i = 0, slot = 0; i < fm->nr_args; i++) {
		u32 arg_regs = (fm->arg_size[i] + 7) / 8;
		u8 flags = fm->arg_flags[i];
		u32 reg;

		if (slot + arg_regs > MAX_BPF_FUNC_REG_ARGS)
			break;
		reg = BPF_REG_1 + slot;
		slot += arg_regs;

		if (!(flags & BTF_FMODEL_ARENA_ARG))
			continue;
		if (WARN_ON_ONCE(!bpf_prog_uses_arena_insns(bpf_prog)))
			return -EINVAL;

		/* mov eN, eN: truncate and clear the upper 32 bits */
		emit_mov_reg(&prog, false, reg, reg);
		if (flags & BTF_FMODEL_NULLABLE_ARG) {
			/* test eN, eN; jz over the 3-byte add */
			maybe_emit_mod(&prog, reg, reg, false);
			EMIT2(0x85, add_2reg(0xC0, reg, reg));
			EMIT2(X86_JE, 3);
		}
		/* add rN, r12 */
		maybe_emit_mod(&prog, reg, X86_REG_R12, true);
		EMIT2(0x01, add_2reg(0xC0, reg, X86_REG_R12));
	}

	*pprog = prog;
	return prog - start;
}

/*
 * A strict compare against 1 or 128 is the non-strict one against 0 or
 * 127, a strict one against -129 or, with a signed condition, -1 the
 * non-strict one against -128 or 0: the compare against 0 is a test
 * without immediate, 127 and -128 fit into an imm8.
 */
static void adjust_cmp_imm(u32 *op, s32 *imm)
{
	if (*imm == 1 || *imm == 128) {
		switch (*op) {
		case BPF_JLT:
			*op = BPF_JLE;
			break;
		case BPF_JGE:
			*op = BPF_JGT;
			break;
		case BPF_JSLT:
			*op = BPF_JSLE;
			break;
		case BPF_JSGE:
			*op = BPF_JSGT;
			break;
		default:
			return;
		}
		*imm -= 1;
	} else if (*imm == -1 || *imm == -129) {
		switch (*op) {
		case BPF_JGT:
		case BPF_JLE:
			/* -1 is the largest unsigned value */
			if (*imm == -1)
				return;
			*op = *op == BPF_JGT ? BPF_JGE : BPF_JLT;
			break;
		case BPF_JSGT:
			*op = BPF_JSGE;
			break;
		case BPF_JSLE:
			*op = BPF_JSLT;
			break;
		default:
			return;
		}
		*imm += 1;
	}
}

/*
 * There is no branch for unwind_goto, only an extable entry
 *   { .insn = ret_addr - 1, .fixup = target - ret_addr }
 * where ret_addr is the return address of the call in front of it. unwind_exit
 * replaces the return address of its frame with bpf_unwind_target() of it.
 * ret_addr - 1, because what follows the call can have an entry of its own.
 * No insn starts there, so a fault cannot hit the entry.
 */
static int add_unwind_exentry(struct bpf_prog *prog, u8 *image, u8 *rw_image, int *excnt,
			      int ret_addr, int target)
{
	struct exception_table_entry *ex;
	s64 delta;

	if (!prog->aux->extable)
		return 0;
	if (*excnt >= prog->aux->num_exentries) {
		pr_err("unwind extable bug\n");
		return -EFAULT;
	}
	ex = &prog->aux->extable[(*excnt)++];
	delta = image + ret_addr - 1 - (u8 *)&ex->insn;
	if (!is_simm32(delta)) {
		pr_err("extable->insn doesn't fit into 32-bit\n");
		return -EFAULT;
	}
	/* switch ex to rw buffer for writes */
	ex = (void *)rw_image + ((void *)ex - (void *)image);
	ex->insn = delta;
	ex->fixup = target - ret_addr;
	ex->data = EX_TYPE_BPF;
	return 0;
}

static u64 bpf_unwind_target(u64 ret_addr)
{
	const struct exception_table_entry *e = search_bpf_extables(ret_addr - 1);

	/*
	 * The verifier made sure that unwind_goto follows a call in a prog. When
	 * the kernel called an async callback, it's exit.
	 */
	return e ? ret_addr + e->fixup : ret_addr;
}

/* unwind_exit: fix the return address of this frame. r0 and r2 are returned. */
static int emit_unwind_ret_addr(u8 **pprog, u8 *ip)
{
	u8 *prog = *pprog, *start = prog;

	EMIT2(0x50, 0x56);			/* push rax; push rsi */
	EMIT4(0x48, 0x8B, 0x7D, 0x08);		/* mov rdi, [rbp + 8] */
	ip += prog - start;
	ip += x86_call_depth_emit_accounting(&prog, bpf_unwind_target, ip);
	if (emit_call(&prog, bpf_unwind_target, ip))
		return -EINVAL;
	EMIT4(0x48, 0x89, 0x45, 0x08);		/* mov [rbp + 8], rax */
	EMIT2(0x5E, 0x58);			/* pop rsi; pop rax */
	*pprog = prog;
	return 0;
}

static int do_jit(struct bpf_verifier_env *env, struct bpf_prog *bpf_prog, int *addrs, u8 *image,
		  u8 *rw_image, int oldproglen, struct jit_context *ctx, bool jmp_padding)
{
	bool tail_call_reachable = bpf_prog->aux->tail_call_reachable;
	struct bpf_insn *insn = bpf_prog->insnsi;
	bool callee_regs_used[4] = {};
	int insn_cnt = bpf_prog->len;
	bool seen_exit = false;
	u8 temp[BPF_MAX_INSN_SIZE + BPF_INSN_SAFETY];
	void __percpu *priv_frame_ptr = NULL;
	u16 out_stack_arg_cnt, outgoing_rsp;
	u64 arena_vm_start = 0, user_vm_start = 0;
	void __percpu *priv_stack_ptr;
	int i, excnt = 0;
	int ilen, proglen = 0;
	u8 *ip, *prog = temp;
	u32 stack_depth;
	int callee_saved_size;
	s32 outgoing_arg_base;
	int err;

	stack_depth = bpf_prog->aux->stack_depth;
	out_stack_arg_cnt = bpf_out_stack_arg_cnt(env, bpf_prog);
	priv_stack_ptr = bpf_prog->aux->priv_stack_ptr;
	if (priv_stack_ptr) {
		priv_frame_ptr = priv_stack_ptr + PRIV_STACK_GUARD_SZ + round_up(stack_depth, 8);
		stack_depth = 0;
	}

	/*
	 * Follow x86-64 calling convention for both BPF-to-BPF and
	 * kfunc calls:
	 *   - Arg 6 is passed in R9 register
	 *   - Args 7+ are passed on the stack at [rsp]
	 *
	 * Incoming arg 6 is read from R9 (BPF r11+8 → MOV from R9).
	 * Incoming args 7+ are read from [rbp + 16], [rbp + 24], ...
	 * (BPF r11+16, r11+24, ... map directly with no offset change).
	 *
	 * tail_call_reachable is rejected by the verifier and priv_stack
	 * is disabled by the JIT when stack args exist, so R9 is always
	 * available.
	 *
	 * Stack layout (high to low):
	 *   [rbp + 16 + ...]    incoming stack args 7+ (from caller)
	 *   [rbp + 8]           return address
	 *   [rbp]               saved rbp
	 *   [rbp - prog_stack]  program stack
	 *   [below]             callee-saved regs
	 *   [below]             outgoing args 7+ (= rsp)
	 */
	if (bpf_prog_uses_arena_insns(bpf_prog)) {
		arena_vm_start = bpf_arena_get_kern_vm_start(bpf_prog->aux->arena);
		user_vm_start = bpf_arena_get_user_vm_start(bpf_prog->aux->arena);
	}

	detect_reg_usage(insn, insn_cnt, callee_regs_used);

	emit_prologue(&prog, image, stack_depth,
		      bpf_prog_was_classic(bpf_prog), tail_call_reachable,
		      bpf_is_subprog(bpf_prog), bpf_prog->aux->exception_cb);

	bpf_prog->aux->ksym.fp_start = prog - temp;

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

	/* Compute callee-saved register area size. */
	callee_saved_size = 0;
	if (bpf_prog->aux->exception_boundary || arena_vm_start)
		callee_saved_size += 8; /* r12 */
	if (bpf_prog->aux->exception_boundary) {
		callee_saved_size += 4 * 8; /* rbx, r13, r14, r15 */
	} else {
		int j;

		for (j = 0; j < 4; j++)
			if (callee_regs_used[j])
				callee_saved_size += 8;
	}
	/*
	 * Base offset from rbp for translating BPF outgoing args 7+
	 * to native offsets. BPF uses negative offsets from r11
	 * (r11-8 for arg6, r11-16 for arg7, ...) while x86 uses
	 * positive offsets from rsp ([rsp+0] for arg7, [rsp+8] for
	 * arg8, ...). Arg 6 goes to R9 directly.
	 *
	 * The translation reverses direction:
	 *   native_off = outgoing_arg_base - outgoing_rsp - bpf_off - 16
	 *
	 * Note that tail_call_reachable is guaranteed to be false when
	 * stack args exist, so tcc pushes need not be accounted for.
	 */
	outgoing_arg_base = -(round_up(stack_depth, 8) + callee_saved_size);

	/*
	 * Allocate outgoing stack arg area for args 7+ only.
	 * Arg 6 goes into r9 register, not on stack.
	 */
	outgoing_rsp = out_stack_arg_cnt > 1 ? (out_stack_arg_cnt - 1) * 8 : 0;
	if (bpf_prog->aux->exception_boundary)
		bpf_prog->aux->stack_arg_sp_adjust = outgoing_rsp;
	emit_sub_rsp(&prog, outgoing_rsp);

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
		bool accesses_stack_only;
		u8 b3 = 0;
		u8 *start_of_ldx;
		s64 jmp_offset;
		s32 insn_off;
		int insn_idx;
		u8 jmp_cond;
		u8 *func;
		int nops;
		u32 jmp_op = BPF_OP(insn->code);
		s32 cmp_imm;

		if (priv_frame_ptr) {
			if (src_reg == BPF_REG_FP)
				src_reg = X86_REG_R9;

			if (dst_reg == BPF_REG_FP)
				dst_reg = X86_REG_R9;
		}

		if (bpf_insn_is_indirect_target(env, bpf_prog, i - 1))
			EMIT_ENDBR();

		ip = image + addrs[i - 1] + (prog - temp);
		insn_idx = i - 1 + bpf_prog->aux->subprog_start;
		accesses_stack_only =
			env ? !env->insn_aux_data[insn_idx].non_stack_access :
			      false;

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
			emit_alu_reg(&prog, BPF_OP(insn->code), dst_reg, src_reg,
				     BPF_CLASS(insn->code) == BPF_ALU64);
			break;

		case BPF_ALU64 | BPF_MOV | BPF_X:
			if (insn_is_cast_user(insn)) {
				/* mov dst_reg32, src_reg32 to zero extend the low half */
				emit_mov_reg(&prog, false, dst_reg, src_reg);
				/* mov r11, the high half of user_vm_start; or r11, dst_reg */
				emit_mov_imm64(&prog, AUX_REG, user_vm_start >> 32, 0);
				emit_alu_reg(&prog, BPF_OR, AUX_REG, dst_reg, true);
				/* test dst_reg32, dst_reg32; a NULL pointer stays NULL */
				maybe_emit_mod(&prog, dst_reg, dst_reg, false);
				EMIT2(0x85, add_2reg(0xC0, dst_reg, dst_reg));
				/* cmovne dst_reg, r11 */
				/* WARNING: Intel swapped src/dst register encoding in CMOVcc !!! */
				maybe_emit_mod(&prog, AUX_REG, dst_reg, true);
				EMIT3(0x0F, 0x45, add_2reg(0xC0, AUX_REG, dst_reg));
				break;
			} else if (insn_is_mov_percpu_addr(insn)) {
				/* mov <dst>, <src> (if necessary) */
				EMIT_mov(dst_reg, src_reg);
#ifdef CONFIG_SMP
				/* add <dst>, gs:[<off>] */
				EMIT2(0x65, add_2mod(0x48, 0, dst_reg));
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
			emit_neg(&prog, dst_reg, BPF_CLASS(insn->code) == BPF_ALU64);
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
			if (BPF_OP(insn->code) == BPF_AND &&
			    (imm32 == 0xff || imm32 == 0xffff)) {
				/* movzx dst_reg, dst_reg8/16 */
				emit_movzx_reg(&prog, imm32 == 0xff ? 8 : 16,
					       dst_reg, dst_reg);
				break;
			}
			if (BPF_OP(insn->code) == BPF_XOR && imm32 == -1) {
				/* not dst_reg */
				maybe_emit_1mod(&prog, dst_reg,
						BPF_CLASS(insn->code) == BPF_ALU64);
				EMIT2(0xF7, add_1reg(0xD0, dst_reg));
				break;
			}

			emit_alu_imm(&prog, BPF_OP(insn->code), dst_reg, imm32,
				     BPF_CLASS(insn->code) == BPF_ALU64);
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

			/* LLVM strength-reduces the 32-bit ones itself */
			if (is64 && BPF_SRC(insn->code) == BPF_K &&
			    emit_div_const(&prog, insn, dst_reg))
				goto div_done;

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
				emit_mov_imm32(&prog, false, BPF_REG_3, 0);

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
div_done:
			if (dst_reg != BPF_REG_3)
				EMIT1(0x5A); /* pop rdx */
			if (dst_reg != BPF_REG_0)
				EMIT1(0x58); /* pop rax */
			break;
		}

		case BPF_ALU | BPF_MUL | BPF_K:
		case BPF_ALU64 | BPF_MUL | BPF_K:
			/*
			 * lea for 3, 5 and 9, which LLVM leaves as a
			 * multiplication, but not for r13, which needs a
			 * displacement as base and makes the lea a slow three
			 * operand form on Intel.
			 */
			if ((imm32 == 3 || imm32 == 5 || imm32 == 9) &&
			    !base_needs_disp(dst_reg)) {
				/* lea dst_reg, [dst_reg + dst_reg * (imm32 - 1)] */
				if (BPF_CLASS(insn->code) == BPF_ALU64)
					EMIT1(add_3mod(0x48, dst_reg, dst_reg, dst_reg));
				else if (is_ereg(dst_reg))
					EMIT1(add_3mod(0x40, dst_reg, dst_reg, dst_reg));
				EMIT1(0x8D);
				emit_insn_suffix_SIB(&prog, dst_reg, dst_reg, dst_reg, 0,
						     ilog2(imm32 - 1));
				break;
			}

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
			emit_shift_imm(&prog, BPF_OP(insn->code), dst_reg,
				       imm32, BPF_CLASS(insn->code) == BPF_ALU64);
			break;

		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_ARSH | BPF_X:
		case BPF_ALU64 | BPF_LSH | BPF_X:
		case BPF_ALU64 | BPF_RSH | BPF_X:
		case BPF_ALU64 | BPF_ARSH | BPF_X:
			/*
			 * BMI2 shifts take the count from any register and,
			 * unlike the cl forms, are a single uop on Intel, so
			 * prefer them also when the count already is in rcx.
			 */
			if (boot_cpu_has(X86_FEATURE_BMI2)) {
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
				/* Emit 'bswap eax' to swap lower 4 bytes */
				if (is_ereg(dst_reg))
					EMIT1(0x41);
				EMIT2(0x0F, add_1reg(0xC8, dst_reg));

				/*
				 * Emit 'shr eax, 16' to move the swapped lower
				 * 2 bytes back down and zero extend
				 */
				emit_shift_imm(&prog, BPF_RSH, dst_reg, 16, false);
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
				emit_movzx_reg(&prog, 16, dst_reg, dst_reg);
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

		case BPF_ST | BPF_MEM | BPF_B:
		case BPF_ST | BPF_MEM | BPF_H:
		case BPF_ST | BPF_MEM | BPF_W:
		case BPF_ST | BPF_MEM | BPF_DW:
			if (dst_reg == BPF_REG_PARAMS && insn->off == -8) {
				/* Arg 6: store immediate in r9 register */
				emit_mov_imm64(&prog, X86_REG_R9, imm32 >> 31,
					       imm32);
				break;
			}
			insn_off = insn->off;
			if (dst_reg == BPF_REG_PARAMS) {
				/*
				 * Args 7+: reverse BPF negative offsets to
				 * x86 positive rsp offsets.
				 * BPF off=-16 → [rsp+0], off=-24 → [rsp+8], ...
				 */
				insn_off = outgoing_arg_base - outgoing_rsp -
					   insn_off - 16;
				dst_reg = BPF_REG_FP;
			}
			if (!accesses_stack_only) {
				err = emit_kasan_check(env, &prog, dst_reg,
						       insn_off,
						       BPF_SIZE(insn->code), ip,
						       true);
				if (err)
					return err;
			}

			emit_st(&prog, insn, dst_reg, insn_off);
			break;

			/* STX: *(u8*)(dst_reg + off) = src_reg */
		case BPF_STX | BPF_MEM | BPF_B:
		case BPF_STX | BPF_MEM | BPF_H:
		case BPF_STX | BPF_MEM | BPF_W:
		case BPF_STX | BPF_MEM | BPF_DW:
			if (dst_reg == BPF_REG_PARAMS && insn->off == -8) {
				/* Arg 6: store register value in r9 */
				EMIT_mov(X86_REG_R9, src_reg);
				break;
			}
			insn_off = insn->off;
			if (dst_reg == BPF_REG_PARAMS) {
				insn_off = outgoing_arg_base - outgoing_rsp - insn_off - 16;
				dst_reg = BPF_REG_FP;
			}
			if (!accesses_stack_only) {
				err = emit_kasan_check(env, &prog, dst_reg,
						       insn_off,
						       BPF_SIZE(insn->code), ip,
						       true);
				if (err)
					return err;
			}
			emit_stx(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn_off);
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
		case BPF_LDX | BPF_PROBE_MEM32SX | BPF_B:
		case BPF_LDX | BPF_PROBE_MEM32SX | BPF_H:
		case BPF_LDX | BPF_PROBE_MEM32SX | BPF_W:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_B:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_H:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_W:
		case BPF_STX | BPF_PROBE_MEM32 | BPF_DW:
			start_of_ldx = prog;
			if (BPF_CLASS(insn->code) == BPF_LDX) {
				if (BPF_MODE(insn->code) == BPF_PROBE_MEM32SX)
					emit_ldsx_r12(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
				else
					emit_ldx_r12(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
			} else {
				emit_stx_r12(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
			}
populate_extable:
			{
				u32 arena_reg, fixup_reg;
				bool is_write;

				/*
				 * src_reg/dst_reg holds the address in the arena region with upper
				 * 32-bits being zero because of a preceding addr_space_cast(r<n>,
				 * 0x0, 0x1) instruction. This address is adjusted with the addition
				 * of arena_vm_start (see the implementation of BPF_PROBE_MEM32 and
				 * BPF_PROBE_ATOMIC) before being used for the memory access. Pass
				 * the reg holding the unmodified 32-bit address to
				 * ex_handler_bpf().
				 *
				 * A load-acquire is of BPF_STX class, but reads from src_reg
				 * into dst_reg like a BPF_LDX does, hence it must not be
				 * treated as a store here.
				 */
				if (BPF_CLASS(insn->code) == BPF_LDX ||
				    bpf_atomic_is_load_acq(insn)) {
					arena_reg = reg2pt_regs[src_reg];
					fixup_reg = reg2pt_regs[dst_reg];
					is_write = false;
				} else {
					/*
					 * A store has no destination register to clear,
					 * except for a read-modify-write with BPF_FETCH,
					 * which also reads the old value into src_reg, or
					 * into r0 for a BPF_CMPXCHG. Either way the access
					 * is still reported as a write.
					 */
					int load_reg = bpf_atomic_load_reg(insn);

					arena_reg = reg2pt_regs[dst_reg];
					fixup_reg = load_reg < 0 ? DONT_CLEAR :
						    reg2pt_regs[load_reg];
					is_write = true;
				}

				err = emit_arena_exentry(bpf_prog, image, rw_image, &excnt,
							 image + proglen + (start_of_ldx - temp),
							 prog - start_of_ldx, fixup_reg,
							 arena_reg, is_write, insn->off);
				if (err)
					return err;
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
			if (src_reg == BPF_REG_PARAMS) {
				if (insn_off == 8) {
					/* Incoming arg 6: read from r9 */
					EMIT_mov(dst_reg, X86_REG_R9);
					break;
				}
				src_reg = BPF_REG_FP;
				/*
				 * Incoming args 7+: native_off == bpf_off
				 * (r11+16 → [rbp+16], r11+24 → [rbp+24], ...)
				 * No offset adjustment needed.
				 */
			}

			if (BPF_MODE(insn->code) == BPF_PROBE_MEM ||
			    BPF_MODE(insn->code) == BPF_PROBE_MEMSX) {
				/* Conservatively check that src_reg + insn->off is a kernel address:
				 *   src_reg + insn->off > TASK_SIZE_MAX + PAGE_SIZE
				 *   and
				 *   src_reg + insn->off < VSYSCALL_ADDR
				 *
				 * The check is done on src_reg + insn->off - VSYSCALL_ADDR
				 * against the next power of two above the limit. Addresses
				 * between the limit and that power of two are non-canonical
				 * and would fault anyway, so they get zeroed a bit earlier.
				 */

				u64 limit = TASK_SIZE_MAX + PAGE_SIZE - VSYSCALL_ADDR;
				u8 *end_of_jmp;

				/*
				 * lea r11, [src_reg + insn->off - VSYSCALL_ADDR]
				 *
				 * VSYSCALL_ADDR is a small negative number, so the
				 * whole offset fits into the 32-bit displacement.
				 */
				BUILD_BUG_ON((long)(s32)VSYSCALL_ADDR != (long)VSYSCALL_ADDR);
				EMIT2(add_2mod(0x48, src_reg, AUX_REG), 0x8D);
				emit_insn_suffix(&prog, src_reg, AUX_REG,
						 insn->off - (s32)VSYSCALL_ADDR);

				/* shr r11, fls64(limit) */
				emit_shift_imm(&prog, BPF_RSH, AUX_REG, fls64(limit), true);

				/* if not zero, goto load */
				EMIT2(X86_JNE, 0);
				end_of_jmp = prog;

				/* xor dst_reg, dst_reg */
				emit_mov_imm32(&prog, false, dst_reg, 0);
				/* jmp byte_after_ldx */
				EMIT2(0xEB, 0);

				/* populate jmp_offset for JNE above to jump to start_of_ldx */
				start_of_ldx = prog;
				end_of_jmp[-1] = start_of_ldx - end_of_jmp;
			} else if (!accesses_stack_only) {
				err = emit_kasan_check(env, &prog, src_reg,
						       insn_off,
						       BPF_SIZE(insn->code), ip,
						       false);
				if (err)
					return err;
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
				ex->fixup = FIELD_PREP(FIXUP_INSN_LEN_MASK, prog - start_of_ldx) |
					    FIELD_PREP(FIXUP_REG_MASK, reg2pt_regs[dst_reg]);
			}
			break;

		case BPF_STX | BPF_ATOMIC | BPF_B:
		case BPF_STX | BPF_ATOMIC | BPF_H:
			if (!bpf_atomic_is_load_store(insn)) {
				pr_err("bpf_jit: 1- and 2-byte RMW atomics are not supported\n");
				return -EFAULT;
			}
			fallthrough;
		case BPF_STX | BPF_ATOMIC | BPF_W:
		case BPF_STX | BPF_ATOMIC | BPF_DW: {
			u32 real_src_reg = src_reg;
			u32 real_dst_reg = dst_reg;
			bool is_atomic_fetch = is_atomic_fetch_op(insn);
			u8 *old_prog;

			if (is_atomic_fetch) {
				/* Will need RAX as a CMPXCHG operand so save R0 */
				old_prog = prog;
				emit_mov_reg(&prog, true, BPF_REG_AX, BPF_REG_0);
				if (src_reg == BPF_REG_0)
					real_src_reg = BPF_REG_AX;
				if (dst_reg == BPF_REG_0)
					real_dst_reg = BPF_REG_AX;
				ip += (prog - old_prog);
			}
			if (!bpf_atomic_is_load_store(insn)) {
				if (!accesses_stack_only) {
					err = emit_kasan_check(
						env, &prog, real_dst_reg,
						insn->off, BPF_SIZE(insn->code),
						ip, true);
					if (err)
						return err;
				}
			}
			if (is_atomic_fetch) {
				err = emit_atomic_fetch_rmw(&prog, insn, real_dst_reg,
							    real_src_reg, -1, false,
							    NULL, NULL);
				if (err)
					return err;
				break;
			}

			if (bpf_atomic_is_load_store(insn))
				err = emit_atomic_ld_st(env, &prog, insn, ip,
							dst_reg, src_reg,
							accesses_stack_only);
			else
				err = emit_atomic_rmw(&prog, insn->imm, dst_reg, src_reg,
						      insn->off, BPF_SIZE(insn->code));
			if (err)
				return err;
			break;
		}
		case BPF_STX | BPF_PROBE_ATOMIC | BPF_B:
		case BPF_STX | BPF_PROBE_ATOMIC | BPF_H:
			if (!bpf_atomic_is_load_store(insn)) {
				pr_err("bpf_jit: 1- and 2-byte RMW atomics are not supported\n");
				return -EFAULT;
			}
			fallthrough;
		case BPF_STX | BPF_PROBE_ATOMIC | BPF_W:
		case BPF_STX | BPF_PROBE_ATOMIC | BPF_DW:
			if (is_atomic_fetch_op(insn)) {
				u32 real_src_reg = src_reg, real_dst_reg = dst_reg;
				/*
				 * An access through a number has its address in
				 * BPF_REG_AX (see arena_scalar_access()), so R0 is
				 * saved on the stack instead, as div and mod do.
				 */
				bool r0_on_stack = dst_reg == BPF_REG_AX;
				u8 *fault[2], *resume;
				int j;

				/* Will need RAX as a CMPXCHG operand so save R0 */
				if (r0_on_stack) {
					EMIT1(0x50); /* push rax */
				} else {
					emit_mov_reg(&prog, true, BPF_REG_AX, BPF_REG_0);
					if (src_reg == BPF_REG_0)
						real_src_reg = BPF_REG_AX;
					if (dst_reg == BPF_REG_0)
						real_dst_reg = BPF_REG_AX;
				}

				err = emit_atomic_fetch_rmw(&prog, insn, real_dst_reg,
							    real_src_reg, X86_REG_R12,
							    r0_on_stack, fault, &resume);
				if (err)
					return err;

				/*
				 * The page may be unmapped between the load and the
				 * CMPXCHG, so both need an entry. Both report a write,
				 * since the BPF insn is a read-modify-write.
				 */
				for (j = 0; j < 2; j++) {
					err = emit_arena_exentry(bpf_prog, image, rw_image,
								 &excnt,
								 image + proglen +
									 (fault[j] - temp),
								 resume - fault[j],
								 reg2pt_regs[real_src_reg],
								 reg2pt_regs[real_dst_reg],
								 true, insn->off);
					if (err)
						return err;
				}
				break;
			}
			start_of_ldx = prog;

			if (bpf_atomic_is_load_store(insn))
				err = emit_atomic_ld_st_index(&prog, insn->imm,
							      BPF_SIZE(insn->code), dst_reg,
							      src_reg, X86_REG_R12, insn->off);
			else
				err = emit_atomic_rmw_index(&prog, insn->imm, BPF_SIZE(insn->code),
							    dst_reg, src_reg, X86_REG_R12,
							    insn->off);
			if (err)
				return err;
			goto populate_extable;

			/* call */
		case BPF_JMP | BPF_CALL: {
			const struct btf_func_model *fm = NULL;

			func = (u8 *) __bpf_call_base + imm32;
			if (src_reg == BPF_PSEUDO_CALL && tail_call_reachable) {
				u8 *tcc_load = prog;

				LOAD_TAIL_CALL_CNT_PTR(stack_depth);
				ip += prog - tcc_load;
			}
			if (!imm32)
				return -EINVAL;
			if (src_reg == BPF_PSEUDO_KFUNC_CALL) {
				fm = bpf_jit_find_kfunc_model(bpf_prog, insn);
				if (!fm)
					return -EINVAL;
				err = emit_kfunc_arena_args(bpf_prog, fm, &prog);
				if (err < 0)
					return err;
				ip += err;
				ip += emit_kfunc_arg_moves(fm, outgoing_arg_base -
							   outgoing_rsp, &prog);
			}
			if (func == (u8 *)arch_bpf_timed_may_goto) {
				u32 fp = priv_frame_ptr ? X86_REG_R9 : BPF_REG_FP;

				/*
				 * AX has the offset of count and timestamp
				 * in the stack. Turn it into a pointer.
				 * add r10, rbp or add r10, r9
				 */
				maybe_emit_mod(&prog, BPF_REG_AX, fp, true);
				EMIT2(0x01, add_2reg(0xC0, BPF_REG_AX, fp));
				ip += 3;
			}
			if (priv_frame_ptr) {
				push_r9(&prog);
				ip += 2;
			}
			ip += x86_call_depth_emit_accounting(&prog, func, ip);
			if (emit_call(&prog, func, ip))
				return -EINVAL;
			if (priv_frame_ptr)
				pop_r9(&prog);
			/*
			 * A kfunc returning more than 8 bytes hands the second
			 * half back in RDX (the native ABI's second return reg),
			 * but BPF expects it in R0:R2. BPF R0 is RAX (no move
			 * needed), while BPF R2 is RSI, so copy RDX into RSI.
			 */
			if (fm && fm->ret_size > 8)
				emit_mov_reg(&prog, true, BPF_REG_2, BPF_REG_3);
			break;
		}

			/* callx: call of a bpf subprog whose address is in dst_reg */
		case BPF_JMP | BPF_CALL | BPF_X:
			/*
			 * The verifier makes sure that callees of callx are
			 * not tail call reachable, hence unlike a direct call
			 * of a subprog there is no need to pass
			 * tail_call_cnt_ptr in rax.
			 */
			if (priv_frame_ptr) {
				push_r9(&prog);
				ip += 2;
			}
			if (emit_indirect_call(&prog, insn->dst_reg, ip))
				return -EINVAL;
			if (priv_frame_ptr)
				pop_r9(&prog);
			break;

			/* unwind_goto. The verifier expanded may_goto. */
		case BPF_JMP | BPF_JCOND:
			if (!priv_frame_ptr) {
				err = add_unwind_exentry(bpf_prog, image, rw_image, &excnt,
							 addrs[i - 1], addrs[i + insn->off]);
				if (err)
					return err;
				break;
			}
			/*
			 * 'pop r9' after the call has to be done on the way to
			 * the pad too:
			 *   call foo
			 *   pop r9      <- exit returns here
			 *   jmp 1f
			 *   pop r9      <- unwind_exit returns here
			 *   jmp pad
			 * 1:
			 */
			err = add_unwind_exentry(bpf_prog, image, rw_image, &excnt,
						 addrs[i - 1] - 2, addrs[i - 1] + 2);
			if (err)
				return err;
			EMIT2(0xEB, 7);
			pop_r9(&prog);
			EMIT1_off32(0xE9, addrs[i + insn->off] - (addrs[i - 1] + 9));
			break;

		case BPF_JMP | BPF_TAIL_CALL:
			if (imm32)
				emit_bpf_tail_call_direct(bpf_prog,
							  &bpf_prog->aux->poke_tab[imm32 - 1],
							  &prog,
							  ip,
							  callee_regs_used,
							  stack_depth,
							  ctx);
			else
				emit_bpf_tail_call_indirect(bpf_prog,
							    &prog,
							    callee_regs_used,
							    stack_depth,
							    ip,
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
			if ((u32)imm32 <= U8_MAX) {
				/*
				 * test dst_reg8, imm8: with the upper bits of the
				 * mask clear the result only depends on the low byte.
				 */
				if (is_ereg_8l(dst_reg))
					EMIT1(add_1mod(0x40, dst_reg));
				if (is_axreg(dst_reg))
					EMIT2(0xA8, imm32);
				else
					EMIT3(0xF6, add_1reg(0xC0, dst_reg), imm32);
				goto emit_cond_jmp;
			}
			/* test dst_reg, imm32 */
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_JMP);
			if (is_axreg(dst_reg))
				EMIT1_off32(0xA9, imm32);
			else
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
			cmp_imm = imm32;
			adjust_cmp_imm(&jmp_op, &cmp_imm);

			/* test dst_reg, dst_reg to save one extra byte */
			if (cmp_imm == 0) {
				maybe_emit_mod(&prog, dst_reg, dst_reg,
					       BPF_CLASS(insn->code) == BPF_JMP);
				EMIT2(0x85, add_2reg(0xC0, dst_reg, dst_reg));
				goto emit_cond_jmp;
			}

			/* cmp dst_reg, imm8/32 */
			maybe_emit_1mod(&prog, dst_reg,
					BPF_CLASS(insn->code) == BPF_JMP);

			if (is_imm8(cmp_imm))
				EMIT3(0x83, add_1reg(0xF8, dst_reg), cmp_imm);
			else if (is_axreg(dst_reg))
				EMIT1_off32(0x3D, cmp_imm);
			else
				EMIT2_off32(0x81, add_1reg(0xF8, dst_reg), cmp_imm);

emit_cond_jmp:		/* Convert BPF opcode to x86 */
			switch (jmp_op) {
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

		case BPF_JMP | BPF_JA | BPF_X:
			emit_indirect_jump(&prog, insn->dst_reg, ip);
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
			if (src_reg == BPF_UNWIND_EXIT && bpf_prog_can_unwind(bpf_prog) &&
			    emit_unwind_ret_addr(&prog, ip))
				return -EINVAL;
			if (seen_exit) {
				jmp_offset = ctx->cleanup_addr - addrs[i];
				goto emit_jmp;
			}
			seen_exit = true;
			/* Update cleanup_addr */
			ctx->cleanup_addr = proglen + (prog - temp);
			if (bpf_prog_was_classic(bpf_prog) &&
			    !ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN)) {
				if (emit_spectre_bhb_barrier(&prog, ip, bpf_prog))
					return -EINVAL;
			}
			/* Deallocate outgoing args 7+ area. */
			emit_add_rsp(&prog, outgoing_rsp);
			if (bpf_prog->aux->exception_boundary) {
				pop_callee_regs(&prog, all_callee_regs_used);
				pop_r12(&prog);
			} else {
				pop_callee_regs(&prog, callee_regs_used);
				if (arena_vm_start)
					pop_r12(&prog);
			}
			EMIT1(0xC9);         /* leave */
			bpf_prog->aux->ksym.fp_end = prog - temp;

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

/* the eightbytes taken by the on-stack arguments, alignment holes included */
static int get_nr_stack_slots(const u8 *pos_of_slot, int nr_slots)
{
	int slot;

	for (slot = nr_slots - 1; slot >= 0; slot--) {
		if (x86_arg_on_stack(pos_of_slot[slot]))
			return x86_arg_stack_off(pos_of_slot[slot]) / 8 + 1;
	}

	return 0;
}

/*
 * Convert an arena kernel address into the arena pointer form on its way
 * into the BPF ctx, rax = (u32)(src - kern_vm_start). A nullable arg
 * preserves NULL, tested on the full 64-bit kernel pointer. The 32-bit
 * subtraction both truncates and clears the upper half, so the stored
 * value satisfies the JIT invariant for arena pointer registers.
 */
static void emit_arena_arg_conv(u8 **pprog, u32 src_reg, bool nullable, u32 base_lo)
{
	u8 *prog = *pprog;

	if (nullable) {
		if (src_reg != BPF_REG_0)
			emit_mov_reg(&prog, true, BPF_REG_0, src_reg);
		/* test rax, rax; jz over the 5-byte sub */
		EMIT3(0x48, 0x85, 0xC0);
		EMIT2(X86_JE, 5);
	} else if (src_reg != BPF_REG_0) {
		emit_mov_reg(&prog, false, BPF_REG_0, src_reg);
	}
	/* sub eax, base_lo */
	EMIT1_off32(0x2D, base_lo);

	*pprog = prog;
}

static void save_args(const struct btf_func_model *m, u8 **prog,
		      const u8 *pos_of_slot, int stack_size,
		      bool for_call_origin, u32 flags, u64 arena_base)
{
	int arg_regs, first_off = 0, nr_stack_slots = 0;
	bool use_jmp = bpf_trampoline_use_jmp(flags);
	int stack_args_off = (use_jmp || (flags & BPF_TRAMP_F_INDIRECT)) ? 16 : 24;
	int bargs_off = stack_size;
	int i, j, slot, off, doff;

	/* Store function arguments to stack.
	 * For a function that accepts two pointers the sequence will be:
	 * mov QWORD PTR [rbp-0x10],rdi
	 * mov QWORD PTR [rbp-0x8],rsi
	 */
	for (i = 0, slot = 0; i < m->nr_args; i++) {
		bool arena_arg = arena_base && (m->arg_flags[i] & BTF_FMODEL_ARENA_ARG);
		bool nullable = m->arg_flags[i] & BTF_FMODEL_NULLABLE_ARG;

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
		 *
		 * An argument aligned to 16 bytes, an __int128 or a struct
		 * holding one, also starts at an even stack slot, which can
		 * leave a hole before it. bpf_jit_place_args() worked out
		 * where each eightbyte is.
		 */
		for (j = 0; j < arg_regs; j++, slot++, bargs_off -= 8) {
			u8 pos = pos_of_slot[slot];
			u32 src;

			if (!x86_arg_on_stack(pos)) {
				/*
				 * Only copy the arguments on-stack to
				 * current 'stack_size' and ignore the regs,
				 * used to prepare the arguments on-stack for
				 * origin call.
				 */
				if (for_call_origin)
					continue;

				/* copy the arguments from regs into stack */
				src = x86_arg_reg[pos];
				if (arena_arg) {
					emit_arena_arg_conv(prog, src, nullable, (u32)arena_base);
					src = BPF_REG_0;
				}
				emit_stx(prog, BPF_DW, BPF_REG_FP, src, -bargs_off);
				continue;
			}

			/* copy function arguments from origin stack frame
			 * into current stack frame.
			 *
			 * The arguments on-stack start above the saved rbp
			 * and the return addresses: two return addresses
			 * (origin call and caller) when the trampoline is
			 * entered through the fentry call, so rbp + 24, and
			 * a single one when it is entered with a jmp or
			 * called indirectly, so rbp + 16.
			 *
			 * The area for the origin call mirrors the incoming
			 * one, hole and all; only the bpf program takes the
			 * arguments packed.
			 */
			off = x86_arg_stack_off(pos);
			doff = for_call_origin ? stack_size - off : bargs_off;
			emit_ldx(prog, BPF_DW, BPF_REG_0, BPF_REG_FP,
				 stack_args_off + off);
			if (arena_arg)
				emit_arena_arg_conv(prog, BPF_REG_0, nullable,
						    (u32)arena_base);
			emit_stx(prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -doff);

			if (!nr_stack_slots)
				first_off = doff;
			nr_stack_slots++;
		}
	}

	clean_stack_garbage(m, prog, nr_stack_slots, first_off);
}

static void restore_regs(u8 **prog, const u8 *pos_of_slot, int nr_slots,
			 int stack_size)
{
	int slot;

	/* Restore function arguments from stack.
	 * For a function that accepts two pointers the sequence will be:
	 * EMIT4(0x48, 0x8B, 0x7D, 0xF0); mov rdi,QWORD PTR [rbp-0x10]
	 * EMIT4(0x48, 0x8B, 0x75, 0xF8); mov rsi,QWORD PTR [rbp-0x8]
	 *
	 * The logic here is similar to what we do in save_args()
	 */
	for (slot = 0; slot < nr_slots; slot++, stack_size -= 8) {
		if (!x86_arg_on_stack(pos_of_slot[slot]))
			emit_ldx(prog, BPF_DW, x86_arg_reg[pos_of_slot[slot]],
				 BPF_REG_FP, -stack_size);
	}
}

static int invoke_bpf_prog(const struct btf_func_model *m, u8 **pprog,
			   struct bpf_tramp_image *im,
			   struct bpf_tramp_node *node, int stack_size,
			   int run_ctx_off, bool save_ret,
			   void *image, void *rw_image)
{
	u8 *prog = *pprog;
	u8 *jmp_insn, *skip;
	int ctx_cookie_off = offsetof(struct bpf_tramp_run_ctx, bpf_cookie);
	struct bpf_prog *p = node->link->prog;
	u64 cookie = node->cookie;

	/* nop, patched to skip this prog when it is detached */
	skip = image + (prog - (u8 *)rw_image);
	emit_nops(&prog, X86_PATCH_SIZE);

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

	bpf_tramp_image_add_skip(im, p, skip, image + (prog - (u8 *)rw_image));

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
		      struct bpf_tramp_image *im,
		      struct bpf_tramp_nodes *tl, int stack_size,
		      int run_ctx_off, int func_meta_off, bool save_ret,
		      void *image, void *rw_image, u64 func_meta,
		      int cookie_off)
{
	int i, cur_cookie = (cookie_off - stack_size) / 8;
	u8 *prog = *pprog;

	for (i = 0; i < tl->nr_nodes; i++) {
		if (tl->nodes[i]->link->prog->call_session_cookie) {
			emit_store_stack_imm64(&prog, BPF_REG_0, -func_meta_off,
				func_meta | (cur_cookie << BPF_TRAMP_COOKIE_INDEX_SHIFT));
			cur_cookie--;
		}
		if (invoke_bpf_prog(m, &prog, im, tl->nodes[i], stack_size,
				    run_ctx_off, save_ret, image, rw_image))
			return -EINVAL;
	}
	*pprog = prog;
	return 0;
}

static int invoke_bpf_mod_ret(const struct btf_func_model *m, u8 **pprog,
			      struct bpf_tramp_image *im,
			      struct bpf_tramp_nodes *tl, int stack_size,
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
	for (i = 0; i < tl->nr_nodes; i++) {
		if (invoke_bpf_prog(m, &prog, im, tl->nodes[i], stack_size, run_ctx_off, true,
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

/* mov rax, qword ptr [rbp - off] */
#define LOAD_TRAMP_TAIL_CALL_CNT_PTR(off)	\
	__LOAD_TCC_PTR(-(off))

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
					 struct bpf_tramp_nodes *tnodes,
					 void *func_addr)
{
	int i, ret, nr_regs, stack_size = 0;
	u8 pos_of_slot[MAX_BPF_FUNC_ARG_SLOTS];
	int regs_off, func_meta_off, ip_off, run_ctx_off, arg_stack_off, rbx_off;
	int tcc_off = 0;
	struct bpf_tramp_nodes *fentry = &tnodes[BPF_TRAMP_FENTRY];
	struct bpf_tramp_nodes *fexit = &tnodes[BPF_TRAMP_FEXIT];
	struct bpf_tramp_nodes *fmod_ret = &tnodes[BPF_TRAMP_MODIFY_RETURN];
	void *orig_call = func_addr;
	int cookie_off, cookie_cnt;
	u8 **branches = NULL;
	u64 arena_base;
	u64 func_meta;
	u8 *prog;
	bool save_ret;

	/*
	 * F_INDIRECT is only compatible with F_RET_FENTRY_RET, it is
	 * explicitly incompatible with F_CALL_ORIG | F_SKIP_FRAME | F_IP_ARG
	 * because @func_addr.
	 */
	WARN_ON_ONCE((flags & BPF_TRAMP_F_INDIRECT) &&
		     (flags & ~(BPF_TRAMP_F_INDIRECT | BPF_TRAMP_F_RET_FENTRY_RET)));

	arena_base = bpf_tramp_arena_base(m, tnodes, flags);

	/*
	 * x86-64 supports up to MAX_BPF_FUNC_ARGS argument slots, six in
	 * registers and the rest on the stack, where bpf_jit_place_args()
	 * puts them.
	 */
	nr_regs = bpf_jit_place_args(&x86_arg_abi, m, pos_of_slot);
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
	 *                 [ arg slotN       ]  always, packed, whether
	 *                 [ ...             ]  in a register or on the
	 * RBP - regs_off  [ arg slot1       ]  stack; program's ctx pointer
	 *
	 * RBP - func_meta_off [ slot count, etc ]  always
	 *
	 * RBP - ip_off    [ traced function ]  BPF_TRAMP_F_IP_ARG flag
	 *
	 * RBP - rbx_off   [ rbx value       ]  always
	 *
	 * RBP - run_ctx_off [ bpf_tramp_run_ctx ]
	 *
	 * RBP - tcc_off   [ tail_call_cnt_ptr ]  BPF_TRAMP_F_TAIL_CALL_CTX
	 *
	 *                     [ stack slotN ]  BPF_TRAMP_F_CALL_ORIG,
	 *                     [ ...         ]  the incoming stack
	 *                     [ stack slot2 ]  arguments, alignment
	 * RBP - arg_stack_off [ stack slot1 ]  holes included
	 */

	/* room for return value of orig_call or fentry prog */
	save_ret = flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET);
	if (save_ret)
		stack_size += 8;

	stack_size += nr_regs * 8;
	regs_off = stack_size;

	/* function matedata, such as the argument slot count */
	stack_size += 8;
	func_meta_off = stack_size;

	if (flags & BPF_TRAMP_F_IP_ARG)
		stack_size += 8; /* room for IP address argument */

	ip_off = stack_size;

	cookie_cnt = bpf_fsession_cookie_cnt(tnodes);
	/* room for session cookies */
	stack_size += cookie_cnt * 8;
	cookie_off = stack_size;

	stack_size += 8;
	rbx_off = stack_size;

	stack_size += (sizeof(struct bpf_tramp_run_ctx) + 7) & ~0x7;
	run_ctx_off = stack_size;

	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX) {
		stack_size += 8;
		tcc_off = stack_size;
	}

	if (nr_regs > 6 && (flags & BPF_TRAMP_F_CALL_ORIG)) {
		/* the space that used to pass arguments on-stack */
		stack_size += get_nr_stack_slots(pos_of_slot, nr_regs) * 8;
		/* make sure the stack pointer is 16-byte aligned if we
		 * need pass arguments on stack, which means
		 *  [stack_size + 8(rbp) + 8(rip) + 8(origin rip)]
		 * should be 16-byte aligned. Following code depend on
		 * that stack_size is already 8-byte aligned.
		 */
		if (bpf_trampoline_use_jmp(flags)) {
			/* no rip in the "jmp" case */
			stack_size += (stack_size % 16) ? 8 : 0;
		} else {
			stack_size += (stack_size % 16) ? 0 : 8;
		}
	}

	arg_stack_off = stack_size;

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
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
	if (im)
		im->ksym.fp_start = prog - (u8 *)rw_image;

	/* sub rsp, stack_size */
	emit_sub_rsp(&prog, stack_size);
	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
		/* mov QWORD PTR [rbp - tcc_off], rax */
		emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_0, -tcc_off);
	/* mov QWORD PTR [rbp - rbx_off], rbx */
	emit_stx(&prog, BPF_DW, BPF_REG_FP, BPF_REG_6, -rbx_off);

	func_meta = nr_regs;
	/* Store number of argument slots of the traced function */
	emit_store_stack_imm64(&prog, BPF_REG_0, -func_meta_off, func_meta);

	if (flags & BPF_TRAMP_F_IP_ARG) {
		/* Store IP address of the traced function */
		emit_store_stack_imm64(&prog, BPF_REG_0, -ip_off, (long)func_addr);
	}

	save_args(m, &prog, pos_of_slot, regs_off, false, flags, arena_base);

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		/* arg1: mov rdi, im */
		emit_mov_imm64(&prog, BPF_REG_1, (long) im >> 32, (u32) (long) im);
		if (emit_rsb_call(&prog, __bpf_tramp_enter,
				  image + (prog - (u8 *)rw_image))) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (bpf_fsession_cnt(tnodes)) {
		/* clear all the session cookies' value */
		for (int i = 0; i < cookie_cnt; i++)
			emit_store_stack_imm64(&prog, BPF_REG_0, -cookie_off + 8 * i, 0);
		/* clear the return value to make sure fentry always get 0 */
		emit_store_stack_imm64(&prog, BPF_REG_0, -8, 0);
	}

	if (fentry->nr_nodes) {
		if (invoke_bpf(m, &prog, im, fentry, regs_off, run_ctx_off, func_meta_off,
			       flags & BPF_TRAMP_F_RET_FENTRY_RET, image, rw_image,
			       func_meta, cookie_off))
			return -EINVAL;
	}

	if (fmod_ret->nr_nodes) {
		branches = kcalloc(fmod_ret->nr_nodes, sizeof(u8 *),
				   GFP_KERNEL);
		if (!branches)
			return -ENOMEM;

		if (invoke_bpf_mod_ret(m, &prog, im, fmod_ret, regs_off,
				       run_ctx_off, branches, image, rw_image)) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		restore_regs(&prog, pos_of_slot, nr_regs, regs_off);
		save_args(m, &prog, pos_of_slot, arg_stack_off, true, flags, 0);

		if (flags & BPF_TRAMP_F_TAIL_CALL_CTX) {
			/* Before calling the original function, load the
			 * tail_call_cnt_ptr from stack to rax.
			 */
			LOAD_TRAMP_TAIL_CALL_CNT_PTR(tcc_off);
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
	}

	if (fmod_ret->nr_nodes) {
		/* From Intel 64 and IA-32 Architectures Optimization
		 * Reference Manual, 3.4.1.4 Code Alignment, Assembly/Compiler
		 * Coding Rule 11: All branch targets should be 16-byte
		 * aligned.
		 */
		emit_align(&prog, 16);
		/* Update the branches saved in invoke_bpf_mod_ret with the
		 * aligned address of do_fexit.
		 */
		for (i = 0; i < fmod_ret->nr_nodes; i++) {
			emit_cond_near_jump(&branches[i], image + (prog - (u8 *)rw_image),
					    image + (branches[i] - (u8 *)rw_image), X86_JNE);
		}
	}

	/* set the "is_return" flag for fsession */
	func_meta |= (1ULL << BPF_TRAMP_IS_RETURN_SHIFT);
	if (bpf_fsession_cnt(tnodes))
		emit_store_stack_imm64(&prog, BPF_REG_0, -func_meta_off, func_meta);

	if (fexit->nr_nodes) {
		if (invoke_bpf(m, &prog, im, fexit, regs_off, run_ctx_off, func_meta_off,
			       false, image, rw_image, func_meta, cookie_off)) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (flags & BPF_TRAMP_F_RESTORE_REGS)
		restore_regs(&prog, pos_of_slot, nr_regs, regs_off);

	/* This needs to be done regardless. If there were fmod_ret programs,
	 * the return value is only updated on the stack and still needs to be
	 * restored to R0.
	 */
	if (flags & BPF_TRAMP_F_CALL_ORIG) {
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
		LOAD_TRAMP_TAIL_CALL_CNT_PTR(tcc_off);
	}

	/* restore return value of orig_call or fentry prog back into RAX */
	if (save_ret)
		emit_ldx(&prog, BPF_DW, BPF_REG_0, BPF_REG_FP, -8);

	emit_ldx(&prog, BPF_DW, BPF_REG_6, BPF_REG_FP, -rbx_off);

	EMIT1(0xC9); /* leave */
	if (im)
		im->ksym.fp_end = prog - (u8 *)rw_image;

	if (flags & BPF_TRAMP_F_SKIP_FRAME) {
		/* skip our return address and return to parent */
		emit_add_rsp(&prog, 8); /* add rsp, 8 */
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
	return bpf_prog_pack_alloc(size, jit_fill_hole, false);
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
				struct bpf_tramp_nodes *tnodes,
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
					    flags, tnodes, func_addr);
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
			     struct bpf_tramp_nodes *tnodes, void *func_addr)
{
	struct bpf_tramp_image im = {};

	if (!trampoline_size_image)
		return -ENOMEM;

	return __arch_prepare_bpf_trampoline(&im, trampoline_size_image,
					     trampoline_size_image + PAGE_SIZE,
					     trampoline_size_image, m, flags,
					     tnodes, func_addr);
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

		emit_indirect_jump(&prog, BPF_REG_3 /* R3 -> rdx */, image + (prog - buf));

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
			       bpf_jit_get_prog_name(prog));
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

struct bpf_prog *bpf_int_jit_compile(struct bpf_verifier_env *env, struct bpf_prog *prog)
{
	struct bpf_binary_header *rw_header = NULL;
	struct bpf_binary_header *header = NULL;
	void __percpu *priv_stack_ptr = NULL;
	struct x64_jit_data *jit_data;
	int priv_stack_alloc_sz;
	int proglen, oldproglen = 0;
	struct jit_context ctx = {};
	bool extra_pass = false;
	bool padding = false;
	u8 *rw_image = NULL;
	u8 *image = NULL;
	int *addrs;
	int pass;
	int i;

	if (!prog->jit_requested)
		return prog;

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc_obj(*jit_data);
		if (!jit_data)
			return prog;
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
		if (!priv_stack_ptr)
			goto out_priv_stack;

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

	/*
	 * An arena fetching AND/OR/XOR is lowered to a CMPXCHG loop whose load
	 * and CMPXCHG can both fault, one more entry than the verifier reserved.
	 * The extra pass of a program with subprograms returned above, so the
	 * count is adjusted once.
	 */
	for (i = 0; prog->aux->arena && i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];

		if (BPF_CLASS(insn->code) == BPF_STX &&
		    BPF_MODE(insn->code) == BPF_PROBE_ATOMIC &&
		    is_atomic_fetch_op(insn))
			prog->aux->num_exentries++;
	}

	addrs = kvmalloc_objs(*addrs, prog->len + 1);
	if (!addrs)
		goto out_addrs;

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
		proglen = do_jit(env, prog, addrs, image, rw_image, oldproglen,
				 &ctx, padding);
		if (proglen <= 0) {
out_image:
			image = NULL;
			if (header) {
				bpf_arch_text_copy(&header->size, &rw_header->size,
						   sizeof(rw_header->size));
				bpf_jit_binary_pack_free(header, rw_header);
			}
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
			 * to LDX|PROBE_MEM|size to make JITing easier. Arena
			 * accesses add one each, and an arena fetching AND/OR/XOR
			 * one more, see the adjustment of num_exentries above.
			 */
			u32 align = __alignof__(struct exception_table_entry);
			u32 extable_size = prog->aux->num_exentries *
				sizeof(struct exception_table_entry);

			/* allocate module memory for x86 insns and extable */
			header = bpf_jit_binary_pack_alloc(roundup(proglen, align) + extable_size,
							   &image, align, &rw_header, &rw_image,
							   jit_fill_hole,
							   bpf_prog_was_classic(prog));
			if (!header)
				goto out_addrs;
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
		 * The bpf_prog_update_insn_ptrs function expects addrs to
		 * point to the first byte of the jitted instruction (unlike
		 * the bpf_prog_fill_jited_linfo below, which, for historical
		 * reasons, expects to point to the next instruction)
		 */
		bpf_prog_update_insn_ptrs(prog, addrs, image);

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

	return prog;
}

bool bpf_jit_supports_callx(void)
{
	/*
	 * FineIBT poisons ENDBR at the entry of a JITed function and expects
	 * indirect callers to go through the CFI preamble instead.
	 * callx doesn't do that yet.
	 */
	return cfi_mode != CFI_FINEIBT;
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}

bool bpf_jit_supports_kfunc_ret_reg_pair(void)
{
	return true;
}

bool bpf_jit_supports_unwind(void)
{
	return true;
}

const struct bpf_jit_arg_abi *bpf_jit_arg_abi(void)
{
	return &x86_arg_abi;
}

bool bpf_jit_supports_stack_args(void)
{
	return true;
}

bool bpf_jit_supports_arena_args(void)
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

/*
 * Frame sizes are 32-bit immediates in the prologue, epilogue and tail call
 * sequences, a tail call pops the caller's frame and lands in the target's
 * prologue before the target allocates its own, and private stacks are
 * allocated from the program's own depth, so MAX_BPF_STACK_JIT frames need
 * nothing special.
 */
bool bpf_jit_supports_large_stack(void)
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
}

void bpf_arch_poke_desc_update(struct bpf_jit_poke_descriptor *poke,
			       struct bpf_prog *new, struct bpf_prog *old)
{
	u8 *old_addr, *new_addr, *old_bypass_addr;
	enum bpf_text_poke_type t;
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
		t = old_addr ? BPF_MOD_JUMP : BPF_MOD_NOP;
		ret = __bpf_arch_text_poke(poke->tailcall_target,
					   t, BPF_MOD_JUMP,
					   old_addr, new_addr);
		BUG_ON(ret < 0);
		if (!old) {
			ret = __bpf_arch_text_poke(poke->tailcall_bypass,
						   BPF_MOD_JUMP, BPF_MOD_NOP,
						   poke->bypass_addr,
						   NULL);
			BUG_ON(ret < 0);
		}
	} else {
		t = old_bypass_addr ? BPF_MOD_JUMP : BPF_MOD_NOP;
		ret = __bpf_arch_text_poke(poke->tailcall_bypass,
					   t, BPF_MOD_JUMP, old_bypass_addr,
					   poke->bypass_addr);
		BUG_ON(ret < 0);
		/* let other CPUs finish the execution of program
		 * so that it will not possible to expose them
		 * to invalid nop, stack unwind, nop state
		 */
		if (!ret)
			synchronize_rcu();
		t = old_addr ? BPF_MOD_JUMP : BPF_MOD_NOP;
		ret = __bpf_arch_text_poke(poke->tailcall_target,
					   t, BPF_MOD_NOP, old_addr, NULL);
		BUG_ON(ret < 0);
	}
}

bool bpf_jit_supports_arena(void)
{
	return true;
}

bool bpf_jit_supports_arena_scalar(void)
{
	return true;
}

bool bpf_jit_supports_insn(struct bpf_insn *insn, bool in_arena)
{
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

bool bpf_jit_supports_timed_may_goto(void)
{
	return true;
}

bool bpf_jit_supports_fsession(void)
{
	return true;
}
