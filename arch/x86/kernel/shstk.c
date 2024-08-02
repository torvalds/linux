// SPDX-License-Identifier: GPL-2.0
/*
 * shstk.c - Intel shadow stack support
 *
 * Copyright (c) 2021, Intel Corporation.
 * Yu-cheng Yu <yu-cheng.yu@intel.com>
 */

#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/compat.h>
#include <linux/sizes.h>
#include <linux/user.h>
#include <linux/syscalls.h>
#include <asm/msr.h>
#include <asm/fpu/xstate.h>
#include <asm/fpu/types.h>
#include <asm/shstk.h>
#include <asm/special_insns.h>
#include <asm/fpu/api.h>
#include <asm/prctl.h>

#define SS_FRAME_SIZE 8

static bool features_enabled(unsigned long features)
{
	return current->thread.features & features;
}

static void features_set(unsigned long features)
{
	current->thread.features |= features;
}

static void features_clr(unsigned long features)
{
	current->thread.features &= ~features;
}

/*
 * Create a restore token on the shadow stack.  A token is always 8-byte
 * and aligned to 8.
 */
static int create_rstor_token(unsigned long ssp, unsigned long *token_addr)
{
	unsigned long addr;

	/* Token must be aligned */
	if (!IS_ALIGNED(ssp, 8))
		return -EINVAL;

	addr = ssp - SS_FRAME_SIZE;

	/*
	 * SSP is aligned, so reserved bits and mode bit are a zero, just mark
	 * the token 64-bit.
	 */
	ssp |= BIT(0);

	if (write_user_shstk_64((u64 __user *)addr, (u64)ssp))
		return -EFAULT;

	if (token_addr)
		*token_addr = addr;

	return 0;
}

/*
 * VM_SHADOW_STACK will have a guard page. This helps userspace protect
 * itself from attacks. The reasoning is as follows:
 *
 * The shadow stack pointer(SSP) is moved by CALL, RET, and INCSSPQ. The
 * INCSSP instruction can increment the shadow stack pointer. It is the
 * shadow stack analog of an instruction like:
 *
 *   addq $0x80, %rsp
 *
 * However, there is one important difference between an ADD on %rsp
 * and INCSSP. In addition to modifying SSP, INCSSP also reads from the
 * memory of the first and last elements that were "popped". It can be
 * thought of as acting like this:
 *
 * READ_ONCE(ssp);       // read+discard top element on stack
 * ssp += nr_to_pop * 8; // move the shadow stack
 * READ_ONCE(ssp-8);     // read+discard last popped stack element
 *
 * The maximum distance INCSSP can move the SSP is 2040 bytes, before
 * it would read the memory. Therefore a single page gap will be enough
 * to prevent any operation from shifting the SSP to an adjacent stack,
 * since it would have to land in the gap at least once, causing a
 * fault.
 */
static unsigned long alloc_shstk(unsigned long addr, unsigned long size,
				 unsigned long token_offset, bool set_res_tok)
{
	int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_ABOVE4G;
	struct mm_struct *mm = current->mm;
	unsigned long mapped_addr, unused;

	if (addr)
		flags |= MAP_FIXED_NOREPLACE;

	mmap_write_lock(mm);
	mapped_addr = do_mmap(NULL, addr, size, PROT_READ, flags,
			      VM_SHADOW_STACK | VM_WRITE, 0, &unused, NULL);
	mmap_write_unlock(mm);

	if (!set_res_tok || IS_ERR_VALUE(mapped_addr))
		goto out;

	if (create_rstor_token(mapped_addr + token_offset, NULL)) {
		vm_munmap(mapped_addr, size);
		return -EINVAL;
	}

out:
	return mapped_addr;
}

static unsigned long adjust_shstk_size(unsigned long size)
{
	if (size)
		return PAGE_ALIGN(size);

	return PAGE_ALIGN(min_t(unsigned long long, rlimit(RLIMIT_STACK), SZ_4G));
}

static void unmap_shadow_stack(u64 base, u64 size)
{
	int r;

	r = vm_munmap(base, size);

	/*
	 * mmap_write_lock_killable() failed with -EINTR. This means
	 * the process is about to die and have it's MM cleaned up.
	 * This task shouldn't ever make it back to userspace. In this
	 * case it is ok to leak a shadow stack, so just exit out.
	 */
	if (r == -EINTR)
		return;

	/*
	 * For all other types of vm_munmap() failure, either the
	 * system is out of memory or there is bug.
	 */
	WARN_ON_ONCE(r);
}

static int shstk_setup(void)
{
	struct thread_shstk *shstk = &current->thread.shstk;
	unsigned long addr, size;

	/* Already enabled */
	if (features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	/* Also not supported for 32 bit */
	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) || in_ia32_syscall())
		return -EOPNOTSUPP;

	size = adjust_shstk_size(0);
	addr = alloc_shstk(0, size, 0, false);
	if (IS_ERR_VALUE(addr))
		return PTR_ERR((void *)addr);

	fpregs_lock_and_load();
	wrmsrl(MSR_IA32_PL3_SSP, addr + size);
	wrmsrl(MSR_IA32_U_CET, CET_SHSTK_EN);
	fpregs_unlock();

	shstk->base = addr;
	shstk->size = size;
	features_set(ARCH_SHSTK_SHSTK);

	return 0;
}

void reset_thread_features(void)
{
	memset(&current->thread.shstk, 0, sizeof(struct thread_shstk));
	current->thread.features = 0;
	current->thread.features_locked = 0;
}

unsigned long shstk_alloc_thread_stack(struct task_struct *tsk, unsigned long clone_flags,
				       unsigned long stack_size)
{
	struct thread_shstk *shstk = &tsk->thread.shstk;
	unsigned long addr, size;

	/*
	 * If shadow stack is not enabled on the new thread, skip any
	 * switch to a new shadow stack.
	 */
	if (!features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	/*
	 * For CLONE_VFORK the child will share the parents shadow stack.
	 * Make sure to clear the internal tracking of the thread shadow
	 * stack so the freeing logic run for child knows to leave it alone.
	 */
	if (clone_flags & CLONE_VFORK) {
		shstk->base = 0;
		shstk->size = 0;
		return 0;
	}

	/*
	 * For !CLONE_VM the child will use a copy of the parents shadow
	 * stack.
	 */
	if (!(clone_flags & CLONE_VM))
		return 0;

	size = adjust_shstk_size(stack_size);
	addr = alloc_shstk(0, size, 0, false);
	if (IS_ERR_VALUE(addr))
		return addr;

	shstk->base = addr;
	shstk->size = size;

	return addr + size;
}

static unsigned long get_user_shstk_addr(void)
{
	unsigned long long ssp;

	fpregs_lock_and_load();

	rdmsrl(MSR_IA32_PL3_SSP, ssp);

	fpregs_unlock();

	return ssp;
}

#define SHSTK_DATA_BIT BIT(63)

static int put_shstk_data(u64 __user *addr, u64 data)
{
	if (WARN_ON_ONCE(data & SHSTK_DATA_BIT))
		return -EINVAL;

	/*
	 * Mark the high bit so that the sigframe can't be processed as a
	 * return address.
	 */
	if (write_user_shstk_64(addr, data | SHSTK_DATA_BIT))
		return -EFAULT;
	return 0;
}

static int get_shstk_data(unsigned long *data, unsigned long __user *addr)
{
	unsigned long ldata;

	if (unlikely(get_user(ldata, addr)))
		return -EFAULT;

	if (!(ldata & SHSTK_DATA_BIT))
		return -EINVAL;

	*data = ldata & ~SHSTK_DATA_BIT;

	return 0;
}

static int shstk_push_sigframe(unsigned long *ssp)
{
	unsigned long target_ssp = *ssp;

	/* Token must be aligned */
	if (!IS_ALIGNED(target_ssp, 8))
		return -EINVAL;

	*ssp -= SS_FRAME_SIZE;
	if (put_shstk_data((void __user *)*ssp, target_ssp))
		return -EFAULT;

	return 0;
}

static int shstk_pop_sigframe(unsigned long *ssp)
{
	struct vm_area_struct *vma;
	unsigned long token_addr;
	bool need_to_check_vma;
	int err = 1;

	/*
	 * It is possible for the SSP to be off the end of a shadow stack by 4
	 * or 8 bytes. If the shadow stack is at the start of a page or 4 bytes
	 * before it, it might be this case, so check that the address being
	 * read is actually shadow stack.
	 */
	if (!IS_ALIGNED(*ssp, 8))
		return -EINVAL;

	need_to_check_vma = PAGE_ALIGN(*ssp) == *ssp;

	if (need_to_check_vma)
		mmap_read_lock_killable(current->mm);

	err = get_shstk_data(&token_addr, (unsigned long __user *)*ssp);
	if (unlikely(err))
		goto out_err;

	if (need_to_check_vma) {
		vma = find_vma(current->mm, *ssp);
		if (!vma || !(vma->vm_flags & VM_SHADOW_STACK)) {
			err = -EFAULT;
			goto out_err;
		}

		mmap_read_unlock(current->mm);
	}

	/* Restore SSP aligned? */
	if (unlikely(!IS_ALIGNED(token_addr, 8)))
		return -EINVAL;

	/* SSP in userspace? */
	if (unlikely(token_addr >= TASK_SIZE_MAX))
		return -EINVAL;

	*ssp = token_addr;

	return 0;
out_err:
	if (need_to_check_vma)
		mmap_read_unlock(current->mm);
	return err;
}

int setup_signal_shadow_stack(struct ksignal *ksig)
{
	void __user *restorer = ksig->ka.sa.sa_restorer;
	unsigned long ssp;
	int err;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) ||
	    !features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	if (!restorer)
		return -EINVAL;

	ssp = get_user_shstk_addr();
	if (unlikely(!ssp))
		return -EINVAL;

	err = shstk_push_sigframe(&ssp);
	if (unlikely(err))
		return err;

	/* Push restorer address */
	ssp -= SS_FRAME_SIZE;
	err = write_user_shstk_64((u64 __user *)ssp, (u64)restorer);
	if (unlikely(err))
		return -EFAULT;

	fpregs_lock_and_load();
	wrmsrl(MSR_IA32_PL3_SSP, ssp);
	fpregs_unlock();

	return 0;
}

int restore_signal_shadow_stack(void)
{
	unsigned long ssp;
	int err;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) ||
	    !features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	ssp = get_user_shstk_addr();
	if (unlikely(!ssp))
		return -EINVAL;

	err = shstk_pop_sigframe(&ssp);
	if (unlikely(err))
		return err;

	fpregs_lock_and_load();
	wrmsrl(MSR_IA32_PL3_SSP, ssp);
	fpregs_unlock();

	return 0;
}

void shstk_free(struct task_struct *tsk)
{
	struct thread_shstk *shstk = &tsk->thread.shstk;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) ||
	    !features_enabled(ARCH_SHSTK_SHSTK))
		return;

	/*
	 * When fork() with CLONE_VM fails, the child (tsk) already has a
	 * shadow stack allocated, and exit_thread() calls this function to
	 * free it.  In this case the parent (current) and the child share
	 * the same mm struct.
	 */
	if (!tsk->mm || tsk->mm != current->mm)
		return;

	/*
	 * If shstk->base is NULL, then this task is not managing its
	 * own shadow stack (CLONE_VFORK). So skip freeing it.
	 */
	if (!shstk->base)
		return;

	/*
	 * shstk->base is NULL for CLONE_VFORK child tasks, and so is
	 * normal. But size = 0 on a shstk->base is not normal and
	 * indicated an attempt to free the thread shadow stack twice.
	 * Warn about it.
	 */
	if (WARN_ON(!shstk->size))
		return;

	unmap_shadow_stack(shstk->base, shstk->size);

	shstk->size = 0;
}

static int wrss_control(bool enable)
{
	u64 msrval;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK))
		return -EOPNOTSUPP;

	/*
	 * Only enable WRSS if shadow stack is enabled. If shadow stack is not
	 * enabled, WRSS will already be disabled, so don't bother clearing it
	 * when disabling.
	 */
	if (!features_enabled(ARCH_SHSTK_SHSTK))
		return -EPERM;

	/* Already enabled/disabled? */
	if (features_enabled(ARCH_SHSTK_WRSS) == enable)
		return 0;

	fpregs_lock_and_load();
	rdmsrl(MSR_IA32_U_CET, msrval);

	if (enable) {
		features_set(ARCH_SHSTK_WRSS);
		msrval |= CET_WRSS_EN;
	} else {
		features_clr(ARCH_SHSTK_WRSS);
		if (!(msrval & CET_WRSS_EN))
			goto unlock;

		msrval &= ~CET_WRSS_EN;
	}

	wrmsrl(MSR_IA32_U_CET, msrval);

unlock:
	fpregs_unlock();

	return 0;
}

static int shstk_disable(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK))
		return -EOPNOTSUPP;

	/* Already disabled? */
	if (!features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	fpregs_lock_and_load();
	/* Disable WRSS too when disabling shadow stack */
	wrmsrl(MSR_IA32_U_CET, 0);
	wrmsrl(MSR_IA32_PL3_SSP, 0);
	fpregs_unlock();

	shstk_free(current);
	features_clr(ARCH_SHSTK_SHSTK | ARCH_SHSTK_WRSS);

	return 0;
}

SYSCALL_DEFINE3(map_shadow_stack, unsigned long, addr, unsigned long, size, unsigned int, flags)
{
	bool set_tok = flags & SHADOW_STACK_SET_TOKEN;
	unsigned long aligned_size;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK))
		return -EOPNOTSUPP;

	if (flags & ~SHADOW_STACK_SET_TOKEN)
		return -EINVAL;

	/* If there isn't space for a token */
	if (set_tok && size < 8)
		return -ENOSPC;

	if (addr && addr < SZ_4G)
		return -ERANGE;

	/*
	 * An overflow would result in attempting to write the restore token
	 * to the wrong location. Not catastrophic, but just return the right
	 * error code and block it.
	 */
	aligned_size = PAGE_ALIGN(size);
	if (aligned_size < size)
		return -EOVERFLOW;

	return alloc_shstk(addr, aligned_size, size, set_tok);
}

long shstk_prctl(struct task_struct *task, int option, unsigned long arg2)
{
	unsigned long features = arg2;

	if (option == ARCH_SHSTK_STATUS) {
		return put_user(task->thread.features, (unsigned long __user *)arg2);
	}

	if (option == ARCH_SHSTK_LOCK) {
		task->thread.features_locked |= features;
		return 0;
	}

	/* Only allow via ptrace */
	if (task != current) {
		if (option == ARCH_SHSTK_UNLOCK && IS_ENABLED(CONFIG_CHECKPOINT_RESTORE)) {
			task->thread.features_locked &= ~features;
			return 0;
		}
		return -EINVAL;
	}

	/* Do not allow to change locked features */
	if (features & task->thread.features_locked)
		return -EPERM;

	/* Only support enabling/disabling one feature at a time. */
	if (hweight_long(features) > 1)
		return -EINVAL;

	if (option == ARCH_SHSTK_DISABLE) {
		if (features & ARCH_SHSTK_WRSS)
			return wrss_control(false);
		if (features & ARCH_SHSTK_SHSTK)
			return shstk_disable();
		return -EINVAL;
	}

	/* Handle ARCH_SHSTK_ENABLE */
	if (features & ARCH_SHSTK_SHSTK)
		return shstk_setup();
	if (features & ARCH_SHSTK_WRSS)
		return wrss_control(true);
	return -EINVAL;
}

int shstk_update_last_frame(unsigned long val)
{
	unsigned long ssp;

	if (!features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	ssp = get_user_shstk_addr();
	return write_user_shstk_64((u64 __user *)ssp, (u64)val);
}

bool shstk_is_enabled(void)
{
	return features_enabled(ARCH_SHSTK_SHSTK);
}
