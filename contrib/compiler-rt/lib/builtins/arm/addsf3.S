/*===-- addsf3.S - Adds two single precision floating pointer numbers-----===//
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===//
 *
 * This file implements the __addsf3 (single precision floating pointer number
 * addition with the IEEE-754 default rounding (to nearest, ties to even)
 * function for the ARM Thumb1 ISA.
 *
 *===----------------------------------------------------------------------===*/

#include "../assembly.h"
#define significandBits 23
#define typeWidth 32

	.syntax unified
	.text
  .thumb
  .p2align 2

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_fadd, __addsf3)

DEFINE_COMPILERRT_THUMB_FUNCTION(__addsf3)
  push {r4, r5, r6, r7, lr}
  // Get the absolute value of a and b.
  lsls r2, r0, #1
  lsls r3, r1, #1
  lsrs r2, r2, #1  /* aAbs */
  beq  LOCAL_LABEL(a_zero_nan_inf)
  lsrs r3, r3, #1  /* bAbs */
  beq  LOCAL_LABEL(zero_nan_inf)

  // Detect if a or b is infinity or Nan.
  lsrs r6, r2, #(significandBits)
  lsrs r7, r3, #(significandBits)
  cmp  r6, #0xFF
  beq  LOCAL_LABEL(zero_nan_inf)
  cmp  r7, #0xFF
  beq  LOCAL_LABEL(zero_nan_inf)

  // Swap Rep and Abs so that a and aAbs has the larger absolute value.
  cmp r2, r3
  bhs LOCAL_LABEL(no_swap)
  movs r4, r0
  movs r5, r2
  movs r0, r1
  movs r2, r3
  movs r1, r4
  movs r3, r5
LOCAL_LABEL(no_swap):

  // Get the significands and shift them to give us round, guard and sticky.
  lsls r4, r0, #(typeWidth - significandBits)
  lsrs r4, r4, #(typeWidth - significandBits - 3) /* aSignificand << 3 */
  lsls r5, r1, #(typeWidth - significandBits)
  lsrs r5, r5, #(typeWidth - significandBits - 3) /* bSignificand << 3 */

  // Get the implicitBit.
  movs r6, #1
  lsls r6, r6, #(significandBits + 3)

  // Get aExponent and set implicit bit if necessary.
  lsrs r2, r2, #(significandBits)
  beq LOCAL_LABEL(a_done_implicit_bit)
  orrs r4, r6
LOCAL_LABEL(a_done_implicit_bit):

  // Get bExponent and set implicit bit if necessary.
  lsrs r3, r3, #(significandBits)
  beq LOCAL_LABEL(b_done_implicit_bit)
  orrs r5, r6
LOCAL_LABEL(b_done_implicit_bit):

  // Get the difference in exponents.
  subs r6, r2, r3
  beq LOCAL_LABEL(done_align)

  // If b is denormal, then a must be normal as align > 0, and we only need to
  // right shift bSignificand by (align - 1) bits.
  cmp  r3, #0
  bne  1f
  subs r6, r6, #1
1:

  // No longer needs bExponent. r3 is dead here.
  // Set sticky bits of b: sticky = bSignificand << (typeWidth - align).
  movs r3, #(typeWidth)
  subs r3, r3, r6
  movs r7, r5
  lsls r7, r3
  beq 1f
  movs r7, #1
1:

  // bSignificand = bSignificand >> align | sticky;
  lsrs r5, r6
  orrs r5, r7
  bne LOCAL_LABEL(done_align)
  movs r5, #1 //  sticky; b is known to be non-zero.

LOCAL_LABEL(done_align):
  // isSubtraction = (aRep ^ bRep) >> 31;
  movs r7, r0
  eors r7, r1
  lsrs r7, #31
  bne LOCAL_LABEL(do_substraction)

  // Same sign, do Addition.

  // aSignificand += bSignificand;
  adds r4, r4, r5

  // Check carry bit.
  movs r6, #1
  lsls r6, r6, #(significandBits + 3 + 1)
  movs r7, r4
  ands r7, r6
  beq LOCAL_LABEL(form_result)
  // If the addition carried up, we need to right-shift the result and
  // adjust the exponent.
  movs r7, r4
  movs r6, #1
  ands r7, r6 // sticky = aSignificand & 1;
  lsrs r4, #1
  orrs r4, r7  // result Significand
  adds r2, #1  // result Exponent
  // If we have overflowed the type, return +/- infinity.
  cmp  r2, 0xFF
  beq  LOCAL_LABEL(ret_inf)

LOCAL_LABEL(form_result):
  // Shift the sign, exponent and significand into place.
  lsrs r0, #(typeWidth - 1)
  lsls r0, #(typeWidth - 1) // Get Sign.
  lsls r2, #(significandBits)
  orrs r0, r2
  movs r1, r4
  lsls r4, #(typeWidth - significandBits - 3)
  lsrs r4, #(typeWidth - significandBits)
  orrs r0, r4

  // Final rounding.  The result may overflow to infinity, but that is the
  // correct result in that case.
  // roundGuardSticky = aSignificand & 0x7;
  movs r2, #0x7
  ands r1, r2
  // if (roundGuardSticky > 0x4) result++;

  cmp r1, #0x4
  blt LOCAL_LABEL(done_round)
  beq 1f
  adds r0, #1
  pop {r4, r5, r6, r7, pc}
1:

  // if (roundGuardSticky == 0x4) result += result & 1;
  movs r1, r0
  lsrs r1, #1
  bcc  LOCAL_LABEL(done_round)
  adds r0, r0, #1
LOCAL_LABEL(done_round):
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(do_substraction):
  subs r4, r4, r5 // aSignificand -= bSignificand;
  beq  LOCAL_LABEL(ret_zero)
  movs r6, r4
  cmp  r2, 0
  beq  LOCAL_LABEL(form_result) // if a's exp is 0, no need to normalize.
  // If partial cancellation occured, we need to left-shift the result
  // and adjust the exponent:
  lsrs r6, r6, #(significandBits + 3)
  bne LOCAL_LABEL(form_result)

  push {r0, r1, r2, r3}
  movs r0, r4
  bl   SYMBOL_NAME(__clzsi2)
  movs r5, r0
  pop {r0, r1, r2, r3}
  // shift = rep_clz(aSignificand) - rep_clz(implicitBit << 3);
  subs r5, r5, #(typeWidth - significandBits - 3 - 1)
  // aSignificand <<= shift; aExponent -= shift;
  lsls r4, r5
  subs  r2, r2, r5
  bgt LOCAL_LABEL(form_result)

  // Do normalization if aExponent <= 0.
  movs r6, #1
  subs r6, r6, r2 // 1 - aExponent;
  movs r2, #0 // aExponent = 0;
  movs r3, #(typeWidth) // bExponent is dead.
  subs r3, r3, r6
  movs r7, r4
  lsls r7, r3  // stickyBit = (bool)(aSignificant << (typeWidth - align))
  beq 1f
  movs r7, #1
1:
  lsrs r4, r6 /* aSignificand >> shift */
  orrs r4, r7
  b LOCAL_LABEL(form_result)

LOCAL_LABEL(ret_zero):
  movs r0, #0
  pop {r4, r5, r6, r7, pc}


LOCAL_LABEL(a_zero_nan_inf):
  lsrs r3, r3, #1

LOCAL_LABEL(zero_nan_inf):
  // Here  r2 has aAbs, r3 has bAbs
  movs r4, #0xFF
  lsls r4, r4, #(significandBits) // Make +inf.

  cmp r2, r4
  bhi LOCAL_LABEL(a_is_nan)
  cmp r3, r4
  bhi LOCAL_LABEL(b_is_nan)

  cmp r2, r4
  bne LOCAL_LABEL(a_is_rational)
  // aAbs is INF.
  eors r1, r0 // aRep ^ bRep.
  movs r6, #1
  lsls r6, r6, #(typeWidth - 1) // get sign mask.
  cmp r1, r6 // if they only differ on sign bit, it's -INF + INF
  beq LOCAL_LABEL(a_is_nan)
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(a_is_rational):
  cmp r3, r4
  bne LOCAL_LABEL(b_is_rational)
  movs r0, r1
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(b_is_rational):
  // either a or b or both are zero.
  adds r4, r2, r3
  beq  LOCAL_LABEL(both_zero)
  cmp r2, #0 // is absA 0 ?
  beq LOCAL_LABEL(ret_b)
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(both_zero):
  ands r0, r1 // +0 + -0 = +0
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(ret_b):
  movs r0, r1

LOCAL_LABEL(ret):
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(b_is_nan):
  movs r0, r1
LOCAL_LABEL(a_is_nan):
  movs r1, #1
  lsls r1, r1, #(significandBits -1) // r1 is quiet bit.
  orrs r0, r1
  pop {r4, r5, r6, r7, pc}

LOCAL_LABEL(ret_inf):
  movs r4, #0xFF
  lsls r4, r4, #(significandBits)
  orrs r0, r4
  lsrs r0, r0, #(significandBits)
  lsls r0, r0, #(significandBits)
  pop {r4, r5, r6, r7, pc}


END_COMPILERRT_FUNCTION(__addsf3)

NO_EXEC_STACK_DIRECTIVE
