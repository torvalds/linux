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
// #include <linux/mm.h>
#include <linux/kbuild.h>

int foo(void)
{
	DEFINE(AOFF_task_thread, offsetof(struct task_struct, thread));
	BLANK();
	DEFINE(AOFF_thread_fork_kpsr,
			offsetof(struct thread_struct, fork_kpsr));
	BLANK();
	DEFINE(AOFF_mm_context, offsetof(struct mm_struct, context));

	/* DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28); */
	return 0;
}
