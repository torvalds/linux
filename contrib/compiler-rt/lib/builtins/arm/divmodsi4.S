/*===-- divmodsi4.S - 32-bit signed integer divide and modulus ------------===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __divmodsi4 (32-bit signed integer divide and
 * modulus) function for the ARM architecture.  A naive digit-by-digit
 * computation is employed for simplicity.
 *
 *===----------------------------------------------------------------------===*/

#include "../assembly.h"

#define ESTABLISH_FRAME    \
    push   {r4-r7, lr}   ;\
    add     r7,     sp, #12
#define CLEAR_FRAME_AND_RETURN \
    pop    {r4-r7, pc}

	.syntax unified
	.text
  DEFINE_CODE_STATE

@ int __divmodsi4(int divident, int divisor, int *remainder)
@   Calculate the quotient and remainder of the (signed) division.  The return
@   value is the quotient, the remainder is placed in the variable.

	.p2align 3
DEFINE_COMPILERRT_FUNCTION(__divmodsi4)
#if __ARM_ARCH_EXT_IDIV__
	tst     r1, r1
	beq     LOCAL_LABEL(divzero)
	mov 	r3, r0
	sdiv	r0, r3, r1
	mls 	r1, r0, r1, r3
	str 	r1, [r2]
	bx  	lr
LOCAL_LABEL(divzero):
	mov     r0, #0
	bx      lr
#else
    ESTABLISH_FRAME
//  Set aside the sign of the quotient and modulus, and the address for the
//  modulus.
    eor     r4,     r0, r1
    mov     r5,     r0
    mov     r6,     r2
//  Take the absolute value of a and b via abs(x) = (x^(x >> 31)) - (x >> 31).
    eor     ip,     r0, r0, asr #31
    eor     lr,     r1, r1, asr #31
    sub     r0,     ip, r0, asr #31
    sub     r1,     lr, r1, asr #31
//  Unsigned divmod:
    bl      SYMBOL_NAME(__udivmodsi4)
//  Apply the sign of quotient and modulus
    ldr     r1,    [r6]
    eor     r0,     r0, r4, asr #31
    eor     r1,     r1, r5, asr #31
    sub     r0,     r0, r4, asr #31
    sub     r1,     r1, r5, asr #31
    str     r1,    [r6]
    CLEAR_FRAME_AND_RETURN
#endif
END_COMPILERRT_FUNCTION(__divmodsi4)

NO_EXEC_STACK_DIRECTIVE

