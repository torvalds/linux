/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/kernel/entry.S
 *
 * Kernel entry-points.
 */

#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/pal.h>
#include <asm/errno.h>
#include <asm/unistd.h>

	.text
	.set noat
	.cfi_sections	.debug_frame

/* Stack offsets.  */
#define SP_OFF			184
#define SWITCH_STACK_SIZE	320

.macro	CFI_START_OSF_FRAME	func
	.align	4
	.globl	\func
	.type	\func,@function
\func:
	.cfi_startproc simple
	.cfi_return_column 64
	.cfi_def_cfa	$sp, 48
	.cfi_rel_offset	64, 8
	.cfi_rel_offset	$gp, 16
	.cfi_rel_offset	$16, 24
	.cfi_rel_offset	$17, 32
	.cfi_rel_offset	$18, 40
.endm

.macro	CFI_END_OSF_FRAME	func
	.cfi_endproc
	.size	\func, . - \func
.endm

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs 9-15 preserved by C code
 * regs 16-18 saved by PAL-code
 * regs 29-30 saved and set up by PAL-code
 * JRP - Save regs 16-18 in a special area of the stack, so that
 * the palcode-provided values are available to the signal handler.
 */

.macro	SAVE_ALL
	subq	$sp, SP_OFF, $sp
	.cfi_adjust_cfa_offset	SP_OFF
	stq	$0, 0($sp)
	stq	$1, 8($sp)
	stq	$2, 16($sp)
	stq	$3, 24($sp)
	stq	$4, 32($sp)
	stq	$28, 144($sp)
	.cfi_rel_offset	$0, 0
	.cfi_rel_offset $1, 8
	.cfi_rel_offset	$2, 16
	.cfi_rel_offset	$3, 24
	.cfi_rel_offset	$4, 32
	.cfi_rel_offset	$28, 144
	lda	$2, alpha_mv
	stq	$5, 40($sp)
	stq	$6, 48($sp)
	stq	$7, 56($sp)
	stq	$8, 64($sp)
	stq	$19, 72($sp)
	stq	$20, 80($sp)
	stq	$21, 88($sp)
	ldq	$2, HAE_CACHE($2)
	stq	$22, 96($sp)
	stq	$23, 104($sp)
	stq	$24, 112($sp)
	stq	$25, 120($sp)
	stq	$26, 128($sp)
	stq	$27, 136($sp)
	stq	$2, 152($sp)
	stq	$16, 160($sp)
	stq	$17, 168($sp)
	stq	$18, 176($sp)
	.cfi_rel_offset	$5, 40
	.cfi_rel_offset	$6, 48
	.cfi_rel_offset	$7, 56
	.cfi_rel_offset	$8, 64
	.cfi_rel_offset $19, 72
	.cfi_rel_offset	$20, 80
	.cfi_rel_offset	$21, 88
	.cfi_rel_offset $22, 96
	.cfi_rel_offset	$23, 104
	.cfi_rel_offset	$24, 112
	.cfi_rel_offset	$25, 120
	.cfi_rel_offset	$26, 128
	.cfi_rel_offset	$27, 136
.endm

.macro	RESTORE_ALL
	lda	$19, alpha_mv
	ldq	$0, 0($sp)
	ldq	$1, 8($sp)
	ldq	$2, 16($sp)
	ldq	$3, 24($sp)
	ldq	$21, 152($sp)
	ldq	$20, HAE_CACHE($19)
	ldq	$4, 32($sp)
	ldq	$5, 40($sp)
	ldq	$6, 48($sp)
	ldq	$7, 56($sp)
	subq	$20, $21, $20
	ldq	$8, 64($sp)
	beq	$20, 99f
	ldq	$20, HAE_REG($19)
	stq	$21, HAE_CACHE($19)
	stq	$21, 0($20)
99:	ldq	$19, 72($sp)
	ldq	$20, 80($sp)
	ldq	$21, 88($sp)
	ldq	$22, 96($sp)
	ldq	$23, 104($sp)
	ldq	$24, 112($sp)
	ldq	$25, 120($sp)
	ldq	$26, 128($sp)
	ldq	$27, 136($sp)
	ldq	$28, 144($sp)
	addq	$sp, SP_OFF, $sp
	.cfi_restore	$0
	.cfi_restore	$1
	.cfi_restore	$2
	.cfi_restore	$3
	.cfi_restore	$4
	.cfi_restore	$5
	.cfi_restore	$6
	.cfi_restore	$7
	.cfi_restore	$8
	.cfi_restore	$19
	.cfi_restore	$20
	.cfi_restore	$21
	.cfi_restore	$22
	.cfi_restore	$23
	.cfi_restore	$24
	.cfi_restore	$25
	.cfi_restore	$26
	.cfi_restore	$27
	.cfi_restore	$28
	.cfi_adjust_cfa_offset	-SP_OFF
.endm

.macro	DO_SWITCH_STACK
	bsr	$1, do_switch_stack
	.cfi_adjust_cfa_offset	SWITCH_STACK_SIZE
	.cfi_rel_offset	$9, 0
	.cfi_rel_offset	$10, 8
	.cfi_rel_offset	$11, 16
	.cfi_rel_offset	$12, 24
	.cfi_rel_offset	$13, 32
	.cfi_rel_offset	$14, 40
	.cfi_rel_offset	$15, 48
	/* We don't really care about the FP registers for debugging.  */
.endm

.macro	UNDO_SWITCH_STACK
	bsr	$1, undo_switch_stack
	.cfi_restore	$9
	.cfi_restore	$10
	.cfi_restore	$11
	.cfi_restore	$12
	.cfi_restore	$13
	.cfi_restore	$14
	.cfi_restore	$15
	.cfi_adjust_cfa_offset	-SWITCH_STACK_SIZE
.endm

/*
 * Non-syscall kernel entry points.
 */

CFI_START_OSF_FRAME entInt
	SAVE_ALL
	lda	$8, 0x3fff
	lda	$26, ret_from_sys_call
	bic	$sp, $8, $8
	mov	$sp, $19
	jsr	$31, do_entInt
CFI_END_OSF_FRAME entInt

CFI_START_OSF_FRAME entArith
	SAVE_ALL
	lda	$8, 0x3fff
	lda	$26, ret_from_sys_call
	bic	$sp, $8, $8
	mov	$sp, $18
	jsr	$31, do_entArith
CFI_END_OSF_FRAME entArith

CFI_START_OSF_FRAME entMM
	SAVE_ALL
/* save $9 - $15 so the inline exception code can manipulate them.  */
	subq	$sp, 56, $sp
	.cfi_adjust_cfa_offset	56
	stq	$9, 0($sp)
	stq	$10, 8($sp)
	stq	$11, 16($sp)
	stq	$12, 24($sp)
	stq	$13, 32($sp)
	stq	$14, 40($sp)
	stq	$15, 48($sp)
	.cfi_rel_offset	$9, 0
	.cfi_rel_offset	$10, 8
	.cfi_rel_offset	$11, 16
	.cfi_rel_offset	$12, 24
	.cfi_rel_offset	$13, 32
	.cfi_rel_offset	$14, 40
	.cfi_rel_offset	$15, 48
	addq	$sp, 56, $19
/* handle the fault */
	lda	$8, 0x3fff
	bic	$sp, $8, $8
	jsr	$26, do_page_fault
/* reload the registers after the exception code played.  */
	ldq	$9, 0($sp)
	ldq	$10, 8($sp)
	ldq	$11, 16($sp)
	ldq	$12, 24($sp)
	ldq	$13, 32($sp)
	ldq	$14, 40($sp)
	ldq	$15, 48($sp)
	addq	$sp, 56, $sp
	.cfi_restore	$9
	.cfi_restore	$10
	.cfi_restore	$11
	.cfi_restore	$12
	.cfi_restore	$13
	.cfi_restore	$14
	.cfi_restore	$15
	.cfi_adjust_cfa_offset	-56
/* finish up the syscall as normal.  */
	br	ret_from_sys_call
CFI_END_OSF_FRAME entMM

CFI_START_OSF_FRAME entIF
	SAVE_ALL
	lda	$8, 0x3fff
	lda	$26, ret_from_sys_call
	bic	$sp, $8, $8
	mov	$sp, $17
	jsr	$31, do_entIF
CFI_END_OSF_FRAME entIF

CFI_START_OSF_FRAME entUna
	lda	$sp, -256($sp)
	.cfi_adjust_cfa_offset	256
	stq	$0, 0($sp)
	.cfi_rel_offset	$0, 0
	.cfi_remember_state
	ldq	$0, 256($sp)	/* get PS */
	stq	$1, 8($sp)
	stq	$2, 16($sp)
	stq	$3, 24($sp)
	and	$0, 8, $0		/* user mode? */
	stq	$4, 32($sp)
	bne	$0, entUnaUser	/* yup -> do user-level unaligned fault */
	stq	$5, 40($sp)
	stq	$6, 48($sp)
	stq	$7, 56($sp)
	stq	$8, 64($sp)
	stq	$9, 72($sp)
	stq	$10, 80($sp)
	stq	$11, 88($sp)
	stq	$12, 96($sp)
	stq	$13, 104($sp)
	stq	$14, 112($sp)
	stq	$15, 120($sp)
	/* 16-18 PAL-saved */
	stq	$19, 152($sp)
	stq	$20, 160($sp)
	stq	$21, 168($sp)
	stq	$22, 176($sp)
	stq	$23, 184($sp)
	stq	$24, 192($sp)
	stq	$25, 200($sp)
	stq	$26, 208($sp)
	stq	$27, 216($sp)
	stq	$28, 224($sp)
	mov	$sp, $19
	stq	$gp, 232($sp)
	.cfi_rel_offset	$1, 1*8
	.cfi_rel_offset	$2, 2*8
	.cfi_rel_offset	$3, 3*8
	.cfi_rel_offset	$4, 4*8
	.cfi_rel_offset	$5, 5*8
	.cfi_rel_offset	$6, 6*8
	.cfi_rel_offset	$7, 7*8
	.cfi_rel_offset	$8, 8*8
	.cfi_rel_offset	$9, 9*8
	.cfi_rel_offset	$10, 10*8
	.cfi_rel_offset	$11, 11*8
	.cfi_rel_offset	$12, 12*8
	.cfi_rel_offset	$13, 13*8
	.cfi_rel_offset	$14, 14*8
	.cfi_rel_offset	$15, 15*8
	.cfi_rel_offset	$19, 19*8
	.cfi_rel_offset	$20, 20*8
	.cfi_rel_offset	$21, 21*8
	.cfi_rel_offset	$22, 22*8
	.cfi_rel_offset	$23, 23*8
	.cfi_rel_offset	$24, 24*8
	.cfi_rel_offset	$25, 25*8
	.cfi_rel_offset	$26, 26*8
	.cfi_rel_offset	$27, 27*8
	.cfi_rel_offset	$28, 28*8
	.cfi_rel_offset	$29, 29*8
	lda	$8, 0x3fff
	stq	$31, 248($sp)
	bic	$sp, $8, $8
	jsr	$26, do_entUna
	ldq	$0, 0($sp)
	ldq	$1, 8($sp)
	ldq	$2, 16($sp)
	ldq	$3, 24($sp)
	ldq	$4, 32($sp)
	ldq	$5, 40($sp)
	ldq	$6, 48($sp)
	ldq	$7, 56($sp)
	ldq	$8, 64($sp)
	ldq	$9, 72($sp)
	ldq	$10, 80($sp)
	ldq	$11, 88($sp)
	ldq	$12, 96($sp)
	ldq	$13, 104($sp)
	ldq	$14, 112($sp)
	ldq	$15, 120($sp)
	/* 16-18 PAL-saved */
	ldq	$19, 152($sp)
	ldq	$20, 160($sp)
	ldq	$21, 168($sp)
	ldq	$22, 176($sp)
	ldq	$23, 184($sp)
	ldq	$24, 192($sp)
	ldq	$25, 200($sp)
	ldq	$26, 208($sp)
	ldq	$27, 216($sp)
	ldq	$28, 224($sp)
	ldq	$gp, 232($sp)
	lda	$sp, 256($sp)
	.cfi_restore	$1
	.cfi_restore	$2
	.cfi_restore	$3
	.cfi_restore	$4
	.cfi_restore	$5
	.cfi_restore	$6
	.cfi_restore	$7
	.cfi_restore	$8
	.cfi_restore	$9
	.cfi_restore	$10
	.cfi_restore	$11
	.cfi_restore	$12
	.cfi_restore	$13
	.cfi_restore	$14
	.cfi_restore	$15
	.cfi_restore	$19
	.cfi_restore	$20
	.cfi_restore	$21
	.cfi_restore	$22
	.cfi_restore	$23
	.cfi_restore	$24
	.cfi_restore	$25
	.cfi_restore	$26
	.cfi_restore	$27
	.cfi_restore	$28
	.cfi_restore	$29
	.cfi_adjust_cfa_offset	-256
	call_pal PAL_rti

	.align	4
entUnaUser:
	.cfi_restore_state
	ldq	$0, 0($sp)	/* restore original $0 */
	lda	$sp, 256($sp)	/* pop entUna's stack frame */
	.cfi_restore	$0
	.cfi_adjust_cfa_offset	-256
	SAVE_ALL		/* setup normal kernel stack */
	lda	$sp, -56($sp)
	.cfi_adjust_cfa_offset	56
	stq	$9, 0($sp)
	stq	$10, 8($sp)
	stq	$11, 16($sp)
	stq	$12, 24($sp)
	stq	$13, 32($sp)
	stq	$14, 40($sp)
	stq	$15, 48($sp)
	.cfi_rel_offset	$9, 0
	.cfi_rel_offset	$10, 8
	.cfi_rel_offset	$11, 16
	.cfi_rel_offset	$12, 24
	.cfi_rel_offset	$13, 32
	.cfi_rel_offset	$14, 40
	.cfi_rel_offset	$15, 48
	lda	$8, 0x3fff
	addq	$sp, 56, $19
	bic	$sp, $8, $8
	jsr	$26, do_entUnaUser
	ldq	$9, 0($sp)
	ldq	$10, 8($sp)
	ldq	$11, 16($sp)
	ldq	$12, 24($sp)
	ldq	$13, 32($sp)
	ldq	$14, 40($sp)
	ldq	$15, 48($sp)
	lda	$sp, 56($sp)
	.cfi_restore	$9
	.cfi_restore	$10
	.cfi_restore	$11
	.cfi_restore	$12
	.cfi_restore	$13
	.cfi_restore	$14
	.cfi_restore	$15
	.cfi_adjust_cfa_offset	-56
	br	ret_from_sys_call
CFI_END_OSF_FRAME entUna

CFI_START_OSF_FRAME entDbg
	SAVE_ALL
	lda	$8, 0x3fff
	lda	$26, ret_from_sys_call
	bic	$sp, $8, $8
	mov	$sp, $16
	jsr	$31, do_entDbg
CFI_END_OSF_FRAME entDbg

/*
 * The system call entry point is special.  Most importantly, it looks
 * like a function call to userspace as far as clobbered registers.  We
 * do preserve the argument registers (for syscall restarts) and $26
 * (for leaf syscall functions).
 *
 * So much for theory.  We don't take advantage of this yet.
 *
 * Note that a0-a2 are not saved by PALcode as with the other entry points.
 */

	.align	4
	.globl	entSys
	.type	entSys, @function
	.cfi_startproc simple
	.cfi_return_column 64
	.cfi_def_cfa	$sp, 48
	.cfi_rel_offset	64, 8
	.cfi_rel_offset	$gp, 16
entSys:
	SAVE_ALL
	lda	$8, 0x3fff
	bic	$sp, $8, $8
	lda	$4, NR_SYSCALLS($31)
	stq	$16, SP_OFF+24($sp)
	lda	$5, sys_call_table
	lda	$27, sys_ni_syscall
	cmpult	$0, $4, $4
	ldl	$3, TI_FLAGS($8)
	stq	$17, SP_OFF+32($sp)
	s8addq	$0, $5, $5
	stq	$18, SP_OFF+40($sp)
	.cfi_rel_offset	$16, SP_OFF+24
	.cfi_rel_offset	$17, SP_OFF+32
	.cfi_rel_offset	$18, SP_OFF+40
#ifdef CONFIG_AUDITSYSCALL
	lda     $6, _TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT
	and     $3, $6, $3
#endif
	bne     $3, strace
	beq	$4, 1f
	ldq	$27, 0($5)
1:	jsr	$26, ($27), alpha_ni_syscall
	ldgp	$gp, 0($26)
	blt	$0, $syscall_error	/* the call failed */
	stq	$0, 0($sp)
	stq	$31, 72($sp)		/* a3=0 => no error */

	.align	4
	.globl	ret_from_sys_call
ret_from_sys_call:
	cmovne	$26, 0, $18		/* $18 = 0 => non-restartable */
	ldq	$0, SP_OFF($sp)
	and	$0, 8, $0
	beq	$0, ret_to_kernel
ret_to_user:
	/* Make sure need_resched and sigpending don't change between
		sampling and the rti.  */
	lda	$16, 7
	call_pal PAL_swpipl
	ldl	$17, TI_FLAGS($8)
	and	$17, _TIF_WORK_MASK, $2
	bne	$2, work_pending
restore_all:
	.cfi_remember_state
	RESTORE_ALL
	call_pal PAL_rti

ret_to_kernel:
	.cfi_restore_state
	lda	$16, 7
	call_pal PAL_swpipl
	br restore_all

	.align 3
$syscall_error:
	/*
	 * Some system calls (e.g., ptrace) can return arbitrary
	 * values which might normally be mistaken as error numbers.
	 * Those functions must zero $0 (v0) directly in the stack
	 * frame to indicate that a negative return value wasn't an
	 * error number..
	 */
	ldq	$18, 0($sp)	/* old syscall nr (zero if success) */
	beq	$18, $ret_success

	ldq	$19, 72($sp)	/* .. and this a3 */
	subq	$31, $0, $0	/* with error in v0 */
	addq	$31, 1, $1	/* set a3 for errno return */
	stq	$0, 0($sp)
	mov	$31, $26	/* tell "ret_from_sys_call" we can restart */
	stq	$1, 72($sp)	/* a3 for return */
	br	ret_from_sys_call

$ret_success:
	stq	$0, 0($sp)
	stq	$31, 72($sp)	/* a3=0 => no error */
	br	ret_from_sys_call

/*
 * Do all cleanup when returning from all interrupts and system calls.
 *
 * Arguments:
 *       $8: current.
 *      $17: TI_FLAGS.
 *      $18: The old syscall number, or zero if this is not a return
 *           from a syscall that errored and is possibly restartable.
 *      $19: The old a3 value
 */

	.align	4
	.type	work_pending, @function
work_pending:
	and	$17, _TIF_NOTIFY_RESUME | _TIF_SIGPENDING, $2
	bne	$2, $work_notifysig

$work_resched:
	/*
	 * We can get here only if we returned from syscall without SIGPENDING
	 * or got through work_notifysig already.  Either case means no syscall
	 * restarts for us, so let $18 and $19 burn.
	 */
	jsr	$26, schedule
	mov	0, $18
	br	ret_to_user

$work_notifysig:
	mov	$sp, $16
	DO_SWITCH_STACK
	jsr	$26, do_work_pending
	UNDO_SWITCH_STACK
	br	restore_all

/*
 * PTRACE syscall handler
 */

	.align	4
	.type	strace, @function
strace:
	/* set up signal stack, call syscall_trace */
	DO_SWITCH_STACK
	jsr	$26, syscall_trace_enter /* returns the syscall number */
	UNDO_SWITCH_STACK

	/* get the arguments back.. */
	ldq	$16, SP_OFF+24($sp)
	ldq	$17, SP_OFF+32($sp)
	ldq	$18, SP_OFF+40($sp)
	ldq	$19, 72($sp)
	ldq	$20, 80($sp)
	ldq	$21, 88($sp)

	/* get the system call pointer.. */
	lda	$1, NR_SYSCALLS($31)
	lda	$2, sys_call_table
	lda	$27, alpha_ni_syscall
	cmpult	$0, $1, $1
	s8addq	$0, $2, $2
	beq	$1, 1f
	ldq	$27, 0($2)
1:	jsr	$26, ($27), sys_gettimeofday
ret_from_straced:
	ldgp	$gp, 0($26)

	/* check return.. */
	blt	$0, $strace_error	/* the call failed */
	stq	$31, 72($sp)		/* a3=0 => no error */
$strace_success:
	stq	$0, 0($sp)		/* save return value */

	DO_SWITCH_STACK
	jsr	$26, syscall_trace_leave
	UNDO_SWITCH_STACK
	br	$31, ret_from_sys_call

	.align	3
$strace_error:
	ldq	$18, 0($sp)	/* old syscall nr (zero if success) */
	beq	$18, $strace_success
	ldq	$19, 72($sp)	/* .. and this a3 */

	subq	$31, $0, $0	/* with error in v0 */
	addq	$31, 1, $1	/* set a3 for errno return */
	stq	$0, 0($sp)
	stq	$1, 72($sp)	/* a3 for return */

	DO_SWITCH_STACK
	mov	$18, $9		/* save old syscall number */
	mov	$19, $10	/* save old a3 */
	jsr	$26, syscall_trace_leave
	mov	$9, $18
	mov	$10, $19
	UNDO_SWITCH_STACK

	mov	$31, $26	/* tell "ret_from_sys_call" we can restart */
	br	ret_from_sys_call
CFI_END_OSF_FRAME entSys

/*
 * Save and restore the switch stack -- aka the balance of the user context.
 */

	.align	4
	.type	do_switch_stack, @function
	.cfi_startproc simple
	.cfi_return_column 64
	.cfi_def_cfa $sp, 0
	.cfi_register 64, $1
do_switch_stack:
	lda	$sp, -SWITCH_STACK_SIZE($sp)
	.cfi_adjust_cfa_offset	SWITCH_STACK_SIZE
	stq	$9, 0($sp)
	stq	$10, 8($sp)
	stq	$11, 16($sp)
	stq	$12, 24($sp)
	stq	$13, 32($sp)
	stq	$14, 40($sp)
	stq	$15, 48($sp)
	stq	$26, 56($sp)
	stt	$f0, 64($sp)
	stt	$f1, 72($sp)
	stt	$f2, 80($sp)
	stt	$f3, 88($sp)
	stt	$f4, 96($sp)
	stt	$f5, 104($sp)
	stt	$f6, 112($sp)
	stt	$f7, 120($sp)
	stt	$f8, 128($sp)
	stt	$f9, 136($sp)
	stt	$f10, 144($sp)
	stt	$f11, 152($sp)
	stt	$f12, 160($sp)
	stt	$f13, 168($sp)
	stt	$f14, 176($sp)
	stt	$f15, 184($sp)
	stt	$f16, 192($sp)
	stt	$f17, 200($sp)
	stt	$f18, 208($sp)
	stt	$f19, 216($sp)
	stt	$f20, 224($sp)
	stt	$f21, 232($sp)
	stt	$f22, 240($sp)
	stt	$f23, 248($sp)
	stt	$f24, 256($sp)
	stt	$f25, 264($sp)
	stt	$f26, 272($sp)
	stt	$f27, 280($sp)
	mf_fpcr	$f0		# get fpcr
	stt	$f28, 288($sp)
	stt	$f29, 296($sp)
	stt	$f30, 304($sp)
	stt	$f0, 312($sp)	# save fpcr in slot of $f31
	ldt	$f0, 64($sp)	# dont let "do_switch_stack" change fp state.
	ret	$31, ($1), 1
	.cfi_endproc
	.size	do_switch_stack, .-do_switch_stack

	.align	4
	.type	undo_switch_stack, @function
	.cfi_startproc simple
	.cfi_def_cfa $sp, 0
	.cfi_register 64, $1
undo_switch_stack:
	ldq	$9, 0($sp)
	ldq	$10, 8($sp)
	ldq	$11, 16($sp)
	ldq	$12, 24($sp)
	ldq	$13, 32($sp)
	ldq	$14, 40($sp)
	ldq	$15, 48($sp)
	ldq	$26, 56($sp)
	ldt	$f30, 312($sp)	# get saved fpcr
	ldt	$f0, 64($sp)
	ldt	$f1, 72($sp)
	ldt	$f2, 80($sp)
	ldt	$f3, 88($sp)
	mt_fpcr	$f30		# install saved fpcr
	ldt	$f4, 96($sp)
	ldt	$f5, 104($sp)
	ldt	$f6, 112($sp)
	ldt	$f7, 120($sp)
	ldt	$f8, 128($sp)
	ldt	$f9, 136($sp)
	ldt	$f10, 144($sp)
	ldt	$f11, 152($sp)
	ldt	$f12, 160($sp)
	ldt	$f13, 168($sp)
	ldt	$f14, 176($sp)
	ldt	$f15, 184($sp)
	ldt	$f16, 192($sp)
	ldt	$f17, 200($sp)
	ldt	$f18, 208($sp)
	ldt	$f19, 216($sp)
	ldt	$f20, 224($sp)
	ldt	$f21, 232($sp)
	ldt	$f22, 240($sp)
	ldt	$f23, 248($sp)
	ldt	$f24, 256($sp)
	ldt	$f25, 264($sp)
	ldt	$f26, 272($sp)
	ldt	$f27, 280($sp)
	ldt	$f28, 288($sp)
	ldt	$f29, 296($sp)
	ldt	$f30, 304($sp)
	lda	$sp, SWITCH_STACK_SIZE($sp)
	ret	$31, ($1), 1
	.cfi_endproc
	.size	undo_switch_stack, .-undo_switch_stack

/*
 * The meat of the context switch code.
 */

	.align	4
	.globl	alpha_switch_to
	.type	alpha_switch_to, @function
	.cfi_startproc
alpha_switch_to:
	DO_SWITCH_STACK
	call_pal PAL_swpctx
	lda	$8, 0x3fff
	UNDO_SWITCH_STACK
	bic	$sp, $8, $8
	mov	$17, $0
	ret
	.cfi_endproc
	.size	alpha_switch_to, .-alpha_switch_to

/*
 * New processes begin life here.
 */

	.globl	ret_from_fork
	.align	4
	.ent	ret_from_fork
ret_from_fork:
	lda	$26, ret_from_sys_call
	mov	$17, $16
	jmp	$31, schedule_tail
.end ret_from_fork

/*
 * ... and new kernel threads - here
 */
	.align 4
	.globl	ret_from_kernel_thread
	.ent	ret_from_kernel_thread
ret_from_kernel_thread:
	mov	$17, $16
	jsr	$26, schedule_tail
	mov	$9, $27
	mov	$10, $16
	jsr	$26, ($9)
	br	$31, ret_to_user
.end ret_from_kernel_thread


/*
 * Special system calls.  Most of these are special in that they either
 * have to play switch_stack games or in some way use the pt_regs struct.
 */

.macro	fork_like name
	.align	4
	.globl	alpha_\name
	.ent	alpha_\name
alpha_\name:
	.prologue 0
	bsr	$1, do_switch_stack
	jsr	$26, sys_\name
	ldq	$26, 56($sp)
	lda	$sp, SWITCH_STACK_SIZE($sp)
	ret
.end	alpha_\name
.endm

fork_like fork
fork_like vfork
fork_like clone

	.align	4
	.globl	sys_sigreturn
	.ent	sys_sigreturn
sys_sigreturn:
	.prologue 0
	lda	$9, ret_from_straced
	cmpult	$26, $9, $9
	lda	$sp, -SWITCH_STACK_SIZE($sp)
	jsr	$26, do_sigreturn
	bne	$9, 1f
	jsr	$26, syscall_trace_leave
1:	br	$1, undo_switch_stack
	br	ret_from_sys_call
.end sys_sigreturn

	.align	4
	.globl	sys_rt_sigreturn
	.ent	sys_rt_sigreturn
sys_rt_sigreturn:
	.prologue 0
	lda	$9, ret_from_straced
	cmpult	$26, $9, $9
	lda	$sp, -SWITCH_STACK_SIZE($sp)
	jsr	$26, do_rt_sigreturn
	bne	$9, 1f
	jsr	$26, syscall_trace_leave
1:	br	$1, undo_switch_stack
	br	ret_from_sys_call
.end sys_rt_sigreturn

	.align	4
	.globl	alpha_ni_syscall
	.ent	alpha_ni_syscall
alpha_ni_syscall:
	.prologue 0
	/* Special because it also implements overflow handling via
	   syscall number 0.  And if you recall, zero is a special
	   trigger for "not an error".  Store large non-zero there.  */
	lda	$0, -ENOSYS
	unop
	stq	$0, 0($sp)
	ret
.end alpha_ni_syscall
