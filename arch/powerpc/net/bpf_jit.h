/* bpf_jit.h: BPF JIT compiler for PPC64
 *
 * Copyright 2011 Matt Evans <matt@ozlabs.org>, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#define BPF_PPC_STACK_LOCALS	32
#define BPF_PPC_STACK_BASIC	(48+64)
#define BPF_PPC_STACK_SAVE	(18*8)
#define BPF_PPC_STACKFRAME	(BPF_PPC_STACK_BASIC+BPF_PPC_STACK_LOCALS+ \
				 BPF_PPC_STACK_SAVE)
#define BPF_PPC_SLOWPATH_FRAME	(48+64)

/*
 * Generated code register usage:
 *
 * As normal PPC C ABI (e.g. r1=sp, r2=TOC), with:
 *
 * skb		r3	(Entry parameter)
 * A register	r4
 * X register	r5
 * addr param	r6
 * r7-r10	scratch
 * skb->data	r14
 * skb headlen	r15	(skb->len - skb->data_len)
 * m[0]		r16
 * m[...]	...
 * m[15]	r31
 */
#define r_skb		3
#define r_ret		3
#define r_A		4
#define r_X		5
#define r_addr		6
#define r_scratch1	7
#define r_scratch2	8
#define r_D		14
#define r_HL		15
#define r_M		16

#ifndef __ASSEMBLY__

/*
 * Assembly helpers from arch/powerpc/net/bpf_jit.S:
 */
#define DECLARE_LOAD_FUNC(func)	\
	extern u8 func[], func##_negative_offset[], func##_positive_offset[]

DECLARE_LOAD_FUNC(sk_load_word);
DECLARE_LOAD_FUNC(sk_load_half);
DECLARE_LOAD_FUNC(sk_load_byte);
DECLARE_LOAD_FUNC(sk_load_byte_msh);

#define FUNCTION_DESCR_SIZE	24

/*
 * 16-bit immediate helper macros: HA() is for use with sign-extending instrs
 * (e.g. LD, ADDI).  If the bottom 16 bits is "-ve", add another bit into the
 * top half to negate the effect (i.e. 0xffff + 1 = 0x(1)0000).
 */
#define IMM_H(i)		((uintptr_t)(i)>>16)
#define IMM_HA(i)		(((uintptr_t)(i)>>16) +			      \
				 (((uintptr_t)(i) & 0x8000) >> 15))
#define IMM_L(i)		((uintptr_t)(i) & 0xffff)

#define PLANT_INSTR(d, idx, instr)					      \
	do { if (d) { (d)[idx] = instr; } idx++; } while (0)
#define EMIT(instr)		PLANT_INSTR(image, ctx->idx, instr)

#define PPC_NOP()		EMIT(PPC_INST_NOP)
#define PPC_BLR()		EMIT(PPC_INST_BLR)
#define PPC_BLRL()		EMIT(PPC_INST_BLRL)
#define PPC_MTLR(r)		EMIT(PPC_INST_MTLR | ___PPC_RT(r))
#define PPC_ADDI(d, a, i)	EMIT(PPC_INST_ADDI | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | IMM_L(i))
#define PPC_MR(d, a)		PPC_OR(d, a, a)
#define PPC_LI(r, i)		PPC_ADDI(r, 0, i)
#define PPC_ADDIS(d, a, i)	EMIT(PPC_INST_ADDIS |			      \
				     ___PPC_RS(d) | ___PPC_RA(a) | IMM_L(i))
#define PPC_LIS(r, i)		PPC_ADDIS(r, 0, i)
#define PPC_STD(r, base, i)	EMIT(PPC_INST_STD | ___PPC_RS(r) |	      \
				     ___PPC_RA(base) | ((i) & 0xfffc))

#define PPC_LD(r, base, i)	EMIT(PPC_INST_LD | ___PPC_RT(r) |	      \
				     ___PPC_RA(base) | IMM_L(i))
#define PPC_LWZ(r, base, i)	EMIT(PPC_INST_LWZ | ___PPC_RT(r) |	      \
				     ___PPC_RA(base) | IMM_L(i))
#define PPC_LHZ(r, base, i)	EMIT(PPC_INST_LHZ | ___PPC_RT(r) |	      \
				     ___PPC_RA(base) | IMM_L(i))
#define PPC_LHBRX(r, base, b)	EMIT(PPC_INST_LHBRX | ___PPC_RT(r) |	      \
				     ___PPC_RA(base) | ___PPC_RB(b))
/* Convenience helpers for the above with 'far' offsets: */
#define PPC_LD_OFFS(r, base, i) do { if ((i) < 32768) PPC_LD(r, base, i);     \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LD(r, r, IMM_L(i)); } } while(0)

#define PPC_LWZ_OFFS(r, base, i) do { if ((i) < 32768) PPC_LWZ(r, base, i);   \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LWZ(r, r, IMM_L(i)); } } while(0)

#define PPC_LHZ_OFFS(r, base, i) do { if ((i) < 32768) PPC_LHZ(r, base, i);   \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LHZ(r, r, IMM_L(i)); } } while(0)

#define PPC_CMPWI(a, i)		EMIT(PPC_INST_CMPWI | ___PPC_RA(a) | IMM_L(i))
#define PPC_CMPDI(a, i)		EMIT(PPC_INST_CMPDI | ___PPC_RA(a) | IMM_L(i))
#define PPC_CMPLWI(a, i)	EMIT(PPC_INST_CMPLWI | ___PPC_RA(a) | IMM_L(i))
#define PPC_CMPLW(a, b)		EMIT(PPC_INST_CMPLW | ___PPC_RA(a) | ___PPC_RB(b))

#define PPC_SUB(d, a, b)	EMIT(PPC_INST_SUB | ___PPC_RT(d) |	      \
				     ___PPC_RB(a) | ___PPC_RA(b))
#define PPC_ADD(d, a, b)	EMIT(PPC_INST_ADD | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_MUL(d, a, b)	EMIT(PPC_INST_MULLW | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_MULHWU(d, a, b)	EMIT(PPC_INST_MULHWU | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_MULI(d, a, i)	EMIT(PPC_INST_MULLI | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | IMM_L(i))
#define PPC_DIVWU(d, a, b)	EMIT(PPC_INST_DIVWU | ___PPC_RT(d) |	      \
				     ___PPC_RA(a) | ___PPC_RB(b))
#define PPC_AND(d, a, b)	EMIT(PPC_INST_AND | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_ANDI(d, a, i)	EMIT(PPC_INST_ANDI | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | IMM_L(i))
#define PPC_AND_DOT(d, a, b)	EMIT(PPC_INST_ANDDOT | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_OR(d, a, b)		EMIT(PPC_INST_OR | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_ORI(d, a, i)	EMIT(PPC_INST_ORI | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | IMM_L(i))
#define PPC_ORIS(d, a, i)	EMIT(PPC_INST_ORIS | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | IMM_L(i))
#define PPC_XOR(d, a, b)	EMIT(PPC_INST_XOR | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(b))
#define PPC_XORI(d, a, i)	EMIT(PPC_INST_XORI | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | IMM_L(i))
#define PPC_XORIS(d, a, i)	EMIT(PPC_INST_XORIS | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | IMM_L(i))
#define PPC_SLW(d, a, s)	EMIT(PPC_INST_SLW | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(s))
#define PPC_SRW(d, a, s)	EMIT(PPC_INST_SRW | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | ___PPC_RB(s))
/* slwi = rlwinm Rx, Ry, n, 0, 31-n */
#define PPC_SLWI(d, a, i)	EMIT(PPC_INST_RLWINM | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | __PPC_SH(i) |	      \
				     __PPC_MB(0) | __PPC_ME(31-(i)))
/* srwi = rlwinm Rx, Ry, 32-n, n, 31 */
#define PPC_SRWI(d, a, i)	EMIT(PPC_INST_RLWINM | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | __PPC_SH(32-(i)) |	      \
				     __PPC_MB(i) | __PPC_ME(31))
/* sldi = rldicr Rx, Ry, n, 63-n */
#define PPC_SLDI(d, a, i)	EMIT(PPC_INST_RLDICR | ___PPC_RA(d) |	      \
				     ___PPC_RS(a) | __PPC_SH(i) |	      \
				     __PPC_MB(63-(i)) | (((i) & 0x20) >> 4))
#define PPC_NEG(d, a)		EMIT(PPC_INST_NEG | ___PPC_RT(d) | ___PPC_RA(a))

/* Long jump; (unconditional 'branch') */
#define PPC_JMP(dest)		EMIT(PPC_INST_BRANCH |			      \
				     (((dest) - (ctx->idx * 4)) & 0x03fffffc))
/* "cond" here covers BO:BI fields. */
#define PPC_BCC_SHORT(cond, dest)	EMIT(PPC_INST_BRANCH_COND |	      \
					     (((cond) & 0x3ff) << 16) |	      \
					     (((dest) - (ctx->idx * 4)) &     \
					      0xfffc))
#define PPC_LI32(d, i)		do { PPC_LI(d, IMM_L(i));		      \
		if ((u32)(uintptr_t)(i) >= 32768) {			      \
			PPC_ADDIS(d, d, IMM_HA(i));			      \
		} } while(0)
#define PPC_LI64(d, i)		do {					      \
		if (!((uintptr_t)(i) & 0xffffffff00000000ULL))		      \
			PPC_LI32(d, i);					      \
		else {							      \
			PPC_LIS(d, ((uintptr_t)(i) >> 48));		      \
			if ((uintptr_t)(i) & 0x0000ffff00000000ULL)	      \
				PPC_ORI(d, d,				      \
					((uintptr_t)(i) >> 32) & 0xffff);     \
			PPC_SLDI(d, d, 32);				      \
			if ((uintptr_t)(i) & 0x00000000ffff0000ULL)	      \
				PPC_ORIS(d, d,				      \
					 ((uintptr_t)(i) >> 16) & 0xffff);    \
			if ((uintptr_t)(i) & 0x000000000000ffffULL)	      \
				PPC_ORI(d, d, (uintptr_t)(i) & 0xffff);	      \
		} } while (0);

#define PPC_LHBRX_OFFS(r, base, i) \
		do { PPC_LI32(r, i); PPC_LHBRX(r, r, base); } while(0)
#ifdef __LITTLE_ENDIAN__
#define PPC_NTOHS_OFFS(r, base, i)	PPC_LHBRX_OFFS(r, base, i)
#else
#define PPC_NTOHS_OFFS(r, base, i)	PPC_LHZ_OFFS(r, base, i)
#endif

static inline bool is_nearbranch(int offset)
{
	return (offset < 32768) && (offset >= -32768);
}

/*
 * The fly in the ointment of code size changing from pass to pass is
 * avoided by padding the short branch case with a NOP.	 If code size differs
 * with different branch reaches we will have the issue of code moving from
 * one pass to the next and will need a few passes to converge on a stable
 * state.
 */
#define PPC_BCC(cond, dest)	do {					      \
		if (is_nearbranch((dest) - (ctx->idx * 4))) {		      \
			PPC_BCC_SHORT(cond, dest);			      \
			PPC_NOP();					      \
		} else {						      \
			/* Flip the 'T or F' bit to invert comparison */      \
			PPC_BCC_SHORT(cond ^ COND_CMP_TRUE, (ctx->idx+2)*4);  \
			PPC_JMP(dest);					      \
		} } while(0)

/* To create a branch condition, select a bit of cr0... */
#define CR0_LT		0
#define CR0_GT		1
#define CR0_EQ		2
/* ...and modify BO[3] */
#define COND_CMP_TRUE	0x100
#define COND_CMP_FALSE	0x000
/* Together, they make all required comparisons: */
#define COND_GT		(CR0_GT | COND_CMP_TRUE)
#define COND_GE		(CR0_LT | COND_CMP_FALSE)
#define COND_EQ		(CR0_EQ | COND_CMP_TRUE)
#define COND_NE		(CR0_EQ | COND_CMP_FALSE)
#define COND_LT		(CR0_LT | COND_CMP_TRUE)

#define SEEN_DATAREF 0x10000 /* might call external helpers */
#define SEEN_XREG    0x20000 /* X reg is used */
#define SEEN_MEM     0x40000 /* SEEN_MEM+(1<<n) = use mem[n] for temporary
			      * storage */
#define SEEN_MEM_MSK 0x0ffff

struct codegen_context {
	unsigned int seen;
	unsigned int idx;
	int pc_ret0; /* bpf index of first RET #0 instruction (if any) */
};

#endif

#endif
