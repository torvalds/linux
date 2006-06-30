/* -*- mode: asm -*-
 * Due to problems while transferring data I've put these routines as assembly
 * code.
 * Since I'm no PPC assembler guru, the code is just the assembler version of

int oktag_to_io(long *paddr,long *addr,long len)
{
  long *addr2 = addr;
  for(len=(len+sizeof(long)-1)/sizeof(long);len--;)
    *paddr = *addr2++;
  return addr2 - addr;
}

int oktag_from_io(long *addr,long *paddr,long len)
{
  long *addr2 = addr;
  for(len=(len+sizeof(long)-1)/sizeof(long);len--;)
    *addr2++ = *paddr;
  return addr2 - addr;
}

 * assembled using gcc -O2 -S, with two exception catch points where data
 * is moved to/from the IO register.
 */


#ifdef CONFIG_APUS

	.file	"oktagon_io.c"

gcc2_compiled.:
/*
	.section ".text"
*/
	.align 2
	.globl oktag_to_io
	.type	 oktag_to_io,@function
oktag_to_io:
	addi 5,5,3
	srwi 5,5,2
	cmpwi 1,5,0
	mr 9,3
	mr 3,4
	addi 5,5,-1
	bc 12,6,.L3
.L5:
	cmpwi 1,5,0
	lwz 0,0(3)
	addi 3,3,4
	addi 5,5,-1
exp1:	stw 0,0(9)
	bc 4,6,.L5
.L3:
ret1:	subf 3,4,3
	srawi 3,3,2
	blr
.Lfe1:
	.size	 oktag_to_io,.Lfe1-oktag_to_io
	.align 2
	.globl oktag_from_io
	.type	 oktag_from_io,@function
oktag_from_io:
	addi 5,5,3
	srwi 5,5,2
	cmpwi 1,5,0
	mr 9,3
	addi 5,5,-1
	bc 12,6,.L9
.L11:
	cmpwi 1,5,0
exp2:	lwz 0,0(4)
	addi 5,5,-1
	stw 0,0(3)
	addi 3,3,4
	bc 4,6,.L11
.L9:
ret2:	subf 3,9,3
	srawi 3,3,2
	blr
.Lfe2:
	.size	 oktag_from_io,.Lfe2-oktag_from_io
	.ident	"GCC: (GNU) egcs-2.90.29 980515 (egcs-1.0.3 release)"

/*
 * Exception table.
 * Second longword shows where to jump when an exception at the addr the first
 * longword is pointing to is caught.
 */

.section __ex_table,"a"
	.align	2
oktagon_except:
	.long	exp1,ret1
	.long	exp2,ret2

#else

/*
The code which follows is for 680x0 based assembler and is meant for
Linux/m68k. It was created by cross compiling the code using the
instructions given above. I then added the four labels used in the
exception handler table at the bottom of this file.
- Kevin <kcozens@interlog.com>
*/

#ifdef CONFIG_AMIGA

	.file	"oktagon_io.c"
	.version	"01.01"
gcc2_compiled.:
.text
	.align 	2
.globl oktag_to_io
	.type	 oktag_to_io,@function
oktag_to_io:
	link.w %a6,#0
	move.l %d2,-(%sp)
	move.l 8(%a6),%a1
	move.l 12(%a6),%d1
	move.l %d1,%a0
	move.l 16(%a6),%d0
	addq.l #3,%d0
	lsr.l #2,%d0
	subq.l #1,%d0
	moveq.l #-1,%d2
	cmp.l %d0,%d2
	jbeq .L3
.L5:
exp1:
	move.l (%a0)+,(%a1)
	dbra %d0,.L5
	clr.w %d0
	subq.l #1,%d0
	jbcc .L5
.L3:
ret1:
	move.l %a0,%d0
	sub.l %d1,%d0
	asr.l #2,%d0
	move.l -4(%a6),%d2
	unlk %a6
	rts

.Lfe1:
	.size	 oktag_to_io,.Lfe1-oktag_to_io
	.align 	2
.globl oktag_from_io
	.type	 oktag_from_io,@function
oktag_from_io:
	link.w %a6,#0
	move.l %d2,-(%sp)
	move.l 8(%a6),%d1
	move.l 12(%a6),%a1
	move.l %d1,%a0
	move.l 16(%a6),%d0
	addq.l #3,%d0
	lsr.l #2,%d0
	subq.l #1,%d0
	moveq.l #-1,%d2
	cmp.l %d0,%d2
	jbeq .L9
.L11:
exp2:
	move.l (%a1),(%a0)+
	dbra %d0,.L11
	clr.w %d0
	subq.l #1,%d0
	jbcc .L11
.L9:
ret2:
	move.l %a0,%d0
	sub.l %d1,%d0
	asr.l #2,%d0
	move.l -4(%a6),%d2
	unlk %a6
	rts
.Lfe2:
	.size	 oktag_from_io,.Lfe2-oktag_from_io
	.ident	"GCC: (GNU) 2.7.2.1"

/*
 * Exception table.
 * Second longword shows where to jump when an exception at the addr the first
 * longword is pointing to is caught.
 */

.section __ex_table,"a"
	.align	2
oktagon_except:
	.long	exp1,ret1
	.long	exp2,ret2

#endif
#endif
