/*
 * arch/arm/mach-lpc32xx/suspend.S
 *
 * Original authors: Dmitry Chigirev, Vitaly Wool <source@mvista.com>
 * Modified by Kevin Wells <kevin.wells@nxp.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "lpc32xx.h"

/* Using named register defines makes the code easier to follow */
#define WORK1_REG			r0
#define WORK2_REG			r1
#define SAVED_HCLK_DIV_REG		r2
#define SAVED_HCLK_PLL_REG		r3
#define SAVED_DRAM_CLKCTRL_REG		r4
#define SAVED_PWR_CTRL_REG		r5
#define CLKPWRBASE_REG			r6
#define EMCBASE_REG			r7

#define LPC32XX_EMC_STATUS_OFFS		0x04
#define LPC32XX_EMC_STATUS_BUSY		0x1
#define LPC32XX_EMC_STATUS_SELF_RFSH	0x4

#define LPC32XX_CLKPWR_PWR_CTRL_OFFS	0x44
#define LPC32XX_CLKPWR_HCLK_DIV_OFFS	0x40
#define LPC32XX_CLKPWR_HCLKPLL_CTRL_OFFS 0x58

#define CLKPWR_PCLK_DIV_MASK		0xFFFFFE7F

	.text

ENTRY(lpc32xx_sys_suspend)
	@ Save a copy of the used registers in IRAM, r0 is corrupted
	adr	r0, tmp_stack_end
	stmfd	r0!, {r3 - r7, sp, lr}

	@ Load a few common register addresses
	adr	WORK1_REG, reg_bases
	ldr	CLKPWRBASE_REG, [WORK1_REG, #0]
	ldr	EMCBASE_REG, [WORK1_REG, #4]

	ldr	SAVED_PWR_CTRL_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_PWR_CTRL_OFFS]
	orr	WORK1_REG, SAVED_PWR_CTRL_REG, #LPC32XX_CLKPWR_SDRAM_SELF_RFSH

	@ Wait for SDRAM busy status to go busy and then idle
	@ This guarantees a small windows where DRAM isn't busy
1:
	ldr	WORK2_REG, [EMCBASE_REG, #LPC32XX_EMC_STATUS_OFFS]
	and	WORK2_REG, WORK2_REG, #LPC32XX_EMC_STATUS_BUSY
	cmp	WORK2_REG, #LPC32XX_EMC_STATUS_BUSY
	bne	1b @ Branch while idle
2:
	ldr	WORK2_REG, [EMCBASE_REG, #LPC32XX_EMC_STATUS_OFFS]
	and	WORK2_REG, WORK2_REG, #LPC32XX_EMC_STATUS_BUSY
	cmp	WORK2_REG, #LPC32XX_EMC_STATUS_BUSY
	beq	2b @ Branch until idle

	@ Setup self-refresh with support for manual exit of
	@ self-refresh mode
	str	WORK1_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]
	orr	WORK2_REG, WORK1_REG, #LPC32XX_CLKPWR_UPD_SDRAM_SELF_RFSH
	str	WORK2_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]
	str	WORK1_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]

	@ Wait for self-refresh acknowledge, clocks to the DRAM device
	@ will automatically stop on start of self-refresh
3:
	ldr	WORK2_REG, [EMCBASE_REG, #LPC32XX_EMC_STATUS_OFFS]
	and	WORK2_REG, WORK2_REG, #LPC32XX_EMC_STATUS_SELF_RFSH
	cmp	WORK2_REG, #LPC32XX_EMC_STATUS_SELF_RFSH
	bne	3b @ Branch until self-refresh mode starts

	@ Enter direct-run mode from run mode
	bic	WORK1_REG, WORK1_REG, #LPC32XX_CLKPWR_SELECT_RUN_MODE
	str	WORK1_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]

	@ Safe disable of DRAM clock in EMC block, prevents DDR sync
	@ issues on restart
	ldr	SAVED_HCLK_DIV_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_HCLK_DIV_OFFS]
	and	WORK2_REG, SAVED_HCLK_DIV_REG, #CLKPWR_PCLK_DIV_MASK
	str	WORK2_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_HCLK_DIV_OFFS]

	@ Save HCLK PLL state and disable HCLK PLL
	ldr	SAVED_HCLK_PLL_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_HCLKPLL_CTRL_OFFS]
	bic	WORK2_REG, SAVED_HCLK_PLL_REG, #LPC32XX_CLKPWR_HCLKPLL_POWER_UP
	str	WORK2_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_HCLKPLL_CTRL_OFFS]

	@ Enter stop mode until an enabled event occurs
	orr	WORK1_REG, WORK1_REG, #LPC32XX_CLKPWR_STOP_MODE_CTRL
	str	WORK1_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]
	.rept 9
	nop
	.endr

	@ Clear stop status
	bic	WORK1_REG, WORK1_REG, #LPC32XX_CLKPWR_STOP_MODE_CTRL

	@ Restore original HCLK PLL value and wait for PLL lock
	str	SAVED_HCLK_PLL_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_HCLKPLL_CTRL_OFFS]
4:
	ldr	WORK2_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_HCLKPLL_CTRL_OFFS]
	and	WORK2_REG, WORK2_REG, #LPC32XX_CLKPWR_HCLKPLL_PLL_STS
	bne	4b

	@ Re-enter run mode with self-refresh flag cleared, but no DRAM
	@ update yet. DRAM is still in self-refresh
	str	SAVED_PWR_CTRL_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_PWR_CTRL_OFFS]

	@ Restore original DRAM clock mode to restore DRAM clocks
	str	SAVED_HCLK_DIV_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_HCLK_DIV_OFFS]

	@ Clear self-refresh mode
	orr	WORK1_REG, SAVED_PWR_CTRL_REG,\
		#LPC32XX_CLKPWR_UPD_SDRAM_SELF_RFSH
	str	WORK1_REG, [CLKPWRBASE_REG, #LPC32XX_CLKPWR_PWR_CTRL_OFFS]
	str	SAVED_PWR_CTRL_REG, [CLKPWRBASE_REG,\
		#LPC32XX_CLKPWR_PWR_CTRL_OFFS]

	@ Wait for EMC to clear self-refresh mode
5:
	ldr	WORK2_REG, [EMCBASE_REG, #LPC32XX_EMC_STATUS_OFFS]
	and	WORK2_REG, WORK2_REG, #LPC32XX_EMC_STATUS_SELF_RFSH
	bne	5b @ Branch until self-refresh has exited

	@ restore regs and return
	adr	r0, tmp_stack
	ldmfd	r0!, {r3 - r7, sp, pc}

reg_bases:
	.long	IO_ADDRESS(LPC32XX_CLK_PM_BASE)
	.long	IO_ADDRESS(LPC32XX_EMC_BASE)

tmp_stack:
	.long	0, 0, 0, 0, 0, 0, 0
tmp_stack_end:

ENTRY(lpc32xx_sys_suspend_sz)
	.word	. - lpc32xx_sys_suspend
