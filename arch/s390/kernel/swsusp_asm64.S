/*
 * S390 64-bit swsusp implementation
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 *	      Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

/*
 * Save register context in absolute 0 lowcore and call swsusp_save() to
 * create in-memory kernel image. The context is saved in the designated
 * "store status" memory locations (see POP).
 * We return from this function twice. The first time during the suspend to
 * disk process. The second time via the swsusp_arch_resume() function
 * (see below) in the resume process.
 * This function runs with disabled interrupts.
 */
	.section .text
	.align	4
	.globl swsusp_arch_suspend
swsusp_arch_suspend:
	stmg	%r6,%r15,__SF_GPRS(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)

	/* Deactivate DAT */
	stnsm	__SF_EMPTY(%r15),0xfb

	/* Store prefix register on stack */
	stpx	__SF_EMPTY(%r15)

	/* Save prefix register contents for lowcore */
	llgf	%r4,__SF_EMPTY(%r15)

	/* Get pointer to save area */
	lghi	%r1,0x1000

	/* Save CPU address */
	stap	__LC_CPU_ADDRESS(%r0)

	/* Store registers */
	mvc	0x318(4,%r1),__SF_EMPTY(%r15)	/* move prefix to lowcore */
	stfpc	0x31c(%r1)			/* store fpu control */
	std	0,0x200(%r1)			/* store f0 */
	std	1,0x208(%r1)			/* store f1 */
	std	2,0x210(%r1)			/* store f2 */
	std	3,0x218(%r1)			/* store f3 */
	std	4,0x220(%r1)			/* store f4 */
	std	5,0x228(%r1)			/* store f5 */
	std	6,0x230(%r1)			/* store f6 */
	std	7,0x238(%r1)			/* store f7 */
	std	8,0x240(%r1)			/* store f8 */
	std	9,0x248(%r1)			/* store f9 */
	std	10,0x250(%r1)			/* store f10 */
	std	11,0x258(%r1)			/* store f11 */
	std	12,0x260(%r1)			/* store f12 */
	std	13,0x268(%r1)			/* store f13 */
	std	14,0x270(%r1)			/* store f14 */
	std	15,0x278(%r1)			/* store f15 */
	stam	%a0,%a15,0x340(%r1)		/* store access registers */
	stctg	%c0,%c15,0x380(%r1)		/* store control registers */
	stmg	%r0,%r15,0x280(%r1)		/* store general registers */

	stpt	0x328(%r1)			/* store timer */
	stck	__SF_EMPTY(%r15)		/* store clock */
	stckc	0x330(%r1)			/* store clock comparator */

	/* Update cputime accounting before going to sleep */
	lg	%r0,__LC_LAST_UPDATE_TIMER
	slg	%r0,0x328(%r1)
	alg	%r0,__LC_SYSTEM_TIMER
	stg	%r0,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),0x328(%r1)
	lg	%r0,__LC_LAST_UPDATE_CLOCK
	slg	%r0,__SF_EMPTY(%r15)
	alg	%r0,__LC_STEAL_TIMER
	stg	%r0,__LC_STEAL_TIMER
	mvc	__LC_LAST_UPDATE_CLOCK(8),__SF_EMPTY(%r15)

	/* Activate DAT */
	stosm	__SF_EMPTY(%r15),0x04

	/* Set prefix page to zero */
	xc	__SF_EMPTY(4,%r15),__SF_EMPTY(%r15)
	spx	__SF_EMPTY(%r15)

	lghi	%r2,0
	lghi	%r3,2*PAGE_SIZE
	lghi	%r5,2*PAGE_SIZE
1:	mvcle	%r2,%r4,0
	jo	1b

	/* Save image */
	brasl	%r14,swsusp_save

	/* Restore prefix register and return */
	lghi	%r1,0x1000
	spx	0x318(%r1)
	lmg	%r6,%r15,STACK_FRAME_OVERHEAD + __SF_GPRS(%r15)
	lghi	%r2,0
	br	%r14

/*
 * Restore saved memory image to correct place and restore register context.
 * Then we return to the function that called swsusp_arch_suspend().
 * swsusp_arch_resume() runs with disabled interrupts.
 */
	.globl swsusp_arch_resume
swsusp_arch_resume:
	stmg	%r6,%r15,__SF_GPRS(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)

	/* Make all free pages stable */
	lghi	%r2,1
	brasl	%r14,arch_set_page_states

	/* Deactivate DAT */
	stnsm	__SF_EMPTY(%r15),0xfb

	/* Set prefix page to zero */
	xc	__SF_EMPTY(4,%r15),__SF_EMPTY(%r15)
	spx	__SF_EMPTY(%r15)

	/* Restore saved image */
	larl	%r1,restore_pblist
	lg	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	2f
0:
	lg	%r2,8(%r1)
	lg	%r4,0(%r1)
	lghi	%r3,PAGE_SIZE
	lghi	%r5,PAGE_SIZE
1:
	mvcle	%r2,%r4,0
	jo	1b
	lg	%r1,16(%r1)
	ltgr	%r1,%r1
	jnz	0b
2:
	ptlb				/* flush tlb */

	/* Reset System */
	larl	%r1,restart_entry
	larl	%r2,.Lrestart_diag308_psw
	og	%r1,0(%r2)
	stg	%r1,0(%r0)
	larl	%r1,.Lnew_pgm_check_psw
	epsw	%r2,%r3
	stm	%r2,%r3,0(%r1)
	mvc	__LC_PGM_NEW_PSW(16,%r0),0(%r1)
	lghi	%r0,0
	diag	%r0,%r0,0x308
restart_entry:
	lhi	%r1,1
	sigp	%r1,%r0,0x12
	sam64
	larl	%r1,.Lnew_pgm_check_psw
	lpswe	0(%r1)
pgm_check_entry:

	/* Switch to original suspend CPU */
	larl	%r1,.Lresume_cpu		/* Resume CPU address: r2 */
	stap	0(%r1)
	llgh	%r2,0(%r1)
	llgh	%r1,__LC_CPU_ADDRESS(%r0)	/* Suspend CPU address: r1 */
	cgr	%r1,%r2
	je	restore_registers		/* r1 = r2 -> nothing to do */
	larl	%r4,.Lrestart_suspend_psw	/* Set new restart PSW */
	mvc	__LC_RESTART_PSW(16,%r0),0(%r4)
3:
	sigp	%r9,%r1,__SIGP_INITIAL_CPU_RESET
	brc	8,4f	/* accepted */
	brc	2,3b	/* busy, try again */

	/* Suspend CPU not available -> panic */
	larl	%r15,init_thread_union
	ahi	%r15,1<<(PAGE_SHIFT+THREAD_ORDER)
	larl	%r2,.Lpanic_string
	larl	%r3,_sclp_print_early
	lghi	%r1,0
	sam31
	sigp	%r1,%r0,0x12
	basr	%r14,%r3
	larl	%r3,.Ldisabled_wait_31
	lpsw	0(%r3)
4:
	/* Switch to suspend CPU */
	sigp	%r9,%r1,__SIGP_RESTART	/* start suspend CPU */
	brc	2,4b			/* busy, try again */
5:
	sigp	%r9,%r2,__SIGP_STOP	/* stop resume (current) CPU */
	brc	2,5b			/* busy, try again */
6:	j	6b

restart_suspend:
	larl	%r1,.Lresume_cpu
	llgh	%r2,0(%r1)
7:
	sigp	%r9,%r2,__SIGP_SENSE	/* Wait for resume CPU */
	brc	2,7b			/* busy, try again */
	tmll	%r9,0x40		/* Test if resume CPU is stopped */
	jz	7b

restore_registers:
	/* Restore registers */
	lghi	%r13,0x1000		/* %r1 = pointer to save area */

	/* Ignore time spent in suspended state. */
	llgf	%r1,0x318(%r13)
	stck	__LC_LAST_UPDATE_CLOCK(%r1)
	spt	0x328(%r13)		/* reprogram timer */
	//sckc	0x330(%r13)		/* set clock comparator */

	lctlg	%c0,%c15,0x380(%r13)	/* load control registers */
	lam	%a0,%a15,0x340(%r13)	/* load access registers */

	lfpc	0x31c(%r13)		/* load fpu control */
	ld	0,0x200(%r13)		/* load f0 */
	ld	1,0x208(%r13)		/* load f1 */
	ld	2,0x210(%r13)		/* load f2 */
	ld	3,0x218(%r13)		/* load f3 */
	ld	4,0x220(%r13)		/* load f4 */
	ld	5,0x228(%r13)		/* load f5 */
	ld	6,0x230(%r13)		/* load f6 */
	ld	7,0x238(%r13)		/* load f7 */
	ld	8,0x240(%r13)		/* load f8 */
	ld	9,0x248(%r13)		/* load f9 */
	ld	10,0x250(%r13)		/* load f10 */
	ld	11,0x258(%r13)		/* load f11 */
	ld	12,0x260(%r13)		/* load f12 */
	ld	13,0x268(%r13)		/* load f13 */
	ld	14,0x270(%r13)		/* load f14 */
	ld	15,0x278(%r13)		/* load f15 */

	/* Load old stack */
	lg	%r15,0x2f8(%r13)

	/* Restore prefix register */
	spx	0x318(%r13)

	/* Activate DAT */
	stosm	__SF_EMPTY(%r15),0x04

	/* Make all free pages unstable */
	lghi	%r2,0
	brasl	%r14,arch_set_page_states

	/* Return 0 */
	lmg	%r6,%r15,STACK_FRAME_OVERHEAD + __SF_GPRS(%r15)
	lghi	%r2,0
	br	%r14

	.section .data.nosave,"aw",@progbits
	.align	8
.Ldisabled_wait_31:
	.long  0x000a0000,0x00000000
.Lpanic_string:
	.asciz	"Resume not possible because suspend CPU is no longer available"
	.align	8
.Lrestart_diag308_psw:
	.long	0x00080000,0x80000000
.Lrestart_suspend_psw:
	.quad	0x0000000180000000,restart_suspend
.Lnew_pgm_check_psw:
	.quad	0,pgm_check_entry
.Lresume_cpu:
	.byte	0,0
