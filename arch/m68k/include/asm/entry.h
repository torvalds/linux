#ifndef __M68K_ENTRY_H
#define __M68K_ENTRY_H

#include <asm/setup.h>
#include <asm/page.h>
#ifdef __ASSEMBLY__
#include <asm/thread_info.h>
#endif

/*
 * Stack layout in 'ret_from_exception':
 *
 *	This allows access to the syscall arguments in registers d1-d5
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

/* the following macro is used when enabling interrupts */
#if defined(MACH_ATARI_ONLY)
	/* block out HSYNC on the atari */
#define ALLOWINT	(~0x400)
#define	MAX_NOINT_IPL	3
#else
	/* portable version */
#define ALLOWINT	(~0x700)
#define	MAX_NOINT_IPL	0
#endif /* machine compilation types */

#ifdef __ASSEMBLY__
/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs a3-a6 and d6-d7 are preserved by C code
 * the kernel doesn't mess with usp unless it needs to
 */
#define SWITCH_STACK_SIZE	(6*4+4)	/* includes return address */

#ifdef CONFIG_COLDFIRE
#ifdef CONFIG_COLDFIRE_SW_A7
/*
 * This is made a little more tricky on older ColdFires. There is no
 * separate supervisor and user stack pointers. Need to artificially
 * construct a usp in software... When doing this we need to disable
 * interrupts, otherwise bad things will happen.
 */
.globl sw_usp
.globl sw_ksp

.macro SAVE_ALL_SYS
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

.macro SAVE_ALL_INT
	SAVE_ALL_SYS
	moveq	#-1,%d0			/* not system call entry */
	movel	%d0,%sp@(PT_OFF_ORIG_D0)
.endm

.macro RESTORE_USER
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
.endm

.macro RDUSP
	movel	sw_usp,%a3
.endm

.macro WRUSP
	movel	%a3,sw_usp
.endm

#else /* !CONFIG_COLDFIRE_SW_A7 */
/*
 * Modern ColdFire parts have separate supervisor and user stack
 * pointers. Simple load and restore macros for this case.
 */
.macro SAVE_ALL_SYS
	move	#0x2700,%sr		/* disable intrs */
	clrl	%sp@-			/* stkadj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	lea	%sp@(-32),%sp		/* space for 8 regs */
	moveml	%d1-%d5/%a0-%a2,%sp@
.endm

.macro SAVE_ALL_INT
	move	#0x2700,%sr		/* disable intrs */
	clrl	%sp@-			/* stkadj */
	pea	-1:w			/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	lea	%sp@(-32),%sp		/* space for 8 regs */
	moveml	%d1-%d5/%a0-%a2,%sp@
.endm

.macro RESTORE_USER
	moveml	%sp@,%d1-%d5/%a0-%a2
	lea	%sp@(32),%sp		/* space for 8 regs */
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stkadj */
	rte
.endm

.macro RDUSP
	/*move	%usp,%a3*/
	.word	0x4e6b
.endm

.macro WRUSP
	/*move	%a3,%usp*/
	.word	0x4e63
.endm

#endif /* !CONFIG_COLDFIRE_SW_A7 */

.macro SAVE_SWITCH_STACK
	lea	%sp@(-24),%sp		/* 6 regs */
	moveml	%a3-%a6/%d6-%d7,%sp@
.endm

.macro RESTORE_SWITCH_STACK
	moveml	%sp@,%a3-%a6/%d6-%d7
	lea	%sp@(24),%sp		/* 6 regs */
.endm

#else /* !CONFIG_COLDFIRE */

/*
 * All other types of m68k parts (68000, 680x0, CPU32) have the same
 * entry and exit code.
 */

/*
 * a -1 in the orig_d0 field signifies
 * that the stack frame is NOT for syscall
 */
.macro SAVE_ALL_INT
	clrl	%sp@-			/* stk_adj */
	pea	-1:w			/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	moveml	%d1-%d5/%a0-%a2,%sp@-
.endm

.macro SAVE_ALL_SYS
	clrl	%sp@-			/* stk_adj */
	movel	%d0,%sp@-		/* orig d0 */
	movel	%d0,%sp@-		/* d0 */
	moveml	%d1-%d5/%a0-%a2,%sp@-
.endm

.macro RESTORE_ALL
	moveml	%sp@+,%a0-%a2/%d1-%d5
	movel	%sp@+,%d0
	addql	#4,%sp			/* orig d0 */
	addl	%sp@+,%sp		/* stk adj */
	rte
.endm


.macro SAVE_SWITCH_STACK
	moveml	%a3-%a6/%d6-%d7,%sp@-
.endm

.macro RESTORE_SWITCH_STACK
	moveml	%sp@+,%a3-%a6/%d6-%d7
.endm

#endif /* !CONFIG_COLDFIRE */

/*
 * Register %a2 is reserved and set to current task on MMU enabled systems.
 * Non-MMU systems do not reserve %a2 in this way, and this definition is
 * not used for them.
 */
#define curptr a2

#define GET_CURRENT(tmp) get_current tmp
.macro get_current reg=%d0
	movel	%sp,\reg
	andw	#-THREAD_SIZE,\reg
	movel	\reg,%curptr
	movel	%curptr@,%curptr
.endm

#else /* C source */

#define STR(X) STR1(X)
#define STR1(X) #X

#define SAVE_ALL_INT				\
	"clrl	%%sp@-;"    /* stk_adj */	\
	"pea	-1:w;"	    /* orig d0 = -1 */	\
	"movel	%%d0,%%sp@-;" /* d0 */		\
	"moveml	%%d1-%%d5/%%a0-%%a2,%%sp@-"

#define GET_CURRENT(tmp) \
	"movel	%%sp,"#tmp"\n\t" \
	"andw	#-"STR(THREAD_SIZE)","#tmp"\n\t" \
	"movel	"#tmp",%%a2\n\t" \
	"movel	%%a2@,%%a2"

#endif

#endif /* __M68K_ENTRY_H */
