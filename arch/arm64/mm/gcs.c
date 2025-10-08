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

SYSCALL_DEFINE3(map_shadow_stack, unsigned long, addr, unsigned long, size, unsigned int, flags)
{
	unsigned long alloc_size;
	unsigned long __user *cap_ptr;
	unsigned long cap_val;
	int ret = 0;
	int cap_offset;

	if (!system_supports_gcs())
		return -EOPNOTSUPP;

	if (flags & ~(SHADOW_STACK_SET_TOKEN | SHADOW_STACK_SET_MARKER))
		return -EINVAL;

	if (!PAGE_ALIGNED(addr))
		return -EINVAL;

	if (size == 8 || !IS_ALIGNED(size, 8))
		return -EINVAL;

	/*
	 * An overflow would result in attempting to write the restore token
	 * to the wrong location. Not catastrophic, but just return the right
	 * error code and block it.
	 */
	alloc_size = PAGE_ALIGN(size);
	if (alloc_size < size)
		return -EOVERFLOW;

	addr = alloc_gcs(addr, alloc_size);
	if (IS_ERR_VALUE(addr))
		return addr;

	/*
	 * Put a cap token at the end of the allocated region so it
	 * can be switched to.
	 */
	if (flags & SHADOW_STACK_SET_TOKEN) {
		/* Leave an extra empty frame as a top of stack marker? */
		if (flags & SHADOW_STACK_SET_MARKER)
			cap_offset = 2;
		else
			cap_offset = 1;

		cap_ptr = (unsigned long __user *)(addr + size -
						   (cap_offset * sizeof(unsigned long)));
		cap_val = GCS_CAP(cap_ptr);

		put_user_gcs(cap_val, cap_ptr, &ret);
		if (ret != 0) {
			vm_munmap(addr, size);
			return -EFAULT;
		}

		/*
		 * Ensure the new cap is ordered before standard
		 * memory accesses to the same location.
		 */
		gcsb_dsync();
	}

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

	if (!task->mm || task->mm != current->mm)
		return;

	if (task->thread.gcs_base)
		vm_munmap(task->thread.gcs_base, task->thread.gcs_size);

	task->thread.gcspr_el0 = 0;
	task->thread.gcs_base = 0;
	task->thread.gcs_size = 0;
}

int arch_set_shadow_stack_status(struct task_struct *task, unsigned long arg)
{
	unsigned long gcs, size;
	int ret;

	if (!system_supports_gcs())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(task)))
		return -EINVAL;

	/* Reject unknown flags */
	if (arg & ~PR_SHADOW_STACK_SUPPORTED_STATUS_MASK)
		return -EINVAL;

	ret = gcs_check_locked(task, arg);
	if (ret != 0)
		return ret;

	/* If we are enabling GCS then make sure we have a stack */
	if (arg & PR_SHADOW_STACK_ENABLE &&
	    !task_gcs_el0_enabled(task)) {
		/* Do not allow GCS to be reenabled */
		if (task->thread.gcs_base || task->thread.gcspr_el0)
			return -EINVAL;

		if (task != current)
			return -EBUSY;

		size = gcs_size(0);
		gcs = alloc_gcs(0, size);
		if (!gcs)
			return -ENOMEM;

		task->thread.gcspr_el0 = gcs + size - sizeof(u64);
		task->thread.gcs_base = gcs;
		task->thread.gcs_size = size;
		if (task == current)
			write_sysreg_s(task->thread.gcspr_el0,
				       SYS_GCSPR_EL0);
	}

	task->thread.gcs_el0_mode = arg;
	if (task == current)
		gcs_set_el0_mode(task);

	return 0;
}

int arch_get_shadow_stack_status(struct task_struct *task,
				 unsigned long __user *arg)
{
	if (!system_supports_gcs())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(task)))
		return -EINVAL;

	return put_user(task->thread.gcs_el0_mode, arg);
}

int arch_lock_shadow_stack_status(struct task_struct *task,
				  unsigned long arg)
{
	if (!system_supports_gcs())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(task)))
		return -EINVAL;

	/*
	 * We support locking unknown bits so applications can prevent
	 * any changes in a future proof manner.
	 */
	task->thread.gcs_el0_locked |= arg;

	return 0;
}
