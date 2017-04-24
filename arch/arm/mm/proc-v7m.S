/*
 *  linux/arch/arm/mm/proc-v7m.S
 *
 *  Copyright (C) 2008 ARM Ltd.
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv7-M processor support.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/v7m.h>
#include "proc-macros.S"

ENTRY(cpu_v7m_proc_init)
	ret	lr
ENDPROC(cpu_v7m_proc_init)

ENTRY(cpu_v7m_proc_fin)
	ret	lr
ENDPROC(cpu_v7m_proc_fin)

/*
 *	cpu_v7m_reset(loc)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_v7m_reset)
	ret	r0
ENDPROC(cpu_v7m_reset)

/*
 *	cpu_v7m_do_idle()
 *
 *	Idle the processor (eg, wait for interrupt).
 *
 *	IRQs are already disabled.
 */
ENTRY(cpu_v7m_do_idle)
	wfi
	ret	lr
ENDPROC(cpu_v7m_do_idle)

ENTRY(cpu_v7m_dcache_clean_area)
	ret	lr
ENDPROC(cpu_v7m_dcache_clean_area)

/*
 * There is no MMU, so here is nothing to do.
 */
ENTRY(cpu_v7m_switch_mm)
	ret	lr
ENDPROC(cpu_v7m_switch_mm)

.globl	cpu_v7m_suspend_size
.equ	cpu_v7m_suspend_size, 0

#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_v7m_do_suspend)
	ret	lr
ENDPROC(cpu_v7m_do_suspend)

ENTRY(cpu_v7m_do_resume)
	ret	lr
ENDPROC(cpu_v7m_do_resume)
#endif

ENTRY(cpu_cm7_dcache_clean_area)
	dcache_line_size r2, r3
	movw	r3, #:lower16:BASEADDR_V7M_SCB + V7M_SCB_DCCMVAC
	movt	r3, #:upper16:BASEADDR_V7M_SCB + V7M_SCB_DCCMVAC

1:	str	r0, [r3]		@ clean D entry
	add	r0, r0, r2
	subs	r1, r1, r2
	bhi	1b
	dsb
	ret	lr
ENDPROC(cpu_cm7_dcache_clean_area)

ENTRY(cpu_cm7_proc_fin)
	movw	r2, #:lower16:(BASEADDR_V7M_SCB + V7M_SCB_CCR)
	movt	r2, #:upper16:(BASEADDR_V7M_SCB + V7M_SCB_CCR)
	ldr	r0, [r2]
	bic	r0, r0, #(V7M_SCB_CCR_DC | V7M_SCB_CCR_IC)
	str	r0, [r2]
	ret	lr
ENDPROC(cpu_cm7_proc_fin)

	.section ".init.text", #alloc, #execinstr

__v7m_cm7_setup:
	mov	r8, #(V7M_SCB_CCR_DC | V7M_SCB_CCR_IC| V7M_SCB_CCR_BP)
	b	__v7m_setup_cont
/*
 *	__v7m_setup
 *
 *	This should be able to cover all ARMv7-M cores.
 */
__v7m_setup:
	mov	r8, 0

__v7m_setup_cont:
	@ Configure the vector table base address
	ldr	r0, =BASEADDR_V7M_SCB
	ldr	r12, =vector_table
	str	r12, [r0, V7M_SCB_VTOR]

	@ enable UsageFault, BusFault and MemManage fault.
	ldr	r5, [r0, #V7M_SCB_SHCSR]
	orr	r5, #(V7M_SCB_SHCSR_USGFAULTENA | V7M_SCB_SHCSR_BUSFAULTENA | V7M_SCB_SHCSR_MEMFAULTENA)
	str	r5, [r0, #V7M_SCB_SHCSR]

	@ Lower the priority of the SVC and PendSV exceptions
	mov	r5, #0x80000000
	str	r5, [r0, V7M_SCB_SHPR2]	@ set SVC priority
	mov	r5, #0x00800000
	str	r5, [r0, V7M_SCB_SHPR3]	@ set PendSV priority

	@ SVC to switch to handler mode. Notice that this requires sp to
	@ point to writeable memory because the processor saves
	@ some registers to the stack.
	badr	r1, 1f
	ldr	r5, [r12, #11 * 4]	@ read the SVC vector entry
	str	r1, [r12, #11 * 4]	@ write the temporary SVC vector entry
	dsb
	mov	r6, lr			@ save LR
	ldr	sp, =init_thread_union + THREAD_START_SP
	cpsie	i
	svc	#0
1:	cpsid	i
	str	r5, [r12, #11 * 4]	@ restore the original SVC vector entry
	mov	lr, r6			@ restore LR

	@ Special-purpose control register
	mov	r1, #1
	msr	control, r1		@ Thread mode has unpriviledged access

	@ Configure caches (if implemented)
	teq     r8, #0
	stmneia	sp, {r0-r6, lr}		@ v7m_invalidate_l1 touches r0-r6
	blne	v7m_invalidate_l1
	teq     r8, #0			@ re-evalutae condition
	ldmneia	sp, {r0-r6, lr}

	@ Configure the System Control Register to ensure 8-byte stack alignment
	@ Note the STKALIGN bit is either RW or RAO.
	ldr	r0, [r0, V7M_SCB_CCR]   @ system control register
	orr	r0, #V7M_SCB_CCR_STKALIGN
	orr	r0, r0, r8

	ret	lr
ENDPROC(__v7m_setup)

/*
 * Cortex-M7 processor functions
 */
	globl_equ	cpu_cm7_proc_init,	cpu_v7m_proc_init
	globl_equ	cpu_cm7_reset,		cpu_v7m_reset
	globl_equ	cpu_cm7_do_idle,	cpu_v7m_do_idle
	globl_equ	cpu_cm7_switch_mm,	cpu_v7m_switch_mm

	define_processor_functions v7m, dabort=nommu_early_abort, pabort=legacy_pabort, nommu=1
	define_processor_functions cm7, dabort=nommu_early_abort, pabort=legacy_pabort, nommu=1

	.section ".rodata"
	string cpu_arch_name, "armv7m"
	string cpu_elf_name "v7m"
	string cpu_v7m_name "ARMv7-M"

	.section ".proc.info.init", #alloc

.macro __v7m_proc name, initfunc, cache_fns = nop_cache_fns, hwcaps = 0,  proc_fns = v7m_processor_functions
	.long	0			/* proc_info_list.__cpu_mm_mmu_flags */
	.long	0			/* proc_info_list.__cpu_io_mmu_flags */
	initfn	\initfunc, \name
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_HALF | HWCAP_THUMB | HWCAP_FAST_MULT | \hwcaps
	.long	cpu_v7m_name
	.long   \proc_fns
	.long	0			/* proc_info_list.tlb */
	.long	0			/* proc_info_list.user */
	.long	\cache_fns
.endm

	/*
	 * Match ARM Cortex-M7 processor.
	 */
	.type	__v7m_cm7_proc_info, #object
__v7m_cm7_proc_info:
	.long	0x410fc270		/* ARM Cortex-M7 0xC27 */
	.long	0xff0ffff0		/* Mask off revision, patch release */
	__v7m_proc __v7m_cm7_proc_info, __v7m_cm7_setup, hwcaps = HWCAP_EDSP, cache_fns = v7m_cache_fns, proc_fns = cm7_processor_functions
	.size	__v7m_cm7_proc_info, . - __v7m_cm7_proc_info

	/*
	 * Match ARM Cortex-M4 processor.
	 */
	.type	__v7m_cm4_proc_info, #object
__v7m_cm4_proc_info:
	.long	0x410fc240		/* ARM Cortex-M4 0xC24 */
	.long	0xff0ffff0		/* Mask off revision, patch release */
	__v7m_proc __v7m_cm4_proc_info, __v7m_setup, hwcaps = HWCAP_EDSP
	.size	__v7m_cm4_proc_info, . - __v7m_cm4_proc_info

	/*
	 * Match ARM Cortex-M3 processor.
	 */
	.type	__v7m_cm3_proc_info, #object
__v7m_cm3_proc_info:
	.long	0x410fc230		/* ARM Cortex-M3 0xC23 */
	.long	0xff0ffff0		/* Mask off revision, patch release */
	__v7m_proc __v7m_cm3_proc_info, __v7m_setup
	.size	__v7m_cm3_proc_info, . - __v7m_cm3_proc_info

	/*
	 * Match any ARMv7-M processor core.
	 */
	.type	__v7m_proc_info, #object
__v7m_proc_info:
	.long	0x000f0000		@ Required ID value
	.long	0x000f0000		@ Mask for ID
	__v7m_proc __v7m_proc_info, __v7m_setup
	.size	__v7m_proc_info, . - __v7m_proc_info

