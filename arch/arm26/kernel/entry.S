/* arch/arm26/kernel/entry.S
 * 
 * Assembled from chunks of code in arch/arm
 *
 * Copyright (C) 2003 Ian Molton
 * Based on the work of RMK.
 *
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/hardware.h>
#include <asm/sysirq.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>

	.macro	zero_fp
#ifndef CONFIG_NO_FRAME_POINTER
	mov	fp, #0
#endif
	.endm

	.text

@ Bad Abort numbers
@ -----------------
@
#define BAD_PREFETCH	0
#define BAD_DATA	1
#define BAD_ADDREXCPTN	2
#define BAD_IRQ		3
#define BAD_UNDEFINSTR	4

@ OS version number used in SWIs
@  RISC OS is 0
@  RISC iX is 8
@
#define OS_NUMBER	9
#define ARMSWI_OFFSET	0x000f0000

@
@ Stack format (ensured by USER_* and SVC_*)
@ PSR and PC are comined on arm26
@

#define S_OFF		8

#define S_OLD_R0	64
#define S_PC		60
#define S_LR		56
#define S_SP		52
#define S_IP		48
#define S_FP		44
#define S_R10		40
#define S_R9		36
#define S_R8		32
#define S_R7		28
#define S_R6		24
#define S_R5		20
#define S_R4		16
#define S_R3		12
#define S_R2		8
#define S_R1		4
#define S_R0		0

	.macro	save_user_regs
	str	r0, [sp, #-4]!   @ Store SVC r0
	str	lr, [sp, #-4]!   @ Store user mode PC
	sub	sp, sp, #15*4
	stmia	sp, {r0 - lr}^   @ Store the other user-mode regs
	mov	r0, r0
	.endm

	.macro	slow_restore_user_regs
	ldmia	sp, {r0 - lr}^   @ restore the user regs not including PC
	mov	r0, r0
	ldr	lr, [sp, #15*4]  @ get user PC
	add	sp, sp, #15*4+8  @ free stack
	movs	pc, lr           @ return
	.endm

	.macro	fast_restore_user_regs
	add	sp, sp, #S_OFF
	ldmib	sp, {r1 - lr}^
	mov	r0, r0
	ldr	lr, [sp, #15*4]
	add	sp, sp, #15*4+8
	movs	pc, lr
	.endm

	.macro	save_svc_regs
	str     sp, [sp, #-16]!
	str     lr, [sp, #8]
	str     lr, [sp, #4]
	stmfd   sp!, {r0 - r12}
	mov     r0, #-1
	str     r0, [sp, #S_OLD_R0]
	zero_fp
	.endm

	.macro	save_svc_regs_irq
	str     sp, [sp, #-16]!
	str     lr, [sp, #4]
	ldr     lr, .LCirq
	ldr     lr, [lr]
	str     lr, [sp, #8]
	stmfd   sp!, {r0 - r12}
	mov     r0, #-1
	str     r0, [sp, #S_OLD_R0]
	zero_fp
	.endm

	.macro	restore_svc_regs
                ldmfd   sp, {r0 - pc}^
	.endm

	.macro	mask_pc, rd, rm
	bic	\rd, \rm, #PCMASK
	.endm

	.macro  disable_irqs, temp
	mov     \temp, pc
	orr     \temp, \temp, #PSR_I_BIT
	teqp    \temp, #0
	.endm

	.macro	enable_irqs, temp
	mov     \temp, pc
	and     \temp, \temp, #~PSR_I_BIT
	teqp	\temp, #0
	.endm

	.macro	initialise_traps_extra
	.endm

	.macro	get_thread_info, rd
	mov	\rd, sp, lsr #13
	mov	\rd, \rd, lsl #13
	.endm

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function - r0 to r6.
 *
 * Note that tbl == why is intentional.
 *
 * We must set at least "tsk" and "why" when calling ret_with_reschedule.
 */
scno	.req	r7		@ syscall number
tbl	.req	r8		@ syscall table pointer
why	.req	r8		@ Linux syscall (!= 0)
tsk	.req	r9		@ current thread_info

/*
 * Get the system call number.
 */
	.macro	get_scno
	mask_pc	lr, lr
	ldr	scno, [lr, #-4]		@ get SWI instruction
	.endm
/*
 *  -----------------------------------------------------------------------
 */

/* 
 * We rely on the fact that R0 is at the bottom of the stack (due to
 * slow/fast restore user regs).
 */
#if S_R0 != 0
#error "Please fix"
#endif

/*
 * This is the fast syscall return path.  We do as little as
 * possible here, and this includes saving r0 back into the SVC
 * stack.
 */
ret_fast_syscall:
	disable_irqs r1				@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	fast_work_pending
	fast_restore_user_regs

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
fast_work_pending:
	str	r0, [sp, #S_R0+S_OFF]!		@ returned r0
work_pending:
	tst	r1, #_TIF_NEED_RESCHED
	bne	work_resched
	tst	r1, #_TIF_NOTIFY_RESUME | _TIF_SIGPENDING
	beq	no_work_pending
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	bl	do_notify_resume
	disable_irqs r1				@ disable interrupts
	b	no_work_pending

work_resched:
	bl	schedule
/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
	disable_irqs r1				@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	work_pending
no_work_pending:
	slow_restore_user_regs

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	get_thread_info tsk
	ldr	r1, [tsk, #TI_FLAGS]		@ check for syscall tracing
	mov	why, #1
	tst	r1, #_TIF_SYSCALL_TRACE		@ are we tracing syscalls?
	beq	ret_slow_syscall
	mov	r1, sp
	mov	r0, #1				@ trace exit [IP = 1]
	bl	syscall_trace
	b	ret_slow_syscall
	
// FIXME - is this strictly necessary?
#include "calls.S"

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

	.align	5
ENTRY(vector_swi)
	save_user_regs
	zero_fp
	get_scno

#ifdef CONFIG_ALIGNMENT_TRAP
	ldr	ip, __cr_alignment
	ldr	ip, [ip]
	mcr	p15, 0, ip, c1, c0		@ update control register
#endif
	enable_irqs ip

	str	r4, [sp, #-S_OFF]!		@ push fifth arg

	get_thread_info tsk
	ldr	ip, [tsk, #TI_FLAGS]		@ check for syscall tracing
	bic	scno, scno, #0xff000000		@ mask off SWI op-code
	eor	scno, scno, #OS_NUMBER << 20	@ check OS number
	adr	tbl, sys_call_table		@ load syscall table pointer
	tst	ip, #_TIF_SYSCALL_TRACE		@ are we tracing syscalls?
	bne	__sys_trace

	adral	lr, ret_fast_syscall            @ set return address
        orral	lr, lr, #PSR_I_BIT | MODE_SVC26 @ Force SVC mode on return
	cmp	scno, #NR_syscalls		@ check upper syscall limit
	ldrcc	pc, [tbl, scno, lsl #2]		@ call sys_* routine

	add	r1, sp, #S_OFF
2:	mov	why, #0				@ no longer a real syscall
	cmp	scno, #ARMSWI_OFFSET
	eor	r0, scno, #OS_NUMBER << 20	@ put OS number back
	bcs	arm_syscall	
	b	sys_ni_syscall			@ not private func

	/*
	 * This is the really slow path.  We're going to be doing
	 * context switches, and waiting for our parent to respond.
	 */
__sys_trace:
	add	r1, sp, #S_OFF
	mov	r0, #0				@ trace entry [IP = 0]
	bl	syscall_trace

	adral   lr, __sys_trace_return          @ set return address
        orral   lr, lr, #PSR_I_BIT | MODE_SVC26 @ Force SVC mode on return
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	cmp	scno, #NR_syscalls		@ check upper syscall limit
	ldmccia	r1, {r0 - r3}			@ have to reload r0 - r3
	ldrcc	pc, [tbl, scno, lsl #2]		@ call sys_* routine
	b	2b

__sys_trace_return:
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
	mov	r1, sp
	mov	r0, #1				@ trace exit [IP = 1]
	bl	syscall_trace
	b	ret_slow_syscall

	.align	5
#ifdef CONFIG_ALIGNMENT_TRAP
	.type	__cr_alignment, #object
__cr_alignment:
	.word	cr_alignment
#endif

	.type	sys_call_table, #object
ENTRY(sys_call_table)
#include "calls.S"

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r5 = syscall table
		.type	sys_syscall, #function
sys_syscall:
		eor	scno, r0, #OS_NUMBER << 20
		cmp	scno, #NR_syscalls	@ check range
		stmleia	sp, {r5, r6}		@ shuffle args
		movle	r0, r1
		movle	r1, r2
		movle	r2, r3
		movle	r3, r4
		ldrle	pc, [tbl, scno, lsl #2]
		b	sys_ni_syscall

sys_fork_wrapper:
		add	r0, sp, #S_OFF
		b	sys_fork

sys_vfork_wrapper:
		add	r0, sp, #S_OFF
		b	sys_vfork

sys_execve_wrapper:
		add	r3, sp, #S_OFF
		b	sys_execve

sys_clone_wapper:
		add	r2, sp, #S_OFF
		b	sys_clone

sys_sigsuspend_wrapper:
		add	r3, sp, #S_OFF
		b	sys_sigsuspend

sys_rt_sigsuspend_wrapper:
		add	r2, sp, #S_OFF
		b	sys_rt_sigsuspend

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	sys_sigreturn

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	sys_rt_sigreturn

sys_sigaltstack_wrapper:
		ldr	r2, [sp, #S_OFF + S_SP]
		b	do_sigaltstack

/*
 * Note: off_4k (r5) is always units of 4K.  If we can't do the requested
 * offset, we return EINVAL.  FIXME - this lost some stuff from arm32 to
 * ifdefs. check it out.
 */
sys_mmap2:
		tst	r5, #((1 << (PAGE_SHIFT - 12)) - 1)
		moveq	r5, r5, lsr #PAGE_SHIFT - 12
		streq	r5, [sp, #4]
		beq	do_mmap2
		mov	r0, #-EINVAL
		RETINSTR(mov,pc, lr)

/*
 *  Design issues:
 *   - We have several modes that each vector can be called from,
 *     each with its own set of registers.  On entry to any vector,
 *     we *must* save the registers used in *that* mode.
 *
 *   - This code must be as fast as possible.
 *
 *  There are a few restrictions on the vectors:
 *   - the SWI vector cannot be called from *any* non-user mode
 *
 *   - the FP emulator is *never* called from *any* non-user mode undefined
 *     instruction.
 *
 */

		.text

		.macro handle_irq
1:		mov     r4, #IOC_BASE
		ldrb    r6, [r4, #0x24]            @ get high priority first
		adr     r5, irq_prio_h
		teq     r6, #0
		ldreqb  r6, [r4, #0x14]            @ get low priority
		adreq   r5, irq_prio_l

                teq     r6, #0                     @ If an IRQ happened...
                ldrneb  r0, [r5, r6]               @ get IRQ number
                movne   r1, sp                     @ get struct pt_regs
                adrne   lr, 1b                     @ Set return address to 1b
                orrne   lr, lr, #PSR_I_BIT | MODE_SVC26  @ (and force SVC mode)
                bne     asm_do_IRQ                 @ process IRQ (if asserted)
		.endm


/*
 * Interrupt table (incorporates priority)
 */
		.macro	irq_prio_table
irq_prio_l:	.byte	 0, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 4, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
irq_prio_h:	.byte	 0, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	12, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.endm

#if 1
/*
 * Uncomment these if you wish to get more debugging into about data aborts.
 * FIXME - I bet we can find a way to encode these and keep performance.
 */
#define FAULT_CODE_LDRSTRPOST	0x80
#define FAULT_CODE_LDRSTRPRE	0x40
#define FAULT_CODE_LDRSTRREG	0x20
#define FAULT_CODE_LDMSTM	0x10
#define FAULT_CODE_LDCSTC	0x08
#endif
#define FAULT_CODE_PREFETCH	0x04
#define FAULT_CODE_WRITE	0x02
#define FAULT_CODE_FORCECOW	0x01

/*=============================================================================
 * Undefined FIQs
 *-----------------------------------------------------------------------------
 */
_unexp_fiq:	ldr     sp, .LCfiq
		mov	r12, #IOC_BASE
		strb	r12, [r12, #0x38]	@ Disable FIQ register
		teqp	pc, #PSR_I_BIT | PSR_F_BIT | MODE_SVC26
		mov	r0, r0
		stmfd	sp!, {r0 - r3, ip, lr}
		adr	r0, Lfiqmsg
		bl	printk
		ldmfd	sp!, {r0 - r3, ip, lr}
		teqp	pc, #PSR_I_BIT | PSR_F_BIT | MODE_FIQ26
		mov	r0, r0
		movs	pc, lr

Lfiqmsg:	.ascii	"*** Unexpected FIQ\n\0"
		.align

.LCfiq:		.word	__temp_fiq
.LCirq:		.word	__temp_irq

/*=============================================================================
 * Undefined instruction handler
 *-----------------------------------------------------------------------------
 * Handles floating point instructions
 */
vector_undefinstr:
		tst	lr, #MODE_SVC26          @ did we come from a non-user mode?
		bne	__und_svc                @ yes - deal with it.
/* Otherwise, fall through for the user-space (common) case. */
		save_user_regs
		zero_fp                                 @ zero frame pointer
		teqp	pc, #PSR_I_BIT | MODE_SVC26     @ disable IRQs
.Lbug_undef:
		ldr	r4, .LC2
                ldr     pc, [r4]         @ Call FP module entry point
/* FIXME - should we trap for a null pointer here? */

/* The SVC mode case */
__und_svc:	save_svc_regs                           @ Non-user mode
                mask_pc r0, lr
                and     r2, lr, #3
                sub     r0, r0, #4
                mov     r1, sp
                bl      do_undefinstr
                restore_svc_regs

/* We get here if the FP emulator doesnt handle the undef instr.
 * If the insn WAS handled, the emulator jumps to ret_from_exception by itself/
 */
		.globl	fpundefinstr 
fpundefinstr:
		mov	r0, lr
		mov	r1, sp
		teqp	pc, #MODE_SVC26
		bl	do_undefinstr
		b	ret_from_exception		@ Normal FP exit

#if defined CONFIG_FPE_NWFPE || defined CONFIG_FPE_FASTFPE
		/* The FPE is always present */
		.equ	fpe_not_present, 0
#else
/* We get here if an undefined instruction happens and the floating
 * point emulator is not present.  If the offending instruction was
 * a WFS, we just perform a normal return as if we had emulated the
 * operation.  This is a hack to allow some basic userland binaries
 * to run so that the emulator module proper can be loaded. --philb
 * FIXME - probably a broken useless hack...
 */
fpe_not_present:
		adr	r10, wfs_mask_data
		ldmia	r10, {r4, r5, r6, r7, r8}
		ldr	r10, [sp, #S_PC]		@ Load PC
		sub	r10, r10, #4
		mask_pc	r10, r10
		ldrt	r10, [r10]			@ get instruction
		and	r5, r10, r5
		teq	r5, r4				@ Is it WFS?
		beq	ret_from_exception
		and	r5, r10, r8
		teq	r5, r6				@ Is it LDF/STF on sp or fp?
		teqne	r5, r7
		bne	fpundefinstr
		tst	r10, #0x00200000		@ Does it have WB
		beq	ret_from_exception
		and	r4, r10, #255			@ get offset
		and	r6, r10, #0x000f0000
		tst	r10, #0x00800000		@ +/-
		ldr	r5, [sp, r6, lsr #14]		@ Load reg
		rsbeq	r4, r4, #0
		add	r5, r5, r4, lsl #2
		str	r5, [sp, r6, lsr #14]		@ Save reg
		b	ret_from_exception

wfs_mask_data:	.word	0x0e200110			@ WFS/RFS
		.word	0x0fef0fff
		.word	0x0d0d0100			@ LDF [sp]/STF [sp]
		.word	0x0d0b0100			@ LDF [fp]/STF [fp]
		.word	0x0f0f0f00
#endif

.LC2:		.word	fp_enter

/*=============================================================================
 * Prefetch abort handler
 *-----------------------------------------------------------------------------
 */
#define DEBUG_UNDEF
/* remember: lr = USR pc */
vector_prefetch:
		sub	lr, lr, #4
		tst	lr, #MODE_SVC26
		bne	__pabt_invalid
		save_user_regs
		teqp	pc, #MODE_SVC26         @ Enable IRQs...
		mask_pc	r0, lr			@ Address of abort
		mov	r1, sp			@ Tasks registers
		bl	do_PrefetchAbort
		teq	r0, #0			@ If non-zero, we believe this abort..
		bne	ret_from_exception
#ifdef DEBUG_UNDEF
		adr	r0, t
		bl	printk
#endif
		ldr	lr, [sp,#S_PC]		@ FIXME program to test this on.  I think its
		b	.Lbug_undef		@ broken at the moment though!)

__pabt_invalid:	save_svc_regs
		mov	r0, sp			@ Prefetch aborts are definitely *not*
		mov	r1, #BAD_PREFETCH	@ allowed in non-user modes.  We cant
		and	r2, lr, #3		@ recover from this problem.
		b	bad_mode

#ifdef DEBUG_UNDEF
t:		.ascii "*** undef ***\r\n\0"
		.align
#endif

/*=============================================================================
 * Address exception handler
 *-----------------------------------------------------------------------------
 * These aren't too critical.
 * (they're not supposed to happen).
 * In order to debug the reason for address exceptions in non-user modes,
 * we have to obtain all the registers so that we can see what's going on.
 */

vector_addrexcptn:
		sub	lr, lr, #8
		tst	lr, #3
		bne	Laddrexcptn_not_user
		save_user_regs
		teq	pc, #MODE_SVC26
		mask_pc	r0, lr			@ Point to instruction
		mov	r1, sp			@ Point to registers
		mov	r2, #0x400
		mov	lr, pc
		bl	do_excpt
		b	ret_from_exception

Laddrexcptn_not_user:
		save_svc_regs
		and	r2, lr, #3
		teq	r2, #3
		bne	Laddrexcptn_illegal_mode
		teqp	pc, #MODE_SVC26
		mask_pc	r0, lr
		mov	r1, sp
		orr	r2, r2, #0x400
		bl	do_excpt
		ldmia	sp, {r0 - lr}		@ I cant remember the reason I changed this...
		add	sp, sp, #15*4
		movs	pc, lr

Laddrexcptn_illegal_mode:
		mov	r0, sp
		str	lr, [sp, #-4]!
		orr	r1, r2, #PSR_I_BIT | PSR_F_BIT
		teqp	r1, #0			@ change into mode (wont be user mode)
		mov	r0, r0
		mov	r1, r8			@ Any register from r8 - r14 can be banked
		mov	r2, r9
		mov	r3, r10
		mov	r4, r11
		mov	r5, r12
		mov	r6, r13
		mov	r7, r14
		teqp	pc, #PSR_F_BIT | MODE_SVC26 @ back to svc
		mov	r0, r0
		stmfd	sp!, {r1-r7}
		ldmia	r0, {r0-r7}
		stmfd	sp!, {r0-r7}
		mov	r0, sp
		mov	r1, #BAD_ADDREXCPTN
		b	bad_mode

/*=============================================================================
 * Interrupt (IRQ) handler
 *-----------------------------------------------------------------------------
 * Note: if the IRQ was taken whilst in user mode, then *no* kernel routine
 * is running, so do not have to save svc lr.
 *
 * Entered in IRQ mode.
 */

vector_IRQ:	ldr     sp, .LCirq         @ Setup some temporary stack
                sub     lr, lr, #4
                str     lr, [sp]           @ push return address

		tst     lr, #3
		bne	__irq_non_usr

__irq_usr:	teqp	pc, #PSR_I_BIT | MODE_SVC26     @ Enter SVC mode
		mov	r0, r0

		ldr	lr, .LCirq
		ldr	lr, [lr]           @ Restore lr for jump back to USR

		save_user_regs

		handle_irq

		mov	why, #0
		get_thread_info tsk
		b	ret_to_user

@ Place the IRQ priority table here so that the handle_irq macros above
@ and below here can access it.

		irq_prio_table

__irq_non_usr:	teqp	pc, #PSR_I_BIT | MODE_SVC26     @ Enter SVC mode
		mov	r0, r0

		save_svc_regs_irq

                and	r2, lr, #3
		teq	r2, #3
		bne	__irq_invalid                @ IRQ not from SVC mode

		handle_irq

		restore_svc_regs

__irq_invalid:	mov	r0, sp
		mov	r1, #BAD_IRQ
		b	bad_mode

/*=============================================================================
 * Data abort handler code
 *-----------------------------------------------------------------------------
 *
 * This handles both exceptions from user and SVC modes, computes the address
 *  range of the problem, and does any correction that is required.  It then
 *  calls the kernel data abort routine.
 *
 * This is where I wish that the ARM would tell you which address aborted.
 */

vector_data:	sub	lr, lr, #8		@ Correct lr
		tst	lr, #3
		bne	Ldata_not_user
		save_user_regs
		teqp	pc, #MODE_SVC26
		mask_pc	r0, lr
		bl	Ldata_do
		b	ret_from_exception

Ldata_not_user:
		save_svc_regs
		and	r2, lr, #3
		teq	r2, #3
		bne	Ldata_illegal_mode
		tst	lr, #PSR_I_BIT
		teqeqp	pc, #MODE_SVC26
		mask_pc	r0, lr
		bl	Ldata_do
		restore_svc_regs

Ldata_illegal_mode:
		mov	r0, sp
		mov	r1, #BAD_DATA
		b	bad_mode

Ldata_do:	mov	r3, sp
		ldr	r4, [r0]		@ Get instruction
		mov	r2, #0
		tst	r4, #1 << 20		@ Check to see if it is a write instruction
		orreq	r2, r2, #FAULT_CODE_WRITE @ Indicate write instruction
		mov	r1, r4, lsr #22		@ Now branch to the relevent processing routine
		and	r1, r1, #15 << 2
		add	pc, pc, r1
		movs	pc, lr
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_ldrstr_post	@ ldr	rd, [rn], #m
		b	Ldata_ldrstr_numindex	@ ldr	rd, [rn, #m]	@ RegVal
		b	Ldata_ldrstr_post	@ ldr	rd, [rn], rm
		b	Ldata_ldrstr_regindex	@ ldr	rd, [rn, rm]
		b	Ldata_ldmstm		@ ldm*a	rn, <rlist>
		b	Ldata_ldmstm		@ ldm*b	rn, <rlist>
		b	Ldata_unknown
		b	Ldata_unknown
		b	Ldata_ldrstr_post	@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
		b	Ldata_ldcstc_pre	@ ldc	rd, [rn, #m]
		b	Ldata_unknown
Ldata_unknown:	@ Part of jumptable
		mov	r0, r1
		mov	r1, r4
		mov	r2, r3
		b	baddataabort

Ldata_ldrstr_post:
		mov	r0, r4, lsr #14		@ Get Rn
		and	r0, r0, #15 << 2	@ Mask out reg.
		teq	r0, #15 << 2
		ldr	r0, [r3, r0]		@ Get register
		biceq	r0, r0, #PCMASK
		mov	r1, r0
#ifdef FAULT_CODE_LDRSTRPOST
		orr	r2, r2, #FAULT_CODE_LDRSTRPOST
#endif
		b	do_DataAbort

Ldata_ldrstr_numindex:
		mov	r0, r4, lsr #14		@ Get Rn
		and	r0, r0, #15 << 2	@ Mask out reg.
		teq	r0, #15 << 2
		ldr	r0, [r3, r0]		@ Get register
		mov	r1, r4, lsl #20
		biceq	r0, r0, #PCMASK
		tst	r4, #1 << 23
		addne	r0, r0, r1, lsr #20
		subeq	r0, r0, r1, lsr #20
		mov	r1, r0
#ifdef FAULT_CODE_LDRSTRPRE
		orr	r2, r2, #FAULT_CODE_LDRSTRPRE
#endif
		b	do_DataAbort

Ldata_ldrstr_regindex:
		mov	r0, r4, lsr #14		@ Get Rn
		and	r0, r0, #15 << 2	@ Mask out reg.
		teq	r0, #15 << 2
		ldr	r0, [r3, r0]		@ Get register
		and	r7, r4, #15
		biceq	r0, r0, #PCMASK
		teq	r7, #15			@ Check for PC
		ldr	r7, [r3, r7, lsl #2]	@ Get Rm
		and	r8, r4, #0x60		@ Get shift types
		biceq	r7, r7, #PCMASK
		mov	r9, r4, lsr #7		@ Get shift amount
		and	r9, r9, #31
		teq	r8, #0
		moveq	r7, r7, lsl r9
		teq	r8, #0x20		@ LSR shift
		moveq	r7, r7, lsr r9
		teq	r8, #0x40		@ ASR shift
		moveq	r7, r7, asr r9
		teq	r8, #0x60		@ ROR shift
		moveq	r7, r7, ror r9
		tst	r4, #1 << 23
		addne	r0, r0, r7
		subeq	r0, r0, r7		@ Apply correction
		mov	r1, r0
#ifdef FAULT_CODE_LDRSTRREG
		orr	r2, r2, #FAULT_CODE_LDRSTRREG
#endif
		b	do_DataAbort

Ldata_ldmstm:
		mov	r7, #0x11
		orr	r7, r7, r7, lsl #8
		and	r0, r4, r7
		and	r1, r4, r7, lsl #1
		add	r0, r0, r1, lsr #1
		and	r1, r4, r7, lsl #2
		add	r0, r0, r1, lsr #2
		and	r1, r4, r7, lsl #3
		add	r0, r0, r1, lsr #3
		add	r0, r0, r0, lsr #8
		add	r0, r0, r0, lsr #4
		and	r7, r0, #15		@ r7 = no. of registers to transfer.
		mov	r5, r4, lsr #14		@ Get Rn
		and	r5, r5, #15 << 2
		ldr	r0, [r3, r5]		@ Get reg
		eor	r6, r4, r4, lsl #2
		tst	r6, #1 << 23		@ Check inc/dec ^ writeback
		rsbeq	r7, r7, #0
		add	r7, r0, r7, lsl #2	@ Do correction (signed)
		subne	r1, r7, #1
		subeq	r1, r0, #1
		moveq	r0, r7
		tst	r4, #1 << 21		@ Check writeback
		strne	r7, [r3, r5]
		eor	r6, r4, r4, lsl #1
		tst	r6, #1 << 24		@ Check Pre/Post ^ inc/dec
		addeq	r0, r0, #4
		addeq	r1, r1, #4
		teq	r5, #15*4		@ CHECK FOR PC
		biceq	r1, r1, #PCMASK
		biceq	r0, r0, #PCMASK
#ifdef FAULT_CODE_LDMSTM
		orr	r2, r2, #FAULT_CODE_LDMSTM
#endif
		b	do_DataAbort

Ldata_ldcstc_pre:
		mov	r0, r4, lsr #14		@ Get Rn
		and	r0, r0, #15 << 2	@ Mask out reg.
		teq	r0, #15 << 2
		ldr	r0, [r3, r0]		@ Get register
		mov	r1, r4, lsl #24		@ Get offset
		biceq	r0, r0, #PCMASK
		tst	r4, #1 << 23
		addne	r0, r0, r1, lsr #24
		subeq	r0, r0, r1, lsr #24
		mov	r1, r0
#ifdef FAULT_CODE_LDCSTC
		orr	r2, r2, #FAULT_CODE_LDCSTC
#endif
		b	do_DataAbort


/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
		get_thread_info tsk
		mov	why, #0
		b	ret_to_user

		.data
ENTRY(fp_enter)
		.word	fpe_not_present
		.text
/*
 * Register switch for older 26-bit only ARMs
 */
ENTRY(__switch_to)
		add	r0, r0, #TI_CPU_SAVE
		stmia	r0, {r4 - sl, fp, sp, lr}
		add	r1, r1, #TI_CPU_SAVE
		ldmia	r1, {r4 - sl, fp, sp, pc}^

/*
 *=============================================================================
 *		Low-level interface code
 *-----------------------------------------------------------------------------
 *		Trap initialisation
 *-----------------------------------------------------------------------------
 *
 * Note - FIQ code has changed.  The default is a couple of words in 0x1c, 0x20
 * that call _unexp_fiq.  Nowever, we now copy the FIQ routine to 0x1c (removes
 * some excess cycles).
 *
 * What we need to put into 0-0x1c are branches to branch to the kernel.
 */

		.section ".init.text",#alloc,#execinstr

.Ljump_addresses:
		swi	SYS_ERROR0
		.word	vector_undefinstr	- 12
		.word	vector_swi		- 16
		.word	vector_prefetch		- 20
		.word	vector_data		- 24
		.word	vector_addrexcptn	- 28
		.word	vector_IRQ		- 32
		.word	_unexp_fiq		- 36
		b	. + 8
/*
 * initialise the trap system
 */
ENTRY(__trap_init)
		stmfd	sp!, {r4 - r7, lr}
		adr	r1, .Ljump_addresses
		ldmia	r1, {r1 - r7, ip, lr}
		orr	r2, lr, r2, lsr #2
		orr	r3, lr, r3, lsr #2
		orr	r4, lr, r4, lsr #2
		orr	r5, lr, r5, lsr #2
		orr	r6, lr, r6, lsr #2
		orr	r7, lr, r7, lsr #2
		orr	ip, lr, ip, lsr #2
		mov	r0, #0
		stmia	r0, {r1 - r7, ip}
		ldmfd	sp!, {r4 - r7, pc}^

		.bss
__temp_irq:	.space	4				@ saved lr_irq
__temp_fiq:	.space	128
