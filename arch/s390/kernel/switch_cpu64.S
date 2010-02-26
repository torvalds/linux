/*
 * 64-bit switch cpu code
 *
 * Copyright IBM Corp. 2009
 *
 */

#include <asm/asm-offsets.h>
#include <asm/lowcore.h>
#include <asm/ptrace.h>

# smp_switch_to_cpu switches to destination cpu and executes the passed function
# Parameter: %r2 - function to call
#	     %r3 - function parameter
#	     %r4 - stack poiner
#	     %r5 - current cpu
#	     %r6 - destination cpu

	.section .text
	.align 4
	.globl smp_switch_to_cpu
smp_switch_to_cpu:
	stmg	%r6,%r15,__SF_GPRS(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)
	larl	%r1,.gprregs
	stmg	%r0,%r15,0(%r1)
1:	sigp	%r0,%r6,__SIGP_RESTART	/* start destination CPU */
	brc	2,1b			/* busy, try again */
2:	sigp	%r0,%r5,__SIGP_STOP	/* stop current CPU */
	brc	2,2b			/* busy, try again */
3:	j	3b

	.globl	smp_restart_cpu
smp_restart_cpu:
	larl	%r1,.gprregs
	lmg	%r0,%r15,0(%r1)
1:	sigp	%r0,%r5,__SIGP_SENSE	/* Wait for calling CPU */
	brc	10,1b			/* busy, accepted (status 0), running */
	tmll	%r0,0x40		/* Test if calling CPU is stopped */
	jz	1b
	ltgr	%r4,%r4			/* New stack ? */
	jz	1f
	lgr	%r15,%r4
1:	basr	%r14,%r2

	.section .data,"aw",@progbits
.gprregs:
	.rept	16
	.quad	0
	.endr
