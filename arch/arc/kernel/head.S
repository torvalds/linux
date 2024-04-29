/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARC CPU startup Code
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: Dec 2007
 *  -Check if we are running on Simulator or on real hardware
 *      to skip certain things during boot on simulator
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/entry.h>
#include <asm/arcregs.h>
#include <asm/cache.h>
#include <asm/dsp-impl.h>
#include <asm/irqflags.h>

.macro CPU_EARLY_SETUP

	; Setting up Vectror Table (in case exception happens in early boot
	sr	@_int_vec_base_lds, [AUX_INTR_VEC_BASE]

	; Disable I-cache/D-cache if kernel so configured
	lr	r5, [ARC_REG_IC_BCR]
	breq    r5, 0, 1f		; I$ doesn't exist
	lr	r5, [ARC_REG_IC_CTRL]
#ifdef CONFIG_ARC_HAS_ICACHE
	bclr	r5, r5, 0		; 0 - Enable, 1 is Disable
#else
	bset	r5, r5, 0		; I$ exists, but is not used
#endif
	sr	r5, [ARC_REG_IC_CTRL]

1:
	lr	r5, [ARC_REG_DC_BCR]
	breq    r5, 0, 1f		; D$ doesn't exist
	lr	r5, [ARC_REG_DC_CTRL]
	bclr	r5, r5, 6		; Invalidate (discard w/o wback)
#ifdef CONFIG_ARC_HAS_DCACHE
	bclr	r5, r5, 0		; Enable (+Inv)
#else
	bset	r5, r5, 0		; Disable (+Inv)
#endif
	sr	r5, [ARC_REG_DC_CTRL]

1:

#ifdef CONFIG_ISA_ARCV2
	; Unaligned access is disabled at reset, so re-enable early as
	; gcc 7.3.1 (ARC GNU 2018.03) onwards generates unaligned access
	; by default
	lr	r5, [status32]
#ifdef CONFIG_ARC_USE_UNALIGNED_MEM_ACCESS
	bset	r5, r5, STATUS_AD_BIT
#else
	; Although disabled at reset, bootloader might have enabled it
	bclr	r5, r5, STATUS_AD_BIT
#endif
	kflag	r5

#ifdef CONFIG_ARC_LPB_DISABLE
	lr	r5, [ARC_REG_LPB_BUILD]
	breq    r5, 0, 1f		; LPB doesn't exist
	mov	r5, 1
	sr	r5, [ARC_REG_LPB_CTRL]
1:
#endif /* CONFIG_ARC_LPB_DISABLE */

	/* On HSDK, CCMs need to remapped super early */
#ifdef CONFIG_ARC_SOC_HSDK
	mov	r6, 0x60000000
	lr	r5, [ARC_REG_ICCM_BUILD]
	breq	r5, 0, 1f
	sr	r6, [ARC_REG_AUX_ICCM]
1:
	lr	r5, [ARC_REG_DCCM_BUILD]
	breq	r5, 0, 2f
	sr	r6, [ARC_REG_AUX_DCCM]
2:
#endif	/* CONFIG_ARC_SOC_HSDK */

#endif	/* CONFIG_ISA_ARCV2 */

	; Config DSP_CTRL properly, so kernel may use integer multiply,
	; multiply-accumulate, and divide operations
	DSP_EARLY_INIT
.endm

	.section .init.text, "ax",@progbits

;----------------------------------------------------------------
; Default Reset Handler (jumped into from Reset vector)
; - Don't clobber r0,r1,r2 as they might have u-boot provided args
; - Platforms can override this weak version if needed
;----------------------------------------------------------------
WEAK(res_service)
	j	stext
END(res_service)

;----------------------------------------------------------------
; Kernel Entry point
;----------------------------------------------------------------
ENTRY(stext)

	CPU_EARLY_SETUP

#ifdef CONFIG_SMP
	GET_CPU_ID  r5
	cmp	r5, 0
	mov.nz	r0, r5
	bz	.Lmaster_proceed

	; Non-Masters wait for Master to boot enough and bring them up
	; when they resume, tail-call to entry point
	mov	blink, @first_lines_of_secondary
	j	arc_platform_smp_wait_to_boot

.Lmaster_proceed:
#endif

	; Clear BSS before updating any globals
	; XXX: use ZOL here
	mov	r5, __bss_start
	sub	r6, __bss_stop, r5
	lsr.f	lp_count, r6, 2
	lpnz	1f
	st.ab   0, [r5, 4]
1:

	; Uboot - kernel ABI
	;    r0 = [0] No uboot interaction, [1] cmdline in r2, [2] DTB in r2
	;    r1 = magic number (always zero as of now)
	;    r2 = pointer to uboot provided cmdline or external DTB in mem
	; These are handled later in handle_uboot_args()
	st	r0, [@uboot_tag]
	st      r1, [@uboot_magic]
	st	r2, [@uboot_arg]

	; setup "current" tsk and optionally cache it in dedicated r25
	mov	r9, @init_task
	SET_CURR_TASK_ON_CPU  r9, r0	; r9 = tsk, r0 = scratch

	; setup stack (fp, sp)
	mov	fp, 0

	; tsk->thread_info is really a PAGE, whose bottom hoists stack
	GET_TSK_STACK_BASE r9, sp	; r9 = tsk, sp = stack base(output)

	j	start_kernel	; "C" entry point
END(stext)

#ifdef CONFIG_SMP
;----------------------------------------------------------------
;     First lines of code run by secondary before jumping to 'C'
;----------------------------------------------------------------
	.section .text, "ax",@progbits
ENTRY(first_lines_of_secondary)

	; setup per-cpu idle task as "current" on this CPU
	ld	r0, [@secondary_idle_tsk]
	SET_CURR_TASK_ON_CPU  r0, r1

	; setup stack (fp, sp)
	mov	fp, 0

	; set its stack base to tsk->thread_info bottom
	GET_TSK_STACK_BASE r0, sp

	j	start_kernel_secondary
END(first_lines_of_secondary)
#endif
