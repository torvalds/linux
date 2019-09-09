/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (C) Copyright 2007
 * Texas Instruments
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * (C) Copyright 2004
 * Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 */
#include <linux/linkage.h>

#include <asm/assembler.h>

#include "omap34xx.h"
#include "iomap.h"
#include "cm3xxx.h"
#include "prm3xxx.h"
#include "sdrc.h"
#include "sram.h"
#include "control.h"

/*
 * Registers access definitions
 */
#define SDRC_SCRATCHPAD_SEM_OFFS	0xc
#define SDRC_SCRATCHPAD_SEM_V	OMAP343X_SCRATCHPAD_REGADDR\
					(SDRC_SCRATCHPAD_SEM_OFFS)
#define PM_PREPWSTST_CORE_P	OMAP3430_PRM_BASE + CORE_MOD +\
					OMAP3430_PM_PREPWSTST
#define PM_PWSTCTRL_MPU_P	OMAP3430_PRM_BASE + MPU_MOD + OMAP2_PM_PWSTCTRL
#define CM_IDLEST1_CORE_V	OMAP34XX_CM_REGADDR(CORE_MOD, CM_IDLEST1)
#define CM_IDLEST_CKGEN_V	OMAP34XX_CM_REGADDR(PLL_MOD, CM_IDLEST)
#define SRAM_BASE_P		OMAP3_SRAM_PA
#define CONTROL_STAT		OMAP343X_CTRL_BASE + OMAP343X_CONTROL_STATUS
#define CONTROL_MEM_RTA_CTRL	(OMAP343X_CTRL_BASE +\
					OMAP36XX_CONTROL_MEM_RTA_CTRL)

/* Move this as correct place is available */
#define SCRATCHPAD_MEM_OFFS	0x310
#define SCRATCHPAD_BASE_P	(OMAP343X_CTRL_BASE +\
					OMAP343X_CONTROL_MEM_WKUP +\
					SCRATCHPAD_MEM_OFFS)
#define SDRC_POWER_V		OMAP34XX_SDRC_REGADDR(SDRC_POWER)
#define SDRC_SYSCONFIG_P	(OMAP343X_SDRC_BASE + SDRC_SYSCONFIG)
#define SDRC_MR_0_P		(OMAP343X_SDRC_BASE + SDRC_MR_0)
#define SDRC_EMR2_0_P		(OMAP343X_SDRC_BASE + SDRC_EMR2_0)
#define SDRC_MANUAL_0_P		(OMAP343X_SDRC_BASE + SDRC_MANUAL_0)
#define SDRC_MR_1_P		(OMAP343X_SDRC_BASE + SDRC_MR_1)
#define SDRC_EMR2_1_P		(OMAP343X_SDRC_BASE + SDRC_EMR2_1)
#define SDRC_MANUAL_1_P		(OMAP343X_SDRC_BASE + SDRC_MANUAL_1)
#define SDRC_DLLA_STATUS_V	OMAP34XX_SDRC_REGADDR(SDRC_DLLA_STATUS)
#define SDRC_DLLA_CTRL_V	OMAP34XX_SDRC_REGADDR(SDRC_DLLA_CTRL)

/*
 * This file needs be built unconditionally as ARM to interoperate correctly
 * with non-Thumb-2-capable firmware.
 */
	.arm

/*
 * API functions
 */

	.text
/*
 * L2 cache needs to be toggled for stable OFF mode functionality on 3630.
 * This function sets up a flag that will allow for this toggling to take
 * place on 3630. Hopefully some version in the future may not need this.
 */
ENTRY(enable_omap3630_toggle_l2_on_restore)
	stmfd	sp!, {lr}	@ save registers on stack
	/* Setup so that we will disable and enable l2 */
	mov	r1, #0x1
	adrl	r3, l2dis_3630_offset	@ may be too distant for plain adr
	ldr	r2, [r3]		@ value for offset
	str	r1, [r2, r3]		@ write to l2dis_3630
	ldmfd	sp!, {pc}	@ restore regs and return
ENDPROC(enable_omap3630_toggle_l2_on_restore)

/*
 * Function to call rom code to save secure ram context.
 *
 * r0 = physical address of the parameters
 */
	.arch armv7-a
	.arch_extension sec
ENTRY(save_secure_ram_context)
	stmfd	sp!, {r4 - r11, lr}	@ save registers on stack
	mov	r3, r0			@ physical address of parameters
	mov	r0, #25			@ set service ID for PPA
	mov	r12, r0			@ copy secure service ID in r12
	mov	r1, #0			@ set task id for ROM code in r1
	mov	r2, #4			@ set some flags in r2, r6
	mov	r6, #0xff
	dsb				@ data write barrier
	dmb				@ data memory barrier
	smc	#1			@ call SMI monitor (smi #1)
	nop
	nop
	nop
	nop
	ldmfd	sp!, {r4 - r11, pc}
ENDPROC(save_secure_ram_context)

/*
 * ======================
 * == Idle entry point ==
 * ======================
 */

/*
 * Forces OMAP into idle state
 *
 * omap34xx_cpu_suspend() - This bit of code saves the CPU context if needed
 * and executes the WFI instruction. Calling WFI effectively changes the
 * power domains states to the desired target power states.
 *
 *
 * Notes:
 * - only the minimum set of functions gets copied to internal SRAM at boot
 *   and after wake-up from OFF mode, cf. omap_push_sram_idle. The function
 *   pointers in SDRAM or SRAM are called depending on the desired low power
 *   target state.
 * - when the OMAP wakes up it continues at different execution points
 *   depending on the low power mode (non-OFF vs OFF modes),
 *   cf. 'Resume path for xxx mode' comments.
 */
	.align	3
ENTRY(omap34xx_cpu_suspend)
	stmfd	sp!, {r4 - r11, lr}	@ save registers on stack

	/*
	 * r0 contains information about saving context:
	 *   0 - No context lost
	 *   1 - Only L1 and logic lost
	 *   2 - Only L2 lost (Even L1 is retained we clean it along with L2)
	 *   3 - Both L1 and L2 lost and logic lost
	 */

	/*
	 * For OFF mode: save context and jump to WFI in SDRAM (omap3_do_wfi)
	 * For non-OFF modes: jump to the WFI code in SRAM (omap3_do_wfi_sram)
	 */
	ldr	r4, omap3_do_wfi_sram_addr
	ldr	r5, [r4]
	cmp	r0, #0x0		@ If no context save required,
	bxeq	r5			@  jump to the WFI code in SRAM


	/* Otherwise fall through to the save context code */
save_context_wfi:
	/*
	 * jump out to kernel flush routine
	 *  - reuse that code is better
	 *  - it executes in a cached space so is faster than refetch per-block
	 *  - should be faster and will change with kernel
	 *  - 'might' have to copy address, load and jump to it
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	ldr	r1, kernel_flush
	mov	lr, pc
	bx	r1

	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)	@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	/*
	 * Invalidate L1 data cache. Even though only invalidate is
	 * necessary exported flush API is used here. Doing clean
	 * on already clean cache would be almost NOP.
	 */
	ldr	r1, kernel_flush
	blx	r1
	b	omap3_do_wfi
ENDPROC(omap34xx_cpu_suspend)
omap3_do_wfi_sram_addr:
	.word omap3_do_wfi_sram
kernel_flush:
	.word v7_flush_dcache_all

/* ===================================
 * == WFI instruction => Enter idle ==
 * ===================================
 */

/*
 * Do WFI instruction
 * Includes the resume path for non-OFF modes
 *
 * This code gets copied to internal SRAM and is accessible
 * from both SDRAM and SRAM:
 * - executed from SRAM for non-off modes (omap3_do_wfi_sram),
 * - executed from SDRAM for OFF mode (omap3_do_wfi).
 */
	.align	3
ENTRY(omap3_do_wfi)
	ldr	r4, sdrc_power		@ read the SDRC_POWER register
	ldr	r5, [r4]		@ read the contents of SDRC_POWER
	orr	r5, r5, #0x40		@ enable self refresh on idle req
	str	r5, [r4]		@ write back to SDRC_POWER register

	/* Data memory barrier and Data sync barrier */
	dsb
	dmb

/*
 * ===================================
 * == WFI instruction => Enter idle ==
 * ===================================
 */
	wfi				@ wait for interrupt

/*
 * ===================================
 * == Resume path for non-OFF modes ==
 * ===================================
 */
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

/*
 * This function implements the erratum ID i581 WA:
 *  SDRC state restore before accessing the SDRAM
 *
 * Only used at return from non-OFF mode. For OFF
 * mode the ROM code configures the SDRC and
 * the DPLL before calling the restore code directly
 * from DDR.
 */

/* Make sure SDRC accesses are ok */
wait_sdrc_ok:

/* DPLL3 must be locked before accessing the SDRC. Maybe the HW ensures this */
	ldr	r4, cm_idlest_ckgen
wait_dpll3_lock:
	ldr	r5, [r4]
	tst	r5, #1
	beq	wait_dpll3_lock

	ldr	r4, cm_idlest1_core
wait_sdrc_ready:
	ldr	r5, [r4]
	tst	r5, #0x2
	bne	wait_sdrc_ready
	/* allow DLL powerdown upon hw idle req */
	ldr	r4, sdrc_power
	ldr	r5, [r4]
	bic	r5, r5, #0x40
	str	r5, [r4]

is_dll_in_lock_mode:
	/* Is dll in lock mode? */
	ldr	r4, sdrc_dlla_ctrl
	ldr	r5, [r4]
	tst	r5, #0x4
	bne	exit_nonoff_modes	@ Return if locked
	/* wait till dll locks */
wait_dll_lock_timed:
	ldr	r4, sdrc_dlla_status
	/* Wait 20uS for lock */
	mov	r6, #8
wait_dll_lock:
	subs	r6, r6, #0x1
	beq	kick_dll
	ldr	r5, [r4]
	and	r5, r5, #0x4
	cmp	r5, #0x4
	bne	wait_dll_lock
	b	exit_nonoff_modes	@ Return when locked

	/* disable/reenable DLL if not locked */
kick_dll:
	ldr	r4, sdrc_dlla_ctrl
	ldr	r5, [r4]
	mov	r6, r5
	bic	r6, #(1<<3)		@ disable dll
	str	r6, [r4]
	dsb
	orr	r6, r6, #(1<<3)		@ enable dll
	str	r6, [r4]
	dsb
	b	wait_dll_lock_timed

exit_nonoff_modes:
	/* Re-enable C-bit if needed */
	mrc	p15, 0, r0, c1, c0, 0
	tst	r0, #(1 << 2)		@ Check C bit enabled?
	orreq	r0, r0, #(1 << 2)	@ Enable the C bit if cleared
	mcreq	p15, 0, r0, c1, c0, 0
	isb

/*
 * ===================================
 * == Exit point from non-OFF modes ==
 * ===================================
 */
	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(omap3_do_wfi)
sdrc_power:
	.word	SDRC_POWER_V
cm_idlest1_core:
	.word	CM_IDLEST1_CORE_V
cm_idlest_ckgen:
	.word	CM_IDLEST_CKGEN_V
sdrc_dlla_status:
	.word	SDRC_DLLA_STATUS_V
sdrc_dlla_ctrl:
	.word	SDRC_DLLA_CTRL_V
ENTRY(omap3_do_wfi_sz)
	.word	. - omap3_do_wfi


/*
 * ==============================
 * == Resume path for OFF mode ==
 * ==============================
 */

/*
 * The restore_* functions are called by the ROM code
 *  when back from WFI in OFF mode.
 * Cf. the get_*restore_pointer functions.
 *
 *  restore_es3: applies to 34xx >= ES3.0
 *  restore_3630: applies to 36xx
 *  restore: common code for 3xxx
 *
 * Note: when back from CORE and MPU OFF mode we are running
 *  from SDRAM, without MMU, without the caches and prediction.
 *  Also the SRAM content has been cleared.
 */
ENTRY(omap3_restore_es3)
	ldr	r5, pm_prepwstst_core_p
	ldr	r4, [r5]
	and	r4, r4, #0x3
	cmp	r4, #0x0	@ Check if previous power state of CORE is OFF
	bne	omap3_restore	@ Fall through to OMAP3 common code
	adr	r0, es3_sdrc_fix
	ldr	r1, sram_base
	ldr	r2, es3_sdrc_fix_sz
	mov	r2, r2, ror #2
copy_to_sram:
	ldmia	r0!, {r3}	@ val = *src
	stmia	r1!, {r3}	@ *dst = val
	subs	r2, r2, #0x1	@ num_words--
	bne	copy_to_sram
	ldr	r1, sram_base
	blx	r1
	b	omap3_restore	@ Fall through to OMAP3 common code
ENDPROC(omap3_restore_es3)

ENTRY(omap3_restore_3630)
	ldr	r1, pm_prepwstst_core_p
	ldr	r2, [r1]
	and	r2, r2, #0x3
	cmp	r2, #0x0	@ Check if previous power state of CORE is OFF
	bne	omap3_restore	@ Fall through to OMAP3 common code
	/* Disable RTA before giving control */
	ldr	r1, control_mem_rta
	mov	r2, #OMAP36XX_RTA_DISABLE
	str	r2, [r1]
ENDPROC(omap3_restore_3630)

	/* Fall through to common code for the remaining logic */

ENTRY(omap3_restore)
	/*
	 * Read the pwstctrl register to check the reason for mpu reset.
	 * This tells us what was lost.
	 */
	ldr	r1, pm_pwstctrl_mpu
	ldr	r2, [r1]
	and	r2, r2, #0x3
	cmp	r2, #0x0	@ Check if target power state was OFF or RET
	bne	logic_l1_restore

	adr	r1, l2dis_3630_offset	@ address for offset
	ldr	r0, [r1]		@ value for offset
	ldr	r0, [r1, r0]		@ value at l2dis_3630
	cmp	r0, #0x1	@ should we disable L2 on 3630?
	bne	skipl2dis
	mrc	p15, 0, r0, c1, c0, 1
	bic	r0, r0, #2	@ disable L2 cache
	mcr	p15, 0, r0, c1, c0, 1
skipl2dis:
	ldr	r0, control_stat
	ldr	r1, [r0]
	and	r1, #0x700
	cmp	r1, #0x300
	beq	l2_inv_gp
	adr	r0, l2_inv_api_params_offset
	ldr	r3, [r0]
	add	r3, r3, r0		@ r3 points to dummy parameters
	mov	r0, #40			@ set service ID for PPA
	mov	r12, r0			@ copy secure Service ID in r12
	mov	r1, #0			@ set task id for ROM code in r1
	mov	r2, #4			@ set some flags in r2, r6
	mov	r6, #0xff
	dsb				@ data write barrier
	dmb				@ data memory barrier
	smc	#1			@ call SMI monitor (smi #1)
	/* Write to Aux control register to set some bits */
	mov	r0, #42			@ set service ID for PPA
	mov	r12, r0			@ copy secure Service ID in r12
	mov	r1, #0			@ set task id for ROM code in r1
	mov	r2, #4			@ set some flags in r2, r6
	mov	r6, #0xff
	ldr	r4, scratchpad_base
	ldr	r3, [r4, #0xBC]		@ r3 points to parameters
	dsb				@ data write barrier
	dmb				@ data memory barrier
	smc	#1			@ call SMI monitor (smi #1)

#ifdef CONFIG_OMAP3_L2_AUX_SECURE_SAVE_RESTORE
	/* Restore L2 aux control register */
					@ set service ID for PPA
	mov	r0, #CONFIG_OMAP3_L2_AUX_SECURE_SERVICE_SET_ID
	mov	r12, r0			@ copy service ID in r12
	mov	r1, #0			@ set task ID for ROM code in r1
	mov	r2, #4			@ set some flags in r2, r6
	mov	r6, #0xff
	ldr	r4, scratchpad_base
	ldr	r3, [r4, #0xBC]
	adds	r3, r3, #8		@ r3 points to parameters
	dsb				@ data write barrier
	dmb				@ data memory barrier
	smc	#1			@ call SMI monitor (smi #1)
#endif
	b	logic_l1_restore

	.align
l2_inv_api_params_offset:
	.long	l2_inv_api_params - .
l2_inv_gp:
	/* Execute smi to invalidate L2 cache */
	mov r12, #0x1			@ set up to invalidate L2
	smc	#0			@ Call SMI monitor (smieq)
	/* Write to Aux control register to set some bits */
	ldr	r4, scratchpad_base
	ldr	r3, [r4,#0xBC]
	ldr	r0, [r3,#4]
	mov	r12, #0x3
	smc	#0			@ Call SMI monitor (smieq)
	ldr	r4, scratchpad_base
	ldr	r3, [r4,#0xBC]
	ldr	r0, [r3,#12]
	mov	r12, #0x2
	smc	#0			@ Call SMI monitor (smieq)
logic_l1_restore:
	adr	r0, l2dis_3630_offset	@ adress for offset
	ldr	r1, [r0]		@ value for offset
	ldr	r1, [r0, r1]		@ value at l2dis_3630
	cmp	r1, #0x1		@ Test if L2 re-enable needed on 3630
	bne	skipl2reen
	mrc	p15, 0, r1, c1, c0, 1
	orr	r1, r1, #2		@ re-enable L2 cache
	mcr	p15, 0, r1, c1, c0, 1
skipl2reen:

	/* Now branch to the common CPU resume function */
	b	cpu_resume
ENDPROC(omap3_restore)

	.ltorg

/*
 * Local variables
 */
pm_prepwstst_core_p:
	.word	PM_PREPWSTST_CORE_P
pm_pwstctrl_mpu:
	.word	PM_PWSTCTRL_MPU_P
scratchpad_base:
	.word	SCRATCHPAD_BASE_P
sram_base:
	.word	SRAM_BASE_P + 0x8000
control_stat:
	.word	CONTROL_STAT
control_mem_rta:
	.word	CONTROL_MEM_RTA_CTRL
l2dis_3630_offset:
	.long	l2dis_3630 - .

	.data
	.align	2
l2dis_3630:
	.word	0

	.data
	.align	2
l2_inv_api_params:
	.word	0x1, 0x00

/*
 * Internal functions
 */

/*
 * This function implements the erratum ID i443 WA, applies to 34xx >= ES3.0
 * Copied to and run from SRAM in order to reconfigure the SDRC parameters.
 */
	.text
	.align	3
ENTRY(es3_sdrc_fix)
	ldr	r4, sdrc_syscfg		@ get config addr
	ldr	r5, [r4]		@ get value
	tst	r5, #0x100		@ is part access blocked
	it	eq
	biceq	r5, r5, #0x100		@ clear bit if set
	str	r5, [r4]		@ write back change
	ldr	r4, sdrc_mr_0		@ get config addr
	ldr	r5, [r4]		@ get value
	str	r5, [r4]		@ write back change
	ldr	r4, sdrc_emr2_0		@ get config addr
	ldr	r5, [r4]		@ get value
	str	r5, [r4]		@ write back change
	ldr	r4, sdrc_manual_0	@ get config addr
	mov	r5, #0x2		@ autorefresh command
	str	r5, [r4]		@ kick off refreshes
	ldr	r4, sdrc_mr_1		@ get config addr
	ldr	r5, [r4]		@ get value
	str	r5, [r4]		@ write back change
	ldr	r4, sdrc_emr2_1		@ get config addr
	ldr	r5, [r4]		@ get value
	str	r5, [r4]		@ write back change
	ldr	r4, sdrc_manual_1	@ get config addr
	mov	r5, #0x2		@ autorefresh command
	str	r5, [r4]		@ kick off refreshes
	bx	lr

/*
 * Local variables
 */
	.align
sdrc_syscfg:
	.word	SDRC_SYSCONFIG_P
sdrc_mr_0:
	.word	SDRC_MR_0_P
sdrc_emr2_0:
	.word	SDRC_EMR2_0_P
sdrc_manual_0:
	.word	SDRC_MANUAL_0_P
sdrc_mr_1:
	.word	SDRC_MR_1_P
sdrc_emr2_1:
	.word	SDRC_EMR2_1_P
sdrc_manual_1:
	.word	SDRC_MANUAL_1_P
ENDPROC(es3_sdrc_fix)
ENTRY(es3_sdrc_fix_sz)
	.word	. - es3_sdrc_fix
