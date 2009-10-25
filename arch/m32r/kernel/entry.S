/*
 *  linux/arch/m32r/kernel/entry.S
 *
 *  Copyright (c) 2001, 2002  Hirokazu Takata, Hitoshi Yamamoto, H. Kondo
 *  Copyright (c) 2003  Hitoshi Yamamoto
 *  Copyright (c) 2004  Hirokazu Takata <takata at linux-m32r.org>
 *
 *  Taken from i386 version.
 *    Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_system_call':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in fork.c:copy_thread, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 * M32R/M32Rx/M32R2
 *       @(sp)      - r4
 *       @(0x04,sp) - r5
 *       @(0x08,sp) - r6
 *       @(0x0c,sp) - *pt_regs
 *       @(0x10,sp) - r0
 *       @(0x14,sp) - r1
 *       @(0x18,sp) - r2
 *       @(0x1c,sp) - r3
 *       @(0x20,sp) - r7
 *       @(0x24,sp) - r8
 *       @(0x28,sp) - r9
 *       @(0x2c,sp) - r10
 *       @(0x30,sp) - r11
 *       @(0x34,sp) - r12
 *       @(0x38,sp) - syscall_nr
 *       @(0x3c,sp) - acc0h
 *       @(0x40,sp) - acc0l
 *       @(0x44,sp) - acc1h		; ISA_DSP_LEVEL2 only
 *       @(0x48,sp) - acc1l		; ISA_DSP_LEVEL2 only
 *       @(0x4c,sp) - psw
 *       @(0x50,sp) - bpc
 *       @(0x54,sp) - bbpsw
 *       @(0x58,sp) - bbpc
 *       @(0x5c,sp) - spu (cr3)
 *       @(0x60,sp) - fp (r13)
 *       @(0x64,sp) - lr (r14)
 *       @(0x68,sp) - spi (cr2)
 *       @(0x6c,sp) - orig_r0
 */

#include <linux/linkage.h>
#include <asm/irq.h>
#include <asm/unistd.h>
#include <asm/assembler.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/page.h>
#include <asm/m32r.h>
#include <asm/mmu_context.h>

#if !defined(CONFIG_MMU)
#define sys_madvise		sys_ni_syscall
#define sys_readahead		sys_ni_syscall
#define sys_mprotect		sys_ni_syscall
#define sys_msync		sys_ni_syscall
#define sys_mlock		sys_ni_syscall
#define sys_munlock		sys_ni_syscall
#define sys_mlockall		sys_ni_syscall
#define sys_munlockall		sys_ni_syscall
#define sys_mremap		sys_ni_syscall
#define sys_mincore		sys_ni_syscall
#define sys_remap_file_pages	sys_ni_syscall
#endif /* CONFIG_MMU */

#define R4(reg)			@reg
#define R5(reg)			@(0x04,reg)
#define R6(reg)			@(0x08,reg)
#define PTREGS(reg)		@(0x0C,reg)
#define R0(reg)			@(0x10,reg)
#define R1(reg)			@(0x14,reg)
#define R2(reg)			@(0x18,reg)
#define R3(reg)			@(0x1C,reg)
#define R7(reg)			@(0x20,reg)
#define R8(reg)			@(0x24,reg)
#define R9(reg)			@(0x28,reg)
#define R10(reg)		@(0x2C,reg)
#define R11(reg)		@(0x30,reg)
#define R12(reg)		@(0x34,reg)
#define SYSCALL_NR(reg)		@(0x38,reg)
#define ACC0H(reg)		@(0x3C,reg)
#define ACC0L(reg)		@(0x40,reg)
#define ACC1H(reg)		@(0x44,reg)
#define ACC1L(reg)		@(0x48,reg)
#define PSW(reg)		@(0x4C,reg)
#define BPC(reg)		@(0x50,reg)
#define BBPSW(reg)		@(0x54,reg)
#define BBPC(reg)		@(0x58,reg)
#define SPU(reg)		@(0x5C,reg)
#define FP(reg)			@(0x60,reg)  /* FP = R13 */
#define LR(reg)			@(0x64,reg)
#define SP(reg)			@(0x68,reg)
#define ORIG_R0(reg)		@(0x6C,reg)

#define nr_syscalls ((syscall_table_size)/4)

#ifdef CONFIG_PREEMPT
#define preempt_stop(x)		DISABLE_INTERRUPTS(x)
#else
#define preempt_stop(x)
#define resume_kernel		restore_all
#endif

/* how to get the thread information struct from ASM */
#define GET_THREAD_INFO(reg)	GET_THREAD_INFO reg
	.macro GET_THREAD_INFO reg
	ldi	\reg, #-THREAD_SIZE
	and	\reg, sp
	.endm

ENTRY(ret_from_fork)
	pop	r0
	bl	schedule_tail
	GET_THREAD_INFO(r8)
	bra	syscall_exit

/*
 * Return to user mode is not as complex as all this looks,
 * but we want the default path for a system call return to
 * go as quickly as possible which is why some of this is
 * less clear than it otherwise should be.
 */

	; userspace resumption stub bypassing syscall exit tracing
	ALIGN
ret_from_exception:
	preempt_stop(r4)
ret_from_intr:
	ld	r4, PSW(sp)
#ifdef CONFIG_ISA_M32R2
	and3	r4, r4, #0x8800		; check BSM and BPM bits
#else
	and3	r4, r4, #0x8000		; check BSM bit
#endif
	beqz	r4, resume_kernel
resume_userspace:
	DISABLE_INTERRUPTS(r4)		; make sure we don't miss an interrupt
					; setting need_resched or sigpending
					; between sampling and the iret
	GET_THREAD_INFO(r8)
	ld	r9, @(TI_FLAGS, r8)
	and3	r4, r9, #_TIF_WORK_MASK	; is there any work to be done on
					; int/exception return?
	bnez	r4, work_pending
	bra	restore_all

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	GET_THREAD_INFO(r8)
	ld	r9, @(TI_PRE_COUNT, r8)	; non-zero preempt_count ?
	bnez	r9, restore_all
need_resched:
	ld	r9, @(TI_FLAGS, r8)	; need_resched set ?
	and3	r4, r9, #_TIF_NEED_RESCHED
	beqz	r4, restore_all
	ld	r4, PSW(sp)		; interrupts off (exception path) ?
	and3	r4, r4, #0x4000
	beqz	r4, restore_all
	LDIMM	(r4, PREEMPT_ACTIVE)
	st	r4, @(TI_PRE_COUNT, r8)
	ENABLE_INTERRUPTS(r4)
	bl	schedule
	ldi	r4, #0
	st	r4, @(TI_PRE_COUNT, r8)
	DISABLE_INTERRUPTS(r4)
	bra	need_resched
#endif

	; system call handler stub
ENTRY(system_call)
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	ENABLE_INTERRUPTS(r4)		; Enable interrupt
	st	sp, PTREGS(sp)		; implicit pt_regs parameter
	cmpui	r7, #NR_syscalls
	bnc	syscall_badsys
	st	r7, SYSCALL_NR(sp)	; syscall_nr
					; system call tracing in operation
	GET_THREAD_INFO(r8)
	ld	r9, @(TI_FLAGS, r8)
	and3	r4, r9, #_TIF_SYSCALL_TRACE
	bnez	r4, syscall_trace_entry
syscall_call:
	slli	r7, #2			; table jump for the system call
	LDIMM	(r4, sys_call_table)
	add	r7, r4
	ld	r7, @r7
	jl	r7			; execute system call
	st	r0, R0(sp)		; save the return value
syscall_exit:
	DISABLE_INTERRUPTS(r4)		; make sure we don't miss an interrupt
					; setting need_resched or sigpending
					; between sampling and the iret
	ld	r9, @(TI_FLAGS, r8)
	and3	r4, r9, #_TIF_ALLWORK_MASK	; current->work
	bnez	r4, syscall_exit_work
restore_all:
	RESTORE_ALL

	# perform work that needs to be done immediately before resumption
	# r9 : flags
	ALIGN
work_pending:
	and3	r4, r9, #_TIF_NEED_RESCHED
	beqz	r4, work_notifysig
work_resched:
	bl	schedule
	DISABLE_INTERRUPTS(r4)		; make sure we don't miss an interrupt
					; setting need_resched or sigpending
					; between sampling and the iret
	ld	r9, @(TI_FLAGS, r8)
	and3	r4, r9, #_TIF_WORK_MASK	; is there any work to be done other
					; than syscall tracing?
	beqz	r4, restore_all
	and3	r4, r4, #_TIF_NEED_RESCHED
	bnez	r4, work_resched

work_notifysig:				; deal with pending signals and
					; notify-resume requests
	mv	r0, sp			; arg1 : struct pt_regs *regs
	ldi	r1, #0			; arg2 : sigset_t *oldset
	mv	r2, r9			; arg3 : __u32 thread_info_flags
	bl	do_notify_resume
	bra	restore_all

	; perform syscall exit tracing
	ALIGN
syscall_trace_entry:
	ldi	r4, #-ENOSYS
	st	r4, R0(sp)
	bl	do_syscall_trace
	ld	r0, ORIG_R0(sp)
	ld	r1, R1(sp)
	ld	r2, R2(sp)
	ld	r3, R3(sp)
	ld	r4, R4(sp)
	ld	r5, R5(sp)
	ld	r6, R6(sp)
	ld	r7, SYSCALL_NR(sp)
	cmpui	r7, #NR_syscalls
	bc	syscall_call
	bra	syscall_exit

	; perform syscall exit tracing
	ALIGN
syscall_exit_work:
	ld	r9, @(TI_FLAGS, r8)
	and3	r4, r9, #_TIF_SYSCALL_TRACE
	beqz	r4, work_pending
	ENABLE_INTERRUPTS(r4)		; could let do_syscall_trace() call
					; schedule() instead
	bl	do_syscall_trace
	bra	resume_userspace

	ALIGN
syscall_fault:
	SAVE_ALL
	GET_THREAD_INFO(r8)
	ldi	r4, #-EFAULT
	st	r4, R0(sp)
	bra	resume_userspace

	ALIGN
syscall_badsys:
	ldi	r4, #-ENOSYS
	st	r4, R0(sp)
	bra	resume_userspace

	.global	eit_vector

	.equ ei_vec_table, eit_vector + 0x0200

/*
 * EI handler routine
 */
ENTRY(ei_handler)
#if defined(CONFIG_CHIP_M32700)
	; WORKAROUND: force to clear SM bit and use the kernel stack (SPI).
	SWITCH_TO_KERNEL_STACK
#endif
	SAVE_ALL
	mv	r1, sp			; arg1(regs)
	; get ICU status
	seth	r0, #shigh(M32R_ICU_ISTS_ADDR)
	ld	r0, @(low(M32R_ICU_ISTS_ADDR),r0)
	push	r0
#if defined(CONFIG_SMP)
	/*
	 * If IRQ == 0      --> Nothing to do,  Not write IMASK
	 * If IRQ == IPI    --> Do IPI handler, Not write IMASK
	 * If IRQ != 0, IPI --> Do do_IRQ(),    Write IMASK
	 */
	slli	r0, #4
	srli	r0, #24			; r0(irq_num<<2)
	;; IRQ exist check
#if defined(CONFIG_CHIP_M32700)
	/* WORKAROUND: IMASK bug M32700-TS1, TS2 chip. */
	bnez	r0, 0f
	ld24	r14, #0x00070000
	seth	r0, #shigh(M32R_ICU_IMASK_ADDR)
	st	r14, @(low(M32R_ICU_IMASK_ADDR),r0)
	bra	1f
	.fillinsn
0:
#endif /* CONFIG_CHIP_M32700 */
	beqz	r0, 1f			; if (!irq_num) goto exit
	;; IPI check
	cmpi	r0, #(M32R_IRQ_IPI0<<2)	; ISN < IPI0 check
	bc	2f
	cmpi	r0, #((M32R_IRQ_IPI7+1)<<2)	; ISN > IPI7 check
	bnc	2f
	LDIMM	(r2, ei_vec_table)
	add	r2, r0
	ld	r2, @r2
	beqz	r2, 1f			; if (no IPI handler) goto exit
	mv	r0, r1			; arg0(regs)
	jl	r2
	.fillinsn
1:
	addi	sp, #4
	bra	restore_all
	.fillinsn
2:
	srli	r0, #2
#else /* not CONFIG_SMP */
	srli	r0, #22			; r0(irq)
#endif /* not CONFIG_SMP */

#if defined(CONFIG_PLAT_HAS_INT1ICU)
	add3	r2, r0, #-(M32R_IRQ_INT1)	; INT1# interrupt
	bnez	r2, 3f
	seth	r0, #shigh(M32R_INT1ICU_ISTS)
	lduh	r0, @(low(M32R_INT1ICU_ISTS),r0)	; bit10-6 : ISN
	slli	r0, #21
	srli	r0, #27				; ISN
	addi	r0, #(M32R_INT1ICU_IRQ_BASE)
	bra	check_end
	.fillinsn
3:
#endif /* CONFIG_PLAT_HAS_INT1ICU */
#if defined(CONFIG_PLAT_HAS_INT0ICU)
	add3	r2, r0, #-(M32R_IRQ_INT0)	; INT0# interrupt
	bnez	r2, 4f
	seth	r0, #shigh(M32R_INT0ICU_ISTS)
	lduh	r0, @(low(M32R_INT0ICU_ISTS),r0)	; bit10-6 : ISN
	slli	r0, #21
	srli	r0, #27				; ISN
	add3	r0, r0, #(M32R_INT0ICU_IRQ_BASE)
	bra	check_end
	.fillinsn
4:
#endif /* CONFIG_PLAT_HAS_INT0ICU */
#if defined(CONFIG_PLAT_HAS_INT2ICU)
	add3	r2, r0, #-(M32R_IRQ_INT2)	; INT2# interrupt
	bnez	r2, 5f
	seth	r0, #shigh(M32R_INT2ICU_ISTS)
	lduh	r0, @(low(M32R_INT2ICU_ISTS),r0)	; bit10-6 : ISN
	slli	r0, #21
	srli	r0, #27				; ISN
	add3	r0, r0, #(M32R_INT2ICU_IRQ_BASE)
	; bra	check_end
	.fillinsn
5:
#endif /* CONFIG_PLAT_HAS_INT2ICU */

check_end:
	bl	do_IRQ
	pop	r14
	seth	r0, #shigh(M32R_ICU_IMASK_ADDR)
	st	r14, @(low(M32R_ICU_IMASK_ADDR),r0)
	bra  ret_from_intr

/*
 * Default EIT handler
 */
	ALIGN
int_msg:
	.asciz  "Unknown interrupt\n"
	.byte	0

ENTRY(default_eit_handler)
	push	r0
	mvfc	r0, psw
	push	r1
	push	r2
	push	r3
	push	r0
	LDIMM	(r0, __KERNEL_DS)
	mv	r0, r1
	mv	r0, r2
	LDIMM	(r0, int_msg)
	bl	printk
	pop	r0
	pop	r3
	pop	r2
	pop	r1
	mvtc	r0, psw
	pop	r0
infinit:
	bra	infinit

#ifdef CONFIG_MMU
/*
 * Access Exception handler
 */
ENTRY(ace_handler)
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL

	seth	r2, #shigh(MMU_REG_BASE)	/* Check status register */
	ld	r4, @(low(MESTS_offset),r2)
	st	r4, @(low(MESTS_offset),r2)
	srl3	r1, r4, #4
#ifdef CONFIG_CHIP_M32700
	and3	r1, r1, #0x0000ffff
	; WORKAROUND: ignore TME bit for the M32700(TS1).
#endif /* CONFIG_CHIP_M32700 */
	beqz	r1, inst
oprand:
	ld	r2, @(low(MDEVA_offset),r2)	; set address
	srli	r1, #1
	bra	1f
inst:
	and3	r1, r4, #2
	srli	r1, #1
	or3	r1, r1, #8
	mvfc	r2, bpc				; set address
	.fillinsn
1:
	mvfc	r3, psw
	mv	r0, sp
	and3	r3, r3, 0x800
	srli	r3, #9
	or	r1, r3
	/*
	 * do_page_fault():
	 *    r0 : struct pt_regs *regs
	 *    r1 : unsigned long error-code
	 *    r2 : unsigned long address
	 * error-code:
	 *    +------+------+------+------+
	 *    | bit3 | bit2 | bit1 | bit0 |
	 *    +------+------+------+------+
	 *    bit 3 == 0:means data,          1:means instruction
	 *    bit 2 == 0:means kernel,        1:means user-mode
	 *    bit 1 == 0:means read,          1:means write
	 *    bit 0 == 0:means no page found  1:means protection fault
	 *
	 */
	bl	do_page_fault
	bra	ret_from_intr
#endif  /* CONFIG_MMU */


ENTRY(alignment_check)
	/* void alignment_check(int error_code) */
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	ldi	r1, #0x30			; error_code
	mv	r0, sp				; pt_regs
	bl	do_alignment_check
error_code:
	bra	ret_from_exception

ENTRY(rie_handler)
	/* void rie_handler(int error_code) */
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	ldi	r1, #0x20			; error_code
	mv	r0, sp				; pt_regs
	bl	do_rie_handler
	bra	error_code

ENTRY(pie_handler)
	/* void pie_handler(int error_code) */
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	ldi	r1, #0				; error_code ; FIXME
	mv	r0, sp				; pt_regs
	bl	do_pie_handler
	bra	error_code

ENTRY(debug_trap)
	/* void debug_trap(void) */
	.global	withdraw_debug_trap
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	mv	r0, sp				; pt_regs
	bl	withdraw_debug_trap
	ldi	r1, #0				; error_code
	mv	r0, sp				; pt_regs
	bl	do_debug_trap
	bra	error_code

ENTRY(ill_trap)
	/* void ill_trap(void) */
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	ldi	r1, #0				; error_code ; FIXME
	mv	r0, sp				; pt_regs
	bl	do_ill_trap
	bra	error_code

ENTRY(cache_flushing_handler)
	/* void _flush_cache_all(void); */
	.global	_flush_cache_all
	SWITCH_TO_KERNEL_STACK
	push	r0
	push	r1
	push	r2
	push	r3
	push	r4
	push	r5
	push	r6
	push	r7
	push	lr
	bl	_flush_cache_all
	pop	lr
	pop	r7
	pop	r6
	pop	r5
	pop	r4
	pop	r3
	pop	r2
	pop	r1
	pop	r0
	rte

	.section .rodata,"a"
#include "syscall_table.S"

syscall_table_size=(.-sys_call_table)
