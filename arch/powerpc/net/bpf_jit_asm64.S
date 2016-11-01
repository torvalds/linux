/*
 * bpf_jit_asm64.S: Packet/header access helper functions
 * for PPC64 BPF compiler.
 *
 * Copyright 2016, Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 * 		   IBM Corporation
 *
 * Based on bpf_jit_asm.S by Matt Evans
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <asm/ppc_asm.h>
#include <asm/ptrace.h>
#include "bpf_jit64.h"

/*
 * All of these routines are called directly from generated code,
 * with the below register usage:
 * r27		skb pointer (ctx)
 * r25		skb header length
 * r26		skb->data pointer
 * r4		offset
 *
 * Result is passed back in:
 * r8		data read in host endian format (accumulator)
 *
 * r9 is used as a temporary register
 */

#define r_skb	r27
#define r_hlen	r25
#define r_data	r26
#define r_off	r4
#define r_val	r8
#define r_tmp	r9

_GLOBAL_TOC(sk_load_word)
	cmpdi	r_off, 0
	blt	bpf_slow_path_word_neg
	b	sk_load_word_positive_offset

_GLOBAL_TOC(sk_load_word_positive_offset)
	/* Are we accessing past headlen? */
	subi	r_tmp, r_hlen, 4
	cmpd	r_tmp, r_off
	blt	bpf_slow_path_word
	/* Nope, just hitting the header.  cr0 here is eq or gt! */
	LWZX_BE	r_val, r_data, r_off
	blr	/* Return success, cr0 != LT */

_GLOBAL_TOC(sk_load_half)
	cmpdi	r_off, 0
	blt	bpf_slow_path_half_neg
	b	sk_load_half_positive_offset

_GLOBAL_TOC(sk_load_half_positive_offset)
	subi	r_tmp, r_hlen, 2
	cmpd	r_tmp, r_off
	blt	bpf_slow_path_half
	LHZX_BE	r_val, r_data, r_off
	blr

_GLOBAL_TOC(sk_load_byte)
	cmpdi	r_off, 0
	blt	bpf_slow_path_byte_neg
	b	sk_load_byte_positive_offset

_GLOBAL_TOC(sk_load_byte_positive_offset)
	cmpd	r_hlen, r_off
	ble	bpf_slow_path_byte
	lbzx	r_val, r_data, r_off
	blr

/*
 * Call out to skb_copy_bits:
 * Allocate a new stack frame here to remain ABI-compliant in
 * stashing LR.
 */
#define bpf_slow_path_common(SIZE)					\
	mflr	r0;							\
	std	r0, PPC_LR_STKOFF(r1);					\
	stdu	r1, -(STACK_FRAME_MIN_SIZE + BPF_PPC_STACK_LOCALS)(r1);	\
	mr	r3, r_skb;						\
	/* r4 = r_off as passed */					\
	addi	r5, r1, STACK_FRAME_MIN_SIZE;				\
	li	r6, SIZE;						\
	bl	skb_copy_bits;						\
	nop;								\
	/* save r5 */							\
	addi	r5, r1, STACK_FRAME_MIN_SIZE;				\
	/* r3 = 0 on success */						\
	addi	r1, r1, STACK_FRAME_MIN_SIZE + BPF_PPC_STACK_LOCALS;	\
	ld	r0, PPC_LR_STKOFF(r1);					\
	mtlr	r0;							\
	cmpdi	r3, 0;							\
	blt	bpf_error;	/* cr0 = LT */

bpf_slow_path_word:
	bpf_slow_path_common(4)
	/* Data value is on stack, and cr0 != LT */
	LWZX_BE	r_val, 0, r5
	blr

bpf_slow_path_half:
	bpf_slow_path_common(2)
	LHZX_BE	r_val, 0, r5
	blr

bpf_slow_path_byte:
	bpf_slow_path_common(1)
	lbzx	r_val, 0, r5
	blr

/*
 * Call out to bpf_internal_load_pointer_neg_helper
 */
#define sk_negative_common(SIZE)				\
	mflr	r0;						\
	std	r0, PPC_LR_STKOFF(r1);				\
	stdu	r1, -STACK_FRAME_MIN_SIZE(r1);			\
	mr	r3, r_skb;					\
	/* r4 = r_off, as passed */				\
	li	r5, SIZE;					\
	bl	bpf_internal_load_pointer_neg_helper;		\
	nop;							\
	addi	r1, r1, STACK_FRAME_MIN_SIZE;			\
	ld	r0, PPC_LR_STKOFF(r1);				\
	mtlr	r0;						\
	/* R3 != 0 on success */				\
	cmpldi	r3, 0;						\
	beq	bpf_error_slow;	/* cr0 = EQ */

bpf_slow_path_word_neg:
	lis     r_tmp, -32	/* SKF_LL_OFF */
	cmpd	r_off, r_tmp	/* addr < SKF_* */
	blt	bpf_error	/* cr0 = LT */
	b	sk_load_word_negative_offset

_GLOBAL_TOC(sk_load_word_negative_offset)
	sk_negative_common(4)
	LWZX_BE	r_val, 0, r3
	blr

bpf_slow_path_half_neg:
	lis     r_tmp, -32	/* SKF_LL_OFF */
	cmpd	r_off, r_tmp	/* addr < SKF_* */
	blt	bpf_error	/* cr0 = LT */
	b	sk_load_half_negative_offset

_GLOBAL_TOC(sk_load_half_negative_offset)
	sk_negative_common(2)
	LHZX_BE	r_val, 0, r3
	blr

bpf_slow_path_byte_neg:
	lis     r_tmp, -32	/* SKF_LL_OFF */
	cmpd	r_off, r_tmp	/* addr < SKF_* */
	blt	bpf_error	/* cr0 = LT */
	b	sk_load_byte_negative_offset

_GLOBAL_TOC(sk_load_byte_negative_offset)
	sk_negative_common(1)
	lbzx	r_val, 0, r3
	blr

bpf_error_slow:
	/* fabricate a cr0 = lt */
	li	r_tmp, -1
	cmpdi	r_tmp, 0
bpf_error:
	/*
	 * Entered with cr0 = lt
	 * Generated code will 'blt epilogue', returning 0.
	 */
	li	r_val, 0
	blr
