/*
 * arch/alpha/lib/copy_page.S
 *
 * Copy an entire page.
 */

	.text
	.align 4
	.global copy_page
	.ent copy_page
copy_page:
	.prologue 0

	lda	$18,128
	nop
	unop
	nop

1:	ldq	$0,0($17)
	ldq	$1,8($17)
	ldq	$2,16($17)
	ldq	$3,24($17)

	ldq	$4,32($17)
	ldq	$5,40($17)
	ldq	$6,48($17)
	ldq	$7,56($17)

	stq	$0,0($16)
	subq	$18,1,$18
	stq	$1,8($16)
	addq	$17,64,$17

	stq	$2,16($16)
	stq	$3,24($16)
	stq	$4,32($16)
	stq	$5,40($16)

	stq	$6,48($16)
	stq	$7,56($16)
	addq	$16,64,$16
	bne	$18, 1b

	ret
	nop
	unop
	nop

	.end copy_page
