/* 
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 * 
 * System call entry code / Linux gateway page
 * Copyright (c) Matthew Wilcox 1999 <willy@infradead.org>
 * Licensed under the GNU GPL.
 * thanks to Philipp Rumpf, Mike Shaver and various others
 * sorry about the wall, puffin..
 */

/*
How does the Linux gateway page on PA-RISC work?
------------------------------------------------
The Linux gateway page on PA-RISC is "special".
It actually has PAGE_GATEWAY bits set (this is linux terminology; in parisc
terminology it's Execute, promote to PL0) in the page map.  So anything
executing on this page executes with kernel level privilege (there's more to it
than that: to have this happen, you also have to use a branch with a ,gate
completer to activate the privilege promotion).  The upshot is that everything
that runs on the gateway page runs at kernel privilege but with the current
user process address space (although you have access to kernel space via %sr2).
For the 0x100 syscall entry, we redo the space registers to point to the kernel
address space (preserving the user address space in %sr3), move to wide mode if
required, save the user registers and branch into the kernel syscall entry
point.  For all the other functions, we execute at kernel privilege but don't
flip address spaces. The basic upshot of this is that these code snippets are
executed atomically (because the kernel can't be pre-empted) and they may
perform architecturally forbidden (to PL3) operations (like setting control
registers).
*/


#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/page.h>
#include <asm/psw.h>
#include <asm/thread_info.h>
#include <asm/assembly.h>
#include <asm/processor.h>
#include <asm/cache.h>

#include <linux/linkage.h>

	/* We fill the empty parts of the gateway page with
 	 * something that will kill the kernel or a
 	 * userspace application.
	 */
#define KILL_INSN	break	0,0

	.level          PA_ASM_LEVEL

	.macro	lws_pagefault_disable reg1,reg2
	mfctl	%cr30, \reg2
	ldo	TASK_PAGEFAULT_DISABLED(\reg2), \reg2
	ldw	0(%sr2,\reg2), \reg1
	ldo	1(\reg1), \reg1
	stw	\reg1, 0(%sr2,\reg2)
	.endm

	.macro	lws_pagefault_enable reg1,reg2
	mfctl	%cr30, \reg2
	ldo	TASK_PAGEFAULT_DISABLED(\reg2), \reg2
	ldw	0(%sr2,\reg2), \reg1
	ldo	-1(\reg1), \reg1
	stw	\reg1, 0(%sr2,\reg2)
	.endm

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

	/* ADDRESS 0xb0 to 0xb8, lws uses two insns for entry */
	/* Light-weight-syscall entry must always be located at 0xb0 */
	/* WARNING: Keep this number updated with table size changes */
#define __NR_lws_entries (5)

lws_entry:
	gate	lws_start, %r0		/* increase privilege */
	depi	PRIV_USER, 31, 2, %r31	/* Ensure we return into user mode. */

	/* Fill from 0xb8 to 0xe0 */
	.rept 10
	KILL_INSN
	.endr

	/* This function MUST be located at 0xe0 for glibc's threading 
	mechanism to work. DO NOT MOVE THIS CODE EVER! */
set_thread_pointer:
	gate	.+8, %r0		/* increase privilege */
	depi	PRIV_USER, 31, 2, %r31	/* Ensure we return into user mode. */
	be	0(%sr7,%r31)		/* return to user space */
	mtctl	%r26, %cr27		/* move arg0 to the control register */

	/* Increase the chance of trapping if random jumps occur to this
	address, fill from 0xf0 to 0x100 */
	.rept 4
	KILL_INSN
	.endr

/* This address must remain fixed at 0x100 for glibc's syscalls to work */
	.align LINUX_GATEWAY_ADDR
linux_gateway_entry:
	gate	.+8, %r0			/* become privileged */
	mtsp	%r0,%sr4			/* get kernel space into sr4 */
	mtsp	%r0,%sr5			/* get kernel space into sr5 */
	mtsp	%r0,%sr6			/* get kernel space into sr6 */

#ifdef CONFIG_64BIT
	/* Store W bit on entry to the syscall in case it's a wide userland
	 * process. */
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

	/* We use a rsm/ssm pair to prevent sr3 from being clobbered
	 * by external interrupts.
	 */
	mfsp    %sr7,%r1                        /* save user sr7 */
	rsm	PSW_SM_I, %r0			/* disable interrupts */
	mtsp    %r1,%sr3                        /* and store it in sr3 */

	mfctl   %cr30,%r1
	xor     %r1,%r30,%r30                   /* ye olde xor trick */
	xor     %r1,%r30,%r1
	xor     %r1,%r30,%r30

	LDREG	TASK_STACK(%r30),%r30		/* set up kernel stack */
	ldo	FRAME_SIZE(%r30),%r30
	/* N.B.: It is critical that we don't set sr7 to 0 until r30
	 *       contains a valid kernel stack pointer. It is also
	 *       critical that we don't start using the kernel stack
	 *       until after sr7 has been set to 0.
	 */

	mtsp	%r0,%sr7			/* get kernel space into sr7 */
	ssm	PSW_SM_I, %r0			/* enable interrupts */
	STREGM	%r1,FRAME_SIZE(%r30)		/* save r1 (usp) here for now */
	mfctl	%cr30,%r1			/* get task ptr in %r1 */

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
	STREG   %r0, TASK_PT_ORIG_R28(%r1)      /* don't prohibit restarts */
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
	LDREG	TASK_TI_FLAGS(%r1),%r1
	ldi	_TIF_SYSCALL_TRACE_MASK, %r19
	and,COND(=) %r1, %r19, %r0
	b,n	.Ltracesys
	
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
	load32	sys_call_table, %r19
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
	mfctl	%cr30,%r1			/* get task ptr */
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

	copy	%r2,%r26
	ldil	L%do_syscall_trace_enter,%r1
	ldil	L%tracesys_next,%r2
	be	R%do_syscall_trace_enter(%sr7,%r1)
	ldo	R%tracesys_next(%r2),%r2
	
tracesys_next:
	/* do_syscall_trace_enter either returned the syscallno, or -1L,
	 *  so we skip restoring the PT_GR20 below, since we pulled it from
	 *  task->thread.regs.gr[20] above.
	 */
	copy	%ret0,%r20

	mfctl	%cr30,%r1			/* get task ptr */
	LDREG   TASK_PT_GR28(%r1), %r28		/* Restore return value */
	LDREG   TASK_PT_GR26(%r1), %r26		/* Restore the users args */
	LDREG   TASK_PT_GR25(%r1), %r25
	LDREG   TASK_PT_GR24(%r1), %r24
	LDREG   TASK_PT_GR23(%r1), %r23
	LDREG   TASK_PT_GR22(%r1), %r22
	LDREG   TASK_PT_GR21(%r1), %r21
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#else
	stw     %r22, -52(%r30)                 /* 5th argument */
	stw     %r21, -56(%r30)                 /* 6th argument */
#endif

	cmpib,COND(=),n -1,%r20,tracesys_exit /* seccomp may have returned -1 */
	comiclr,>>	__NR_Linux_syscalls, %r20, %r0
	b,n	.Ltracesys_nosys

	/* Note!  We cannot use the syscall table that is mapped
	nearby since the gateway page is mapped execute-only. */

#ifdef CONFIG_64BIT
	LDREG	TASK_PT_GR30(%r1), %r19		/* get users sp back */
	extrd,u	%r19,63,1,%r2			/* W hidden in bottom bit */

	ldil	L%sys_call_table, %r1
	or,=	%r2,%r2,%r2
	addil	L%(sys_call_table64-sys_call_table), %r1
	ldo	R%sys_call_table(%r1), %r19
	or,=	%r2,%r2,%r2
	ldo	R%sys_call_table64(%r1), %r19
#else
	load32	sys_call_table, %r19
#endif

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

.Ltracesys_nosys:
	ldo	-ENOSYS(%r0),%r28		/* set errno */

	/* Do *not* call this function on the gateway page, because it
	makes a direct call to syscall_trace. */
	
tracesys_exit:
	mfctl	%cr30,%r1			/* get task ptr */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	ldo	TASK_REGS(%r1),%r26
	BL	do_syscall_trace_exit,%r2
	STREG   %r28,TASK_PT_GR28(%r1)          /* save return value now */
	mfctl	%cr30,%r1			/* get task ptr */
	LDREG   TASK_PT_GR28(%r1), %r28		/* Restore return val. */

	ldil	L%syscall_exit,%r1
	be,n	R%syscall_exit(%sr7,%r1)

.Ltrace_rt_sigreturn:
	comib,<> 0,%r25,.Ltrace_in_syscall
	ldil	L%tracesys_sigexit,%r2
	be      0(%sr7,%r19)
	ldo	R%tracesys_sigexit(%r2),%r2

tracesys_sigexit:
	mfctl	%cr30,%r1			/* get task ptr */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	BL	do_syscall_trace_exit,%r2
	ldo	TASK_REGS(%r1),%r26

	ldil	L%syscall_exit_rfi,%r1
	be,n	R%syscall_exit_rfi(%sr7,%r1)


	/*********************************************************
		32/64-bit Light-Weight-Syscall ABI

		* - Indicates a hint for userspace inline asm
		implementations.

		Syscall number (caller-saves)
	        - %r20
	        * In asm clobber.

		Argument registers (caller-saves)
	        - %r26, %r25, %r24, %r23, %r22
	        * In asm input.

		Return registers (caller-saves)
	        - %r28 (return), %r21 (errno)
	        * In asm output.

		Caller-saves registers
	        - %r1, %r27, %r29
	        - %r2 (return pointer)
	        - %r31 (ble link register)
	        * In asm clobber.

		Callee-saves registers
	        - %r3-%r18
	        - %r30 (stack pointer)
	        * Not in asm clobber.

		If userspace is 32-bit:
		Callee-saves registers
	        - %r19 (32-bit PIC register)

		Differences from 32-bit calling convention:
		- Syscall number in %r20
		- Additional argument register %r22 (arg4)
		- Callee-saves %r19.

		If userspace is 64-bit:
		Callee-saves registers
		- %r27 (64-bit PIC register)

		Differences from 64-bit calling convention:
		- Syscall number in %r20
		- Additional argument register %r22 (arg4)
		- Callee-saves %r27.

		Error codes returned by entry path:

		ENOSYS - r20 was an invalid LWS number.

	*********************************************************/
lws_start:

#ifdef CONFIG_64BIT
	ssm	PSW_SM_W, %r1
	extrd,u	%r1,PSW_W_BIT,1,%r1
	/* sp must be aligned on 4, so deposit the W bit setting into
	 * the bottom of sp temporarily */
	or,od	%r1,%r30,%r30

	/* Clip LWS number to a 32-bit value for 32-bit processes */
	depdi	0, 31, 32, %r20
#endif	

        /* Is the lws entry number valid? */
	comiclr,>>	__NR_lws_entries, %r20, %r0
	b,n	lws_exit_nosys

	/* Load table start */
	ldil	L%lws_table, %r1
	ldo	R%lws_table(%r1), %r28	/* Scratch use of r28 */
	LDREGX	%r20(%sr2,r28), %r21	/* Scratch use of r21 */

	/* Jump to lws, lws table pointers already relocated */
	be,n	0(%sr2,%r21)

lws_exit_noerror:
	lws_pagefault_enable	%r1,%r21
	stw,ma	%r20, 0(%sr2,%r20)
	ssm	PSW_SM_I, %r0
	b	lws_exit
	copy	%r0, %r21

lws_wouldblock:
	ssm	PSW_SM_I, %r0
	ldo	2(%r0), %r28
	b	lws_exit
	ldo	-EAGAIN(%r0), %r21

lws_pagefault:
	lws_pagefault_enable	%r1,%r21
	stw,ma	%r20, 0(%sr2,%r20)
	ssm	PSW_SM_I, %r0
	ldo	3(%r0),%r28
	b	lws_exit
	ldo	-EAGAIN(%r0),%r21

lws_fault:
	ldo	1(%r0),%r28
	b	lws_exit
	ldo	-EFAULT(%r0),%r21

lws_exit_nosys:
	ldo	-ENOSYS(%r0),%r21
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
	be,n	0(%sr7, %r31)


	
	/***************************************************
		Implementing 32bit CAS as an atomic operation:

		%r26 - Address to examine
		%r25 - Old value to check (old)
		%r24 - New value to set (new)
		%r28 - Return prev through this register.
		%r21 - Kernel error code

		%r21 returns the following error codes:
		EAGAIN - CAS is busy, ldcw failed, try again.
		EFAULT - Read or write failed.		

		If EAGAIN is returned, %r28 indicates the busy reason:
		r28 == 1 - CAS is busy. lock contended.
		r28 == 2 - CAS is busy. ldcw failed.
		r28 == 3 - CAS is busy. page fault.

		Scratch: r20, r28, r1

	****************************************************/

	/* ELF64 Process entry path */
lws_compare_and_swap64:
#ifdef CONFIG_64BIT
	b,n	lws_compare_and_swap
#else
	/* If we are not a 64-bit kernel, then we don't
	 * have 64-bit input registers, and calling
	 * the 64-bit LWS CAS returns ENOSYS.
	 */
	b,n	lws_exit_nosys
#endif

	/* ELF32/ELF64 Process entry path */
lws_compare_and_swap32:
#ifdef CONFIG_64BIT
	/* Wide mode user process? */
	bb,<,n  %sp, 31, lws_compare_and_swap

	/* Clip all the input registers for 32-bit processes */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
#endif

lws_compare_and_swap:
	/* Trigger memory reference interruptions without writing to memory */
1:	ldw	0(%r26), %r28
2:	stbys,e	%r0, 0(%r26)

	/* Calculate 8-bit hash index from virtual address */
	extru_safe	%r26, 27, 8, %r20

	/* Load start of lock table */
	ldil	L%lws_lock_start, %r28
	ldo	R%lws_lock_start(%r28), %r28

	/* Find lock to use, the hash index is one of 0 to
	   255, multiplied by 16 (keep it 16-byte aligned)
	   and add to the lock table offset. */
	shlw	%r20, 4, %r20
	add	%r20, %r28, %r20

	rsm	PSW_SM_I, %r0				/* Disable interrupts */

	/* Try to acquire the lock */
	LDCW	0(%sr2,%r20), %r28
	comclr,<>	%r0, %r28, %r0
	b,n	lws_wouldblock

	/* Disable page faults to prevent sleeping in critical region */
	lws_pagefault_disable	%r21,%r28

	/*
		prev = *addr;
		if ( prev == old )
		  *addr = new;
		return prev;
	*/

	/* NOTES:
		This all works because intr_do_signal
		and schedule both check the return iasq
		and see that we are on the kernel page
		so this process is never scheduled off
		or is ever sent any signal of any sort,
		thus it is wholly atomic from usrspace's
		perspective
	*/
	/* The load and store could fail */
3:	ldw	0(%r26), %r28
	sub,<>	%r28, %r25, %r0
4:	stw	%r24, 0(%r26)
	b,n	lws_exit_noerror

	/* A fault occurred on load or stbys,e store */
5:	b,n	lws_fault
	ASM_EXCEPTIONTABLE_ENTRY(1b-linux_gateway_page, 5b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(2b-linux_gateway_page, 5b-linux_gateway_page)

	/* A page fault occurred in critical region */
6:	b,n	lws_pagefault
	ASM_EXCEPTIONTABLE_ENTRY(3b-linux_gateway_page, 6b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(4b-linux_gateway_page, 6b-linux_gateway_page)


	/***************************************************
		New CAS implementation which uses pointers and variable size
		information. The value pointed by old and new MUST NOT change
		while performing CAS. The lock only protects the value at %r26.

		%r26 - Address to examine
		%r25 - Pointer to the value to check (old)
		%r24 - Pointer to the value to set (new)
		%r23 - Size of the variable (0/1/2/3 for 8/16/32/64 bit)
		%r28 - Return non-zero on failure
		%r21 - Kernel error code

		%r21 returns the following error codes:
		EAGAIN - CAS is busy, ldcw failed, try again.
		EFAULT - Read or write failed.

		If EAGAIN is returned, %r28 indicates the busy reason:
		r28 == 1 - CAS is busy. lock contended.
		r28 == 2 - CAS is busy. ldcw failed.
		r28 == 3 - CAS is busy. page fault.

		Scratch: r20, r22, r28, r29, r1, fr4 (32bit for 64bit CAS only)

	****************************************************/

lws_compare_and_swap_2:
#ifdef CONFIG_64BIT
	/* Wide mode user process? */
	bb,<,n	%sp, 31, cas2_begin

	/* Clip the input registers for 32-bit processes. We don't
	   need to clip %r23 as we only use it for word operations */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
#endif

cas2_begin:
	/* Check the validity of the size pointer */
	subi,>>= 3, %r23, %r0
	b,n	lws_exit_nosys

	/* Jump to the functions which will load the old and new values into
	   registers depending on the their size */
	shlw	%r23, 2, %r29
	blr	%r29, %r0
	nop

	/* 8-bit load */
1:	ldb	0(%r25), %r25
	b	cas2_lock_start
2:	ldb	0(%r24), %r24
	nop
	nop
	nop
	nop
	nop

	/* 16-bit load */
3:	ldh	0(%r25), %r25
	b	cas2_lock_start
4:	ldh	0(%r24), %r24
	nop
	nop
	nop
	nop
	nop

	/* 32-bit load */
5:	ldw	0(%r25), %r25
	b	cas2_lock_start
6:	ldw	0(%r24), %r24
	nop
	nop
	nop
	nop
	nop

	/* 64-bit load */
#ifdef CONFIG_64BIT
7:	ldd	0(%r25), %r25
8:	ldd	0(%r24), %r24
#else
	/* Load old value into r22/r23 - high/low */
7:	ldw	0(%r25), %r22
8:	ldw	4(%r25), %r23
	/* Load new value into fr4 for atomic store later */
9:	flddx	0(%r24), %fr4
#endif

cas2_lock_start:
	/* Trigger memory reference interruptions without writing to memory */
	copy	%r26, %r28
	depi_safe	0, 31, 2, %r28
10:	ldw	0(%r28), %r1
11:	stbys,e	%r0, 0(%r28)

	/* Calculate 8-bit hash index from virtual address */
	extru_safe	%r26, 27, 8, %r20

	/* Load start of lock table */
	ldil	L%lws_lock_start, %r28
	ldo	R%lws_lock_start(%r28), %r28

	/* Find lock to use, the hash index is one of 0 to
	   255, multiplied by 16 (keep it 16-byte aligned)
	   and add to the lock table offset. */
	shlw	%r20, 4, %r20
	add	%r20, %r28, %r20

	rsm	PSW_SM_I, %r0			/* Disable interrupts */

	/* Try to acquire the lock */
	LDCW	0(%sr2,%r20), %r28
	comclr,<>	%r0, %r28, %r0
	b,n	lws_wouldblock

	/* Disable page faults to prevent sleeping in critical region */
	lws_pagefault_disable	%r21,%r28

	/*
		prev = *addr;
		if ( prev == old )
		  *addr = new;
		return prev;
	*/

	/* NOTES:
		This all works because intr_do_signal
		and schedule both check the return iasq
		and see that we are on the kernel page
		so this process is never scheduled off
		or is ever sent any signal of any sort,
		thus it is wholly atomic from usrspace's
		perspective
	*/

	/* Jump to the correct function */
	blr	%r29, %r0
	/* Set %r28 as non-zero for now */
	ldo	1(%r0),%r28

	/* 8-bit CAS */
12:	ldb	0(%r26), %r29
	sub,=	%r29, %r25, %r0
	b,n	lws_exit_noerror
13:	stb	%r24, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 16-bit CAS */
14:	ldh	0(%r26), %r29
	sub,=	%r29, %r25, %r0
	b,n	lws_exit_noerror
15:	sth	%r24, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 32-bit CAS */
16:	ldw	0(%r26), %r29
	sub,=	%r29, %r25, %r0
	b,n	lws_exit_noerror
17:	stw	%r24, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 64-bit CAS */
#ifdef CONFIG_64BIT
18:	ldd	0(%r26), %r29
	sub,*=	%r29, %r25, %r0
	b,n	lws_exit_noerror
19:	std	%r24, 0(%r26)
	copy	%r0, %r28
#else
	/* Compare first word */
18:	ldw	0(%r26), %r29
	sub,=	%r29, %r22, %r0
	b,n	lws_exit_noerror
	/* Compare second word */
19:	ldw	4(%r26), %r29
	sub,=	%r29, %r23, %r0
	b,n	lws_exit_noerror
	/* Perform the store */
20:	fstdx	%fr4, 0(%r26)
	copy	%r0, %r28
#endif
	b	lws_exit_noerror
	copy	%r0, %r28

	/* A fault occurred on load or stbys,e store */
30:	b,n	lws_fault
	ASM_EXCEPTIONTABLE_ENTRY(1b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(2b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(3b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(4b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(5b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(6b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(7b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(8b-linux_gateway_page, 30b-linux_gateway_page)
#ifndef CONFIG_64BIT
	ASM_EXCEPTIONTABLE_ENTRY(9b-linux_gateway_page, 30b-linux_gateway_page)
#endif

	ASM_EXCEPTIONTABLE_ENTRY(10b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(11b-linux_gateway_page, 30b-linux_gateway_page)

	/* A page fault occurred in critical region */
31:	b,n	lws_pagefault
	ASM_EXCEPTIONTABLE_ENTRY(12b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(13b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(14b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(15b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(16b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(17b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(18b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(19b-linux_gateway_page, 31b-linux_gateway_page)
#ifndef CONFIG_64BIT
	ASM_EXCEPTIONTABLE_ENTRY(20b-linux_gateway_page, 31b-linux_gateway_page)
#endif


	/***************************************************
		LWS atomic exchange.

		%r26 - Exchange address
		%r25 - Size of the variable (0/1/2/3 for 8/16/32/64 bit)
		%r24 - Address of new value
		%r23 - Address of old value
		%r28 - Return non-zero on failure
		%r21 - Kernel error code

		%r21 returns the following error codes:
		EAGAIN - CAS is busy, ldcw failed, try again.
		EFAULT - Read or write failed.

		If EAGAIN is returned, %r28 indicates the busy reason:
		r28 == 1 - CAS is busy. lock contended.
		r28 == 2 - CAS is busy. ldcw failed.
		r28 == 3 - CAS is busy. page fault.

		Scratch: r20, r1

	****************************************************/

lws_atomic_xchg:
#ifdef CONFIG_64BIT
	/* Wide mode user process? */
	bb,<,n	%sp, 31, atomic_xchg_begin

	/* Clip the input registers for 32-bit processes. We don't
	   need to clip %r23 as we only use it for word operations */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
	depdi	0, 31, 32, %r23
#endif

atomic_xchg_begin:
	/* Check the validity of the size pointer */
	subi,>>= 3, %r25, %r0
	b,n	lws_exit_nosys

	/* Jump to the functions which will load the old and new values into
	   registers depending on the their size */
	shlw	%r25, 2, %r1
	blr	%r1, %r0
	nop

	/* Perform exception checks */

	/* 8-bit exchange */
1:	ldb	0(%r24), %r20
	copy	%r23, %r20
	depi_safe	0, 31, 2, %r20
	b	atomic_xchg_start
2:	stbys,e	%r0, 0(%r20)
	nop
	nop
	nop

	/* 16-bit exchange */
3:	ldh	0(%r24), %r20
	copy	%r23, %r20
	depi_safe	0, 31, 2, %r20
	b	atomic_xchg_start
4:	stbys,e	%r0, 0(%r20)
	nop
	nop
	nop

	/* 32-bit exchange */
5:	ldw	0(%r24), %r20
	b	atomic_xchg_start
6:	stbys,e	%r0, 0(%r23)
	nop
	nop
	nop
	nop
	nop

	/* 64-bit exchange */
#ifdef CONFIG_64BIT
7:	ldd	0(%r24), %r20
8:	stdby,e	%r0, 0(%r23)
#else
7:	ldw	0(%r24), %r20
8:	ldw	4(%r24), %r20
	copy	%r23, %r20
	depi_safe	0, 31, 2, %r20
9:	stbys,e	%r0, 0(%r20)
10:	stbys,e	%r0, 4(%r20)
#endif

atomic_xchg_start:
	/* Trigger memory reference interruptions without writing to memory */
	copy	%r26, %r28
	depi_safe	0, 31, 2, %r28
11:	ldw	0(%r28), %r1
12:	stbys,e	%r0, 0(%r28)

	/* Calculate 8-bit hash index from virtual address */
	extru_safe  %r26, 27, 8, %r20

	/* Load start of lock table */
	ldil	L%lws_lock_start, %r28
	ldo	R%lws_lock_start(%r28), %r28

	/* Find lock to use, the hash index is one of 0 to
	   255, multiplied by 16 (keep it 16-byte aligned)
	   and add to the lock table offset. */
	shlw	%r20, 4, %r20
	add	%r20, %r28, %r20

	rsm	PSW_SM_I, %r0			/* Disable interrupts */

	/* Try to acquire the lock */
	LDCW	0(%sr2,%r20), %r28
	comclr,<>	%r0, %r28, %r0
	b,n	lws_wouldblock

	/* Disable page faults to prevent sleeping in critical region */
	lws_pagefault_disable	%r21,%r28

	/* NOTES:
		This all works because intr_do_signal
		and schedule both check the return iasq
		and see that we are on the kernel page
		so this process is never scheduled off
		or is ever sent any signal of any sort,
		thus it is wholly atomic from userspace's
		perspective
	*/

	/* Jump to the correct function */
	blr	%r1, %r0
	/* Set %r28 as non-zero for now */
	ldo	1(%r0),%r28

	/* 8-bit exchange */
14:	ldb	0(%r26), %r1
15:	stb	%r1, 0(%r23)
15:	ldb	0(%r24), %r1
17:	stb	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 16-bit exchange */
18:	ldh	0(%r26), %r1
19:	sth	%r1, 0(%r23)
20:	ldh	0(%r24), %r1
21:	sth	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 32-bit exchange */
22:	ldw	0(%r26), %r1
23:	stw	%r1, 0(%r23)
24:	ldw	0(%r24), %r1
25:	stw	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28
	nop
	nop

	/* 64-bit exchange */
#ifdef CONFIG_64BIT
26:	ldd	0(%r26), %r1
27:	std	%r1, 0(%r23)
28:	ldd	0(%r24), %r1
29:	std	%r1, 0(%r26)
#else
26:	flddx	0(%r26), %fr4
27:	fstdx	%fr4, 0(%r23)
28:	flddx	0(%r24), %fr4
29:	fstdx	%fr4, 0(%r26)
#endif
	b	lws_exit_noerror
	copy	%r0, %r28

	/* A fault occurred on load or stbys,e store */
30:	b,n	lws_fault
	ASM_EXCEPTIONTABLE_ENTRY(1b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(2b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(3b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(4b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(5b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(6b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(7b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(8b-linux_gateway_page, 30b-linux_gateway_page)
#ifndef CONFIG_64BIT
	ASM_EXCEPTIONTABLE_ENTRY(9b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(10b-linux_gateway_page, 30b-linux_gateway_page)
#endif

	ASM_EXCEPTIONTABLE_ENTRY(11b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(12b-linux_gateway_page, 30b-linux_gateway_page)

	/* A page fault occurred in critical region */
31:	b,n	lws_pagefault
	ASM_EXCEPTIONTABLE_ENTRY(14b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(15b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(16b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(17b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(18b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(19b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(20b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(21b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(22b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(23b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(24b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(25b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(26b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(27b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(28b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(29b-linux_gateway_page, 31b-linux_gateway_page)

	/***************************************************
		LWS atomic store.

		%r26 - Address to store
		%r25 - Size of the variable (0/1/2/3 for 8/16/32/64 bit)
		%r24 - Address of value to store
		%r28 - Return non-zero on failure
		%r21 - Kernel error code

		%r21 returns the following error codes:
		EAGAIN - CAS is busy, ldcw failed, try again.
		EFAULT - Read or write failed.

		If EAGAIN is returned, %r28 indicates the busy reason:
		r28 == 1 - CAS is busy. lock contended.
		r28 == 2 - CAS is busy. ldcw failed.
		r28 == 3 - CAS is busy. page fault.

		Scratch: r20, r1

	****************************************************/

lws_atomic_store:
#ifdef CONFIG_64BIT
	/* Wide mode user process? */
	bb,<,n	%sp, 31, atomic_store_begin

	/* Clip the input registers for 32-bit processes. We don't
	   need to clip %r23 as we only use it for word operations */
	depdi	0, 31, 32, %r26
	depdi	0, 31, 32, %r25
	depdi	0, 31, 32, %r24
#endif

atomic_store_begin:
	/* Check the validity of the size pointer */
	subi,>>= 3, %r25, %r0
	b,n	lws_exit_nosys

	shlw	%r25, 1, %r1
	blr	%r1, %r0
	nop

	/* Perform exception checks */

	/* 8-bit store */
1:	ldb	0(%r24), %r20
	b,n	atomic_store_start
	nop
	nop

	/* 16-bit store */
2:	ldh	0(%r24), %r20
	b,n	atomic_store_start
	nop
	nop

	/* 32-bit store */
3:	ldw	0(%r24), %r20
	b,n	atomic_store_start
	nop
	nop

	/* 64-bit store */
#ifdef CONFIG_64BIT
4:	ldd	0(%r24), %r20
#else
4:	ldw	0(%r24), %r20
5:	ldw	4(%r24), %r20
#endif

atomic_store_start:
	/* Trigger memory reference interruptions without writing to memory */
	copy	%r26, %r28
	depi_safe	0, 31, 2, %r28
6:	ldw	0(%r28), %r1
7:	stbys,e	%r0, 0(%r28)

	/* Calculate 8-bit hash index from virtual address */
	extru_safe  %r26, 27, 8, %r20

	/* Load start of lock table */
	ldil	L%lws_lock_start, %r28
	ldo	R%lws_lock_start(%r28), %r28

	/* Find lock to use, the hash index is one of 0 to
	   255, multiplied by 16 (keep it 16-byte aligned)
	   and add to the lock table offset. */
	shlw	%r20, 4, %r20
	add	%r20, %r28, %r20

	rsm	PSW_SM_I, %r0			/* Disable interrupts */

	/* Try to acquire the lock */
	LDCW	0(%sr2,%r20), %r28
	comclr,<>	%r0, %r28, %r0
	b,n	lws_wouldblock

	/* Disable page faults to prevent sleeping in critical region */
	lws_pagefault_disable	%r21,%r28

	/* NOTES:
		This all works because intr_do_signal
		and schedule both check the return iasq
		and see that we are on the kernel page
		so this process is never scheduled off
		or is ever sent any signal of any sort,
		thus it is wholly atomic from userspace's
		perspective
	*/

	/* Jump to the correct function */
	blr	%r1, %r0
	/* Set %r28 as non-zero for now */
	ldo	1(%r0),%r28

	/* 8-bit store */
9:	ldb	0(%r24), %r1
10:	stb	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28

	/* 16-bit store */
11:	ldh	0(%r24), %r1
12:	sth	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28

	/* 32-bit store */
13:	ldw	0(%r24), %r1
14:	stw	%r1, 0(%r26)
	b	lws_exit_noerror
	copy	%r0, %r28

	/* 64-bit store */
#ifdef CONFIG_64BIT
15:	ldd	0(%r24), %r1
16:	std	%r1, 0(%r26)
#else
15:	flddx	0(%r24), %fr4
16:	fstdx	%fr4, 0(%r26)
#endif
	b	lws_exit_noerror
	copy	%r0, %r28

	/* A fault occurred on load or stbys,e store */
30:	b,n	lws_fault
	ASM_EXCEPTIONTABLE_ENTRY(1b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(2b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(3b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(4b-linux_gateway_page, 30b-linux_gateway_page)
#ifndef CONFIG_64BIT
	ASM_EXCEPTIONTABLE_ENTRY(5b-linux_gateway_page, 30b-linux_gateway_page)
#endif

	ASM_EXCEPTIONTABLE_ENTRY(6b-linux_gateway_page, 30b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(7b-linux_gateway_page, 30b-linux_gateway_page)

	/* A page fault occurred in critical region */
31:	b,n	lws_pagefault
	ASM_EXCEPTIONTABLE_ENTRY(9b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(10b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(11b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(12b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(13b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(14b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(15b-linux_gateway_page, 31b-linux_gateway_page)
	ASM_EXCEPTIONTABLE_ENTRY(16b-linux_gateway_page, 31b-linux_gateway_page)

	/* Make sure nothing else is placed on this page */
	.align PAGE_SIZE
END(linux_gateway_page)
ENTRY(end_linux_gateway_page)

	/* Relocate symbols assuming linux_gateway_page is mapped
	   to virtual address 0x0 */

#define LWS_ENTRY(_name_) ASM_ULONG_INSN (lws_##_name_ - linux_gateway_page)

	.section .rodata,"a"

	.align 8
	/* Light-weight-syscall table */
	/* Start of lws table. */
ENTRY(lws_table)
	LWS_ENTRY(compare_and_swap32)		/* 0 - ELF32 Atomic 32bit CAS */
	LWS_ENTRY(compare_and_swap64)		/* 1 - ELF64 Atomic 32bit CAS */
	LWS_ENTRY(compare_and_swap_2)		/* 2 - Atomic 64bit CAS */
	LWS_ENTRY(atomic_xchg)			/* 3 - Atomic Exchange */
	LWS_ENTRY(atomic_store)			/* 4 - Atomic Store */
END(lws_table)
	/* End of lws table */

#ifdef CONFIG_64BIT
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)
#else
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
#endif
#define __SYSCALL(nr, entry)	ASM_ULONG_INSN entry
	.align 8
ENTRY(sys_call_table)
	.export sys_call_table,data
#include <asm/syscall_table_32.h>    /* 32-bit syscalls */
END(sys_call_table)

#ifdef CONFIG_64BIT
	.align 8
ENTRY(sys_call_table64)
#include <asm/syscall_table_64.h>    /* 64-bit syscalls */
END(sys_call_table64)
#endif

	/*
		All light-weight-syscall atomic operations 
		will use this set of locks 

		NOTE: The lws_lock_start symbol must be
		at least 16-byte aligned for safe use
		with ldcw.
	*/
	.section .data
	.align	L1_CACHE_BYTES
ENTRY(lws_lock_start)
	/* lws locks */
	.rept 256
	/* Keep locks aligned at 16-bytes */
	.word 1
	.word 0 
	.word 0
	.word 0
	.endr
END(lws_lock_start)
	.previous

.end
