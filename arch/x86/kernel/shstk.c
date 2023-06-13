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

static unsigned long alloc_shstk(unsigned long size)
{
	int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_ABOVE4G;
	struct mm_struct *mm = current->mm;
	unsigned long addr, unused;

	mmap_write_lock(mm);
	addr = do_mmap(NULL, 0, size, PROT_READ, flags,
		       VM_SHADOW_STACK | VM_WRITE, 0, &unused, NULL);

	mmap_write_unlock(mm);

	return addr;
}

static unsigned long adjust_shstk_size(unsigned long size)
{
	if (size)
		return PAGE_ALIGN(size);

	return PAGE_ALIGN(min_t(unsigned long long, rlimit(RLIMIT_STACK), SZ_4G));
}

static void unmap_shadow_stack(u64 base, u64 size)
{
	while (1) {
		int r;

		r = vm_munmap(base, size);

		/*
		 * vm_munmap() returns -EINTR when mmap_lock is held by
		 * something else, and that lock should not be held for a
		 * long time.  Retry it for the case.
		 */
		if (r == -EINTR) {
			cond_resched();
			continue;
		}

		/*
		 * For all other types of vm_munmap() failure, either the
		 * system is out of memory or there is bug.
		 */
		WARN_ON_ONCE(r);
		break;
	}
}

static int shstk_setup(void)
{
	struct thread_shstk *shstk = &current->thread.shstk;
	unsigned long addr, size;

	/* Already enabled */
	if (features_enabled(ARCH_SHSTK_SHSTK))
		return 0;

	/* Also not supported for 32 bit and x32 */
	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) || in_32bit_syscall())
		return -EOPNOTSUPP;

	size = adjust_shstk_size(0);
	addr = alloc_shstk(size);
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
	 * For CLONE_VM, except vfork, the child needs a separate shadow
	 * stack.
	 */
	if ((clone_flags & (CLONE_VFORK | CLONE_VM)) != CLONE_VM)
		return 0;

	size = adjust_shstk_size(stack_size);
	addr = alloc_shstk(size);
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
	if (put_shstk_data((void *__user)*ssp, target_ssp))
		return -EFAULT;

	return 0;
}

static int shstk_pop_sigframe(unsigned long *ssp)
{
	unsigned long token_addr;
	int err;

	if (!IS_ALIGNED(*ssp, 8))
		return -EINVAL;

	err = get_shstk_data(&token_addr, (unsigned long __user *)*ssp);
	if (unlikely(err))
		return err;

	/* Restore SSP aligned? */
	if (unlikely(!IS_ALIGNED(token_addr, 8)))
		return -EINVAL;

	/* SSP in userspace? */
	if (unlikely(token_addr >= TASK_SIZE_MAX))
		return -EINVAL;

	*ssp = token_addr;

	return 0;
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

	unmap_shadow_stack(shstk->base, shstk->size);
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
	features_clr(ARCH_SHSTK_SHSTK);

	return 0;
}

long shstk_prctl(struct task_struct *task, int option, unsigned long features)
{
	if (option == ARCH_SHSTK_LOCK) {
		task->thread.features_locked |= features;
		return 0;
	}

	/* Don't allow via ptrace */
	if (task != current)
		return -EINVAL;

	/* Do not allow to change locked features */
	if (features & task->thread.features_locked)
		return -EPERM;

	/* Only support enabling/disabling one feature at a time. */
	if (hweight_long(features) > 1)
		return -EINVAL;

	if (option == ARCH_SHSTK_DISABLE) {
		return -EINVAL;
	}

	/* Handle ARCH_SHSTK_ENABLE */
	return -EINVAL;
}
