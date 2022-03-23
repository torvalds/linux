/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES-NI + SSE2 implementation of AEGIS-128
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 */

#include <linux/linkage.h>
#include <asm/frame.h>

#define STATE0	%xmm0
#define STATE1	%xmm1
#define STATE2	%xmm2
#define STATE3	%xmm3
#define STATE4	%xmm4
#define KEY	%xmm5
#define MSG	%xmm5
#define T0	%xmm6
#define T1	%xmm7

#define STATEP	%rdi
#define LEN	%rsi
#define SRC	%rdx
#define DST	%rcx

.section .rodata.cst16.aegis128_const, "aM", @progbits, 32
.align 16
.Laegis128_const_0:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
.Laegis128_const_1:
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst16.aegis128_counter, "aM", @progbits, 16
.align 16
.Laegis128_counter:
	.byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f

.text

/*
 * aegis128_update
 * input:
 *   STATE[0-4] - input state
 * output:
 *   STATE[0-4] - output state (shifted positions)
 * changed:
 *   T0
 */
.macro aegis128_update
	movdqa STATE4, T0
	aesenc STATE0, STATE4
	aesenc STATE1, STATE0
	aesenc STATE2, STATE1
	aesenc STATE3, STATE2
	aesenc T0,     STATE3
.endm

/*
 * __load_partial: internal ABI
 * input:
 *   LEN - bytes
 *   SRC - src
 * output:
 *   MSG  - message block
 * changed:
 *   T0
 *   %r8
 *   %r9
 */
SYM_FUNC_START_LOCAL(__load_partial)
	xor %r9d, %r9d
	pxor MSG, MSG

	mov LEN, %r8
	and $0x1, %r8
	jz .Lld_partial_1

	mov LEN, %r8
	and $0x1E, %r8
	add SRC, %r8
	mov (%r8), %r9b

.Lld_partial_1:
	mov LEN, %r8
	and $0x2, %r8
	jz .Lld_partial_2

	mov LEN, %r8
	and $0x1C, %r8
	add SRC, %r8
	shl $0x10, %r9
	mov (%r8), %r9w

.Lld_partial_2:
	mov LEN, %r8
	and $0x4, %r8
	jz .Lld_partial_4

	mov LEN, %r8
	and $0x18, %r8
	add SRC, %r8
	shl $32, %r9
	mov (%r8), %r8d
	xor %r8, %r9

.Lld_partial_4:
	movq %r9, MSG

	mov LEN, %r8
	and $0x8, %r8
	jz .Lld_partial_8

	mov LEN, %r8
	and $0x10, %r8
	add SRC, %r8
	pslldq $8, MSG
	movq (%r8), T0
	pxor T0, MSG

.Lld_partial_8:
	RET
SYM_FUNC_END(__load_partial)

/*
 * __store_partial: internal ABI
 * input:
 *   LEN - bytes
 *   DST - dst
 * output:
 *   T0   - message block
 * changed:
 *   %r8
 *   %r9
 *   %r10
 */
SYM_FUNC_START_LOCAL(__store_partial)
	mov LEN, %r8
	mov DST, %r9

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
	shr $0x10, %r10

	sub $2, %r8
	add $2, %r9

.Lst_partial_2:
	cmp $1, %r8
	jl .Lst_partial_1

	mov %r10b, (%r9)

.Lst_partial_1:
	RET
SYM_FUNC_END(__store_partial)

/*
 * void crypto_aegis128_aesni_init(void *state, const void *key, const void *iv);
 */
SYM_FUNC_START(crypto_aegis128_aesni_init)
	FRAME_BEGIN

	/* load IV: */
	movdqu (%rdx), T1

	/* load key: */
	movdqa (%rsi), KEY
	pxor KEY, T1
	movdqa T1, STATE0
	movdqa KEY, STATE3
	movdqa KEY, STATE4

	/* load the constants: */
	movdqa .Laegis128_const_0, STATE2
	movdqa .Laegis128_const_1, STATE1
	pxor STATE2, STATE3
	pxor STATE1, STATE4

	/* update 10 times with KEY / KEY xor IV: */
	aegis128_update; pxor KEY, STATE4
	aegis128_update; pxor T1,  STATE3
	aegis128_update; pxor KEY, STATE2
	aegis128_update; pxor T1,  STATE1
	aegis128_update; pxor KEY, STATE0
	aegis128_update; pxor T1,  STATE4
	aegis128_update; pxor KEY, STATE3
	aegis128_update; pxor T1,  STATE2
	aegis128_update; pxor KEY, STATE1
	aegis128_update; pxor T1,  STATE0

	/* store the state: */
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)

	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_init)

/*
 * void crypto_aegis128_aesni_ad(void *state, unsigned int length,
 *                               const void *data);
 */
SYM_FUNC_START(crypto_aegis128_aesni_ad)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Lad_out

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	mov SRC, %r8
	and $0xF, %r8
	jnz .Lad_u_loop

.align 8
.Lad_a_loop:
	movdqa 0x00(SRC), MSG
	aegis128_update
	pxor MSG, STATE4
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_1

	movdqa 0x10(SRC), MSG
	aegis128_update
	pxor MSG, STATE3
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_2

	movdqa 0x20(SRC), MSG
	aegis128_update
	pxor MSG, STATE2
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_3

	movdqa 0x30(SRC), MSG
	aegis128_update
	pxor MSG, STATE1
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_4

	movdqa 0x40(SRC), MSG
	aegis128_update
	pxor MSG, STATE0
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_0

	add $0x50, SRC
	jmp .Lad_a_loop

.align 8
.Lad_u_loop:
	movdqu 0x00(SRC), MSG
	aegis128_update
	pxor MSG, STATE4
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_1

	movdqu 0x10(SRC), MSG
	aegis128_update
	pxor MSG, STATE3
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_2

	movdqu 0x20(SRC), MSG
	aegis128_update
	pxor MSG, STATE2
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_3

	movdqu 0x30(SRC), MSG
	aegis128_update
	pxor MSG, STATE1
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_4

	movdqu 0x40(SRC), MSG
	aegis128_update
	pxor MSG, STATE0
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_0

	add $0x50, SRC
	jmp .Lad_u_loop

	/* store the state: */
.Lad_out_0:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
	FRAME_END
	RET

.Lad_out_1:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	FRAME_END
	RET

.Lad_out_2:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	FRAME_END
	RET

.Lad_out_3:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	FRAME_END
	RET

.Lad_out_4:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
	FRAME_END
	RET

.Lad_out:
	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_ad)

.macro encrypt_block a s0 s1 s2 s3 s4 i
	movdq\a (\i * 0x10)(SRC), MSG
	movdqa MSG, T0
	pxor \s1, T0
	pxor \s4, T0
	movdqa \s2, T1
	pand \s3, T1
	pxor T1, T0
	movdq\a T0, (\i * 0x10)(DST)

	aegis128_update
	pxor MSG, \s4

	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lenc_out_\i
.endm

/*
 * void crypto_aegis128_aesni_enc(void *state, unsigned int length,
 *                                const void *src, void *dst);
 */
SYM_FUNC_START(crypto_aegis128_aesni_enc)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Lenc_out

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	mov  SRC,  %r8
	or   DST,  %r8
	and $0xF, %r8
	jnz .Lenc_u_loop

.align 8
.Lenc_a_loop:
	encrypt_block a STATE0 STATE1 STATE2 STATE3 STATE4 0
	encrypt_block a STATE4 STATE0 STATE1 STATE2 STATE3 1
	encrypt_block a STATE3 STATE4 STATE0 STATE1 STATE2 2
	encrypt_block a STATE2 STATE3 STATE4 STATE0 STATE1 3
	encrypt_block a STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Lenc_a_loop

.align 8
.Lenc_u_loop:
	encrypt_block u STATE0 STATE1 STATE2 STATE3 STATE4 0
	encrypt_block u STATE4 STATE0 STATE1 STATE2 STATE3 1
	encrypt_block u STATE3 STATE4 STATE0 STATE1 STATE2 2
	encrypt_block u STATE2 STATE3 STATE4 STATE0 STATE1 3
	encrypt_block u STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Lenc_u_loop

	/* store the state: */
.Lenc_out_0:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	FRAME_END
	RET

.Lenc_out_1:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	FRAME_END
	RET

.Lenc_out_2:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	FRAME_END
	RET

.Lenc_out_3:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
	FRAME_END
	RET

.Lenc_out_4:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
	FRAME_END
	RET

.Lenc_out:
	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_enc)

/*
 * void crypto_aegis128_aesni_enc_tail(void *state, unsigned int length,
 *                                     const void *src, void *dst);
 */
SYM_FUNC_START(crypto_aegis128_aesni_enc_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* encrypt message: */
	call __load_partial

	movdqa MSG, T0
	pxor STATE1, T0
	pxor STATE4, T0
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, T0

	call __store_partial

	aegis128_update
	pxor MSG, STATE4

	/* store the state: */
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)

	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_enc_tail)

.macro decrypt_block a s0 s1 s2 s3 s4 i
	movdq\a (\i * 0x10)(SRC), MSG
	pxor \s1, MSG
	pxor \s4, MSG
	movdqa \s2, T1
	pand \s3, T1
	pxor T1, MSG
	movdq\a MSG, (\i * 0x10)(DST)

	aegis128_update
	pxor MSG, \s4

	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Ldec_out_\i
.endm

/*
 * void crypto_aegis128_aesni_dec(void *state, unsigned int length,
 *                                const void *src, void *dst);
 */
SYM_FUNC_START(crypto_aegis128_aesni_dec)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Ldec_out

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	mov  SRC, %r8
	or   DST, %r8
	and $0xF, %r8
	jnz .Ldec_u_loop

.align 8
.Ldec_a_loop:
	decrypt_block a STATE0 STATE1 STATE2 STATE3 STATE4 0
	decrypt_block a STATE4 STATE0 STATE1 STATE2 STATE3 1
	decrypt_block a STATE3 STATE4 STATE0 STATE1 STATE2 2
	decrypt_block a STATE2 STATE3 STATE4 STATE0 STATE1 3
	decrypt_block a STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Ldec_a_loop

.align 8
.Ldec_u_loop:
	decrypt_block u STATE0 STATE1 STATE2 STATE3 STATE4 0
	decrypt_block u STATE4 STATE0 STATE1 STATE2 STATE3 1
	decrypt_block u STATE3 STATE4 STATE0 STATE1 STATE2 2
	decrypt_block u STATE2 STATE3 STATE4 STATE0 STATE1 3
	decrypt_block u STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Ldec_u_loop

	/* store the state: */
.Ldec_out_0:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	FRAME_END
	RET

.Ldec_out_1:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	FRAME_END
	RET

.Ldec_out_2:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	FRAME_END
	RET

.Ldec_out_3:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
	FRAME_END
	RET

.Ldec_out_4:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
	FRAME_END
	RET

.Ldec_out:
	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_dec)

/*
 * void crypto_aegis128_aesni_dec_tail(void *state, unsigned int length,
 *                                     const void *src, void *dst);
 */
SYM_FUNC_START(crypto_aegis128_aesni_dec_tail)
	FRAME_BEGIN

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* decrypt message: */
	call __load_partial

	pxor STATE1, MSG
	pxor STATE4, MSG
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, MSG

	movdqa MSG, T0
	call __store_partial

	/* mask with byte count: */
	movq LEN, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	movdqa .Laegis128_counter, T1
	pcmpgtb T1, T0
	pand T0, MSG

	aegis128_update
	pxor MSG, STATE4

	/* store the state: */
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)

	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_dec_tail)

/*
 * void crypto_aegis128_aesni_final(void *state, void *tag_xor,
 *                                  u64 assoclen, u64 cryptlen);
 */
SYM_FUNC_START(crypto_aegis128_aesni_final)
	FRAME_BEGIN

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* prepare length block: */
	movq %rdx, MSG
	movq %rcx, T0
	pslldq $8, T0
	pxor T0, MSG
	psllq $3, MSG /* multiply by 8 (to get bit count) */

	pxor STATE3, MSG

	/* update state: */
	aegis128_update; pxor MSG, STATE4
	aegis128_update; pxor MSG, STATE3
	aegis128_update; pxor MSG, STATE2
	aegis128_update; pxor MSG, STATE1
	aegis128_update; pxor MSG, STATE0
	aegis128_update; pxor MSG, STATE4
	aegis128_update; pxor MSG, STATE3

	/* xor tag: */
	movdqu (%rsi), MSG

	pxor STATE0, MSG
	pxor STATE1, MSG
	pxor STATE2, MSG
	pxor STATE3, MSG
	pxor STATE4, MSG

	movdqu MSG, (%rsi)

	FRAME_END
	RET
SYM_FUNC_END(crypto_aegis128_aesni_final)
