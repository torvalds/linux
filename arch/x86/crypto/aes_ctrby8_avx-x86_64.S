/*
 *	Implement AES CTR mode by8 optimization with AVX instructions. (x86_64)
 *
 * This is AES128/192/256 CTR mode optimization implementation. It requires
 * the support of Intel(R) AESNI and AVX instructions.
 *
 * This work was inspired by the AES CTR mode optimization published
 * in Intel Optimized IPSEC Cryptograhpic library.
 * Additional information on it can be found at:
 *    http://downloadcenter.intel.com/Detail_Desc.aspx?agr=Y&DwnldID=22972
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Contact Information:
 * James Guilford <james.guilford@intel.com>
 * Sean Gulley <sean.m.gulley@intel.com>
 * Chandramouli Narayanan <mouli@linux.intel.com>
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * Neither the name of Intel Corporation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/linkage.h>
#include <asm/inst.h>

#define VMOVDQ		vmovdqu

#define xdata0		%xmm0
#define xdata1		%xmm1
#define xdata2		%xmm2
#define xdata3		%xmm3
#define xdata4		%xmm4
#define xdata5		%xmm5
#define xdata6		%xmm6
#define xdata7		%xmm7
#define xcounter	%xmm8
#define xbyteswap	%xmm9
#define xkey0		%xmm10
#define xkey4		%xmm11
#define xkey8		%xmm12
#define xkey12		%xmm13
#define xkeyA		%xmm14
#define xkeyB		%xmm15

#define p_in		%rdi
#define p_iv		%rsi
#define p_keys		%rdx
#define p_out		%rcx
#define num_bytes	%r8

#define tmp		%r10
#define	DDQ_DATA	0
#define	XDATA		1
#define KEY_128		1
#define KEY_192		2
#define KEY_256		3

.section .rodata
.align 16

byteswap_const:
	.octa 0x000102030405060708090A0B0C0D0E0F
ddq_low_msk:
	.octa 0x0000000000000000FFFFFFFFFFFFFFFF
ddq_high_add_1:
	.octa 0x00000000000000010000000000000000
ddq_add_1:
	.octa 0x00000000000000000000000000000001
ddq_add_2:
	.octa 0x00000000000000000000000000000002
ddq_add_3:
	.octa 0x00000000000000000000000000000003
ddq_add_4:
	.octa 0x00000000000000000000000000000004
ddq_add_5:
	.octa 0x00000000000000000000000000000005
ddq_add_6:
	.octa 0x00000000000000000000000000000006
ddq_add_7:
	.octa 0x00000000000000000000000000000007
ddq_add_8:
	.octa 0x00000000000000000000000000000008

.text

/* generate a unique variable for ddq_add_x */

.macro setddq n
	var_ddq_add = ddq_add_\n
.endm

/* generate a unique variable for xmm register */
.macro setxdata n
	var_xdata = %xmm\n
.endm

/* club the numeric 'id' to the symbol 'name' */

.macro club name, id
.altmacro
	.if \name == DDQ_DATA
		setddq %\id
	.elseif \name == XDATA
		setxdata %\id
	.endif
.noaltmacro
.endm

/*
 * do_aes num_in_par load_keys key_len
 * This increments p_in, but not p_out
 */
.macro do_aes b, k, key_len
	.set by, \b
	.set load_keys, \k
	.set klen, \key_len

	.if (load_keys)
		vmovdqa	0*16(p_keys), xkey0
	.endif

	vpshufb	xbyteswap, xcounter, xdata0

	.set i, 1
	.rept (by - 1)
		club DDQ_DATA, i
		club XDATA, i
		vpaddq	var_ddq_add(%rip), xcounter, var_xdata
		vptest	ddq_low_msk(%rip), var_xdata
		jnz 1f
		vpaddq	ddq_high_add_1(%rip), var_xdata, var_xdata
		vpaddq	ddq_high_add_1(%rip), xcounter, xcounter
		1:
		vpshufb	xbyteswap, var_xdata, var_xdata
		.set i, (i +1)
	.endr

	vmovdqa	1*16(p_keys), xkeyA

	vpxor	xkey0, xdata0, xdata0
	club DDQ_DATA, by
	vpaddq	var_ddq_add(%rip), xcounter, xcounter
	vptest	ddq_low_msk(%rip), xcounter
	jnz	1f
	vpaddq	ddq_high_add_1(%rip), xcounter, xcounter
	1:

	.set i, 1
	.rept (by - 1)
		club XDATA, i
		vpxor	xkey0, var_xdata, var_xdata
		.set i, (i +1)
	.endr

	vmovdqa	2*16(p_keys), xkeyB

	.set i, 0
	.rept by
		club XDATA, i
		vaesenc	xkeyA, var_xdata, var_xdata		/* key 1 */
		.set i, (i +1)
	.endr

	.if (klen == KEY_128)
		.if (load_keys)
			vmovdqa	3*16(p_keys), xkey4
		.endif
	.else
		vmovdqa	3*16(p_keys), xkeyA
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		vaesenc	xkeyB, var_xdata, var_xdata		/* key 2 */
		.set i, (i +1)
	.endr

	add	$(16*by), p_in

	.if (klen == KEY_128)
		vmovdqa	4*16(p_keys), xkeyB
	.else
		.if (load_keys)
			vmovdqa	4*16(p_keys), xkey4
		.endif
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		/* key 3 */
		.if (klen == KEY_128)
			vaesenc	xkey4, var_xdata, var_xdata
		.else
			vaesenc	xkeyA, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	vmovdqa	5*16(p_keys), xkeyA

	.set i, 0
	.rept by
		club XDATA, i
		/* key 4 */
		.if (klen == KEY_128)
			vaesenc	xkeyB, var_xdata, var_xdata
		.else
			vaesenc	xkey4, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	.if (klen == KEY_128)
		.if (load_keys)
			vmovdqa	6*16(p_keys), xkey8
		.endif
	.else
		vmovdqa	6*16(p_keys), xkeyB
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		vaesenc	xkeyA, var_xdata, var_xdata		/* key 5 */
		.set i, (i +1)
	.endr

	vmovdqa	7*16(p_keys), xkeyA

	.set i, 0
	.rept by
		club XDATA, i
		/* key 6 */
		.if (klen == KEY_128)
			vaesenc	xkey8, var_xdata, var_xdata
		.else
			vaesenc	xkeyB, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	.if (klen == KEY_128)
		vmovdqa	8*16(p_keys), xkeyB
	.else
		.if (load_keys)
			vmovdqa	8*16(p_keys), xkey8
		.endif
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		vaesenc	xkeyA, var_xdata, var_xdata		/* key 7 */
		.set i, (i +1)
	.endr

	.if (klen == KEY_128)
		.if (load_keys)
			vmovdqa	9*16(p_keys), xkey12
		.endif
	.else
		vmovdqa	9*16(p_keys), xkeyA
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		/* key 8 */
		.if (klen == KEY_128)
			vaesenc	xkeyB, var_xdata, var_xdata
		.else
			vaesenc	xkey8, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	vmovdqa	10*16(p_keys), xkeyB

	.set i, 0
	.rept by
		club XDATA, i
		/* key 9 */
		.if (klen == KEY_128)
			vaesenc	xkey12, var_xdata, var_xdata
		.else
			vaesenc	xkeyA, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	.if (klen != KEY_128)
		vmovdqa	11*16(p_keys), xkeyA
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		/* key 10 */
		.if (klen == KEY_128)
			vaesenclast	xkeyB, var_xdata, var_xdata
		.else
			vaesenc	xkeyB, var_xdata, var_xdata
		.endif
		.set i, (i +1)
	.endr

	.if (klen != KEY_128)
		.if (load_keys)
			vmovdqa	12*16(p_keys), xkey12
		.endif

		.set i, 0
		.rept by
			club XDATA, i
			vaesenc	xkeyA, var_xdata, var_xdata	/* key 11 */
			.set i, (i +1)
		.endr

		.if (klen == KEY_256)
			vmovdqa	13*16(p_keys), xkeyA
		.endif

		.set i, 0
		.rept by
			club XDATA, i
			.if (klen == KEY_256)
				/* key 12 */
				vaesenc	xkey12, var_xdata, var_xdata
			.else
				vaesenclast xkey12, var_xdata, var_xdata
			.endif
			.set i, (i +1)
		.endr

		.if (klen == KEY_256)
			vmovdqa	14*16(p_keys), xkeyB

			.set i, 0
			.rept by
				club XDATA, i
				/* key 13 */
				vaesenc	xkeyA, var_xdata, var_xdata
				.set i, (i +1)
			.endr

			.set i, 0
			.rept by
				club XDATA, i
				/* key 14 */
				vaesenclast	xkeyB, var_xdata, var_xdata
				.set i, (i +1)
			.endr
		.endif
	.endif

	.set i, 0
	.rept (by / 2)
		.set j, (i+1)
		VMOVDQ	(i*16 - 16*by)(p_in), xkeyA
		VMOVDQ	(j*16 - 16*by)(p_in), xkeyB
		club XDATA, i
		vpxor	xkeyA, var_xdata, var_xdata
		club XDATA, j
		vpxor	xkeyB, var_xdata, var_xdata
		.set i, (i+2)
	.endr

	.if (i < by)
		VMOVDQ	(i*16 - 16*by)(p_in), xkeyA
		club XDATA, i
		vpxor	xkeyA, var_xdata, var_xdata
	.endif

	.set i, 0
	.rept by
		club XDATA, i
		VMOVDQ	var_xdata, i*16(p_out)
		.set i, (i+1)
	.endr
.endm

.macro do_aes_load val, key_len
	do_aes \val, 1, \key_len
.endm

.macro do_aes_noload val, key_len
	do_aes \val, 0, \key_len
.endm

/* main body of aes ctr load */

.macro do_aes_ctrmain key_len
	cmp	$16, num_bytes
	jb	.Ldo_return2\key_len

	vmovdqa	byteswap_const(%rip), xbyteswap
	vmovdqu	(p_iv), xcounter
	vpshufb	xbyteswap, xcounter, xcounter

	mov	num_bytes, tmp
	and	$(7*16), tmp
	jz	.Lmult_of_8_blks\key_len

	/* 1 <= tmp <= 7 */
	cmp	$(4*16), tmp
	jg	.Lgt4\key_len
	je	.Leq4\key_len

.Llt4\key_len:
	cmp	$(2*16), tmp
	jg	.Leq3\key_len
	je	.Leq2\key_len

.Leq1\key_len:
	do_aes_load	1, \key_len
	add	$(1*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Leq2\key_len:
	do_aes_load	2, \key_len
	add	$(2*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len


.Leq3\key_len:
	do_aes_load	3, \key_len
	add	$(3*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Leq4\key_len:
	do_aes_load	4, \key_len
	add	$(4*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Lgt4\key_len:
	cmp	$(6*16), tmp
	jg	.Leq7\key_len
	je	.Leq6\key_len

.Leq5\key_len:
	do_aes_load	5, \key_len
	add	$(5*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Leq6\key_len:
	do_aes_load	6, \key_len
	add	$(6*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Leq7\key_len:
	do_aes_load	7, \key_len
	add	$(7*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\key_len
	jmp	.Lmain_loop2\key_len

.Lmult_of_8_blks\key_len:
	.if (\key_len != KEY_128)
		vmovdqa	0*16(p_keys), xkey0
		vmovdqa	4*16(p_keys), xkey4
		vmovdqa	8*16(p_keys), xkey8
		vmovdqa	12*16(p_keys), xkey12
	.else
		vmovdqa	0*16(p_keys), xkey0
		vmovdqa	3*16(p_keys), xkey4
		vmovdqa	6*16(p_keys), xkey8
		vmovdqa	9*16(p_keys), xkey12
	.endif
.align 16
.Lmain_loop2\key_len:
	/* num_bytes is a multiple of 8 and >0 */
	do_aes_noload	8, \key_len
	add	$(8*16), p_out
	sub	$(8*16), num_bytes
	jne	.Lmain_loop2\key_len

.Ldo_return2\key_len:
	/* return updated IV */
	vpshufb	xbyteswap, xcounter, xcounter
	vmovdqu	xcounter, (p_iv)
	ret
.endm

/*
 * routine to do AES128 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_128_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
ENTRY(aes_ctr_enc_128_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_128

ENDPROC(aes_ctr_enc_128_avx_by8)

/*
 * routine to do AES192 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_192_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
ENTRY(aes_ctr_enc_192_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_192

ENDPROC(aes_ctr_enc_192_avx_by8)

/*
 * routine to do AES256 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_256_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
ENTRY(aes_ctr_enc_256_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_256

ENDPROC(aes_ctr_enc_256_avx_by8)
