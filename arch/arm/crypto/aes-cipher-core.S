/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/cache.h>

	.text
	.align		5

	rk		.req	r0
	rounds		.req	r1
	in		.req	r2
	out		.req	r3
	ttab		.req	ip

	t0		.req	lr
	t1		.req	r2
	t2		.req	r3

	.macro		__select, out, in, idx
	.if		__LINUX_ARM_ARCH__ < 7
	and		\out, \in, #0xff << (8 * \idx)
	.else
	ubfx		\out, \in, #(8 * \idx), #8
	.endif
	.endm

	.macro		__load, out, in, idx, sz, op
	.if		__LINUX_ARM_ARCH__ < 7 && \idx > 0
	ldr\op		\out, [ttab, \in, lsr #(8 * \idx) - \sz]
	.else
	ldr\op		\out, [ttab, \in, lsl #\sz]
	.endif
	.endm

	.macro		__hround, out0, out1, in0, in1, in2, in3, t3, t4, enc, sz, op, oldcpsr
	__select	\out0, \in0, 0
	__select	t0, \in1, 1
	__load		\out0, \out0, 0, \sz, \op
	__load		t0, t0, 1, \sz, \op

	.if		\enc
	__select	\out1, \in1, 0
	__select	t1, \in2, 1
	.else
	__select	\out1, \in3, 0
	__select	t1, \in0, 1
	.endif
	__load		\out1, \out1, 0, \sz, \op
	__select	t2, \in2, 2
	__load		t1, t1, 1, \sz, \op
	__load		t2, t2, 2, \sz, \op

	eor		\out0, \out0, t0, ror #24

	__select	t0, \in3, 3
	.if		\enc
	__select	\t3, \in3, 2
	__select	\t4, \in0, 3
	.else
	__select	\t3, \in1, 2
	__select	\t4, \in2, 3
	.endif
	__load		\t3, \t3, 2, \sz, \op
	__load		t0, t0, 3, \sz, \op
	__load		\t4, \t4, 3, \sz, \op

	.ifnb		\oldcpsr
	/*
	 * This is the final round and we're done with all data-dependent table
	 * lookups, so we can safely re-enable interrupts.
	 */
	restore_irqs	\oldcpsr
	.endif

	eor		\out1, \out1, t1, ror #24
	eor		\out0, \out0, t2, ror #16
	ldm		rk!, {t1, t2}
	eor		\out1, \out1, \t3, ror #16
	eor		\out0, \out0, t0, ror #8
	eor		\out1, \out1, \t4, ror #8
	eor		\out0, \out0, t1
	eor		\out1, \out1, t2
	.endm

	.macro		fround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op, oldcpsr
	__hround	\out0, \out1, \in0, \in1, \in2, \in3, \out2, \out3, 1, \sz, \op
	__hround	\out2, \out3, \in2, \in3, \in0, \in1, \in1, \in2, 1, \sz, \op, \oldcpsr
	.endm

	.macro		iround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op, oldcpsr
	__hround	\out0, \out1, \in0, \in3, \in2, \in1, \out2, \out3, 0, \sz, \op
	__hround	\out2, \out3, \in2, \in1, \in0, \in3, \in1, \in0, 0, \sz, \op, \oldcpsr
	.endm

	.macro		do_crypt, round, ttab, ltab, bsz
	push		{r3-r11, lr}

	// Load keys first, to reduce latency in case they're not cached yet.
	ldm		rk!, {r8-r11}

	ldr		r4, [in]
	ldr		r5, [in, #4]
	ldr		r6, [in, #8]
	ldr		r7, [in, #12]

#ifdef CONFIG_CPU_BIG_ENDIAN
	rev_l		r4, t0
	rev_l		r5, t0
	rev_l		r6, t0
	rev_l		r7, t0
#endif

	eor		r4, r4, r8
	eor		r5, r5, r9
	eor		r6, r6, r10
	eor		r7, r7, r11

	mov_l		ttab, \ttab
	/*
	 * Disable interrupts and prefetch the 1024-byte 'ft' or 'it' table into
	 * L1 cache, assuming cacheline size >= 32.  This is a hardening measure
	 * intended to make cache-timing attacks more difficult.  They may not
	 * be fully prevented, however; see the paper
	 * https://cr.yp.to/antiforgery/cachetiming-20050414.pdf
	 * ("Cache-timing attacks on AES") for a discussion of the many
	 * difficulties involved in writing truly constant-time AES software.
	 */
	 save_and_disable_irqs	t0
	.set		i, 0
	.rept		1024 / 128
	ldr		r8, [ttab, #i + 0]
	ldr		r9, [ttab, #i + 32]
	ldr		r10, [ttab, #i + 64]
	ldr		r11, [ttab, #i + 96]
	.set		i, i + 128
	.endr
	push		{t0}		// oldcpsr

	tst		rounds, #2
	bne		1f

0:	\round		r8, r9, r10, r11, r4, r5, r6, r7
	\round		r4, r5, r6, r7, r8, r9, r10, r11

1:	subs		rounds, rounds, #4
	\round		r8, r9, r10, r11, r4, r5, r6, r7
	bls		2f
	\round		r4, r5, r6, r7, r8, r9, r10, r11
	b		0b

2:	.ifb		\ltab
	add		ttab, ttab, #1
	.else
	mov_l		ttab, \ltab
	// Prefetch inverse S-box for final round; see explanation above
	.set		i, 0
	.rept		256 / 64
	ldr		t0, [ttab, #i + 0]
	ldr		t1, [ttab, #i + 32]
	.set		i, i + 64
	.endr
	.endif

	pop		{rounds}	// oldcpsr
	\round		r4, r5, r6, r7, r8, r9, r10, r11, \bsz, b, rounds

#ifdef CONFIG_CPU_BIG_ENDIAN
	rev_l		r4, t0
	rev_l		r5, t0
	rev_l		r6, t0
	rev_l		r7, t0
#endif

	ldr		out, [sp]

	str		r4, [out]
	str		r5, [out, #4]
	str		r6, [out, #8]
	str		r7, [out, #12]

	pop		{r3-r11, pc}

	.align		3
	.ltorg
	.endm

ENTRY(__aes_arm_encrypt)
	do_crypt	fround, crypto_ft_tab,, 2
ENDPROC(__aes_arm_encrypt)

	.align		5
ENTRY(__aes_arm_decrypt)
	do_crypt	iround, crypto_it_tab, crypto_aes_inv_sbox, 0
ENDPROC(__aes_arm_decrypt)
