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

	.text
	.align 4
	.globl __udivdi3
__udivdi3:
	save %sp,-104,%sp
	mov %i3,%o3
	cmp %i2,0
	bne .LL40
	mov %i1,%i3
	cmp %o3,%i0
	bleu .LL41
	mov %i3,%o1
	! Inlined udiv_qrnnd
	mov	32,%g1
	subcc	%i0,%o3,%g0
1:	bcs	5f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	sub	%i0,%o3,%i0	! this kills msb of n
	addx	%i0,%i0,%i0	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%i0,%o3,%g0
	bcs	3f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%i0,%o3,%i0	! this kills msb of n
4:	sub	%i0,%o3,%i0
5:	addxcc	%i0,%i0,%i0
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o1,%o1,%o1	! shift n1n0 and a 0-bit in lsb
	sub	%i0,%o3,%i0
3:	xnor	%o1,0,%o1
	! End of inline udiv_qrnnd
	b .LL45
	mov 0,%o2
.LL41:
	cmp %o3,0
	bne .LL77
	mov %i0,%o2
	mov 1,%o0
	mov 0,%o1
	wr %g0, 0, %y
	udiv %o0, %o1, %o0
	mov %o0,%o3
	mov %i0,%o2
.LL77:
	mov 0,%o4
	! Inlined udiv_qrnnd
	mov	32,%g1
	subcc	%o4,%o3,%g0
1:	bcs	5f
	 addxcc %o2,%o2,%o2	! shift n1n0 and a q-bit in lsb
	sub	%o4,%o3,%o4	! this kills msb of n
	addx	%o4,%o4,%o4	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%o4,%o3,%g0
	bcs	3f
	 addxcc %o2,%o2,%o2	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%o4,%o3,%o4	! this kills msb of n
4:	sub	%o4,%o3,%o4
5:	addxcc	%o4,%o4,%o4
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o2,%o2,%o2	! shift n1n0 and a 0-bit in lsb
	sub	%o4,%o3,%o4
3:	xnor	%o2,0,%o2
	! End of inline udiv_qrnnd
	mov %o4,%i0
	mov %i3,%o1
	! Inlined udiv_qrnnd
	mov	32,%g1
	subcc	%i0,%o3,%g0
1:	bcs	5f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	sub	%i0,%o3,%i0	! this kills msb of n
	addx	%i0,%i0,%i0	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%i0,%o3,%g0
	bcs	3f
	 addxcc %o1,%o1,%o1	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%i0,%o3,%i0	! this kills msb of n
4:	sub	%i0,%o3,%i0
5:	addxcc	%i0,%i0,%i0
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o1,%o1,%o1	! shift n1n0 and a 0-bit in lsb
	sub	%i0,%o3,%i0
3:	xnor	%o1,0,%o1
	! End of inline udiv_qrnnd
	b .LL78
	mov %o1,%l1
.LL40:
	cmp %i2,%i0
	bleu .LL46
	sethi %hi(65535),%o0
	b .LL73
	mov 0,%o1
.LL46:
	or %o0,%lo(65535),%o0
	cmp %i2,%o0
	bgu .LL53
	mov %i2,%o1
	cmp %i2,256
	addx %g0,-1,%o0
	b .LL59
	and %o0,8,%o2
.LL53:
	sethi %hi(16777215),%o0
	or %o0,%lo(16777215),%o0
	cmp %o1,%o0
	bgu .LL59
	mov 24,%o2
	mov 16,%o2
.LL59:
	srl %o1,%o2,%o1
	sethi %hi(__clz_tab),%o0
	or %o0,%lo(__clz_tab),%o0
	ldub [%o1+%o0],%o0
	add %o0,%o2,%o0
	mov 32,%o1
	subcc %o1,%o0,%o2
	bne,a .LL67
	mov 32,%o0
	cmp %i0,%i2
	bgu .LL69
	cmp %i3,%o3
	blu .LL73
	mov 0,%o1
.LL69:
	b .LL73
	mov 1,%o1
.LL67:
	sub %o0,%o2,%o0
	sll %i2,%o2,%i2
	srl %o3,%o0,%o1
	or %i2,%o1,%i2
	sll %o3,%o2,%o3
	srl %i0,%o0,%o1
	sll %i0,%o2,%i0
	srl %i3,%o0,%o0
	or %i0,%o0,%i0
	sll %i3,%o2,%i3
	mov %i0,%o5
	mov %o1,%o4
	! Inlined udiv_qrnnd
	mov	32,%g1
	subcc	%o4,%i2,%g0
1:	bcs	5f
	 addxcc %o5,%o5,%o5	! shift n1n0 and a q-bit in lsb
	sub	%o4,%i2,%o4	! this kills msb of n
	addx	%o4,%o4,%o4	! so this cannot give carry
	subcc	%g1,1,%g1
2:	bne	1b
	 subcc	%o4,%i2,%g0
	bcs	3f
	 addxcc %o5,%o5,%o5	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%o4,%i2,%o4	! this kills msb of n
4:	sub	%o4,%i2,%o4
5:	addxcc	%o4,%o4,%o4
	bcc	2b
	 subcc	%g1,1,%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%o5,%o5,%o5	! shift n1n0 and a 0-bit in lsb
	sub	%o4,%i2,%o4
3:	xnor	%o5,0,%o5
	! End of inline udiv_qrnnd
	mov %o4,%i0
	mov %o5,%o1
	! Inlined umul_ppmm
	wr	%g0,%o1,%y	! SPARC has 0-3 delay insn after a wr
	sra	%o3,31,%g2	! Do not move this insn
	and	%o1,%g2,%g2	! Do not move this insn
	andcc	%g0,0,%g1	! Do not move this insn
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,%o3,%g1
	mulscc	%g1,0,%g1
	add	%g1,%g2,%o0
	rd	%y,%o2
	cmp %o0,%i0
	bgu,a .LL73
	add %o1,-1,%o1
	bne,a .LL45
	mov 0,%o2
	cmp %o2,%i3
	bleu .LL45
	mov 0,%o2
	add %o1,-1,%o1
.LL73:
	mov 0,%o2
.LL45:
	mov %o1,%l1
.LL78:
	mov %o2,%l0
	mov %l0,%i0
	mov %l1,%i1
	ret
	restore
