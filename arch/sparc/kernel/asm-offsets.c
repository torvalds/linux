// SPDX-License-Identifier: GPL-2.0
/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 *
 * On sparc, thread_info data is static and TI_XXX offsets are computed by hand.
 */

#include <linux/sched.h>
#include <linux/mm_types.h>
// #include <linux/mm.h>
#include <linux/kbuild.h>

#include <asm/hibernate.h>

#ifdef CONFIG_SPARC32
static int __used sparc32_foo(void)
{
	DEFINE(AOFF_thread_fork_kpsr,
			offsetof(struct thread_struct, fork_kpsr));
	return 0;
}
#else
static int __used sparc64_foo(void)
{
#ifdef CONFIG_HIBERNATION
	BLANK();
	OFFSET(SC_REG_FP, saved_context, fp);
	OFFSET(SC_REG_CWP, saved_context, cwp);
	OFFSET(SC_REG_WSTATE, saved_context, wstate);

	OFFSET(SC_REG_TICK, saved_context, tick);
	OFFSET(SC_REG_PSTATE, saved_context, pstate);

	OFFSET(SC_REG_G4, saved_context, g4);
	OFFSET(SC_REG_G5, saved_context, g5);
	OFFSET(SC_REG_G6, saved_context, g6);
#endif
	return 0;
}
#endif

static int __used foo(void)
{
	BLANK();
	DEFINE(AOFF_task_thread, offsetof(struct task_struct, thread));
	BLANK();
	DEFINE(AOFF_mm_context, offsetof(struct mm_struct, context));
	BLANK();
	DEFINE(VMA_VM_MM,    offsetof(struct vm_area_struct, vm_mm));

	/* DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28); */
	return 0;
}

