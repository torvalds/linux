/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include "hardware.h"

/*
 * ==================== low level suspend ====================
 *
 * Better to follow below rules to use ARM registers:
 * r0: pm_info structure address;
 * r1 ~ r4: for saving pm_info members;
 * r5 ~ r10: free registers;
 * r11: io base address.
 *
 * suspend ocram space layout:
 * ======================== high address ======================
 *                              .
 *                              .
 *                              .
 *                              ^
 *                              ^
 *                              ^
 *                      imx7_suspend code
 *              PM_INFO structure(imx7_cpu_pm_info)
 * ======================== low address =======================
 */

/*
 * Below offsets are based on struct imx7_cpu_pm_info
 * which defined in arch/arm/mach-imx/pm-imx7.c, this
 * structure contains necessary pm info for low level
 * suspend related code.
 */
#define PM_INFO_M4_RESERVE0_OFFSET		0x0
#define PM_INFO_M4_RESERVE1_OFFSET		0x4
#define PM_INFO_M4_RESERVE2_OFFSET		0x8
#define PM_INFO_PBASE_OFFSET			0xc
#define PM_INFO_RESUME_ADDR_OFFSET		0x10
#define PM_INFO_DDR_TYPE_OFFSET			0x14
#define PM_INFO_PM_INFO_SIZE_OFFSET		0x18
#define PM_INFO_MX7_DDRC_P_OFFSET		0x1c
#define PM_INFO_MX7_DDRC_V_OFFSET		0x20
#define PM_INFO_MX7_DDRC_PHY_P_OFFSET		0x24
#define PM_INFO_MX7_DDRC_PHY_V_OFFSET		0x28
#define PM_INFO_MX7_SRC_P_OFFSET		0x2c
#define PM_INFO_MX7_SRC_V_OFFSET		0x30
#define PM_INFO_MX7_IOMUXC_GPR_P_OFFSET		0x34
#define PM_INFO_MX7_IOMUXC_GPR_V_OFFSET		0x38
#define PM_INFO_MX7_CCM_P_OFFSET		0x3c
#define PM_INFO_MX7_CCM_V_OFFSET		0x40
#define PM_INFO_MX7_GPC_P_OFFSET		0x44
#define PM_INFO_MX7_GPC_V_OFFSET		0x48
#define PM_INFO_MX7_SNVS_P_OFFSET		0x4c
#define PM_INFO_MX7_SNVS_V_OFFSET		0x50
#define PM_INFO_MX7_ANATOP_P_OFFSET		0x54
#define PM_INFO_MX7_ANATOP_V_OFFSET		0x58
#define PM_INFO_MX7_LPSR_P_OFFSET		0x5c
#define PM_INFO_MX7_LPSR_V_OFFSET		0x60
#define PM_INFO_MX7_TTBR1_V_OFFSET		0x64
#define PM_INFO_DDRC_REG_NUM_OFFSET		0x68
#define PM_INFO_DDRC_REG_OFFSET			0x6c
#define PM_INFO_DDRC_VALUE_OFFSET		0x70
#define PM_INFO_DDRC_PHY_REG_NUM_OFFSET		0x16c
#define PM_INFO_DDRC_PHY_REG_OFFSET		0x170
#define PM_INFO_DDRC_PHY_VALUE_OFFSET		0x174

#define MX7_SRC_GPR1	0x74
#define MX7_SRC_GPR2	0x78
#define GPC_PGC_C0	0x800
#define GPC_PGC_FM	0xa00
#define ANADIG_SNVS_MISC_CTRL	0x380
#define DDRC_STAT	0x4
#define DDRC_PWRCTL	0x30
#define DDRC_PSTAT	0x3fc
#define DDRC_PCTRL_0	0x490
#define DDRC_DFIMISC	0x1b0
#define DDRC_SWCTL	0x320
#define DDRC_SWSTAT	0x324
#define DDRPHY_LP_CON0	0x18

	.align 3

	.macro	disable_l1_dcache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	/* disable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	bic	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0
	dsb
	isb

	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	.endm

	.macro store_ttbr1

	/* Store TTBR1 to pm_info->ttbr1 */
	mrc	p15, 0, r7, c2, c0, 1
	str	r7, [r0, #PM_INFO_MX7_TTBR1_V_OFFSET]

	/* Disable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	/* Flush the BTAC. */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

	ldr	r6, =iram_tlb_phys_addr
	ldr	r6, [r6]
	dsb
	isb

	/* Store the IRAM table in TTBR1 */
	mcr	p15, 0, r6, c2, c0, 1
	/* Read TTBCR and set PD0=1, N = 1 */
	mrc	p15, 0, r6, c2, c0, 2
	orr	r6, r6, #0x11
	mcr	p15, 0, r6, c2, c0, 2

	dsb
	isb

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

	.endm

	.macro restore_ttbr1

	/* Enable L1 data cache. */
	mrc	p15, 0, r6, c1, c0, 0
	orr	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0

	dsb
	isb

	/* Restore TTBCR */
	/* Read TTBCR and set PD0=0, N = 0 */
	mrc	p15, 0, r6, c2, c0, 2
	bic	r6, r6, #0x11
	mcr	p15, 0, r6, c2, c0, 2
	dsb
	isb

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

	/* Enable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	orr	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

	/* Restore TTBR1, get the origin ttbr1 from pm info */
	ldr	r7, [r0, #PM_INFO_MX7_TTBR1_V_OFFSET]
	mcr	p15, 0, r7, c2, c0, 1

	.endm

	.macro ddrc_enter_self_refresh

	ldr	r11, [r0, #PM_INFO_MX7_DDRC_V_OFFSET]

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r11, #DDRC_PWRCTL]

	/* wait rw port_busy clear */
	ldr	r6, =(0x1 << 16)
	orr	r6, r6, #0x1
1:
	ldr	r7, [r11, #DDRC_PSTAT]
	ands	r7, r7, r6
	bne	1b

	/* enter self-refresh bit 5 */
	ldr	r7, =(0x1 << 5)
	str	r7, [r11, #DDRC_PWRCTL]

	/* wait until self-refresh mode entered */
2:
	ldr	r7, [r11, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	bne	2b
3:
	ldr	r7, [r11, #DDRC_STAT]
	ands	r7, r7, #0x20
	beq	3b

	/* disable dram clk */
	ldr	r7, [r11, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 3)
	str	r7, [r11, #DDRC_PWRCTL]

	.endm

	.macro ddrc_exit_self_refresh

	cmp	r5, #0x0
	ldreq	r11, [r0, #PM_INFO_MX7_DDRC_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX7_DDRC_P_OFFSET]

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r11, #DDRC_PWRCTL]

	/* wait until self-refresh mode entered */
4:
	ldr	r7, [r11, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	beq	4b

	/* enable auto self-refresh */
	ldr	r7, [r11, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 0)
	str	r7, [r11, #DDRC_PWRCTL]

	.endm

	.macro wait_delay
5:
	subs	r6, r6, #0x1
	bne	5b

	.endm

	.macro ddr_enter_retention

	ldr	r11, [r0, #PM_INFO_MX7_DDRC_V_OFFSET]

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r11, #DDRC_PCTRL_0]

	/* wait rw port_busy clear */
	ldr	r6, =(0x1 << 16)
	orr	r6, r6, #0x1
6:
	ldr	r7, [r11, #DDRC_PSTAT]
	ands	r7, r7, r6
	bne	6b

	ldr	r11, [r0, #PM_INFO_MX7_DDRC_V_OFFSET]
	/* enter self-refresh bit 5 */
	ldr	r7, =(0x1 << 5)
	str	r7, [r11, #DDRC_PWRCTL]

	/* wait until self-refresh mode entered */
7:
	ldr	r7, [r11, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	bne	7b
8:
	ldr	r7, [r11, #DDRC_STAT]
	ands	r7, r7, #0x20
	beq	8b

	/* disable dram clk */
	ldr	r7, =(0x1 << 5)
	orr	r7, r7, #(1 << 3)
	str	r7, [r11, #DDRC_PWRCTL]

	/* reset ddr_phy  */
	ldr	r11, [r0, #PM_INFO_MX7_ANATOP_V_OFFSET]
	ldr	r7, =0x0
	str	r7, [r11, #ANADIG_SNVS_MISC_CTRL]

	/* delay 7 us */
	ldr	r6, =6000
	wait_delay

	ldr	r11, [r0, #PM_INFO_MX7_SRC_V_OFFSET]
	ldr	r6, =0x1000
	ldr	r7, [r11, r6]
	orr	r7, r7, #0x1
	str	r7, [r11, r6]
	/* turn off ddr power */
	ldr	r11, [r0, #PM_INFO_MX7_ANATOP_V_OFFSET]
	ldr	r7, =(0x1 << 29)
	str	r7, [r11, #ANADIG_SNVS_MISC_CTRL]

	.endm

	.macro ddr_exit_retention

	cmp	r5, #0x0
	ldreq	r1, [r0, #PM_INFO_MX7_ANATOP_V_OFFSET]
	ldrne	r1, [r0, #PM_INFO_MX7_ANATOP_P_OFFSET]
	ldreq	r2, [r0, #PM_INFO_MX7_SRC_V_OFFSET]
	ldrne	r2, [r0, #PM_INFO_MX7_SRC_P_OFFSET]
	ldreq	r3, [r0, #PM_INFO_MX7_DDRC_V_OFFSET]
	ldrne	r3, [r0, #PM_INFO_MX7_DDRC_P_OFFSET]
	ldreq	r4, [r0, #PM_INFO_MX7_DDRC_PHY_V_OFFSET]
	ldrne	r4, [r0, #PM_INFO_MX7_DDRC_PHY_P_OFFSET]
	ldreq	r10, [r0, #PM_INFO_MX7_CCM_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7_CCM_P_OFFSET]
	ldreq	r11, [r0, #PM_INFO_MX7_IOMUXC_GPR_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX7_IOMUXC_GPR_P_OFFSET]

	/* turn on ddr power */
	ldr	r7, =(0x1 << 29)
	str	r7, [r1, #ANADIG_SNVS_MISC_CTRL]

	ldr	r6, =50
	wait_delay

	ldr	r7, =0x0
	str	r7, [r1, #ANADIG_SNVS_MISC_CTRL]

	/* clear ddr_phy reset */
	ldr	r6, =0x1000
	ldr	r7, [r2, r6]
	orr	r7, r7, #0x3
	str	r7, [r2, r6]
	ldr	r7, [r2, r6]
	bic	r7, r7, #0x1
	str	r7, [r2, r6]

	ldr	r6, [r0, #PM_INFO_DDRC_REG_NUM_OFFSET]
	ldr	r7, =PM_INFO_DDRC_REG_OFFSET
	add	r7, r7, r0
9:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x4
	str	r9, [r3, r8]
	subs	r6, r6, #0x1
	bne	9b
	ldr	r7, =0x20
	str	r7, [r3, #DDRC_PWRCTL]
	ldr	r7, =0x0
	str	r7, [r3, #DDRC_DFIMISC]

	/* do PHY, clear ddr_phy reset */
	ldr	r6, =0x1000
	ldr	r7, [r2, r6]
	bic	r7, r7, #0x2
	str	r7, [r2, r6]

	ldr	r7, =(0x1 << 30)
	str	r7, [r1, #ANADIG_SNVS_MISC_CTRL]

	/* need to delay ~5mS */
	ldr	r6, =0x100000
	wait_delay

	ldr	r6, [r0, #PM_INFO_DDRC_PHY_REG_NUM_OFFSET]
	ldr	r7, =PM_INFO_DDRC_PHY_REG_OFFSET
	add	r7, r7, r0

10:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x4
	str	r9, [r4, r8]
	subs	r6, r6, #0x1
	bne	10b

	ldr	r7, =0x0
	add	r9, r10, #0x4000
	str	r7, [r9, #0x130]

	ldr	r7, =0x170
	orr	r7, r7, #0x8
	str	r7, [r11, #0x20]

	ldr	r7, =0x2
	add	r9, r10, #0x4000
	str	r7, [r9, #0x130]

	ldr	r7, =0xf
	str	r7, [r4, #DDRPHY_LP_CON0]

	/* wait until self-refresh mode entered */
11:
	ldr	r7, [r3, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	bne	11b
	ldr	r7, =0x0
	str	r7, [r3, #DDRC_SWCTL]
	ldr	r7, =0x1
	str	r7, [r3, #DDRC_DFIMISC]
	ldr	r7, =0x1
	str	r7, [r3, #DDRC_SWCTL]
12:
	ldr	r7, [r3, #DDRC_SWSTAT]
	and	r7, r7, #0x1
	cmp	r7, #0x1
	bne	12b
13:
	ldr	r7, [r3, #DDRC_STAT]
	and	r7, r7, #0x20
	cmp	r7, #0x20
	bne	13b

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r3, #DDRC_PWRCTL]
14:
	ldr	r7, [r3, #DDRC_STAT]
	and	r7, r7, #0x30
	cmp	r7, #0x0
	bne	14b

15:
	ldr	r7, [r3, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x1
	bne	15b

	/* enable port */
	ldr	r7, =0x1
	str	r7, [r3, #DDRC_PCTRL_0]

	.endm

ENTRY(imx7_suspend)
	push	{r4-r12}

	/* check whether it is a standby mode */
	ldr	r11, [r0, #PM_INFO_MX7_GPC_V_OFFSET]
	ldr	r7, [r11, #GPC_PGC_C0]
	cmp	r7, #0
	beq	ddr_only_self_refresh

	/*
	 * The value of r0 is mapped the same in origin table and IRAM table,
	 * thus no need to care r0 here.
	 */
	ldr	r1, [r0, #PM_INFO_PBASE_OFFSET]
	ldr	r2, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	ldr	r3, [r0, #PM_INFO_DDR_TYPE_OFFSET]
	ldr	r4, [r0, #PM_INFO_PM_INFO_SIZE_OFFSET]

	/*
	 * counting the resume address in iram
	 * to set it in SRC register.
	 */
	ldr	r6, =imx7_suspend
	ldr	r7, =resume
	sub	r7, r7, r6
	add	r8, r1, r4
	add	r9, r8, r7

	ldr	r11, [r0, #PM_INFO_MX7_SRC_V_OFFSET]
	/* store physical resume addr and pm_info address. */
	str	r9, [r11, #MX7_SRC_GPR1]
	str	r1, [r11, #MX7_SRC_GPR2]

	disable_l1_dcache

	store_ttbr1

	ldr	r11, [r0, #PM_INFO_MX7_GPC_V_OFFSET]
	ldr	r7, [r11, #GPC_PGC_FM]
	cmp	r7, #0
	beq	ddr_only_self_refresh

	ddr_enter_retention
	/* enter LPSR mode if resume addr is valid */
	ldr	r11, [r0, #PM_INFO_MX7_LPSR_V_OFFSET]
	ldr	r7, [r11]
	cmp	r7, #0x0
	beq	ddr_retention_enter_out

	/* shut down vddsoc to enter lpsr mode */
	ldr	r11, [r0, #PM_INFO_MX7_SNVS_V_OFFSET]
	ldr	r7, [r11, #0x38]
	orr	r7, r7, #0x60
	str	r7, [r11, #0x38]
wait_shutdown:
	b	wait_shutdown

ddr_only_self_refresh:
	ddrc_enter_self_refresh
ddr_retention_enter_out:

	/* Zzz, enter stop mode */
	wfi
	nop
	nop
	nop
	nop

	mov	r5, #0x0

	ldr	r11, [r0, #PM_INFO_MX7_GPC_V_OFFSET]
	ldr	r7, [r11, #GPC_PGC_FM]
	cmp	r7, #0
	beq	wfi_ddr_self_refresh_out

	ddr_exit_retention
	b	wfi_ddr_retention_out
wfi_ddr_self_refresh_out:
	ddrc_exit_self_refresh
wfi_ddr_retention_out:

	/* check whether it is a standby mode */
	ldr	r11, [r0, #PM_INFO_MX7_GPC_V_OFFSET]
	ldr	r7, [r11, #GPC_PGC_C0]
	cmp	r7, #0
	beq	standby_out

	restore_ttbr1
standby_out:
	pop	{r4-r12}
	/* return to suspend finish */
	mov	pc, lr

resume:
	/* invalidate L1 I-cache first */
	mov     r6, #0x0
	mcr     p15, 0, r6, c7, c5, 0
	mcr     p15, 0, r6, c7, c5, 6
	/* enable the Icache and branch prediction */
	mov     r6, #0x1800
	mcr     p15, 0, r6, c1, c0, 0
	isb

	/* get physical resume address from pm_info. */
	ldr	lr, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	/* clear core0's entry and parameter */
	ldr	r11, [r0, #PM_INFO_MX7_SRC_P_OFFSET]
	mov	r7, #0x0
	str	r7, [r11, #MX7_SRC_GPR1]
	str	r7, [r11, #MX7_SRC_GPR2]

	mov	r5, #0x1

	ldr	r11, [r0, #PM_INFO_MX7_GPC_P_OFFSET]
	ldr	r7, [r11, #GPC_PGC_FM]
	cmp	r7, #0
	beq	dsm_ddr_self_refresh_out

	ddr_exit_retention
	b	dsm_ddr_retention_out
dsm_ddr_self_refresh_out:
	ddrc_exit_self_refresh
dsm_ddr_retention_out:

	mov	pc, lr
ENDPROC(imx7_suspend)

ENTRY(ca7_cpu_resume)
	bl	v7_invalidate_l1
	b	cpu_resume
ENDPROC(ca7_cpu_resume)
