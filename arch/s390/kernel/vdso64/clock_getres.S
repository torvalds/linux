/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of clock_getres() for 64 bits processes in a
 * s390 kernel for use in the vDSO
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#include <asm/vdso.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/dwarf.h>

	.text
	.align 4
	.globl __kernel_clock_getres
	.type  __kernel_clock_getres,@function
__kernel_clock_getres:
	CFI_STARTPROC
	larl	%r1,4f
	cghi	%r2,__CLOCK_REALTIME_COARSE
	je	0f
	cghi	%r2,__CLOCK_MONOTONIC_COARSE
	je	0f
	larl	%r1,3f
	cghi	%r2,__CLOCK_REALTIME
	je	0f
	cghi	%r2,__CLOCK_MONOTONIC
	je	0f
	cghi	%r2,__CLOCK_THREAD_CPUTIME_ID
	je	0f
	cghi	%r2,-2		/* Per-thread CPUCLOCK with PID=0, VIRT=1 */
	jne	2f
	larl	%r5,_vdso_data
	icm	%r0,15,__LC_ECTG_OK(%r5)
	jz	2f
0:	ltgr	%r3,%r3
	jz	1f				/* res == NULL */
	lg	%r0,0(%r1)
	xc	0(8,%r3),0(%r3)			/* set tp->tv_sec to zero */
	stg	%r0,8(%r3)			/* store tp->tv_usec */
1:	lghi	%r2,0
	br	%r14
2:	lghi	%r1,__NR_clock_getres		/* fallback to svc */
	svc	0
	br	%r14
	CFI_ENDPROC
3:	.quad	__CLOCK_REALTIME_RES
4:	.quad	__CLOCK_COARSE_RES
	.size	__kernel_clock_getres,.-__kernel_clock_getres
