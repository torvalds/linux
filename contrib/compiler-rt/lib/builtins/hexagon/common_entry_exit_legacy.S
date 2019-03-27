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




/* Save r27:26 at fp+#-8, r25:24 at fp+#-16, r23:22 at fp+#-24, r21:20 at
   fp+#-32, r19:18 at fp+#-40, and r17:16 at fp+#-48. */




/* The compiler knows that the __save_* functions clobber LR.  No other
   registers should be used without informing the compiler. */

/* Since we can only issue one store per packet, we don't hurt performance by
   simply jumping to the right point in this sequence of stores. */

FUNCTION_BEGIN __save_r27_through_r16
		memd(fp+#-48) = r17:16
FALLTHROUGH_TAIL_CALL __save_r27_through_r16 __save_r27_through_r18
		memd(fp+#-40) = r19:18
FALLTHROUGH_TAIL_CALL __save_r27_through_r18 __save_r27_through_r20
		memd(fp+#-32) = r21:20
FALLTHROUGH_TAIL_CALL __save_r27_through_r20 __save_r27_through_r22
		memd(fp+#-24) = r23:22
FALLTHROUGH_TAIL_CALL __save_r27_through_r22 __save_r27_through_r24
		memd(fp+#-16) = r25:24
	{
		memd(fp+#-8) = r27:26
		jumpr lr
	}
FUNCTION_END __save_r27_through_r24




/* For each of the *_before_sibcall functions, jumpr lr is executed in parallel
   with deallocframe.  That way, the return gets the old value of lr, which is
   where these functions need to return, and at the same time, lr gets the value
   it needs going into the sibcall. */

FUNCTION_BEGIN __restore_r27_through_r20_and_deallocframe_before_sibcall
	{
		r21:20 = memd(fp+#-32)
		r23:22 = memd(fp+#-24)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r20_and_deallocframe_before_sibcall __restore_r27_through_r24_and_deallocframe_before_sibcall
	{
		r25:24 = memd(fp+#-16)
		jump __restore_r27_through_r26_and_deallocframe_before_sibcall
	}
FUNCTION_END __restore_r27_through_r24_and_deallocframe_before_sibcall




FUNCTION_BEGIN __restore_r27_through_r16_and_deallocframe_before_sibcall
		r17:16 = memd(fp+#-48)
FALLTHROUGH_TAIL_CALL __restore_r27_through_r16_and_deallocframe_before_sibcall __restore_r27_through_r18_and_deallocframe_before_sibcall
	{
		r19:18 = memd(fp+#-40)
		r21:20 = memd(fp+#-32)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r18_and_deallocframe_before_sibcall __restore_r27_through_r22_and_deallocframe_before_sibcall
	{
		r23:22 = memd(fp+#-24)
		r25:24 = memd(fp+#-16)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r22_and_deallocframe_before_sibcall __restore_r27_through_r26_and_deallocframe_before_sibcall
	{
		r27:26 = memd(fp+#-8)
		deallocframe
		jumpr lr
	}
FUNCTION_END __restore_r27_through_r26_and_deallocframe_before_sibcall




/* Here we use the extra load bandwidth to restore LR early, allowing the return
   to occur in parallel with the deallocframe. */

FUNCTION_BEGIN __restore_r27_through_r16_and_deallocframe
	{
		r17:16 = memd(fp+#-48)
		r19:18 = memd(fp+#-40)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r16_and_deallocframe __restore_r27_through_r20_and_deallocframe
	{
		r21:20 = memd(fp+#-32)
		r23:22 = memd(fp+#-24)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r20_and_deallocframe __restore_r27_through_r24_and_deallocframe
	{
		lr = memw(fp+#4)
		r25:24 = memd(fp+#-16)
	}
	{
		r27:26 = memd(fp+#-8)
		deallocframe
		jumpr lr
	}
FUNCTION_END __restore_r27_through_r24_and_deallocframe




/* Here the load bandwidth is maximized for all three functions. */

FUNCTION_BEGIN __restore_r27_through_r18_and_deallocframe
	{
		r19:18 = memd(fp+#-40)
		r21:20 = memd(fp+#-32)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r18_and_deallocframe __restore_r27_through_r22_and_deallocframe
	{
		r23:22 = memd(fp+#-24)
		r25:24 = memd(fp+#-16)
	}
FALLTHROUGH_TAIL_CALL __restore_r27_through_r22_and_deallocframe __restore_r27_through_r26_and_deallocframe
	{
		r27:26 = memd(fp+#-8)
		deallocframe
	}
		jumpr lr
FUNCTION_END __restore_r27_through_r26_and_deallocframe
