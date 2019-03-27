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


FUNCTION_BEGIN __hexagon_moddi3
	{
		p3 = tstbit(r1,#31)
	}
	{
		r1:0 = abs(r1:0)
		r3:2 = abs(r3:2)
	}
	{
		r6 = cl0(r1:0)              // count leading 0's of dividend (numerator)
		r7 = cl0(r3:2)              // count leading 0's of divisor (denominator)
		r5:4 = r3:2                 // divisor moved into working registers
		r3:2 = r1:0                 // dividend is the initial remainder, r3:2 contains remainder
	}
	{
		r10 = sub(r7,r6)            // left shift count for bit & divisor
		r1:0 = #0                   // initialize quotient to 0
		r15:14 = #1                 // initialize bit to 1
	}
	{
		r11 = add(r10,#1)           // loop count is 1 more than shift count
		r13:12 = lsl(r5:4,r10)      // shift divisor msb into same bit position as dividend msb
		r15:14 = lsl(r15:14,r10)    // shift the bit left by same amount as divisor
	}
	{
		p0 = cmp.gtu(r5:4,r3:2)     // check if divisor > dividend
		loop0(1f,r11)               // register loop
	}
	{
		if (p0) jump .hexagon_moddi3_return          // if divisor > dividend, we're done, so return
	}
	.falign
1:
	{
		p0 = cmp.gtu(r13:12,r3:2)   // set predicate reg if shifted divisor > current remainder
	}
	{
		r7:6 = sub(r3:2, r13:12)    // subtract shifted divisor from current remainder
		r9:8 = add(r1:0, r15:14)    // save current quotient to temp (r9:8)
	}
	{
		r1:0 = vmux(p0, r1:0, r9:8) // choose either current quotient or new quotient (r9:8)
		r3:2 = vmux(p0, r3:2, r7:6) // choose either current remainder or new remainder (r7:6)
	}
	{
		r15:14 = lsr(r15:14, #1)    // shift bit right by 1 for next iteration
		r13:12 = lsr(r13:12, #1)    // shift "shifted divisor" right by 1 for next iteration
	}:endloop0

.hexagon_moddi3_return:
	{
		r1:0 = neg(r3:2)
	}
	{
		r1:0 = vmux(p3,r1:0,r3:2)
		jumpr r31
	}
FUNCTION_END __hexagon_moddi3

  .globl __qdsp_moddi3
  .set   __qdsp_moddi3, __hexagon_moddi3
