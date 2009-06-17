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
	.align	2
	.globl swsusp_arch_suspend
swsusp_arch_suspend:
	stmg	%r6,%r15,__SF_GPRS(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)

	/* Deactivate DAT */
	stnsm	__SF_EMPTY(%r15),0xfb

	/* Switch off lowcore protection */
	stctg	%c0,%c0,__SF_EMPTY(%r15)
	ni	__SF_EMPTY+4(%r15),0xef
	lctlg	%c0,%c0,__SF_EMPTY(%r15)

	/* Store prefix register on stack */
	stpx	__SF_EMPTY(%r15)

	/* Setup base register for lowcore (absolute 0) */
	llgf	%r1,__SF_EMPTY(%r15)

	/* Get pointer to save area */
	aghi	%r1,0x1000

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
	stckc	0x330(%r1)			/* store clock comparator */

	/* Activate DAT */
	stosm	__SF_EMPTY(%r15),0x04

	/* Set prefix page to zero */
	xc	__SF_EMPTY(4,%r15),__SF_EMPTY(%r15)
	spx	__SF_EMPTY(%r15)

	/* Setup lowcore */
	brasl	%r14,setup_lowcore_early

	/* Save image */
	brasl	%r14,swsusp_save

	/* Switch on lowcore protection */
	stctg	%c0,%c0,__SF_EMPTY(%r15)
	oi	__SF_EMPTY+4(%r15),0x10
	lctlg	%c0,%c0,__SF_EMPTY(%r15)

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

	/* Save boot cpu number */
	brasl	%r14,smp_get_phys_cpu_id
	lgr	%r10,%r2

	/* Deactivate DAT */
	stnsm	__SF_EMPTY(%r15),0xfb

	/* Switch off lowcore protection */
	stctg	%c0,%c0,__SF_EMPTY(%r15)
	ni	__SF_EMPTY+4(%r15),0xef
	lctlg	%c0,%c0,__SF_EMPTY(%r15)

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

	/* Restore registers */
	lghi	%r13,0x1000		/* %r1 = pointer to save arae */

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

	/* Pointer to save arae */
	lghi	%r13,0x1000

	/* Switch CPUs */
	lgr	%r2,%r10		/* get cpu id */
	llgf	%r3,0x318(%r13)
	brasl	%r14,smp_switch_boot_cpu_in_resume

	/* Restore prefix register */
	spx	0x318(%r13)

	/* Switch on lowcore protection */
	stctg	%c0,%c0,__SF_EMPTY(%r15)
	oi	__SF_EMPTY+4(%r15),0x10
	lctlg	%c0,%c0,__SF_EMPTY(%r15)

	/* Activate DAT */
	stosm	__SF_EMPTY(%r15),0x04

	/* Return 0 */
	lmg	%r6,%r15,STACK_FRAME_OVERHEAD + __SF_GPRS(%r15)
	lghi	%r2,0
	br	%r14
