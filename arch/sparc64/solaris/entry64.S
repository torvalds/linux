/* $Id: entry64.S,v 1.7 2002/02/09 19:49:31 davem Exp $
 * entry64.S:  Solaris syscall emulation entry point.
 *
 * Copyright (C) 1996,1997,1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 */

#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

#include "conv.h"

#define NR_SYSCALLS	256

	.text
solaris_syscall_trace:
	add		%sp, PTREGS_OFF, %o0
	call		syscall_trace
	 mov		0, %o1
	srl		%i0, 0, %o0
	mov		%i4, %o4
	srl		%i1, 0, %o1
	mov		%i5, %o5
	andcc		%l3, 1, %g0
	be,pt		%icc, 2f
	 srl		%i2, 0, %o2
	b,pt		%xcc, 2f
	 add		%sp, PTREGS_OFF, %o0

solaris_sucks:
/* Solaris is a big system which needs to be able to do all the things
 * in Inf+1 different ways */
	add		%i6, 0x5c, %o0
	mov		%i0, %g1
	mov		%i1, %i0
	mov		%i2, %i1
	srl		%o0, 0, %o0
	mov		%i3, %i2
	movrz		%g1, 256, %g1 /* Ensure we don't loop forever */
	mov		%i4, %i3
	mov		%i5, %i4
	ba,pt		%xcc, solaris_sparc_syscall
exen:	 lduwa		[%o0] ASI_S, %i5

exenf:	ba,pt		%xcc, solaris_sparc_syscall
	 clr		%i5

/* For shared binaries, binfmt_elf32 already sets up personality
   and exec_domain. This is to handle static binaries as well */
solaris_reg:
	call		solaris_register
	 nop
	ba,pt		%xcc, 1f
	 mov		%i4, %o4

linux_syscall_for_solaris:
	sethi		%hi(sys_call_table32), %l6
	or		%l6, %lo(sys_call_table32), %l6
	sll		%l3, 2, %l4
	ba,pt		%xcc, 10f
	 lduw		[%l6 + %l4], %l3

	/* Solaris system calls enter here... */
	.align	32
	.globl	solaris_sparc_syscall, entry64_personality_patch
solaris_sparc_syscall:
entry64_personality_patch:
	ldub		[%g4 + 0x0], %l0
	cmp		%g1, 255
	bg,pn		%icc, solaris_unimplemented
	 srl		%g1, 0, %g1
	sethi		%hi(solaris_sys_table), %l7
	or		%l7, %lo(solaris_sys_table), %l7
	brz,pn		%g1, solaris_sucks
	 mov		%i4, %o4
	sll		%g1, 2, %l4
	cmp		%l0, 1
	bne,pn		%icc, solaris_reg
1:	 srl		%i0, 0, %o0
	lduw		[%l7 + %l4], %l3
	srl		%i1, 0, %o1
	ldx		[%g6 + TI_FLAGS], %l5
	cmp		%l3, NR_SYSCALLS
	bleu,a,pn	%xcc, linux_syscall_for_solaris
	 nop
	andcc		%l3, 1, %g0
	bne,a,pn	%icc, 10f
	 add		%sp, PTREGS_OFF, %o0
10:	srl		%i2, 0, %o2
	mov		%i5, %o5
	andn		%l3, 3, %l7
	andcc		%l5, _TIF_SYSCALL_TRACE, %g0				
	bne,pn		%icc, solaris_syscall_trace		
	 mov		%i0, %l5
2:	call		%l7
	 srl		%i3, 0, %o3
ret_from_solaris:
	stx		%o0, [%sp + PTREGS_OFF + PT_V9_I0]
	ldx		[%g6 + TI_FLAGS], %l6
	sra		%o0, 0, %o0
	mov		%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	ldx		[%sp + PTREGS_OFF + PT_V9_TSTATE], %g3
	cmp		%o0, -ERESTART_RESTARTBLOCK
	sllx		%g2, 32, %g2
	bgeu,pn		%xcc, 1f
	 andcc		%l6, _TIF_SYSCALL_TRACE, %l6	

	/* System call success, clear Carry condition code. */
	andn		%g3, %g2, %g3
	stx		%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]	
	bne,pn		%icc, solaris_syscall_trace2
	 ldx		[%sp + PTREGS_OFF + PT_V9_TNPC], %l1
	andcc		%l1, 1, %g0
	bne,pn		%icc, 2f
	 clr		%l6
	add		%l1, 0x4, %l2				         
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]	 ! pc = npc
	call		rtrap
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC] !npc = npc+4

	/* When tnpc & 1, this comes from setcontext and we don't want to advance pc */
2:	andn		%l1, 3, %l1
	call		rtrap
	 stx		%l1, [%sp + PTREGS_OFF + PT_V9_TNPC] !npc = npc&~3

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub		%g0, %o0, %o0
	or		%g3, %g2, %g3
	cmp		%o0, ERANGE	/* 0-ERANGE are identity mapped */
	bleu,pt		%icc, 1f
	 cmp		%o0, EMEDIUMTYPE
	bgu,pn		%icc, 1f
	 sethi		%hi(solaris_err_table), %l6
	sll		%o0, 2, %o0
	or		%l6, %lo(solaris_err_table), %l6
	ldsw		[%l6 + %o0], %o0
1:	stx		%o0, [%sp + PTREGS_OFF + PT_V9_I0]
	mov		1, %l6
	stx		%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]
	bne,pn		%icc, solaris_syscall_trace2
	 ldx		[%sp + PTREGS_OFF + PT_V9_TNPC], %l1
	andcc		%l1, 1, %g0
	bne,pn		%icc, 2b
	 add		%l1, 0x4, %l2
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]  ! pc = npc
	call		rtrap
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC] !npc = npc+4 

solaris_syscall_trace2:
	add		%sp, PTREGS_OFF, %o0
	call		syscall_trace
	 mov		1, %o1
	add		%l1, 0x4, %l2			/* npc = npc+4 */
	andcc		%l1, 1, %g0
	bne,pn		%icc, 2b
	 nop
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]
	call		rtrap
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]

	/* This one is tricky, so that's why we do it in assembly */
	.globl		solaris_sigsuspend
solaris_sigsuspend:
	call		do_sol_sigsuspend
	 nop
	brlz,pn		%o0, ret_from_solaris
	 nop
	call		sys_sigsuspend
	 stx		%o0, [%sp + PTREGS_OFF + PT_V9_I0]

	.globl		solaris_getpid
solaris_getpid:
	call		sys_getppid
	 nop
	call		sys_getpid
	 stx		%o0, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt		%xcc, ret_from_solaris
	 nop

	.globl	solaris_getuid
solaris_getuid:
	call		sys_geteuid
	 nop
	call		sys_getuid
	 stx		%o1, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt		%xcc, ret_from_solaris
	 nop

	.globl	solaris_getgid
solaris_getgid:
	call		sys_getegid
	 nop
	call		sys_getgid
	 stx		%o1, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt		%xcc, ret_from_solaris
	 nop

	.globl		solaris_unimplemented
solaris_unimplemented:
	call		do_sol_unimplemented
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, ret_from_solaris
	 nop

	.section	__ex_table,#alloc
	.align		4
	.word		exen, exenf

