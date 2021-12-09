/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Just-In-Time compiler for eBPF bytecode on 32-bit and 64-bit MIPS.
 *
 * Copyright (c) 2021 Anyfi Networks AB.
 * Author: Johan Almbladh <johan.almbladh@gmail.com>
 *
 * Based on code and ideas from
 * Copyright (c) 2017 Cavium, Inc.
 * Copyright (c) 2017 Shubham Bansal <illusionist.neo@gmail.com>
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 */

#ifndef _BPF_JIT_COMP_H
#define _BPF_JIT_COMP_H

/* MIPS registers */
#define MIPS_R_ZERO	0   /* Const zero */
#define MIPS_R_AT	1   /* Asm temp   */
#define MIPS_R_V0	2   /* Result     */
#define MIPS_R_V1	3   /* Result     */
#define MIPS_R_A0	4   /* Argument   */
#define MIPS_R_A1	5   /* Argument   */
#define MIPS_R_A2	6   /* Argument   */
#define MIPS_R_A3	7   /* Argument   */
#define MIPS_R_A4	8   /* Arg (n64)  */
#define MIPS_R_A5	9   /* Arg (n64)  */
#define MIPS_R_A6	10  /* Arg (n64)  */
#define MIPS_R_A7	11  /* Arg (n64)  */
#define MIPS_R_T0	8   /* Temp (o32) */
#define MIPS_R_T1	9   /* Temp (o32) */
#define MIPS_R_T2	10  /* Temp (o32) */
#define MIPS_R_T3	11  /* Temp (o32) */
#define MIPS_R_T4	12  /* Temporary  */
#define MIPS_R_T5	13  /* Temporary  */
#define MIPS_R_T6	14  /* Temporary  */
#define MIPS_R_T7	15  /* Temporary  */
#define MIPS_R_S0	16  /* Saved      */
#define MIPS_R_S1	17  /* Saved      */
#define MIPS_R_S2	18  /* Saved      */
#define MIPS_R_S3	19  /* Saved      */
#define MIPS_R_S4	20  /* Saved      */
#define MIPS_R_S5	21  /* Saved      */
#define MIPS_R_S6	22  /* Saved      */
#define MIPS_R_S7	23  /* Saved      */
#define MIPS_R_T8	24  /* Temporary  */
#define MIPS_R_T9	25  /* Temporary  */
/*      MIPS_R_K0	26     Reserved   */
/*      MIPS_R_K1	27     Reserved   */
#define MIPS_R_GP	28  /* Global ptr */
#define MIPS_R_SP	29  /* Stack ptr  */
#define MIPS_R_FP	30  /* Frame ptr  */
#define MIPS_R_RA	31  /* Return     */

/*
 * Jump address mask for immediate jumps. The four most significant bits
 * must be equal to PC.
 */
#define MIPS_JMP_MASK	0x0fffffffUL

/* Maximum number of iterations in offset table computation */
#define JIT_MAX_ITERATIONS	8

/*
 * Jump pseudo-instructions used internally
 * for branch conversion and branch optimization.
 */
#define JIT_JNSET	0xe0
#define JIT_JNOP	0xf0

/* Descriptor flag for PC-relative branch conversion */
#define JIT_DESC_CONVERT	BIT(31)

/* JIT context for an eBPF program */
struct jit_context {
	struct bpf_prog *program;     /* The eBPF program being JITed        */
	u32 *descriptors;             /* eBPF to JITed CPU insn descriptors  */
	u32 *target;                  /* JITed code buffer                   */
	u32 bpf_index;                /* Index of current BPF program insn   */
	u32 jit_index;                /* Index of current JIT target insn    */
	u32 changes;                  /* Number of PC-relative branch conv   */
	u32 accessed;                 /* Bit mask of read eBPF registers     */
	u32 clobbered;                /* Bit mask of modified CPU registers  */
	u32 stack_size;               /* Total allocated stack size in bytes */
	u32 saved_size;               /* Size of callee-saved registers      */
	u32 stack_used;               /* Stack size used for function calls  */
};

/* Emit the instruction if the JIT memory space has been allocated */
#define __emit(ctx, func, ...)					\
do {								\
	if ((ctx)->target != NULL) {				\
		u32 *p = &(ctx)->target[ctx->jit_index];	\
		uasm_i_##func(&p, ##__VA_ARGS__);		\
	}							\
	(ctx)->jit_index++;					\
} while (0)
#define emit(...) __emit(__VA_ARGS__)

/* Workaround for R10000 ll/sc errata */
#ifdef CONFIG_WAR_R10000
#define LLSC_beqz	beqzl
#else
#define LLSC_beqz	beqz
#endif

/* Workaround for Loongson-3 ll/sc errata */
#ifdef CONFIG_CPU_LOONGSON3_WORKAROUNDS
#define LLSC_sync(ctx)	emit(ctx, sync, 0)
#define LLSC_offset	4
#else
#define LLSC_sync(ctx)
#define LLSC_offset	0
#endif

/* Workaround for Loongson-2F jump errata */
#ifdef CONFIG_CPU_JUMP_WORKAROUNDS
#define JALR_MASK	0xffffffffcfffffffULL
#else
#define JALR_MASK	(~0ULL)
#endif

/*
 * Mark a BPF register as accessed, it needs to be
 * initialized by the program if expected, e.g. FP.
 */
static inline void access_reg(struct jit_context *ctx, u8 reg)
{
	ctx->accessed |= BIT(reg);
}

/*
 * Mark a CPU register as clobbered, it needs to be
 * saved/restored by the program if callee-saved.
 */
static inline void clobber_reg(struct jit_context *ctx, u8 reg)
{
	ctx->clobbered |= BIT(reg);
}

/*
 * Push registers on the stack, starting at a given depth from the stack
 * pointer and increasing. The next depth to be written is returned.
 */
int push_regs(struct jit_context *ctx, u32 mask, u32 excl, int depth);

/*
 * Pop registers from the stack, starting at a given depth from the stack
 * pointer and increasing. The next depth to be read is returned.
 */
int pop_regs(struct jit_context *ctx, u32 mask, u32 excl, int depth);

/* Compute the 28-bit jump target address from a BPF program location */
int get_target(struct jit_context *ctx, u32 loc);

/* Compute the PC-relative offset to relative BPF program offset */
int get_offset(const struct jit_context *ctx, int off);

/* dst = imm (32-bit) */
void emit_mov_i(struct jit_context *ctx, u8 dst, s32 imm);

/* dst = src (32-bit) */
void emit_mov_r(struct jit_context *ctx, u8 dst, u8 src);

/* Validate ALU/ALU64 immediate range */
bool valid_alu_i(u8 op, s32 imm);

/* Rewrite ALU/ALU64 immediate operation */
bool rewrite_alu_i(u8 op, s32 imm, u8 *alu, s32 *val);

/* ALU immediate operation (32-bit) */
void emit_alu_i(struct jit_context *ctx, u8 dst, s32 imm, u8 op);

/* ALU register operation (32-bit) */
void emit_alu_r(struct jit_context *ctx, u8 dst, u8 src, u8 op);

/* Atomic read-modify-write (32-bit) */
void emit_atomic_r(struct jit_context *ctx, u8 dst, u8 src, s16 off, u8 code);

/* Atomic compare-and-exchange (32-bit) */
void emit_cmpxchg_r(struct jit_context *ctx, u8 dst, u8 src, u8 res, s16 off);

/* Swap bytes and truncate a register word or half word */
void emit_bswap_r(struct jit_context *ctx, u8 dst, u32 width);

/* Validate JMP/JMP32 immediate range */
bool valid_jmp_i(u8 op, s32 imm);

/* Prepare a PC-relative jump operation with immediate conditional */
void setup_jmp_i(struct jit_context *ctx, s32 imm, u8 width,
		 u8 bpf_op, s16 bpf_off, u8 *jit_op, s32 *jit_off);

/* Prepare a PC-relative jump operation with register conditional */
void setup_jmp_r(struct jit_context *ctx, bool same_reg,
		 u8 bpf_op, s16 bpf_off, u8 *jit_op, s32 *jit_off);

/* Finish a PC-relative jump operation */
int finish_jmp(struct jit_context *ctx, u8 jit_op, s16 bpf_off);

/* Conditional JMP/JMP32 immediate */
void emit_jmp_i(struct jit_context *ctx, u8 dst, s32 imm, s32 off, u8 op);

/* Conditional JMP/JMP32 register */
void emit_jmp_r(struct jit_context *ctx, u8 dst, u8 src, s32 off, u8 op);

/* Jump always */
int emit_ja(struct jit_context *ctx, s16 off);

/* Jump to epilogue */
int emit_exit(struct jit_context *ctx);

/*
 * Build program prologue to set up the stack and registers.
 * This function is implemented separately for 32-bit and 64-bit JITs.
 */
void build_prologue(struct jit_context *ctx);

/*
 * Build the program epilogue to restore the stack and registers.
 * This function is implemented separately for 32-bit and 64-bit JITs.
 */
void build_epilogue(struct jit_context *ctx, int dest_reg);

/*
 * Convert an eBPF instruction to native instruction, i.e
 * JITs an eBPF instruction.
 * Returns :
 *	0  - Successfully JITed an 8-byte eBPF instruction
 *	>0 - Successfully JITed a 16-byte eBPF instruction
 *	<0 - Failed to JIT.
 * This function is implemented separately for 32-bit and 64-bit JITs.
 */
int build_insn(const struct bpf_insn *insn, struct jit_context *ctx);

#endif /* _BPF_JIT_COMP_H */
