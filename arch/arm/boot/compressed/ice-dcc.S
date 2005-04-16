

	.text

	.global	icedcc_putc

icedcc_putc:
	mov	r2, #0x4000000
1:
	subs	r2, r2, #1
	movlt	pc, r14
	mrc	p14, 0, r1, c0, c0, 0
	tst	r1, #2
	bne	1b

	mcr	p14, 0, r0, c1, c0, 0
	mov	pc, r14
