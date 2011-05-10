/*
 * (C) Copyright 2007
 * Texas Instruments
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * (C) Copyright 2004
 * Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <plat/sram.h>
#include <mach/io.h>

#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#include "sdrc.h"
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

/*
 * The "get_*restore_pointer" functions are used to provide a
 * physical restore address where the ROM code jumps while waking
 * up from MPU OFF/OSWR state.
 * The restore pointer is stored into the scratchpad.
 */

	.text
/* Function call to get the restore pointer for resume from OFF */
ENTRY(get_restore_pointer)
	stmfd	sp!, {lr}	@ save registers on stack
	adr	r0, restore
	ldmfd	sp!, {pc}	@ restore regs and return
ENDPROC(get_restore_pointer)
	.align
ENTRY(get_restore_pointer_sz)
	.word	. - get_restore_pointer

	.text
/* Function call to get the restore pointer for 3630 resume from OFF */
ENTRY(get_omap3630_restore_pointer)
	stmfd	sp!, {lr}	@ save registers on stack
	adr	r0, restore_3630
	ldmfd	sp!, {pc}	@ restore regs and return
ENDPROC(get_omap3630_restore_pointer)
	.align
ENTRY(get_omap3630_restore_pointer_sz)
	.word	. - get_omap3630_restore_pointer

	.text
/* Function call to get the restore pointer for ES3 to resume from OFF */
ENTRY(get_es3_restore_pointer)
	stmfd	sp!, {lr}	@ save registers on stack
	adr	r0, restore_es3
	ldmfd	sp!, {pc}	@ restore regs and return
ENDPROC(get_es3_restore_pointer)
	.align
ENTRY(get_es3_restore_pointer_sz)
	.word	. - get_es3_restore_pointer

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
	adrl	r2, l2dis_3630	@ may be too distant for plain adr
	str	r1, [r2]
	ldmfd	sp!, {pc}	@ restore regs and return
ENDPROC(enable_omap3630_toggle_l2_on_restore)

	.text
/* Function to call rom code to save secure ram context */
	.align	3
ENTRY(save_secure_ram_context)
	stmfd	sp!, {r1-r12, lr}	@ save registers on stack
	adr	r3, api_params		@ r3 points to parameters
	str	r0, [r3,#0x4]		@ r0 has sdram address
	ldr	r12, high_mask
	and	r3, r3, r12
	ldr	r12, sram_phy_addr_mask
	orr	r3, r3, r12
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
	ldmfd	sp!, {r1-r12, pc}
	.align
sram_phy_addr_mask:
	.word	SRAM_BASE_P
high_mask:
	.word	0xffff
api_params:
	.word	0x4, 0x0, 0x0, 0x1, 0x1
ENDPROC(save_secure_ram_context)
ENTRY(save_secure_ram_context_sz)
	.word	. - save_secure_ram_context

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
 * - this code gets copied to internal SRAM at boot and after wake-up
 *   from OFF mode. The execution pointer in SRAM is _omap_sram_idle.
 * - when the OMAP wakes up it continues at different execution points
 *   depending on the low power mode (non-OFF vs OFF modes),
 *   cf. 'Resume path for xxx mode' comments.
 */
	.align	3
ENTRY(omap34xx_cpu_suspend)
	stmfd	sp!, {r0-r12, lr}	@ save registers on stack

	/*
	 * r0 contains CPU context save/restore pointer in sdram
	 * r1 contains information about saving context:
	 *   0 - No context lost
	 *   1 - Only L1 and logic lost
	 *   2 - Only L2 lost (Even L1 is retained we clean it along with L2)
	 *   3 - Both L1 and L2 lost and logic lost
	 */

	/* Directly jump to WFI is the context save is not required */
	cmp	r1, #0x0
	beq	omap3_do_wfi

	/* Otherwise fall through to the save context code */
save_context_wfi:
	mov	r8, r0			@ Store SDRAM address in r8
	mrc	p15, 0, r5, c1, c0, 1	@ Read Auxiliary Control Register
	mov	r4, #0x1		@ Number of parameters for restore call
	stmia	r8!, {r4-r5}		@ Push parameters for restore call
	mrc	p15, 1, r5, c9, c0, 2	@ Read L2 AUX ctrl register
	stmia	r8!, {r4-r5}		@ Push parameters for restore call

        /* Check what that target sleep state is from r1 */
	cmp	r1, #0x2		@ Only L2 lost, no need to save context
	beq	clean_caches

l1_logic_lost:
	mov	r4, sp			@ Store sp
	mrs	r5, spsr		@ Store spsr
	mov	r6, lr			@ Store lr
	stmia	r8!, {r4-r6}

	mrc	p15, 0, r4, c1, c0, 2	@ Coprocessor access control register
	mrc	p15, 0, r5, c2, c0, 0	@ TTBR0
	mrc	p15, 0, r6, c2, c0, 1	@ TTBR1
	mrc	p15, 0, r7, c2, c0, 2	@ TTBCR
	stmia	r8!, {r4-r7}

	mrc	p15, 0, r4, c3, c0, 0	@ Domain access Control Register
	mrc	p15, 0, r5, c10, c2, 0	@ PRRR
	mrc	p15, 0, r6, c10, c2, 1	@ NMRR
	stmia	r8!,{r4-r6}

	mrc	p15, 0, r4, c13, c0, 1	@ Context ID
	mrc	p15, 0, r5, c13, c0, 2	@ User r/w thread and process ID
	mrc	p15, 0, r6, c12, c0, 0	@ Secure or NS vector base address
	mrs	r7, cpsr		@ Store current cpsr
	stmia	r8!, {r4-r7}

	mrc	p15, 0, r4, c1, c0, 0	@ save control register
	stmia	r8!, {r4}

clean_caches:
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
	/*
	 * The kernel doesn't interwork: v7_flush_dcache_all in particluar will
	 * always return in Thumb state when CONFIG_THUMB2_KERNEL is enabled.
	 * This sequence switches back to ARM.  Note that .align may insert a
	 * nop: bx pc needs to be word-aligned in order to work.
	 */
 THUMB(	.thumb		)
 THUMB(	.align		)
 THUMB(	bx	pc	)
 THUMB(	nop		)
	.arm

omap3_do_wfi:
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
	bl wait_sdrc_ok

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
	ldmfd	sp!, {r0-r12, pc}	@ restore regs and return


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
 */
restore_es3:
	ldr	r5, pm_prepwstst_core_p
	ldr	r4, [r5]
	and	r4, r4, #0x3
	cmp	r4, #0x0	@ Check if previous power state of CORE is OFF
	bne	restore
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
	b	restore

restore_3630:
	ldr	r1, pm_prepwstst_core_p
	ldr	r2, [r1]
	and	r2, r2, #0x3
	cmp	r2, #0x0	@ Check if previous power state of CORE is OFF
	bne	restore
	/* Disable RTA before giving control */
	ldr	r1, control_mem_rta
	mov	r2, #OMAP36XX_RTA_DISABLE
	str	r2, [r1]

	/* Fall through to common code for the remaining logic */

restore:
	/*
	 * Check what was the reason for mpu reset and store the reason in r9:
	 *  0 - No context lost
	 *  1 - Only L1 and logic lost
	 *  2 - Only L2 lost - In this case, we wont be here
	 *  3 - Both L1 and L2 lost
	 */
	ldr	r1, pm_pwstctrl_mpu
	ldr	r2, [r1]
	and	r2, r2, #0x3
	cmp	r2, #0x0	@ Check if target power state was OFF or RET
	moveq	r9, #0x3	@ MPU OFF => L1 and L2 lost
	movne	r9, #0x1	@ Only L1 and L2 lost => avoid L2 invalidation
	bne	logic_l1_restore

	ldr	r0, l2dis_3630
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
	mov	r0, #40			@ set service ID for PPA
	mov	r12, r0			@ copy secure Service ID in r12
	mov	r1, #0			@ set task id for ROM code in r1
	mov	r2, #4			@ set some flags in r2, r6
	mov	r6, #0xff
	adr	r3, l2_inv_api_params	@ r3 points to dummy parameters
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
l2_inv_api_params:
	.word	0x1, 0x00
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
	ldr	r1, l2dis_3630
	cmp	r1, #0x1		@ Test if L2 re-enable needed on 3630
	bne	skipl2reen
	mrc	p15, 0, r1, c1, c0, 1
	orr	r1, r1, #2		@ re-enable L2 cache
	mcr	p15, 0, r1, c1, c0, 1
skipl2reen:
	mov	r1, #0
	/*
	 * Invalidate all instruction caches to PoU
	 * and flush branch target cache
	 */
	mcr	p15, 0, r1, c7, c5, 0

	ldr	r4, scratchpad_base
	ldr	r3, [r4,#0xBC]
	adds	r3, r3, #16

	ldmia	r3!, {r4-r6}
	mov	sp, r4			@ Restore sp
	msr	spsr_cxsf, r5		@ Restore spsr
	mov	lr, r6			@ Restore lr

	ldmia	r3!, {r4-r7}
	mcr	p15, 0, r4, c1, c0, 2	@ Coprocessor access Control Register
	mcr	p15, 0, r5, c2, c0, 0	@ TTBR0
	mcr	p15, 0, r6, c2, c0, 1	@ TTBR1
	mcr	p15, 0, r7, c2, c0, 2	@ TTBCR

	ldmia	r3!,{r4-r6}
	mcr	p15, 0, r4, c3, c0, 0	@ Domain access Control Register
	mcr	p15, 0, r5, c10, c2, 0	@ PRRR
	mcr	p15, 0, r6, c10, c2, 1	@ NMRR


	ldmia	r3!,{r4-r7}
	mcr	p15, 0, r4, c13, c0, 1	@ Context ID
	mcr	p15, 0, r5, c13, c0, 2	@ User r/w thread and process ID
	mrc	p15, 0, r6, c12, c0, 0	@ Secure or NS vector base address
	msr	cpsr, r7		@ store cpsr

	/* Enabling MMU here */
	mrc	p15, 0, r7, c2, c0, 2 	@ Read TTBRControl
	/* Extract N (0:2) bits and decide whether to use TTBR0 or TTBR1 */
	and	r7, #0x7
	cmp	r7, #0x0
	beq	usettbr0
ttbr_error:
	/*
	 * More work needs to be done to support N[0:2] value other than 0
	 * So looping here so that the error can be detected
	 */
	b	ttbr_error
usettbr0:
	mrc	p15, 0, r2, c2, c0, 0
	ldr	r5, ttbrbit_mask
	and	r2, r5
	mov	r4, pc
	ldr	r5, table_index_mask
	and	r4, r5			@ r4 = 31 to 20 bits of pc
	/* Extract the value to be written to table entry */
	ldr	r1, table_entry
	/* r1 has the value to be written to table entry*/
	add	r1, r1, r4
	/* Getting the address of table entry to modify */
	lsr	r4, #18
	/* r2 has the location which needs to be modified */
	add	r2, r4
	/* Storing previous entry of location being modified */
	ldr	r5, scratchpad_base
	ldr	r4, [r2]
	str	r4, [r5, #0xC0]
	/* Modify the table entry */
	str	r1, [r2]
	/*
	 * Storing address of entry being modified
	 * - will be restored after enabling MMU
	 */
	ldr	r5, scratchpad_base
	str	r2, [r5, #0xC4]

	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 4	@ Flush prefetch buffer
	mcr	p15, 0, r0, c7, c5, 6	@ Invalidate branch predictor array
	mcr	p15, 0, r0, c8, c5, 0	@ Invalidate instruction TLB
	mcr	p15, 0, r0, c8, c6, 0	@ Invalidate data TLB
	/*
	 * Restore control register. This enables the MMU.
	 * The caches and prediction are not enabled here, they
	 * will be enabled after restoring the MMU table entry.
	 */
	ldmia	r3!, {r4}
	/* Store previous value of control register in scratchpad */
	str	r4, [r5, #0xC8]
	ldr	r2, cache_pred_disable_mask
	and	r4, r2
	mcr	p15, 0, r4, c1, c0, 0
	dsb
	isb
	ldr     r0, =restoremmu_on
	bx      r0

/*
 * ==============================
 * == Exit point from OFF mode ==
 * ==============================
 */
restoremmu_on:
	ldmfd	sp!, {r0-r12, pc}	@ restore regs and return


/*
 * Internal functions
 */

/* This function implements the erratum ID i443 WA, applies to 34xx >= ES3.0 */
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

/*
 * PC-relative stores lead to undefined behaviour in Thumb-2: use a r7 as a
 * base instead.
 * Be careful not to clobber r7 when maintaing this code.
 */

is_dll_in_lock_mode:
	/* Is dll in lock mode? */
	ldr	r4, sdrc_dlla_ctrl
	ldr	r5, [r4]
	tst	r5, #0x4
	bxne	lr			@ Return if locked
	/* wait till dll locks */
	adr	r7, kick_counter
wait_dll_lock_timed:
	ldr	r4, wait_dll_lock_counter
	add	r4, r4, #1
	str	r4, [r7, #wait_dll_lock_counter - kick_counter]
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
	bx	lr			@ Return when locked

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
	ldr	r4, kick_counter
	add	r4, r4, #1
	str	r4, [r7]		@ kick_counter
	b	wait_dll_lock_timed

	.align
cm_idlest1_core:
	.word	CM_IDLEST1_CORE_V
cm_idlest_ckgen:
	.word	CM_IDLEST_CKGEN_V
sdrc_dlla_status:
	.word	SDRC_DLLA_STATUS_V
sdrc_dlla_ctrl:
	.word	SDRC_DLLA_CTRL_V
pm_prepwstst_core_p:
	.word	PM_PREPWSTST_CORE_P
pm_pwstctrl_mpu:
	.word	PM_PWSTCTRL_MPU_P
scratchpad_base:
	.word	SCRATCHPAD_BASE_P
sram_base:
	.word	SRAM_BASE_P + 0x8000
sdrc_power:
	.word	SDRC_POWER_V
ttbrbit_mask:
	.word	0xFFFFC000
table_index_mask:
	.word	0xFFF00000
table_entry:
	.word	0x00000C02
cache_pred_disable_mask:
	.word	0xFFFFE7FB
control_stat:
	.word	CONTROL_STAT
control_mem_rta:
	.word	CONTROL_MEM_RTA_CTRL
kernel_flush:
	.word	v7_flush_dcache_all
l2dis_3630:
	.word	0
	/*
	 * When exporting to userspace while the counters are in SRAM,
	 * these 2 words need to be at the end to facilitate retrival!
	 */
kick_counter:
	.word	0
wait_dll_lock_counter:
	.word	0
ENDPROC(omap34xx_cpu_suspend)

ENTRY(omap34xx_cpu_suspend_sz)
	.word	. - omap34xx_cpu_suspend
