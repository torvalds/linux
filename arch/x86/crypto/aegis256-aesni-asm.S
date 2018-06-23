/*
 * AES-NI + SSE2 implementation of AEGIS-128L
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

#define STATE0	%xmm0
#define STATE1	%xmm1
#define STATE2	%xmm2
#define STATE3	%xmm3
#define STATE4	%xmm4
#define STATE5	%xmm5
#define MSG	%xmm6
#define T0	%xmm7
#define T1	%xmm8
#define T2	%xmm9
#define T3	%xmm10

#define STATEP	%rdi
#define LEN	%rsi
#define SRC	%rdx
#define DST	%rcx

.section .rodata.cst16.aegis256_const, "aM", @progbits, 32
.align 16
.Laegis256_const_0:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
.Laegis256_const_1:
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst16.aegis256_counter, "aM", @progbits, 16
.align 16
.Laegis256_counter:
	.byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f

.text

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
__load_partial:
	xor %r9, %r9
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
	ret
ENDPROC(__load_partial)

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
__store_partial:
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
	ret
ENDPROC(__store_partial)

.macro update
	movdqa STATE5, T0
	aesenc STATE0, STATE5
	aesenc STATE1, STATE0
	aesenc STATE2, STATE1
	aesenc STATE3, STATE2
	aesenc STATE4, STATE3
	aesenc T0,     STATE4
.endm

.macro update0 m
	update
	pxor \m, STATE5
.endm

.macro update1 m
	update
	pxor \m, STATE4
.endm

.macro update2 m
	update
	pxor \m, STATE3
.endm

.macro update3 m
	update
	pxor \m, STATE2
.endm

.macro update4 m
	update
	pxor \m, STATE1
.endm

.macro update5 m
	update
	pxor \m, STATE0
.endm

.macro state_load
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4
	movdqu 0x50(STATEP), STATE5
.endm

.macro state_store s0 s1 s2 s3 s4 s5
	movdqu \s5, 0x00(STATEP)
	movdqu \s0, 0x10(STATEP)
	movdqu \s1, 0x20(STATEP)
	movdqu \s2, 0x30(STATEP)
	movdqu \s3, 0x40(STATEP)
	movdqu \s4, 0x50(STATEP)
.endm

.macro state_store0
	state_store STATE0 STATE1 STATE2 STATE3 STATE4 STATE5
.endm

.macro state_store1
	state_store STATE5 STATE0 STATE1 STATE2 STATE3 STATE4
.endm

.macro state_store2
	state_store STATE4 STATE5 STATE0 STATE1 STATE2 STATE3
.endm

.macro state_store3
	state_store STATE3 STATE4 STATE5 STATE0 STATE1 STATE2
.endm

.macro state_store4
	state_store STATE2 STATE3 STATE4 STATE5 STATE0 STATE1
.endm

.macro state_store5
	state_store STATE1 STATE2 STATE3 STATE4 STATE5 STATE0
.endm

/*
 * void crypto_aegis256_aesni_init(void *state, const void *key, const void *iv);
 */
ENTRY(crypto_aegis256_aesni_init)
	FRAME_BEGIN

	/* load key: */
	movdqa 0x00(%rsi), MSG
	movdqa 0x10(%rsi), T1
	movdqa MSG, STATE4
	movdqa T1, STATE5

	/* load IV: */
	movdqu 0x00(%rdx), T2
	movdqu 0x10(%rdx), T3
	pxor MSG, T2
	pxor T1, T3
	movdqa T2, STATE0
	movdqa T3, STATE1

	/* load the constants: */
	movdqa .Laegis256_const_0, STATE3
	movdqa .Laegis256_const_1, STATE2
	pxor STATE3, STATE4
	pxor STATE2, STATE5

	/* update 10 times with IV and KEY: */
	update0 MSG
	update1 T1
	update2 T2
	update3 T3
	update4 MSG
	update5 T1
	update0 T2
	update1 T3
	update2 MSG
	update3 T1
	update4 T2
	update5 T3
	update0 MSG
	update1 T1
	update2 T2
	update3 T3

	state_store3

	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_init)

.macro ad_block a i
	movdq\a (\i * 0x10)(SRC), MSG
	update\i MSG
	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lad_out_\i
.endm

/*
 * void crypto_aegis256_aesni_ad(void *state, unsigned int length,
 *                               const void *data);
 */
ENTRY(crypto_aegis256_aesni_ad)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Lad_out

	state_load

	mov  SRC, %r8
	and $0xf, %r8
	jnz .Lad_u_loop

.align 8
.Lad_a_loop:
	ad_block a 0
	ad_block a 1
	ad_block a 2
	ad_block a 3
	ad_block a 4
	ad_block a 5

	add $0x60, SRC
	jmp .Lad_a_loop

.align 8
.Lad_u_loop:
	ad_block u 0
	ad_block u 1
	ad_block u 2
	ad_block u 3
	ad_block u 4
	ad_block u 5

	add $0x60, SRC
	jmp .Lad_u_loop

.Lad_out_0:
	state_store0
	FRAME_END
	ret

.Lad_out_1:
	state_store1
	FRAME_END
	ret

.Lad_out_2:
	state_store2
	FRAME_END
	ret

.Lad_out_3:
	state_store3
	FRAME_END
	ret

.Lad_out_4:
	state_store4
	FRAME_END
	ret

.Lad_out_5:
	state_store5
	FRAME_END
	ret

.Lad_out:
	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_ad)

.macro crypt m s0 s1 s2 s3 s4 s5
	pxor \s1, \m
	pxor \s4, \m
	pxor \s5, \m
	movdqa \s2, T3
	pand \s3, T3
	pxor T3, \m
.endm

.macro crypt0 m
	crypt \m STATE0 STATE1 STATE2 STATE3 STATE4 STATE5
.endm

.macro crypt1 m
	crypt \m STATE5 STATE0 STATE1 STATE2 STATE3 STATE4
.endm

.macro crypt2 m
	crypt \m STATE4 STATE5 STATE0 STATE1 STATE2 STATE3
.endm

.macro crypt3 m
	crypt \m STATE3 STATE4 STATE5 STATE0 STATE1 STATE2
.endm

.macro crypt4 m
	crypt \m STATE2 STATE3 STATE4 STATE5 STATE0 STATE1
.endm

.macro crypt5 m
	crypt \m STATE1 STATE2 STATE3 STATE4 STATE5 STATE0
.endm

.macro encrypt_block a i
	movdq\a (\i * 0x10)(SRC), MSG
	movdqa MSG, T0
	crypt\i T0
	movdq\a T0, (\i * 0x10)(DST)

	update\i MSG

	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Lenc_out_\i
.endm

.macro decrypt_block a i
	movdq\a (\i * 0x10)(SRC), MSG
	crypt\i MSG
	movdq\a MSG, (\i * 0x10)(DST)

	update\i MSG

	sub $0x10, LEN
	cmp $0x10, LEN
	jl .Ldec_out_\i
.endm

/*
 * void crypto_aegis256_aesni_enc(void *state, unsigned int length,
 *                                const void *src, void *dst);
 */
ENTRY(crypto_aegis256_aesni_enc)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Lenc_out

	state_load

	mov  SRC, %r8
	or   DST, %r8
	and $0xf, %r8
	jnz .Lenc_u_loop

.align 8
.Lenc_a_loop:
	encrypt_block a 0
	encrypt_block a 1
	encrypt_block a 2
	encrypt_block a 3
	encrypt_block a 4
	encrypt_block a 5

	add $0x60, SRC
	add $0x60, DST
	jmp .Lenc_a_loop

.align 8
.Lenc_u_loop:
	encrypt_block u 0
	encrypt_block u 1
	encrypt_block u 2
	encrypt_block u 3
	encrypt_block u 4
	encrypt_block u 5

	add $0x60, SRC
	add $0x60, DST
	jmp .Lenc_u_loop

.Lenc_out_0:
	state_store0
	FRAME_END
	ret

.Lenc_out_1:
	state_store1
	FRAME_END
	ret

.Lenc_out_2:
	state_store2
	FRAME_END
	ret

.Lenc_out_3:
	state_store3
	FRAME_END
	ret

.Lenc_out_4:
	state_store4
	FRAME_END
	ret

.Lenc_out_5:
	state_store5
	FRAME_END
	ret

.Lenc_out:
	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_enc)

/*
 * void crypto_aegis256_aesni_enc_tail(void *state, unsigned int length,
 *                                     const void *src, void *dst);
 */
ENTRY(crypto_aegis256_aesni_enc_tail)
	FRAME_BEGIN

	state_load

	/* encrypt message: */
	call __load_partial

	movdqa MSG, T0
	crypt0 T0

	call __store_partial

	update0 MSG

	state_store0

	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_enc_tail)

/*
 * void crypto_aegis256_aesni_dec(void *state, unsigned int length,
 *                                const void *src, void *dst);
 */
ENTRY(crypto_aegis256_aesni_dec)
	FRAME_BEGIN

	cmp $0x10, LEN
	jb .Ldec_out

	state_load

	mov  SRC, %r8
	or   DST, %r8
	and $0xF, %r8
	jnz .Ldec_u_loop

.align 8
.Ldec_a_loop:
	decrypt_block a 0
	decrypt_block a 1
	decrypt_block a 2
	decrypt_block a 3
	decrypt_block a 4
	decrypt_block a 5

	add $0x60, SRC
	add $0x60, DST
	jmp .Ldec_a_loop

.align 8
.Ldec_u_loop:
	decrypt_block u 0
	decrypt_block u 1
	decrypt_block u 2
	decrypt_block u 3
	decrypt_block u 4
	decrypt_block u 5

	add $0x60, SRC
	add $0x60, DST
	jmp .Ldec_u_loop

.Ldec_out_0:
	state_store0
	FRAME_END
	ret

.Ldec_out_1:
	state_store1
	FRAME_END
	ret

.Ldec_out_2:
	state_store2
	FRAME_END
	ret

.Ldec_out_3:
	state_store3
	FRAME_END
	ret

.Ldec_out_4:
	state_store4
	FRAME_END
	ret

.Ldec_out_5:
	state_store5
	FRAME_END
	ret

.Ldec_out:
	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_dec)

/*
 * void crypto_aegis256_aesni_dec_tail(void *state, unsigned int length,
 *                                     const void *src, void *dst);
 */
ENTRY(crypto_aegis256_aesni_dec_tail)
	FRAME_BEGIN

	state_load

	/* decrypt message: */
	call __load_partial

	crypt0 MSG

	movdqa MSG, T0
	call __store_partial

	/* mask with byte count: */
	movq LEN, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	punpcklbw T0, T0
	movdqa .Laegis256_counter, T1
	pcmpgtb T1, T0
	pand T0, MSG

	update0 MSG

	state_store0

	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_dec_tail)

/*
 * void crypto_aegis256_aesni_final(void *state, void *tag_xor,
 *                                  u64 assoclen, u64 cryptlen);
 */
ENTRY(crypto_aegis256_aesni_final)
	FRAME_BEGIN

	state_load

	/* prepare length block: */
	movq %rdx, MSG
	movq %rcx, T0
	pslldq $8, T0
	pxor T0, MSG
	psllq $3, MSG /* multiply by 8 (to get bit count) */

	pxor STATE3, MSG

	/* update state: */
	update0 MSG
	update1 MSG
	update2 MSG
	update3 MSG
	update4 MSG
	update5 MSG
	update0 MSG

	/* xor tag: */
	movdqu (%rsi), MSG

	pxor STATE0, MSG
	pxor STATE1, MSG
	pxor STATE2, MSG
	pxor STATE3, MSG
	pxor STATE4, MSG
	pxor STATE5, MSG

	movdqu MSG, (%rsi)

	FRAME_END
	ret
ENDPROC(crypto_aegis256_aesni_final)
