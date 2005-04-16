/*
 * linux/arch/alpha/memset.S
 *
 * This is an efficient (and small) implementation of the C library "memset()"
 * function for the alpha.
 *
 *	(C) Copyright 1996 Linus Torvalds
 *
 * This routine is "moral-ware": you are free to use it any way you wish, and
 * the only obligation I put on you is a moral one: if you make any improvements
 * to the routine, please send me your improvements for me to use similarly.
 *
 * The scheduling comments are according to the EV5 documentation (and done by 
 * hand, so they might well be incorrect, please do tell me about it..)
 */

	.set noat
	.set noreorder
.text
	.globl memset
	.globl __memset
	.globl __memsetw
	.globl __constant_c_memset
	.ent __memset
.align 5
__memset:
	.frame $30,0,$26,0
	.prologue 0

	and $17,255,$1		/* E1 */
	insbl $17,1,$17		/* .. E0 */
	bis $17,$1,$17		/* E0 (p-c latency, next cycle) */
	sll $17,16,$1		/* E1 (p-c latency, next cycle) */

	bis $17,$1,$17		/* E0 (p-c latency, next cycle) */
	sll $17,32,$1		/* E1 (p-c latency, next cycle) */
	bis $17,$1,$17		/* E0 (p-c latency, next cycle) */
	ldq_u $31,0($30)	/* .. E1 */

.align 5
__constant_c_memset:
	addq $18,$16,$6		/* E0 */
	bis $16,$16,$0		/* .. E1 */
	xor $16,$6,$1		/* E0 */
	ble $18,end		/* .. E1 */

	bic $1,7,$1		/* E0 */
	beq $1,within_one_quad	/* .. E1 (note EV5 zero-latency forwarding) */
	and $16,7,$3		/* E0 */
	beq $3,aligned		/* .. E1 (note EV5 zero-latency forwarding) */

	ldq_u $4,0($16)		/* E0 */
	bis $16,$16,$5		/* .. E1 */
	insql $17,$16,$2	/* E0 */
	subq $3,8,$3		/* .. E1 */

	addq $18,$3,$18		/* E0	$18 is new count ($3 is negative) */
	mskql $4,$16,$4		/* .. E1 (and possible load stall) */
	subq $16,$3,$16		/* E0 	$16 is new aligned destination */
	bis $2,$4,$1		/* .. E1 */

	bis $31,$31,$31		/* E0 */
	ldq_u $31,0($30)	/* .. E1 */
	stq_u $1,0($5)		/* E0 */
	bis $31,$31,$31		/* .. E1 */

.align 4
aligned:
	sra $18,3,$3		/* E0 */
	and $18,7,$18		/* .. E1 */
	bis $16,$16,$5		/* E0 */
	beq $3,no_quad		/* .. E1 */

.align 3
loop:
	stq $17,0($5)		/* E0 */
	subq $3,1,$3		/* .. E1 */
	addq $5,8,$5		/* E0 */
	bne $3,loop		/* .. E1 */

no_quad:
	bis $31,$31,$31		/* E0 */
	beq $18,end		/* .. E1 */
	ldq $7,0($5)		/* E0 */
	mskqh $7,$6,$2		/* .. E1 (and load stall) */

	insqh $17,$6,$4		/* E0 */
	bis $2,$4,$1		/* .. E1 */
	stq $1,0($5)		/* E0 */
	ret $31,($26),1		/* .. E1 */

.align 3
within_one_quad:
	ldq_u $1,0($16)		/* E0 */
	insql $17,$16,$2	/* E1 */
	mskql $1,$16,$4		/* E0 (after load stall) */
	bis $2,$4,$2		/* E0 */

	mskql $2,$6,$4		/* E0 */
	mskqh $1,$6,$2		/* .. E1 */
	bis $2,$4,$1		/* E0 */
	stq_u $1,0($16)		/* E0 */

end:
	ret $31,($26),1		/* E1 */
	.end __memset

	.align 5
	.ent __memsetw
__memsetw:
	.prologue 0

	inswl $17,0,$1		/* E0 */
	inswl $17,2,$2		/* E0 */
	inswl $17,4,$3		/* E0 */
	or $1,$2,$1		/* .. E1 */
	inswl $17,6,$4		/* E0 */
	or $1,$3,$1		/* .. E1 */
	or $1,$4,$17		/* E0 */
	br __constant_c_memset	/* .. E1 */

	.end __memsetw

memset = __memset
