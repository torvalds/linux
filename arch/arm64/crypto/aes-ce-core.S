/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.arch		armv8-a+crypto

ENTRY(__aes_ce_encrypt)
	sub		w3, w3, #2
	ld1		{v0.16b}, [x2]
	ld1		{v1.4s}, [x0], #16
	cmp		w3, #10
	bmi		0f
	bne		3f
	mov		v3.16b, v1.16b
	b		2f
0:	mov		v2.16b, v1.16b
	ld1		{v3.4s}, [x0], #16
1:	aese		v0.16b, v2.16b
	aesmc		v0.16b, v0.16b
2:	ld1		{v1.4s}, [x0], #16
	aese		v0.16b, v3.16b
	aesmc		v0.16b, v0.16b
3:	ld1		{v2.4s}, [x0], #16
	subs		w3, w3, #3
	aese		v0.16b, v1.16b
	aesmc		v0.16b, v0.16b
	ld1		{v3.4s}, [x0], #16
	bpl		1b
	aese		v0.16b, v2.16b
	eor		v0.16b, v0.16b, v3.16b
	st1		{v0.16b}, [x1]
	ret
ENDPROC(__aes_ce_encrypt)

ENTRY(__aes_ce_decrypt)
	sub		w3, w3, #2
	ld1		{v0.16b}, [x2]
	ld1		{v1.4s}, [x0], #16
	cmp		w3, #10
	bmi		0f
	bne		3f
	mov		v3.16b, v1.16b
	b		2f
0:	mov		v2.16b, v1.16b
	ld1		{v3.4s}, [x0], #16
1:	aesd		v0.16b, v2.16b
	aesimc		v0.16b, v0.16b
2:	ld1		{v1.4s}, [x0], #16
	aesd		v0.16b, v3.16b
	aesimc		v0.16b, v0.16b
3:	ld1		{v2.4s}, [x0], #16
	subs		w3, w3, #3
	aesd		v0.16b, v1.16b
	aesimc		v0.16b, v0.16b
	ld1		{v3.4s}, [x0], #16
	bpl		1b
	aesd		v0.16b, v2.16b
	eor		v0.16b, v0.16b, v3.16b
	st1		{v0.16b}, [x1]
	ret
ENDPROC(__aes_ce_decrypt)

/*
 * __aes_ce_sub() - use the aese instruction to perform the AES sbox
 *                  substitution on each byte in 'input'
 */
ENTRY(__aes_ce_sub)
	dup		v1.4s, w0
	movi		v0.16b, #0
	aese		v0.16b, v1.16b
	umov		w0, v0.s[0]
	ret
ENDPROC(__aes_ce_sub)

ENTRY(__aes_ce_invert)
	ld1		{v0.4s}, [x1]
	aesimc		v1.16b, v0.16b
	st1		{v1.4s}, [x0]
	ret
ENDPROC(__aes_ce_invert)
