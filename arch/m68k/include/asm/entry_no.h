#ifndef __M68KNOMMU_ENTRY_H
#define __M68KNOMMU_ENTRY_H

#include <asm/setup.h>
#include <asm/page.h>

/*
 * Stack layout in 'ret_from_exception':
 *
 * This allows access to the syscall arguments in registers d1-d5
 *
 *	 0(sp) - d1
 *	 4(sp) - d2
 *	 8(sp) - d3
 *	 C(sp) - d4
 *	10(sp) - d5
 *	14(sp) - a0
 *	18(sp) - a1
 *	1C(sp) - a2
 *	20(sp) - d0
 *	24(sp) - orig_d0
 *	28(sp) - stack adjustment
 *	2C(sp) - [ sr              ] [ format & vector ]
 *	2E(sp) - [ pc-hiword       ] [ sr              ]
 *	30(sp) - [ pc-loword       ] [ pc-hiword       ]
 *	32(sp) - [ format & vector ] [ pc-loword       ]
 *		  ^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^
 *			M68K		  COLDFIRE
 */

#define ALLOWINT (~0x700)

#ifdef __ASSEMBLY__

/* process bits for task_struct.flags */
PF_TRACESYS_OFF = 3
PF_TRACESYS_BIT = 5
PF_PTRACED_OFF = 3
PF_PTRACED_BIT = 4
PF_DTRACE_OFF = 1
PF_DTRACE_BIT = 5

LENOSYS = 38

#define SWITCH_STACK_SIZE (6*4+4)	/* Includes return address */

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs are a2-a6 and d6-d7 preserved by C code
 * the kernel doesn't mess with usp unless it needs to
 */

#ifdef CONFIG_COLDFIRE
/*
 * This is made a little more tricky on the ColdFire. There is no
 * separate kernel and user stack pointers. Need to artificially
 * construct a usp in software... When doing this we need to disable
 * interrupts, otherwise bad things could happen.
 */
.macro SAVE_ALL
	move	#0x2700,%sr		/* disable intrs */
	btst	#5,%sp@(2)		/* from user? */
	bnes	6f			/* no, skip */
	movel	%sp,sw_usp		/* save user sp */
	addql	#8,sw_usp		/* remove exception */
	movel	sw_ksp,%sp		/* kernel sp */
	subql	#8,%sp			/* room for exception */
	clrl	%sp@-			/* stkadj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	lea	%sp@(-32),%sp		/* space for 8 regs */
	moveml	%d1-%d5/%a0-%a2,%sp@
	movel	sw_usp,%a0		/* get usp */
	movel	%a0@-,%sp@(PT_OFF_PC)	/* copy exception program counter */
	movel	%a0@-,%sp@(PT_OFF_FORMATVEC)/*copy exception format/vector/sr */
	bra	7f
	6:
	clrl	%sp@-			/* stkadj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	lea	%sp@(-32),%sp		/* space for 8 regs */
	moveml	%d1-%d5/%a0-%a2,%sp@
	7:
.endm

.macro RESTORE_ALL
	btst	#5,%sp@(PT_SR)		/* going user? */
	bnes	8f			/* no, skip */
	move	#0x2700,%sr		/* disable intrs */
	movel	sw_usp,%a0		/* get usp */
	movel	%sp@(PT_OFF_PC),%a0@-	/* copy exception program counter */
	movel	%sp@(PT_OFF_FORMATVEC),%a0@-/*copy exception format/vector/sr */
	moveml	%sp@,%d1-%d5/%a0-%a2
	lea	%sp@(32),%sp		/* space for 8 regs */
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stkadj */
	addql	#8,%sp			/* remove exception */
	movel	%sp,sw_ksp		/* save ksp */
	subql	#8,sw_usp		/* set exception */
	movel	sw_usp,%sp		/* restore usp */
	rte
	8:
	moveml	%sp@,%d1-%d5/%a0-%a2
	lea	%sp@(32),%sp		/* space for 8 regs */
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stkadj */
	rte
.endm

/*
 * Quick exception save, use current stack only.
 */
.macro SAVE_LOCAL
	move	#0x2700,%sr		/* disable intrs */
	clrl	%sp@-			/* stkadj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	lea	%sp@(-32),%sp		/* space for 8 regs */
	moveml	%d1-%d5/%a0-%a2,%sp@
.endm

.macro RESTORE_LOCAL
	moveml	%sp@,%d1-%d5/%a0-%a2
	lea	%sp@(32),%sp		/* space for 8 regs */
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stkadj */
	rte
.endm

.macro SAVE_SWITCH_STACK
	lea	%sp@(-24),%sp		/* 6 regs */
	moveml	%a3-%a6/%d6-%d7,%sp@
.endm

.macro RESTORE_SWITCH_STACK
	moveml	%sp@,%a3-%a6/%d6-%d7
	lea	%sp@(24),%sp		/* 6 regs */
.endm

/*
 * Software copy of the user and kernel stack pointers... Ugh...
 * Need these to get around ColdFire not having separate kernel
 * and user stack pointers.
 */
.globl sw_usp
.globl sw_ksp

#else /* !CONFIG_COLDFIRE */

/*
 * Standard 68k interrupt entry and exit macros.
 */
.macro SAVE_ALL
	clrl	%sp@-			/* stkadj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	moveml	%d1-%d5/%a0-%a2,%sp@-
.endm

.macro RESTORE_ALL
	moveml	%sp@+,%a0-%a2/%d1-%d5
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stkadj */
	rte
.endm

.macro SAVE_SWITCH_STACK
	moveml	%a3-%a6/%d6-%d7,%sp@-
.endm

.macro RESTORE_SWITCH_STACK
	moveml	%sp@+,%a3-%a6/%d6-%d7
.endm

#endif /* !CONFIG_COLDFIRE */
#endif /* __ASSEMBLY__ */
#endif /* __M68KNOMMU_ENTRY_H */
