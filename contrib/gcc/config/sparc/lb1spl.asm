/* This is an assembly language implementation of mulsi3, divsi3, and modsi3
   for the sparclite processor.

   These routines are all from the SPARClite User's Guide, slightly edited
   to match the desired calling convention, and also to optimize them.  */

#ifdef L_udivsi3
.text
	.align 4
	.global .udiv
	.proc	04
.udiv:
	wr	%g0,%g0,%y	! Not a delayed write for sparclite
	tst	%g0
	divscc	%o0,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	retl
	divscc	%g1,%o1,%o0
#endif

#ifdef L_umodsi3
.text
	.align 4
	.global .urem
	.proc	04
.urem:
	wr	%g0,%g0,%y	! Not a delayed write for sparclite
	tst	%g0
	divscc	%o0,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	divscc	%g1,%o1,%g1
	bl 1f
	rd	%y,%o0
	retl
	nop
1:	retl
	add	%o0,%o1,%o0
#endif

#ifdef L_divsi3
.text
	.align 4
	.global .div
	.proc	04
! ??? This routine could be made faster if was optimized, and if it was
! rewritten to only calculate the quotient.
.div:
	wr	%g0,%g0,%y	! Not a delayed write for sparclite
	mov	%o1,%o4
	tst	%o1
	bl,a	1f
	sub	%g0,%o4,%o4
1:	tst	%o0
	bl,a	2f
	mov	-1,%y
2:	divscc	%o0,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	be	6f
	mov	%y,%o3
	bg	4f
	addcc	%o3,%o4,%g0
	be,a	6f
	mov	%g0,%o3
	tst	%o0
	bl	5f
	tst	%g1
	ba	5f
	add	%o3,%o4,%o3
4:	subcc	%o3,%o4,%g0
	be,a	6f
	mov	%g0,%o3
	tst	%o0
	bge	5f
	tst	%g1
	sub	%o3,%o4,%o3
5:	bl,a	6f
	add	%g1,1,%g1
6:	tst	%o1
	bl,a	7f
	sub	%g0,%g1,%g1
7:	retl
	mov	%g1,%o0		! Quotient is in %g1.
#endif

#ifdef L_modsi3
.text
	.align 4
	.global .rem
	.proc	04
! ??? This routine could be made faster if was optimized, and if it was
! rewritten to only calculate the remainder.
.rem:
	wr	%g0,%g0,%y	! Not a delayed write for sparclite
	mov	%o1,%o4
	tst	%o1
	bl,a	1f
	sub	%g0,%o4,%o4
1:	tst	%o0
	bl,a	2f
	mov	-1,%y
2:	divscc	%o0,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	divscc	%g1,%o4,%g1
	be	6f
	mov	%y,%o3
	bg	4f
	addcc	%o3,%o4,%g0
	be,a	6f
	mov	%g0,%o3
	tst	%o0
	bl	5f
	tst	%g1
	ba	5f
	add	%o3,%o4,%o3
4:	subcc	%o3,%o4,%g0
	be,a	6f
	mov	%g0,%o3
	tst	%o0
	bge	5f
	tst	%g1
	sub	%o3,%o4,%o3
5:	bl,a	6f
	add	%g1,1,%g1
6:	tst	%o1
	bl,a	7f
	sub	%g0,%g1,%g1
7:	retl
	mov	%o3,%o0		! Remainder is in %o3.
#endif
