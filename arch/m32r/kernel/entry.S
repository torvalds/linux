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

CF_MASK		= 0x00000001
TF_MASK		= 0x00000100
IF_MASK		= 0x00000200
DF_MASK		= 0x00000400
NT_MASK		= 0x00004000
VM_MASK		= 0x00020000

#ifdef CONFIG_PREEMPT
#define preempt_stop(x)		CLI(x)
#else
#define preempt_stop(x)
#define resume_kernel		restore_all
#endif

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
ENTRY(resume_userspace)
	CLI(r4)				; make sure we don't miss an interrupt
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
	STI(r4)
	bl	schedule
	ldi	r4, #0
	st	r4, @(TI_PRE_COUNT, r8)
	CLI(r4)
	bra	need_resched
#endif

	; system call handler stub
ENTRY(system_call)
	SWITCH_TO_KERNEL_STACK
	SAVE_ALL
	STI(r4)				; Enable interrupt
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
	CLI(r4)				; make sure we don't miss an interrupt
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
	CLI(r4)				; make sure we don't miss an interrupt
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
	STI(r4)				; could let do_syscall_trace() call
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
	SWITCH_TO_KERNEL_STACK
	; WORKAROUND: force to clear SM bit and use the kernel stack (SPI).
#endif
	SAVE_ALL
	mv	r1, sp			; arg1(regs)
#if defined(CONFIG_CHIP_VDEC2) || defined(CONFIG_CHIP_XNUX2) \
	|| defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_M32102) \
	|| defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_M32104)

;    GET_ICU_STATUS;
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
	beqz	r0, 3f			; if (!irq_num) goto exit
#else
	beqz	r0, 1f			; if (!irq_num) goto exit
#endif	/* WORKAROUND */
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
	bra	ret_to_intr
#if defined(CONFIG_CHIP_M32700)
	/* WORKAROUND: IMASK bug M32700-TS1, TS2 chip. */
	.fillinsn
3:
	ld24	r14, #0x00070000
	seth	r0, #shigh(M32R_ICU_IMASK_ADDR)
	st	r14, @(low(M32R_ICU_IMASK_ADDR), r0)
	addi	sp, #4
	bra	ret_to_intr
#endif	/* WORKAROUND */
	;; do_IRQ
	.fillinsn
2:
	srli	r0, #2
#if defined(CONFIG_PLAT_USRV)
	add3	r2, r0, #-(M32R_IRQ_INT1)	; INT1# interrupt
	bnez	r2, 9f
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27				; ISN
	addi	r0, #(M32700UT_PLD_IRQ_BASE)
	.fillinsn
9:
#elif defined(CONFIG_PLAT_M32700UT)
	add3	r2, r0, #-(M32R_IRQ_INT1)       ; INT1# interrupt
	bnez	r2, check_int0
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	addi	r0, #(M32700UT_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int0:
	add3	r2, r0, #-(M32R_IRQ_INT0)       ; INT0# interrupt
	bnez	r2, check_int2
	; read ICU status of LAN-board
	seth	r0, #high(M32700UT_LAN_ICUISTS)
	or3	r0, r0, #low(M32700UT_LAN_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(M32700UT_LAN_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int2:
	add3	r2, r0, #-(M32R_IRQ_INT2)       ; INT2# interrupt
	bnez	r2, check_end
	; read ICU status of LCD-board
	seth	r0, #high(M32700UT_LCD_ICUISTS)
	or3	r0, r0, #low(M32700UT_LCD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(M32700UT_LCD_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_end:
#elif defined(CONFIG_PLAT_OPSPUT)
	add3	r2, r0, #-(M32R_IRQ_INT1)       ; INT1# interrupt
	bnez	r2, check_int0
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	addi	r0, #(OPSPUT_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int0:
	add3	r2, r0, #-(M32R_IRQ_INT0)       ; INT0# interrupt
	bnez	r2, check_int2
	; read ICU status of LAN-board
	seth	r0, #high(OPSPUT_LAN_ICUISTS)
	or3	r0, r0, #low(OPSPUT_LAN_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(OPSPUT_LAN_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int2:
	add3	r2, r0, #-(M32R_IRQ_INT2)       ; INT2# interrupt
	bnez	r2, check_end
	; read ICU status of LCD-board
	seth	r0, #high(OPSPUT_LCD_ICUISTS)
	or3	r0, r0, #low(OPSPUT_LCD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(OPSPUT_LCD_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_end:
#endif  /* CONFIG_PLAT_OPSPUT */
	bl	do_IRQ			; r0(irq), r1(regs)
#else  /* not CONFIG_SMP */
	srli	r0, #22			; r0(irq)
#if defined(CONFIG_PLAT_USRV)
	add3	r2, r0, #-(M32R_IRQ_INT1)	; INT1# interrupt
	bnez	r2, 1f
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27				; ISN
	addi	r0, #(M32700UT_PLD_IRQ_BASE)
	.fillinsn
1:
#elif defined(CONFIG_PLAT_M32700UT)
	add3	r2, r0, #-(M32R_IRQ_INT1)       ; INT1# interrupt
	bnez	r2, check_int0
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	addi	r0, #(M32700UT_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int0:
	add3	r2, r0, #-(M32R_IRQ_INT0)       ; INT0# interrupt
	bnez	r2, check_int2
	; read ICU status of LAN-board
	seth	r0, #high(M32700UT_LAN_ICUISTS)
	or3	r0, r0, #low(M32700UT_LAN_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(M32700UT_LAN_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int2:
	add3	r2, r0, #-(M32R_IRQ_INT2)       ; INT2# interrupt
	bnez	r2, check_end
	; read ICU status of LCD-board
	seth	r0, #high(M32700UT_LCD_ICUISTS)
	or3	r0, r0, #low(M32700UT_LCD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(M32700UT_LCD_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_end:
#elif defined(CONFIG_PLAT_OPSPUT)
	add3	r2, r0, #-(M32R_IRQ_INT1)       ; INT1# interrupt
	bnez	r2, check_int0
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	addi	r0, #(OPSPUT_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int0:
	add3	r2, r0, #-(M32R_IRQ_INT0)       ; INT0# interrupt
	bnez	r2, check_int2
	; read ICU status of LAN-board
	seth	r0, #high(OPSPUT_LAN_ICUISTS)
	or3	r0, r0, #low(OPSPUT_LAN_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(OPSPUT_LAN_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_int2:
	add3	r2, r0, #-(M32R_IRQ_INT2)       ; INT2# interrupt
	bnez	r2, check_end
	; read ICU status of LCD-board
	seth	r0, #high(OPSPUT_LCD_ICUISTS)
	or3	r0, r0, #low(OPSPUT_LCD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	add3	r0, r0, #(OPSPUT_LCD_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_end:
#elif defined(CONFIG_PLAT_M32104UT)
	add3	r2, r0, #-(M32R_IRQ_INT1)       ; INT1# interrupt
	bnez	r2, check_end
	; read ICU status register of PLD
	seth	r0, #high(PLD_ICUISTS)
	or3	r0, r0, #low(PLD_ICUISTS)
	lduh	r0, @r0
	slli	r0, #21
	srli	r0, #27                         ; ISN
	addi	r0, #(M32104UT_PLD_IRQ_BASE)
	bra	check_end
	.fillinsn
check_end:
#endif  /* CONFIG_PLAT_M32104UT */
	bl	do_IRQ
#endif  /* CONFIG_SMP */
	pop	r14
	seth	r0, #shigh(M32R_ICU_IMASK_ADDR)
	st	r14, @(low(M32R_ICU_IMASK_ADDR),r0)
#else
#error no chip configuration
#endif
ret_to_intr:
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
	.global	withdraw_debug_trap
	/* void debug_trap(void) */
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


/* Cache flushing handler */
ENTRY(cache_flushing_handler)
	.global	_flush_cache_all
	/* void _flush_cache_all(void); */
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

.data
ENTRY(sys_call_table)
	.long sys_restart_syscall	/* 0  -  old "setup()" system call*/
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open			/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink		/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod			/* 15 */
	.long sys_ni_syscall		/* lchown16 syscall holder */
	.long sys_ni_syscall		/* old break syscall holder */
	.long sys_ni_syscall		/* old stat syscall holder */
	.long sys_lseek
	.long sys_getpid		/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_ni_syscall		/* setuid16 syscall holder */
	.long sys_ni_syscall		/* getuid16 syscall holder */
	.long sys_stime			/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_ni_syscall		/* old fstat syscall holder */
	.long sys_pause
	.long sys_utime			/* 30 */
	.long sys_ni_syscall		/* old stty syscall holder */
	.long sys_cachectl		/* for M32R */ /* old gtty syscall holder */
	.long sys_access
	.long sys_ni_syscall		/* nice	syscall holder */
	.long sys_ni_syscall		/* 35  -  old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir			/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall		/* old prof syscall holder */
	.long sys_brk			/* 45 */
	.long sys_ni_syscall		/* setgid16 syscall holder */
	.long sys_getgid		/* will be unused */
	.long sys_ni_syscall		/* signal syscall holder */
	.long sys_ni_syscall		/* geteuid16  syscall holder */
	.long sys_ni_syscall		/* 50 - getegid16 syscall holder */
	.long sys_acct
	.long sys_umount		/* recycled never used phys() */
	.long sys_ni_syscall		/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl			/* 55 - will be unused */
	.long sys_ni_syscall		/* mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall		/* old ulimit syscall holder */
	.long sys_ni_syscall		/* sys_olduname */
	.long sys_umask			/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp		/* 65 */
	.long sys_setsid
	.long sys_ni_syscall		/* sigaction syscall holder */
	.long sys_ni_syscall		/* sgetmask syscall holder */
	.long sys_ni_syscall		/* ssetmask syscall holder */
	.long sys_ni_syscall		/* 70 - setreuid16 syscall holder */
	.long sys_ni_syscall		/* setregid16 syscall holder */
	.long sys_ni_syscall		/* sigsuspend syscall holder */
	.long sys_ni_syscall		/* sigpending syscall holder */
	.long sys_sethostname
	.long sys_setrlimit		/* 75 */
	.long sys_getrlimit/*will be unused*/
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_ni_syscall		/* 80 - getgroups16 syscall holder */
	.long sys_ni_syscall		/* setgroups16 syscall holder */
	.long sys_ni_syscall		/* sys_oldselect */
	.long sys_symlink
	.long sys_ni_syscall		/* old lstat syscall holder */
	.long sys_readlink		/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_ni_syscall		/* readdir syscall holder */
	.long sys_ni_syscall		/* 90 - old_mmap syscall holder */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_ni_syscall		/* 95 - fchwon16  syscall holder */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall		/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs		/* 100 */
	.long sys_ni_syscall		/* ioperm syscall holder */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer		/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall		/* old uname syscall holder */
	.long sys_ni_syscall		/* 110  -  iopl syscall holder */
	.long sys_vhangup
	.long sys_ni_syscall		/* idle syscall holder */
	.long sys_ni_syscall		/* vm86old syscall holder */
	.long sys_wait4
	.long sys_swapoff		/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_ni_syscall		/* sigreturn syscall holder */
	.long sys_clone			/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall		/* modify_ldt syscall holder */
	.long sys_adjtimex
	.long sys_mprotect		/* 125 */
	.long sys_ni_syscall		/* sigprocmask syscall holder */
	.long sys_ni_syscall		/* create_module syscall holder */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall		/* 130 - get_kernel_syms */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs			/* 135 */
	.long sys_personality
	.long sys_ni_syscall		/* afs_syscall syscall holder */
	.long sys_ni_syscall		/* setfsuid16 syscall holder */
	.long sys_ni_syscall		/* setfsgid16 syscall holder */
	.long sys_llseek		/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv			/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock			/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min	/* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_ni_syscall		/* setresuid16 syscall holder */
	.long sys_ni_syscall		/* 165 - getresuid16 syscall holder */
	.long sys_tas			/* vm86 syscall holder */
	.long sys_ni_syscall		/* query_module syscall holder */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid		/* 170 */
	.long sys_getresgid
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64		/* 180 */
	.long sys_pwrite64
	.long sys_ni_syscall		/* chown16 syscall holder */
	.long sys_getcwd
	.long sys_capget
	.long sys_capset		/* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long sys_vfork			/* 190 */
	.long sys_getrlimit
	.long sys_mmap2
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64		/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_lchown
	.long sys_getuid
	.long sys_getgid		/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups		/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid		/* 210 */
	.long sys_getresgid
	.long sys_chown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid		/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_mincore
	.long sys_madvise
	.long sys_getdents64		/* 220 */
	.long sys_fcntl64
	.long sys_ni_syscall		/* reserved for TUX */
	.long sys_ni_syscall		/* Reserved for Security */
	.long sys_gettid
	.long sys_readahead		/* 225 */
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr
	.long sys_getxattr
	.long sys_lgetxattr		/* 230 */
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr		/* 235 */
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_tkill
	.long sys_sendfile64
	.long sys_futex			/* 240 */
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_ni_syscall		/* reserved for "set_thread_area" system call */
	.long sys_ni_syscall		/* reserved for "get_thread_area" system call */
	.long sys_io_setup		/* 245 */
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit
	.long sys_io_cancel
	.long sys_fadvise64		/* 250 */
	.long sys_ni_syscall
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl		/* 255 */
	.long sys_epoll_wait
 	.long sys_remap_file_pages
 	.long sys_set_tid_address
 	.long sys_timer_create
 	.long sys_timer_settime		/* 260 */
 	.long sys_timer_gettime
 	.long sys_timer_getoverrun
 	.long sys_timer_delete
 	.long sys_clock_settime
 	.long sys_clock_gettime		/* 265 */
 	.long sys_clock_getres
 	.long sys_clock_nanosleep
	.long sys_statfs64
	.long sys_fstatfs64
	.long sys_tgkill		/* 270 */
	.long sys_utimes
 	.long sys_fadvise64_64
	.long sys_ni_syscall		/* Reserved for sys_vserver */
        .long sys_ni_syscall		/* Reserved for sys_mbind */
        .long sys_ni_syscall		/* Reserved for sys_get_mempolicy */
        .long sys_ni_syscall		/* Reserved for sys_set_mempolicy */
        .long sys_mq_open
        .long sys_mq_unlink
        .long sys_mq_timedsend
        .long sys_mq_timedreceive       /* 280 */
        .long sys_mq_notify
        .long sys_mq_getsetattr
        .long sys_ni_syscall            /* reserved for kexec */
	.long sys_waitid

syscall_table_size=(.-sys_call_table)
