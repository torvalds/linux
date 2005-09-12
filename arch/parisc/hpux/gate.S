/*
 *
 * Linux/PARISC Project (http://www.parisc-linux.org/)
 *
 * System call entry code Copyright (c) Matthew Wilcox 1999 <willy@bofh.ai>
 * Licensed under the GNU GPL.
 * thanks to Philipp Rumpf, Mike Shaver and various others
 * sorry about the wall, puffin..
 */

#include <asm/assembly.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/errno.h>

#ifdef __LP64__
	.level          2.0w
#else
	.level		1.1
#endif
	.text

#ifdef __LP64__
#define FRAME_SIZE	128
#else
#define FRAME_SIZE	64
#endif
	.import hpux_call_table
	.import hpux_syscall_exit,code
	.export hpux_gateway_page

	.align 4096
hpux_gateway_page:
	nop
#ifdef __LP64__
#warning NEEDS WORK for 64-bit
#endif
	ldw     -64(%r30), %r29                 ;! 8th argument
	ldw	-60(%r30), %r19			;! 7th argument
	ldw	-56(%r30), %r20			;! 6th argument
	ldw	-52(%r30), %r21			;! 5th argument
	gate	.+8, %r0			/* become privileged */
	mtsp	%r0,%sr4			/* get kernel space into sr4 */
	mtsp	%r0,%sr5			/* get kernel space into sr5 */
	mtsp	%r0,%sr6			/* get kernel space into sr6 */
	mfsp    %sr7,%r1                        /* save user sr7 */
	mtsp    %r1,%sr3                        /* and store it in sr3 */

	mtctl   %r30,%cr28
	mfctl   %cr30,%r1
	xor     %r1,%r30,%r30                   /* ye olde xor trick */
	xor     %r1,%r30,%r1
	xor     %r1,%r30,%r30
	ldo     TASK_SZ_ALGN+FRAME_SIZE(%r30),%r30  /* set up kernel stack */

	/* N.B.: It is critical that we don't set sr7 to 0 until r30
	 *       contains a valid kernel stack pointer. It is also
	 *       critical that we don't start using the kernel stack
	 *       until after sr7 has been set to 0.
	 */

	mtsp	%r0,%sr7			/* get kernel space into sr7 */
	STREG   %r1,TASK_PT_GR30-TASK_SZ_ALGN-FRAME_SIZE(%r30) /* save usp */
	ldo     -TASK_SZ_ALGN-FRAME_SIZE(%r30),%r1   /* get task ptr in %r1 */

	/* Save some registers for sigcontext and potential task
	   switch (see entry.S for the details of which ones are
	   saved/restored).  TASK_PT_PSW is zeroed so we can see whether
	   a process is on a syscall or not.  For an interrupt the real
	   PSW value is stored.  This is needed for gdb and sys_ptrace. */
	STREG	%r0,  TASK_PT_PSW(%r1)
	STREG	%r2,  TASK_PT_GR2(%r1)		/* preserve rp */
	STREG   %r19, TASK_PT_GR19(%r1)         /* 7th argument */
	STREG   %r20, TASK_PT_GR20(%r1)         /* 6th argument */
	STREG   %r21, TASK_PT_GR21(%r1)         /* 5th argument */
	STREG   %r22, TASK_PT_GR22(%r1)         /* syscall # */
	STREG	%r23, TASK_PT_GR23(%r1)		/* 4th argument */
	STREG	%r24, TASK_PT_GR24(%r1)		/* 3rd argument */
	STREG	%r25, TASK_PT_GR25(%r1)		/* 2nd argument */
	STREG	%r26, TASK_PT_GR26(%r1)	 	/* 1st argument */
	STREG	%r27, TASK_PT_GR27(%r1)		/* user dp */
	STREG   %r28, TASK_PT_GR28(%r1)         /* return value 0 */
	STREG   %r28, TASK_PT_ORIG_R28(%r1)     /* return value 0 (saved for signals) */
	STREG   %r29, TASK_PT_GR29(%r1)         /* 8th argument */
	STREG	%r31, TASK_PT_GR31(%r1)		/* preserve syscall return ptr */
	
	ldo	TASK_PT_FR0(%r1), %r27		/* save fpregs from the kernel */
	save_fp	%r27				/* or potential task switch  */

	mfctl	%cr11, %r27			/* i.e. SAR */
	STREG	%r27, TASK_PT_SAR(%r1)

	loadgp

	stw	%r21, -52(%r30)			;! 5th argument
	stw	%r20, -56(%r30)			;! 6th argument
	stw	%r19, -60(%r30)			;! 7th argument
	stw     %r29, -64(%r30)                 ;! 8th argument

	ldil	L%hpux_call_table, %r21
	ldo	R%hpux_call_table(%r21), %r21
	comiclr,>>=	__NR_HPUX_syscalls, %r22, %r0
	b,n	syscall_nosys
	ldwx,s	%r22(%r21), %r21
	ldil	L%hpux_syscall_exit,%r2
	be	0(%sr7,%r21)
	ldo	R%hpux_syscall_exit(%r2),%r2

syscall_nosys:
	ldil	L%hpux_syscall_exit,%r1
	be	R%hpux_syscall_exit(%sr7,%r1)
	ldo	-ENOSYS(%r0),%r28

	.align 4096
	.export end_hpux_gateway_page
end_hpux_gateway_page:
