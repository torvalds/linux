/*
 * bpf_jib_asm.S: Packet/header access helper functions for MIPS/MIPS64 BPF
 * compiler.
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#include <asm/asm.h>
#include <asm/regdef.h>
#include "bpf_jit.h"

/* ABI
 *
 * r_skb_hl	skb header length
 * r_skb_data	skb data
 * r_off(a1)	offset register
 * r_A		BPF register A
 * r_X		PF register X
 * r_skb(a0)	*skb
 * r_M		*scratch memory
 * r_skb_le	skb length
 * r_s0		Scratch register 0
 * r_s1		Scratch register 1
 *
 * On entry:
 * a0: *skb
 * a1: offset (imm or imm + X)
 *
 * All non-BPF-ABI registers are free for use. On return, we only
 * care about r_ret. The BPF-ABI registers are assumed to remain
 * unmodified during the entire filter operation.
 */

#define skb	a0
#define offset	a1
#define SKF_LL_OFF  (-0x200000) /* Can't include linux/filter.h in assembly */

	/* We know better :) so prevent assembler reordering etc */
	.set 	noreorder

#define is_offset_negative(TYPE)				\
	/* If offset is negative we have more work to do */	\
	slti	t0, offset, 0;					\
	bgtz	t0, bpf_slow_path_##TYPE##_neg;			\
	/* Be careful what follows in DS. */

#define is_offset_in_header(SIZE, TYPE)				\
	/* Reading from header? */				\
	addiu	$r_s0, $r_skb_hl, -SIZE;			\
	slt	t0, $r_s0, offset;				\
	bgtz	t0, bpf_slow_path_##TYPE;			\

LEAF(sk_load_word)
	is_offset_negative(word)
	.globl sk_load_word_positive
sk_load_word_positive:
	is_offset_in_header(4, word)
	/* Offset within header boundaries */
	PTR_ADDU t1, $r_skb_data, offset
	lw	$r_A, 0(t1)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	wsbh	t0, $r_A
	rotr	$r_A, t0, 16
#endif
	jr	$r_ra
	 move	$r_ret, zero
	END(sk_load_word)

LEAF(sk_load_half)
	is_offset_negative(half)
	.globl sk_load_half_positive
sk_load_half_positive:
	is_offset_in_header(2, half)
	/* Offset within header boundaries */
	PTR_ADDU t1, $r_skb_data, offset
	lh	$r_A, 0(t1)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	wsbh	t0, $r_A
	seh	$r_A, t0
#endif
	jr	$r_ra
	 move	$r_ret, zero
	END(sk_load_half)

LEAF(sk_load_byte)
	is_offset_negative(byte)
	.globl sk_load_byte_positive
sk_load_byte_positive:
	is_offset_in_header(1, byte)
	/* Offset within header boundaries */
	PTR_ADDU t1, $r_skb_data, offset
	lb	$r_A, 0(t1)
	jr	$r_ra
	 move	$r_ret, zero
	END(sk_load_byte)

/*
 * call skb_copy_bits:
 * (prototype in linux/skbuff.h)
 *
 * int skb_copy_bits(sk_buff *skb, int offset, void *to, int len)
 *
 * o32 mandates we leave 4 spaces for argument registers in case
 * the callee needs to use them. Even though we don't care about
 * the argument registers ourselves, we need to allocate that space
 * to remain ABI compliant since the callee may want to use that space.
 * We also allocate 2 more spaces for $r_ra and our return register (*to).
 *
 * n64 is a bit different. The *caller* will allocate the space to preserve
 * the arguments. So in 64-bit kernels, we allocate the 4-arg space for no
 * good reason but it does not matter that much really.
 *
 * (void *to) is returned in r_s0
 *
 */
#define bpf_slow_path_common(SIZE)				\
	/* Quick check. Are we within reasonable boundaries? */ \
	LONG_ADDIU	$r_s1, $r_skb_len, -SIZE;		\
	sltu		$r_s0, offset, $r_s1;			\
	beqz		$r_s0, fault;				\
	/* Load 4th argument in DS */				\
	 LONG_ADDIU	a3, zero, SIZE;				\
	PTR_ADDIU	$r_sp, $r_sp, -(6 * SZREG);		\
	PTR_LA		t0, skb_copy_bits;			\
	PTR_S		$r_ra, (5 * SZREG)($r_sp);		\
	/* Assign low slot to a2 */				\
	move		a2, $r_sp;				\
	jalr		t0;					\
	/* Reset our destination slot (DS but it's ok) */	\
	 INT_S		zero, (4 * SZREG)($r_sp);		\
	/*							\
	 * skb_copy_bits returns 0 on success and -EFAULT	\
	 * on error. Our data live in a2. Do not bother with	\
	 * our data if an error has been returned.		\
	 */							\
	/* Restore our frame */					\
	PTR_L		$r_ra, (5 * SZREG)($r_sp);		\
	INT_L		$r_s0, (4 * SZREG)($r_sp);		\
	bltz		v0, fault;				\
	 PTR_ADDIU	$r_sp, $r_sp, 6 * SZREG;		\
	move		$r_ret, zero;				\

NESTED(bpf_slow_path_word, (6 * SZREG), $r_sp)
	bpf_slow_path_common(4)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	wsbh	t0, $r_s0
	jr	$r_ra
	 rotr	$r_A, t0, 16
#endif
	jr	$r_ra
	move	$r_A, $r_s0

	END(bpf_slow_path_word)

NESTED(bpf_slow_path_half, (6 * SZREG), $r_sp)
	bpf_slow_path_common(2)
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	jr	$r_ra
	 wsbh	$r_A, $r_s0
#endif
	jr	$r_ra
	 move	$r_A, $r_s0

	END(bpf_slow_path_half)

NESTED(bpf_slow_path_byte, (6 * SZREG), $r_sp)
	bpf_slow_path_common(1)
	jr	$r_ra
	 move	$r_A, $r_s0

	END(bpf_slow_path_byte)

/*
 * Negative entry points
 */
	.macro bpf_is_end_of_data
	li	t0, SKF_LL_OFF
	/* Reading link layer data? */
	slt	t1, offset, t0
	bgtz	t1, fault
	/* Be careful what follows in DS. */
	.endm
/*
 * call skb_copy_bits:
 * (prototype in linux/filter.h)
 *
 * void *bpf_internal_load_pointer_neg_helper(const struct sk_buff *skb,
 *                                            int k, unsigned int size)
 *
 * see above (bpf_slow_path_common) for ABI restrictions
 */
#define bpf_negative_common(SIZE)					\
	PTR_ADDIU	$r_sp, $r_sp, -(6 * SZREG);			\
	PTR_LA		t0, bpf_internal_load_pointer_neg_helper;	\
	PTR_S		$r_ra, (5 * SZREG)($r_sp);			\
	jalr		t0;						\
	 li		a2, SIZE;					\
	PTR_L		$r_ra, (5 * SZREG)($r_sp);			\
	/* Check return pointer */					\
	beqz		v0, fault;					\
	 PTR_ADDIU	$r_sp, $r_sp, 6 * SZREG;			\
	/* Preserve our pointer */					\
	move		$r_s0, v0;					\
	/* Set return value */						\
	move		$r_ret, zero;					\

bpf_slow_path_word_neg:
	bpf_is_end_of_data
NESTED(sk_load_word_negative, (6 * SZREG), $r_sp)
	bpf_negative_common(4)
	jr	$r_ra
	 lw	$r_A, 0($r_s0)
	END(sk_load_word_negative)

bpf_slow_path_half_neg:
	bpf_is_end_of_data
NESTED(sk_load_half_negative, (6 * SZREG), $r_sp)
	bpf_negative_common(2)
	jr	$r_ra
	 lhu	$r_A, 0($r_s0)
	END(sk_load_half_negative)

bpf_slow_path_byte_neg:
	bpf_is_end_of_data
NESTED(sk_load_byte_negative, (6 * SZREG), $r_sp)
	bpf_negative_common(1)
	jr	$r_ra
	 lbu	$r_A, 0($r_s0)
	END(sk_load_byte_negative)

fault:
	jr	$r_ra
	 addiu $r_ret, zero, 1
