/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The interface that a back-end should provide to bpf_jit_core.c.
 *
 * Copyright (c) 2024 Synopsys Inc.
 * Author: Shahab Vahedi <shahab@synopsys.com>
 */

#ifndef _ARC_BPF_JIT_H
#define _ARC_BPF_JIT_H

#include <linux/bpf.h>
#include <linux/filter.h>

/* Print debug info and assert. */
//#define ARC_BPF_JIT_DEBUG

/* Determine the address type of the target. */
#ifdef CONFIG_ISA_ARCV2
#define ARC_ADDR u32
#endif

/*
 * For the translation of some BPF instructions, a temporary register
 * might be needed for some interim data.
 */
#define JIT_REG_TMP MAX_BPF_JIT_REG

/*
 * Buffer access: If buffer "b" is not NULL, advance by "n" bytes.
 *
 * This macro must be used in any place that potentially requires a
 * "buf + len". This way, we make sure that the "buf" argument for
 * the underlying "arc_*(buf, ...)" ends up as NULL instead of something
 * like "0+4" or "0+8", etc. Those "arc_*()" functions check their "buf"
 * value to decide if instructions should be emitted or not.
 */
#define BUF(b, n) (((b) != NULL) ? ((b) + (n)) : (b))

/************** Functions that the back-end must provide **************/
/* Extension for 32-bit operations. */
inline u8 zext(u8 *buf, u8 rd);
/***** Moves *****/
u8 mov_r32(u8 *buf, u8 rd, u8 rs, u8 sign_ext);
u8 mov_r32_i32(u8 *buf, u8 reg, s32 imm);
u8 mov_r64(u8 *buf, u8 rd, u8 rs, u8 sign_ext);
u8 mov_r64_i32(u8 *buf, u8 reg, s32 imm);
u8 mov_r64_i64(u8 *buf, u8 reg, u32 lo, u32 hi);
/***** Loads and stores *****/
u8 load_r(u8 *buf, u8 rd, u8 rs, s16 off, u8 size, bool sign_ext);
u8 store_r(u8 *buf, u8 rd, u8 rs, s16 off, u8 size);
u8 store_i(u8 *buf, s32 imm, u8 rd, s16 off, u8 size);
/***** Addition *****/
u8 add_r32(u8 *buf, u8 rd, u8 rs);
u8 add_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 add_r64(u8 *buf, u8 rd, u8 rs);
u8 add_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Subtraction *****/
u8 sub_r32(u8 *buf, u8 rd, u8 rs);
u8 sub_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 sub_r64(u8 *buf, u8 rd, u8 rs);
u8 sub_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Multiplication *****/
u8 mul_r32(u8 *buf, u8 rd, u8 rs);
u8 mul_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 mul_r64(u8 *buf, u8 rd, u8 rs);
u8 mul_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Division *****/
u8 div_r32(u8 *buf, u8 rd, u8 rs, bool sign_ext);
u8 div_r32_i32(u8 *buf, u8 rd, s32 imm, bool sign_ext);
/***** Remainder *****/
u8 mod_r32(u8 *buf, u8 rd, u8 rs, bool sign_ext);
u8 mod_r32_i32(u8 *buf, u8 rd, s32 imm, bool sign_ext);
/***** Bitwise AND *****/
u8 and_r32(u8 *buf, u8 rd, u8 rs);
u8 and_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 and_r64(u8 *buf, u8 rd, u8 rs);
u8 and_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Bitwise OR *****/
u8 or_r32(u8 *buf, u8 rd, u8 rs);
u8 or_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 or_r64(u8 *buf, u8 rd, u8 rs);
u8 or_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Bitwise XOR *****/
u8 xor_r32(u8 *buf, u8 rd, u8 rs);
u8 xor_r32_i32(u8 *buf, u8 rd, s32 imm);
u8 xor_r64(u8 *buf, u8 rd, u8 rs);
u8 xor_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Bitwise Negate *****/
u8 neg_r32(u8 *buf, u8 r);
u8 neg_r64(u8 *buf, u8 r);
/***** Bitwise left shift *****/
u8 lsh_r32(u8 *buf, u8 rd, u8 rs);
u8 lsh_r32_i32(u8 *buf, u8 rd, u8 imm);
u8 lsh_r64(u8 *buf, u8 rd, u8 rs);
u8 lsh_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Bitwise right shift (logical) *****/
u8 rsh_r32(u8 *buf, u8 rd, u8 rs);
u8 rsh_r32_i32(u8 *buf, u8 rd, u8 imm);
u8 rsh_r64(u8 *buf, u8 rd, u8 rs);
u8 rsh_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Bitwise right shift (arithmetic) *****/
u8 arsh_r32(u8 *buf, u8 rd, u8 rs);
u8 arsh_r32_i32(u8 *buf, u8 rd, u8 imm);
u8 arsh_r64(u8 *buf, u8 rd, u8 rs);
u8 arsh_r64_i32(u8 *buf, u8 rd, s32 imm);
/***** Frame related *****/
u32 mask_for_used_regs(u8 bpf_reg, bool is_call);
u8 arc_prologue(u8 *buf, u32 usage, u16 frame_size);
u8 arc_epilogue(u8 *buf, u32 usage, u16 frame_size);
/***** Jumps *****/
/*
 * Different sorts of conditions (ARC enum as opposed to BPF_*).
 *
 * Do not change the order of enums here. ARC_CC_SLE+1 is used
 * to determine the number of JCCs.
 */
enum ARC_CC {
	ARC_CC_UGT = 0,		/* unsigned >  */
	ARC_CC_UGE,		/* unsigned >= */
	ARC_CC_ULT,		/* unsigned <  */
	ARC_CC_ULE,		/* unsigned <= */
	ARC_CC_SGT,		/*   signed >  */
	ARC_CC_SGE,		/*   signed >= */
	ARC_CC_SLT,		/*   signed <  */
	ARC_CC_SLE,		/*   signed <= */
	ARC_CC_AL,		/* always      */
	ARC_CC_EQ,		/*          == */
	ARC_CC_NE,		/*          != */
	ARC_CC_SET,		/* test        */
	ARC_CC_LAST
};

/*
 * A few notes:
 *
 * - check_jmp_*() are prerequisites before calling the gen_jmp_*().
 *   They return "true" if the jump is possible and "false" otherwise.
 *
 * - The notion of "*_off" is to emphasize that these parameters are
 *   merely offsets in the JIT stream and not absolute addresses. One
 *   can look at them as addresses if the JIT code would start from
 *   address 0x0000_0000. Nonetheless, since the buffer address for the
 *   JIT is on a word-aligned address, this works and actually makes
 *   things simpler (offsets are in the range of u32 which is more than
 *   enough).
 */
bool check_jmp_32(u32 curr_off, u32 targ_off, u8 cond);
bool check_jmp_64(u32 curr_off, u32 targ_off, u8 cond);
u8 gen_jmp_32(u8 *buf, u8 rd, u8 rs, u8 cond, u32 c_off, u32 t_off);
u8 gen_jmp_64(u8 *buf, u8 rd, u8 rs, u8 cond, u32 c_off, u32 t_off);
/***** Miscellaneous *****/
u8 gen_func_call(u8 *buf, ARC_ADDR func_addr, bool external_func);
u8 arc_to_bpf_return(u8 *buf);
/*
 * - Perform byte swaps on "rd" based on the "size".
 * - If "force" is set, do it unconditionally. Otherwise, consider the
 *   desired "endian"ness and the host endianness.
 * - For data "size"s up to 32 bits, perform a zero-extension if asked
 *   by the "do_zext" boolean.
 */
u8 gen_swap(u8 *buf, u8 rd, u8 size, u8 endian, bool force, bool do_zext);

#endif /* _ARC_BPF_JIT_H */
