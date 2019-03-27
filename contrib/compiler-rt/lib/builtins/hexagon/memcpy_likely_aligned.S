//===------------------------- memcopy routines ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


	.macro FUNCTION_BEGIN name
	.text
	.p2align 5
	.globl \name
	.type  \name, @function
\name:
	.endm

	.macro FUNCTION_END name
	.size  \name, . - \name
	.endm

FUNCTION_BEGIN __hexagon_memcpy_likely_aligned_min32bytes_mult8bytes
	{
		p0 = bitsclr(r1,#7)
		p0 = bitsclr(r0,#7)
		if (p0.new) r5:4 = memd(r1)
		r3 = #-3
	}
	{
		if (!p0) jump .Lmemcpy_call
		if (p0) memd(r0++#8) = r5:4
		if (p0) r5:4 = memd(r1+#8)
		r3 += lsr(r2,#3)
	}
	{
		memd(r0++#8) = r5:4
		r5:4 = memd(r1+#16)
		r1 = add(r1,#24)
		loop0(1f,r3)
	}
	.falign
1:
	{
		memd(r0++#8) = r5:4
		r5:4 = memd(r1++#8)
	}:endloop0
	{
		memd(r0) = r5:4
		r0 -= add(r2,#-8)
		jumpr r31
	}
FUNCTION_END __hexagon_memcpy_likely_aligned_min32bytes_mult8bytes

.Lmemcpy_call:
#ifdef __PIC__
	jump memcpy@PLT
#else
	jump memcpy
#endif

  .globl __qdsp_memcpy_likely_aligned_min32bytes_mult8bytes
  .set   __qdsp_memcpy_likely_aligned_min32bytes_mult8bytes, \
         __hexagon_memcpy_likely_aligned_min32bytes_mult8bytes
