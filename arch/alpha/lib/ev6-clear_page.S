/*
 * arch/alpha/lib/ev6-clear_page.S
 *
 * Zero an entire page.
 */

        .text
        .align 4
        .global clear_page
        .ent clear_page
clear_page:
        .prologue 0

	lda	$0,128
	lda	$1,125
	addq	$16,64,$2
	addq	$16,128,$3

	addq	$16,192,$17
	wh64	($16)
	wh64	($2)
	wh64	($3)

1:	wh64	($17)
	stq	$31,0($16)
	subq	$0,1,$0
	subq	$1,1,$1

	stq	$31,8($16)
	stq	$31,16($16)
	addq	$17,64,$2
	nop

	stq	$31,24($16)
	stq	$31,32($16)
	cmovgt	$1,$2,$17
	nop

	stq	$31,40($16)
	stq	$31,48($16)
	nop
	nop

	stq	$31,56($16)
	addq	$16,64,$16
	nop
	bne	$0,1b

	ret
	nop
	nop
	nop

	.end clear_page
