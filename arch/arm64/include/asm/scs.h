/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLY__

#include <asm/asm-offsets.h>
#include <asm/sysreg.h>

#ifdef CONFIG_SHADOW_CALL_STACK
	scs_sp	.req	x18

	.macro scs_load_current
	get_current_task scs_sp
	ldr	scs_sp, [scs_sp, #TSK_TI_SCS_SP]
	.endm

	.macro scs_save tsk
	str	scs_sp, [\tsk, #TSK_TI_SCS_SP]
	.endm
#else
	.macro scs_load_current
	.endm

	.macro scs_save tsk
	.endm
#endif /* CONFIG_SHADOW_CALL_STACK */


#else

#include <linux/scs.h>
#include <asm/cpufeature.h>

#ifdef CONFIG_UNWIND_PATCH_PAC_INTO_SCS
static inline void dynamic_scs_init(void)
{
	extern bool __pi_dynamic_scs_is_enabled;

	if (__pi_dynamic_scs_is_enabled) {
		pr_info("Enabling dynamic shadow call stack\n");
		static_branch_enable(&dynamic_scs_enabled);
	}
}
#else
static inline void dynamic_scs_init(void) {}
#endif

enum {
	EDYNSCS_INVALID_CIE_HEADER		= 1,
	EDYNSCS_INVALID_CIE_SDATA_SIZE		= 2,
	EDYNSCS_INVALID_FDE_AUGM_DATA_SIZE	= 3,
	EDYNSCS_INVALID_CFA_OPCODE		= 4,
};

int __pi_scs_patch(const u8 eh_frame[], int size, bool skip_dry_run);

#endif /* __ASSEMBLY __ */

#endif /* _ASM_SCS_H */
