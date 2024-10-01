// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/types.h>

#include <asm/cpufeature.h>
#include <asm/page.h>

/*
 * Apply the GCS mode configured for the specified task to the
 * hardware.
 */
void gcs_set_el0_mode(struct task_struct *task)
{
	u64 gcscre0_el1 = GCSCRE0_EL1_nTR;

	if (task->thread.gcs_el0_mode & PR_SHADOW_STACK_ENABLE)
		gcscre0_el1 |= GCSCRE0_EL1_RVCHKEN | GCSCRE0_EL1_PCRSEL;

	if (task->thread.gcs_el0_mode & PR_SHADOW_STACK_WRITE)
		gcscre0_el1 |= GCSCRE0_EL1_STREn;

	if (task->thread.gcs_el0_mode & PR_SHADOW_STACK_PUSH)
		gcscre0_el1 |= GCSCRE0_EL1_PUSHMEn;

	write_sysreg_s(gcscre0_el1, SYS_GCSCRE0_EL1);
}

void gcs_free(struct task_struct *task)
{
	if (!system_supports_gcs())
		return;

	if (task->thread.gcs_base)
		vm_munmap(task->thread.gcs_base, task->thread.gcs_size);

	task->thread.gcspr_el0 = 0;
	task->thread.gcs_base = 0;
	task->thread.gcs_size = 0;
}
