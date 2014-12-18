/*
 * This file contains the 64-bit "server" PowerPC variant
 * of the low level exception handling including exception
 * vectors, exception return, part of the slb and stab
 * handling and other fixed offset specific things.
 *
 * This file is meant to be #included from head_64.S due to
 * position dependent assembly.
 *
 * Most of this originates from head_64.S and thus has the same
 * copyright history.
 *
 */

#include <asm/hw_irq.h>
#include <asm/exception-64s.h>
#include <asm/ptrace.h>

/*
 * We layout physical memory as follows:
 * 0x0000 - 0x00ff : Secondary processor spin code
 * 0x0100 - 0x17ff : pSeries Interrupt prologs
 * 0x1800 - 0x4000 : interrupt support common interrupt prologs
 * 0x4000 - 0x5fff : pSeries interrupts with IR=1,DR=1
 * 0x6000 - 0x6fff : more interrupt support including for IR=1,DR=1
 * 0x7000 - 0x7fff : FWNMI data area
 * 0x8000 - 0x8fff : Initial (CPU0) segment table
 * 0x9000 -        : Early init and support code
 */
	/* Syscall routine is used twice, in reloc-off and reloc-on paths */
#define SYSCALL_PSERIES_1 					\
BEGIN_FTR_SECTION						\
	cmpdi	r0,0x1ebe ; 					\
	beq-	1f ;						\
END_FTR_SECTION_IFSET(CPU_FTR_REAL_LE)				\
	mr	r9,r13 ;					\
	GET_PACA(r13) ;						\
	mfspr	r11,SPRN_SRR0 ;					\
0:

#define SYSCALL_PSERIES_2_RFID 					\
	mfspr	r12,SPRN_SRR1 ;					\
	ld	r10,PACAKBASE(r13) ; 				\
	LOAD_HANDLER(r10, system_call_entry) ; 			\
	mtspr	SPRN_SRR0,r10 ; 				\
	ld	r10,PACAKMSR(r13) ;				\
	mtspr	SPRN_SRR1,r10 ; 				\
	rfid ; 							\
	b	. ;	/* prevent speculative execution */

#define SYSCALL_PSERIES_3					\
	/* Fast LE/BE switch system call */			\
1:	mfspr	r12,SPRN_SRR1 ;					\
	xori	r12,r12,MSR_LE ;				\
	mtspr	SPRN_SRR1,r12 ;					\
	rfid ;		/* return to userspace */		\
	b	. ;	/* prevent speculative execution */

#if defined(CONFIG_RELOCATABLE)
	/*
	 * We can't branch directly; in the direct case we use LR
	 * and system_call_entry restores LR.  (We thus need to move
	 * LR to r10 in the RFID case too.)
	 */
#define SYSCALL_PSERIES_2_DIRECT				\
	mflr	r10 ;						\
	ld	r12,PACAKBASE(r13) ; 				\
	LOAD_HANDLER(r12, system_call_entry_direct) ;		\
	mtctr	r12 ;						\
	mfspr	r12,SPRN_SRR1 ;					\
	/* Re-use of r13... No spare regs to do this */	\
	li	r13,MSR_RI ;					\
	mtmsrd 	r13,1 ;						\
	GET_PACA(r13) ;	/* get r13 back */			\
	bctr ;
#else
	/* We can branch directly */
#define SYSCALL_PSERIES_2_DIRECT				\
	mfspr	r12,SPRN_SRR1 ;					\
	li	r10,MSR_RI ;					\
	mtmsrd 	r10,1 ;			/* Set RI (EE=0) */	\
	b	system_call_entry_direct ;
#endif

/*
 * This is the start of the interrupt handlers for pSeries
 * This code runs with relocation off.
 * Code from here to __end_interrupts gets copied down to real
 * address 0x100 when we are running a relocatable kernel.
 * Therefore any relative branches in this section must only
 * branch to labels in this section.
 */
	. = 0x100
	.globl __start_interrupts
__start_interrupts:

	.globl system_reset_pSeries;
system_reset_pSeries:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)
#ifdef CONFIG_PPC_P7_NAP
BEGIN_FTR_SECTION
	/* Running native on arch 2.06 or later, check if we are
	 * waking up from nap. We only handle no state loss and
	 * supervisor state loss. We do -not- handle hypervisor
	 * state loss at this time.
	 */
	mfspr	r13,SPRN_SRR1
	rlwinm.	r13,r13,47-31,30,31
	beq	9f

	/* waking up from powersave (nap) state */
	cmpwi	cr1,r13,2
	/* Total loss of HV state is fatal, we could try to use the
	 * PIR to locate a PACA, then use an emergency stack etc...
	 * OPAL v3 based powernv platforms have new idle states
	 * which fall in this catagory.
	 */
	bgt	cr1,8f
	GET_PACA(r13)

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	li	r0,KVM_HWTHREAD_IN_KERNEL
	stb	r0,HSTATE_HWTHREAD_STATE(r13)
	/* Order setting hwthread_state vs. testing hwthread_req */
	sync
	lbz	r0,HSTATE_HWTHREAD_REQ(r13)
	cmpwi	r0,0
	beq	1f
	b	kvm_start_guest
1:
#endif

	/* Return SRR1 from power7_nap() */
	mfspr	r3,SPRN_SRR1
	beq	cr1,2f
	b	power7_wakeup_noloss
2:	b	power7_wakeup_loss

	/* Fast Sleep wakeup on PowerNV */
8:	GET_PACA(r13)
	b 	power7_wakeup_tb_loss

9:
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
#endif /* CONFIG_PPC_P7_NAP */
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, system_reset_common, EXC_STD,
				 NOTEST, 0x100)

	. = 0x200
machine_check_pSeries_1:
	/* This is moved out of line as it can be patched by FW, but
	 * some code path might still want to branch into the original
	 * vector
	 */
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)		/* save r13 */
#ifdef CONFIG_PPC_P7_NAP
BEGIN_FTR_SECTION
	/* Running native on arch 2.06 or later, check if we are
	 * waking up from nap. We only handle no state loss and
	 * supervisor state loss. We do -not- handle hypervisor
	 * state loss at this time.
	 */
	mfspr	r13,SPRN_SRR1
	rlwinm.	r13,r13,47-31,30,31
	OPT_GET_SPR(r13, SPRN_CFAR, CPU_FTR_CFAR)
	beq	9f

	mfspr	r13,SPRN_SRR1
	rlwinm.	r13,r13,47-31,30,31
	/* waking up from powersave (nap) state */
	cmpwi	cr1,r13,2
	/* Total loss of HV state is fatal. let's just stay stuck here */
	OPT_GET_SPR(r13, SPRN_CFAR, CPU_FTR_CFAR)
	bgt	cr1,.
9:
	OPT_SET_SPR(r13, SPRN_CFAR, CPU_FTR_CFAR)
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
#endif /* CONFIG_PPC_P7_NAP */
	EXCEPTION_PROLOG_0(PACA_EXMC)
BEGIN_FTR_SECTION
	b	machine_check_pSeries_early
FTR_SECTION_ELSE
	b	machine_check_pSeries_0
ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE)

	. = 0x300
	.globl data_access_pSeries
data_access_pSeries:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, data_access_common, EXC_STD,
				 KVMTEST, 0x300)

	. = 0x380
	.globl data_access_slb_pSeries
data_access_slb_pSeries:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXSLB)
	EXCEPTION_PROLOG_1(PACA_EXSLB, KVMTEST, 0x380)
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_DAR
#ifdef __DISABLED__
	/* Keep that around for when we re-implement dynamic VSIDs */
	cmpdi	r3,0
	bge	slb_miss_user_pseries
#endif /* __DISABLED__ */
	mfspr	r12,SPRN_SRR1
#ifndef CONFIG_RELOCATABLE
	b	slb_miss_realmode
#else
	/*
	 * We can't just use a direct branch to slb_miss_realmode
	 * because the distance from here to there depends on where
	 * the kernel ends up being put.
	 */
	mfctr	r11
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10, slb_miss_realmode)
	mtctr	r10
	bctr
#endif

	STD_EXCEPTION_PSERIES(0x400, 0x400, instruction_access)

	. = 0x480
	.globl instruction_access_slb_pSeries
instruction_access_slb_pSeries:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXSLB)
	EXCEPTION_PROLOG_1(PACA_EXSLB, KVMTEST_PR, 0x480)
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_SRR0		/* SRR0 is faulting address */
#ifdef __DISABLED__
	/* Keep that around for when we re-implement dynamic VSIDs */
	cmpdi	r3,0
	bge	slb_miss_user_pseries
#endif /* __DISABLED__ */
	mfspr	r12,SPRN_SRR1
#ifndef CONFIG_RELOCATABLE
	b	slb_miss_realmode
#else
	mfctr	r11
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10, slb_miss_realmode)
	mtctr	r10
	bctr
#endif

	/* We open code these as we can't have a ". = x" (even with
	 * x = "." within a feature section
	 */
	. = 0x500;
	.globl hardware_interrupt_pSeries;
	.globl hardware_interrupt_hv;
hardware_interrupt_pSeries:
hardware_interrupt_hv:
	HMT_MEDIUM_PPR_DISCARD
	BEGIN_FTR_SECTION
		_MASKABLE_EXCEPTION_PSERIES(0x502, hardware_interrupt,
					    EXC_HV, SOFTEN_TEST_HV)
		KVM_HANDLER(PACA_EXGEN, EXC_HV, 0x502)
	FTR_SECTION_ELSE
		_MASKABLE_EXCEPTION_PSERIES(0x500, hardware_interrupt,
					    EXC_STD, SOFTEN_TEST_HV_201)
		KVM_HANDLER(PACA_EXGEN, EXC_STD, 0x500)
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)

	STD_EXCEPTION_PSERIES(0x600, 0x600, alignment)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x600)

	STD_EXCEPTION_PSERIES(0x700, 0x700, program_check)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x700)

	STD_EXCEPTION_PSERIES(0x800, 0x800, fp_unavailable)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x800)

	. = 0x900
	.globl decrementer_pSeries
decrementer_pSeries:
	_MASKABLE_EXCEPTION_PSERIES(0x900, decrementer, EXC_STD, SOFTEN_TEST_PR)

	STD_EXCEPTION_HV(0x980, 0x982, hdecrementer)

	MASKABLE_EXCEPTION_PSERIES(0xa00, 0xa00, doorbell_super)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xa00)

	STD_EXCEPTION_PSERIES(0xb00, 0xb00, trap_0b)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xb00)

	. = 0xc00
	.globl	system_call_pSeries
system_call_pSeries:
	 /*
	  * If CONFIG_KVM_BOOK3S_64_HANDLER is set, save the PPR (on systems
	  * that support it) before changing to HMT_MEDIUM. That allows the KVM
	  * code to save that value into the guest state (it is the guest's PPR
	  * value). Otherwise just change to HMT_MEDIUM as userspace has
	  * already saved the PPR.
	  */
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
	SET_SCRATCH0(r13)
	GET_PACA(r13)
	std	r9,PACA_EXGEN+EX_R9(r13)
	OPT_GET_SPR(r9, SPRN_PPR, CPU_FTR_HAS_PPR);
	HMT_MEDIUM;
	std	r10,PACA_EXGEN+EX_R10(r13)
	OPT_SAVE_REG_TO_PACA(PACA_EXGEN+EX_PPR, r9, CPU_FTR_HAS_PPR);
	mfcr	r9
	KVMTEST(0xc00)
	GET_SCRATCH0(r13)
#else
	HMT_MEDIUM;
#endif
	SYSCALL_PSERIES_1
	SYSCALL_PSERIES_2_RFID
	SYSCALL_PSERIES_3
	KVM_HANDLER(PACA_EXGEN, EXC_STD, 0xc00)

	STD_EXCEPTION_PSERIES(0xd00, 0xd00, single_step)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xd00)

	/* At 0xe??? we have a bunch of hypervisor exceptions, we branch
	 * out of line to handle them
	 */
	. = 0xe00
hv_data_storage_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	h_data_storage_hv

	. = 0xe20
hv_instr_storage_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	h_instr_storage_hv

	. = 0xe40
emulation_assist_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	emulation_assist_hv

	. = 0xe60
hv_exception_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	hmi_exception_early

	. = 0xe80
hv_doorbell_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	h_doorbell_hv

	/* We need to deal with the Altivec unavailable exception
	 * here which is at 0xf20, thus in the middle of the
	 * prolog code of the PerformanceMonitor one. A little
	 * trickery is thus necessary
	 */
	. = 0xf00
performance_monitor_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	performance_monitor_pSeries

	. = 0xf20
altivec_unavailable_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	altivec_unavailable_pSeries

	. = 0xf40
vsx_unavailable_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	vsx_unavailable_pSeries

	. = 0xf60
facility_unavailable_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	facility_unavailable_pSeries

	. = 0xf80
hv_facility_unavailable_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	facility_unavailable_hv

#ifdef CONFIG_CBE_RAS
	STD_EXCEPTION_HV(0x1200, 0x1202, cbe_system_error)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_HV, 0x1202)
#endif /* CONFIG_CBE_RAS */

	STD_EXCEPTION_PSERIES(0x1300, 0x1300, instruction_breakpoint)
	KVM_HANDLER_PR_SKIP(PACA_EXGEN, EXC_STD, 0x1300)

	. = 0x1500
	.global denorm_exception_hv
denorm_exception_hv:
	HMT_MEDIUM_PPR_DISCARD
	mtspr	SPRN_SPRG_HSCRATCH0,r13
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	EXCEPTION_PROLOG_1(PACA_EXGEN, NOTEST, 0x1500)

#ifdef CONFIG_PPC_DENORMALISATION
	mfspr	r10,SPRN_HSRR1
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	andis.	r10,r10,(HSRR1_DENORM)@h /* denorm? */
	addi	r11,r11,-4		/* HSRR0 is next instruction */
	bne+	denorm_assist
#endif

	KVMTEST(0x1500)
	EXCEPTION_PROLOG_PSERIES_1(denorm_common, EXC_HV)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_STD, 0x1500)

#ifdef CONFIG_CBE_RAS
	STD_EXCEPTION_HV(0x1600, 0x1602, cbe_maintenance)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_HV, 0x1602)
#endif /* CONFIG_CBE_RAS */

	STD_EXCEPTION_PSERIES(0x1700, 0x1700, altivec_assist)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x1700)

#ifdef CONFIG_CBE_RAS
	STD_EXCEPTION_HV(0x1800, 0x1802, cbe_thermal)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_HV, 0x1802)
#else
	. = 0x1800
#endif /* CONFIG_CBE_RAS */


/*** Out of line interrupts support ***/

	.align	7
	/* moved from 0x200 */
machine_check_pSeries_early:
BEGIN_FTR_SECTION
	EXCEPTION_PROLOG_1(PACA_EXMC, NOTEST, 0x200)
	/*
	 * Register contents:
	 * R13		= PACA
	 * R9		= CR
	 * Original R9 to R13 is saved on PACA_EXMC
	 *
	 * Switch to mc_emergency stack and handle re-entrancy (we limit
	 * the nested MCE upto level 4 to avoid stack overflow).
	 * Save MCE registers srr1, srr0, dar and dsisr and then set ME=1
	 *
	 * We use paca->in_mce to check whether this is the first entry or
	 * nested machine check. We increment paca->in_mce to track nested
	 * machine checks.
	 *
	 * If this is the first entry then set stack pointer to
	 * paca->mc_emergency_sp, otherwise r1 is already pointing to
	 * stack frame on mc_emergency stack.
	 *
	 * NOTE: We are here with MSR_ME=0 (off), which means we risk a
	 * checkstop if we get another machine check exception before we do
	 * rfid with MSR_ME=1.
	 */
	mr	r11,r1			/* Save r1 */
	lhz	r10,PACA_IN_MCE(r13)
	cmpwi	r10,0			/* Are we in nested machine check */
	bne	0f			/* Yes, we are. */
	/* First machine check entry */
	ld	r1,PACAMCEMERGSP(r13)	/* Use MC emergency stack */
0:	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame */
	addi	r10,r10,1		/* increment paca->in_mce */
	sth	r10,PACA_IN_MCE(r13)
	/* Limit nested MCE to level 4 to avoid stack overflow */
	cmpwi	r10,4
	bgt	2f			/* Check if we hit limit of 4 */
	std	r11,GPR1(r1)		/* Save r1 on the stack. */
	std	r11,0(r1)		/* make stack chain pointer */
	mfspr	r11,SPRN_SRR0		/* Save SRR0 */
	std	r11,_NIP(r1)
	mfspr	r11,SPRN_SRR1		/* Save SRR1 */
	std	r11,_MSR(r1)
	mfspr	r11,SPRN_DAR		/* Save DAR */
	std	r11,_DAR(r1)
	mfspr	r11,SPRN_DSISR		/* Save DSISR */
	std	r11,_DSISR(r1)
	std	r9,_CCR(r1)		/* Save CR in stackframe */
	/* Save r9 through r13 from EXMC save area to stack frame. */
	EXCEPTION_PROLOG_COMMON_2(PACA_EXMC)
	mfmsr	r11			/* get MSR value */
	ori	r11,r11,MSR_ME		/* turn on ME bit */
	ori	r11,r11,MSR_RI		/* turn on RI bit */
	ld	r12,PACAKBASE(r13)	/* get high part of &label */
	LOAD_HANDLER(r12, machine_check_handle_early)
1:	mtspr	SPRN_SRR0,r12
	mtspr	SPRN_SRR1,r11
	rfid
	b	.	/* prevent speculative execution */
2:
	/* Stack overflow. Stay on emergency stack and panic.
	 * Keep the ME bit off while panic-ing, so that if we hit
	 * another machine check we checkstop.
	 */
	addi	r1,r1,INT_FRAME_SIZE	/* go back to previous stack frame */
	ld	r11,PACAKMSR(r13)
	ld	r12,PACAKBASE(r13)
	LOAD_HANDLER(r12, unrecover_mce)
	li	r10,MSR_ME
	andc	r11,r11,r10		/* Turn off MSR_ME */
	b	1b
	b	.	/* prevent speculative execution */
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)

machine_check_pSeries:
	.globl machine_check_fwnmi
machine_check_fwnmi:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)		/* save r13 */
	EXCEPTION_PROLOG_0(PACA_EXMC)
machine_check_pSeries_0:
	EXCEPTION_PROLOG_1(PACA_EXMC, KVMTEST, 0x200)
	EXCEPTION_PROLOG_PSERIES_1(machine_check_common, EXC_STD)
	KVM_HANDLER_SKIP(PACA_EXMC, EXC_STD, 0x200)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_STD, 0x300)
	KVM_HANDLER_SKIP(PACA_EXSLB, EXC_STD, 0x380)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x400)
	KVM_HANDLER_PR(PACA_EXSLB, EXC_STD, 0x480)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0x900)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0x982)

#ifdef CONFIG_PPC_DENORMALISATION
denorm_assist:
BEGIN_FTR_SECTION
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER6 do that here for all FP regs.
 */
	mfmsr	r10
	ori	r10,r10,(MSR_FP|MSR_FE0|MSR_FE1)
	xori	r10,r10,(MSR_FE0|MSR_FE1)
	mtmsrd	r10
	sync

#define FMR2(n)  fmr (n), (n) ; fmr n+1, n+1
#define FMR4(n)  FMR2(n) ; FMR2(n+2)
#define FMR8(n)  FMR4(n) ; FMR4(n+4)
#define FMR16(n) FMR8(n) ; FMR8(n+8)
#define FMR32(n) FMR16(n) ; FMR16(n+16)
	FMR32(0)

FTR_SECTION_ELSE
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER7 do that here for the first 32 VSX registers only.
 */
	mfmsr	r10
	oris	r10,r10,MSR_VSX@h
	mtmsrd	r10
	sync

#define XVCPSGNDP2(n) XVCPSGNDP(n,n,n) ; XVCPSGNDP(n+1,n+1,n+1)
#define XVCPSGNDP4(n) XVCPSGNDP2(n) ; XVCPSGNDP2(n+2)
#define XVCPSGNDP8(n) XVCPSGNDP4(n) ; XVCPSGNDP4(n+4)
#define XVCPSGNDP16(n) XVCPSGNDP8(n) ; XVCPSGNDP8(n+8)
#define XVCPSGNDP32(n) XVCPSGNDP16(n) ; XVCPSGNDP16(n+16)
	XVCPSGNDP32(0)

ALT_FTR_SECTION_END_IFCLR(CPU_FTR_ARCH_206)

BEGIN_FTR_SECTION
	b	denorm_done
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER8 we need to do that for all 64 VSX registers
 */
	XVCPSGNDP32(32)
denorm_done:
	mtspr	SPRN_HSRR0,r11
	mtcrf	0x80,r9
	ld	r9,PACA_EXGEN+EX_R9(r13)
	RESTORE_PPR_PACA(PACA_EXGEN, r10)
BEGIN_FTR_SECTION
	ld	r10,PACA_EXGEN+EX_CFAR(r13)
	mtspr	SPRN_CFAR,r10
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	HRFID
	b	.
#endif

	.align	7
	/* moved from 0xe00 */
	STD_EXCEPTION_HV_OOL(0xe02, h_data_storage)
	KVM_HANDLER_SKIP(PACA_EXGEN, EXC_HV, 0xe02)
	STD_EXCEPTION_HV_OOL(0xe22, h_instr_storage)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0xe22)
	STD_EXCEPTION_HV_OOL(0xe42, emulation_assist)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0xe42)
	MASKABLE_EXCEPTION_HV_OOL(0xe62, hmi_exception)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0xe62)

	MASKABLE_EXCEPTION_HV_OOL(0xe82, h_doorbell)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0xe82)

	/* moved from 0xf00 */
	STD_EXCEPTION_PSERIES_OOL(0xf00, performance_monitor)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xf00)
	STD_EXCEPTION_PSERIES_OOL(0xf20, altivec_unavailable)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xf20)
	STD_EXCEPTION_PSERIES_OOL(0xf40, vsx_unavailable)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xf40)
	STD_EXCEPTION_PSERIES_OOL(0xf60, facility_unavailable)
	KVM_HANDLER_PR(PACA_EXGEN, EXC_STD, 0xf60)
	STD_EXCEPTION_HV_OOL(0xf82, facility_unavailable)
	KVM_HANDLER(PACA_EXGEN, EXC_HV, 0xf82)

/*
 * An interrupt came in while soft-disabled. We set paca->irq_happened, then:
 * - If it was a decrementer interrupt, we bump the dec to max and and return.
 * - If it was a doorbell we return immediately since doorbells are edge
 *   triggered and won't automatically refire.
 * - If it was a HMI we return immediately since we handled it in realmode
 *   and it won't refire.
 * - else we hard disable and return.
 * This is called with r10 containing the value to OR to the paca field.
 */
#define MASKED_INTERRUPT(_H)				\
masked_##_H##interrupt:					\
	std	r11,PACA_EXGEN+EX_R11(r13);		\
	lbz	r11,PACAIRQHAPPENED(r13);		\
	or	r11,r11,r10;				\
	stb	r11,PACAIRQHAPPENED(r13);		\
	cmpwi	r10,PACA_IRQ_DEC;			\
	bne	1f;					\
	lis	r10,0x7fff;				\
	ori	r10,r10,0xffff;				\
	mtspr	SPRN_DEC,r10;				\
	b	2f;					\
1:	cmpwi	r10,PACA_IRQ_DBELL;			\
	beq	2f;					\
	cmpwi	r10,PACA_IRQ_HMI;			\
	beq	2f;					\
	mfspr	r10,SPRN_##_H##SRR1;			\
	rldicl	r10,r10,48,1; /* clear MSR_EE */	\
	rotldi	r10,r10,16;				\
	mtspr	SPRN_##_H##SRR1,r10;			\
2:	mtcrf	0x80,r9;				\
	ld	r9,PACA_EXGEN+EX_R9(r13);		\
	ld	r10,PACA_EXGEN+EX_R10(r13);		\
	ld	r11,PACA_EXGEN+EX_R11(r13);		\
	GET_SCRATCH0(r13);				\
	##_H##rfid;					\
	b	.
	
	MASKED_INTERRUPT()
	MASKED_INTERRUPT(H)

/*
 * Called from arch_local_irq_enable when an interrupt needs
 * to be resent. r3 contains 0x500, 0x900, 0xa00 or 0xe80 to indicate
 * which kind of interrupt. MSR:EE is already off. We generate a
 * stackframe like if a real interrupt had happened.
 *
 * Note: While MSR:EE is off, we need to make sure that _MSR
 * in the generated frame has EE set to 1 or the exception
 * handler will not properly re-enable them.
 */
_GLOBAL(__replay_interrupt)
	/* We are going to jump to the exception common code which
	 * will retrieve various register values from the PACA which
	 * we don't give a damn about, so we don't bother storing them.
	 */
	mfmsr	r12
	mflr	r11
	mfcr	r9
	ori	r12,r12,MSR_EE
	cmpwi	r3,0x900
	beq	decrementer_common
	cmpwi	r3,0x500
	beq	hardware_interrupt_common
BEGIN_FTR_SECTION
	cmpwi	r3,0xe80
	beq	h_doorbell_common
FTR_SECTION_ELSE
	cmpwi	r3,0xa00
	beq	doorbell_super_common
ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE)
	blr

#ifdef CONFIG_PPC_PSERIES
/*
 * Vectors for the FWNMI option.  Share common code.
 */
	.globl system_reset_fwnmi
      .align 7
system_reset_fwnmi:
	HMT_MEDIUM_PPR_DISCARD
	SET_SCRATCH0(r13)		/* save r13 */
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, system_reset_common, EXC_STD,
				 NOTEST, 0x100)

#endif /* CONFIG_PPC_PSERIES */

#ifdef __DISABLED__
/*
 * This is used for when the SLB miss handler has to go virtual,
 * which doesn't happen for now anymore but will once we re-implement
 * dynamic VSIDs for shared page tables
 */
slb_miss_user_pseries:
	std	r10,PACA_EXGEN+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R11(r13)
	std	r12,PACA_EXGEN+EX_R12(r13)
	GET_SCRATCH0(r10)
	ld	r11,PACA_EXSLB+EX_R9(r13)
	ld	r12,PACA_EXSLB+EX_R3(r13)
	std	r10,PACA_EXGEN+EX_R13(r13)
	std	r11,PACA_EXGEN+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R3(r13)
	clrrdi	r12,r13,32
	mfmsr	r10
	mfspr	r11,SRR0			/* save SRR0 */
	ori	r12,r12,slb_miss_user_common@l	/* virt addr of handler */
	ori	r10,r10,MSR_IR|MSR_DR|MSR_RI
	mtspr	SRR0,r12
	mfspr	r12,SRR1			/* and SRR1 */
	mtspr	SRR1,r10
	rfid
	b	.				/* prevent spec. execution */
#endif /* __DISABLED__ */

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
kvmppc_skip_interrupt:
	/*
	 * Here all GPRs are unchanged from when the interrupt happened
	 * except for r13, which is saved in SPRG_SCRATCH0.
	 */
	mfspr	r13, SPRN_SRR0
	addi	r13, r13, 4
	mtspr	SPRN_SRR0, r13
	GET_SCRATCH0(r13)
	rfid
	b	.

kvmppc_skip_Hinterrupt:
	/*
	 * Here all GPRs are unchanged from when the interrupt happened
	 * except for r13, which is saved in SPRG_SCRATCH0.
	 */
	mfspr	r13, SPRN_HSRR0
	addi	r13, r13, 4
	mtspr	SPRN_HSRR0, r13
	GET_SCRATCH0(r13)
	hrfid
	b	.
#endif

/*
 * Code from here down to __end_handlers is invoked from the
 * exception prologs above.  Because the prologs assemble the
 * addresses of these handlers using the LOAD_HANDLER macro,
 * which uses an ori instruction, these handlers must be in
 * the first 64k of the kernel image.
 */

/*** Common interrupt handlers ***/

	STD_EXCEPTION_COMMON(0x100, system_reset, system_reset_exception)

	STD_EXCEPTION_COMMON_ASYNC(0x500, hardware_interrupt, do_IRQ)
	STD_EXCEPTION_COMMON_ASYNC(0x900, decrementer, timer_interrupt)
	STD_EXCEPTION_COMMON(0x980, hdecrementer, hdec_interrupt)
#ifdef CONFIG_PPC_DOORBELL
	STD_EXCEPTION_COMMON_ASYNC(0xa00, doorbell_super, doorbell_exception)
#else
	STD_EXCEPTION_COMMON_ASYNC(0xa00, doorbell_super, unknown_exception)
#endif
	STD_EXCEPTION_COMMON(0xb00, trap_0b, unknown_exception)
	STD_EXCEPTION_COMMON(0xd00, single_step, single_step_exception)
	STD_EXCEPTION_COMMON(0xe00, trap_0e, unknown_exception)
	STD_EXCEPTION_COMMON(0xe40, emulation_assist, emulation_assist_interrupt)
	STD_EXCEPTION_COMMON_ASYNC(0xe60, hmi_exception, handle_hmi_exception)
#ifdef CONFIG_PPC_DOORBELL
	STD_EXCEPTION_COMMON_ASYNC(0xe80, h_doorbell, doorbell_exception)
#else
	STD_EXCEPTION_COMMON_ASYNC(0xe80, h_doorbell, unknown_exception)
#endif
	STD_EXCEPTION_COMMON_ASYNC(0xf00, performance_monitor, performance_monitor_exception)
	STD_EXCEPTION_COMMON(0x1300, instruction_breakpoint, instruction_breakpoint_exception)
	STD_EXCEPTION_COMMON(0x1502, denorm, unknown_exception)
#ifdef CONFIG_ALTIVEC
	STD_EXCEPTION_COMMON(0x1700, altivec_assist, altivec_assist_exception)
#else
	STD_EXCEPTION_COMMON(0x1700, altivec_assist, unknown_exception)
#endif
#ifdef CONFIG_CBE_RAS
	STD_EXCEPTION_COMMON(0x1200, cbe_system_error, cbe_system_error_exception)
	STD_EXCEPTION_COMMON(0x1600, cbe_maintenance, cbe_maintenance_exception)
	STD_EXCEPTION_COMMON(0x1800, cbe_thermal, cbe_thermal_exception)
#endif /* CONFIG_CBE_RAS */

	/*
	 * Relocation-on interrupts: A subset of the interrupts can be delivered
	 * with IR=1/DR=1, if AIL==2 and MSR.HV won't be changed by delivering
	 * it.  Addresses are the same as the original interrupt addresses, but
	 * offset by 0xc000000000004000.
	 * It's impossible to receive interrupts below 0x300 via this mechanism.
	 * KVM: None of these traps are from the guest ; anything that escalated
	 * to HV=1 from HV=0 is delivered via real mode handlers.
	 */

	/*
	 * This uses the standard macro, since the original 0x300 vector
	 * only has extra guff for STAB-based processors -- which never
	 * come here.
	 */
	STD_RELON_EXCEPTION_PSERIES(0x4300, 0x300, data_access)
	. = 0x4380
	.globl data_access_slb_relon_pSeries
data_access_slb_relon_pSeries:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXSLB)
	EXCEPTION_PROLOG_1(PACA_EXSLB, NOTEST, 0x380)
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_DAR
	mfspr	r12,SPRN_SRR1
#ifndef CONFIG_RELOCATABLE
	b	slb_miss_realmode
#else
	/*
	 * We can't just use a direct branch to slb_miss_realmode
	 * because the distance from here to there depends on where
	 * the kernel ends up being put.
	 */
	mfctr	r11
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10, slb_miss_realmode)
	mtctr	r10
	bctr
#endif

	STD_RELON_EXCEPTION_PSERIES(0x4400, 0x400, instruction_access)
	. = 0x4480
	.globl instruction_access_slb_relon_pSeries
instruction_access_slb_relon_pSeries:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXSLB)
	EXCEPTION_PROLOG_1(PACA_EXSLB, NOTEST, 0x480)
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_SRR0		/* SRR0 is faulting address */
	mfspr	r12,SPRN_SRR1
#ifndef CONFIG_RELOCATABLE
	b	slb_miss_realmode
#else
	mfctr	r11
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10, slb_miss_realmode)
	mtctr	r10
	bctr
#endif

	. = 0x4500
	.globl hardware_interrupt_relon_pSeries;
	.globl hardware_interrupt_relon_hv;
hardware_interrupt_relon_pSeries:
hardware_interrupt_relon_hv:
	BEGIN_FTR_SECTION
		_MASKABLE_RELON_EXCEPTION_PSERIES(0x502, hardware_interrupt, EXC_HV, SOFTEN_TEST_HV)
	FTR_SECTION_ELSE
		_MASKABLE_RELON_EXCEPTION_PSERIES(0x500, hardware_interrupt, EXC_STD, SOFTEN_TEST_PR)
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE)
	STD_RELON_EXCEPTION_PSERIES(0x4600, 0x600, alignment)
	STD_RELON_EXCEPTION_PSERIES(0x4700, 0x700, program_check)
	STD_RELON_EXCEPTION_PSERIES(0x4800, 0x800, fp_unavailable)
	MASKABLE_RELON_EXCEPTION_PSERIES(0x4900, 0x900, decrementer)
	STD_RELON_EXCEPTION_HV(0x4980, 0x982, hdecrementer)
	MASKABLE_RELON_EXCEPTION_PSERIES(0x4a00, 0xa00, doorbell_super)
	STD_RELON_EXCEPTION_PSERIES(0x4b00, 0xb00, trap_0b)

	. = 0x4c00
	.globl system_call_relon_pSeries
system_call_relon_pSeries:
	HMT_MEDIUM
	SYSCALL_PSERIES_1
	SYSCALL_PSERIES_2_DIRECT
	SYSCALL_PSERIES_3

	STD_RELON_EXCEPTION_PSERIES(0x4d00, 0xd00, single_step)

	. = 0x4e00
	b	.	/* Can't happen, see v2.07 Book III-S section 6.5 */

	. = 0x4e20
	b	.	/* Can't happen, see v2.07 Book III-S section 6.5 */

	. = 0x4e40
emulation_assist_relon_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	emulation_assist_relon_hv

	. = 0x4e60
	b	.	/* Can't happen, see v2.07 Book III-S section 6.5 */

	. = 0x4e80
h_doorbell_relon_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	h_doorbell_relon_hv

	. = 0x4f00
performance_monitor_relon_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	performance_monitor_relon_pSeries

	. = 0x4f20
altivec_unavailable_relon_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	altivec_unavailable_relon_pSeries

	. = 0x4f40
vsx_unavailable_relon_pseries_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	vsx_unavailable_relon_pSeries

	. = 0x4f60
facility_unavailable_relon_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	facility_unavailable_relon_pSeries

	. = 0x4f80
hv_facility_unavailable_relon_trampoline:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	hv_facility_unavailable_relon_hv

	STD_RELON_EXCEPTION_PSERIES(0x5300, 0x1300, instruction_breakpoint)
#ifdef CONFIG_PPC_DENORMALISATION
	. = 0x5500
	b	denorm_exception_hv
#endif
	STD_RELON_EXCEPTION_PSERIES(0x5700, 0x1700, altivec_assist)

	/* Other future vectors */
	.align	7
	.globl	__end_interrupts
__end_interrupts:

	.align	7
system_call_entry_direct:
#if defined(CONFIG_RELOCATABLE)
	/* The first level prologue may have used LR to get here, saving
	 * orig in r10.  To save hacking/ifdeffing common code, restore here.
	 */
	mtlr	r10
#endif
system_call_entry:
	b	system_call_common

ppc64_runlatch_on_trampoline:
	b	__ppc64_runlatch_on

/*
 * Here r13 points to the paca, r9 contains the saved CR,
 * SRR0 and SRR1 are saved in r11 and r12,
 * r9 - r13 are saved in paca->exgen.
 */
	.align	7
	.globl data_access_common
data_access_common:
	mfspr	r10,SPRN_DAR
	std	r10,PACA_EXGEN+EX_DAR(r13)
	mfspr	r10,SPRN_DSISR
	stw	r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0x300, PACA_EXGEN)
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r12,_MSR(r1)
	ld	r3,PACA_EXGEN+EX_DAR(r13)
	lwz	r4,PACA_EXGEN+EX_DSISR(r13)
	li	r5,0x300
	b	do_hash_page		/* Try to handle as hpte fault */

	.align  7
	.globl  h_data_storage_common
h_data_storage_common:
	mfspr   r10,SPRN_HDAR
	std     r10,PACA_EXGEN+EX_DAR(r13)
	mfspr   r10,SPRN_HDSISR
	stw     r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0xe00, PACA_EXGEN)
	bl      save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl      unknown_exception
	b       ret_from_except

	.align	7
	.globl instruction_access_common
instruction_access_common:
	EXCEPTION_PROLOG_COMMON(0x400, PACA_EXGEN)
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r12,_MSR(r1)
	ld	r3,_NIP(r1)
	andis.	r4,r12,0x5820
	li	r5,0x400
	b	do_hash_page		/* Try to handle as hpte fault */

	STD_EXCEPTION_COMMON(0xe20, h_instr_storage, unknown_exception)

/*
 * Here is the common SLB miss user that is used when going to virtual
 * mode for SLB misses, that is currently not used
 */
#ifdef __DISABLED__
	.align	7
	.globl	slb_miss_user_common
slb_miss_user_common:
	mflr	r10
	std	r3,PACA_EXGEN+EX_DAR(r13)
	stw	r9,PACA_EXGEN+EX_CCR(r13)
	std	r10,PACA_EXGEN+EX_LR(r13)
	std	r11,PACA_EXGEN+EX_SRR0(r13)
	bl	slb_allocate_user

	ld	r10,PACA_EXGEN+EX_LR(r13)
	ld	r3,PACA_EXGEN+EX_R3(r13)
	lwz	r9,PACA_EXGEN+EX_CCR(r13)
	ld	r11,PACA_EXGEN+EX_SRR0(r13)
	mtlr	r10
	beq-	slb_miss_fault

	andi.	r10,r12,MSR_RI		/* check for unrecoverable exception */
	beq-	unrecov_user_slb
	mfmsr	r10

.machine push
.machine "power4"
	mtcrf	0x80,r9
.machine pop

	clrrdi	r10,r10,2		/* clear RI before setting SRR0/1 */
	mtmsrd	r10,1

	mtspr	SRR0,r11
	mtspr	SRR1,r12

	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	rfid
	b	.

slb_miss_fault:
	EXCEPTION_PROLOG_COMMON(0x380, PACA_EXGEN)
	ld	r4,PACA_EXGEN+EX_DAR(r13)
	li	r5,0
	std	r4,_DAR(r1)
	std	r5,_DSISR(r1)
	b	handle_page_fault

unrecov_user_slb:
	EXCEPTION_PROLOG_COMMON(0x4200, PACA_EXGEN)
	RECONCILE_IRQ_STATE(r10, r11)
	bl	save_nvgprs
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	1b

#endif /* __DISABLED__ */


	/*
	 * Machine check is different because we use a different
	 * save area: PACA_EXMC instead of PACA_EXGEN.
	 */
	.align	7
	.globl machine_check_common
machine_check_common:

	mfspr	r10,SPRN_DAR
	std	r10,PACA_EXGEN+EX_DAR(r13)
	mfspr	r10,SPRN_DSISR
	stw	r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0x200, PACA_EXMC)
	FINISH_NAP
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r3,PACA_EXGEN+EX_DAR(r13)
	lwz	r4,PACA_EXGEN+EX_DSISR(r13)
	std	r3,_DAR(r1)
	std	r4,_DSISR(r1)
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_exception
	b	ret_from_except

	.align	7
	.globl alignment_common
alignment_common:
	mfspr	r10,SPRN_DAR
	std	r10,PACA_EXGEN+EX_DAR(r13)
	mfspr	r10,SPRN_DSISR
	stw	r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0x600, PACA_EXGEN)
	ld	r3,PACA_EXGEN+EX_DAR(r13)
	lwz	r4,PACA_EXGEN+EX_DSISR(r13)
	std	r3,_DAR(r1)
	std	r4,_DSISR(r1)
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	alignment_exception
	b	ret_from_except

	.align	7
	.globl program_check_common
program_check_common:
	EXCEPTION_PROLOG_COMMON(0x700, PACA_EXGEN)
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	program_check_exception
	b	ret_from_except

	.align	7
	.globl fp_unavailable_common
fp_unavailable_common:
	EXCEPTION_PROLOG_COMMON(0x800, PACA_EXGEN)
	bne	1f			/* if from user, just load it up */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	kernel_fp_unavailable_exception
	BUG_OPCODE
1:
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	bl	load_up_fpu
	b	fast_exception_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	fp_unavailable_tm
	b	ret_from_except
#endif
	.align	7
	.globl altivec_unavailable_common
altivec_unavailable_common:
	EXCEPTION_PROLOG_COMMON(0xf20, PACA_EXGEN)
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	beq	1f
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
  BEGIN_FTR_SECTION_NESTED(69)
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
  END_FTR_SECTION_NESTED(CPU_FTR_TM, CPU_FTR_TM, 69)
#endif
	bl	load_up_altivec
	b	fast_exception_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_tm
	b	ret_from_except
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_exception
	b	ret_from_except

	.align	7
	.globl vsx_unavailable_common
vsx_unavailable_common:
	EXCEPTION_PROLOG_COMMON(0xf40, PACA_EXGEN)
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	beq	1f
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
  BEGIN_FTR_SECTION_NESTED(69)
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
  END_FTR_SECTION_NESTED(CPU_FTR_TM, CPU_FTR_TM, 69)
#endif
	b	load_up_vsx
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_tm
	b	ret_from_except
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_exception
	b	ret_from_except

	STD_EXCEPTION_COMMON(0xf60, facility_unavailable, facility_unavailable_exception)
	STD_EXCEPTION_COMMON(0xf80, hv_facility_unavailable, facility_unavailable_exception)

	.align	7
	.globl	__end_handlers
__end_handlers:

	/* Equivalents to the above handlers for relocation-on interrupt vectors */
	STD_RELON_EXCEPTION_HV_OOL(0xe40, emulation_assist)
	MASKABLE_RELON_EXCEPTION_HV_OOL(0xe80, h_doorbell)

	STD_RELON_EXCEPTION_PSERIES_OOL(0xf00, performance_monitor)
	STD_RELON_EXCEPTION_PSERIES_OOL(0xf20, altivec_unavailable)
	STD_RELON_EXCEPTION_PSERIES_OOL(0xf40, vsx_unavailable)
	STD_RELON_EXCEPTION_PSERIES_OOL(0xf60, facility_unavailable)
	STD_RELON_EXCEPTION_HV_OOL(0xf80, hv_facility_unavailable)

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
/*
 * Data area reserved for FWNMI option.
 * This address (0x7000) is fixed by the RPA.
 */
	.= 0x7000
	.globl fwnmi_data_area
fwnmi_data_area:

	/* pseries and powernv need to keep the whole page from
	 * 0x7000 to 0x8000 free for use by the firmware
	 */
	. = 0x8000
#endif /* defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV) */

	.globl hmi_exception_early
hmi_exception_early:
	EXCEPTION_PROLOG_1(PACA_EXGEN, NOTEST, 0xe60)
	mr	r10,r1			/* Save r1			*/
	ld	r1,PACAEMERGSP(r13)	/* Use emergency stack		*/
	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame		*/
	std	r9,_CCR(r1)		/* save CR in stackframe	*/
	mfspr	r11,SPRN_HSRR0		/* Save HSRR0 */
	std	r11,_NIP(r1)		/* save HSRR0 in stackframe	*/
	mfspr	r12,SPRN_HSRR1		/* Save SRR1 */
	std	r12,_MSR(r1)		/* save SRR1 in stackframe	*/
	std	r10,0(r1)		/* make stack chain pointer	*/
	std	r0,GPR0(r1)		/* save r0 in stackframe	*/
	std	r10,GPR1(r1)		/* save r1 in stackframe	*/
	EXCEPTION_PROLOG_COMMON_2(PACA_EXGEN)
	EXCEPTION_PROLOG_COMMON_3(0xe60)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	hmi_exception_realmode
	/* Windup the stack. */
	/* Move original HSRR0 and HSRR1 into the respective regs */
	ld	r9,_MSR(r1)
	mtspr	SPRN_HSRR1,r9
	ld	r3,_NIP(r1)
	mtspr	SPRN_HSRR0,r3
	ld	r9,_CTR(r1)
	mtctr	r9
	ld	r9,_XER(r1)
	mtxer	r9
	ld	r9,_LINK(r1)
	mtlr	r9
	REST_GPR(0, r1)
	REST_8GPRS(2, r1)
	REST_GPR(10, r1)
	ld	r11,_CCR(r1)
	mtcr	r11
	REST_GPR(11, r1)
	REST_2GPRS(12, r1)
	/* restore original r1. */
	ld	r1,GPR1(r1)

	/*
	 * Go to virtual mode and pull the HMI event information from
	 * firmware.
	 */
	.globl hmi_exception_after_realmode
hmi_exception_after_realmode:
	SET_SCRATCH0(r13)
	EXCEPTION_PROLOG_0(PACA_EXGEN)
	b	hmi_exception_hv


#define MACHINE_CHECK_HANDLER_WINDUP			\
	/* Clear MSR_RI before setting SRR0 and SRR1. */\
	li	r0,MSR_RI;				\
	mfmsr	r9;		/* get MSR value */	\
	andc	r9,r9,r0;				\
	mtmsrd	r9,1;		/* Clear MSR_RI */	\
	/* Move original SRR0 and SRR1 into the respective regs */	\
	ld	r9,_MSR(r1);				\
	mtspr	SPRN_SRR1,r9;				\
	ld	r3,_NIP(r1);				\
	mtspr	SPRN_SRR0,r3;				\
	ld	r9,_CTR(r1);				\
	mtctr	r9;					\
	ld	r9,_XER(r1);				\
	mtxer	r9;					\
	ld	r9,_LINK(r1);				\
	mtlr	r9;					\
	REST_GPR(0, r1);				\
	REST_8GPRS(2, r1);				\
	REST_GPR(10, r1);				\
	ld	r11,_CCR(r1);				\
	mtcr	r11;					\
	/* Decrement paca->in_mce. */			\
	lhz	r12,PACA_IN_MCE(r13);			\
	subi	r12,r12,1;				\
	sth	r12,PACA_IN_MCE(r13);			\
	REST_GPR(11, r1);				\
	REST_2GPRS(12, r1);				\
	/* restore original r1. */			\
	ld	r1,GPR1(r1)

	/*
	 * Handle machine check early in real mode. We come here with
	 * ME=1, MMU (IR=0 and DR=0) off and using MC emergency stack.
	 */
	.align	7
	.globl machine_check_handle_early
machine_check_handle_early:
	std	r0,GPR0(r1)	/* Save r0 */
	EXCEPTION_PROLOG_COMMON_3(0x200)
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_early
	std	r3,RESULT(r1)	/* Save result */
	ld	r12,_MSR(r1)
#ifdef	CONFIG_PPC_P7_NAP
	/*
	 * Check if thread was in power saving mode. We come here when any
	 * of the following is true:
	 * a. thread wasn't in power saving mode
	 * b. thread was in power saving mode with no state loss or
	 *    supervisor state loss
	 *
	 * Go back to nap again if (b) is true.
	 */
	rlwinm.	r11,r12,47-31,30,31	/* Was it in power saving mode? */
	beq	4f			/* No, it wasn;t */
	/* Thread was in power saving mode. Go back to nap again. */
	cmpwi	r11,2
	bne	3f
	/* Supervisor state loss */
	li	r0,1
	stb	r0,PACA_NAPSTATELOST(r13)
3:	bl	machine_check_queue_event
	MACHINE_CHECK_HANDLER_WINDUP
	GET_PACA(r13)
	ld	r1,PACAR1(r13)
	b	power7_enter_nap_mode
4:
#endif
	/*
	 * Check if we are coming from hypervisor userspace. If yes then we
	 * continue in host kernel in V mode to deliver the MC event.
	 */
	rldicl.	r11,r12,4,63		/* See if MC hit while in HV mode. */
	beq	5f
	andi.	r11,r12,MSR_PR		/* See if coming from user. */
	bne	9f			/* continue in V mode if we are. */

5:
#ifdef CONFIG_KVM_BOOK3S_64_HV
	/*
	 * We are coming from kernel context. Check if we are coming from
	 * guest. if yes, then we can continue. We will fall through
	 * do_kvm_200->kvmppc_interrupt to deliver the MC event to guest.
	 */
	lbz	r11,HSTATE_IN_GUEST(r13)
	cmpwi	r11,0			/* Check if coming from guest */
	bne	9f			/* continue if we are. */
#endif
	/*
	 * At this point we are not sure about what context we come from.
	 * Queue up the MCE event and return from the interrupt.
	 * But before that, check if this is an un-recoverable exception.
	 * If yes, then stay on emergency stack and panic.
	 */
	andi.	r11,r12,MSR_RI
	bne	2f
1:	mfspr	r11,SPRN_SRR0
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10,unrecover_mce)
	mtspr	SPRN_SRR0,r10
	ld	r10,PACAKMSR(r13)
	/*
	 * We are going down. But there are chances that we might get hit by
	 * another MCE during panic path and we may run into unstable state
	 * with no way out. Hence, turn ME bit off while going down, so that
	 * when another MCE is hit during panic path, system will checkstop
	 * and hypervisor will get restarted cleanly by SP.
	 */
	li	r3,MSR_ME
	andc	r10,r10,r3		/* Turn off MSR_ME */
	mtspr	SPRN_SRR1,r10
	rfid
	b	.
2:
	/*
	 * Check if we have successfully handled/recovered from error, if not
	 * then stay on emergency stack and panic.
	 */
	ld	r3,RESULT(r1)	/* Load result */
	cmpdi	r3,0		/* see if we handled MCE successfully */

	beq	1b		/* if !handled then panic */
	/*
	 * Return from MC interrupt.
	 * Queue up the MCE event so that we can log it later, while
	 * returning from kernel or opal call.
	 */
	bl	machine_check_queue_event
	MACHINE_CHECK_HANDLER_WINDUP
	rfid
9:
	/* Deliver the machine check to host kernel in V mode. */
	MACHINE_CHECK_HANDLER_WINDUP
	b	machine_check_pSeries

unrecover_mce:
	/* Invoke machine_check_exception to print MCE event and panic. */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_exception
	/*
	 * We will not reach here. Even if we did, there is no way out. Call
	 * unrecoverable_exception and die.
	 */
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	1b
/*
 * r13 points to the PACA, r9 contains the saved CR,
 * r12 contain the saved SRR1, SRR0 is still ready for return
 * r3 has the faulting address
 * r9 - r13 are saved in paca->exslb.
 * r3 is saved in paca->slb_r3
 * We assume we aren't going to take any exceptions during this procedure.
 */
slb_miss_realmode:
	mflr	r10
#ifdef CONFIG_RELOCATABLE
	mtctr	r11
#endif

	stw	r9,PACA_EXSLB+EX_CCR(r13)	/* save CR in exc. frame */
	std	r10,PACA_EXSLB+EX_LR(r13)	/* save LR */

	bl	slb_allocate_realmode

	/* All done -- return from exception. */

	ld	r10,PACA_EXSLB+EX_LR(r13)
	ld	r3,PACA_EXSLB+EX_R3(r13)
	lwz	r9,PACA_EXSLB+EX_CCR(r13)	/* get saved CR */

	mtlr	r10

	andi.	r10,r12,MSR_RI	/* check for unrecoverable exception */
	beq-	2f

.machine	push
.machine	"power4"
	mtcrf	0x80,r9
	mtcrf	0x01,r9		/* slb_allocate uses cr0 and cr7 */
.machine	pop

	RESTORE_PPR_PACA(PACA_EXSLB, r9)
	ld	r9,PACA_EXSLB+EX_R9(r13)
	ld	r10,PACA_EXSLB+EX_R10(r13)
	ld	r11,PACA_EXSLB+EX_R11(r13)
	ld	r12,PACA_EXSLB+EX_R12(r13)
	ld	r13,PACA_EXSLB+EX_R13(r13)
	rfid
	b	.	/* prevent speculative execution */

2:	mfspr	r11,SPRN_SRR0
	ld	r10,PACAKBASE(r13)
	LOAD_HANDLER(r10,unrecov_slb)
	mtspr	SPRN_SRR0,r10
	ld	r10,PACAKMSR(r13)
	mtspr	SPRN_SRR1,r10
	rfid
	b	.

unrecov_slb:
	EXCEPTION_PROLOG_COMMON(0x4100, PACA_EXSLB)
	RECONCILE_IRQ_STATE(r10, r11)
	bl	save_nvgprs
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	1b


#ifdef CONFIG_PPC_970_NAP
power4_fixup_nap:
	andc	r9,r9,r10
	std	r9,TI_LOCAL_FLAGS(r11)
	ld	r10,_LINK(r1)		/* make idle task do the */
	std	r10,_NIP(r1)		/* equivalent of a blr */
	blr
#endif

/*
 * Hash table stuff
 */
	.align	7
do_hash_page:
	std	r3,_DAR(r1)
	std	r4,_DSISR(r1)

	andis.	r0,r4,0xa410		/* weird error? */
	bne-	handle_page_fault	/* if not, try to insert a HPTE */
	andis.  r0,r4,DSISR_DABRMATCH@h
	bne-    handle_dabr_fault
	CURRENT_THREAD_INFO(r11, r1)
	lwz	r0,TI_PREEMPT(r11)	/* If we're in an "NMI" */
	andis.	r0,r0,NMI_MASK@h	/* (i.e. an irq when soft-disabled) */
	bne	77f			/* then don't call hash_page now */
	/*
	 * We need to set the _PAGE_USER bit if MSR_PR is set or if we are
	 * accessing a userspace segment (even from the kernel). We assume
	 * kernel addresses always have the high bit set.
	 */
	rlwinm	r4,r4,32-25+9,31-9,31-9	/* DSISR_STORE -> _PAGE_RW */
	rotldi	r0,r3,15		/* Move high bit into MSR_PR posn */
	orc	r0,r12,r0		/* MSR_PR | ~high_bit */
	rlwimi	r4,r0,32-13,30,30	/* becomes _PAGE_USER access bit */
	ori	r4,r4,1			/* add _PAGE_PRESENT */
	rlwimi	r4,r5,22+2,31-2,31-2	/* Set _PAGE_EXEC if trap is 0x400 */

	/*
	 * r3 contains the faulting address
	 * r4 contains the required access permissions
	 * r5 contains the trap number
	 * r6 contains dsisr
	 *
	 * at return r3 = 0 for success, 1 for page fault, negative for error
	 */
	ld      r6,_DSISR(r1)
	bl	hash_page		/* build HPTE if possible */
	cmpdi	r3,0			/* see if hash_page succeeded */

	/* Success */
	beq	fast_exc_return_irq	/* Return from exception on success */

	/* Error */
	blt-	13f

/* Here we have a page fault that hash_page can't handle. */
handle_page_fault:
11:	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_page_fault
	cmpdi	r3,0
	beq+	12f
	bl	save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r4,_DAR(r1)
	bl	bad_page_fault
	b	ret_from_except

/* We have a data breakpoint exception - handle it */
handle_dabr_fault:
	bl	save_nvgprs
	ld      r4,_DAR(r1)
	ld      r5,_DSISR(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl      do_break
12:	b       ret_from_except_lite


/* We have a page fault that hash_page could handle but HV refused
 * the PTE insertion
 */
13:	bl	save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	low_hash_fault
	b	ret_from_except

/*
 * We come here as a result of a DSI at a point where we don't want
 * to call hash_page, such as when we are accessing memory (possibly
 * user memory) inside a PMU interrupt that occurred while interrupts
 * were soft-disabled.  We want to invoke the exception handler for
 * the access, or panic if there isn't a handler.
 */
77:	bl	save_nvgprs
	mr	r4,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r5,SIGSEGV
	bl	bad_page_fault
	b	ret_from_except

/*
 * Here we have detected that the kernel stack pointer is bad.
 * R9 contains the saved CR, r13 points to the paca,
 * r10 contains the (bad) kernel stack pointer,
 * r11 and r12 contain the saved SRR0 and SRR1.
 * We switch to using an emergency stack, save the registers there,
 * and call kernel_bad_stack(), which panics.
 */
bad_stack:
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,64+INT_FRAME_SIZE
	std	r9,_CCR(r1)
	std	r10,GPR1(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	mfspr	r11,SPRN_DAR
	mfspr	r12,SPRN_DSISR
	std	r11,_DAR(r1)
	std	r12,_DSISR(r1)
	mflr	r10
	mfctr	r11
	mfxer	r12
	std	r10,_LINK(r1)
	std	r11,_CTR(r1)
	std	r12,_XER(r1)
	SAVE_GPR(0,r1)
	SAVE_GPR(2,r1)
	ld	r10,EX_R3(r3)
	std	r10,GPR3(r1)
	SAVE_GPR(4,r1)
	SAVE_4GPRS(5,r1)
	ld	r9,EX_R9(r3)
	ld	r10,EX_R10(r3)
	SAVE_2GPRS(9,r1)
	ld	r9,EX_R11(r3)
	ld	r10,EX_R12(r3)
	ld	r11,EX_R13(r3)
	std	r9,GPR11(r1)
	std	r10,GPR12(r1)
	std	r11,GPR13(r1)
BEGIN_FTR_SECTION
	ld	r10,EX_CFAR(r3)
	std	r10,ORIG_GPR3(r1)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	SAVE_8GPRS(14,r1)
	SAVE_10GPRS(22,r1)
	lhz	r12,PACA_TRAP_SAVE(r13)
	std	r12,_TRAP(r1)
	addi	r11,r1,INT_FRAME_SIZE
	std	r11,0(r1)
	li	r12,0
	std	r12,0(r11)
	ld	r2,PACATOC(r13)
	ld	r11,exception_marker@toc(r2)
	std	r12,RESULT(r1)
	std	r11,STACK_FRAME_OVERHEAD-16(r1)
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	kernel_bad_stack
	b	1b
