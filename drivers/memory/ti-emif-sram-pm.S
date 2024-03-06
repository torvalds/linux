/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Low level PM code for TI EMIF
 *
 * Copyright (C) 2016-2017 Texas Instruments Incorporated - http://www.ti.com/
 *	Dave Gerlach
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/page.h>

#include "emif.h"
#include "ti-emif-asm-offsets.h"

#define EMIF_POWER_MGMT_WAIT_SELF_REFRESH_8192_CYCLES	0x00a0
#define EMIF_POWER_MGMT_SR_TIMER_MASK			0x00f0
#define EMIF_POWER_MGMT_SELF_REFRESH_MODE		0x0200
#define EMIF_POWER_MGMT_SELF_REFRESH_MODE_MASK		0x0700

#define EMIF_SDCFG_TYPE_DDR2				0x2 << SDRAM_TYPE_SHIFT
#define EMIF_SDCFG_TYPE_DDR3				0x3 << SDRAM_TYPE_SHIFT
#define EMIF_STATUS_READY				0x4

#define AM43XX_EMIF_PHY_CTRL_REG_COUNT                  0x120

#define EMIF_AM437X_REGISTERS				0x1

	.arm
	.align 3
	.arch armv7-a

ENTRY(ti_emif_sram)

/*
 * void ti_emif_save_context(void)
 *
 * Used during suspend to save the context of all required EMIF registers
 * to local memory if the EMIF is going to lose context during the sleep
 * transition. Operates on the VIRTUAL address of the EMIF.
 */
ENTRY(ti_emif_save_context)
	stmfd   sp!, {r4 - r11, lr}     @ save registers on stack

	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_VIRT_OFFSET]
	ldr	r2, [r4, #EMIF_PM_REGS_VIRT_OFFSET]

	/* Save EMIF configuration */
	ldr	r1, [r0, #EMIF_SDRAM_CONFIG]
	str	r1, [r2, #EMIF_SDCFG_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_SDRAM_REFRESH_CONTROL]
	str	r1, [r2, #EMIF_REF_CTRL_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_SDRAM_TIMING_1]
	str     r1, [r2, #EMIF_TIMING1_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_SDRAM_TIMING_2]
	str     r1, [r2, #EMIF_TIMING2_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_SDRAM_TIMING_3]
	str     r1, [r2, #EMIF_TIMING3_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]
	str     r1, [r2, #EMIF_PMCR_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_POWER_MANAGEMENT_CTRL_SHDW]
	str     r1, [r2, #EMIF_PMCR_SHDW_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG]
	str     r1, [r2, #EMIF_ZQCFG_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_DDR_PHY_CTRL_1]
	str     r1, [r2, #EMIF_DDR_PHY_CTLR_1_OFFSET]

	ldr	r1, [r0, #EMIF_COS_CONFIG]
	str     r1, [r2, #EMIF_COS_CONFIG_OFFSET]

	ldr	r1, [r0, #EMIF_PRIORITY_TO_CLASS_OF_SERVICE_MAPPING]
	str     r1, [r2, #EMIF_PRIORITY_TO_COS_MAPPING_OFFSET]

	ldr	r1, [r0, #EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_1_MAPPING]
	str     r1, [r2, #EMIF_CONNECT_ID_SERV_1_MAP_OFFSET]

	ldr	r1, [r0, #EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_2_MAPPING]
	str     r1, [r2, #EMIF_CONNECT_ID_SERV_2_MAP_OFFSET]

	ldr	r1, [r0, #EMIF_OCP_CONFIG]
	str     r1, [r2, #EMIF_OCP_CONFIG_VAL_OFFSET]

	ldr	r5, [r4, #EMIF_PM_CONFIG_OFFSET]
	cmp	r5, #EMIF_SRAM_AM43_REG_LAYOUT
	bne	emif_skip_save_extra_regs

	ldr	r1, [r0, #EMIF_READ_WRITE_LEVELING_RAMP_CONTROL]
	str     r1, [r2, #EMIF_RD_WR_LEVEL_RAMP_CTRL_OFFSET]

	ldr	r1, [r0, #EMIF_READ_WRITE_EXECUTION_THRESHOLD]
	str     r1, [r2, #EMIF_RD_WR_EXEC_THRESH_OFFSET]

	ldr	r1, [r0, #EMIF_LPDDR2_NVM_TIMING]
	str     r1, [r2, #EMIF_LPDDR2_NVM_TIM_OFFSET]

	ldr	r1, [r0, #EMIF_LPDDR2_NVM_TIMING_SHDW]
	str     r1, [r2, #EMIF_LPDDR2_NVM_TIM_SHDW_OFFSET]

	ldr	r1, [r0, #EMIF_DLL_CALIB_CTRL]
	str     r1, [r2, #EMIF_DLL_CALIB_CTRL_VAL_OFFSET]

	ldr	r1, [r0, #EMIF_DLL_CALIB_CTRL_SHDW]
	str     r1, [r2, #EMIF_DLL_CALIB_CTRL_VAL_SHDW_OFFSET]

	/* Loop and save entire block of emif phy regs */
	mov	r5, #0x0
	add	r4, r2, #EMIF_EXT_PHY_CTRL_VALS_OFFSET
	add	r3, r0, #EMIF_EXT_PHY_CTRL_1
ddr_phy_ctrl_save:
	ldr	r1, [r3, r5]
	str	r1, [r4, r5]
	add	r5, r5, #0x4
	cmp	r5, #AM43XX_EMIF_PHY_CTRL_REG_COUNT
	bne	ddr_phy_ctrl_save

emif_skip_save_extra_regs:
	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(ti_emif_save_context)

/*
 * void ti_emif_restore_context(void)
 *
 * Used during resume to restore the context of all required EMIF registers
 * from local memory after the EMIF has lost context during a sleep transition.
 * Operates on the PHYSICAL address of the EMIF.
 */
ENTRY(ti_emif_restore_context)
	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_PHYS_OFFSET]
	ldr	r2, [r4, #EMIF_PM_REGS_PHYS_OFFSET]

	/* Config EMIF Timings */
	ldr     r1, [r2, #EMIF_DDR_PHY_CTLR_1_OFFSET]
	str	r1, [r0, #EMIF_DDR_PHY_CTRL_1]
	str	r1, [r0, #EMIF_DDR_PHY_CTRL_1_SHDW]

	ldr     r1, [r2, #EMIF_TIMING1_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_TIMING_1]
	str	r1, [r0, #EMIF_SDRAM_TIMING_1_SHDW]

	ldr     r1, [r2, #EMIF_TIMING2_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_TIMING_2]
	str	r1, [r0, #EMIF_SDRAM_TIMING_2_SHDW]

	ldr     r1, [r2, #EMIF_TIMING3_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_TIMING_3]
	str	r1, [r0, #EMIF_SDRAM_TIMING_3_SHDW]

	ldr     r1, [r2, #EMIF_REF_CTRL_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_REFRESH_CONTROL]
	str	r1, [r0, #EMIF_SDRAM_REFRESH_CTRL_SHDW]

	ldr     r1, [r2, #EMIF_PMCR_VAL_OFFSET]
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]

	ldr     r1, [r2, #EMIF_PMCR_SHDW_VAL_OFFSET]
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CTRL_SHDW]

	ldr     r1, [r2, #EMIF_COS_CONFIG_OFFSET]
	str	r1, [r0, #EMIF_COS_CONFIG]

	ldr     r1, [r2, #EMIF_PRIORITY_TO_COS_MAPPING_OFFSET]
	str	r1, [r0, #EMIF_PRIORITY_TO_CLASS_OF_SERVICE_MAPPING]

	ldr	r1, [r2, #EMIF_CONNECT_ID_SERV_1_MAP_OFFSET]
	str	r1, [r0, #EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_1_MAPPING]

	ldr     r1, [r2, #EMIF_CONNECT_ID_SERV_2_MAP_OFFSET]
	str	r1, [r0, #EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_2_MAPPING]

	ldr     r1, [r2, #EMIF_OCP_CONFIG_VAL_OFFSET]
	str	r1, [r0, #EMIF_OCP_CONFIG]

	ldr	r5, [r4, #EMIF_PM_CONFIG_OFFSET]
	cmp	r5, #EMIF_SRAM_AM43_REG_LAYOUT
	bne	emif_skip_restore_extra_regs

	ldr     r1, [r2, #EMIF_RD_WR_LEVEL_RAMP_CTRL_OFFSET]
	str	r1, [r0, #EMIF_READ_WRITE_LEVELING_RAMP_CONTROL]

	ldr     r1, [r2, #EMIF_RD_WR_EXEC_THRESH_OFFSET]
	str	r1, [r0, #EMIF_READ_WRITE_EXECUTION_THRESHOLD]

	ldr     r1, [r2, #EMIF_LPDDR2_NVM_TIM_OFFSET]
	str	r1, [r0, #EMIF_LPDDR2_NVM_TIMING]

	ldr     r1, [r2, #EMIF_LPDDR2_NVM_TIM_SHDW_OFFSET]
	str	r1, [r0, #EMIF_LPDDR2_NVM_TIMING_SHDW]

	ldr     r1, [r2, #EMIF_DLL_CALIB_CTRL_VAL_OFFSET]
	str	r1, [r0, #EMIF_DLL_CALIB_CTRL]

	ldr     r1, [r2, #EMIF_DLL_CALIB_CTRL_VAL_SHDW_OFFSET]
	str	r1, [r0, #EMIF_DLL_CALIB_CTRL_SHDW]

	ldr     r1, [r2, #EMIF_ZQCFG_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG]

	/* Loop and restore entire block of emif phy regs */
	mov	r5, #0x0
	/* Load ti_emif_regs_amx3 + EMIF_EXT_PHY_CTRL_VALS_OFFSET for address
	 * to phy register save space
	 */
	add	r3, r2, #EMIF_EXT_PHY_CTRL_VALS_OFFSET
	add	r4, r0, #EMIF_EXT_PHY_CTRL_1
ddr_phy_ctrl_restore:
	ldr	r1, [r3, r5]
	str	r1, [r4, r5]
	add	r5, r5, #0x4
	cmp	r5, #AM43XX_EMIF_PHY_CTRL_REG_COUNT
	bne	ddr_phy_ctrl_restore

emif_skip_restore_extra_regs:
	/*
	 * Output impedence calib needed only for DDR3
	 * but since the initial state of this will be
	 * disabled for DDR2 no harm in restoring the
	 * old configuration
	 */
	ldr     r1, [r2, #EMIF_ZQCFG_VAL_OFFSET]
	str	r1, [r0, #EMIF_SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG]

	/* Write to sdcfg last for DDR2 only */
	ldr	r1, [r2, #EMIF_SDCFG_VAL_OFFSET]
	and	r2, r1, #SDRAM_TYPE_MASK
	cmp	r2, #EMIF_SDCFG_TYPE_DDR2
	streq	r1, [r0, #EMIF_SDRAM_CONFIG]

	mov	pc, lr
ENDPROC(ti_emif_restore_context)

/*
 * void ti_emif_run_hw_leveling(void)
 *
 * Used during resume to run hardware leveling again and restore the
 * configuration of the EMIF PHY, only for DDR3.
 */
ENTRY(ti_emif_run_hw_leveling)
	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_PHYS_OFFSET]

	ldr	r3, [r0, #EMIF_READ_WRITE_LEVELING_CONTROL]
	orr	r3, r3, #RDWRLVLFULL_START
	ldr	r2, [r0, #EMIF_SDRAM_CONFIG]
	and	r2, r2, #SDRAM_TYPE_MASK
	cmp	r2, #EMIF_SDCFG_TYPE_DDR3
	bne	skip_hwlvl

	str	r3, [r0, #EMIF_READ_WRITE_LEVELING_CONTROL]

	/*
	 * If EMIF registers are touched during initial stage of HW
	 * leveling sequence there will be an L3 NOC timeout error issued
	 * as the EMIF will not respond, which is not fatal, but it is
	 * avoidable. This small wait loop is enough time for this condition
	 * to clear, even at worst case of CPU running at max speed of 1Ghz.
	 */
	mov	r2, #0x2000
1:
	subs	r2, r2, #0x1
	bne	1b

	/* Bit clears when operation is complete */
2:	ldr     r1, [r0, #EMIF_READ_WRITE_LEVELING_CONTROL]
	tst     r1, #RDWRLVLFULL_START
	bne     2b

skip_hwlvl:
	mov	pc, lr
ENDPROC(ti_emif_run_hw_leveling)

/*
 * void ti_emif_enter_sr(void)
 *
 * Programs the EMIF to tell the SDRAM to enter into self-refresh
 * mode during a sleep transition. Operates on the VIRTUAL address
 * of the EMIF.
 */
ENTRY(ti_emif_enter_sr)
	stmfd   sp!, {r4 - r11, lr}     @ save registers on stack

	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_VIRT_OFFSET]
	ldr	r2, [r4, #EMIF_PM_REGS_VIRT_OFFSET]

	ldr	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]
	bic	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE_MASK
	orr	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]

	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(ti_emif_enter_sr)

/*
 * void ti_emif_exit_sr(void)
 *
 * Programs the EMIF to tell the SDRAM to exit self-refresh mode
 * after a sleep transition. Operates on the PHYSICAL address of
 * the EMIF.
 */
ENTRY(ti_emif_exit_sr)
	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_PHYS_OFFSET]
	ldr	r2, [r4, #EMIF_PM_REGS_PHYS_OFFSET]

	/*
	 * Toggle EMIF to exit refresh mode:
	 * if EMIF lost context, PWR_MGT_CTRL is currently 0, writing disable
	 *   (0x0), wont do diddly squat! so do a toggle from SR(0x2) to disable
	 *   (0x0) here.
	 * *If* EMIF did not lose context, nothing broken as we write the same
	 *   value(0x2) to reg before we write a disable (0x0).
	 */
	ldr	r1, [r2, #EMIF_PMCR_VAL_OFFSET]
	bic	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE_MASK
	orr	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]
	bic	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE_MASK
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]

        /* Wait for EMIF to become ready */
1:	ldr     r1, [r0, #EMIF_STATUS]
	tst     r1, #EMIF_STATUS_READY
	beq     1b

	mov	pc, lr
ENDPROC(ti_emif_exit_sr)

/*
 * void ti_emif_abort_sr(void)
 *
 * Disables self-refresh after a failed transition to a low-power
 * state so the kernel can jump back to DDR and follow abort path.
 * Operates on the VIRTUAL address of the EMIF.
 */
ENTRY(ti_emif_abort_sr)
	stmfd   sp!, {r4 - r11, lr}     @ save registers on stack

	adr	r4, ti_emif_pm_sram_data
	ldr	r0, [r4, #EMIF_PM_BASE_ADDR_VIRT_OFFSET]
	ldr	r2, [r4, #EMIF_PM_REGS_VIRT_OFFSET]

	ldr	r1, [r2, #EMIF_PMCR_VAL_OFFSET]
	bic	r1, r1, #EMIF_POWER_MGMT_SELF_REFRESH_MODE_MASK
	str	r1, [r0, #EMIF_POWER_MANAGEMENT_CONTROL]

	/* Wait for EMIF to become ready */
1:	ldr     r1, [r0, #EMIF_STATUS]
	tst     r1, #EMIF_STATUS_READY
	beq     1b

	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(ti_emif_abort_sr)

	.align 3
ENTRY(ti_emif_pm_sram_data)
	.space EMIF_PM_DATA_SIZE
ENTRY(ti_emif_sram_sz)
        .word   . - ti_emif_save_context
