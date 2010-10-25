#include <linux/linkage.h>

/*
 * Multiply operation for 32 bit integers.
 *	Input :	Operand1 in Reg r5
 *		Operand2 in Reg r6
 *	Output: Result [op1 * op2] in Reg r3
 */
	.text
	.globl	__mulsi3
	.type __mulsi3,  @function
	.ent __mulsi3

__mulsi3:
	.frame	r1, 0, r15
	add	r3, r0, r0
	beqi	r5, result_is_zero /* multiply by zero */
	beqi	r6, result_is_zero /* multiply by zero */
	bgeid	r5, r5_pos
	xor	r4, r5, r6 /* get the sign of the result */
	rsubi	r5, r5, 0 /* make r5 positive */
r5_pos:
	bgei	r6, r6_pos
	rsubi	r6, r6, 0 /* make r6 positive */
r6_pos:
	bri	l1
l2:
	add	r5, r5, r5
l1:
	srl	r6, r6
	addc	r7, r0, r0
	beqi	r7, l2
	bneid	r6, l2
	add	r3, r3, r5
	blti	r4, negateresult
	rtsd	r15, 8
	nop
negateresult:
	rtsd	r15, 8
	rsub	r3, r3, r0
result_is_zero:
	rtsd	r15, 8
	addi	r3, r0, 0

.size __mulsi3,  . - __mulsi3
.end __mulsi3
