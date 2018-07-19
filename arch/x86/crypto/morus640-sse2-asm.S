/*
 * SSE2 implementation of MORUS-640
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/frame.h>

#define SHUFFLE_MASK(i0, i1, i2, i3) \
	(i0 | (i1 << 2) | (i2 << 4) | (i3 << 6))

#define MASK1 SHUFFLE_MASK(3, 0, 1, 2)
#define MASK2 SHUFFLE_MASK(2, 3, 0, 1)
#define MASK3 SHUFFLE_MASK(1, 2, 3, 0)

#define STATE0	%xmm0
#define STATE1	%xmm1
#define STATE2	%xmm2
#define STATE3	%xmm3
#define STATE4	%xmm4
#define KEY	%xmm5
#define MSG	%xmm5
#define T0	%xmm6
#define T1	%xmm7

.section .rodata.cst16.morus640_const, "aM", @progbits, 32
.align 16
.Lmorus640_const_0:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
.Lmorus640_const_1:
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst16.morus640_counter, "aM", @progbits, 16
.align 16
.Lmorus640_counter:
	.byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f

.text

.macro morus640_round s0, s1, s2, s3, s4, b, w
	movdqa \s1, T0
	pand \s2, T0
	pxor T0, \s0
	pxor \s3, \s0
	movdqa \s0, T0
	pslld $\b, T0
	psrld $(32 - \b), \s0
	pxor T0, \s0
	pshufd $\w, \s3, \s3
.endm

/*
 * __morus640_update: internal ABI
 * input:
 *   STATE[0-4] - input state
 *   MSG        - message block
 * output:
 *   STATE[0-4] - output state
 * changed:
 *   T0
 */
__morus640_update:
	morus640_round STATE0, STATE1, STATE2, STATE3, STATE4,  5, MASK1
	pxor MSG, STATE1
	morus640_round STATE1, STATE2, STATE3, STATE4, STATE0, 31, MASK2
	pxor MSG, STATE2
	morus640_round STATE2, STATE3, STATE4, STATE0, STATE1,  7, MASK3
	pxor MSG, STATE3
	morus640_round STATE3, STATE4, STATE0, STATE1, STATE2, 22, MASK2
	pxor MSG, STATE4
	morus640_round STATE4, STATE0, STATE1, STATE2, STATE3, 13, MASK1
	ret
ENDPROC(__morus640_update)


/*
 * __morus640_update_zero: internal ABI
 * input:
 *   STATE[0-4] - input state
 * output:
 *   STATE[0-4] - output state
 * changed:
 *   T0
 */
__morus640_update_zero:
	morus640_round STATE0, STATE1, STATE2, STATE3, STATE4,  5, MASK1
	morus640_round STATE1, STATE2, STATE3, STATE4, STATE0, 31, MASK2
	morus640_round STATE2, STATE3, STATE4, STATE0, STATE1,  7, MASK3
	morus640_round STATE3, STATE4, STATE0, STATE1, STATE2, 22, MASK2
	morus640_round STATE4, STATE0, STATE1, STATE2, STATE3, 13, MASK1
	ret
ENDPROC(__morus640_update_zero)

/*
 * __load_partial: internal ABI
 * input:
 *   %rsi - src
 *   %rcx - bytes
 * output:
 *   MSG  - message block
 * changed:
 *   T0
 *   %r8
 *   %r9
 */
__load_partial:
	xor %r9, %r9
	pxor MSG, MSG

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
	movq %r9, MSG

	mov %rcx, %r8
	and $0x8, %r8
	jz .Lld_partial_8

	mov %rcx, %r8
	and $0x10, %r8
	add %rsi, %r8
	pslldq $8, MSG
	movq (%r8), T0
	pxor T0, MSG

.Lld_partial_8:
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

	movq T0, %r10

	cmp $8, %r8
	jl .Lst_partial_8

	mov %r10, (%r9)
	psrldq $8, T0
	movq T0, %r10

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
 * void crypto_morus640_sse2_init(void *state, const void *key, const void *iv);
 */
ENTRY(crypto_morus640_sse2_init)
	FRAME_BEGIN

	/* load IV: */
	movdqu (%rdx), STATE0
	/* load key: */
	movdqu (%rsi), KEY
	movdqa KEY, STATE1
	/* load all ones: */
	pcmpeqd STATE2, STATE2
	/* load the constants: */
	movdqa .Lmorus640_const_0, STATE3
	movdqa .Lmorus640_const_1, STATE4

	/* update 16 times with zero: */
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero
	call __morus640_update_zero

	/* xor-in the key again after updates: */
	pxor KEY, STATE1

	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_init)

/*
 * void crypto_morus640_sse2_ad(void *state, const void *data,
 *                              unsigned int length);
 */
ENTRY(crypto_morus640_sse2_ad)
	FRAME_BEGIN

	cmp $16, %rdx
	jb .Lad_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	mov %rsi, %r8
	and $0xF, %r8
	jnz .Lad_u_loop

.align 4
.Lad_a_loop:
	movdqa (%rsi), MSG
	call __morus640_update
	sub $16, %rdx
	add $16, %rsi
	cmp $16, %rdx
	jge .Lad_a_loop

	jmp .Lad_cont
.align 4
.Lad_u_loop:
	movdqu (%rsi), MSG
	call __morus640_update
	sub $16, %rdx
	add $16, %rsi
	cmp $16, %rdx
	jge .Lad_u_loop

.Lad_cont:
	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

.Lad_out:
	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_ad)

/*
 * void crypto_morus640_sse2_enc(void *state, const void *src, void *dst,
 *                               unsigned int length);
 */
ENTRY(crypto_morus640_sse2_enc)
	FRAME_BEGIN

	cmp $16, %rcx
	jb .Lenc_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	mov %rsi, %r8
	or  %rdx, %r8
	and $0xF, %r8
	jnz .Lenc_u_loop

.align 4
.Lenc_a_loop:
	movdqa (%rsi), MSG
	movdqa MSG, T0
	pxor STATE0, T0
	pshufd $MASK3, STATE1, T1
	pxor T1, T0
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, T0
	movdqa T0, (%rdx)

	call __morus640_update
	sub $16, %rcx
	add $16, %rsi
	add $16, %rdx
	cmp $16, %rcx
	jge .Lenc_a_loop

	jmp .Lenc_cont
.align 4
.Lenc_u_loop:
	movdqu (%rsi), MSG
	movdqa MSG, T0
	pxor STATE0, T0
	pshufd $MASK3, STATE1, T1
	pxor T1, T0
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, T0
	movdqu T0, (%rdx)

	call __morus640_update
	sub $16, %rcx
	add $16, %rsi
	add $16, %rdx
	cmp $16, %rcx
	jge .Lenc_u_loop

.Lenc_cont:
	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

.Lenc_out:
	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_enc)

/*
 * void crypto_morus640_sse2_enc_tail(void *state, const void *src, void *dst,
 *                                    unsigned int length);
 */
ENTRY(crypto_morus640_sse2_enc_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	/* encrypt message: */
	call __load_partial

	movdqa MSG, T0
	pxor STATE0, T0
	pshufd $MASK3, STATE1, T1
	pxor T1, T0
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, T0

	call __store_partial

	call __morus640_update

	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_enc_tail)

/*
 * void crypto_morus640_sse2_dec(void *state, const void *src, void *dst,
 *                               unsigned int length);
 */
ENTRY(crypto_morus640_sse2_dec)
	FRAME_BEGIN

	cmp $16, %rcx
	jb .Ldec_out

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	mov %rsi, %r8
	or  %rdx, %r8
	and $0xF, %r8
	jnz .Ldec_u_loop

.align 4
.Ldec_a_loop:
	movdqa (%rsi), MSG
	pxor STATE0, MSG
	pshufd $MASK3, STATE1, T0
	pxor T0, MSG
	movdqa STATE2, T0
	pand STATE3, T0
	pxor T0, MSG
	movdqa MSG, (%rdx)

	call __morus640_update
	sub $16, %rcx
	add $16, %rsi
	add $16, %rdx
	cmp $16, %rcx
	jge .Ldec_a_loop

	jmp .Ldec_cont
.align 4
.Ldec_u_loop:
	movdqu (%rsi), MSG
	pxor STATE0, MSG
	pshufd $MASK3, STATE1, T0
	pxor T0, MSG
	movdqa STATE2, T0
	pand STATE3, T0
	pxor T0, MSG
	movdqu MSG, (%rdx)

	call __morus640_update
	sub $16, %rcx
	add $16, %rsi
	add $16, %rdx
	cmp $16, %rcx
	jge .Ldec_u_loop

.Ldec_cont:
	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

.Ldec_out:
	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_dec)

/*
 * void crypto_morus640_sse2_dec_tail(void *state, const void *src, void *dst,
 *                                    unsigned int length);
 */
ENTRY(crypto_morus640_sse2_dec_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	/* decrypt message: */
	call __load_partial

	pxor STATE0, MSG
	pshufd $MASK3, STATE1, T0
	pxor T0, MSG
	movdqa STATE2, T0
	pand STATE3, T0
	pxor T0, MSG
	movdqa MSG, T0

	call __store_partial

	/* mask with byte count: */
	movq %rcx, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	movdqa .Lmorus640_counter, T1
	pcmpgtb T1, T0
	pand T0, MSG

	call __morus640_update

	/* store the state: */
	movdqu STATE0, (0 * 16)(%rdi)
	movdqu STATE1, (1 * 16)(%rdi)
	movdqu STATE2, (2 * 16)(%rdi)
	movdqu STATE3, (3 * 16)(%rdi)
	movdqu STATE4, (4 * 16)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_dec_tail)

/*
 * void crypto_morus640_sse2_final(void *state, void *tag_xor,
 *	                           u64 assoclen, u64 cryptlen);
 */
ENTRY(crypto_morus640_sse2_final)
	FRAME_BEGIN

	/* load the state: */
	movdqu (0 * 16)(%rdi), STATE0
	movdqu (1 * 16)(%rdi), STATE1
	movdqu (2 * 16)(%rdi), STATE2
	movdqu (3 * 16)(%rdi), STATE3
	movdqu (4 * 16)(%rdi), STATE4

	/* xor state[0] into state[4]: */
	pxor STATE0, STATE4

	/* prepare length block: */
	movq %rdx, MSG
	movq %rcx, T0
	pslldq $8, T0
	pxor T0, MSG
	psllq $3, MSG /* multiply by 8 (to get bit count) */

	/* update state: */
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update
	call __morus640_update

	/* xor tag: */
	movdqu (%rsi), MSG

	pxor STATE0, MSG
	pshufd $MASK3, STATE1, T0
	pxor T0, MSG
	movdqa STATE2, T0
	pand STATE3, T0
	pxor T0, MSG

	movdqu MSG, (%rsi)

	FRAME_END
	ret
ENDPROC(crypto_morus640_sse2_final)
