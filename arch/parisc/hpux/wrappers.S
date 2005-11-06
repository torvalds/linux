/*
 * Linux/PARISC Project (http://www.parisc-linux.org/)
 *
 * HP-UX System Call Wrapper routines and System Call Return Path
 *
 * Copyright (C) 2000 Hewlett-Packard (John Marvin)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __LP64__
#warning PA64 support needs more work...did first cut
#endif

#include <asm/asm-offsets.h>
#include <asm/assembly.h>
#include <asm/signal.h>

#ifdef __LP64__
	.level          2.0w
#else
	.level		1.1
#endif
	.text

	/* These should probably go in a header file somewhere.
	 * They are duplicated in kernel/wrappers.S
	 * Possibly we should consider consolidating these
	 * register save/restore macros.
	 */
	.macro	reg_save regs
#ifdef __LP64__
#warning NEEDS WORK for 64-bit
#endif
	STREG	%r3, PT_GR3(\regs)
	STREG	%r4, PT_GR4(\regs)
	STREG	%r5, PT_GR5(\regs)
	STREG	%r6, PT_GR6(\regs)
	STREG	%r7, PT_GR7(\regs)
	STREG	%r8, PT_GR8(\regs)
	STREG	%r9, PT_GR9(\regs)
	STREG	%r10,PT_GR10(\regs)
	STREG	%r11,PT_GR11(\regs)
	STREG	%r12,PT_GR12(\regs)
	STREG	%r13,PT_GR13(\regs)
	STREG	%r14,PT_GR14(\regs)
	STREG	%r15,PT_GR15(\regs)
	STREG	%r16,PT_GR16(\regs)
	STREG	%r17,PT_GR17(\regs)
	STREG	%r18,PT_GR18(\regs)
	.endm

	.macro	reg_restore regs
	LDREG	PT_GR3(\regs), %r3
	LDREG	PT_GR4(\regs), %r4
	LDREG	PT_GR5(\regs), %r5
	LDREG	PT_GR6(\regs), %r6
	LDREG	PT_GR7(\regs), %r7
	LDREG	PT_GR8(\regs), %r8
	LDREG	PT_GR9(\regs), %r9
	LDREG	PT_GR10(\regs),%r10
	LDREG	PT_GR11(\regs),%r11
	LDREG	PT_GR12(\regs),%r12
	LDREG	PT_GR13(\regs),%r13
	LDREG	PT_GR14(\regs),%r14
	LDREG	PT_GR15(\regs),%r15
	LDREG	PT_GR16(\regs),%r16
	LDREG	PT_GR17(\regs),%r17
	LDREG	PT_GR18(\regs),%r18
	.endm


	.export hpux_fork_wrapper
	.export hpux_child_return
	.import sys_fork

hpux_fork_wrapper:
	ldo	TASK_REGS-TASK_SZ_ALGN-64(%r30),%r1	    ;! get pt regs
							    ;! pointer in task
	reg_save %r1

	STREG	%r2,-20(%r30)
	ldo	64(%r30),%r30
	STREG	%r2,PT_GR19(%r1)	;! save for child
	STREG	%r30,PT_GR21(%r1)	;! save for child

	LDREG	PT_GR30(%r1),%r25
	mtctl	%r25,%cr29
	copy	%r1,%r24
	bl	sys_clone,%r2
	ldi	SIGCHLD,%r26

	LDREG	-84(%r30),%r2
fork_return:
	ldo	-64(%r30),%r30
	ldo	TASK_REGS-TASK_SZ_ALGN-64(%r30),%r1	    ;! get pt regs

	reg_restore %r1

	/*
	 * HP-UX wants pid (child gets parent pid, parent gets child pid)
	 * in r28 and a flag in r29 (r29 == 1 for child, 0 for parent).
	 * Linux fork returns 0 for child, pid for parent. Since HP-UX
	 * libc stub throws away parent pid and returns 0 for child,
	 * we'll just return 0 for parent pid now. Only applications
	 * that jump directly to the gateway page (not supported) will
	 * know the difference. We can fix this later if necessary.
	 */

	ldo	-1024(%r0),%r1
	comb,>>=,n %r28,%r1,fork_exit  /* just let the syscall exit handle it */
	or,=	%r28,%r0,%r0
	or,tr	%r0,%r0,%r29	/* r28 <> 0, we are parent, set r29 to 0 */
	ldo	1(%r0),%r29 	/* r28 == 0, we are child,  set r29 to 1 */

fork_exit:
	bv	%r0(%r2)
	nop

	/* Set the return value for the child */

hpux_child_return:
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	bl	schedule_tail, %r2
	nop
#endif

	LDREG	TASK_PT_GR19-TASK_SZ_ALGN-128(%r30),%r2
	b fork_return
	copy %r0,%r28

	.export hpux_execve_wrapper
	.export hpux_execv_wrapper
	.import hpux_execve

hpux_execv_wrapper:
	copy %r0,%r24  /* NULL environment */

hpux_execve_wrapper:

	ldo	TASK_REGS-TASK_SZ_ALGN-64(%r30),%r1	    ;! get pt regs

	/*
	 * Do we need to save/restore r3-r18 here?
	 * I don't think so. why would new thread need old
	 * threads registers?
	 */

	/* Store arg0, arg1 and arg2 so that hpux_execve will find them */

	STREG %r26,PT_GR26(%r1)
	STREG %r25,PT_GR25(%r1)
	STREG %r24,PT_GR24(%r1)

	STREG %r2,-20(%r30)
	ldo 64(%r30),%r30
	bl hpux_execve,%r2
	copy %r1,%arg0

	ldo -64(%r30),%r30
	LDREG	-20(%r30),%r2

	/* If exec succeeded we need to load the args */

	ldo -1024(%r0),%r1
	comb,>>= %r28,%r1,exec_error
	copy %r2,%r19
	ldo	-TASK_SZ_ALGN-64(%r30),%r1	   ;! get task ptr
	LDREG	TASK_PT_GR26(%r1),%r26
	LDREG	TASK_PT_GR25(%r1),%r25
	LDREG	TASK_PT_GR24(%r1),%r24
	LDREG	TASK_PT_GR23(%r1),%r23
	copy %r0,%r2	/* Flag to syscall_exit not to clear args */

exec_error:
	bv %r0(%r19)
	nop

	.export hpux_pipe_wrapper
	.import hpux_pipe

	/* HP-UX expects pipefd's returned in r28 & r29 */

hpux_pipe_wrapper:
	STREG %r2,-20(%r30)
	ldo 64(%r30),%r30
	bl hpux_pipe,%r2
	ldo -56(%r30),%r26 /* pass local array to hpux_pipe */


	ldo -1024(%r0),%r1
	comb,>>= %r28,%r1,pipe_exit /* let syscall exit handle it */
	LDREG	-84(%r30),%r2

	/* if success, load fd's from stack array */

	LDREG	-56(%r30),%r28
	LDREG	-52(%r30),%r29

pipe_exit:
	bv %r0(%r2)
	ldo -64(%r30),%r30

	.export hpux_syscall_exit
	.import syscall_exit

hpux_syscall_exit:

	/*
	 *
	 * HP-UX call return conventions:
	 *
	 * if error:
	 *	 r22 = 1
	 *	 r28 = errno value
	 *	 r29 = secondary return value
	 * else
	 *	 r22 = 0
	 *	 r28 = return value
	 *	 r29 = secondary return value
	 *
	 * For now, we'll just check to see if r28 is < (unsigned long)-1024
	 * (to handle addresses > 2 Gb) and if so set r22 to zero. If not,
	 * we'll complement r28 and set r22 to 1. Wrappers will be
	 * needed for syscalls that care about the secondary return value.
	 * The wrapper may also need a way of avoiding the following code,
	 * but we'll deal with that when it becomes necessary.
	 */

	ldo -1024(%r0),%r1
	comb,<< %r28,%r1,no_error
	copy %r0,%r22
	subi 0,%r28,%r28
	ldo 1(%r0),%r22

no_error:
	b syscall_exit
	nop

	.export hpux_unimplemented_wrapper
	.import hpux_unimplemented

hpux_unimplemented_wrapper:
	b hpux_unimplemented
	STREG %r22,-64(%r30)  /* overwrite arg8 with syscall number */
