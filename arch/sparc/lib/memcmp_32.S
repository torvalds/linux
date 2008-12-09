	.text
	.align 4
	.global __memcmp, memcmp
__memcmp:
memcmp:
	cmp	%o2, 0
	ble	L3
	 mov	0, %g3
L5:
	ldub	[%o0], %g2
	ldub	[%o1], %g3
	sub	%g2, %g3, %g2
	mov	%g2, %g3
	sll	%g2, 24, %g2

	cmp	%g2, 0
	bne	L3
	 add	%o0, 1, %o0

	add	%o2, -1, %o2

	cmp	%o2, 0
	bg	L5
	 add	%o1, 1, %o1
L3:
	sll	%g3, 24, %o0
	sra	%o0, 24, %o0

	retl
	 nop
