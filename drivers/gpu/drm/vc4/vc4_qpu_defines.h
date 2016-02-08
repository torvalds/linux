/*
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef VC4_QPU_DEFINES_H
#define VC4_QPU_DEFINES_H

enum qpu_op_add {
	QPU_A_NOP,
	QPU_A_FADD,
	QPU_A_FSUB,
	QPU_A_FMIN,
	QPU_A_FMAX,
	QPU_A_FMINABS,
	QPU_A_FMAXABS,
	QPU_A_FTOI,
	QPU_A_ITOF,
	QPU_A_ADD = 12,
	QPU_A_SUB,
	QPU_A_SHR,
	QPU_A_ASR,
	QPU_A_ROR,
	QPU_A_SHL,
	QPU_A_MIN,
	QPU_A_MAX,
	QPU_A_AND,
	QPU_A_OR,
	QPU_A_XOR,
	QPU_A_NOT,
	QPU_A_CLZ,
	QPU_A_V8ADDS = 30,
	QPU_A_V8SUBS = 31,
};

enum qpu_op_mul {
	QPU_M_NOP,
	QPU_M_FMUL,
	QPU_M_MUL24,
	QPU_M_V8MULD,
	QPU_M_V8MIN,
	QPU_M_V8MAX,
	QPU_M_V8ADDS,
	QPU_M_V8SUBS,
};

enum qpu_raddr {
	QPU_R_FRAG_PAYLOAD_ZW = 15, /* W for A file, Z for B file */
	/* 0-31 are the plain regfile a or b fields */
	QPU_R_UNIF = 32,
	QPU_R_VARY = 35,
	QPU_R_ELEM_QPU = 38,
	QPU_R_NOP,
	QPU_R_XY_PIXEL_COORD = 41,
	QPU_R_MS_REV_FLAGS = 41,
	QPU_R_VPM = 48,
	QPU_R_VPM_LD_BUSY,
	QPU_R_VPM_LD_WAIT,
	QPU_R_MUTEX_ACQUIRE,
};

enum qpu_waddr {
	/* 0-31 are the plain regfile a or b fields */
	QPU_W_ACC0 = 32, /* aka r0 */
	QPU_W_ACC1,
	QPU_W_ACC2,
	QPU_W_ACC3,
	QPU_W_TMU_NOSWAP,
	QPU_W_ACC5,
	QPU_W_HOST_INT,
	QPU_W_NOP,
	QPU_W_UNIFORMS_ADDRESS,
	QPU_W_QUAD_XY, /* X for regfile a, Y for regfile b */
	QPU_W_MS_FLAGS = 42,
	QPU_W_REV_FLAG = 42,
	QPU_W_TLB_STENCIL_SETUP = 43,
	QPU_W_TLB_Z,
	QPU_W_TLB_COLOR_MS,
	QPU_W_TLB_COLOR_ALL,
	QPU_W_TLB_ALPHA_MASK,
	QPU_W_VPM,
	QPU_W_VPMVCD_SETUP, /* LD for regfile a, ST for regfile b */
	QPU_W_VPM_ADDR, /* LD for regfile a, ST for regfile b */
	QPU_W_MUTEX_RELEASE,
	QPU_W_SFU_RECIP,
	QPU_W_SFU_RECIPSQRT,
	QPU_W_SFU_EXP,
	QPU_W_SFU_LOG,
	QPU_W_TMU0_S,
	QPU_W_TMU0_T,
	QPU_W_TMU0_R,
	QPU_W_TMU0_B,
	QPU_W_TMU1_S,
	QPU_W_TMU1_T,
	QPU_W_TMU1_R,
	QPU_W_TMU1_B,
};

enum qpu_sig_bits {
	QPU_SIG_SW_BREAKPOINT,
	QPU_SIG_NONE,
	QPU_SIG_THREAD_SWITCH,
	QPU_SIG_PROG_END,
	QPU_SIG_WAIT_FOR_SCOREBOARD,
	QPU_SIG_SCOREBOARD_UNLOCK,
	QPU_SIG_LAST_THREAD_SWITCH,
	QPU_SIG_COVERAGE_LOAD,
	QPU_SIG_COLOR_LOAD,
	QPU_SIG_COLOR_LOAD_END,
	QPU_SIG_LOAD_TMU0,
	QPU_SIG_LOAD_TMU1,
	QPU_SIG_ALPHA_MASK_LOAD,
	QPU_SIG_SMALL_IMM,
	QPU_SIG_LOAD_IMM,
	QPU_SIG_BRANCH
};

enum qpu_mux {
	/* hardware mux values */
	QPU_MUX_R0,
	QPU_MUX_R1,
	QPU_MUX_R2,
	QPU_MUX_R3,
	QPU_MUX_R4,
	QPU_MUX_R5,
	QPU_MUX_A,
	QPU_MUX_B,

	/* non-hardware mux values */
	QPU_MUX_IMM,
};

enum qpu_cond {
	QPU_COND_NEVER,
	QPU_COND_ALWAYS,
	QPU_COND_ZS,
	QPU_COND_ZC,
	QPU_COND_NS,
	QPU_COND_NC,
	QPU_COND_CS,
	QPU_COND_CC,
};

enum qpu_pack_mul {
	QPU_PACK_MUL_NOP,
	/* replicated to each 8 bits of the 32-bit dst. */
	QPU_PACK_MUL_8888 = 3,
	QPU_PACK_MUL_8A,
	QPU_PACK_MUL_8B,
	QPU_PACK_MUL_8C,
	QPU_PACK_MUL_8D,
};

enum qpu_pack_a {
	QPU_PACK_A_NOP,
	/* convert to 16 bit float if float input, or to int16. */
	QPU_PACK_A_16A,
	QPU_PACK_A_16B,
	/* replicated to each 8 bits of the 32-bit dst. */
	QPU_PACK_A_8888,
	/* Convert to 8-bit unsigned int. */
	QPU_PACK_A_8A,
	QPU_PACK_A_8B,
	QPU_PACK_A_8C,
	QPU_PACK_A_8D,

	/* Saturating variants of the previous instructions. */
	QPU_PACK_A_32_SAT, /* int-only */
	QPU_PACK_A_16A_SAT, /* int or float */
	QPU_PACK_A_16B_SAT,
	QPU_PACK_A_8888_SAT,
	QPU_PACK_A_8A_SAT,
	QPU_PACK_A_8B_SAT,
	QPU_PACK_A_8C_SAT,
	QPU_PACK_A_8D_SAT,
};

enum qpu_unpack_r4 {
	QPU_UNPACK_R4_NOP,
	QPU_UNPACK_R4_F16A_TO_F32,
	QPU_UNPACK_R4_F16B_TO_F32,
	QPU_UNPACK_R4_8D_REP,
	QPU_UNPACK_R4_8A,
	QPU_UNPACK_R4_8B,
	QPU_UNPACK_R4_8C,
	QPU_UNPACK_R4_8D,
};

#define QPU_MASK(high, low) \
	((((uint64_t)1 << ((high) - (low) + 1)) - 1) << (low))

#define QPU_GET_FIELD(word, field) \
	((uint32_t)(((word)  & field ## _MASK) >> field ## _SHIFT))

#define QPU_SIG_SHIFT                   60
#define QPU_SIG_MASK                    QPU_MASK(63, 60)

#define QPU_UNPACK_SHIFT                57
#define QPU_UNPACK_MASK                 QPU_MASK(59, 57)

/**
 * If set, the pack field means PACK_MUL or R4 packing, instead of normal
 * regfile a packing.
 */
#define QPU_PM                          ((uint64_t)1 << 56)

#define QPU_PACK_SHIFT                  52
#define QPU_PACK_MASK                   QPU_MASK(55, 52)

#define QPU_COND_ADD_SHIFT              49
#define QPU_COND_ADD_MASK               QPU_MASK(51, 49)
#define QPU_COND_MUL_SHIFT              46
#define QPU_COND_MUL_MASK               QPU_MASK(48, 46)

#define QPU_SF                          ((uint64_t)1 << 45)

#define QPU_WADDR_ADD_SHIFT             38
#define QPU_WADDR_ADD_MASK              QPU_MASK(43, 38)
#define QPU_WADDR_MUL_SHIFT             32
#define QPU_WADDR_MUL_MASK              QPU_MASK(37, 32)

#define QPU_OP_MUL_SHIFT                29
#define QPU_OP_MUL_MASK                 QPU_MASK(31, 29)

#define QPU_RADDR_A_SHIFT               18
#define QPU_RADDR_A_MASK                QPU_MASK(23, 18)
#define QPU_RADDR_B_SHIFT               12
#define QPU_RADDR_B_MASK                QPU_MASK(17, 12)
#define QPU_SMALL_IMM_SHIFT             12
#define QPU_SMALL_IMM_MASK              QPU_MASK(17, 12)

#define QPU_ADD_A_SHIFT                 9
#define QPU_ADD_A_MASK                  QPU_MASK(11, 9)
#define QPU_ADD_B_SHIFT                 6
#define QPU_ADD_B_MASK                  QPU_MASK(8, 6)
#define QPU_MUL_A_SHIFT                 3
#define QPU_MUL_A_MASK                  QPU_MASK(5, 3)
#define QPU_MUL_B_SHIFT                 0
#define QPU_MUL_B_MASK                  QPU_MASK(2, 0)

#define QPU_WS                          ((uint64_t)1 << 44)

#define QPU_OP_ADD_SHIFT                24
#define QPU_OP_ADD_MASK                 QPU_MASK(28, 24)

#endif /* VC4_QPU_DEFINES_H */
