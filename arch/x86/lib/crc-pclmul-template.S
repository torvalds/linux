/* SPDX-License-Identifier: GPL-2.0-or-later */
//
// Template to generate [V]PCLMULQDQ-based CRC functions for x86
//
// Copyright 2025 Google LLC
//
// Author: Eric Biggers <ebiggers@google.com>

#include <linux/linkage.h>
#include <linux/objtool.h>

// Offsets within the generated constants table
.set OFFSETOF_BSWAP_MASK,			-5*16	// msb-first CRCs only
.set OFFSETOF_FOLD_ACROSS_2048_BITS_CONSTS,	-4*16	// must precede next
.set OFFSETOF_FOLD_ACROSS_1024_BITS_CONSTS,	-3*16	// must precede next
.set OFFSETOF_FOLD_ACROSS_512_BITS_CONSTS,	-2*16	// must precede next
.set OFFSETOF_FOLD_ACROSS_256_BITS_CONSTS,	-1*16	// must precede next
.set OFFSETOF_FOLD_ACROSS_128_BITS_CONSTS,	0*16	// must be 0
.set OFFSETOF_SHUF_TABLE,			1*16
.set OFFSETOF_BARRETT_REDUCTION_CONSTS,		4*16

// Emit a VEX (or EVEX) coded instruction if allowed, or emulate it using the
// corresponding non-VEX instruction plus any needed moves.  The supported
// instruction formats are:
//
//     - Two-arg [src, dst], where the non-VEX format is the same.
//     - Three-arg [src1, src2, dst] where the non-VEX format is
//	 [src1, src2_and_dst].  If src2 != dst, then src1 must != dst too.
//
// \insn gives the instruction without a "v" prefix and including any immediate
// argument if needed to make the instruction follow one of the above formats.
// If \unaligned_mem_tmp is given, then the emitted non-VEX code moves \arg1 to
// it first; this is needed when \arg1 is an unaligned mem operand.
.macro	_cond_vex	insn:req, arg1:req, arg2:req, arg3, unaligned_mem_tmp
.if AVX_LEVEL == 0
  // VEX not allowed.  Emulate it.
  .ifnb \arg3 // Three-arg [src1, src2, dst]
    .ifc "\arg2", "\arg3" // src2 == dst?
      .ifnb \unaligned_mem_tmp
	movdqu		\arg1, \unaligned_mem_tmp
	\insn		\unaligned_mem_tmp, \arg3
      .else
	\insn		\arg1, \arg3
      .endif
    .else // src2 != dst
      .ifc "\arg1", "\arg3"
	.error "Can't have src1 == dst when src2 != dst"
      .endif
      .ifnb \unaligned_mem_tmp
	movdqu		\arg1, \unaligned_mem_tmp
	movdqa		\arg2, \arg3
	\insn		\unaligned_mem_tmp, \arg3
      .else
	movdqa		\arg2, \arg3
	\insn		\arg1, \arg3
      .endif
    .endif
  .else // Two-arg [src, dst]
    .ifnb \unaligned_mem_tmp
	movdqu		\arg1, \unaligned_mem_tmp
	\insn		\unaligned_mem_tmp, \arg2
    .else
	\insn		\arg1, \arg2
    .endif
  .endif
.else
  // VEX is allowed.  Emit the desired instruction directly.
  .ifnb \arg3
	v\insn		\arg1, \arg2, \arg3
  .else
	v\insn		\arg1, \arg2
  .endif
.endif
.endm

// Broadcast an aligned 128-bit mem operand to all 128-bit lanes of a vector
// register of length VL.
.macro	_vbroadcast	src, dst
.if VL == 16
	_cond_vex movdqa,	\src, \dst
.elseif VL == 32
	vbroadcasti128		\src, \dst
.else
	vbroadcasti32x4		\src, \dst
.endif
.endm

// Load \vl bytes from the unaligned mem operand \src into \dst, and if the CRC
// is msb-first use \bswap_mask to reflect the bytes within each 128-bit lane.
.macro	_load_data	vl, src, bswap_mask, dst
.if \vl < 64
	_cond_vex movdqu,	"\src", \dst
.else
	vmovdqu8		\src, \dst
.endif
.if !LSB_CRC
	_cond_vex pshufb,	\bswap_mask, \dst, \dst
.endif
.endm

.macro	_prepare_v0	vl, v0, v1, bswap_mask
.if LSB_CRC
  .if \vl < 64
	_cond_vex pxor,		(BUF), \v0, \v0, unaligned_mem_tmp=\v1
  .else
	vpxorq			(BUF), \v0, \v0
  .endif
.else
	_load_data		\vl, (BUF), \bswap_mask, \v1
  .if \vl < 64
	_cond_vex pxor,		\v1, \v0, \v0
  .else
	vpxorq			\v1, \v0, \v0
  .endif
.endif
.endm

// The x^0..x^63 terms, i.e. poly128 mod x^64, i.e. the physically low qword for
// msb-first order or the physically high qword for lsb-first order
#define LO64_TERMS 0

// The x^64..x^127 terms, i.e. floor(poly128 / x^64), i.e. the physically high
// qword for msb-first order or the physically low qword for lsb-first order
#define HI64_TERMS 1

// Multiply the given \src1_terms of each 128-bit lane of \src1 by the given
// \src2_terms of each 128-bit lane of \src2, and write the result(s) to \dst.
.macro	_pclmulqdq	src1, src1_terms, src2, src2_terms, dst
	_cond_vex "pclmulqdq $((\src1_terms ^ LSB_CRC) << 4) ^ (\src2_terms ^ LSB_CRC),", \
		  \src1, \src2, \dst
.endm

// Fold \acc into \data and store the result back into \acc.  \data can be an
// unaligned mem operand if using VEX is allowed and the CRC is lsb-first so no
// byte-reflection is needed; otherwise it must be a vector register.  \consts
// is a vector register containing the needed fold constants, and \tmp is a
// temporary vector register.  All arguments must be the same length.
.macro	_fold_vec	acc, data, consts, tmp
	_pclmulqdq	\consts, HI64_TERMS, \acc, HI64_TERMS, \tmp
	_pclmulqdq	\consts, LO64_TERMS, \acc, LO64_TERMS, \acc
.if AVX_LEVEL <= 2
	_cond_vex pxor,	\data, \tmp, \tmp
	_cond_vex pxor,	\tmp, \acc, \acc
.else
	vpternlogq	$0x96, \data, \tmp, \acc
.endif
.endm

// Fold \acc into \data and store the result back into \acc.  \data is an
// unaligned mem operand, \consts is a vector register containing the needed
// fold constants, \bswap_mask is a vector register containing the
// byte-reflection table if the CRC is msb-first, and \tmp1 and \tmp2 are
// temporary vector registers.  All arguments must have length \vl.
.macro	_fold_vec_mem	vl, acc, data, consts, bswap_mask, tmp1, tmp2
.if AVX_LEVEL == 0 || !LSB_CRC
	_load_data	\vl, \data, \bswap_mask, \tmp1
	_fold_vec	\acc, \tmp1, \consts, \tmp2
.else
	_fold_vec	\acc, \data, \consts, \tmp1
.endif
.endm

// Load the constants for folding across 2**i vectors of length VL at a time
// into all 128-bit lanes of the vector register CONSTS.
.macro	_load_vec_folding_consts	i
	_vbroadcast OFFSETOF_FOLD_ACROSS_128_BITS_CONSTS+(4-LOG2_VL-\i)*16(CONSTS_PTR), \
		    CONSTS
.endm

// Given vector registers \v0 and \v1 of length \vl, fold \v0 into \v1 and store
// the result back into \v0.  If the remaining length mod \vl is nonzero, also
// fold \vl data bytes from BUF.  For both operations the fold distance is \vl.
// \consts must be a register of length \vl containing the fold constants.
.macro	_fold_vec_final	vl, v0, v1, consts, bswap_mask, tmp1, tmp2
	_fold_vec	\v0, \v1, \consts, \tmp1
	test		$\vl, LEN8
	jz		.Lfold_vec_final_done\@
	_fold_vec_mem	\vl, \v0, (BUF), \consts, \bswap_mask, \tmp1, \tmp2
	add		$\vl, BUF
.Lfold_vec_final_done\@:
.endm

// This macro generates the body of a CRC function with the following prototype:
//
// crc_t crc_func(crc_t crc, const u8 *buf, size_t len, const void *consts);
//
// |crc| is the initial CRC, and crc_t is a data type wide enough to hold it.
// |buf| is the data to checksum.  |len| is the data length in bytes, which must
// be at least 16.  |consts| is a pointer to the fold_across_128_bits_consts
// field of the constants struct that was generated for the chosen CRC variant.
//
// Moving onto the macro parameters, \n is the number of bits in the CRC, e.g.
// 32 for a CRC-32.  Currently the supported values are 8, 16, 32, and 64.  If
// the file is compiled in i386 mode, then the maximum supported value is 32.
//
// \lsb_crc is 1 if the CRC processes the least significant bit of each byte
// first, i.e. maps bit0 to x^7, bit1 to x^6, ..., bit7 to x^0.  \lsb_crc is 0
// if the CRC processes the most significant bit of each byte first, i.e. maps
// bit0 to x^0, bit1 to x^1, bit7 to x^7.
//
// \vl is the maximum length of vector register to use in bytes: 16, 32, or 64.
//
// \avx_level is the level of AVX support to use: 0 for SSE only, 2 for AVX2, or
// 512 for AVX512.
//
// If \vl == 16 && \avx_level == 0, the generated code requires:
// PCLMULQDQ && SSE4.1.  (Note: all known CPUs with PCLMULQDQ also have SSE4.1.)
//
// If \vl == 32 && \avx_level == 2, the generated code requires:
// VPCLMULQDQ && AVX2.
//
// If \vl == 64 && \avx_level == 512, the generated code requires:
// VPCLMULQDQ && AVX512BW && AVX512VL.
//
// Other \vl and \avx_level combinations are either not supported or not useful.
.macro	_crc_pclmul	n, lsb_crc, vl, avx_level
	.set	LSB_CRC,	\lsb_crc
	.set	VL,		\vl
	.set	AVX_LEVEL,	\avx_level

	// Define aliases for the xmm, ymm, or zmm registers according to VL.
.irp i, 0,1,2,3,4,5,6,7
  .if VL == 16
	.set	V\i,		%xmm\i
	.set	LOG2_VL,	4
  .elseif VL == 32
	.set	V\i,		%ymm\i
	.set	LOG2_VL,	5
  .elseif VL == 64
	.set	V\i,		%zmm\i
	.set	LOG2_VL,	6
  .else
	.error "Unsupported vector length"
  .endif
.endr
	// Define aliases for the function parameters.
	// Note: when crc_t is shorter than u32, zero-extension to 32 bits is
	// guaranteed by the ABI.  Zero-extension to 64 bits is *not* guaranteed
	// when crc_t is shorter than u64.
#ifdef __x86_64__
.if \n <= 32
	.set	CRC,		%edi
.else
	.set	CRC,		%rdi
.endif
	.set	BUF,		%rsi
	.set	LEN,		%rdx
	.set	LEN32,		%edx
	.set	LEN8,		%dl
	.set	CONSTS_PTR,	%rcx
#else
	// 32-bit support, assuming -mregparm=3 and not including support for
	// CRC-64 (which would use both eax and edx to pass the crc parameter).
	.set	CRC,		%eax
	.set	BUF,		%edx
	.set	LEN,		%ecx
	.set	LEN32,		%ecx
	.set	LEN8,		%cl
	.set	CONSTS_PTR,	%ebx	// Passed on stack
#endif

	// Define aliases for some local variables.  V0-V5 are used without
	// aliases (for accumulators, data, temporary values, etc).  Staying
	// within the first 8 vector registers keeps the code 32-bit SSE
	// compatible and reduces the size of 64-bit SSE code slightly.
	.set	BSWAP_MASK,	V6
	.set	BSWAP_MASK_YMM,	%ymm6
	.set	BSWAP_MASK_XMM,	%xmm6
	.set	CONSTS,		V7
	.set	CONSTS_YMM,	%ymm7
	.set	CONSTS_XMM,	%xmm7

	// Use ANNOTATE_NOENDBR to suppress an objtool warning, since the
	// functions generated by this macro are called only by static_call.
	ANNOTATE_NOENDBR

#ifdef __i386__
	push		CONSTS_PTR
	mov		8(%esp), CONSTS_PTR
#endif

	// Create a 128-bit vector that contains the initial CRC in the end
	// representing the high-order polynomial coefficients, and the rest 0.
	// If the CRC is msb-first, also load the byte-reflection table.
.if \n <= 32
	_cond_vex movd,	CRC, %xmm0
.else
	_cond_vex movq,	CRC, %xmm0
.endif
.if !LSB_CRC
	_cond_vex pslldq, $(128-\n)/8, %xmm0, %xmm0
	_vbroadcast	OFFSETOF_BSWAP_MASK(CONSTS_PTR), BSWAP_MASK
.endif

	// Load the first vector of data and XOR the initial CRC into the
	// appropriate end of the first 128-bit lane of data.  If LEN < VL, then
	// use a short vector and jump ahead to the final reduction.  (LEN >= 16
	// is guaranteed here but not necessarily LEN >= VL.)
.if VL >= 32
	cmp		$VL, LEN
	jae		.Lat_least_1vec\@
  .if VL == 64
	cmp		$32, LEN32
	jb		.Lless_than_32bytes\@
	_prepare_v0	32, %ymm0, %ymm1, BSWAP_MASK_YMM
	add		$32, BUF
	jmp		.Lreduce_256bits_to_128bits\@
.Lless_than_32bytes\@:
  .endif
	_prepare_v0	16, %xmm0, %xmm1, BSWAP_MASK_XMM
	add		$16, BUF
	vmovdqa		OFFSETOF_FOLD_ACROSS_128_BITS_CONSTS(CONSTS_PTR), CONSTS_XMM
	jmp		.Lcheck_for_partial_block\@
.Lat_least_1vec\@:
.endif
	_prepare_v0	VL, V0, V1, BSWAP_MASK

	// Handle VL <= LEN < 4*VL.
	cmp		$4*VL-1, LEN
	ja		.Lat_least_4vecs\@
	add		$VL, BUF
	// If VL <= LEN < 2*VL, then jump ahead to the reduction from 1 vector.
	// If VL==16 then load fold_across_128_bits_consts first, as the final
	// reduction depends on it and it won't be loaded anywhere else.
	cmp		$2*VL-1, LEN32
.if VL == 16
	_cond_vex movdqa, OFFSETOF_FOLD_ACROSS_128_BITS_CONSTS(CONSTS_PTR), CONSTS_XMM
.endif
	jbe		.Lreduce_1vec_to_128bits\@
	// Otherwise 2*VL <= LEN < 4*VL.  Load one more vector and jump ahead to
	// the reduction from 2 vectors.
	_load_data	VL, (BUF), BSWAP_MASK, V1
	add		$VL, BUF
	jmp		.Lreduce_2vecs_to_1\@

.Lat_least_4vecs\@:
	// Load 3 more vectors of data.
	_load_data	VL, 1*VL(BUF), BSWAP_MASK, V1
	_load_data	VL, 2*VL(BUF), BSWAP_MASK, V2
	_load_data	VL, 3*VL(BUF), BSWAP_MASK, V3
	sub		$-4*VL, BUF	// Shorter than 'add 4*VL' when VL=32
	add		$-4*VL, LEN	// Shorter than 'sub 4*VL' when VL=32

	// Main loop: while LEN >= 4*VL, fold the 4 vectors V0-V3 into the next
	// 4 vectors of data and write the result back to V0-V3.
	cmp		$4*VL-1, LEN	// Shorter than 'cmp 4*VL' when VL=32
	jbe		.Lreduce_4vecs_to_2\@
	_load_vec_folding_consts	2
.Lfold_4vecs_loop\@:
	_fold_vec_mem	VL, V0, 0*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	_fold_vec_mem	VL, V1, 1*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	_fold_vec_mem	VL, V2, 2*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	_fold_vec_mem	VL, V3, 3*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	sub		$-4*VL, BUF
	add		$-4*VL, LEN
	cmp		$4*VL-1, LEN
	ja		.Lfold_4vecs_loop\@

	// Fold V0,V1 into V2,V3 and write the result back to V0,V1.  Then fold
	// two more vectors of data from BUF, if at least that much remains.
.Lreduce_4vecs_to_2\@:
	_load_vec_folding_consts	1
	_fold_vec	V0, V2, CONSTS, V4
	_fold_vec	V1, V3, CONSTS, V4
	test		$2*VL, LEN8
	jz		.Lreduce_2vecs_to_1\@
	_fold_vec_mem	VL, V0, 0*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	_fold_vec_mem	VL, V1, 1*VL(BUF), CONSTS, BSWAP_MASK, V4, V5
	sub		$-2*VL, BUF

	// Fold V0 into V1 and write the result back to V0.  Then fold one more
	// vector of data from BUF, if at least that much remains.
.Lreduce_2vecs_to_1\@:
	_load_vec_folding_consts	0
	_fold_vec_final	VL, V0, V1, CONSTS, BSWAP_MASK, V4, V5

.Lreduce_1vec_to_128bits\@:
.if VL == 64
	// Reduce 512-bit %zmm0 to 256-bit %ymm0.  Then fold 256 more bits of
	// data from BUF, if at least that much remains.
	vbroadcasti128	OFFSETOF_FOLD_ACROSS_256_BITS_CONSTS(CONSTS_PTR), CONSTS_YMM
	vextracti64x4	$1, %zmm0, %ymm1
	_fold_vec_final	32, %ymm0, %ymm1, CONSTS_YMM, BSWAP_MASK_YMM, %ymm4, %ymm5
.Lreduce_256bits_to_128bits\@:
.endif
.if VL >= 32
	// Reduce 256-bit %ymm0 to 128-bit %xmm0.  Then fold 128 more bits of
	// data from BUF, if at least that much remains.
	vmovdqa		OFFSETOF_FOLD_ACROSS_128_BITS_CONSTS(CONSTS_PTR), CONSTS_XMM
	vextracti128	$1, %ymm0, %xmm1
	_fold_vec_final	16, %xmm0, %xmm1, CONSTS_XMM, BSWAP_MASK_XMM, %xmm4, %xmm5
.Lcheck_for_partial_block\@:
.endif
	and		$15, LEN32
	jz		.Lreduce_128bits_to_crc\@

	// 1 <= LEN <= 15 data bytes remain in BUF.  The polynomial is now
	// A*(x^(8*LEN)) + B, where A is the 128-bit polynomial stored in %xmm0
	// and B is the polynomial of the remaining LEN data bytes.  To reduce
	// this to 128 bits without needing fold constants for each possible
	// LEN, rearrange this expression into C1*(x^128) + C2, where
	// C1 = floor(A / x^(128 - 8*LEN)) and C2 = A*x^(8*LEN) + B mod x^128.
	// Then fold C1 into C2, which is just another fold across 128 bits.

.if !LSB_CRC || AVX_LEVEL == 0
	// Load the last 16 data bytes.  Note that originally LEN was >= 16.
	_load_data	16, "-16(BUF,LEN)", BSWAP_MASK_XMM, %xmm2
.endif // Else will use vpblendvb mem operand later.
.if !LSB_CRC
	neg		LEN	// Needed for indexing shuf_table
.endif

	// tmp = A*x^(8*LEN) mod x^128
	// lsb: pshufb by [LEN, LEN+1, ..., 15, -1, -1, ..., -1]
	//	i.e. right-shift by LEN bytes.
	// msb: pshufb by [-1, -1, ..., -1, 0, 1, ..., 15-LEN]
	//	i.e. left-shift by LEN bytes.
	_cond_vex movdqu,	"OFFSETOF_SHUF_TABLE+16(CONSTS_PTR,LEN)", %xmm3
	_cond_vex pshufb,	%xmm3, %xmm0, %xmm1

	// C1 = floor(A / x^(128 - 8*LEN))
	// lsb: pshufb by [-1, -1, ..., -1, 0, 1, ..., LEN-1]
	//	i.e. left-shift by 16-LEN bytes.
	// msb: pshufb by [16-LEN, 16-LEN+1, ..., 15, -1, -1, ..., -1]
	//	i.e. right-shift by 16-LEN bytes.
	_cond_vex pshufb,	"OFFSETOF_SHUF_TABLE+32*!LSB_CRC(CONSTS_PTR,LEN)", \
				%xmm0, %xmm0, unaligned_mem_tmp=%xmm4

	// C2 = tmp + B.  This is just a blend of tmp with the last 16 data
	// bytes (reflected if msb-first).  The blend mask is the shuffle table
	// that was used to create tmp.  0 selects tmp, and 1 last16databytes.
.if AVX_LEVEL == 0
	movdqa		%xmm0, %xmm4
	movdqa		%xmm3, %xmm0
	pblendvb	%xmm2, %xmm1	// uses %xmm0 as implicit operand
	movdqa		%xmm4, %xmm0
.elseif LSB_CRC
	vpblendvb	%xmm3, -16(BUF,LEN), %xmm1, %xmm1
.else
	vpblendvb	%xmm3, %xmm2, %xmm1, %xmm1
.endif

	// Fold C1 into C2 and store the 128-bit result in %xmm0.
	_fold_vec	%xmm0, %xmm1, CONSTS_XMM, %xmm4

.Lreduce_128bits_to_crc\@:
	// Compute the CRC as %xmm0 * x^n mod G.  Here %xmm0 means the 128-bit
	// polynomial stored in %xmm0 (using either lsb-first or msb-first bit
	// order according to LSB_CRC), and G is the CRC's generator polynomial.

	// First, multiply %xmm0 by x^n and reduce the result to 64+n bits:
	//
	//	t0 := (x^(64+n) mod G) * floor(%xmm0 / x^64) +
	//	      x^n * (%xmm0 mod x^64)
	//
	// Store t0 * x^(64-n) in %xmm0.  I.e., actually do:
	//
	//	%xmm0 := ((x^(64+n) mod G) * x^(64-n)) * floor(%xmm0 / x^64) +
	//		 x^64 * (%xmm0 mod x^64)
	//
	// The extra unreduced factor of x^(64-n) makes floor(t0 / x^n) aligned
	// to the HI64_TERMS of %xmm0 so that the next pclmulqdq can easily
	// select it.  The 64-bit constant (x^(64+n) mod G) * x^(64-n) in the
	// msb-first case, or (x^(63+n) mod G) * x^(64-n) in the lsb-first case
	// (considering the extra factor of x that gets implicitly introduced by
	// each pclmulqdq when using lsb-first order), is identical to the
	// constant that was used earlier for folding the LO64_TERMS across 128
	// bits.  Thus it's already available in LO64_TERMS of CONSTS_XMM.
	_pclmulqdq		CONSTS_XMM, LO64_TERMS, %xmm0, HI64_TERMS, %xmm1
.if LSB_CRC
	_cond_vex psrldq,	$8, %xmm0, %xmm0  // x^64 * (%xmm0 mod x^64)
.else
	_cond_vex pslldq,	$8, %xmm0, %xmm0  // x^64 * (%xmm0 mod x^64)
.endif
	_cond_vex pxor,		%xmm1, %xmm0, %xmm0
	// The HI64_TERMS of %xmm0 now contain floor(t0 / x^n).
	// The LO64_TERMS of %xmm0 now contain (t0 mod x^n) * x^(64-n).

	// First step of Barrett reduction: Compute floor(t0 / G).  This is the
	// polynomial by which G needs to be multiplied to cancel out the x^n
	// and higher terms of t0, i.e. to reduce t0 mod G.  First do:
	//
	//	t1 := floor(x^(63+n) / G) * x * floor(t0 / x^n)
	//
	// Then the desired value floor(t0 / G) is floor(t1 / x^64).  The 63 in
	// x^(63+n) is the maximum degree of floor(t0 / x^n) and thus the lowest
	// value that makes enough precision be carried through the calculation.
	//
	// The '* x' makes it so the result is floor(t1 / x^64) rather than
	// floor(t1 / x^63), making it qword-aligned in HI64_TERMS so that it
	// can be extracted much more easily in the next step.  In the lsb-first
	// case the '* x' happens implicitly.  In the msb-first case it must be
	// done explicitly; floor(x^(63+n) / G) * x is a 65-bit constant, so the
	// constant passed to pclmulqdq is (floor(x^(63+n) / G) * x) - x^64, and
	// the multiplication by the x^64 term is handled using a pxor.  The
	// pxor causes the low 64 terms of t1 to be wrong, but they are unused.
	_cond_vex movdqa,	OFFSETOF_BARRETT_REDUCTION_CONSTS(CONSTS_PTR), CONSTS_XMM
	_pclmulqdq		CONSTS_XMM, HI64_TERMS, %xmm0, HI64_TERMS, %xmm1
.if !LSB_CRC
	_cond_vex pxor,		%xmm0, %xmm1, %xmm1 // += x^64 * floor(t0 / x^n)
.endif
	// The HI64_TERMS of %xmm1 now contain floor(t1 / x^64) = floor(t0 / G).

	// Second step of Barrett reduction: Cancel out the x^n and higher terms
	// of t0 by subtracting the needed multiple of G.  This gives the CRC:
	//
	//	crc := t0 - (G * floor(t0 / G))
	//
	// But %xmm0 contains t0 * x^(64-n), so it's more convenient to do:
	//
	//	crc := ((t0 * x^(64-n)) - ((G * x^(64-n)) * floor(t0 / G))) / x^(64-n)
	//
	// Furthermore, since the resulting CRC is n-bit, if mod x^n is
	// explicitly applied to it then the x^n term of G makes no difference
	// in the result and can be omitted.  This helps keep the constant
	// multiplier in 64 bits in most cases.  This gives the following:
	//
	//	%xmm0 := %xmm0 - (((G - x^n) * x^(64-n)) * floor(t0 / G))
	//	crc := (%xmm0 / x^(64-n)) mod x^n
	//
	// In the lsb-first case, each pclmulqdq implicitly introduces
	// an extra factor of x, so in that case the constant that needs to be
	// passed to pclmulqdq is actually '(G - x^n) * x^(63-n)' when n <= 63.
	// For lsb-first CRCs where n=64, the extra factor of x cannot be as
	// easily avoided.  In that case, instead pass '(G - x^n - x^0) / x' to
	// pclmulqdq and handle the x^0 term (i.e. 1) separately.  (All CRC
	// polynomials have nonzero x^n and x^0 terms.)  It works out as: the
	// CRC has be XORed with the physically low qword of %xmm1, representing
	// floor(t0 / G).  The most efficient way to do that is to move it to
	// the physically high qword and use a ternlog to combine the two XORs.
.if LSB_CRC && \n == 64
	_cond_vex punpcklqdq,	%xmm1, %xmm2, %xmm2
	_pclmulqdq		CONSTS_XMM, LO64_TERMS, %xmm1, HI64_TERMS, %xmm1
    .if AVX_LEVEL <= 2
	_cond_vex pxor,		%xmm2, %xmm0, %xmm0
	_cond_vex pxor,		%xmm1, %xmm0, %xmm0
    .else
	vpternlogq		$0x96, %xmm2, %xmm1, %xmm0
    .endif
	_cond_vex "pextrq $1,",	%xmm0, %rax  // (%xmm0 / x^0) mod x^64
.else
	_pclmulqdq		CONSTS_XMM, LO64_TERMS, %xmm1, HI64_TERMS, %xmm1
	_cond_vex pxor,		%xmm1, %xmm0, %xmm0
  .if \n == 8
	_cond_vex "pextrb $7 + LSB_CRC,", %xmm0, %eax // (%xmm0 / x^56) mod x^8
  .elseif \n == 16
	_cond_vex "pextrw $3 + LSB_CRC,", %xmm0, %eax // (%xmm0 / x^48) mod x^16
  .elseif \n == 32
	_cond_vex "pextrd $1 + LSB_CRC,", %xmm0, %eax // (%xmm0 / x^32) mod x^32
  .else // \n == 64 && !LSB_CRC
	_cond_vex movq,		%xmm0, %rax  // (%xmm0 / x^0) mod x^64
  .endif
.endif

.if VL > 16
	vzeroupper	// Needed when ymm or zmm registers may have been used.
.endif
#ifdef __i386__
	pop		CONSTS_PTR
#endif
	RET
.endm

#ifdef CONFIG_AS_VPCLMULQDQ
#define DEFINE_CRC_PCLMUL_FUNCS(prefix, bits, lsb)			\
SYM_FUNC_START(prefix##_pclmul_sse);					\
	_crc_pclmul	n=bits, lsb_crc=lsb, vl=16, avx_level=0;	\
SYM_FUNC_END(prefix##_pclmul_sse);					\
									\
SYM_FUNC_START(prefix##_vpclmul_avx2);					\
	_crc_pclmul	n=bits, lsb_crc=lsb, vl=32, avx_level=2;	\
SYM_FUNC_END(prefix##_vpclmul_avx2);					\
									\
SYM_FUNC_START(prefix##_vpclmul_avx512);				\
	_crc_pclmul	n=bits, lsb_crc=lsb, vl=64, avx_level=512;	\
SYM_FUNC_END(prefix##_vpclmul_avx512);
#else
#define DEFINE_CRC_PCLMUL_FUNCS(prefix, bits, lsb)			\
SYM_FUNC_START(prefix##_pclmul_sse);					\
	_crc_pclmul	n=bits, lsb_crc=lsb, vl=16, avx_level=0;	\
SYM_FUNC_END(prefix##_pclmul_sse);
#endif // !CONFIG_AS_VPCLMULQDQ
