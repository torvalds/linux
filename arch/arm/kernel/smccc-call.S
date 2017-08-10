/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/linkage.h>

#include <asm/opcodes-sec.h>
#include <asm/opcodes-virt.h>
#include <asm/unwind.h>

	/*
	 * Wrap c macros in asm macros to delay expansion until after the
	 * SMCCC asm macro is expanded.
	 */
	.macro SMCCC_SMC
	__SMC(0)
	.endm

	.macro SMCCC_HVC
	__HVC(0)
	.endm

	.macro SMCCC instr
UNWIND(	.fnstart)
	mov	r12, sp
	push	{r4-r7}
UNWIND(	.save	{r4-r7})
	ldm	r12, {r4-r7}
	\instr
	pop	{r4-r7}
	ldr	r12, [sp, #(4 * 4)]
	stm	r12, {r0-r3}
	bx	lr
UNWIND(	.fnend)
	.endm

/*
 * void smccc_smc(unsigned long a0, unsigned long a1, unsigned long a2,
 *		  unsigned long a3, unsigned long a4, unsigned long a5,
 *		  unsigned long a6, unsigned long a7, struct arm_smccc_res *res,
 *		  struct arm_smccc_quirk *quirk)
 */
ENTRY(__arm_smccc_smc)
	SMCCC SMCCC_SMC
ENDPROC(__arm_smccc_smc)

/*
 * void smccc_hvc(unsigned long a0, unsigned long a1, unsigned long a2,
 *		  unsigned long a3, unsigned long a4, unsigned long a5,
 *		  unsigned long a6, unsigned long a7, struct arm_smccc_res *res,
 *		  struct arm_smccc_quirk *quirk)
 */
ENTRY(__arm_smccc_hvc)
	SMCCC SMCCC_HVC
ENDPROC(__arm_smccc_hvc)
