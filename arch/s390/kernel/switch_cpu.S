/*
 * 31-bit switch cpu code
 *
 * Copyright IBM Corp. 2009
 *
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>

# smp_switch_to_cpu switches to destination cpu and executes the passed function
# Parameter: %r2 - function to call
#	     %r3 - function parameter
#	     %r4 - stack poiner
#	     %r5 - current cpu
#	     %r6 - destination cpu

	.section .text
ENTRY(smp_switch_to_cpu)
	stm	%r6,%r15,__SF_GPRS(%r15)
	lr	%r1,%r15
	ahi	%r15,-STACK_FRAME_OVERHEAD
	st	%r1,__SF_BACKCHAIN(%r15)
	basr	%r13,0
0:	la	%r1,.gprregs_addr-0b(%r13)
	l	%r1,0(%r1)
	stm	%r0,%r15,0(%r1)
1:	sigp	%r0,%r6,__SIGP_RESTART	/* start destination CPU */
	brc	2,1b			/* busy, try again */
2:	sigp	%r0,%r5,__SIGP_STOP	/* stop current CPU */
	brc	2,2b			/* busy, try again */
3:	j	3b

ENTRY(smp_restart_cpu)
	basr	%r13,0
0:	la	%r1,.gprregs_addr-0b(%r13)
	l	%r1,0(%r1)
	lm	%r0,%r15,0(%r1)
1:	sigp	%r0,%r5,__SIGP_SENSE	/* Wait for calling CPU */
	brc	10,1b			/* busy, accepted (status 0), running */
	tmll	%r0,0x40		/* Test if calling CPU is stopped */
	jz	1b
	ltr	%r4,%r4			/* New stack ? */
	jz	1f
	lr	%r15,%r4
1:	lr	%r14,%r2		/* r14: Function to call */
	lr	%r2,%r3			/* r2 : Parameter for function*/
	basr	%r14,%r14		/* Call function */

.gprregs_addr:
	.long	.gprregs

	.section .data,"aw",@progbits
.gprregs:
	.rept	16
	.long	0
	.endr
