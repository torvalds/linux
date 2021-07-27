#include <asm/asm-offsets.h>
#include <asm/bug.h>
#ifdef CONFIG_PPC_BOOK3S
#include <asm/exception-64s.h>
#else
#include <asm/exception-64e.h>
#endif
#include <asm/feature-fixups.h>
#include <asm/head-64.h>
#include <asm/hw_irq.h>
#include <asm/kup.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/ptrace.h>
#include <asm/tm.h>

	.section	".toc","aw"
SYS_CALL_TABLE:
	.tc sys_call_table[TC],sys_call_table

#ifdef CONFIG_COMPAT
COMPAT_SYS_CALL_TABLE:
	.tc compat_sys_call_table[TC],compat_sys_call_table
#endif
	.previous

	.align 7

.macro DEBUG_SRR_VALID srr
#ifdef CONFIG_PPC_RFI_SRR_DEBUG
	.ifc \srr,srr
	mfspr	r11,SPRN_SRR0
	ld	r12,_NIP(r1)
100:	tdne	r11,r12
	EMIT_BUG_ENTRY 100b,__FILE__,__LINE__,(BUGFLAG_WARNING | BUGFLAG_ONCE)
	mfspr	r11,SPRN_SRR1
	ld	r12,_MSR(r1)
100:	tdne	r11,r12
	EMIT_BUG_ENTRY 100b,__FILE__,__LINE__,(BUGFLAG_WARNING | BUGFLAG_ONCE)
	.else
	mfspr	r11,SPRN_HSRR0
	ld	r12,_NIP(r1)
100:	tdne	r11,r12
	EMIT_BUG_ENTRY 100b,__FILE__,__LINE__,(BUGFLAG_WARNING | BUGFLAG_ONCE)
	mfspr	r11,SPRN_HSRR1
	ld	r12,_MSR(r1)
100:	tdne	r11,r12
	EMIT_BUG_ENTRY 100b,__FILE__,__LINE__,(BUGFLAG_WARNING | BUGFLAG_ONCE)
	.endif
#endif
.endm

#ifdef CONFIG_PPC_BOOK3S
.macro system_call_vectored name trapnr
	.globl system_call_vectored_\name
system_call_vectored_\name:
_ASM_NOKPROBE_SYMBOL(system_call_vectored_\name)
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	extrdi.	r10, r12, 1, (63-MSR_TS_T_LG) /* transaction active? */
	bne	tabort_syscall
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	SCV_INTERRUPT_TO_KERNEL
	mr	r10,r1
	ld	r1,PACAKSAVE(r13)
	std	r10,0(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	std	r0,GPR0(r1)
	std	r10,GPR1(r1)
	std	r2,GPR2(r1)
	ld	r2,PACATOC(r13)
	mfcr	r12
	li	r11,0
	/* Can we avoid saving r3-r8 in common case? */
	std	r3,GPR3(r1)
	std	r4,GPR4(r1)
	std	r5,GPR5(r1)
	std	r6,GPR6(r1)
	std	r7,GPR7(r1)
	std	r8,GPR8(r1)
	/* Zero r9-r12, this should only be required when restoring all GPRs */
	std	r11,GPR9(r1)
	std	r11,GPR10(r1)
	std	r11,GPR11(r1)
	std	r11,GPR12(r1)
	std	r9,GPR13(r1)
	SAVE_NVGPRS(r1)
	std	r11,_XER(r1)
	std	r11,_LINK(r1)
	std	r11,_CTR(r1)

	li	r11,\trapnr
	std	r11,_TRAP(r1)
	std	r12,_CCR(r1)
	addi	r10,r1,STACK_FRAME_OVERHEAD
	ld	r11,exception_marker@toc(r2)
	std	r11,-16(r10)		/* "regshere" marker */

BEGIN_FTR_SECTION
	HMT_MEDIUM
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	/*
	 * scv enters with MSR[EE]=1 and is immediately considered soft-masked.
	 * The entry vector already sets PACAIRQSOFTMASK to IRQS_ALL_DISABLED,
	 * and interrupts may be masked and pending already.
	 * system_call_exception() will call trace_hardirqs_off() which means
	 * interrupts could already have been blocked before trace_hardirqs_off,
	 * but this is the best we can do.
	 */

	/* Calling convention has r9 = orig r0, r10 = regs */
	mr	r9,r0
	bl	system_call_exception

.Lsyscall_vectored_\name\()_exit:
	addi	r4,r1,STACK_FRAME_OVERHEAD
	li	r5,1 /* scv */
	bl	syscall_exit_prepare
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
.Lsyscall_vectored_\name\()_rst_start:
	lbz	r11,PACAIRQHAPPENED(r13)
	andi.	r11,r11,(~PACA_IRQ_HARD_DIS)@l
	bne-	syscall_vectored_\name\()_restart
	li	r11,IRQS_ENABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	li	r11,0
	stb	r11,PACAIRQHAPPENED(r13) # clear out possible HARD_DIS

	ld	r2,_CCR(r1)
	ld	r4,_NIP(r1)
	ld	r5,_MSR(r1)

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1			/* to clear the reservation */
END_FTR_SECTION_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

BEGIN_FTR_SECTION
	HMT_MEDIUM_LOW
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	cmpdi	r3,0
	bne	.Lsyscall_vectored_\name\()_restore_regs

	/* rfscv returns with LR->NIA and CTR->MSR */
	mtlr	r4
	mtctr	r5

	/* Could zero these as per ABI, but we may consider a stricter ABI
	 * which preserves these if libc implementations can benefit, so
	 * restore them for now until further measurement is done. */
	ld	r0,GPR0(r1)
	ld	r4,GPR4(r1)
	ld	r5,GPR5(r1)
	ld	r6,GPR6(r1)
	ld	r7,GPR7(r1)
	ld	r8,GPR8(r1)
	/* Zero volatile regs that may contain sensitive kernel data */
	li	r9,0
	li	r10,0
	li	r11,0
	li	r12,0
	mtspr	SPRN_XER,r0

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * The value of AMR only matters while we're in the kernel.
	 */
	mtcr	r2
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r13,GPR13(r1)
	ld	r1,GPR1(r1)
	RFSCV_TO_USER
	b	.	/* prevent speculative execution */

.Lsyscall_vectored_\name\()_restore_regs:
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r5

	ld	r3,_CTR(r1)
	ld	r4,_LINK(r1)
	ld	r5,_XER(r1)

	REST_NVGPRS(r1)
	ld	r0,GPR0(r1)
	mtcr	r2
	mtctr	r3
	mtlr	r4
	mtspr	SPRN_XER,r5
	REST_10GPRS(2, r1)
	REST_2GPRS(12, r1)
	ld	r1,GPR1(r1)
	RFI_TO_USER
.Lsyscall_vectored_\name\()_rst_end:

syscall_vectored_\name\()_restart:
_ASM_NOKPROBE_SYMBOL(syscall_vectored_\name\()_restart)
	GET_PACA(r13)
	ld	r1,PACA_EXIT_SAVE_R1(r13)
	ld	r2,PACATOC(r13)
	ld	r3,RESULT(r1)
	addi	r4,r1,STACK_FRAME_OVERHEAD
	li	r11,IRQS_ALL_DISABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	bl	syscall_exit_restart
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
	b	.Lsyscall_vectored_\name\()_rst_start
1:

SOFT_MASK_TABLE(.Lsyscall_vectored_\name\()_rst_start, 1b)
RESTART_TABLE(.Lsyscall_vectored_\name\()_rst_start, .Lsyscall_vectored_\name\()_rst_end, syscall_vectored_\name\()_restart)

.endm

system_call_vectored common 0x3000

/*
 * We instantiate another entry copy for the SIGILL variant, with TRAP=0x7ff0
 * which is tested by system_call_exception when r0 is -1 (as set by vector
 * entry code).
 */
system_call_vectored sigill 0x7ff0


/*
 * Entered via kernel return set up by kernel/sstep.c, must match entry regs
 */
	.globl system_call_vectored_emulate
system_call_vectored_emulate:
_ASM_NOKPROBE_SYMBOL(system_call_vectored_emulate)
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	b	system_call_vectored_common
#endif /* CONFIG_PPC_BOOK3S */

	.balign IFETCH_ALIGN_BYTES
	.globl system_call_common_real
system_call_common_real:
_ASM_NOKPROBE_SYMBOL(system_call_common_real)
	ld	r10,PACAKMSR(r13)	/* get MSR value for kernel */
	mtmsrd	r10

	.balign IFETCH_ALIGN_BYTES
	.globl system_call_common
system_call_common:
_ASM_NOKPROBE_SYMBOL(system_call_common)
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	extrdi.	r10, r12, 1, (63-MSR_TS_T_LG) /* transaction active? */
	bne	tabort_syscall
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	mr	r10,r1
	ld	r1,PACAKSAVE(r13)
	std	r10,0(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	std	r0,GPR0(r1)
	std	r10,GPR1(r1)
	std	r2,GPR2(r1)
#ifdef CONFIG_PPC_FSL_BOOK3E
START_BTB_FLUSH_SECTION
	BTB_FLUSH(r10)
END_BTB_FLUSH_SECTION
#endif
	ld	r2,PACATOC(r13)
	mfcr	r12
	li	r11,0
	/* Can we avoid saving r3-r8 in common case? */
	std	r3,GPR3(r1)
	std	r4,GPR4(r1)
	std	r5,GPR5(r1)
	std	r6,GPR6(r1)
	std	r7,GPR7(r1)
	std	r8,GPR8(r1)
	/* Zero r9-r12, this should only be required when restoring all GPRs */
	std	r11,GPR9(r1)
	std	r11,GPR10(r1)
	std	r11,GPR11(r1)
	std	r11,GPR12(r1)
	std	r9,GPR13(r1)
	SAVE_NVGPRS(r1)
	std	r11,_XER(r1)
	std	r11,_CTR(r1)
	mflr	r10

	/*
	 * This clears CR0.SO (bit 28), which is the error indication on
	 * return from this system call.
	 */
	rldimi	r12,r11,28,(63-28)
	li	r11,0xc00
	std	r10,_LINK(r1)
	std	r11,_TRAP(r1)
	std	r12,_CCR(r1)
	addi	r10,r1,STACK_FRAME_OVERHEAD
	ld	r11,exception_marker@toc(r2)
	std	r11,-16(r10)		/* "regshere" marker */

#ifdef CONFIG_PPC_BOOK3S
	li	r11,1
	stb	r11,PACASRR_VALID(r13)
#endif

	/*
	 * We always enter kernel from userspace with irq soft-mask enabled and
	 * nothing pending. system_call_exception() will call
	 * trace_hardirqs_off().
	 */
	li	r11,IRQS_ALL_DISABLED
	stb	r11,PACAIRQSOFTMASK(r13)
#ifdef CONFIG_PPC_BOOK3S
	li	r12,-1 /* Set MSR_EE and MSR_RI */
	mtmsrd	r12,1
#else
	wrteei	1
#endif

	/* Calling convention has r9 = orig r0, r10 = regs */
	mr	r9,r0
	bl	system_call_exception

.Lsyscall_exit:
	addi	r4,r1,STACK_FRAME_OVERHEAD
	li	r5,0 /* !scv */
	bl	syscall_exit_prepare
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
#ifdef CONFIG_PPC_BOOK3S
.Lsyscall_rst_start:
	lbz	r11,PACAIRQHAPPENED(r13)
	andi.	r11,r11,(~PACA_IRQ_HARD_DIS)@l
	bne-	syscall_restart
#endif
	li	r11,IRQS_ENABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	li	r11,0
	stb	r11,PACAIRQHAPPENED(r13) # clear out possible HARD_DIS

	ld	r2,_CCR(r1)
	ld	r6,_LINK(r1)
	mtlr	r6

#ifdef CONFIG_PPC_BOOK3S
	lbz	r4,PACASRR_VALID(r13)
	cmpdi	r4,0
	bne	1f
	li	r4,0
	stb	r4,PACASRR_VALID(r13)
#endif
	ld	r4,_NIP(r1)
	ld	r5,_MSR(r1)
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r5
1:
	DEBUG_SRR_VALID srr

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1			/* to clear the reservation */
END_FTR_SECTION_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	cmpdi	r3,0
	bne	.Lsyscall_restore_regs
	/* Zero volatile regs that may contain sensitive kernel data */
	li	r0,0
	li	r4,0
	li	r5,0
	li	r6,0
	li	r7,0
	li	r8,0
	li	r9,0
	li	r10,0
	li	r11,0
	li	r12,0
	mtctr	r0
	mtspr	SPRN_XER,r0
.Lsyscall_restore_regs_cont:

BEGIN_FTR_SECTION
	HMT_MEDIUM_LOW
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * The value of AMR only matters while we're in the kernel.
	 */
	mtcr	r2
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r13,GPR13(r1)
	ld	r1,GPR1(r1)
	RFI_TO_USER
	b	.	/* prevent speculative execution */

.Lsyscall_restore_regs:
	ld	r3,_CTR(r1)
	ld	r4,_XER(r1)
	REST_NVGPRS(r1)
	mtctr	r3
	mtspr	SPRN_XER,r4
	ld	r0,GPR0(r1)
	REST_8GPRS(4, r1)
	ld	r12,GPR12(r1)
	b	.Lsyscall_restore_regs_cont
.Lsyscall_rst_end:

#ifdef CONFIG_PPC_BOOK3S
syscall_restart:
_ASM_NOKPROBE_SYMBOL(syscall_restart)
	GET_PACA(r13)
	ld	r1,PACA_EXIT_SAVE_R1(r13)
	ld	r2,PACATOC(r13)
	ld	r3,RESULT(r1)
	addi	r4,r1,STACK_FRAME_OVERHEAD
	li	r11,IRQS_ALL_DISABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	bl	syscall_exit_restart
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
	b	.Lsyscall_rst_start
1:

SOFT_MASK_TABLE(.Lsyscall_rst_start, 1b)
RESTART_TABLE(.Lsyscall_rst_start, .Lsyscall_rst_end, syscall_restart)
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
tabort_syscall:
_ASM_NOKPROBE_SYMBOL(tabort_syscall)
	/* Firstly we need to enable TM in the kernel */
	mfmsr	r10
	li	r9, 1
	rldimi	r10, r9, MSR_TM_LG, 63-MSR_TM_LG
	mtmsrd	r10, 0

	/* tabort, this dooms the transaction, nothing else */
	li	r9, (TM_CAUSE_SYSCALL|TM_CAUSE_PERSISTENT)
	TABORT(R9)

	/*
	 * Return directly to userspace. We have corrupted user register state,
	 * but userspace will never see that register state. Execution will
	 * resume after the tbegin of the aborted transaction with the
	 * checkpointed register state.
	 */
	li	r9, MSR_RI
	andc	r10, r10, r9
	mtmsrd	r10, 1
	mtspr	SPRN_SRR0, r11
	mtspr	SPRN_SRR1, r12
	RFI_TO_USER
	b	.	/* prevent speculative execution */
#endif

	/*
	 * If MSR EE/RI was never enabled, IRQs not reconciled, NVGPRs not
	 * touched, no exit work created, then this can be used.
	 */
	.balign IFETCH_ALIGN_BYTES
	.globl fast_interrupt_return_srr
fast_interrupt_return_srr:
_ASM_NOKPROBE_SYMBOL(fast_interrupt_return_srr)
	kuap_check_amr r3, r4
	ld	r5,_MSR(r1)
	andi.	r0,r5,MSR_PR
#ifdef CONFIG_PPC_BOOK3S
	beq	1f
	kuap_user_restore r3, r4
	b	.Lfast_user_interrupt_return_srr
1:	kuap_kernel_restore r3, r4
	andi.	r0,r5,MSR_RI
	li	r3,0 /* 0 return value, no EMULATE_STACK_STORE */
	bne+	.Lfast_kernel_interrupt_return_srr
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	. /* should not get here */
#else
	bne	.Lfast_user_interrupt_return_srr
	b	.Lfast_kernel_interrupt_return_srr
#endif

.macro interrupt_return_macro srr
	.balign IFETCH_ALIGN_BYTES
	.globl interrupt_return_\srr
interrupt_return_\srr\():
_ASM_NOKPROBE_SYMBOL(interrupt_return_\srr\())
	ld	r4,_MSR(r1)
	andi.	r0,r4,MSR_PR
	beq	interrupt_return_\srr\()_kernel
interrupt_return_\srr\()_user: /* make backtraces match the _kernel variant */
_ASM_NOKPROBE_SYMBOL(interrupt_return_\srr\()_user)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	interrupt_exit_user_prepare
	cmpdi	r3,0
	bne-	.Lrestore_nvgprs_\srr
.Lrestore_nvgprs_\srr\()_cont:
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
#ifdef CONFIG_PPC_BOOK3S
.Linterrupt_return_\srr\()_user_rst_start:
	lbz	r11,PACAIRQHAPPENED(r13)
	andi.	r11,r11,(~PACA_IRQ_HARD_DIS)@l
	bne-	interrupt_return_\srr\()_user_restart
#endif
	li	r11,IRQS_ENABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	li	r11,0
	stb	r11,PACAIRQHAPPENED(r13) # clear out possible HARD_DIS

.Lfast_user_interrupt_return_\srr\():
#ifdef CONFIG_PPC_BOOK3S
	.ifc \srr,srr
	lbz	r4,PACASRR_VALID(r13)
	.else
	lbz	r4,PACAHSRR_VALID(r13)
	.endif
	cmpdi	r4,0
	li	r4,0
	bne	1f
#endif
	ld	r11,_NIP(r1)
	ld	r12,_MSR(r1)
	.ifc \srr,srr
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
1:
#ifdef CONFIG_PPC_BOOK3S
	stb	r4,PACASRR_VALID(r13)
#endif
	.else
	mtspr	SPRN_HSRR0,r11
	mtspr	SPRN_HSRR1,r12
1:
#ifdef CONFIG_PPC_BOOK3S
	stb	r4,PACAHSRR_VALID(r13)
#endif
	.endif
	DEBUG_SRR_VALID \srr

#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
	lbz	r4,PACAIRQSOFTMASK(r13)
	tdnei	r4,IRQS_ENABLED
#endif

BEGIN_FTR_SECTION
	ld	r10,_PPR(r1)
	mtspr	SPRN_PPR,r10
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1		/* to clear the reservation */
FTR_SECTION_ELSE
	ldarx	r0,0,r1
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	ld	r3,_CCR(r1)
	ld	r4,_LINK(r1)
	ld	r5,_CTR(r1)
	ld	r6,_XER(r1)
	li	r0,0

	REST_4GPRS(7, r1)
	REST_2GPRS(11, r1)
	REST_GPR(13, r1)

	mtcr	r3
	mtlr	r4
	mtctr	r5
	mtspr	SPRN_XER,r6

	REST_4GPRS(2, r1)
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	.ifc \srr,srr
	RFI_TO_USER
	.else
	HRFI_TO_USER
	.endif
	b	.	/* prevent speculative execution */
.Linterrupt_return_\srr\()_user_rst_end:

.Lrestore_nvgprs_\srr\():
	REST_NVGPRS(r1)
	b	.Lrestore_nvgprs_\srr\()_cont

#ifdef CONFIG_PPC_BOOK3S
interrupt_return_\srr\()_user_restart:
_ASM_NOKPROBE_SYMBOL(interrupt_return_\srr\()_user_restart)
	GET_PACA(r13)
	ld	r1,PACA_EXIT_SAVE_R1(r13)
	ld	r2,PACATOC(r13)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r11,IRQS_ALL_DISABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	bl	interrupt_exit_user_restart
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
	b	.Linterrupt_return_\srr\()_user_rst_start
1:

SOFT_MASK_TABLE(.Linterrupt_return_\srr\()_user_rst_start, 1b)
RESTART_TABLE(.Linterrupt_return_\srr\()_user_rst_start, .Linterrupt_return_\srr\()_user_rst_end, interrupt_return_\srr\()_user_restart)
#endif

	.balign IFETCH_ALIGN_BYTES
interrupt_return_\srr\()_kernel:
_ASM_NOKPROBE_SYMBOL(interrupt_return_\srr\()_kernel)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	interrupt_exit_kernel_prepare

	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
.Linterrupt_return_\srr\()_kernel_rst_start:
	ld	r11,SOFTE(r1)
	cmpwi	r11,IRQS_ENABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	bne	1f
#ifdef CONFIG_PPC_BOOK3S
	lbz	r11,PACAIRQHAPPENED(r13)
	andi.	r11,r11,(~PACA_IRQ_HARD_DIS)@l
	bne-	interrupt_return_\srr\()_kernel_restart
#endif
	li	r11,0
	stb	r11,PACAIRQHAPPENED(r13) # clear out possible HARD_DIS
1:

.Lfast_kernel_interrupt_return_\srr\():
	cmpdi	cr1,r3,0
#ifdef CONFIG_PPC_BOOK3S
	.ifc \srr,srr
	lbz	r4,PACASRR_VALID(r13)
	.else
	lbz	r4,PACAHSRR_VALID(r13)
	.endif
	cmpdi	r4,0
	li	r4,0
	bne	1f
#endif
	ld	r11,_NIP(r1)
	ld	r12,_MSR(r1)
	.ifc \srr,srr
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
1:
#ifdef CONFIG_PPC_BOOK3S
	stb	r4,PACASRR_VALID(r13)
#endif
	.else
	mtspr	SPRN_HSRR0,r11
	mtspr	SPRN_HSRR1,r12
1:
#ifdef CONFIG_PPC_BOOK3S
	stb	r4,PACAHSRR_VALID(r13)
#endif
	.endif
	DEBUG_SRR_VALID \srr

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1		/* to clear the reservation */
FTR_SECTION_ELSE
	ldarx	r0,0,r1
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	ld	r3,_LINK(r1)
	ld	r4,_CTR(r1)
	ld	r5,_XER(r1)
	ld	r6,_CCR(r1)
	li	r0,0

	REST_4GPRS(7, r1)
	REST_2GPRS(11, r1)

	mtlr	r3
	mtctr	r4
	mtspr	SPRN_XER,r5

	/*
	 * Leaving a stale exception_marker on the stack can confuse
	 * the reliable stack unwinder later on. Clear it.
	 */
	std	r0,STACK_FRAME_OVERHEAD-16(r1)

	REST_4GPRS(2, r1)

	bne-	cr1,1f /* emulate stack store */
	mtcr	r6
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	.ifc \srr,srr
	RFI_TO_KERNEL
	.else
	HRFI_TO_KERNEL
	.endif
	b	.	/* prevent speculative execution */

1:	/*
	 * Emulate stack store with update. New r1 value was already calculated
	 * and updated in our interrupt regs by emulate_loadstore, but we can't
	 * store the previous value of r1 to the stack before re-loading our
	 * registers from it, otherwise they could be clobbered.  Use
	 * PACA_EXGEN as temporary storage to hold the store data, as
	 * interrupts are disabled here so it won't be clobbered.
	 */
	mtcr	r6
	std	r9,PACA_EXGEN+0(r13)
	addi	r9,r1,INT_FRAME_SIZE /* get original r1 */
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	std	r9,0(r1) /* perform store component of stdu */
	ld	r9,PACA_EXGEN+0(r13)

	.ifc \srr,srr
	RFI_TO_KERNEL
	.else
	HRFI_TO_KERNEL
	.endif
	b	.	/* prevent speculative execution */
.Linterrupt_return_\srr\()_kernel_rst_end:

#ifdef CONFIG_PPC_BOOK3S
interrupt_return_\srr\()_kernel_restart:
_ASM_NOKPROBE_SYMBOL(interrupt_return_\srr\()_kernel_restart)
	GET_PACA(r13)
	ld	r1,PACA_EXIT_SAVE_R1(r13)
	ld	r2,PACATOC(r13)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r11,IRQS_ALL_DISABLED
	stb	r11,PACAIRQSOFTMASK(r13)
	bl	interrupt_exit_kernel_restart
	std	r1,PACA_EXIT_SAVE_R1(r13) /* save r1 for restart */
	b	.Linterrupt_return_\srr\()_kernel_rst_start
1:

SOFT_MASK_TABLE(.Linterrupt_return_\srr\()_kernel_rst_start, 1b)
RESTART_TABLE(.Linterrupt_return_\srr\()_kernel_rst_start, .Linterrupt_return_\srr\()_kernel_rst_end, interrupt_return_\srr\()_kernel_restart)
#endif

.endm

interrupt_return_macro srr
#ifdef CONFIG_PPC_BOOK3S
interrupt_return_macro hsrr

	.globl __end_soft_masked
__end_soft_masked:
DEFINE_FIXED_SYMBOL(__end_soft_masked)
#endif /* CONFIG_PPC_BOOK3S */

#ifdef CONFIG_PPC_BOOK3S
_GLOBAL(ret_from_fork_scv)
	bl	schedule_tail
	REST_NVGPRS(r1)
	li	r3,0	/* fork() return value */
	b	.Lsyscall_vectored_common_exit
#endif

_GLOBAL(ret_from_fork)
	bl	schedule_tail
	REST_NVGPRS(r1)
	li	r3,0	/* fork() return value */
	b	.Lsyscall_exit

_GLOBAL(ret_from_kernel_thread)
	bl	schedule_tail
	REST_NVGPRS(r1)
	mtctr	r14
	mr	r3,r15
#ifdef PPC64_ELF_ABI_v2
	mr	r12,r14
#endif
	bctrl
	li	r3,0
	b	.Lsyscall_exit
