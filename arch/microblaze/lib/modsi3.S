#include <linux/linkage.h>

/*
* modulo operation for 32 bit integers.
*	Input :	op1 in Reg r5
*		op2 in Reg r6
*	Output: op1 mod op2 in Reg r3
*/

	.text
	.globl	__modsi3
	.type __modsi3,  @function
	.ent __modsi3

__modsi3:
	.frame	r1, 0, r15

	addik	r1, r1, -16
	swi	r28, r1, 0
	swi	r29, r1, 4
	swi	r30, r1, 8
	swi	r31, r1, 12

	beqi	r6, div_by_zero /* div_by_zero division error */
	beqi	r5, result_is_zero /* result is zero */
	bgeid	r5, r5_pos
	/* get the sign of the result [ depends only on the first arg] */
	add	r28, r5, r0
	rsubi	r5, r5, 0	 /* make r5 positive */
r5_pos:
	bgei	r6, r6_pos
	rsubi	r6, r6, 0	 /* make r6 positive */
r6_pos:
	addik	r3, r0, 0 /* clear mod */
	addik	r30, r0, 0 /* clear div */
	addik	r29, r0, 32 /* initialize the loop count */
/* first part try to find the first '1' in the r5 */
div1:
	add	r5, r5, r5 /* left shift logical r5 */
	bgeid	r5, div1
	addik	r29, r29, -1
div2:
	/* left shift logical r5 get the '1' into the carry */
	add	r5, r5, r5
	addc	r3, r3, r3 /* move that bit into the mod register */
	rsub	r31, r6, r3 /* try to subtract (r30 a r6) */
	blti	r31, mod_too_small
	/* move the r31 to mod since the result was positive */
	or	r3, r0, r31
	addik	r30, r30, 1
mod_too_small:
	addik	r29, r29, -1
	beqi	r29, loop_end
	add	r30, r30, r30 /* shift in the '1' into div */
	bri	div2 /* div2 */
loop_end:
	bgei	r28, return_here
	brid	return_here
	rsubi	r3, r3, 0 /* negate the result */
div_by_zero:
result_is_zero:
	or	r3, r0, r0 /* set result to 0 [both mod as well as div are 0] */
return_here:
/* restore values of csrs and that of r3 and the divisor and the dividend */
	lwi	r28, r1, 0
	lwi	r29, r1, 4
	lwi	r30, r1, 8
	lwi	r31, r1, 12
	rtsd	r15, 8
	addik	r1, r1, 16

.size __modsi3,  . - __modsi3
.end __modsi3
