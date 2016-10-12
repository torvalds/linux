/*
 * Copyright 2014-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>
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
 *                      imx6_suspend code
 *              PM_INFO structure(imx6_cpu_pm_info)
 * ======================== low address =======================
 */

/*
 * Below offsets are based on struct imx6_cpu_pm_info
 * which defined in arch/arm/mach-imx/pm-imx6q.c, this
 * structure contains necessary pm info for low level
 * suspend related code.
 */
#define PM_INFO_PBASE_OFFSET			0x0
#define PM_INFO_RESUME_ADDR_OFFSET		0x4
#define PM_INFO_DDR_TYPE_OFFSET			0x8
#define PM_INFO_PM_INFO_SIZE_OFFSET		0xC
#define PM_INFO_MX6Q_MMDC0_P_OFFSET		0x10
#define PM_INFO_MX6Q_MMDC0_V_OFFSET		0x14
#define PM_INFO_MX6Q_MMDC1_P_OFFSET		0x18
#define PM_INFO_MX6Q_MMDC1_V_OFFSET		0x1C
#define PM_INFO_MX6Q_SRC_P_OFFSET		0x20
#define PM_INFO_MX6Q_SRC_V_OFFSET		0x24
#define PM_INFO_MX6Q_IOMUXC_P_OFFSET	0x28
#define PM_INFO_MX6Q_IOMUXC_V_OFFSET	0x2C
#define PM_INFO_MX6Q_CCM_P_OFFSET		0x30
#define PM_INFO_MX6Q_CCM_V_OFFSET		0x34
#define PM_INFO_MX6Q_GPC_P_OFFSET		0x38
#define PM_INFO_MX6Q_GPC_V_OFFSET		0x3C
#define PM_INFO_MX6Q_L2_P_OFFSET		0x40
#define PM_INFO_MX6Q_L2_V_OFFSET		0x44
#define PM_INFO_MX6Q_ANATOP_P_OFFSET	0x48
#define PM_INFO_MX6Q_ANATOP_V_OFFSET	0x4C
#define PM_INFO_MX6Q_TTBR1_V_OFFSET		0x50
#define PM_INFO_MMDC_IO_NUM_OFFSET		0x54
#define PM_INFO_MMDC_IO_VAL_OFFSET		0x58
/* below offsets depends on MX6_MAX_MMDC_IO_NUM(36) definition */
#define PM_INFO_MMDC_NUM_OFFSET			0x208
#define PM_INFO_MMDC_VAL_OFFSET			0x20C

#define MX6Q_SRC_GPR1	0x20
#define MX6Q_SRC_GPR2	0x24
#define MX6Q_MMDC_MISC	0x18
#define MX6Q_MMDC_MAPSR	0x404
#define MX6Q_MMDC_MPDGCTRL0	0x83c
#define MX6Q_GPC_IMR1	0x08
#define MX6Q_GPC_IMR2	0x0c
#define MX6Q_GPC_IMR3	0x10
#define MX6Q_GPC_IMR4	0x14
#define MX6Q_CCM_CCR	0x0
#define MX6Q_ANATOP_CORE	0x140

	.align 3

	/* Check if the cpu is cortex-a7 */
	.macro is_cortex_a7

	/* Read the primary cpu number is MPIDR */
	mrc     p15, 0, r5, c0, c0, 0
	ldr     r6, =0xfff0
	and     r5, r5, r6
	ldr     r6, =0xc070
	cmp     r5, r6

	.endm

	.macro  disable_l1_cache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push    {r0 - r10, lr}
	ldr     r7, =v7_flush_dcache_all
	mov     lr, pc
	mov     pc, r7
	pop     {r0 - r10, lr}

	/* disable d-cache */
	mrc     p15, 0, r7, c1, c0, 0
	bic     r7, r7, #(1 << 2)
	mcr     p15, 0, r7, c1, c0, 0
	dsb
	isb

	push    {r0 -r10, lr}
	ldr     r7, = v7_flush_dcache_all
	mov     lr, pc
	mov     pc , r7
	pop     {r0 -r10, lr}

	.endm

	.macro  sync_l2_cache

	/* sync L2 cache to drain L2's buffers to DRAM. */
#ifdef CONFIG_CACHE_L2X0
	ldr	r11, [r0, #PM_INFO_MX6Q_L2_V_OFFSET]
	mov	r6, #0x0
	str	r6, [r11, #L2X0_CACHE_SYNC]
1:
	ldr	r6, [r11, #L2X0_CACHE_SYNC]
	ands	r6, r6, #0x1
	bne	1b
#endif

	.endm

	/* r11 must be MMDC base address */
	.macro reset_read_fifo

	/* reset read FIFO, RST_RD_FIFO */
	ldr	r7, =MX6Q_MMDC_MPDGCTRL0
	ldr	r6, [r11, r7]
	orr     r6, r6, #(1 << 31)
	str	r6, [r11, r7]
2:
	ldr	r6, [r11, r7]
	ands	r6, r6, #(1 << 31)
	bne	2b

	/* reset FIFO a second time */
	ldr	r6, [r11, r7]
	orr     r6, r6, #(1 << 31)
	str	r6, [r11, r7]
3:
	ldr	r6, [r11, r7]
	ands	r6, r6, #(1 << 31)
	bne	3b

	/* check if lppdr2 2 channel mode is enabled */
	ldr	r7, =MX6Q_MMDC_MISC
	ldr	r6, [r11, r7]
	ands	r6, r6, #(1 << 2)
	beq	6f

	ldr	r7, =MX6Q_MMDC_MPDGCTRL0
	ldr	r6, [r12, r7]
	orr     r6, r6, #(1 << 31)
	str	r6, [r12, r7]
4:
	ldr	r6, [r12, r7]
	ands	r6, r6, #(1 << 31)
	bne	4b

	ldr	r6, [r12, r7]
	orr     r6, r6, #(1 << 31)
	str	r6, [r12, r7]
5:
	ldr	r6, [r12, r7]
	ands	r6, r6, #(1 << 31)
	bne	5b

6:
	.endm

	/* r11 must be MMDC base address */
	.macro mmdc_out_and_auto_self_refresh

	/* let DDR out of self-refresh */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #(1 << 21)
	str	r7, [r11, #MX6Q_MMDC_MAPSR]
7:
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	bne	7b

	/* enable DDR auto power saving */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #0x1
	str	r7, [r11, #MX6Q_MMDC_MAPSR]

	/* check if lppdr2 2 channel mode is enabled */
	ldr	r7, =MX6Q_MMDC_MISC
	ldr	r6, [r11, r7]
	ands	r6, r6, #(1 << 2)
	beq	9f

	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #(1 << 21)
	str	r7, [r12, #MX6Q_MMDC_MAPSR]
8:
	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	bne	8b

	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #0x1
	str	r7, [r12, #MX6Q_MMDC_MAPSR]
9:
	.endm

	/* r10 must be iomuxc base address */
	.macro resume_iomuxc_gpr

	add	r10, r10, #0x4000
	/* IOMUXC GPR DRAM_RESET_BYPASS */
	ldr	r4, [r10, #0x8]
	bic	r4, r4, #(0x1 << 27)
	str	r4, [r10, #0x8]
	/* IOMUXC GPR DRAM_CKE_BYPASS */
	ldr	r4, [r10, #0x8]
	bic	r4, r4, #(0x1 << 31)
	str	r4, [r10, #0x8]

	.endm

	.macro	resume_io

	/* restore MMDC IO */
	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_IOMUXC_P_OFFSET]

	ldr	r6, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r7, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r7, r7, r0
10:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x8
	str	r9, [r10, r8]
	subs	r6, r6, #0x1
	bne	10b

	cmp	r5, #0x0
	/* Here only MMDC0 is set */
	ldreq	r11, [r0, #PM_INFO_MX6Q_MMDC0_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX6Q_MMDC0_P_OFFSET]
	ldreq	r12, [r0, #PM_INFO_MX6Q_MMDC1_V_OFFSET]
	ldrne	r12, [r0, #PM_INFO_MX6Q_MMDC1_P_OFFSET]

	reset_read_fifo
	mmdc_out_and_auto_self_refresh

	.endm

	.macro	resume_mmdc_io

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_IOMUXC_P_OFFSET]
	ldreq	r11, [r0, #PM_INFO_MX6Q_MMDC0_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX6Q_MMDC0_P_OFFSET]

	/* resume mmdc iomuxc settings */
	ldr	r6, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r7, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r7, r7, r0
11:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x8
	str	r9, [r10, r8]
	subs	r6, r6, #0x1
	bne	11b

	/* check whether we need to restore MMDC */
	cmp	r5, #0x0
	beq	12f

	/* check whether last suspend is with M/F mix off */
	ldr	r9, [r0, #PM_INFO_MX6Q_GPC_P_OFFSET]
	ldr	r6, [r9, #0x220]
	cmp	r6, #0x0
	bne	13f
12:
	resume_iomuxc_gpr
	reset_read_fifo

	b	17f
13:
	/* restore MMDC settings */
	ldr	r6, [r0, #PM_INFO_MMDC_NUM_OFFSET]
	ldr	r7, =PM_INFO_MMDC_VAL_OFFSET
	add	r7, r7, r0
14:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x4
	str	r9, [r11, r8]
	subs	r6, r6, #0x1
	bne	14b

	/* let DDR enter self-refresh */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #(1 << 20)
	str	r7, [r11, #MX6Q_MMDC_MAPSR]
15:
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 24)
	beq	15b

	resume_iomuxc_gpr
	reset_read_fifo

	/* let DDR out of self-refresh */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #(1 << 20)
	str	r7, [r11, #MX6Q_MMDC_MAPSR]
16:
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 24)
	bne	16b

	/* kick off MMDC */
	ldr	r4, =0x0
	str	r4, [r11, #0x1c]

17:
	mmdc_out_and_auto_self_refresh

	.endm

	.macro store_ttbr1

	/* Store TTBR1 to pm_info->ttbr1 */
	mrc	p15, 0, r7, c2, c0, 1
	str	r7, [r0, #PM_INFO_MX6Q_TTBR1_V_OFFSET]

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

	/* Disable L1 data cache. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0

	dsb
	isb

	is_cortex_a7
	beq	17f

#ifdef CONFIG_CACHE_L2X0
	ldr	r8, [r0, #PM_INFO_MX6Q_L2_V_OFFSET]
	mov	r6, #0x0
	str	r6, [r8, #0x100]

	dsb
	isb
#endif
17:
	.endm

	.macro restore_ttbr1

	is_cortex_a7
	beq	18f

#ifdef CONFIG_CACHE_L2X0
	/* Enable L2. */
	ldr	r8, [r0, #PM_INFO_MX6Q_L2_V_OFFSET]
	ldr	r7, =0x1
	str	r7, [r8, #0x100]
#endif

18:
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
	ldr	r7, [r0, #PM_INFO_MX6Q_TTBR1_V_OFFSET]
	mcr	p15, 0, r7, c2, c0, 1

	.endm


ENTRY(imx6_suspend)
	ldr	r1, [r0, #PM_INFO_PBASE_OFFSET]
	ldr	r2, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	ldr	r3, [r0, #PM_INFO_DDR_TYPE_OFFSET]
	ldr	r4, [r0, #PM_INFO_PM_INFO_SIZE_OFFSET]

	/*
	 * counting the resume address in iram
	 * to set it in SRC register.
	 */
	ldr	r6, =imx6_suspend
	ldr	r7, =resume
	sub	r7, r7, r6
	add	r8, r1, r4
	add	r9, r8, r7

	/*
	 * make sure TLB contain the addr we want,
	 * as we will access them after MMDC IO floated.
	 */

	ldr	r11, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]
	ldr	r6, [r11, #0x0]
	ldr	r11, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	ldr	r6, [r11, #0x0]
	ldr	r11, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldr	r6, [r11, #0x0]

	/* use r11 to store the IO address */
	ldr	r11, [r0, #PM_INFO_MX6Q_SRC_V_OFFSET]
	/* store physical resume addr and pm_info address. */
	str	r9, [r11, #MX6Q_SRC_GPR1]
	str	r1, [r11, #MX6Q_SRC_GPR2]

	/*
	 * Check if the cpu is Cortex-A7, for Cortex-A7
	 * the cache implementation is not the same as
	 * Cortex-A9, so the cache maintenance operation
	 * is different.
	 */
	is_cortex_a7
	beq	a7_dache_flush

	/* need to sync L2 cache before DSM. */
	sync_l2_cache
	b	ttbr_store
a7_dache_flush:
	disable_l1_cache
ttbr_store:
	store_ttbr1

	ldr	r11, [r0, #PM_INFO_MX6Q_MMDC0_V_OFFSET]
	ldr	r12, [r0, #PM_INFO_MX6Q_MMDC1_V_OFFSET]
	/*
	 * put DDR explicitly into self-refresh and
	 * disable automatic power savings.
	 */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #0x1
	str	r7, [r11, #MX6Q_MMDC_MAPSR]

	/* make the DDR explicitly enter self-refresh. */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #(1 << 21)
	str	r7, [r11, #MX6Q_MMDC_MAPSR]

poll_dvfs_set:
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	beq	poll_dvfs_set

	/* check if lppdr2 2 channel mode is enabled */
	ldr	r7, =MX6Q_MMDC_MISC
	ldr	r6, [r11, r7]
	ands	r6, r6, #(1 << 2)
	beq	skip_self_refresh_ch1

	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #0x1
	str	r7, [r12, #MX6Q_MMDC_MAPSR]

	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #(1 << 21)
	str	r7, [r12, #MX6Q_MMDC_MAPSR]

poll_dvfs_set_ch1:
	ldr	r7, [r12, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	beq	poll_dvfs_set_ch1

skip_self_refresh_ch1:
	/* use r11 to store the IO address */
	ldr	r11, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldr	r6, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r8, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r8, r8, r0
set_mmdc_io_lpm:
	ldr	r7, [r8], #0x8
	ldr	r9, [r8], #0x4
	str	r9, [r11, r7]
	subs	r6, r6, #0x1
	bne	set_mmdc_io_lpm

	/* check whether it supports Mega/Fast off */
	ldr	r6, [r0, #PM_INFO_MMDC_NUM_OFFSET]
	cmp	r6, #0x0
	beq	set_mmdc_lpm_done

	/* IOMUXC GPR DRAM_RESET */
	add	r11, r11, #0x4000
	ldr	r6, [r11, #0x8]
	orr	r6, r6, #(0x1 << 28)
	str	r6, [r11, #0x8]

	/* IOMUXC GPR DRAM_RESET_BYPASS */
	ldr	r6, [r11, #0x8]
	orr	r6, r6, #(0x1 << 27)
	str	r6, [r11, #0x8]

	/* IOMUXC GPR DRAM_CKE_BYPASS */
	ldr	r6, [r11, #0x8]
	orr	r6, r6, #(0x1 << 31)
	str	r6, [r11, #0x8]
set_mmdc_lpm_done:

	/*
	 * mask all GPC interrupts before
	 * enabling the RBC counters to
	 * avoid the counter starting too
	 * early if an interupt is already
	 * pending.
	 */
	ldr	r11, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	ldr	r6, [r11, #MX6Q_GPC_IMR1]
	ldr	r7, [r11, #MX6Q_GPC_IMR2]
	ldr	r8, [r11, #MX6Q_GPC_IMR3]
	ldr	r9, [r11, #MX6Q_GPC_IMR4]

	ldr	r10, =0xffffffff
	str	r10, [r11, #MX6Q_GPC_IMR1]
	str	r10, [r11, #MX6Q_GPC_IMR2]
	str	r10, [r11, #MX6Q_GPC_IMR3]
	str	r10, [r11, #MX6Q_GPC_IMR4]

	/*
	 * enable the RBC bypass counter here
	 * to hold off the interrupts. RBC counter
	 * = 32 (1ms), Minimum RBC delay should be
	 * 400us for the analog LDOs to power down.
	 */
	ldr	r11, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]
	ldr	r10, [r11, #MX6Q_CCM_CCR]
	bic	r10, r10, #(0x3f << 21)
	orr	r10, r10, #(0x20 << 21)
	str	r10, [r11, #MX6Q_CCM_CCR]

	/* enable the counter. */
	ldr	r10, [r11, #MX6Q_CCM_CCR]
	orr	r10, r10, #(0x1 << 27)
	str	r10, [r11, #MX6Q_CCM_CCR]

	/* unmask all the GPC interrupts. */
	ldr	r11, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	str	r6, [r11, #MX6Q_GPC_IMR1]
	str	r7, [r11, #MX6Q_GPC_IMR2]
	str	r8, [r11, #MX6Q_GPC_IMR3]
	str	r9, [r11, #MX6Q_GPC_IMR4]

	/*
	 * now delay for a short while (3usec)
	 * ARM is at 1GHz at this point
	 * so a short loop should be enough.
	 * this delay is required to ensure that
	 * the RBC counter can start counting in
	 * case an interrupt is already pending
	 * or in case an interrupt arrives just
	 * as ARM is about to assert DSM_request.
	 */
	ldr	r6, =2000
rbc_loop:
	subs	r6, r6, #0x1
	bne	rbc_loop

	/*
	 * ERR005852 Analog: Transition from Deep Sleep Mode to
	 * LDO Bypass Mode may cause the slow response of the
	 * VDDARM_CAP output.
	 *
	 * Software workaround:
	 * if internal ldo(VDDARM) bypassed, switch to analog bypass
	 * mode (0x1E), prio to entering DSM, and then, revert to the
	 * normal bypass mode, when exiting from DSM.
	 */
	ldr	r11, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]
	ldr	r10, [r11, #MX6Q_ANATOP_CORE]
	and	r10, r10, #0x1f
	cmp	r10, #0x1f
	bne	ldo_check_done1
ldo_analog_bypass:
	ldr	r10, [r11, #MX6Q_ANATOP_CORE]
	bic	r10, r10, #0x1f
	orr	r10, r10, #0x1e
	str	r10, [r11, #MX6Q_ANATOP_CORE]
ldo_check_done1:
	/* Zzz, enter stop mode */
	wfi
	nop
	nop
	nop
	nop

	/*
	 * run to here means there is pending
	 * wakeup source, system should auto
	 * resume, we need to restore MMDC IO first
	 */
	/* restore it with 0x1f if use ldo bypass mode.*/
	ldr	r10, [r11, #MX6Q_ANATOP_CORE]
	and	r10, r10, #0x1f
	cmp	r10, #0x1e
	bne	ldo_check_done2
ldo_bypass_restore:
	ldr	r10, [r11, #MX6Q_ANATOP_CORE]
	orr	r10, r10, #0x1f
	str	r10, [r11, #MX6Q_ANATOP_CORE]
ldo_check_done2:
	mov	r5, #0x0
	/* check whether it supports Mega/Fast off */
	ldr	r6, [r0, #PM_INFO_MMDC_NUM_OFFSET]
	cmp	r6, #0x0
	beq	only_resume_io
	resume_mmdc_io
	b	resume_mmdc_done
only_resume_io:
	resume_io
resume_mmdc_done:

	restore_ttbr1

	/* return to suspend finish */
	ret	lr

resume:
	/* invalidate L1 I-cache first */
	mov     r6, #0x0
	mcr     p15, 0, r6, c7, c5, 0
	mcr     p15, 0, r6, c7, c5, 6
	/* enable the Icache and branch prediction */
	mov     r6, #0x1800
	mcr     p15, 0, r6, c1, c0, 0
	isb

	/* restore it with 0x1f if use ldo bypass mode.*/
	ldr	r11, [r0, #PM_INFO_MX6Q_ANATOP_P_OFFSET]
	ldr	r7, [r11, #MX6Q_ANATOP_CORE]
	and	r7, r7, #0x1f
	cmp	r7, #0x1e
	bne	ldo_check_done3
	ldr	r7, [r11, #MX6Q_ANATOP_CORE]
	orr	r7, r7, #0x1f
	str	r7, [r11, #MX6Q_ANATOP_CORE]
ldo_check_done3:
	/* get physical resume address from pm_info. */
	ldr	lr, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	/* clear core0's entry and parameter */
	ldr	r11, [r0, #PM_INFO_MX6Q_SRC_P_OFFSET]
	mov	r7, #0x0
	str	r7, [r11, #MX6Q_SRC_GPR1]
	str	r7, [r11, #MX6Q_SRC_GPR2]

	ldr	r3, [r0, #PM_INFO_DDR_TYPE_OFFSET]
	mov	r5, #0x1
	/* check whether it supports Mega/Fast off */
	ldr	r6, [r0, #PM_INFO_MMDC_NUM_OFFSET]
	cmp	r6, #0x0
	beq	dsm_only_resume_io
	resume_mmdc_io
	b	dsm_resume_mmdc_done
dsm_only_resume_io:
	ldr	r3, [r0, #PM_INFO_DDR_TYPE_OFFSET]
	resume_io
dsm_resume_mmdc_done:

	ret	lr
ENDPROC(imx6_suspend)

/*
 * The following code must assume it is running from physical address
 * where absolute virtual addresses to the data section have to be
 * turned into relative ones.
 */

ENTRY(v7_cpu_resume)
	bl	v7_invalidate_l1
	is_cortex_a7
	beq	done
#ifdef CONFIG_CACHE_L2X0
	bl	l2c310_early_resume
#endif
done:
	b	cpu_resume
ENDPROC(v7_cpu_resume)
