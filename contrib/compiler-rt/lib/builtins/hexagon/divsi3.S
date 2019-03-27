//===----------------------Hexagon builtin routine ------------------------===//
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


FUNCTION_BEGIN __hexagon_divsi3
	{
		p0 = cmp.ge(r0,#0)
		p1 = cmp.ge(r1,#0)
		r1 = abs(r0)
		r2 = abs(r1)
	}
	{
		r3 = cl0(r1)
		r4 = cl0(r2)
		r5 = sub(r1,r2)
		p2 = cmp.gtu(r2,r1)
	}
#if (__HEXAGON_ARCH__ == 60)
	{
		r0 = #0
		p1 = xor(p0,p1)
		p0 = cmp.gtu(r2,r5)
	}
		if (p2) jumpr r31
#else
	{
		r0 = #0
		p1 = xor(p0,p1)
		p0 = cmp.gtu(r2,r5)
		if (p2) jumpr r31
	}
#endif
	{
		r0 = mux(p1,#-1,#1)
		if (p0) jumpr r31
		r4 = sub(r4,r3)
		r3 = #1
	}
	{
		r0 = #0
		r3:2 = vlslw(r3:2,r4)
		loop0(1f,r4)
	}
	.falign
1:
	{
		p0 = cmp.gtu(r2,r1)
		if (!p0.new) r1 = sub(r1,r2)
		if (!p0.new) r0 = add(r0,r3)
		r3:2 = vlsrw(r3:2,#1)
	}:endloop0
	{
		p0 = cmp.gtu(r2,r1)
		if (!p0.new) r0 = add(r0,r3)
		if (!p1) jumpr r31
	}
	{
		r0 = neg(r0)
		jumpr r31
	}
FUNCTION_END __hexagon_divsi3

  .globl __qdsp_divsi3
  .set   __qdsp_divsi3, __hexagon_divsi3
