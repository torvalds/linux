/* Copyright (C) 1989, 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

	.data
	.align 8
	.globl	__clz_tab
__clz_tab:
	.byte	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
	.byte	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6
	.byte	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
	.byte	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
	.byte	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	.byte	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	.byte	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	.byte	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	.size	 __clz_tab,256
	.global .udiv

	.text
	.align 4
	.globl __divdi3
__divdi3:
	save %sp,-104,%sp
	cmp %i0,0
	bge .LL40
	mov 0,%l4
	mov -1,%l4
	sub %g0,%i1,%o0
	mov %o0,%o5
	subcc %g0,%o0,%g0
	sub %g0,%i0,%o0
	subx %o0,0,%o4
	mov %o4,%i0
	mov %o5,%i1
.LL40:
	cmp %i2,0
	bge .LL84
	mov %i3,%o4
	xnor %g0,%l4,%l4
	sub %g0,%i3,%o0
	mov %o0,%o3
	subcc %g0,%o0,%g0
	sub %g0,%i2,%o0
	subx %o0,0,%o2
	mov %o2,%i2
	mov %o3,%i3
	mov %i3,%o4
.LL84:
	cmp %i2,0
	bne .LL45
	mov %i1,%i3
	cmp %o4,%i0
	bleu .LL46
	mov %i3,%o1
	mov	32,%g1
	subcc	%i0,%o4,%g0
1:	bcs	5f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	sub	%i0,%o4,%i0	! this kills msb of n
	addx	%i0,%i0,%i0	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%i0,%o4,%g0
	bcs	3f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%i0,%o4,%i0	! this kills msb of n
4:	sub	%i0,%o4,%i0
5:	addxcc	%i0,%i0,%i0
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o1,%o1,%o1	! shift n1n0 and a 0-bit in lsb
	sub	%i0,%o4,%i0
3:	xnor	%o1,0,%o1
	b .LL50
	mov 0,%o2
.LL46:
	cmp %o4,0
	bne .LL85
	mov %i0,%o2
	mov 1,%o0
	call .udiv,0
	mov 0,%o1
	mov %o0,%o4
	mov %i0,%o2
.LL85:
	mov 0,%g3
	mov	32,%g1
	subcc	%g3,%o4,%g0
1:	bcs	5f
	 addxcc %o2,%o2,%o2	! shift n1n0 and a q-bit in lsb
	sub	%g3,%o4,%g3	! this kills msb of n
	addx	%g3,%g3,%g3	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%g3,%o4,%g0
	bcs	3f
	 addxcc %o2,%o2,%o2	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%g3,%o4,%g3	! this kills msb of n
4:	sub	%g3,%o4,%g3
5:	addxcc	%g3,%g3,%g3
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o2,%o2,%o2	! shift n1n0 and a 0-bit in lsb
	sub	%g3,%o4,%g3
3:	xnor	%o2,0,%o2
	mov %g3,%i0
	mov %i3,%o1
	mov	32,%g1
	subcc	%i0,%o4,%g0
1:	bcs	5f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	sub	%i0,%o4,%i0	! this kills msb of n
	addx	%i0,%i0,%i0	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%i0,%o4,%g0
	bcs	3f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%i0,%o4,%i0	! this kills msb of n
4:	sub	%i0,%o4,%i0
5:	addxcc	%i0,%i0,%i0
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o1,%o1,%o1	! shift n1n0 and a 0-bit in lsb
	sub	%i0,%o4,%i0
3:	xnor	%o1,0,%o1
	b .LL86
	mov %o1,%l1
.LL45:
	cmp %i2,%i0
	bleu .LL51
	sethi %hi(65535),%o0
	b .LL78
	mov 0,%o1
.LL51:
	or %o0,%lo(65535),%o0
	cmp %i2,%o0
	bgu .LL58
	mov %i2,%o1
	cmp %i2,256
	addx %g0,-1,%o0
	b .LL64
	and %o0,8,%o2
.LL58:
	sethi %hi(16777215),%o0
	or %o0,%lo(16777215),%o0
	cmp %i2,%o0
	bgu .LL64
	mov 24,%o2
	mov 16,%o2
.LL64:
	srl %o1,%o2,%o0
	sethi %hi(__clz_tab),%o1
	or %o1,%lo(__clz_tab),%o1
	ldub [%o0+%o1],%o0
	add %o0,%o2,%o0
	mov 32,%o1
	subcc %o1,%o0,%o3
	bne,a .LL72
	sub %o1,%o3,%o1
	cmp %i0,%i2
	bgu .LL74
	cmp %i3,%o4
	blu .LL78
	mov 0,%o1
.LL74:
	b .LL78
	mov 1,%o1
.LL72:
	sll %i2,%o3,%o2
	srl %o4,%o1,%o0
	or %o2,%o0,%i2
	sll %o4,%o3,%o4
	srl %i0,%o1,%o2
	sll %i0,%o3,%o0
	srl %i3,%o1,%o1
	or %o0,%o1,%i0
	sll %i3,%o3,%i3
	mov %i0,%o1
	mov	32,%g1
	subcc	%o2,%i2,%g0
1:	bcs	5f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	sub	%o2,%i2,%o2	! this kills msb of n
	addx	%o2,%o2,%o2	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%o2,%i2,%g0
	bcs	3f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%o2,%i2,%o2	! this kills msb of n
4:	sub	%o2,%i2,%o2
5:	addxcc	%o2,%o2,%o2
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o1,%o1,%o1	! shift n1n0 and a 0-bit in lsb
	sub	%o2,%i2,%o2
3:	xnor	%o1,0,%o1
	mov %o2,%i0
	wr	%g0,%o1,%y	! SPARC has 0-3 delay insn after a wr
	sra	%o4,31,%g2	! Do not move this insn
	and	%o1,%g2,%g2	! Do not move this insn
	andcc	%g0,0,%g1	! Do not move this insn
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,%o4,%g1
	mulscc	%g1,0,%g1
	add	%g1,%g2,%o0
	rd	%y,%o2
	cmp %o0,%i0
	bgu,a .LL78
	add %o1,-1,%o1
	bne,a .LL50
	mov 0,%o2
	cmp %o2,%i3
	bleu .LL50
	mov 0,%o2
	add %o1,-1,%o1
.LL78:
	mov 0,%o2
.LL50:
	mov %o1,%l1
.LL86:
	mov %o2,%l0
	mov %l0,%i0
	mov %l1,%i1
	cmp %l4,0
	be .LL81
	sub %g0,%i1,%o0
	mov %o0,%l3
	subcc %g0,%o0,%g0
	sub %g0,%i0,%o0
	subx %o0,0,%l2
	mov %l2,%i0
	mov %l3,%i1
.LL81:
	ret
	restore
