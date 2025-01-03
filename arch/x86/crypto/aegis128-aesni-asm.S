/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AES-NI + SSE4.1 implementation of AEGIS-128
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 * Copyright 2024 Google LLC
 */

#include <linux/linkage.h>

#define STATE0	%xmm0
#define STATE1	%xmm1
#define STATE2	%xmm2
#define STATE3	%xmm3
#define STATE4	%xmm4
#define KEY	%xmm5
#define MSG	%xmm5
#define T0	%xmm6
#define T1	%xmm7

.section .rodata.cst16.aegis128_const, "aM", @progbits, 32
.align 16
.Laegis128_const_0:
	.byte 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d
	.byte 0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62
.Laegis128_const_1:
	.byte 0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1
	.byte 0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd

.section .rodata.cst32.zeropad_mask, "aM", @progbits, 32
.align 32
.Lzeropad_mask:
	.octa 0xffffffffffffffffffffffffffffffff
	.octa 0

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
 * Load 1 <= LEN (%ecx) <= 15 bytes from the pointer SRC into the xmm register
 * MSG and zeroize any remaining bytes.  Clobbers %rax, %rcx, and %r8.
 */
.macro load_partial
	sub $8, %ecx			/* LEN - 8 */
	jle .Lle8\@

	/* Load 9 <= LEN <= 15 bytes: */
	movq (SRC), MSG			/* Load first 8 bytes */
	mov (SRC, %rcx), %rax		/* Load last 8 bytes */
	neg %ecx
	shl $3, %ecx
	shr %cl, %rax			/* Discard overlapping bytes */
	pinsrq $1, %rax, MSG
	jmp .Ldone\@

.Lle8\@:
	add $4, %ecx			/* LEN - 4 */
	jl .Llt4\@

	/* Load 4 <= LEN <= 8 bytes: */
	mov (SRC), %eax			/* Load first 4 bytes */
	mov (SRC, %rcx), %r8d		/* Load last 4 bytes */
	jmp .Lcombine\@

.Llt4\@:
	/* Load 1 <= LEN <= 3 bytes: */
	add $2, %ecx			/* LEN - 2 */
	movzbl (SRC), %eax		/* Load first byte */
	jl .Lmovq\@
	movzwl (SRC, %rcx), %r8d	/* Load last 2 bytes */
.Lcombine\@:
	shl $3, %ecx
	shl %cl, %r8
	or %r8, %rax			/* Combine the two parts */
.Lmovq\@:
	movq %rax, MSG
.Ldone\@:
.endm

/*
 * Store 1 <= LEN (%ecx) <= 15 bytes from the xmm register \msg to the pointer
 * DST.  Clobbers %rax, %rcx, and %r8.
 */
.macro store_partial msg
	sub $8, %ecx			/* LEN - 8 */
	jl .Llt8\@

	/* Store 8 <= LEN <= 15 bytes: */
	pextrq $1, \msg, %rax
	mov %ecx, %r8d
	shl $3, %ecx
	ror %cl, %rax
	mov %rax, (DST, %r8)		/* Store last LEN - 8 bytes */
	movq \msg, (DST)		/* Store first 8 bytes */
	jmp .Ldone\@

.Llt8\@:
	add $4, %ecx			/* LEN - 4 */
	jl .Llt4\@

	/* Store 4 <= LEN <= 7 bytes: */
	pextrd $1, \msg, %eax
	mov %ecx, %r8d
	shl $3, %ecx
	ror %cl, %eax
	mov %eax, (DST, %r8)		/* Store last LEN - 4 bytes */
	movd \msg, (DST)		/* Store first 4 bytes */
	jmp .Ldone\@

.Llt4\@:
	/* Store 1 <= LEN <= 3 bytes: */
	pextrb $0, \msg, 0(DST)
	cmp $-2, %ecx			/* LEN - 4 == -2, i.e. LEN == 2? */
	jl .Ldone\@
	pextrb $1, \msg, 1(DST)
	je .Ldone\@
	pextrb $2, \msg, 2(DST)
.Ldone\@:
.endm

/*
 * void aegis128_aesni_init(struct aegis_state *state,
 *			    const struct aegis_block *key,
 *			    const u8 iv[AEGIS128_NONCE_SIZE]);
 */
SYM_FUNC_START(aegis128_aesni_init)
	.set STATEP, %rdi
	.set KEYP, %rsi
	.set IVP, %rdx

	/* load IV: */
	movdqu (IVP), T1

	/* load key: */
	movdqa (KEYP), KEY
	pxor KEY, T1
	movdqa T1, STATE0
	movdqa KEY, STATE3
	movdqa KEY, STATE4

	/* load the constants: */
	movdqa .Laegis128_const_0(%rip), STATE2
	movdqa .Laegis128_const_1(%rip), STATE1
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
	RET
SYM_FUNC_END(aegis128_aesni_init)

/*
 * void aegis128_aesni_ad(struct aegis_state *state, const u8 *data,
 *			  unsigned int len);
 *
 * len must be a multiple of 16.
 */
SYM_FUNC_START(aegis128_aesni_ad)
	.set STATEP, %rdi
	.set SRC, %rsi
	.set LEN, %edx

	test LEN, LEN
	jz .Lad_out

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

.align 8
.Lad_loop:
	movdqu 0x00(SRC), MSG
	aegis128_update
	pxor MSG, STATE4
	sub $0x10, LEN
	jz .Lad_out_1

	movdqu 0x10(SRC), MSG
	aegis128_update
	pxor MSG, STATE3
	sub $0x10, LEN
	jz .Lad_out_2

	movdqu 0x20(SRC), MSG
	aegis128_update
	pxor MSG, STATE2
	sub $0x10, LEN
	jz .Lad_out_3

	movdqu 0x30(SRC), MSG
	aegis128_update
	pxor MSG, STATE1
	sub $0x10, LEN
	jz .Lad_out_4

	movdqu 0x40(SRC), MSG
	aegis128_update
	pxor MSG, STATE0
	sub $0x10, LEN
	jz .Lad_out_0

	add $0x50, SRC
	jmp .Lad_loop

	/* store the state: */
.Lad_out_0:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
	RET

.Lad_out_1:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	RET

.Lad_out_2:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	RET

.Lad_out_3:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	RET

.Lad_out_4:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
.Lad_out:
	RET
SYM_FUNC_END(aegis128_aesni_ad)

.macro encrypt_block s0 s1 s2 s3 s4 i
	movdqu (\i * 0x10)(SRC), MSG
	movdqa MSG, T0
	pxor \s1, T0
	pxor \s4, T0
	movdqa \s2, T1
	pand \s3, T1
	pxor T1, T0
	movdqu T0, (\i * 0x10)(DST)

	aegis128_update
	pxor MSG, \s4

	sub $0x10, LEN
	jz .Lenc_out_\i
.endm

/*
 * void aegis128_aesni_enc(struct aegis_state *state, const u8 *src, u8 *dst,
 *			   unsigned int len);
 *
 * len must be nonzero and a multiple of 16.
 */
SYM_FUNC_START(aegis128_aesni_enc)
	.set STATEP, %rdi
	.set SRC, %rsi
	.set DST, %rdx
	.set LEN, %ecx

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

.align 8
.Lenc_loop:
	encrypt_block STATE0 STATE1 STATE2 STATE3 STATE4 0
	encrypt_block STATE4 STATE0 STATE1 STATE2 STATE3 1
	encrypt_block STATE3 STATE4 STATE0 STATE1 STATE2 2
	encrypt_block STATE2 STATE3 STATE4 STATE0 STATE1 3
	encrypt_block STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Lenc_loop

	/* store the state: */
.Lenc_out_0:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	RET

.Lenc_out_1:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	RET

.Lenc_out_2:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	RET

.Lenc_out_3:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
	RET

.Lenc_out_4:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
.Lenc_out:
	RET
SYM_FUNC_END(aegis128_aesni_enc)

/*
 * void aegis128_aesni_enc_tail(struct aegis_state *state, const u8 *src,
 *				u8 *dst, unsigned int len);
 */
SYM_FUNC_START(aegis128_aesni_enc_tail)
	.set STATEP, %rdi
	.set SRC, %rsi
	.set DST, %rdx
	.set LEN, %ecx	/* {load,store}_partial rely on this being %ecx */

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* encrypt message: */
	mov LEN, %r9d
	load_partial

	movdqa MSG, T0
	pxor STATE1, T0
	pxor STATE4, T0
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, T0

	mov %r9d, LEN
	store_partial T0

	aegis128_update
	pxor MSG, STATE4

	/* store the state: */
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	RET
SYM_FUNC_END(aegis128_aesni_enc_tail)

.macro decrypt_block s0 s1 s2 s3 s4 i
	movdqu (\i * 0x10)(SRC), MSG
	pxor \s1, MSG
	pxor \s4, MSG
	movdqa \s2, T1
	pand \s3, T1
	pxor T1, MSG
	movdqu MSG, (\i * 0x10)(DST)

	aegis128_update
	pxor MSG, \s4

	sub $0x10, LEN
	jz .Ldec_out_\i
.endm

/*
 * void aegis128_aesni_dec(struct aegis_state *state, const u8 *src, u8 *dst,
 *			   unsigned int len);
 *
 * len must be nonzero and a multiple of 16.
 */
SYM_FUNC_START(aegis128_aesni_dec)
	.set STATEP, %rdi
	.set SRC, %rsi
	.set DST, %rdx
	.set LEN, %ecx

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

.align 8
.Ldec_loop:
	decrypt_block STATE0 STATE1 STATE2 STATE3 STATE4 0
	decrypt_block STATE4 STATE0 STATE1 STATE2 STATE3 1
	decrypt_block STATE3 STATE4 STATE0 STATE1 STATE2 2
	decrypt_block STATE2 STATE3 STATE4 STATE0 STATE1 3
	decrypt_block STATE1 STATE2 STATE3 STATE4 STATE0 4

	add $0x50, SRC
	add $0x50, DST
	jmp .Ldec_loop

	/* store the state: */
.Ldec_out_0:
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	RET

.Ldec_out_1:
	movdqu STATE3, 0x00(STATEP)
	movdqu STATE4, 0x10(STATEP)
	movdqu STATE0, 0x20(STATEP)
	movdqu STATE1, 0x30(STATEP)
	movdqu STATE2, 0x40(STATEP)
	RET

.Ldec_out_2:
	movdqu STATE2, 0x00(STATEP)
	movdqu STATE3, 0x10(STATEP)
	movdqu STATE4, 0x20(STATEP)
	movdqu STATE0, 0x30(STATEP)
	movdqu STATE1, 0x40(STATEP)
	RET

.Ldec_out_3:
	movdqu STATE1, 0x00(STATEP)
	movdqu STATE2, 0x10(STATEP)
	movdqu STATE3, 0x20(STATEP)
	movdqu STATE4, 0x30(STATEP)
	movdqu STATE0, 0x40(STATEP)
	RET

.Ldec_out_4:
	movdqu STATE0, 0x00(STATEP)
	movdqu STATE1, 0x10(STATEP)
	movdqu STATE2, 0x20(STATEP)
	movdqu STATE3, 0x30(STATEP)
	movdqu STATE4, 0x40(STATEP)
.Ldec_out:
	RET
SYM_FUNC_END(aegis128_aesni_dec)

/*
 * void aegis128_aesni_dec_tail(struct aegis_state *state, const u8 *src,
 *				u8 *dst, unsigned int len);
 */
SYM_FUNC_START(aegis128_aesni_dec_tail)
	.set STATEP, %rdi
	.set SRC, %rsi
	.set DST, %rdx
	.set LEN, %ecx	/* {load,store}_partial rely on this being %ecx */

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* decrypt message: */
	mov LEN, %r9d
	load_partial

	pxor STATE1, MSG
	pxor STATE4, MSG
	movdqa STATE2, T1
	pand STATE3, T1
	pxor T1, MSG

	mov %r9d, LEN
	store_partial MSG

	/* mask with byte count: */
	lea .Lzeropad_mask+16(%rip), %rax
	sub %r9, %rax
	movdqu (%rax), T0
	pand T0, MSG

	aegis128_update
	pxor MSG, STATE4

	/* store the state: */
	movdqu STATE4, 0x00(STATEP)
	movdqu STATE0, 0x10(STATEP)
	movdqu STATE1, 0x20(STATEP)
	movdqu STATE2, 0x30(STATEP)
	movdqu STATE3, 0x40(STATEP)
	RET
SYM_FUNC_END(aegis128_aesni_dec_tail)

/*
 * void aegis128_aesni_final(struct aegis_state *state,
 *			     struct aegis_block *tag_xor,
 *			     unsigned int assoclen, unsigned int cryptlen);
 */
SYM_FUNC_START(aegis128_aesni_final)
	.set STATEP, %rdi
	.set TAG_XOR, %rsi
	.set ASSOCLEN, %edx
	.set CRYPTLEN, %ecx

	/* load the state: */
	movdqu 0x00(STATEP), STATE0
	movdqu 0x10(STATEP), STATE1
	movdqu 0x20(STATEP), STATE2
	movdqu 0x30(STATEP), STATE3
	movdqu 0x40(STATEP), STATE4

	/* prepare length block: */
	movd ASSOCLEN, MSG
	pinsrd $2, CRYPTLEN, MSG
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
	movdqu (TAG_XOR), MSG

	pxor STATE0, MSG
	pxor STATE1, MSG
	pxor STATE2, MSG
	pxor STATE3, MSG
	pxor STATE4, MSG

	movdqu MSG, (TAG_XOR)
	RET
SYM_FUNC_END(aegis128_aesni_final)
