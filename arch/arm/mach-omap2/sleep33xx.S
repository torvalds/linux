/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Low level suspend code for AM33XX SoCs
 *
 * Copyright (C) 2012-2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Dave Gerlach, Vaibhav Bedia
 */

#include <generated/ti-emif-asm-offsets.h>
#include <generated/ti-pm-asm-offsets.h>
#include <linux/linkage.h>
#include <linux/ti-emif-sram.h>
#include <asm/assembler.h>
#include <asm/memory.h>

#include "iomap.h"
#include "cm33xx.h"

#define AM33XX_CM_CLKCTRL_MODULESTATE_DISABLED			0x00030000
#define AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE			0x0003
#define AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE			0x0002

	.arm
	.align 3

ENTRY(am33xx_do_wfi)
	stmfd	sp!, {r4 - r11, lr}	@ save registers on stack

	/*
	 * Flush all data from the L1 and L2 data cache before disabling
	 * SCTLR.C bit.
	 */
	ldr	r1, kernel_flush
	blx	r1

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
	 * Invalidate L1 and L2 data cache.
	 */
	ldr	r1, kernel_flush
	blx	r1

	adr	r9, am33xx_emif_sram_table

	ldr	r3, [r9, #EMIF_PM_ENTER_SR_OFFSET]
	blx	r3

	ldr	r3, [r9, #EMIF_PM_SAVE_CONTEXT_OFFSET]
	blx	r3

	/* Disable EMIF */
	ldr     r1, virt_emif_clkctrl
	ldr     r2, [r1]
	bic     r2, r2, #AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE
	str     r2, [r1]

	ldr	r1, virt_emif_clkctrl
wait_emif_disable:
	ldr	r2, [r1]
	mov	r3, #AM33XX_CM_CLKCTRL_MODULESTATE_DISABLED
	cmp	r2, r3
	bne	wait_emif_disable

	/*
	 * For the MPU WFI to be registered as an interrupt
	 * to WKUP_M3, MPU_CLKCTRL.MODULEMODE needs to be set
	 * to DISABLED
	 */
	ldr	r1, virt_mpu_clkctrl
	ldr	r2, [r1]
	bic	r2, r2, #AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE
	str	r2, [r1]

	/*
	 * Execute an ISB instruction to ensure that all of the
	 * CP15 register changes have been committed.
	 */
	isb

	/*
	 * Execute a barrier instruction to ensure that all cache,
	 * TLB and branch predictor maintenance operations issued
	 * have completed.
	 */
	dsb
	dmb

	/*
	 * Execute a WFI instruction and wait until the
	 * STANDBYWFI output is asserted to indicate that the
	 * CPU is in idle and low power state. CPU can specualatively
	 * prefetch the instructions so add NOPs after WFI. Thirteen
	 * NOPs as per Cortex-A8 pipeline.
	 */
	wfi

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
	nop
	nop
	nop

	/* We come here in case of an abort due to a late interrupt */

	/* Set MPU_CLKCTRL.MODULEMODE back to ENABLE */
	ldr	r1, virt_mpu_clkctrl
	mov	r2, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r2, [r1]

	/* Re-enable EMIF */
	ldr	r1, virt_emif_clkctrl
	mov	r2, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r2, [r1]
wait_emif_enable:
	ldr	r3, [r1]
	cmp	r2, r3
	bne	wait_emif_enable


	ldr	r1, [r9, #EMIF_PM_ABORT_SR_OFFSET]
	blx	r1

	/*
	 * Set SCTLR.C bit to allow data cache allocation
	 */
	mrc	p15, 0, r0, c1, c0, 0
	orr	r0, r0, #(1 << 2)	@ Enable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	/* Let the suspend code know about the abort */
	mov	r0, #1
	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(am33xx_do_wfi)

	.align
ENTRY(am33xx_resume_offset)
	.word . - am33xx_do_wfi

ENTRY(am33xx_resume_from_deep_sleep)
	/* Re-enable EMIF */
	ldr	r0, phys_emif_clkctrl
	mov	r1, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r1, [r0]
wait_emif_enable1:
	ldr	r2, [r0]
	cmp	r1, r2
	bne	wait_emif_enable1

	adr	r9, am33xx_emif_sram_table

	ldr	r1, [r9, #EMIF_PM_RESTORE_CONTEXT_OFFSET]
	blx	r1

	ldr	r1, [r9, #EMIF_PM_EXIT_SR_OFFSET]
	blx	r1

resume_to_ddr:
	/* We are back. Branch to the common CPU resume routine */
	mov	r0, #0
	ldr	pc, resume_addr
ENDPROC(am33xx_resume_from_deep_sleep)

/*
 * Local variables
 */
	.align
resume_addr:
	.word	cpu_resume - PAGE_OFFSET + 0x80000000
kernel_flush:
	.word   v7_flush_dcache_all
virt_mpu_clkctrl:
	.word	AM33XX_CM_MPU_MPU_CLKCTRL
virt_emif_clkctrl:
	.word	AM33XX_CM_PER_EMIF_CLKCTRL
phys_emif_clkctrl:
	.word	(AM33XX_CM_BASE + AM33XX_CM_PER_MOD + \
		AM33XX_CM_PER_EMIF_CLKCTRL_OFFSET)

.align 3
/* DDR related defines */
am33xx_emif_sram_table:
	.space EMIF_PM_FUNCTIONS_SIZE

ENTRY(am33xx_pm_sram)
	.word am33xx_do_wfi
	.word am33xx_do_wfi_sz
	.word am33xx_resume_offset
	.word am33xx_emif_sram_table
	.word am33xx_pm_ro_sram_data

.align 3
ENTRY(am33xx_pm_ro_sram_data)
	.space AMX3_PM_RO_SRAM_DATA_SIZE

ENTRY(am33xx_do_wfi_sz)
	.word	. - am33xx_do_wfi
