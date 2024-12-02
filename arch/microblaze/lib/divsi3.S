/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

/*
* Divide operation for 32 bit integers.
*	Input :	Dividend in Reg r5
*		Divisor in Reg r6
*	Output: Result in Reg r3
*/
	.text
	.globl	__divsi3
	.type __divsi3, @function
	.ent __divsi3
__divsi3:
	.frame	r1, 0, r15

	addik	r1, r1, -16
	swi	r28, r1, 0
	swi	r29, r1, 4
	swi	r30, r1, 8
	swi	r31, r1, 12

	beqi	r6, div_by_zero /* div_by_zero - division error */
	beqi	r5, result_is_zero /* result is zero */
	bgeid	r5, r5_pos
	xor	r28, r5, r6 /* get the sign of the result */
	rsubi	r5, r5, 0 /* make r5 positive */
r5_pos:
	bgei	r6, r6_pos
	rsubi	r6, r6, 0 /* make r6 positive */
r6_pos:
	addik	r30, r0, 0 /* clear mod */
	addik	r3, r0, 0 /* clear div */
	addik	r29, r0, 32 /* initialize the loop count */

	/* first part try to find the first '1' in the r5 */
div0:
	blti	r5, div2 /* this traps r5 == 0x80000000 */
div1:
	add	r5, r5, r5 /* left shift logical r5 */
	bgtid	r5, div1
	addik	r29, r29, -1
div2:
	/* left shift logical r5 get the '1' into the carry */
	add	r5, r5, r5
	addc	r30, r30, r30 /* move that bit into the mod register */
	rsub	r31, r6, r30 /* try to subtract (r30 a r6) */
	blti	r31, mod_too_small
	/* move the r31 to mod since the result was positive */
	or	r30, r0, r31
	addik	r3, r3, 1
mod_too_small:
	addik	r29, r29, -1
	beqi	r29, loop_end
	add	r3, r3, r3 /* shift in the '1' into div */
	bri	div2 /* div2 */
loop_end:
	bgei	r28, return_here
	brid	return_here
	rsubi	r3, r3, 0 /* negate the result */
div_by_zero:
result_is_zero:
	or	r3, r0, r0 /* set result to 0 */
return_here:
/* restore values of csrs and that of r3 and the divisor and the dividend */
	lwi	r28, r1, 0
	lwi	r29, r1, 4
	lwi	r30, r1, 8
	lwi	r31, r1, 12
	rtsd	r15, 8
	addik	r1, r1, 16

.size __divsi3, . - __divsi3
.end __divsi3
