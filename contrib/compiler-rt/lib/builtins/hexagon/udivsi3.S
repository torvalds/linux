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


FUNCTION_BEGIN __hexagon_udivsi3
	{
		r2 = cl0(r0)
		r3 = cl0(r1)
		r5:4 = combine(#1,#0)
		p0 = cmp.gtu(r1,r0)
	}
	{
		r6 = sub(r3,r2)
		r4 = r1
		r1:0 = combine(r0,r4)
		if (p0) jumpr r31
	}
	{
		r3:2 = vlslw(r5:4,r6)
		loop0(1f,r6)
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
		jumpr r31
	}
FUNCTION_END __hexagon_udivsi3

  .globl __qdsp_udivsi3
  .set   __qdsp_udivsi3, __hexagon_udivsi3
