/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLY__

#include <asm/asm-offsets.h>

#ifdef CONFIG_SHADOW_CALL_STACK
	scs_sp	.req	x18

	.macro scs_load tsk, tmp
	ldr	scs_sp, [\tsk, #TSK_TI_SCS_SP]
	.endm

	.macro scs_save tsk, tmp
	str	scs_sp, [\tsk, #TSK_TI_SCS_SP]
	.endm
#else
	.macro scs_load tsk, tmp
	.endm

	.macro scs_save tsk, tmp
	.endm
#endif /* CONFIG_SHADOW_CALL_STACK */

#endif /* __ASSEMBLY __ */

#endif /* _ASM_SCS_H */
