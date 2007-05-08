/* -*- mode: asm -*-
 *
 *  linux/arch/h8300/platform/h8300h/entry.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *  David McCullough <davidm@snapgear.com>
 *
 */

/*
 *  entry.S
 *  include exception/interrupt gateway
 *          system call entry
 */

#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/errno.h>

	.h8300h

/* CPU context save/restore macros. */
	
	.macro	SAVE_ALL
	mov.l	er0,@-sp

	stc	ccr,r0l				/* check kernel mode */
	btst	#4,r0l
	bne	5f

	mov.l	sp,@SYMBOL_NAME(sw_usp)		/* user mode */
	mov.l	@sp,er0
	orc	#0x10,ccr
	mov.l	@SYMBOL_NAME(sw_ksp),sp
	sub.l	#(LRET-LORIG),sp		/* allocate LORIG - LRET */ 
	mov.l	er0,@-sp
	mov.l	er1,@-sp
	mov.l   @SYMBOL_NAME(sw_usp),er0
	mov.l   @(8:16,er0),er1                 /* copy the RET addr */
	mov.l   er1,@(LRET-LER1:16,sp)

	mov.w	e1,r1				/* e1 highbyte = ccr */
	and	#0xef,r1h			/* mask mode? flag */
	sub.w	r0,r0
	mov.b	r1h,r0l
	mov.w	r0,@(LCCR-LER1:16,sp)		/* copy ccr */
	mov.l	@(LORIG-LER1:16,sp),er0
	mov.l	er0,@(LER0-LER1:16,sp)		/* copy ER0 */
	bra	6f
5:
	mov.l	@sp,er0				/* kernel mode */
	subs	#2,sp				/* dummy ccr */
	mov.l	er0,@-sp
	mov.l	er1,@-sp
	mov.w	@(LRET-LER1:16,sp),r1		/* copy old ccr */
	mov.b	r1h,r1l
	mov.b	#0,r1h
	mov.w	r1,@(LCCR-LER1:16,sp)		/* set ccr */
6:
	mov.l	er2,@-sp
	mov.l	er3,@-sp
	mov.l	er6,@-sp			/* syscall arg #6 */
	mov.l	er5,@-sp			/* syscall arg #5 */
	mov.l	er4,@-sp			/* syscall arg #4 */
	.endm

	.macro	RESTORE_ALL
	mov.l	@sp+,er4
	mov.l	@sp+,er5
	mov.l	@sp+,er6
	mov.l	@sp+,er3
	mov.l	@sp+,er2
	mov.w	@(LCCR-LER1:16,sp),r0		/* check kernel mode */
	btst	#4,r0l
	bne	7f

	orc	#0x80,ccr
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@(LER0-LER1:16,sp),er1		/* restore ER0 */
	mov.l	er1,@er0
	mov.w	@(LCCR-LER1:16,sp),r1		/* restore the RET addr */
	mov.b	r1l,r1h
	mov.b	@(LRET+1-LER1:16,sp),r1l
	mov.w	r1,e1
	mov.w	@(LRET+2-LER1:16,sp),r1
	mov.l	er1,@(8:16,er0)

	mov.l	@sp+,er1
	add.l	#(LRET-LER1),sp			/* remove LORIG - LRET */ 
	mov.l	sp,@SYMBOL_NAME(sw_ksp)
	mov.l	er0,sp
	bra	8f
7:
	mov.l	@sp+,er1
	adds	#4,sp
	adds	#2,sp
8:
	mov.l	@sp+,er0
	adds	#4,sp				/* remove the sw created LVEC */
	rte
	.endm
	
.globl SYMBOL_NAME(system_call)
.globl SYMBOL_NAME(ret_from_exception)
.globl SYMBOL_NAME(ret_from_fork)
.globl SYMBOL_NAME(ret_from_interrupt)
.globl SYMBOL_NAME(interrupt_redirect_table)
.globl SYMBOL_NAME(sw_ksp),SYMBOL_NAME(sw_usp)
.globl SYMBOL_NAME(resume)
.globl SYMBOL_NAME(interrupt_redirect_table)
.globl SYMBOL_NAME(interrupt_entry)
.globl SYMBOL_NAME(system_call)
.globl SYMBOL_NAME(trace_break)

#if defined(CONFIG_ROMKERNEL)
INTERRUPTS = 64		
	.section .int_redirect,"ax"
SYMBOL_NAME_LABEL(interrupt_redirect_table)
	.rept	7
	.long	0
	.endr
	jsr	@SYMBOL_NAME(interrupt_entry)	/* NMI */
	jmp	@SYMBOL_NAME(system_call)	/* TRAPA #0 (System call) */
	.long	0
	.long	0
	jmp	@SYMBOL_NAME(trace_break)	/* TRAPA #3 (breakpoint) */
	.rept	INTERRUPTS-12
	jsr	@SYMBOL_NAME(interrupt_entry)
	.endr
#endif
#if defined(CONFIG_RAMKERNEL)
.globl SYMBOL_NAME(interrupt_redirect_table)
	.section .bss
SYMBOL_NAME_LABEL(interrupt_redirect_table)
	.space	4
#endif

	.section .text
	.align	2
SYMBOL_NAME_LABEL(interrupt_entry)
	SAVE_ALL
	mov.w	@(LCCR,sp),r0
	btst	#4,r0l
	bne	1f
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@(4:16,er0),er0
	bra	2f
1:
	mov.l	@(LVEC,sp),er0
2:
#if defined(CONFIG_ROMKERNEL)
	sub.l	#SYMBOL_NAME(interrupt_redirect_table),er0
#endif
#if defined(CONFIG_RAMKERNEL)
	mov.l	@SYMBOL_NAME(interrupt_redirect_table),er1
	sub.l	er1,er0
#endif
	shlr.l	er0
	shlr.l	er0
	dec.l	#1,er0
	mov.l	sp,er1
	subs	#4,er1				/* adjust ret_pc */
	jsr	@SYMBOL_NAME(do_IRQ)
	mov.l	@SYMBOL_NAME(irq_stat)+CPUSTAT_SOFTIRQ_PENDING,er0
	beq	1f
	jsr	@SYMBOL_NAME(do_softirq)
1:
	jmp	@SYMBOL_NAME(ret_from_interrupt)
	
SYMBOL_NAME_LABEL(system_call)
	subs	#4,sp				/* dummy LVEC */
	SAVE_ALL
	mov.w	@(LCCR:16,sp),r1
	bset	#4,r1l
	ldc	r1l,ccr
	mov.l	er0,er4
	mov.l	#-ENOSYS,er0
	mov.l	er0,@(LER0:16,sp)

	/* save top of frame */
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)
	cmp.l	#NR_syscalls,er4
	bcc	SYMBOL_NAME(ret_from_exception):16
	shll.l	er4
	shll.l	er4
	mov.l	#SYMBOL_NAME(sys_call_table),er0
	add.l	er4,er0
	mov.l	@er0,er4
	beq	SYMBOL_NAME(ret_from_exception):16	
	mov.l	sp,er2
	and.w	#0xe000,r2
	mov.b	@((TASK_FLAGS+3-(TIF_SYSCALL_TRACE >> 3)):16,er2),r2l
	btst	#(TIF_SYSCALL_TRACE & 7),r2l
	bne	1f
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	jsr	@er4
	mov.l	er0,@(LER0:16,sp)		/* save the return value */
#if defined(CONFIG_SYSCALL_PRINT)
	jsr	@SYMBOL_NAME(syscall_print)
#endif
	bra	SYMBOL_NAME(ret_from_exception):8
1:
	jsr	SYMBOL_NAME(syscall_trace)
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	jsr	@er4
	mov.l	er0,@(LER0:16,sp)		/* save the return value */
	jsr	@SYMBOL_NAME(syscall_trace)
	bra	SYMBOL_NAME(ret_from_exception):8

SYMBOL_NAME_LABEL(ret_from_fork)
	mov.l	er2,er0
	jsr	@SYMBOL_NAME(schedule_tail)
	bra	SYMBOL_NAME(ret_from_exception):8

SYMBOL_NAME_LABEL(reschedule)
	/* save top of frame */
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)
	jsr	@SYMBOL_NAME(schedule)

SYMBOL_NAME_LABEL(ret_from_exception)
#if defined(CONFIG_PREEMPT)
	orc	#0x80,ccr
#endif
SYMBOL_NAME_LABEL(ret_from_interrupt)
	mov.b	@(LCCR+1:16,sp),r0l
	btst	#4,r0l			/* check if returning to kernel */
	bne	done:8			/* if so, skip resched, signals */
	andc	#0x7f,ccr
	mov.l	sp,er4
	and.w	#0xe000,r4
	mov.l	@(TI_FLAGS:16,er4),er1
	and.l	#_TIF_WORK_MASK,er1
	beq	done:8
1:
	mov.l	@(TI_FLAGS:16,er4),er1
	btst	#TIF_NEED_RESCHED,r1l
	bne	SYMBOL_NAME(reschedule):16
	mov.l	sp,er0
	subs	#4,er0			/* adjust retpc */
	mov.l	er2,er1
	jsr	@SYMBOL_NAME(do_signal)
#if defined(CONFIG_PREEMPT)
	bra	done:8			/* userspace thoru */
3:
	btst	#4,r0l
	beq	done:8			/* userspace thoru */
4:
	mov.l	@(TI_PRE_COUNT:16,er4),er1
	bne	done:8
	mov.l	@(TI_FLAGS:16,er4),er1
	btst	#TIF_NEED_RESCHED,r1l
	beq	done:8
	mov.b	r0l,r0l
	bpl	done:8			/* interrupt off (exception path?) */
	mov.l	#PREEMPT_ACTIVE,er1
	mov.l	er1,@(TI_PRE_COUNT:16,er4)
	andc	#0x7f,ccr
	jsr	@SYMBOL_NAME(schedule)
	sub.l	er1,er1
	mov.l	er1,@(TI_PRE_COUNT:16,er4)
	orc	#0x80,ccr
	bra	4b:8
#endif
done:
	RESTORE_ALL			/* Does RTE */

SYMBOL_NAME_LABEL(resume)
	/*
	 * Beware - when entering resume, offset of tss is in d1,
	 * prev (the current task) is in a0, next (the new task)
	 * is in a1 and d2.b is non-zero if the mm structure is
	 * shared between the tasks, so don't change these
	 * registers until their contents are no longer needed.
	 */

	/* save sr */
	sub.w	r3,r3
	stc	ccr,r3l
	mov.w	r3,@(THREAD_CCR+2:16,er0)

	/* disable interrupts */
	orc	#0x80,ccr
	mov.l	@SYMBOL_NAME(sw_usp),er3
	mov.l	er3,@(THREAD_USP:16,er0)
	mov.l	sp,@(THREAD_KSP:16,er0)

	/* Skip address space switching if they are the same. */
	/* FIXME: what did we hack out of here, this does nothing! */

	mov.l	@(THREAD_USP:16,er1),er0
	mov.l	er0,@SYMBOL_NAME(sw_usp)
	mov.l	@(THREAD_KSP:16,er1),sp
			
	/* restore status register */
	mov.w	@(THREAD_CCR+2:16,er1),r3

	ldc	r3l,ccr
	rts
	
SYMBOL_NAME_LABEL(trace_break)
	subs	#4,sp
	SAVE_ALL
	sub.l	er1,er1
	dec.l	#1,er1
	mov.l	er1,@(LORIG,sp)	
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@er0,er1
	subs	#2,er1
	mov.l	er1,@er0	
	and.w	#0xff,e1
	mov.l	er1,er0
	jsr	@SYMBOL_NAME(trace_trap)
	jmp	@SYMBOL_NAME(ret_from_exception)	

	.section	.bss
SYMBOL_NAME_LABEL(sw_ksp)
	.space	4	
SYMBOL_NAME_LABEL(sw_usp)
	.space	4	
