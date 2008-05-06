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
	/* XXX This is the stuff for sclow.S, kill it. */
	DEFINE(AOFF_task_pid, offsetof(struct task_struct, pid));
	DEFINE(AOFF_task_uid, offsetof(struct task_struct, uid));
	DEFINE(AOFF_task_gid, offsetof(struct task_struct, gid));
	DEFINE(AOFF_task_euid, offsetof(struct task_struct, euid));
	DEFINE(AOFF_task_egid, offsetof(struct task_struct, egid));
	/* DEFINE(THREAD_INFO, offsetof(struct task_struct, stack)); */
	DEFINE(ASIZ_task_uid,	sizeof(current->uid));
	DEFINE(ASIZ_task_gid,	sizeof(current->gid));
	DEFINE(ASIZ_task_euid,	sizeof(current->euid));
	DEFINE(ASIZ_task_egid,	sizeof(current->egid));
	BLANK();
	DEFINE(AOFF_thread_fork_kpsr,
			offsetof(struct thread_struct, fork_kpsr));
	BLANK();
	DEFINE(AOFF_mm_context, offsetof(struct mm_struct, context));

	/* DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28); */
	return 0;
}
