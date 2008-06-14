/* 
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 * 
 * System call entry code Copyright (c) Matthew Wilcox 1999 <willy@bofh.ai>
 * Licensed under the GNU GPL.
 * thanks to Philipp Rumpf, Mike Shaver and various others
 * sorry about the wall, puffin..
 */

#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/page.h>
#include <asm/psw.h>
#include <asm/thread_info.h>
#include <asm/assembly.h>
#include <asm/processor.h>

#include <linux/linkage.h>

	/* We fill the empty parts of the gateway page with
 	 * something that will kill the kernel or a
 	 * userspace application.
	 */
#define KILL_INSN	break	0,0

	.level          LEVEL

	.text

	.import syscall_exit,code
	.import syscall_exit_rfi,code

	/* Linux gateway page is aliased to virtual page 0 in the kernel
	 * address space. Since it is a gateway page it cannot be
	 * dereferenced, so null pointers will still fault. We start
	 * the actual entry point at 0x100. We put break instructions
	 * at the beginning of the page to trap null indirect function
	 * pointers.
	 */

	.align PAGE_SIZE
ENTRY(linux_gateway_page)

        /* ADDRESS 0x00 to 0xb0 = 176 bytes / 4 bytes per insn = 44 insns */
	.rept 44
	KILL_INSN
	.endr

	/* ADDRESS 0xb0 to 0xb4, lws uses 1 insns for entry */
	/* Light-weight-syscall entry must always be located at 0xb0 */
	/* WARNING: Keep this number updated with table size changes */
#define __NR_lws_entries (2)

lws_entry:
	/* Unconditional branch to lws_start, located on the 
	   same gateway page */
	b,n	lws_start

	/* Fill from 0xb4 to 0xe0 */
	.rept 11
	KILL_INSN
	.endr

	/* This function MUST be located at 0xe0 for glibc's threading 
	mechanism to work. DO NOT MOVE THIS CODE EVER! */
set_thread_pointer:
	gate	.+8, %r0		/* increase privilege */
	depi	3, 31, 2, %r31		/* Ensure we return into user mode. */
	be	0(%sr7,%r31)		/* return to user space */
	mtctl	%r26, %cr27		/* move arg0 to the control register */

	/* Increase the chance of trapping if random jumps occur to this
	address, fill from 0xf0 to 0x100 */
	.rept 4
	KILL_INSN
	.endr

/* This address must remain fixed at 0x100 for glibc's syscalls to work */
	.align 256
linux_gateway_entry:
	gate	.+8, %r0			/* become privileged */
	mtsp	%r0,%sr4			/* get kernel space into sr4 */
	mtsp	%r0,%sr5			/* get kernel space into sr5 */
	mtsp	%r0,%sr6			/* get kernel space into sr6 */
	mfsp    %sr7,%r1                        /* save user sr7 */
	mtsp    %r1,%sr3                        /* and store it in sr3 */

#ifdef CONFIG_64BIT
	/* for now we can *always* set the W bit on entry to the syscall
	 * since we don't support wide userland processes.  We could
	 * also save the current SM other than in r0 and restore it on
	 * exit from the syscall, and also use that value to know
	 * whether to do narrow or wide syscalls. -PB
	 */
	ssm	PSW_SM_W, %r1
	extrd,u	%r1,PSW_W_BIT,1,%r1
	/* sp must be aligned on 4, so deposit the W bit setting into
	 * the bottom of sp temporarily */
	or,ev	%r1,%r30,%r30
	b,n	1f
	/* The top halves of argument registers must be cleared on syscall
	 * entry from narrow executable.
	 */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
	depdi	0, 31, 32, %r23
	depdi	0, 31, 32, %r22
	depdi	0, 31, 32, %r21
1:	
#endif
	mfctl   %cr30,%r1
	xor     %r1,%r30,%r30                   /* ye olde xor trick */
	xor     %r1,%r30,%r1
	xor     %r1,%r30,%r30
	
	ldo     THREAD_SZ_ALGN+FRAME_SIZE(%r30),%r30  /* set up kernel stack */

	/* N.B.: It is critical that we don't set sr7 to 0 until r30
	 *       contains a valid kernel stack pointer. It is also
	 *       critical that we don't start using the kernel stack
	 *       until after sr7 has been set to 0.
	 */

	mtsp	%r0,%sr7			/* get kernel space into sr7 */
	STREGM	%r1,FRAME_SIZE(%r30)		/* save r1 (usp) here for now */
	mfctl	%cr30,%r1			/* get task ptr in %r1 */
	LDREG	TI_TASK(%r1),%r1

	/* Save some registers for sigcontext and potential task
	   switch (see entry.S for the details of which ones are
	   saved/restored).  TASK_PT_PSW is zeroed so we can see whether
	   a process is on a syscall or not.  For an interrupt the real
	   PSW value is stored.  This is needed for gdb and sys_ptrace. */
	STREG	%r0,  TASK_PT_PSW(%r1)
	STREG	%r2,  TASK_PT_GR2(%r1)		/* preserve rp */
	STREG	%r19, TASK_PT_GR19(%r1)

	LDREGM	-FRAME_SIZE(%r30), %r2		/* get users sp back */
#ifdef CONFIG_64BIT
	extrd,u	%r2,63,1,%r19			/* W hidden in bottom bit */
#if 0
	xor	%r19,%r2,%r2			/* clear bottom bit */
	depd,z	%r19,1,1,%r19
	std	%r19,TASK_PT_PSW(%r1)
#endif
#endif
	STREG	%r2,  TASK_PT_GR30(%r1)		/* ... and save it */
	
	STREG	%r20, TASK_PT_GR20(%r1)		/* Syscall number */
	STREG	%r21, TASK_PT_GR21(%r1)
	STREG	%r22, TASK_PT_GR22(%r1)
	STREG	%r23, TASK_PT_GR23(%r1)		/* 4th argument */
	STREG	%r24, TASK_PT_GR24(%r1)		/* 3rd argument */
	STREG	%r25, TASK_PT_GR25(%r1)		/* 2nd argument */
	STREG	%r26, TASK_PT_GR26(%r1)	 	/* 1st argument */
	STREG	%r27, TASK_PT_GR27(%r1)		/* user dp */
	STREG   %r28, TASK_PT_GR28(%r1)         /* return value 0 */
	STREG   %r28, TASK_PT_ORIG_R28(%r1)     /* return value 0 (saved for signals) */
	STREG	%r29, TASK_PT_GR29(%r1)		/* return value 1 */
	STREG	%r31, TASK_PT_GR31(%r1)		/* preserve syscall return ptr */
	
	ldo	TASK_PT_FR0(%r1), %r27		/* save fpregs from the kernel */
	save_fp	%r27				/* or potential task switch  */

	mfctl	%cr11, %r27			/* i.e. SAR */
	STREG	%r27, TASK_PT_SAR(%r1)

	loadgp

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
	copy	%r19,%r2			/* W bit back to r2 */
#else
	/* no need to save these on stack in wide mode because the first 8
	 * args are passed in registers */
	stw     %r22, -52(%r30)                 /* 5th argument */
	stw     %r21, -56(%r30)                 /* 6th argument */
#endif

	/* Are we being ptraced? */
	mfctl	%cr30, %r1
	LDREG	TI_TASK(%r1),%r1
	ldw	TASK_PTRACE(%r1), %r1
	bb,<,n	%r1,31,.Ltracesys
	
	/* Note!  We cannot use the syscall table that is mapped
	nearby since the gateway page is mapped execute-only. */

#ifdef CONFIG_64BIT
	ldil	L%sys_call_table, %r1
	or,=	%r2,%r2,%r2
	addil	L%(sys_call_table64-sys_call_table), %r1
	ldo	R%sys_call_table(%r1), %r19
	or,=	%r2,%r2,%r2
	ldo	R%sys_call_table64(%r1), %r19
#else
	ldil	L%sys_call_table, %r1
	ldo     R%sys_call_table(%r1), %r19
#endif	
	comiclr,>>	__NR_Linux_syscalls, %r20, %r0
	b,n	.Lsyscall_nosys
	
	LDREGX  %r20(%r19), %r19

	/* If this is a sys_rt_sigreturn call, and the signal was received
	 * when not in_syscall, then we want to return via syscall_exit_rfi,
	 * not syscall_exit.  Signal no. in r20, in_syscall in r25 (see
	 * trampoline code in signal.c).
	 */
	ldi	__NR_rt_sigreturn,%r2
	comb,=	%r2,%r20,.Lrt_sigreturn
.Lin_syscall:
	ldil	L%syscall_exit,%r2
	be      0(%sr7,%r19)
	ldo	R%syscall_exit(%r2),%r2
.Lrt_sigreturn:
	comib,<> 0,%r25,.Lin_syscall
	ldil	L%syscall_exit_rfi,%r2
	be      0(%sr7,%r19)
	ldo	R%syscall_exit_rfi(%r2),%r2

	/* Note!  Because we are not running where we were linked, any
	calls to functions external to this file must be indirect.  To
	be safe, we apply the opposite rule to functions within this
	file, with local labels given to them to ensure correctness. */
	
.Lsyscall_nosys:
syscall_nosys:
	ldil	L%syscall_exit,%r1
	be	R%syscall_exit(%sr7,%r1)
	ldo	-ENOSYS(%r0),%r28		   /* set errno */


/* Warning! This trace code is a virtual duplicate of the code above so be
 * sure to maintain both! */
.Ltracesys:
tracesys:
	/* Need to save more registers so the debugger can see where we
	 * are.  This saves only the lower 8 bits of PSW, so that the C
	 * bit is still clear on syscalls, and the D bit is set if this
	 * full register save path has been executed.  We check the D
	 * bit on syscall_return_rfi to determine which registers to
	 * restore.  An interrupt results in a full PSW saved with the
	 * C bit set, a non-straced syscall entry results in C and D clear
	 * in the saved PSW.
	 */
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	ssm	0,%r2
	STREG	%r2,TASK_PT_PSW(%r1)		/* Lower 8 bits only!! */
	mfsp	%sr0,%r2
	STREG	%r2,TASK_PT_SR0(%r1)
	mfsp	%sr1,%r2
	STREG	%r2,TASK_PT_SR1(%r1)
	mfsp	%sr2,%r2
	STREG	%r2,TASK_PT_SR2(%r1)
	mfsp	%sr3,%r2
	STREG	%r2,TASK_PT_SR3(%r1)
	STREG	%r2,TASK_PT_SR4(%r1)
	STREG	%r2,TASK_PT_SR5(%r1)
	STREG	%r2,TASK_PT_SR6(%r1)
	STREG	%r2,TASK_PT_SR7(%r1)
	STREG	%r2,TASK_PT_IASQ0(%r1)
	STREG	%r2,TASK_PT_IASQ1(%r1)
	LDREG	TASK_PT_GR31(%r1),%r2
	STREG	%r2,TASK_PT_IAOQ0(%r1)
	ldo	4(%r2),%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	ldo	TASK_REGS(%r1),%r2
	/* reg_save %r2 */
	STREG	%r3,PT_GR3(%r2)
	STREG	%r4,PT_GR4(%r2)
	STREG	%r5,PT_GR5(%r2)
	STREG	%r6,PT_GR6(%r2)
	STREG	%r7,PT_GR7(%r2)
	STREG	%r8,PT_GR8(%r2)
	STREG	%r9,PT_GR9(%r2)
	STREG	%r10,PT_GR10(%r2)
	STREG	%r11,PT_GR11(%r2)
	STREG	%r12,PT_GR12(%r2)
	STREG	%r13,PT_GR13(%r2)
	STREG	%r14,PT_GR14(%r2)
	STREG	%r15,PT_GR15(%r2)
	STREG	%r16,PT_GR16(%r2)
	STREG	%r17,PT_GR17(%r2)
	STREG	%r18,PT_GR18(%r2)
	/* Finished saving things for the debugger */

	ldil	L%syscall_trace,%r1
	ldil	L%tracesys_next,%r2
	be	R%syscall_trace(%sr7,%r1)
	ldo	R%tracesys_next(%r2),%r2
	
tracesys_next:	
	ldil	L%sys_call_table,%r1
	ldo     R%sys_call_table(%r1), %r19

	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	LDREG   TASK_PT_GR20(%r1), %r20
	LDREG   TASK_PT_GR26(%r1), %r26		/* Restore the users args */
	LDREG   TASK_PT_GR25(%r1), %r25
	LDREG   TASK_PT_GR24(%r1), %r24
	LDREG   TASK_PT_GR23(%r1), %r23
#ifdef CONFIG_64BIT
	LDREG   TASK_PT_GR22(%r1), %r22
	LDREG   TASK_PT_GR21(%r1), %r21
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif

	comiclr,>>=	__NR_Linux_syscalls, %r20, %r0
	b,n	.Lsyscall_nosys

	LDREGX  %r20(%r19), %r19

	/* If this is a sys_rt_sigreturn call, and the signal was received
	 * when not in_syscall, then we want to return via syscall_exit_rfi,
	 * not syscall_exit.  Signal no. in r20, in_syscall in r25 (see
	 * trampoline code in signal.c).
	 */
	ldi	__NR_rt_sigreturn,%r2
	comb,=	%r2,%r20,.Ltrace_rt_sigreturn
.Ltrace_in_syscall:
	ldil	L%tracesys_exit,%r2
	be      0(%sr7,%r19)
	ldo	R%tracesys_exit(%r2),%r2

	/* Do *not* call this function on the gateway page, because it
	makes a direct call to syscall_trace. */
	
tracesys_exit:
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	bl	syscall_trace, %r2
	STREG   %r28,TASK_PT_GR28(%r1)          /* save return value now */
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	TI_TASK(%r1), %r1
	LDREG   TASK_PT_GR28(%r1), %r28		/* Restore return val. */

	ldil	L%syscall_exit,%r1
	be,n	R%syscall_exit(%sr7,%r1)

.Ltrace_rt_sigreturn:
	comib,<> 0,%r25,.Ltrace_in_syscall
	ldil	L%tracesys_sigexit,%r2
	be      0(%sr7,%r19)
	ldo	R%tracesys_sigexit(%r2),%r2

tracesys_sigexit:
	ldo     -THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1      /* get task ptr */
	LDREG	0(%r1), %r1
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	bl	syscall_trace, %r2
	nop

	ldil	L%syscall_exit_rfi,%r1
	be,n	R%syscall_exit_rfi(%sr7,%r1)


	/*********************************************************
		Light-weight-syscall code

		r20 - lws number
		r26,r25,r24,r23,r22 - Input registers
		r28 - Function return register
		r21 - Error code.

		Scracth: Any of the above that aren't being
		currently used, including r1. 

		Return pointer: r31 (Not usable)

		Error codes returned by entry path:

		ENOSYS - r20 was an invalid LWS number.

	*********************************************************/
lws_start:
	/* Gate and ensure we return to userspace */
	gate	.+8, %r0
	depi	3, 31, 2, %r31	/* Ensure we return to userspace */

#ifdef CONFIG_64BIT
	/* FIXME: If we are a 64-bit kernel just
	 *        turn this on unconditionally.
	 */
	ssm	PSW_SM_W, %r1
	extrd,u	%r1,PSW_W_BIT,1,%r1
	/* sp must be aligned on 4, so deposit the W bit setting into
	 * the bottom of sp temporarily */
	or,ev	%r1,%r30,%r30

	/* Clip LWS number to a 32-bit value always */
	depdi	0, 31, 32, %r20
#endif	

        /* Is the lws entry number valid? */
	comiclr,>>=	__NR_lws_entries, %r20, %r0
	b,n	lws_exit_nosys

	/* WARNING: Trashing sr2 and sr3 */
	mfsp	%sr7,%r1			/* get userspace into sr3 */
	mtsp	%r1,%sr3
	mtsp	%r0,%sr2			/* get kernel space into sr2 */

	/* Load table start */
	ldil	L%lws_table, %r1
	ldo	R%lws_table(%r1), %r28	/* Scratch use of r28 */
	LDREGX	%r20(%sr2,r28), %r21	/* Scratch use of r21 */

	/* Jump to lws, lws table pointers already relocated */
	be,n	0(%sr2,%r21)

lws_exit_nosys:
	ldo	-ENOSYS(%r0),%r21		   /* set errno */
	/* Fall through: Return to userspace */

lws_exit:
#ifdef CONFIG_64BIT
	/* decide whether to reset the wide mode bit
	 *
	 * For a syscall, the W bit is stored in the lowest bit
	 * of sp.  Extract it and reset W if it is zero */
	extrd,u,*<>	%r30,63,1,%r1
	rsm	PSW_SM_W, %r0
	/* now reset the lowest bit of sp if it was set */
	xor	%r30,%r1,%r30
#endif
	be,n	0(%sr3, %r31)


	
	/***************************************************
		Implementing CAS as an atomic operation:

		%r26 - Address to examine
		%r25 - Old value to check (old)
		%r24 - New value to set (new)
		%r28 - Return prev through this register.
		%r21 - Kernel error code

		If debugging is DISabled:

		%r21 has the following meanings:

		EAGAIN - CAS is busy, ldcw failed, try again.
		EFAULT - Read or write failed.		

		If debugging is enabled:

		EDEADLOCK - CAS called recursively.
		EAGAIN && r28 == 1 - CAS is busy. Lock contended.
		EAGAIN && r28 == 2 - CAS is busy. ldcw failed.
		EFAULT - Read or write failed.

		Scratch: r20, r28, r1

	****************************************************/

	/* Do not enable LWS debugging */
#define ENABLE_LWS_DEBUG 0 

	/* ELF64 Process entry path */
lws_compare_and_swap64:
#ifdef CONFIG_64BIT
	b,n	lws_compare_and_swap
#else
	/* If we are not a 64-bit kernel, then we don't
	 * implement having 64-bit input registers
	 */
	b,n	lws_exit_nosys
#endif

	/* ELF32 Process entry path */
lws_compare_and_swap32:
#ifdef CONFIG_64BIT
	/* Clip all the input registers */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
#endif

lws_compare_and_swap:
#ifdef CONFIG_SMP
	/* Load start of lock table */
	ldil	L%lws_lock_start, %r20
	ldo	R%lws_lock_start(%r20), %r28

	/* Extract four bits from r26 and hash lock (Bits 4-7) */
	extru  %r26, 27, 4, %r20

	/* Find lock to use, the hash is either one of 0 to
	   15, multiplied by 16 (keep it 16-byte aligned)
	   and add to the lock table offset. */
	shlw	%r20, 4, %r20
	add	%r20, %r28, %r20

# if ENABLE_LWS_DEBUG
	/*	
		DEBUG, check for deadlock! 
		If the thread register values are the same
		then we were the one that locked it last and
		this is a recurisve call that will deadlock.
		We *must* giveup this call and fail.
	*/
	ldw	4(%sr2,%r20), %r28			/* Load thread register */
	/* WARNING: If cr27 cycles to the same value we have problems */
	mfctl	%cr27, %r21				/* Get current thread register */
	cmpb,<>,n	%r21, %r28, cas_lock		/* Called recursive? */
	b	lws_exit				/* Return error! */
	ldo	-EDEADLOCK(%r0), %r21
cas_lock:
	cmpb,=,n	%r0, %r28, cas_nocontend	/* Is nobody using it? */
	ldo	1(%r0), %r28				/* 1st case */
	b	lws_exit				/* Contended... */
	ldo	-EAGAIN(%r0), %r21			/* Spin in userspace */
cas_nocontend:
# endif
/* ENABLE_LWS_DEBUG */

	LDCW	0(%sr2,%r20), %r28			/* Try to acquire the lock */
	cmpb,<>,n	%r0, %r28, cas_action		/* Did we get it? */
cas_wouldblock:
	ldo	2(%r0), %r28				/* 2nd case */
	b	lws_exit				/* Contended... */
	ldo	-EAGAIN(%r0), %r21			/* Spin in userspace */
#endif
/* CONFIG_SMP */

	/*
		prev = *addr;
		if ( prev == old )
		  *addr = new;
		return prev;
	*/

	/* NOTES:
		This all works becuse intr_do_signal
		and schedule both check the return iasq
		and see that we are on the kernel page
		so this process is never scheduled off
		or is ever sent any signal of any sort,
		thus it is wholly atomic from usrspaces
		perspective
	*/
cas_action:
#if defined CONFIG_SMP && ENABLE_LWS_DEBUG
	/* DEBUG */
	mfctl	%cr27, %r1
	stw	%r1, 4(%sr2,%r20)
#endif
	/* The load and store could fail */
1:	ldw	0(%sr3,%r26), %r28
	sub,<>	%r28, %r25, %r0
2:	stw	%r24, 0(%sr3,%r26)
#ifdef CONFIG_SMP
	/* Free lock */
	stw	%r20, 0(%sr2,%r20)
# if ENABLE_LWS_DEBUG
	/* Clear thread register indicator */
	stw	%r0, 4(%sr2,%r20)
# endif
#endif
	/* Return to userspace, set no error */
	b	lws_exit
	copy	%r0, %r21

3:		
	/* Error occured on load or store */
#ifdef CONFIG_SMP
	/* Free lock */
	stw	%r20, 0(%sr2,%r20)
# if ENABLE_LWS_DEBUG
	stw	%r0, 4(%sr2,%r20)
# endif
#endif
	b	lws_exit
	ldo	-EFAULT(%r0),%r21	/* set errno */
	nop
	nop
	nop
	nop

	/* Two exception table entries, one for the load,
	   the other for the store. Either return -EFAULT.
	   Each of the entries must be relocated. */
	.section __ex_table,"aw"
	ASM_ULONG_INSN (1b - linux_gateway_page), (3b - linux_gateway_page)
	ASM_ULONG_INSN (2b - linux_gateway_page), (3b - linux_gateway_page)
	.previous


	/* Make sure nothing else is placed on this page */
	.align PAGE_SIZE
END(linux_gateway_page)
ENTRY(end_linux_gateway_page)

	/* Relocate symbols assuming linux_gateway_page is mapped
	   to virtual address 0x0 */

#define LWS_ENTRY(_name_) ASM_ULONG_INSN (lws_##_name_ - linux_gateway_page)

	.section .rodata,"a"

	.align PAGE_SIZE
	/* Light-weight-syscall table */
	/* Start of lws table. */
ENTRY(lws_table)
	LWS_ENTRY(compare_and_swap32)	/* 0 - ELF32 Atomic compare and swap */
	LWS_ENTRY(compare_and_swap64)	/* 1 - ELF64 Atomic compare and swap */
END(lws_table)
	/* End of lws table */

	.align PAGE_SIZE
ENTRY(sys_call_table)
#include "syscall_table.S"
END(sys_call_table)

#ifdef CONFIG_64BIT
	.align PAGE_SIZE
ENTRY(sys_call_table64)
#define SYSCALL_TABLE_64BIT
#include "syscall_table.S"
END(sys_call_table64)
#endif

#ifdef CONFIG_SMP
	/*
		All light-weight-syscall atomic operations 
		will use this set of locks 
	*/
	.section .data
	.align	PAGE_SIZE
ENTRY(lws_lock_start)
	/* lws locks */
	.align 16
	.rept 16
	/* Keep locks aligned at 16-bytes */
	.word 1
	.word 0 
	.word 0
	.word 0
	.endr
END(lws_lock_start)
	.previous
#endif
/* CONFIG_SMP for lws_lock_start */

.end


