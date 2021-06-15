/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, Linaro Limited
 */
#include <linux/linkage.h>
#include <linux/arm-smccc.h>

#include <asm/asm-offsets.h>
#include <asm/assembler.h>

	.macro SMCCC instr
	\instr	#0
	ldr	x4, [sp]
	stp	x0, x1, [x4, #ARM_SMCCC_RES_X0_OFFS]
	stp	x2, x3, [x4, #ARM_SMCCC_RES_X2_OFFS]
	ldr	x4, [sp, #8]
	cbz	x4, 1f /* no quirk structure */
	ldr	x9, [x4, #ARM_SMCCC_QUIRK_ID_OFFS]
	cmp	x9, #ARM_SMCCC_QUIRK_QCOM_A6
	b.ne	1f
	str	x6, [x4, ARM_SMCCC_QUIRK_STATE_OFFS]
1:	ret
	.endm

/*
 * void arm_smccc_smc(unsigned long a0, unsigned long a1, unsigned long a2,
 *		  unsigned long a3, unsigned long a4, unsigned long a5,
 *		  unsigned long a6, unsigned long a7, struct arm_smccc_res *res,
 *		  struct arm_smccc_quirk *quirk)
 */
SYM_FUNC_START(__arm_smccc_smc)
	SMCCC	smc
SYM_FUNC_END(__arm_smccc_smc)
EXPORT_SYMBOL(__arm_smccc_smc)

/*
 * void arm_smccc_hvc(unsigned long a0, unsigned long a1, unsigned long a2,
 *		  unsigned long a3, unsigned long a4, unsigned long a5,
 *		  unsigned long a6, unsigned long a7, struct arm_smccc_res *res,
 *		  struct arm_smccc_quirk *quirk)
 */
SYM_FUNC_START(__arm_smccc_hvc)
	SMCCC	hvc
SYM_FUNC_END(__arm_smccc_hvc)
EXPORT_SYMBOL(__arm_smccc_hvc)

	.macro SMCCC_1_2 instr
	/* Save `res` and free a GPR that won't be clobbered */
	stp     x1, x19, [sp, #-16]!

	/* Ensure `args` won't be clobbered while loading regs in next step */
	mov	x19, x0

	/* Load the registers x0 - x17 from the struct arm_smccc_1_2_regs */
	ldp	x0, x1, [x19, #ARM_SMCCC_1_2_REGS_X0_OFFS]
	ldp	x2, x3, [x19, #ARM_SMCCC_1_2_REGS_X2_OFFS]
	ldp	x4, x5, [x19, #ARM_SMCCC_1_2_REGS_X4_OFFS]
	ldp	x6, x7, [x19, #ARM_SMCCC_1_2_REGS_X6_OFFS]
	ldp	x8, x9, [x19, #ARM_SMCCC_1_2_REGS_X8_OFFS]
	ldp	x10, x11, [x19, #ARM_SMCCC_1_2_REGS_X10_OFFS]
	ldp	x12, x13, [x19, #ARM_SMCCC_1_2_REGS_X12_OFFS]
	ldp	x14, x15, [x19, #ARM_SMCCC_1_2_REGS_X14_OFFS]
	ldp	x16, x17, [x19, #ARM_SMCCC_1_2_REGS_X16_OFFS]

	\instr #0

	/* Load the `res` from the stack */
	ldr	x19, [sp]

	/* Store the registers x0 - x17 into the result structure */
	stp	x0, x1, [x19, #ARM_SMCCC_1_2_REGS_X0_OFFS]
	stp	x2, x3, [x19, #ARM_SMCCC_1_2_REGS_X2_OFFS]
	stp	x4, x5, [x19, #ARM_SMCCC_1_2_REGS_X4_OFFS]
	stp	x6, x7, [x19, #ARM_SMCCC_1_2_REGS_X6_OFFS]
	stp	x8, x9, [x19, #ARM_SMCCC_1_2_REGS_X8_OFFS]
	stp	x10, x11, [x19, #ARM_SMCCC_1_2_REGS_X10_OFFS]
	stp	x12, x13, [x19, #ARM_SMCCC_1_2_REGS_X12_OFFS]
	stp	x14, x15, [x19, #ARM_SMCCC_1_2_REGS_X14_OFFS]
	stp	x16, x17, [x19, #ARM_SMCCC_1_2_REGS_X16_OFFS]

	/* Restore original x19 */
	ldp     xzr, x19, [sp], #16
	ret
.endm

/*
 * void arm_smccc_1_2_hvc(const struct arm_smccc_1_2_regs *args,
 *			  struct arm_smccc_1_2_regs *res);
 */
SYM_FUNC_START(arm_smccc_1_2_hvc)
	SMCCC_1_2 hvc
SYM_FUNC_END(arm_smccc_1_2_hvc)
EXPORT_SYMBOL(arm_smccc_1_2_hvc)

/*
 * void arm_smccc_1_2_smc(const struct arm_smccc_1_2_regs *args,
 *			  struct arm_smccc_1_2_regs *res);
 */
SYM_FUNC_START(arm_smccc_1_2_smc)
	SMCCC_1_2 smc
SYM_FUNC_END(arm_smccc_1_2_smc)
EXPORT_SYMBOL(arm_smccc_1_2_smc)
