/*
 * AVX2 implementation of MORUS-1280
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

#define STATE0		%ymm0
#define STATE0_LOW	%xmm0
#define STATE1		%ymm1
#define STATE2		%ymm2
#define STATE3		%ymm3
#define STATE4		%ymm4
#define KEY		%ymm5
#define MSG		%ymm5
#define MSG_LOW		%xmm5
#define T0		%ymm6
#define T0_LOW		%xmm6
#define T1		%ymm7

.section .rodata.cst32.morus1280_const, "aM", @progbits, 32
.align 32
.Lmorus1280_const:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst32.morus1280_counter, "aM", @progbits, 32
.align 32
.Lmorus1280_counter:
	.byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
	.byte 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f

.text

.macro morus1280_round s0, s1, s2, s3, s4, b, w
	vpand \s1, \s2, T0
	vpxor T0, \s0, \s0
	vpxor \s3, \s0, \s0
	vpsllq $\b, \s0, T0
	vpsrlq $(64 - \b), \s0, \s0
	vpxor T0, \s0, \s0
	vpermq $\w, \s3, \s3
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
	morus1280_round STATE0, STATE1, STATE2, STATE3, STATE4, 13, MASK1
	vpxor MSG, STATE1, STATE1
	morus1280_round STATE1, STATE2, STATE3, STATE4, STATE0, 46, MASK2
	vpxor MSG, STATE2, STATE2
	morus1280_round STATE2, STATE3, STATE4, STATE0, STATE1, 38, MASK3
	vpxor MSG, STATE3, STATE3
	morus1280_round STATE3, STATE4, STATE0, STATE1, STATE2,  7, MASK2
	vpxor MSG, STATE4, STATE4
	morus1280_round STATE4, STATE0, STATE1, STATE2, STATE3,  4, MASK1
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
	morus1280_round STATE0, STATE1, STATE2, STATE3, STATE4, 13, MASK1
	morus1280_round STATE1, STATE2, STATE3, STATE4, STATE0, 46, MASK2
	morus1280_round STATE2, STATE3, STATE4, STATE0, STATE1, 38, MASK3
	morus1280_round STATE3, STATE4, STATE0, STATE1, STATE2,  7, MASK2
	morus1280_round STATE4, STATE0, STATE1, STATE2, STATE3,  4, MASK1
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
	xor %r9, %r9
	vpxor MSG, MSG, MSG

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
	movq %r9, MSG_LOW

	mov %rcx, %r8
	and $0x8, %r8
	jz .Lld_partial_8

	mov %rcx, %r8
	and $0x10, %r8
	add %rsi, %r8
	pshufd $MASK2, MSG_LOW, MSG_LOW
	pinsrq $0, (%r8), MSG_LOW

.Lld_partial_8:
	mov %rcx, %r8
	and $0x10, %r8
	jz .Lld_partial_16

	vpermq $MASK2, MSG, MSG
	movdqu (%rsi), MSG_LOW

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

	movdqu T0_LOW, (%r9)
	vpermq $MASK2, T0, T0

	sub $16, %r8
	add $16, %r9

.Lst_partial_16:
	movq T0_LOW, %r10

	cmp $8, %r8
	jl .Lst_partial_8

	mov %r10, (%r9)
	pextrq $1, T0_LOW, %r10

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
 * void crypto_morus1280_avx2_init(void *state, const void *key,
 *                                 const void *iv);
 */
ENTRY(crypto_morus1280_avx2_init)
	FRAME_BEGIN

	/* load IV: */
	vpxor STATE0, STATE0, STATE0
	movdqu (%rdx), STATE0_LOW
	/* load key: */
	vmovdqu (%rsi), KEY
	vmovdqa KEY, STATE1
	/* load all ones: */
	vpcmpeqd STATE2, STATE2, STATE2
	/* load all zeros: */
	vpxor STATE3, STATE3, STATE3
	/* load the constant: */
	vmovdqa .Lmorus1280_const, STATE4

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
	vpxor KEY, STATE1, STATE1

	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_init)

/*
 * void crypto_morus1280_avx2_ad(void *state, const void *data,
 *                               unsigned int length);
 */
ENTRY(crypto_morus1280_avx2_ad)
	FRAME_BEGIN

	cmp $32, %rdx
	jb .Lad_out

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	mov %rsi,  %r8
	and $0x1F, %r8
	jnz .Lad_u_loop

.align 4
.Lad_a_loop:
	vmovdqa (%rsi), MSG
	call __morus1280_update
	sub $32, %rdx
	add $32, %rsi
	cmp $32, %rdx
	jge .Lad_a_loop

	jmp .Lad_cont
.align 4
.Lad_u_loop:
	vmovdqu (%rsi), MSG
	call __morus1280_update
	sub $32, %rdx
	add $32, %rsi
	cmp $32, %rdx
	jge .Lad_u_loop

.Lad_cont:
	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

.Lad_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_ad)

/*
 * void crypto_morus1280_avx2_enc(void *state, const void *src, void *dst,
 *                                unsigned int length);
 */
ENTRY(crypto_morus1280_avx2_enc)
	FRAME_BEGIN

	cmp $32, %rcx
	jb .Lenc_out

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	mov %rsi,  %r8
	or  %rdx,  %r8
	and $0x1F, %r8
	jnz .Lenc_u_loop

.align 4
.Lenc_a_loop:
	vmovdqa (%rsi), MSG
	vmovdqa MSG, T0
	vpxor STATE0, T0, T0
	vpermq $MASK3, STATE1, T1
	vpxor T1, T0, T0
	vpand STATE2, STATE3, T1
	vpxor T1, T0, T0
	vmovdqa T0, (%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Lenc_a_loop

	jmp .Lenc_cont
.align 4
.Lenc_u_loop:
	vmovdqu (%rsi), MSG
	vmovdqa MSG, T0
	vpxor STATE0, T0, T0
	vpermq $MASK3, STATE1, T1
	vpxor T1, T0, T0
	vpand STATE2, STATE3, T1
	vpxor T1, T0, T0
	vmovdqu T0, (%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Lenc_u_loop

.Lenc_cont:
	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

.Lenc_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_enc)

/*
 * void crypto_morus1280_avx2_enc_tail(void *state, const void *src, void *dst,
 *                                     unsigned int length);
 */
ENTRY(crypto_morus1280_avx2_enc_tail)
	FRAME_BEGIN

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	/* encrypt message: */
	call __load_partial

	vmovdqa MSG, T0
	vpxor STATE0, T0, T0
	vpermq $MASK3, STATE1, T1
	vpxor T1, T0, T0
	vpand STATE2, STATE3, T1
	vpxor T1, T0, T0

	call __store_partial

	call __morus1280_update

	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

	FRAME_END
ENDPROC(crypto_morus1280_avx2_enc_tail)

/*
 * void crypto_morus1280_avx2_dec(void *state, const void *src, void *dst,
 *                                unsigned int length);
 */
ENTRY(crypto_morus1280_avx2_dec)
	FRAME_BEGIN

	cmp $32, %rcx
	jb .Ldec_out

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	mov %rsi,  %r8
	or  %rdx,  %r8
	and $0x1F, %r8
	jnz .Ldec_u_loop

.align 4
.Ldec_a_loop:
	vmovdqa (%rsi), MSG
	vpxor STATE0, MSG, MSG
	vpermq $MASK3, STATE1, T0
	vpxor T0, MSG, MSG
	vpand STATE2, STATE3, T0
	vpxor T0, MSG, MSG
	vmovdqa MSG, (%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Ldec_a_loop

	jmp .Ldec_cont
.align 4
.Ldec_u_loop:
	vmovdqu (%rsi), MSG
	vpxor STATE0, MSG, MSG
	vpermq $MASK3, STATE1, T0
	vpxor T0, MSG, MSG
	vpand STATE2, STATE3, T0
	vpxor T0, MSG, MSG
	vmovdqu MSG, (%rdx)

	call __morus1280_update
	sub $32, %rcx
	add $32, %rsi
	add $32, %rdx
	cmp $32, %rcx
	jge .Ldec_u_loop

.Ldec_cont:
	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

.Ldec_out:
	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_dec)

/*
 * void crypto_morus1280_avx2_dec_tail(void *state, const void *src, void *dst,
 *                                     unsigned int length);
 */
ENTRY(crypto_morus1280_avx2_dec_tail)
	FRAME_BEGIN

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	/* decrypt message: */
	call __load_partial

	vpxor STATE0, MSG, MSG
	vpermq $MASK3, STATE1, T0
	vpxor T0, MSG, MSG
	vpand STATE2, STATE3, T0
	vpxor T0, MSG, MSG
	vmovdqa MSG, T0

	call __store_partial

	/* mask with byte count: */
	movq %rcx, T0_LOW
	vpbroadcastb T0_LOW, T0
	vmovdqa .Lmorus1280_counter, T1
	vpcmpgtb T1, T0, T0
	vpand T0, MSG, MSG

	call __morus1280_update

	/* store the state: */
	vmovdqu STATE0, (0 * 32)(%rdi)
	vmovdqu STATE1, (1 * 32)(%rdi)
	vmovdqu STATE2, (2 * 32)(%rdi)
	vmovdqu STATE3, (3 * 32)(%rdi)
	vmovdqu STATE4, (4 * 32)(%rdi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_dec_tail)

/*
 * void crypto_morus1280_avx2_final(void *state, void *tag_xor,
 *                                  u64 assoclen, u64 cryptlen);
 */
ENTRY(crypto_morus1280_avx2_final)
	FRAME_BEGIN

	/* load the state: */
	vmovdqu (0 * 32)(%rdi), STATE0
	vmovdqu (1 * 32)(%rdi), STATE1
	vmovdqu (2 * 32)(%rdi), STATE2
	vmovdqu (3 * 32)(%rdi), STATE3
	vmovdqu (4 * 32)(%rdi), STATE4

	/* xor state[0] into state[4]: */
	vpxor STATE0, STATE4, STATE4

	/* prepare length block: */
	vpxor MSG, MSG, MSG
	vpinsrq $0, %rdx, MSG_LOW, MSG_LOW
	vpinsrq $1, %rcx, MSG_LOW, MSG_LOW
	vpsllq $3, MSG, MSG /* multiply by 8 (to get bit count) */

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
	vmovdqu (%rsi), MSG

	vpxor STATE0, MSG, MSG
	vpermq $MASK3, STATE1, T0
	vpxor T0, MSG, MSG
	vpand STATE2, STATE3, T0
	vpxor T0, MSG, MSG
	vmovdqu MSG, (%rsi)

	FRAME_END
	ret
ENDPROC(crypto_morus1280_avx2_final)
