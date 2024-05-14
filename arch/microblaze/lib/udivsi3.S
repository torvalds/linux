/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

/*
* Unsigned divide operation.
*	Input :	Divisor in Reg r5
*		Dividend in Reg r6
*	Output: Result in Reg r3
*/

	.text
	.globl	__udivsi3
	.type __udivsi3, @function
	.ent __udivsi3

__udivsi3:

	.frame	r1, 0, r15

	addik	r1, r1, -12
	swi	r29, r1, 0
	swi	r30, r1, 4
	swi	r31, r1, 8

	beqi	r6, div_by_zero /* div_by_zero /* division error */
	beqid	r5, result_is_zero /* result is zero */
	addik	r30, r0, 0 /* clear mod */
	addik	r29, r0, 32 /* initialize the loop count */

/* check if r6 and r5 are equal - if yes, return 1 */
	rsub	r18, r5, r6
	beqid	r18, return_here
	addik	r3, r0, 1

/* check if (uns)r6 is greater than (uns)r5. in that case, just return 0 */
	xor	r18, r5, r6
	bgeid	r18, 16
	add	r3, r0, r0 /* we would anyways clear r3 */
	blti	r6, return_here /* r6[bit 31 = 1] hence is greater */
	bri	checkr6
	rsub	r18, r6, r5 /* microblazecmp */
	blti	r18, return_here

/* if r6 [bit 31] is set, then return result as 1 */
checkr6:
	bgti	r6, div0
	brid	return_here
	addik	r3, r0, 1

/* first part try to find the first '1' in the r5 */
div0:
	blti	r5, div2
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
	bri	return_here
div_by_zero:
result_is_zero:
	or	r3, r0, r0 /* set result to 0 */
return_here:
/* restore values of csrs and that of r3 and the divisor and the dividend */
	lwi	r29, r1, 0
	lwi	r30, r1, 4
	lwi	r31, r1, 8
	rtsd	r15, 8
	addik	r1, r1, 12

.size __udivsi3, . - __udivsi3
.end __udivsi3
