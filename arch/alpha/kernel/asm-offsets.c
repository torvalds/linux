// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */
#define COMPILE_OFFSETS

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/kbuild.h>
#include <asm/machvec.h>

static void __used foo(void)
{
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_FP, offsetof(struct thread_info, fp));
	DEFINE(TI_STATUS, offsetof(struct thread_info, status));
	BLANK();

	DEFINE(SP_OFF, offsetof(struct pt_regs, ps));
	DEFINE(SIZEOF_PT_REGS, sizeof(struct pt_regs));
	BLANK();

	DEFINE(SWITCH_STACK_SIZE, sizeof(struct switch_stack));
	BLANK();

	DEFINE(HAE_CACHE, offsetof(struct alpha_machine_vector, hae_cache));
	DEFINE(HAE_REG, offsetof(struct alpha_machine_vector, hae_register));
}
