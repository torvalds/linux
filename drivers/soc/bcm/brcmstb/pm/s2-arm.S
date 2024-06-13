/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2014-2017 Broadcom
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#include "pm.h"

	.text
	.align	3

#define AON_CTRL_REG		r10
#define DDR_PHY_STATUS_REG	r11

/*
 * r0: AON_CTRL base address
 * r1: DDRY PHY PLL status register address
 */
ENTRY(brcmstb_pm_do_s2)
	stmfd	sp!, {r4-r11, lr}
	mov	AON_CTRL_REG, r0
	mov	DDR_PHY_STATUS_REG, r1

	/* Flush memory transactions */
	dsb

	/* Cache DDR_PHY_STATUS_REG translation */
	ldr	r0, [DDR_PHY_STATUS_REG]

	/* power down request */
	ldr	r0, =PM_S2_COMMAND
	ldr	r1, =0
	str	r1, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]
	ldr	r1, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]
	str	r0, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]
	ldr	r0, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]

	/* Wait for interrupt */
	wfi
	nop

	/* Bring MEMC back up */
1:	ldr	r0, [DDR_PHY_STATUS_REG]
	ands	r0, #1
	beq	1b

	/* Power-up handshake */
	ldr	r0, =1
	str	r0, [AON_CTRL_REG, #AON_CTRL_HOST_MISC_CMDS]
	ldr	r0, [AON_CTRL_REG, #AON_CTRL_HOST_MISC_CMDS]

	ldr	r0, =0
	str	r0, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]
	ldr	r0, [AON_CTRL_REG, #AON_CTRL_PM_CTRL]

	/* Return to caller */
	ldr	r0, =0
	ldmfd	sp!, {r4-r11, pc}

	ENDPROC(brcmstb_pm_do_s2)

	/* Place literal pool here */
	.ltorg

ENTRY(brcmstb_pm_do_s2_sz)
	.word   . - brcmstb_pm_do_s2
