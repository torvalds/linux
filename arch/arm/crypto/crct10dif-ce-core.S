//
// Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions instructions
//
// Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//

//
// Implement fast CRC-T10DIF computation with SSE and PCLMULQDQ instructions
//
// Copyright (c) 2013, Intel Corporation
//
// Authors:
//     Erdinc Ozturk <erdinc.ozturk@intel.com>
//     Vinodh Gopal <vinodh.gopal@intel.com>
//     James Guilford <james.guilford@intel.com>
//     Tim Chen <tim.c.chen@linux.intel.com>
//
// This software is available to you under a choice of one of two
// licenses.  You may choose to be licensed under the terms of the GNU
// General Public License (GPL) Version 2, available from the file
// COPYING in the main directory of this source tree, or the
// OpenIB.org BSD license below:
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the
//   distribution.
//
// * Neither the name of the Intel Corporation nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
//
// THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//       Function API:
//       UINT16 crc_t10dif_pcl(
//               UINT16 init_crc, //initial CRC value, 16 bits
//               const unsigned char *buf, //buffer pointer to calculate CRC on
//               UINT64 len //buffer length in bytes (64-bit data)
//       );
//
//       Reference paper titled "Fast CRC Computation for Generic
//	Polynomials Using PCLMULQDQ Instruction"
//       URL: http://www.intel.com/content/dam/www/public/us/en/documents
//  /white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
//
//

#include <linux/linkage.h>
#include <asm/assembler.h>

#ifdef CONFIG_CPU_ENDIAN_BE8
#define CPU_LE(code...)
#else
#define CPU_LE(code...)		code
#endif

	.text
	.fpu		crypto-neon-fp-armv8

	arg1_low32	.req	r0
	arg2		.req	r1
	arg3		.req	r2

	qzr		.req	q13

	q0l		.req	d0
	q0h		.req	d1
	q1l		.req	d2
	q1h		.req	d3
	q2l		.req	d4
	q2h		.req	d5
	q3l		.req	d6
	q3h		.req	d7
	q4l		.req	d8
	q4h		.req	d9
	q5l		.req	d10
	q5h		.req	d11
	q6l		.req	d12
	q6h		.req	d13
	q7l		.req	d14
	q7h		.req	d15

ENTRY(crc_t10dif_pmull)
	vmov.i8		qzr, #0			// init zero register

	// adjust the 16-bit initial_crc value, scale it to 32 bits
	lsl		arg1_low32, arg1_low32, #16

	// check if smaller than 256
	cmp		arg3, #256

	// for sizes less than 128, we can't fold 64B at a time...
	blt		_less_than_128

	// load the initial crc value
	// crc value does not need to be byte-reflected, but it needs
	// to be moved to the high part of the register.
	// because data will be byte-reflected and will align with
	// initial crc at correct place.
	vmov		s0, arg1_low32		// initial crc
	vext.8		q10, qzr, q0, #4

	// receive the initial 64B data, xor the initial crc value
	vld1.64		{q0-q1}, [arg2, :128]!
	vld1.64		{q2-q3}, [arg2, :128]!
	vld1.64		{q4-q5}, [arg2, :128]!
	vld1.64		{q6-q7}, [arg2, :128]!
CPU_LE(	vrev64.8	q0, q0			)
CPU_LE(	vrev64.8	q1, q1			)
CPU_LE(	vrev64.8	q2, q2			)
CPU_LE(	vrev64.8	q3, q3			)
CPU_LE(	vrev64.8	q4, q4			)
CPU_LE(	vrev64.8	q5, q5			)
CPU_LE(	vrev64.8	q6, q6			)
CPU_LE(	vrev64.8	q7, q7			)

	vswp		d0, d1
	vswp		d2, d3
	vswp		d4, d5
	vswp		d6, d7
	vswp		d8, d9
	vswp		d10, d11
	vswp		d12, d13
	vswp		d14, d15

	// XOR the initial_crc value
	veor.8		q0, q0, q10

	adr		ip, rk3
	vld1.64		{q10}, [ip, :128]	// xmm10 has rk3 and rk4

	//
	// we subtract 256 instead of 128 to save one instruction from the loop
	//
	sub		arg3, arg3, #256

	// at this section of the code, there is 64*x+y (0<=y<64) bytes of
	// buffer. The _fold_64_B_loop will fold 64B at a time
	// until we have 64+y Bytes of buffer


	// fold 64B at a time. This section of the code folds 4 vector
	// registers in parallel
_fold_64_B_loop:

	.macro		fold64, reg1, reg2
	vld1.64		{q11-q12}, [arg2, :128]!

	vmull.p64	q8, \reg1\()h, d21
	vmull.p64	\reg1, \reg1\()l, d20
	vmull.p64	q9, \reg2\()h, d21
	vmull.p64	\reg2, \reg2\()l, d20

CPU_LE(	vrev64.8	q11, q11		)
CPU_LE(	vrev64.8	q12, q12		)
	vswp		d22, d23
	vswp		d24, d25

	veor.8		\reg1, \reg1, q8
	veor.8		\reg2, \reg2, q9
	veor.8		\reg1, \reg1, q11
	veor.8		\reg2, \reg2, q12
	.endm

	fold64		q0, q1
	fold64		q2, q3
	fold64		q4, q5
	fold64		q6, q7

	subs		arg3, arg3, #128

	// check if there is another 64B in the buffer to be able to fold
	bge		_fold_64_B_loop

	// at this point, the buffer pointer is pointing at the last y Bytes
	// of the buffer the 64B of folded data is in 4 of the vector
	// registers: v0, v1, v2, v3

	// fold the 8 vector registers to 1 vector register with different
	// constants

	adr		ip, rk9
	vld1.64		{q10}, [ip, :128]!

	.macro		fold16, reg, rk
	vmull.p64	q8, \reg\()l, d20
	vmull.p64	\reg, \reg\()h, d21
	.ifnb		\rk
	vld1.64		{q10}, [ip, :128]!
	.endif
	veor.8		q7, q7, q8
	veor.8		q7, q7, \reg
	.endm

	fold16		q0, rk11
	fold16		q1, rk13
	fold16		q2, rk15
	fold16		q3, rk17
	fold16		q4, rk19
	fold16		q5, rk1
	fold16		q6

	// instead of 64, we add 48 to the loop counter to save 1 instruction
	// from the loop instead of a cmp instruction, we use the negative
	// flag with the jl instruction
	adds		arg3, arg3, #(128-16)
	blt		_final_reduction_for_128

	// now we have 16+y bytes left to reduce. 16 Bytes is in register v7
	// and the rest is in memory. We can fold 16 bytes at a time if y>=16
	// continue folding 16B at a time

_16B_reduction_loop:
	vmull.p64	q8, d14, d20
	vmull.p64	q7, d15, d21
	veor.8		q7, q7, q8

	vld1.64		{q0}, [arg2, :128]!
CPU_LE(	vrev64.8	q0, q0		)
	vswp		d0, d1
	veor.8		q7, q7, q0
	subs		arg3, arg3, #16

	// instead of a cmp instruction, we utilize the flags with the
	// jge instruction equivalent of: cmp arg3, 16-16
	// check if there is any more 16B in the buffer to be able to fold
	bge		_16B_reduction_loop

	// now we have 16+z bytes left to reduce, where 0<= z < 16.
	// first, we reduce the data in the xmm7 register

_final_reduction_for_128:
	// check if any more data to fold. If not, compute the CRC of
	// the final 128 bits
	adds		arg3, arg3, #16
	beq		_128_done

	// here we are getting data that is less than 16 bytes.
	// since we know that there was data before the pointer, we can
	// offset the input pointer before the actual point, to receive
	// exactly 16 bytes. after that the registers need to be adjusted.
_get_last_two_regs:
	add		arg2, arg2, arg3
	sub		arg2, arg2, #16
	vld1.64		{q1}, [arg2]
CPU_LE(	vrev64.8	q1, q1			)
	vswp		d2, d3

	// get rid of the extra data that was loaded before
	// load the shift constant
	adr		ip, tbl_shf_table + 16
	sub		ip, ip, arg3
	vld1.8		{q0}, [ip]

	// shift v2 to the left by arg3 bytes
	vtbl.8		d4, {d14-d15}, d0
	vtbl.8		d5, {d14-d15}, d1

	// shift v7 to the right by 16-arg3 bytes
	vmov.i8		q9, #0x80
	veor.8		q0, q0, q9
	vtbl.8		d18, {d14-d15}, d0
	vtbl.8		d19, {d14-d15}, d1

	// blend
	vshr.s8		q0, q0, #7		// convert to 8-bit mask
	vbsl.8		q0, q2, q1

	// fold 16 Bytes
	vmull.p64	q8, d18, d20
	vmull.p64	q7, d19, d21
	veor.8		q7, q7, q8
	veor.8		q7, q7, q0

_128_done:
	// compute crc of a 128-bit value
	vldr		d20, rk5
	vldr		d21, rk6		// rk5 and rk6 in xmm10

	// 64b fold
	vext.8		q0, qzr, q7, #8
	vmull.p64	q7, d15, d20
	veor.8		q7, q7, q0

	// 32b fold
	vext.8		q0, q7, qzr, #12
	vmov		s31, s3
	vmull.p64	q0, d0, d21
	veor.8		q7, q0, q7

	// barrett reduction
_barrett:
	vldr		d20, rk7
	vldr		d21, rk8

	vmull.p64	q0, d15, d20
	vext.8		q0, qzr, q0, #12
	vmull.p64	q0, d1, d21
	vext.8		q0, qzr, q0, #12
	veor.8		q7, q7, q0
	vmov		r0, s29

_cleanup:
	// scale the result back to 16 bits
	lsr		r0, r0, #16
	bx		lr

_less_than_128:
	teq		arg3, #0
	beq		_cleanup

	vmov.i8		q0, #0
	vmov		s3, arg1_low32		// get the initial crc value

	vld1.64		{q7}, [arg2, :128]!
CPU_LE(	vrev64.8	q7, q7		)
	vswp		d14, d15
	veor.8		q7, q7, q0

	cmp		arg3, #16
	beq		_128_done		// exactly 16 left
	blt		_less_than_16_left

	// now if there is, load the constants
	vldr		d20, rk1
	vldr		d21, rk2		// rk1 and rk2 in xmm10

	// check if there is enough buffer to be able to fold 16B at a time
	subs		arg3, arg3, #32
	addlt		arg3, arg3, #16
	blt		_get_last_two_regs
	b		_16B_reduction_loop

_less_than_16_left:
	// shl r9, 4
	adr		ip, tbl_shf_table + 16
	sub		ip, ip, arg3
	vld1.8		{q0}, [ip]
	vmov.i8		q9, #0x80
	veor.8		q0, q0, q9
	vtbl.8		d18, {d14-d15}, d0
	vtbl.8		d15, {d14-d15}, d1
	vmov		d14, d18
	b		_128_done
ENDPROC(crc_t10dif_pmull)

// precomputed constants
// these constants are precomputed from the poly:
// 0x8bb70000 (0x8bb7 scaled to 32 bits)
	.align		4
// Q = 0x18BB70000
// rk1 = 2^(32*3) mod Q << 32
// rk2 = 2^(32*5) mod Q << 32
// rk3 = 2^(32*15) mod Q << 32
// rk4 = 2^(32*17) mod Q << 32
// rk5 = 2^(32*3) mod Q << 32
// rk6 = 2^(32*2) mod Q << 32
// rk7 = floor(2^64/Q)
// rk8 = Q

rk3:	.quad		0x9d9d000000000000
rk4:	.quad		0x7cf5000000000000
rk5:	.quad		0x2d56000000000000
rk6:	.quad		0x1368000000000000
rk7:	.quad		0x00000001f65a57f8
rk8:	.quad		0x000000018bb70000
rk9:	.quad		0xceae000000000000
rk10:	.quad		0xbfd6000000000000
rk11:	.quad		0x1e16000000000000
rk12:	.quad		0x713c000000000000
rk13:	.quad		0xf7f9000000000000
rk14:	.quad		0x80a6000000000000
rk15:	.quad		0x044c000000000000
rk16:	.quad		0xe658000000000000
rk17:	.quad		0xad18000000000000
rk18:	.quad		0xa497000000000000
rk19:	.quad		0x6ee3000000000000
rk20:	.quad		0xe7b5000000000000
rk1:	.quad		0x2d56000000000000
rk2:	.quad		0x06df000000000000

tbl_shf_table:
// use these values for shift constants for the tbl/tbx instruction
// different alignments result in values as shown:
//	DDQ 0x008f8e8d8c8b8a898887868584838281 # shl 15 (16-1) / shr1
//	DDQ 0x01008f8e8d8c8b8a8988878685848382 # shl 14 (16-3) / shr2
//	DDQ 0x0201008f8e8d8c8b8a89888786858483 # shl 13 (16-4) / shr3
//	DDQ 0x030201008f8e8d8c8b8a898887868584 # shl 12 (16-4) / shr4
//	DDQ 0x04030201008f8e8d8c8b8a8988878685 # shl 11 (16-5) / shr5
//	DDQ 0x0504030201008f8e8d8c8b8a89888786 # shl 10 (16-6) / shr6
//	DDQ 0x060504030201008f8e8d8c8b8a898887 # shl 9  (16-7) / shr7
//	DDQ 0x07060504030201008f8e8d8c8b8a8988 # shl 8  (16-8) / shr8
//	DDQ 0x0807060504030201008f8e8d8c8b8a89 # shl 7  (16-9) / shr9
//	DDQ 0x090807060504030201008f8e8d8c8b8a # shl 6  (16-10) / shr10
//	DDQ 0x0a090807060504030201008f8e8d8c8b # shl 5  (16-11) / shr11
//	DDQ 0x0b0a090807060504030201008f8e8d8c # shl 4  (16-12) / shr12
//	DDQ 0x0c0b0a090807060504030201008f8e8d # shl 3  (16-13) / shr13
//	DDQ 0x0d0c0b0a090807060504030201008f8e # shl 2  (16-14) / shr14
//	DDQ 0x0e0d0c0b0a090807060504030201008f # shl 1  (16-15) / shr15

	.byte		 0x0, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
	.byte		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe , 0x0
