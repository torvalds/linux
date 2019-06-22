/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2014 Freescale Semiconductor, Inc.
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
#define PM_INFO_MX6Q_MMDC_P_OFFSET		0x10
#define PM_INFO_MX6Q_MMDC_V_OFFSET		0x14
#define PM_INFO_MX6Q_SRC_P_OFFSET		0x18
#define PM_INFO_MX6Q_SRC_V_OFFSET		0x1C
#define PM_INFO_MX6Q_IOMUXC_P_OFFSET		0x20
#define PM_INFO_MX6Q_IOMUXC_V_OFFSET		0x24
#define PM_INFO_MX6Q_CCM_P_OFFSET		0x28
#define PM_INFO_MX6Q_CCM_V_OFFSET		0x2C
#define PM_INFO_MX6Q_GPC_P_OFFSET		0x30
#define PM_INFO_MX6Q_GPC_V_OFFSET		0x34
#define PM_INFO_MX6Q_L2_P_OFFSET		0x38
#define PM_INFO_MX6Q_L2_V_OFFSET		0x3C
#define PM_INFO_MMDC_IO_NUM_OFFSET		0x40
#define PM_INFO_MMDC_IO_VAL_OFFSET		0x44

#define MX6Q_SRC_GPR1	0x20
#define MX6Q_SRC_GPR2	0x24
#define MX6Q_MMDC_MAPSR	0x404
#define MX6Q_MMDC_MPDGCTRL0	0x83c
#define MX6Q_GPC_IMR1	0x08
#define MX6Q_GPC_IMR2	0x0c
#define MX6Q_GPC_IMR3	0x10
#define MX6Q_GPC_IMR4	0x14
#define MX6Q_CCM_CCR	0x0

	.align 3

	.macro  sync_l2_cache

	/* sync L2 cache to drain L2's buffers to DRAM. */
#ifdef CONFIG_CACHE_L2X0
	ldr	r11, [r0, #PM_INFO_MX6Q_L2_V_OFFSET]
	teq	r11, #0
	beq	6f
	mov	r6, #0x0
	str	r6, [r11, #L2X0_CACHE_SYNC]
1:
	ldr	r6, [r11, #L2X0_CACHE_SYNC]
	ands	r6, r6, #0x1
	bne	1b
6:
#endif

	.endm

	.macro	resume_mmdc

	/* restore MMDC IO */
	cmp	r5, #0x0
	ldreq	r11, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX6Q_IOMUXC_P_OFFSET]

	ldr	r6, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r7, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r7, r7, r0
1:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x4
	str	r9, [r11, r8]
	subs	r6, r6, #0x1
	bne	1b

	cmp	r5, #0x0
	ldreq	r11, [r0, #PM_INFO_MX6Q_MMDC_V_OFFSET]
	ldrne	r11, [r0, #PM_INFO_MX6Q_MMDC_P_OFFSET]

	cmp	r3, #IMX_DDR_TYPE_LPDDR2
	bne	4f

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
4:
	/* let DDR out of self-refresh */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #(1 << 21)
	str	r7, [r11, #MX6Q_MMDC_MAPSR]
5:
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	bne	5b

	/* enable DDR auto power saving */
	ldr	r7, [r11, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #0x1
	str	r7, [r11, #MX6Q_MMDC_MAPSR]

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

	/* need to sync L2 cache before DSM. */
	sync_l2_cache

	ldr	r11, [r0, #PM_INFO_MX6Q_MMDC_V_OFFSET]
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

	ldr	r11, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldr	r6, =0x0
	ldr	r7, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r8, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r8, r8, r0
	/* LPDDR2's last 3 IOs need special setting */
	cmp	r3, #IMX_DDR_TYPE_LPDDR2
	subeq	r7, r7, #0x3
set_mmdc_io_lpm:
	ldr	r9, [r8], #0x8
	str	r6, [r11, r9]
	subs	r7, r7, #0x1
	bne	set_mmdc_io_lpm

	cmp 	r3, #IMX_DDR_TYPE_LPDDR2
	bne	set_mmdc_io_lpm_done
	ldr	r6, =0x1000
	ldr	r9, [r8], #0x8
	str	r6, [r11, r9]
	ldr	r9, [r8], #0x8
	str	r6, [r11, r9]
	ldr	r6, =0x80000
	ldr	r9, [r8]
	str	r6, [r11, r9]
set_mmdc_io_lpm_done:

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
	mov	r5, #0x0
	resume_mmdc

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

	/* get physical resume address from pm_info. */
	ldr	lr, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	/* clear core0's entry and parameter */
	ldr	r11, [r0, #PM_INFO_MX6Q_SRC_P_OFFSET]
	mov	r7, #0x0
	str	r7, [r11, #MX6Q_SRC_GPR1]
	str	r7, [r11, #MX6Q_SRC_GPR2]

	ldr	r3, [r0, #PM_INFO_DDR_TYPE_OFFSET]
	mov	r5, #0x1
	resume_mmdc

	ret	lr
ENDPROC(imx6_suspend)

/*
 * The following code must assume it is running from physical address
 * where absolute virtual addresses to the data section have to be
 * turned into relative ones.
 */

ENTRY(v7_cpu_resume)
	bl	v7_invalidate_l1
#ifdef CONFIG_CACHE_L2X0
	bl	l2c310_early_resume
#endif
	b	cpu_resume
ENDPROC(v7_cpu_resume)
