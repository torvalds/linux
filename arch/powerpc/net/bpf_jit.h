/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bpf_jit.h: BPF JIT compiler for PPC
 *
 * Copyright 2011 Matt Evans <matt@ozlabs.org>, IBM Corporation
 * 	     2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 */
#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#ifndef __ASSEMBLY__

#include <asm/types.h>
#include <asm/ppc-opcode.h>
#include <asm/code-patching.h>

#ifdef PPC64_ELF_ABI_v1
#define FUNCTION_DESCR_SIZE	24
#else
#define FUNCTION_DESCR_SIZE	0
#endif

#define PLANT_INSTR(d, idx, instr)					      \
	do { if (d) { (d)[idx] = instr; } idx++; } while (0)
#define EMIT(instr)		PLANT_INSTR(image, ctx->idx, instr)

/* Long jump; (unconditional 'branch') */
#define PPC_JMP(dest)							      \
	do {								      \
		long offset = (long)(dest) - (ctx->idx * 4);		      \
		if (!is_offset_in_branch_range(offset)) {		      \
			pr_err_ratelimited("Branch offset 0x%lx (@%u) out of range\n", offset, ctx->idx);			\
			return -ERANGE;					      \
		}							      \
		EMIT(PPC_INST_BRANCH | (offset & 0x03fffffc));		      \
	} while (0)
/* "cond" here covers BO:BI fields. */
#define PPC_BCC_SHORT(cond, dest)					      \
	do {								      \
		long offset = (long)(dest) - (ctx->idx * 4);		      \
		if (!is_offset_in_cond_branch_range(offset)) {		      \
			pr_err_ratelimited("Conditional branch offset 0x%lx (@%u) out of range\n", offset, ctx->idx);		\
			return -ERANGE;					      \
		}							      \
		EMIT(PPC_INST_BRANCH_COND | (((cond) & 0x3ff) << 16) | (offset & 0xfffc));					\
	} while (0)

/* Sign-extended 32-bit immediate load */
#define PPC_LI32(d, i)		do {					      \
		if ((int)(uintptr_t)(i) >= -32768 &&			      \
				(int)(uintptr_t)(i) < 32768)		      \
			EMIT(PPC_RAW_LI(d, i));				      \
		else {							      \
			EMIT(PPC_RAW_LIS(d, IMM_H(i)));			      \
			if (IMM_L(i))					      \
				EMIT(PPC_RAW_ORI(d, d, IMM_L(i)));	      \
		} } while(0)

#define PPC_LI64(d, i)		do {					      \
		if ((long)(i) >= -2147483648 &&				      \
				(long)(i) < 2147483648)			      \
			PPC_LI32(d, i);					      \
		else {							      \
			if (!((uintptr_t)(i) & 0xffff800000000000ULL))	      \
				EMIT(PPC_RAW_LI(d, ((uintptr_t)(i) >> 32) &   \
						0xffff));		      \
			else {						      \
				EMIT(PPC_RAW_LIS(d, ((uintptr_t)(i) >> 48))); \
				if ((uintptr_t)(i) & 0x0000ffff00000000ULL)   \
					EMIT(PPC_RAW_ORI(d, d,		      \
					  ((uintptr_t)(i) >> 32) & 0xffff));  \
			}						      \
			EMIT(PPC_RAW_SLDI(d, d, 32));			      \
			if ((uintptr_t)(i) & 0x00000000ffff0000ULL)	      \
				EMIT(PPC_RAW_ORIS(d, d,			      \
					 ((uintptr_t)(i) >> 16) & 0xffff));   \
			if ((uintptr_t)(i) & 0x000000000000ffffULL)	      \
				EMIT(PPC_RAW_ORI(d, d, (uintptr_t)(i) &       \
							0xffff));             \
		} } while (0)

#ifdef CONFIG_PPC64
#define PPC_FUNC_ADDR(d,i) do { PPC_LI64(d, i); } while(0)
#else
#define PPC_FUNC_ADDR(d,i) do { PPC_LI32(d, i); } while(0)
#endif

/*
 * The fly in the ointment of code size changing from pass to pass is
 * avoided by padding the short branch case with a NOP.	 If code size differs
 * with different branch reaches we will have the issue of code moving from
 * one pass to the next and will need a few passes to converge on a stable
 * state.
 */
#define PPC_BCC(cond, dest)	do {					      \
		if (is_offset_in_cond_branch_range((long)(dest) - (ctx->idx * 4))) {	\
			PPC_BCC_SHORT(cond, dest);			      \
			EMIT(PPC_RAW_NOP());				      \
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
#define COND_LE		(CR0_GT | COND_CMP_FALSE)

#endif

#endif
