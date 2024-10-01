// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/types.h>

#include <asm/cmpxchg.h>
#include <asm/cpufeature.h>
#include <asm/gcs.h>
#include <asm/page.h>

static unsigned long alloc_gcs(unsigned long addr, unsigned long size)
{
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	struct mm_struct *mm = current->mm;
	unsigned long mapped_addr, unused;

	if (addr)
		flags |= MAP_FIXED_NOREPLACE;

	mmap_write_lock(mm);
	mapped_addr = do_mmap(NULL, addr, size, PROT_READ, flags,
			      VM_SHADOW_STACK | VM_WRITE, 0, &unused, NULL);
	mmap_write_unlock(mm);

	return mapped_addr;
}

static unsigned long gcs_size(unsigned long size)
{
	if (size)
		return PAGE_ALIGN(size);

	/* Allocate RLIMIT_STACK/2 with limits of PAGE_SIZE..2G */
	size = PAGE_ALIGN(min_t(unsigned long long,
				rlimit(RLIMIT_STACK) / 2, SZ_2G));
	return max(PAGE_SIZE, size);
}

unsigned long gcs_alloc_thread_stack(struct task_struct *tsk,
				     const struct kernel_clone_args *args)
{
	unsigned long addr, size;

	if (!system_supports_gcs())
		return 0;

	if (!task_gcs_el0_enabled(tsk))
		return 0;

	if ((args->flags & (CLONE_VFORK | CLONE_VM)) != CLONE_VM) {
		tsk->thread.gcspr_el0 = read_sysreg_s(SYS_GCSPR_EL0);
		return 0;
	}

	size = args->stack_size / 2;

	size = gcs_size(size);
	addr = alloc_gcs(0, size);
	if (IS_ERR_VALUE(addr))
		return addr;

	tsk->thread.gcs_base = addr;
	tsk->thread.gcs_size = size;
	tsk->thread.gcspr_el0 = addr + size - sizeof(u64);

	return addr;
}

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

	/*
	 * When fork() with CLONE_VM fails, the child (tsk) already
	 * has a GCS allocated, and exit_thread() calls this function
	 * to free it.  In this case the parent (current) and the
	 * child share the same mm struct.
	 */
	if (!task->mm || task->mm != current->mm)
		return;

	if (task->thread.gcs_base)
		vm_munmap(task->thread.gcs_base, task->thread.gcs_size);

	task->thread.gcspr_el0 = 0;
	task->thread.gcs_base = 0;
	task->thread.gcs_size = 0;
}
