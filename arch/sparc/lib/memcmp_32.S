	.text
	.align 4
	.global __memcmp, memcmp
__memcmp:
memcmp:
#if 1
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
#else
	save	%sp, -104, %sp
	mov	%i2, %o4
	mov	%i0, %o0

	cmp	%o4, 15
	ble	L72
	 mov	%i1, %i2

	andcc	%i2, 3, %g0
	be	L161
	 andcc	%o0, 3, %g2
L75:
	ldub	[%o0], %g3
	ldub	[%i2], %g2
	add	%o0,1, %o0

	subcc	%g3, %g2, %i0
	bne	L156
	 add	%i2, 1, %i2

	andcc	%i2, 3, %g0
	bne	L75
	 add	%o4, -1, %o4

	andcc	%o0, 3, %g2
L161:
	bne,a	L78
	 mov	%i2, %i1

	mov	%o0, %i5
	mov	%i2, %i3
	srl	%o4, 2, %i4

	cmp	%i4, 0
	bge	L93
	 mov	%i4, %g2

	add %i4, 3, %g2
L93:
	sra	%g2, 2, %g2
	sll	%g2, 2, %g2
	sub	%i4, %g2, %g2

	cmp	%g2, 1
	be,a	L88
	 add	%o0, 4, %i5

	bg	L94
	 cmp	%g2, 2

	cmp	%g2, 0
	be,a	L86
	 ld	[%o0], %g3

	b	L162
	 ld	[%i5], %g3
L94:
	be	L81
	 cmp	%g2, 3

	be,a	L83
	 add	%o0, -4, %i5

	b	L162
	 ld	[%i5], %g3
L81:
	add	%o0, -8, %i5
	ld	[%o0], %g3
	add	%i2, -8, %i3
	ld	[%i2], %g2

	b	L82
	 add	%i4, 2, %i4
L83:
	ld	[%o0], %g4
	add	%i2, -4, %i3
	ld	[%i2], %g1

	b	L84
	 add	%i4, 1, %i4
L86:
	b	L87
	 ld	[%i2], %g2
L88:
	add	%i2, 4, %i3
	ld	[%o0], %g4
	add	%i4, -1, %i4
	ld	[%i2], %g1
L95:
	ld	[%i5], %g3
L162:
	cmp	%g4, %g1
	be	L87
	 ld	[%i3], %g2

	cmp	%g4, %g1
L163:
	bleu	L114
	 mov	-1, %i0

	b	L114
	 mov	1, %i0
L87:
	ld	[%i5 + 4], %g4
	cmp	%g3, %g2
	bne	L163
	 ld	[%i3 + 4], %g1
L84:
	ld	[%i5 + 8], %g3

	cmp	%g4, %g1
	bne	L163
	 ld	[%i3 + 8], %g2
L82:
	ld	[%i5 + 12], %g4
	cmp	%g3, %g2
	bne	L163
	 ld	[%i3 + 12], %g1

	add	%i5, 16, %i5

	addcc	%i4, -4, %i4
	bne	L95
	 add	%i3, 16, %i3

	cmp	%g4, %g1
	bne	L163
	 nop

	b	L114
	 mov	0, %i0
L78:
	srl	%o4, 2, %i0
	and	%o0, -4, %i3
	orcc	%i0, %g0, %g3
	sll	%g2, 3, %o7
	mov	32, %g2

	bge	L129
	 sub	%g2, %o7, %o1

	add	%i0, 3, %g3
L129:
	sra	%g3, 2, %g2
	sll	%g2, 2, %g2
	sub	%i0, %g2, %g2

	cmp	%g2, 1
	be,a	L124
	 ld	[%i3], %o3

	bg	L130
	 cmp	%g2, 2

	cmp	%g2, 0
	be,a	L122
	 ld	[%i3], %o2

	b	L164
	sll	%o3, %o7, %g3
L130:
	be	L117
	 cmp	%g2, 3

	be,a	L119
	 ld	[%i3], %g1

	b	L164
	 sll	%o3, %o7, %g3
L117:
	ld	[%i3], %g4
	add	%i2, -8, %i1
	ld	[%i3 + 4], %o3
	add	%i0, 2, %i0
	ld	[%i2], %i4

	b	L118
	 add	%i3, -4, %i3
L119:
	ld	[%i3 + 4], %g4
	add	%i2, -4, %i1
	ld	[%i2], %i5

	b	L120
	 add	%i0, 1, %i0
L122:
	ld	[%i3 + 4], %g1
	ld	[%i2], %i4

	b	L123
	 add	%i3, 4, %i3
L124:
	add	%i2, 4, %i1
	ld	[%i3 + 4], %o2
	add	%i0, -1, %i0
	ld	[%i2], %i5
	add	%i3, 8, %i3
L131:
	sll	%o3, %o7, %g3
L164:
	srl	%o2, %o1, %g2
	ld	[%i3], %g1
	or	%g3, %g2, %g3

	cmp	%g3, %i5
	bne	L163
	 ld	[%i1], %i4
L123:
	sll	%o2, %o7, %g3
	srl	%g1, %o1, %g2
	ld	[%i3 + 4], %g4
	or	%g3, %g2, %g3

	cmp	%g3, %i4
	bne	L163
	 ld	[%i1 + 4], %i5
L120:
	sll	%g1, %o7, %g3
	srl	%g4, %o1, %g2
	ld	[%i3 + 8], %o3
	or	%g3, %g2, %g3

	cmp	%g3, %i5
	bne	L163
	 ld	[%i1 + 8], %i4
L118:
	sll	%g4, %o7, %g3
	srl	%o3, %o1, %g2
	ld	[%i3 + 12], %o2
	or	%g3, %g2, %g3

	cmp	%g3, %i4
	bne	L163
	 ld	[%i1 + 12], %i5

	add	%i3, 16, %i3
	addcc	%i0, -4, %i0
	bne	L131
	 add	%i1, 16, %i1

	sll	%o3, %o7, %g3
	srl	%o2, %o1, %g2
	or	%g3, %g2, %g3

	cmp	%g3, %i5
	be,a	L114
	 mov	0, %i0

	b,a L163
L114:
	cmp	%i0, 0
	bne	L156
	 and	%o4, -4, %g2

	add	%o0, %g2, %o0
	add	%i2, %g2, %i2
	and	%o4, 3, %o4
L72:
	cmp	%o4, 0
	be	L156
	 mov	0, %i0

	ldub	[%o0], %g3
L165:
	ldub	[%i2], %g2
	add	%o0, 1, %o0

	subcc	%g3, %g2, %i0
	bne	L156
	 add	%i2, 1, %i2

	addcc	%o4, -1, %o4
	bne,a	L165
	 ldub	[%o0], %g3

	mov	0, %i0
L156:
	ret
	restore
#endif
