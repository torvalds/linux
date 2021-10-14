/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 */
/*
 */

#include <linux/linkage.h>

#define M4IF_MCR0_OFFSET			(0x008C)
#define M4IF_MCR0_FDVFS				(0x1 << 11)
#define M4IF_MCR0_FDVACK			(0x1 << 27)

	.align 3

/*
 * ==================== low level suspend ====================
 *
 * On entry
 * r0: pm_info structure address;
 *
 * suspend ocram space layout:
 * ======================== high address ======================
 *                              .
 *                              .
 *                              .
 *                              ^
 *                              ^
 *                              ^
 *                      imx53_suspend code
 *              PM_INFO structure(imx5_cpu_suspend_info)
 * ======================== low address =======================
 */

/* Offsets of members of struct imx5_cpu_suspend_info */
#define SUSPEND_INFO_MX53_M4IF_V_OFFSET		0x0
#define SUSPEND_INFO_MX53_IOMUXC_V_OFFSET	0x4
#define SUSPEND_INFO_MX53_IO_COUNT_OFFSET	0x8
#define SUSPEND_INFO_MX53_IO_STATE_OFFSET	0xc

ENTRY(imx53_suspend)
	stmfd	sp!, {r4,r5,r6,r7}

	/* Save pad config */
	ldr	r1, [r0, #SUSPEND_INFO_MX53_IO_COUNT_OFFSET]
	cmp	r1, #0
	beq	skip_pad_conf_1

	add	r2, r0, #SUSPEND_INFO_MX53_IO_STATE_OFFSET
	ldr	r3, [r0, #SUSPEND_INFO_MX53_IOMUXC_V_OFFSET]

1:
	ldr	r5, [r2], #12	/* IOMUXC register offset */
	ldr	r6, [r3, r5]	/* current value */
	str	r6, [r2], #4	/* save area */
	subs	r1, r1, #1
	bne	1b

skip_pad_conf_1:
	/* Set FDVFS bit of M4IF_MCR0 to request DDR to enter self-refresh */
	ldr	r1, [r0, #SUSPEND_INFO_MX53_M4IF_V_OFFSET]
	ldr	r2,[r1, #M4IF_MCR0_OFFSET]
	orr	r2, r2, #M4IF_MCR0_FDVFS
	str	r2,[r1, #M4IF_MCR0_OFFSET]

	/* Poll FDVACK bit of M4IF_MCR to wait for DDR to enter self-refresh */
wait_sr_ack:
	ldr	r2,[r1, #M4IF_MCR0_OFFSET]
	ands	r2, r2, #M4IF_MCR0_FDVACK
	beq	wait_sr_ack

	/* Set pad config */
	ldr	r1, [r0, #SUSPEND_INFO_MX53_IO_COUNT_OFFSET]
	cmp	r1, #0
	beq	skip_pad_conf_2

	add	r2, r0, #SUSPEND_INFO_MX53_IO_STATE_OFFSET
	ldr	r3, [r0, #SUSPEND_INFO_MX53_IOMUXC_V_OFFSET]

2:
	ldr	r5, [r2], #4	/* IOMUXC register offset */
	ldr	r6, [r2], #4	/* clear */
	ldr	r7, [r3, r5]
	bic	r7, r7, r6
	ldr	r6, [r2], #8	/* set */
	orr	r7, r7, r6
	str	r7, [r3, r5]
	subs	r1, r1, #1
	bne	2b

skip_pad_conf_2:
	/* Zzz, enter stop mode */
	wfi
	nop
	nop
	nop
	nop

	/* Restore pad config */
	ldr	r1, [r0, #SUSPEND_INFO_MX53_IO_COUNT_OFFSET]
	cmp	r1, #0
	beq	skip_pad_conf_3

	add	r2, r0, #SUSPEND_INFO_MX53_IO_STATE_OFFSET
	ldr	r3, [r0, #SUSPEND_INFO_MX53_IOMUXC_V_OFFSET]

3:
	ldr	r5, [r2], #12	/* IOMUXC register offset */
	ldr	r6, [r2], #4	/* saved value */
	str	r6, [r3, r5]
	subs	r1, r1, #1
	bne	3b

skip_pad_conf_3:
	/* Clear FDVFS bit of M4IF_MCR0 to request DDR to exit self-refresh */
	ldr	r1, [r0, #SUSPEND_INFO_MX53_M4IF_V_OFFSET]
	ldr	r2,[r1, #M4IF_MCR0_OFFSET]
	bic	r2, r2, #M4IF_MCR0_FDVFS
	str	r2,[r1, #M4IF_MCR0_OFFSET]

	/* Poll FDVACK bit of M4IF_MCR to wait for DDR to exit self-refresh */
wait_ar_ack:
	ldr	r2,[r1, #M4IF_MCR0_OFFSET]
	ands	r2, r2, #M4IF_MCR0_FDVACK
	bne	wait_ar_ack

	/* Restore registers */
	ldmfd	sp!, {r4,r5,r6,r7}
	mov	pc, lr

ENDPROC(imx53_suspend)

ENTRY(imx53_suspend_sz)
        .word   . - imx53_suspend
