/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/*
 * AES CTR mode by8 optimization with AVX instructions. (x86_64)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Contact Information:
 * James Guilford <james.guilford@intel.com>
 * Sean Gulley <sean.m.gulley@intel.com>
 * Chandramouli Narayanan <mouli@linux.intel.com>
 */
/*
 * This is AES128/192/256 CTR mode optimization implementation. It requires
 * the support of Intel(R) AESNI and AVX instructions.
 *
 * This work was inspired by the AES CTR mode optimization published
 * in Intel Optimized IPSEC Cryptographic library.
 * Additional information on it can be found at:
 *    https://github.com/intel/intel-ipsec-mb
 */

#include <linux/linkage.h>

#define VMOVDQ		vmovdqu

/*
 * Note: the "x" prefix in these aliases means "this is an xmm register".  The
 * alias prefixes have no relation to XCTR where the "X" prefix means "XOR
 * counter".
 */
#define xdata0		%xmm0
#define xdata1		%xmm1
#define xdata2		%xmm2
#define xdata3		%xmm3
#define xdata4		%xmm4
#define xdata5		%xmm5
#define xdata6		%xmm6
#define xdata7		%xmm7
#define xcounter	%xmm8	// CTR mode only
#define xiv		%xmm8	// XCTR mode only
#define xbyteswap	%xmm9	// CTR mode only
#define xtmp		%xmm9	// XCTR mode only
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
#define counter		%r9	// XCTR mode only
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

/* generate a unique variable for xmm register */
.macro setxdata n
	var_xdata = %xmm\n
.endm

/* club the numeric 'id' to the symbol 'name' */

.macro club name, id
.altmacro
	.if \name == XDATA
		setxdata %\id
	.endif
.noaltmacro
.endm

/*
 * do_aes num_in_par load_keys key_len
 * This increments p_in, but not p_out
 */
.macro do_aes b, k, key_len, xctr
	.set by, \b
	.set load_keys, \k
	.set klen, \key_len

	.if (load_keys)
		vmovdqa	0*16(p_keys), xkey0
	.endif

	.if \xctr
		movq counter, xtmp
		.set i, 0
		.rept (by)
			club XDATA, i
			vpaddq	(ddq_add_1 + 16 * i)(%rip), xtmp, var_xdata
			.set i, (i +1)
		.endr
		.set i, 0
		.rept (by)
			club	XDATA, i
			vpxor	xiv, var_xdata, var_xdata
			.set i, (i +1)
		.endr
	.else
		vpshufb	xbyteswap, xcounter, xdata0
		.set i, 1
		.rept (by - 1)
			club XDATA, i
			vpaddq	(ddq_add_1 + 16 * (i - 1))(%rip), xcounter, var_xdata
			vptest	ddq_low_msk(%rip), var_xdata
			jnz 1f
			vpaddq	ddq_high_add_1(%rip), var_xdata, var_xdata
			vpaddq	ddq_high_add_1(%rip), xcounter, xcounter
			1:
			vpshufb	xbyteswap, var_xdata, var_xdata
			.set i, (i +1)
		.endr
	.endif

	vmovdqa	1*16(p_keys), xkeyA

	vpxor	xkey0, xdata0, xdata0
	.if \xctr
		add $by, counter
	.else
		vpaddq	(ddq_add_1 + 16 * (by - 1))(%rip), xcounter, xcounter
		vptest	ddq_low_msk(%rip), xcounter
		jnz	1f
		vpaddq	ddq_high_add_1(%rip), xcounter, xcounter
		1:
	.endif

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

.macro do_aes_load val, key_len, xctr
	do_aes \val, 1, \key_len, \xctr
.endm

.macro do_aes_noload val, key_len, xctr
	do_aes \val, 0, \key_len, \xctr
.endm

/* main body of aes ctr load */

.macro do_aes_ctrmain key_len, xctr
	cmp	$16, num_bytes
	jb	.Ldo_return2\xctr\key_len

	.if \xctr
		shr	$4, counter
		vmovdqu	(p_iv), xiv
	.else
		vmovdqa	byteswap_const(%rip), xbyteswap
		vmovdqu	(p_iv), xcounter
		vpshufb	xbyteswap, xcounter, xcounter
	.endif

	mov	num_bytes, tmp
	and	$(7*16), tmp
	jz	.Lmult_of_8_blks\xctr\key_len

	/* 1 <= tmp <= 7 */
	cmp	$(4*16), tmp
	jg	.Lgt4\xctr\key_len
	je	.Leq4\xctr\key_len

.Llt4\xctr\key_len:
	cmp	$(2*16), tmp
	jg	.Leq3\xctr\key_len
	je	.Leq2\xctr\key_len

.Leq1\xctr\key_len:
	do_aes_load	1, \key_len, \xctr
	add	$(1*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Leq2\xctr\key_len:
	do_aes_load	2, \key_len, \xctr
	add	$(2*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len


.Leq3\xctr\key_len:
	do_aes_load	3, \key_len, \xctr
	add	$(3*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Leq4\xctr\key_len:
	do_aes_load	4, \key_len, \xctr
	add	$(4*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Lgt4\xctr\key_len:
	cmp	$(6*16), tmp
	jg	.Leq7\xctr\key_len
	je	.Leq6\xctr\key_len

.Leq5\xctr\key_len:
	do_aes_load	5, \key_len, \xctr
	add	$(5*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Leq6\xctr\key_len:
	do_aes_load	6, \key_len, \xctr
	add	$(6*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Leq7\xctr\key_len:
	do_aes_load	7, \key_len, \xctr
	add	$(7*16), p_out
	and	$(~7*16), num_bytes
	jz	.Ldo_return2\xctr\key_len
	jmp	.Lmain_loop2\xctr\key_len

.Lmult_of_8_blks\xctr\key_len:
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
.Lmain_loop2\xctr\key_len:
	/* num_bytes is a multiple of 8 and >0 */
	do_aes_noload	8, \key_len, \xctr
	add	$(8*16), p_out
	sub	$(8*16), num_bytes
	jne	.Lmain_loop2\xctr\key_len

.Ldo_return2\xctr\key_len:
	.if !\xctr
		/* return updated IV */
		vpshufb	xbyteswap, xcounter, xcounter
		vmovdqu	xcounter, (p_iv)
	.endif
	RET
.endm

/*
 * routine to do AES128 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_128_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
SYM_FUNC_START(aes_ctr_enc_128_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_128 0

SYM_FUNC_END(aes_ctr_enc_128_avx_by8)

/*
 * routine to do AES192 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_192_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
SYM_FUNC_START(aes_ctr_enc_192_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_192 0

SYM_FUNC_END(aes_ctr_enc_192_avx_by8)

/*
 * routine to do AES256 CTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_ctr_enc_256_avx_by8(void *in, void *iv, void *keys, void *out,
 *			unsigned int num_bytes)
 */
SYM_FUNC_START(aes_ctr_enc_256_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_256 0

SYM_FUNC_END(aes_ctr_enc_256_avx_by8)

/*
 * routine to do AES128 XCTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_xctr_enc_128_avx_by8(const u8 *in, const u8 *iv, const void *keys,
 * 	u8* out, unsigned int num_bytes, unsigned int byte_ctr)
 */
SYM_FUNC_START(aes_xctr_enc_128_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_128 1

SYM_FUNC_END(aes_xctr_enc_128_avx_by8)

/*
 * routine to do AES192 XCTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_xctr_enc_192_avx_by8(const u8 *in, const u8 *iv, const void *keys,
 * 	u8* out, unsigned int num_bytes, unsigned int byte_ctr)
 */
SYM_FUNC_START(aes_xctr_enc_192_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_192 1

SYM_FUNC_END(aes_xctr_enc_192_avx_by8)

/*
 * routine to do AES256 XCTR enc/decrypt "by8"
 * XMM registers are clobbered.
 * Saving/restoring must be done at a higher level
 * aes_xctr_enc_256_avx_by8(const u8 *in, const u8 *iv, const void *keys,
 * 	u8* out, unsigned int num_bytes, unsigned int byte_ctr)
 */
SYM_FUNC_START(aes_xctr_enc_256_avx_by8)
	/* call the aes main loop */
	do_aes_ctrmain KEY_256 1

SYM_FUNC_END(aes_xctr_enc_256_avx_by8)
