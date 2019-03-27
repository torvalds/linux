//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

/* Functions that implement common sequences in function prologues and epilogues
   used to save code size */

	.macro FUNCTION_BEGIN name
	.text
	.globl \name
	.type  \name, @function
	.falign
\name:
	.endm

	.macro FUNCTION_END name
	.size  \name, . - \name
	.endm

	.macro FALLTHROUGH_TAIL_CALL name0 name1
	.size \name0, . - \name0
	.globl \name1
	.type \name1, @function
	.falign
\name1:
	.endm




/* Save r25:24 at fp+#-8 and r27:26 at fp+#-16. */




/* The compiler knows that the __save_* functions clobber LR.  No other
   registers should be used without informing the compiler. */

/* Since we can only issue one store per packet, we don't hurt performance by
   simply jumping to the right point in this sequence of stores. */

FUNCTION_BEGIN __save_r24_through_r27
		memd(fp+#-16) = r27:26
FALLTHROUGH_TAIL_CALL __save_r24_through_r27 __save_r24_through_r25
	{
		memd(fp+#-8) = r25:24
		jumpr lr
	}
FUNCTION_END __save_r24_through_r25




/* For each of the *_before_tailcall functions, jumpr lr is executed in parallel
   with deallocframe.  That way, the return gets the old value of lr, which is
   where these functions need to return, and at the same time, lr gets the value
   it needs going into the tail call. */

FUNCTION_BEGIN __restore_r24_through_r27_and_deallocframe_before_tailcall
		r27:26 = memd(fp+#-16)
FALLTHROUGH_TAIL_CALL __restore_r24_through_r27_and_deallocframe_before_tailcall __restore_r24_through_r25_and_deallocframe_before_tailcall
	{
		r25:24 = memd(fp+#-8)
		deallocframe
		jumpr lr
	}
FUNCTION_END __restore_r24_through_r25_and_deallocframe_before_tailcall




/* Here we use the extra load bandwidth to restore LR early, allowing the return
   to occur in parallel with the deallocframe. */

FUNCTION_BEGIN __restore_r24_through_r27_and_deallocframe
	{
		lr = memw(fp+#4)
		r27:26 = memd(fp+#-16)
	}
	{
		r25:24 = memd(fp+#-8)
		deallocframe
		jumpr lr
	}
FUNCTION_END __restore_r24_through_r27_and_deallocframe




/* Here the load bandwidth is maximized. */

FUNCTION_BEGIN __restore_r24_through_r25_and_deallocframe
	{
		r25:24 = memd(fp+#-8)
		deallocframe
	}
		jumpr lr
FUNCTION_END __restore_r24_through_r25_and_deallocframe
