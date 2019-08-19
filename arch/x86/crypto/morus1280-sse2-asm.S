/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SSE2 implementation of MORUS-1280
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 */

#include <linux/linkage.h>
#include <asm/frame.h>

#define SHUFFLE_MASK(i0, i1, i2, i3) \
	(i0 | (i1 << 2) | (i2 << 4) | (i3 << 6))

#define MASK2 SHUFFLE_MASK(2, 3, 0, 1)

#define STATE0_LO	%xmm0
#define STATE0_HI	%xmm1
#define STATE1_LO	%xmm2
#define STATE1_HI	%xmm3
#define STATE2_LO	%xmm4
#define STATE2_HI	%xmm5
#define STATE3_LO	%xmm6
#define STATE3_HI	%xmm7
#define STATE4_LO	%xmm8
#define STATE4_HI	%xmm9
#define KEY_LO		%xmm10
#define KEY_HI		%xmm11
#define MSG_LO		%xmm10
#define MSG_HI		%xmm11
#define T0_LO		%xmm12
#define T0_HI		%xmm13
#define T1_LO		%xmm14
#define T1_HI		%xmm15

.section .rodata.cst16.morus640_const, "aM", @progbits, 16
.align 16
.Lmorus640_const_0:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
.Lmorus640_const_1:
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst16.morus640_counter, "aM", @progbits, 16
.align 16
.Lmorus640_counter_0:
	.byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
.Lmorus640_counter_1:
	.byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
	.byte 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f

.text

.macro rol1 hi, lo
	/*
	 * HI_1 | HI_0 || LO_1 | LO_0
	 *  ==>
	 * HI_0 | HI_1 || LO_1 | LO_0
	 *  ==>
	 * HI_0 | LO_1 || LO_0 | HI_1
	 */
	pshufd $MASK2, \hi, \hi
	movdqa \hi, T0_LO
	punpcklqdq \lo, T0_LO
	punpckhqdq \hi, \lo
	movdqa \lo, \hi
	movdqa T0_LO, \lo
.endm

.macro rol2 hi, lo
	movdqa \lo, T0_LO
	movdqa \hi, \lo
	movdqa T0_LO, \hi
.endm

.macro rol3 hi, lo
	/*
	 * HI_1 | HI_0 || LO_1 | LO_0
	 *  ==>
	 * HI_0 | HI_1 || LO_1 | LO_0
	 *  ==>
	 * LO_0 | HI_1 || HI_0 | LO_1
	 */
	pshufd $MASK2, \hi, \hi
	movdqa \lo, T0_LO
	punpckhqdq \hi, T0_LO
	punpcklqdq \lo, \hi
	movdqa T0_LO, \lo
.endm

.macro morus1280_round s0_l, s0_h, s1_l, s1_h, s2_l, s2_h, s3_l, s3_h, s4_l, s4_h, b, w
	movdqa \s1_l, T0_LO
	pand \s2_l, T0_LO
	pxor T0_LO, \s0_l

	movdqa \s1_h, T0_LO
	pand \s2_h, T0_LO
	pxor T0_LO, \s0_h

	pxor \s3_l, \s0_l
	pxor \s3_h, \s0_h

	movdqa \s0_l, T0_LO
	psllq $\b, T0_LO
	psrlq $(64 - \b), \s0_l
	pxor T0_LO, \s0_l

	movdqa \s0_h, T0_LO
	psllq $\b, T0_LO
	psrlq $(64 - \b), \s0_h
	pxor T0_LO, \s0_h

	\w \s3_h, \s3_l
.endm

/*
 * __morus1280_update: internal ABI
 * input:
 *   STATE[0-4] - input state
 *   MSG        - message block
 * output:
 *   STATE[0-4] - output state
 * changed:
 *   T0
 */
__morus1280_update:
	morus1280_round \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		13, rol1
	pxor MSG_LO, STATE1_LO
	pxor MSG_HI, STATE1_HI
	morus1280_round \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		46, rol2
	pxor MSG_LO, STATE2_LO
	pxor MSG_HI, STATE2_HI
	morus1280_round \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		38, rol3
	pxor MSG_LO, STATE3_LO
	pxor MSG_HI, STATE3_HI
	morus1280_round \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		7, rol2
	pxor MSG_LO, STATE4_LO
	pxor MSG_HI, STATE4_HI
	morus1280_round \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		4, rol1
	ret
ENDPROC(__morus1280_update)

/*
 * __morus1280_update_zero: internal ABI
 * input:
 *   STATE[0-4] - input state
 * output:
 *   STATE[0-4] - output state
 * changed:
 *   T0
 */
__morus1280_update_zero:
	morus1280_round \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		13, rol1
	morus1280_round \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		46, rol2
	morus1280_round \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		38, rol3
	morus1280_round \
		STATE3_LO, STATE3_HI, \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		7, rol2
	morus1280_round \
		STATE4_LO, STATE4_HI, \
		STATE0_LO, STATE0_HI, \
		STATE1_LO, STATE1_HI, \
		STATE2_LO, STATE2_HI, \
		STATE3_LO, STATE3_HI, \
		4, rol1
	ret
ENDPROC(__morus1280_update_zero)

/*
 * __load_partial: internal ABI
 * input:
 *   %rsi - src
 *   %rcx - bytes
 * output:
 *   MSG  - message block
 * changed:
 *   %r8
 *   %r9
 */
__load_partial:
	xor %r9d, %r9d
	pxor MSG_LO, MSG_LO
	pxor MSG_HI, MSG_HI

	mov %rcx, %r8
	and $0x1, %r8
	jz .Lld_partial_1

	mov %rcx, %r8
	and $0x1E, %r8
	add %rsi, %r8
	mov (%r8), %r9b

.Lld_partial_1:
	mov %rcx, %r8
	and $0x2, %r8
	jz .Lld_partial_2

	mov %rcx, %r8
	and $0x1C, %r8
	add %rsi, %r8
	shl $16, %r9
	mov (%r8), %r9w

.Lld_partial_2:
	mov %rcx, %r8
	and $0x4, %r8
	jz .Lld_partial_4

	mov %rcx, %r8
	and $0x18, %r8
	add %rsi, %r8
	shl $32, %r9
	mov (%r8), %r8d
	xor %r8, %r9

.Lld_partial_4:
	movq %r9, MSG_LO

	mov %rcx, %r8
	and $0x8, %r8
	jz .Lld_partial_8

	mov %rcx, %r8
	and $0x10, %r8
	add %rsi, %r8
	pslldq $8, MSG_LO
	movq (%r8), T0_LO
	pxor T0_LO, MSG_LO

.Lld_partial_8:
	mov %rcx, %r8
	and $0x10, %r8
	jz .Lld_partial_16

	movdqa MSG_LO, MSG_HI
	movdqu (%rsi), MSG_LO

.Lld_partial_16:
	ret
ENDPROC(__load_partial)

/*
 * __store_partial: internal ABI
 * input:
 *   %rdx - dst
 *   %rcx - bytes
 * output:
 *   T0   - message block
 * changed:
 *   %r8
 *   %r9
 *   %r10
 */
__store_partial:
	mov %rcx, %r8
	mov %rdx, %r9

	cmp $16, %r8
	jl .Lst_partial_16

	movdqu T0_LO, (%r9)
	movdqa T0_HI, T0_LO

	sub $16, %r8
	add $16, %r9

.Lst_partial_16:
	movq T0_LO, %r10

	cmp $8, %r8
	jl .Lst_partial_8

	mov %r10, (%r9)
	psrldq $8, T0_LO
	movq T0_LO, %r10

	sub $8, %r8
	add $8, %r9

.Lst_partial_8:
	cmp $4, %r8
	jl .Lst_partial_4

	mov %r10d, (%r9)
	shr $32, %r10

	sub $4, %r8
	add $4, %r9

.Lst_partial_4:
	cmp $2, %r8
	jl .Lst_partial_2

	mov %r10w, (%r9)
	shr $16, %r10

	sub $2, %r8
	add $2, %r9

.Lst_partial_2:
	cmp $1, %r8
	jl .Lst_partial_1

	mov %r10b, (%r9)

.Lst_partial_1:
	ret
ENDPROC(__store_partial)

/*
 * void crypto_morus1280_sse2_init(void *state, const void *key,
 *                                 const void *iv);
 */
ENTRY(crypto_morus1280_sse2_init)
	FRAME_BEGIN

	/* load IV: */
	pxor STATE0_HI, STATE0_HI
	movdqu (%rdx), STATE0_LO
	/* load key: */
	movdqu  0(%rsi), KEY_LO
	movdqu 16(%rsi), KEY_HI
	movdqa KEY_LO, STATE1_LO
	movdqa KEY_HI, STATE1_HI
	/* load all ones: */
	pcmpeqd STATE2_LO, STATE2_LO
	pcmpeqd STATE2_HI, STATE2_HI
	/* load all zeros: */
	pxor STATE3_LO, STATE3_LO
	pxor STATE3_HI, STATE3_HI
	/* load the constant: */
	movdqa .Lmorus640_const_0, STATE4_LO
	movdqa .Lmorus640_const_1, STATE4_HI

	/* update 16 times with zero: */
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero
	call __morus1280_update_zero

	/* xor-in the key again after updates: */
	pxor KEY_LO, STATE1_LO
	pxor KEY_HI, STATE1_HI

	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_init)

/*
 * void crypto_morus1280_sse2_ad(void *state, const void *data,
 *                               unsigned int length);
 */
ENTRY(crypto_morus1280_sse2_ad)
	FRAME_BEGIN

	cmp $32, %rdx
	jb .Lad_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	mov %rsi, %r8
	and $0xF, %r8
	jnz .Lad_u_loop

.align 4
.Lad_a_loop:
	movdqa  0(%rsi), MSG_LO
	movdqa 16(%rsi), MSG_HI
	call __morus1280_update
	sub $32, %rdx
	add $32, %rsi
	cmp $32, %rdx
	jge .Lad_a_loop

	jmp .Lad_cont
.align 4
.Lad_u_loop:
	movdqu  0(%rsi), MSG_LO
	movdqu 16(%rsi), MSG_HI
	call __morus1280_update
	sub $32, %rdx
	add $32, %rsi
	cmp $32, %rdx
	jge .Lad_u_loop

.Lad_cont:
	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

.Lad_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_ad)

/*
 * void crypto_morus1280_sse2_enc(void *state, const void *src, void *dst,
 *                                unsigned int length);
 */
ENTRY(crypto_morus1280_sse2_enc)
	FRAME_BEGIN

	cmp $32, %rcx
	jb .Lenc_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	mov %rsi, %r8
	or  %rdx, %r8
	and $0xF, %r8
	jnz .Lenc_u_loop

.align 4
.Lenc_a_loop:
	movdqa  0(%rsi), MSG_LO
	movdqa 16(%rsi), MSG_HI
	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	movdqa MSG_LO, T0_LO
	movdqa MSG_HI, T0_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI
	pxor STATE0_LO, T0_LO
	pxor STATE0_HI, T0_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI
	movdqa T0_LO,  0(%rdx)
	movdqa T0_HI, 16(%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Lenc_a_loop

	jmp .Lenc_cont
.align 4
.Lenc_u_loop:
	movdqu  0(%rsi), MSG_LO
	movdqu 16(%rsi), MSG_HI
	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	movdqa MSG_LO, T0_LO
	movdqa MSG_HI, T0_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI
	pxor STATE0_LO, T0_LO
	pxor STATE0_HI, T0_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI
	movdqu T0_LO,  0(%rdx)
	movdqu T0_HI, 16(%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Lenc_u_loop

.Lenc_cont:
	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

.Lenc_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_enc)

/*
 * void crypto_morus1280_sse2_enc_tail(void *state, const void *src, void *dst,
 *                                     unsigned int length);
 */
ENTRY(crypto_morus1280_sse2_enc_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	/* encrypt message: */
	call __load_partial

	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	movdqa MSG_LO, T0_LO
	movdqa MSG_HI, T0_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI
	pxor STATE0_LO, T0_LO
	pxor STATE0_HI, T0_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, T0_LO
	pxor T1_HI, T0_HI

	call __store_partial

	call __morus1280_update

	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_enc_tail)

/*
 * void crypto_morus1280_sse2_dec(void *state, const void *src, void *dst,
 *                                unsigned int length);
 */
ENTRY(crypto_morus1280_sse2_dec)
	FRAME_BEGIN

	cmp $32, %rcx
	jb .Ldec_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	mov %rsi, %r8
	or  %rdx, %r8
	and $0xF, %r8
	jnz .Ldec_u_loop

.align 4
.Ldec_a_loop:
	movdqa  0(%rsi), MSG_LO
	movdqa 16(%rsi), MSG_HI
	pxor STATE0_LO, MSG_LO
	pxor STATE0_HI, MSG_HI
	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqa MSG_LO,  0(%rdx)
	movdqa MSG_HI, 16(%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Ldec_a_loop

	jmp .Ldec_cont
.align 4
.Ldec_u_loop:
	movdqu  0(%rsi), MSG_LO
	movdqu 16(%rsi), MSG_HI
	pxor STATE0_LO, MSG_LO
	pxor STATE0_HI, MSG_HI
	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqu MSG_LO,  0(%rdx)
	movdqu MSG_HI, 16(%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Ldec_u_loop

.Ldec_cont:
	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

.Ldec_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_dec)

/*
 * void crypto_morus1280_sse2_dec_tail(void *state, const void *src, void *dst,
 *                                     unsigned int length);
 */
ENTRY(crypto_morus1280_sse2_dec_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	/* decrypt message: */
	call __load_partial

	pxor STATE0_LO, MSG_LO
	pxor STATE0_HI, MSG_HI
	movdqa STATE1_LO, T1_LO
	movdqa STATE1_HI, T1_HI
	rol3 T1_HI, T1_LO
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqa STATE2_LO, T1_LO
	movdqa STATE2_HI, T1_HI
	pand STATE3_LO, T1_LO
	pand STATE3_HI, T1_HI
	pxor T1_LO, MSG_LO
	pxor T1_HI, MSG_HI
	movdqa MSG_LO, T0_LO
	movdqa MSG_HI, T0_HI

	call __store_partial

	/* mask with byte count: */
	movq %rcx, T0_LO
	punpcklbw T0_LO, T0_LO
	punpcklbw T0_LO, T0_LO
	punpcklbw T0_LO, T0_LO
	punpcklbw T0_LO, T0_LO
	movdqa T0_LO, T0_HI
	movdqa .Lmorus640_counter_0, T1_LO
	movdqa .Lmorus640_counter_1, T1_HI
	pcmpgtb T1_LO, T0_LO
	pcmpgtb T1_HI, T0_HI
	pand T0_LO, MSG_LO
	pand T0_HI, MSG_HI

	call __morus1280_update

	/* store the state: */
	movdqu STATE0_LO, (0 * 16)(%rdi)
	movdqu STATE0_HI, (1 * 16)(%rdi)
	movdqu STATE1_LO, (2 * 16)(%rdi)
	movdqu STATE1_HI, (3 * 16)(%rdi)
	movdqu STATE2_LO, (4 * 16)(%rdi)
	movdqu STATE2_HI, (5 * 16)(%rdi)
	movdqu STATE3_LO, (6 * 16)(%rdi)
	movdqu STATE3_HI, (7 * 16)(%rdi)
	movdqu STATE4_LO, (8 * 16)(%rdi)
	movdqu STATE4_HI, (9 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_dec_tail)

/*
 * void crypto_morus1280_sse2_final(void *state, void *tag_xor,
 *                                  u64 assoclen, u64 cryptlen);
 */
ENTRY(crypto_morus1280_sse2_final)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0_LO
	movdqu (1 * 16)(%rdi), STATE0_HI
	movdqu (2 * 16)(%rdi), STATE1_LO
	movdqu (3 * 16)(%rdi), STATE1_HI
	movdqu (4 * 16)(%rdi), STATE2_LO
	movdqu (5 * 16)(%rdi), STATE2_HI
	movdqu (6 * 16)(%rdi), STATE3_LO
	movdqu (7 * 16)(%rdi), STATE3_HI
	movdqu (8 * 16)(%rdi), STATE4_LO
	movdqu (9 * 16)(%rdi), STATE4_HI

	/* xor state[0] into state[4]: */
	pxor STATE0_LO, STATE4_LO
	pxor STATE0_HI, STATE4_HI

	/* prepare length block: */
	movq %rdx, MSG_LO
	movq %rcx, T0_LO
	pslldq $8, T0_LO
	pxor T0_LO, MSG_LO
	psllq $3, MSG_LO /* multiply by 8 (to get bit count) */
	pxor MSG_HI, MSG_HI

	/* update state: */
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update
	call __morus1280_update

	/* xor tag: */
	movdqu  0(%rsi), MSG_LO
	movdqu 16(%rsi), MSG_HI

	pxor STATE0_LO, MSG_LO
	pxor STATE0_HI, MSG_HI
	movdqa STATE1_LO, T0_LO
	movdqa STATE1_HI, T0_HI
	rol3 T0_HI, T0_LO
	pxor T0_LO, MSG_LO
	pxor T0_HI, MSG_HI
	movdqa STATE2_LO, T0_LO
	movdqa STATE2_HI, T0_HI
	pand STATE3_LO, T0_LO
	pand STATE3_HI, T0_HI
	pxor T0_LO, MSG_LO
	pxor T0_HI, MSG_HI

	movdqu MSG_LO,  0(%rsi)
	movdqu MSG_HI, 16(%rsi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_sse2_final)
