/* SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006
   Free Software Foundation, Inc.
*/

!! libgcc routines for the Renesas / SuperH SH CPUs.
!! Contributed by Steve Chamberlain.
!! sac@cygnus.com

!! ashiftrt_r4_x, ___ashrsi3, ___ashlsi3, ___lshrsi3 routines
!! recoded in assembly by Toshiyasu Morita
!! tm@netcom.com

/* SH2 optimizations for ___ashrsi3, ___ashlsi3, ___lshrsi3 and
   ELF local label prefixes by J"orn Rennecke
   amylaar@cygnus.com  */

	.text
	.balign	4
	.global	__movmem
	.global __movstr
	.set __movstr, __movmem	
	/* This would be a lot simpler if r6 contained the byte count
	   minus 64, and we wouldn't be called here for a byte count of 64.  */
__movmem:
	sts.l	pr,@-r15
	shll2	r6
	bsr	__movmemSI52+2
	mov.l	@(48,r5),r0
	.balign	4
movmem_loop: /* Reached with rts */
	mov.l	@(60,r5),r0
	add	#-64,r6
	mov.l	r0,@(60,r4)
	tst	r6,r6
	mov.l	@(56,r5),r0
	bt	movmem_done
	mov.l	r0,@(56,r4)
	cmp/pl	r6
	mov.l	@(52,r5),r0
	add	#64,r5
	mov.l	r0,@(52,r4)
	add	#64,r4
	bt	__movmemSI52
! done all the large groups, do the remainder
! jump to movmem+
	mova	__movmemSI4+4,r0
	add	r6,r0
	jmp	@r0
movmem_done: ! share slot insn, works out aligned.
	lds.l	@r15+,pr
	mov.l	r0,@(56,r4)
	mov.l	@(52,r5),r0
	rts
	mov.l	r0,@(52,r4)
	.balign	4

	.global	__movmemSI64
	.global __movstrSI64
	.set	__movstrSI64, __movmemSI64
__movmemSI64:
	mov.l	@(60,r5),r0
	mov.l	r0,@(60,r4)
	.global	__movmemSI60
	.global __movstrSI60
	.set	__movstrSI60, __movmemSI60
__movmemSI60:
	mov.l	@(56,r5),r0
	mov.l	r0,@(56,r4)
	.global	__movmemSI56
	.global __movstrSI56
	.set	__movstrSI56, __movmemSI56
__movmemSI56:
	mov.l	@(52,r5),r0
	mov.l	r0,@(52,r4)
	.global	__movmemSI52
	.global __movstrSI52
	.set	__movstrSI52, __movmemSI52
__movmemSI52:
	mov.l	@(48,r5),r0
	mov.l	r0,@(48,r4)
	.global	__movmemSI48
	.global	__movstrSI48
	.set	__movstrSI48, __movmemSI48
__movmemSI48:
	mov.l	@(44,r5),r0
	mov.l	r0,@(44,r4)
	.global	__movmemSI44
	.global	__movstrSI44
	.set	__movstrSI44, __movmemSI44
__movmemSI44:
	mov.l	@(40,r5),r0
	mov.l	r0,@(40,r4)
	.global	__movmemSI40
	.global __movstrSI40
	.set	__movstrSI40, __movmemSI40
__movmemSI40:
	mov.l	@(36,r5),r0
	mov.l	r0,@(36,r4)
	.global	__movmemSI36
	.global	__movstrSI36
	.set	__movstrSI36, __movmemSI36
__movmemSI36:
	mov.l	@(32,r5),r0
	mov.l	r0,@(32,r4)
	.global	__movmemSI32
	.global	__movstrSI32
	.set	__movstrSI32, __movmemSI32
__movmemSI32:
	mov.l	@(28,r5),r0
	mov.l	r0,@(28,r4)
	.global	__movmemSI28
	.global	__movstrSI28
	.set	__movstrSI28, __movmemSI28
__movmemSI28:
	mov.l	@(24,r5),r0
	mov.l	r0,@(24,r4)
	.global	__movmemSI24
	.global	__movstrSI24
	.set	__movstrSI24, __movmemSI24
__movmemSI24:
	mov.l	@(20,r5),r0
	mov.l	r0,@(20,r4)
	.global	__movmemSI20
	.global	__movstrSI20
	.set	__movstrSI20, __movmemSI20
__movmemSI20:
	mov.l	@(16,r5),r0
	mov.l	r0,@(16,r4)
	.global	__movmemSI16
	.global	__movstrSI16
	.set	__movstrSI16, __movmemSI16
__movmemSI16:
	mov.l	@(12,r5),r0
	mov.l	r0,@(12,r4)
	.global	__movmemSI12
	.global	__movstrSI12
	.set	__movstrSI12, __movmemSI12
__movmemSI12:
	mov.l	@(8,r5),r0
	mov.l	r0,@(8,r4)
	.global	__movmemSI8
	.global	__movstrSI8
	.set	__movstrSI8, __movmemSI8
__movmemSI8:
	mov.l	@(4,r5),r0
	mov.l	r0,@(4,r4)
	.global	__movmemSI4
	.global	__movstrSI4
	.set	__movstrSI4, __movmemSI4
__movmemSI4:
	mov.l	@(0,r5),r0
	rts
	mov.l	r0,@(0,r4)

	.global	__movmem_i4_even
	.global	__movstr_i4_even
	.set	__movstr_i4_even, __movmem_i4_even

	.global	__movmem_i4_odd
	.global	__movstr_i4_odd
	.set	__movstr_i4_odd, __movmem_i4_odd

	.global	__movmemSI12_i4
	.global	__movstrSI12_i4
	.set	__movstrSI12_i4, __movmemSI12_i4

	.p2align	5
L_movmem_2mod4_end:
	mov.l	r0,@(16,r4)
	rts
	mov.l	r1,@(20,r4)

	.p2align	2

__movmem_i4_even:
	mov.l	@r5+,r0
	bra	L_movmem_start_even
	mov.l	@r5+,r1

__movmem_i4_odd:
	mov.l	@r5+,r1
	add	#-4,r4
	mov.l	@r5+,r2
	mov.l	@r5+,r3
	mov.l	r1,@(4,r4)
	mov.l	r2,@(8,r4)

L_movmem_loop:
	mov.l	r3,@(12,r4)
	dt	r6
	mov.l	@r5+,r0
	bt/s	L_movmem_2mod4_end
	mov.l	@r5+,r1
	add	#16,r4
L_movmem_start_even:
	mov.l	@r5+,r2
	mov.l	@r5+,r3
	mov.l	r0,@r4
	dt	r6
	mov.l	r1,@(4,r4)
	bf/s	L_movmem_loop
	mov.l	r2,@(8,r4)
	rts
	mov.l	r3,@(12,r4)

	.p2align	4
__movmemSI12_i4:
	mov.l	@r5,r0
	mov.l	@(4,r5),r1
	mov.l	@(8,r5),r2
	mov.l	r0,@r4
	mov.l	r1,@(4,r4)
	rts
	mov.l	r2,@(8,r4)
