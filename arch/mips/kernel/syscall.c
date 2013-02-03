/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2000, 2001, 05 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/compiler.h>
#include <linux/ipc.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/elf.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/cachectl.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>
#include <asm/signal.h>
#include <asm/sim.h>
#include <asm/shmparam.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>
#include <asm/switch_to.h>

/*
 * For historic reasons the pipe(2) syscall on MIPS has an unusual calling
 * convention.  It returns results in registers $v0 / $v1 which means there
 * is no need for it to do verify the validity of a userspace pointer
 * argument.  Historically that used to be expensive in Linux.  These days
 * the performance advantage is negligible.
 */
asmlinkage int sysm_pipe(nabi_no_regargs volatile struct pt_regs regs)
{
	int fd[2];
	int error, res;

	error = do_pipe_flags(fd, 0);
	if (error) {
		res = error;
		goto out;
	}
	regs.regs[3] = fd[1];
	res = fd[0];
out:
	return res;
}

SYSCALL_DEFINE6(mips_mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long,
	fd, off_t, offset)
{
	unsigned long result;

	result = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	result = sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);

out:
	return result;
}

SYSCALL_DEFINE6(mips_mmap2, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long, fd,
	unsigned long, pgoff)
{
	if (pgoff & (~PAGE_MASK >> 12))
		return -EINVAL;

	return sys_mmap_pgoff(addr, len, prot, flags, fd, pgoff >> (PAGE_SHIFT-12));
}

save_static_function(sys_fork);
static int __used noinline
_sys_fork(nabi_no_regargs struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.regs[29], 0, NULL, NULL);
}

save_static_function(sys_clone);
static int __used noinline
_sys_clone(nabi_no_regargs struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	parent_tidptr = (int __user *) regs.regs[6];
#ifdef CONFIG_32BIT
	/* We need to fetch the fifth argument off the stack.  */
	child_tidptr = NULL;
	if (clone_flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) {
		int __user *__user *usp = (int __user *__user *) regs.regs[29];
		if (regs.regs[2] == __NR_syscall) {
			if (get_user (child_tidptr, &usp[5]))
				return -EFAULT;
		}
		else if (get_user (child_tidptr, &usp[4]))
			return -EFAULT;
	}
#else
	child_tidptr = (int __user *) regs.regs[8];
#endif
	return do_fork(clone_flags, newsp, 0,
	               parent_tidptr, child_tidptr);
}

SYSCALL_DEFINE1(set_thread_area, unsigned long, addr)
{
	struct thread_info *ti = task_thread_info(current);

	ti->tp_value = addr;
	if (cpu_has_userlocal)
		write_c0_userlocal(addr);

	return 0;
}

static inline int mips_atomic_set(unsigned long addr, unsigned long new)
{
	unsigned long old, tmp;
	struct pt_regs *regs;
	unsigned int err;

	if (unlikely(addr & 3))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_WRITE, addr, 4)))
		return -EINVAL;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		__asm__ __volatile__ (
		"	.set	mips3					\n"
		"	li	%[err], 0				\n"
		"1:	ll	%[old], (%[addr])			\n"
		"	move	%[tmp], %[new]				\n"
		"2:	sc	%[tmp], (%[addr])			\n"
		"	beqzl	%[tmp], 1b				\n"
		"3:							\n"
		"	.section .fixup,\"ax\"				\n"
		"4:	li	%[err], %[efault]			\n"
		"	j	3b					\n"
		"	.previous					\n"
		"	.section __ex_table,\"a\"			\n"
		"	"STR(PTR)"	1b, 4b				\n"
		"	"STR(PTR)"	2b, 4b				\n"
		"	.previous					\n"
		"	.set	mips0					\n"
		: [old] "=&r" (old),
		  [err] "=&r" (err),
		  [tmp] "=&r" (tmp)
		: [addr] "r" (addr),
		  [new] "r" (new),
		  [efault] "i" (-EFAULT)
		: "memory");
	} else if (cpu_has_llsc) {
		__asm__ __volatile__ (
		"	.set	mips3					\n"
		"	li	%[err], 0				\n"
		"1:	ll	%[old], (%[addr])			\n"
		"	move	%[tmp], %[new]				\n"
		"2:	sc	%[tmp], (%[addr])			\n"
		"	bnez	%[tmp], 4f				\n"
		"3:							\n"
		"	.subsection 2					\n"
		"4:	b	1b					\n"
		"	.previous					\n"
		"							\n"
		"	.section .fixup,\"ax\"				\n"
		"5:	li	%[err], %[efault]			\n"
		"	j	3b					\n"
		"	.previous					\n"
		"	.section __ex_table,\"a\"			\n"
		"	"STR(PTR)"	1b, 5b				\n"
		"	"STR(PTR)"	2b, 5b				\n"
		"	.previous					\n"
		"	.set	mips0					\n"
		: [old] "=&r" (old),
		  [err] "=&r" (err),
		  [tmp] "=&r" (tmp)
		: [addr] "r" (addr),
		  [new] "r" (new),
		  [efault] "i" (-EFAULT)
		: "memory");
	} else {
		do {
			preempt_disable();
			ll_bit = 1;
			ll_task = current;
			preempt_enable();

			err = __get_user(old, (unsigned int *) addr);
			err |= __put_user(new, (unsigned int *) addr);
			if (err)
				break;
			rmb();
		} while (!ll_bit);
	}

	if (unlikely(err))
		return err;

	regs = current_pt_regs();
	regs->regs[2] = old;
	regs->regs[7] = 0;	/* No error */

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
	"	move	$29, %0						\n"
	"	j	syscall_exit					\n"
	: /* no outputs */
	: "r" (regs));

	/* unreached.  Honestly.  */
	unreachable();
}

SYSCALL_DEFINE3(sysmips, long, cmd, long, arg1, long, arg2)
{
	switch (cmd) {
	case MIPS_ATOMIC_SET:
		return mips_atomic_set(arg1, arg2);

	case MIPS_FIXADE:
		if (arg1 & ~3)
			return -EINVAL;

		if (arg1 & 1)
			set_thread_flag(TIF_FIXADE);
		else
			clear_thread_flag(TIF_FIXADE);
		if (arg1 & 2)
			set_thread_flag(TIF_LOGADE);
		else
			clear_thread_flag(TIF_LOGADE);

		return 0;

	case FLUSH_CACHE:
		__flush_cache_all();
		return 0;
	}

	return -EINVAL;
}

/*
 * No implemented yet ...
 */
SYSCALL_DEFINE3(cachectl, char *, addr, int, nbytes, int, op)
{
	return -ENOSYS;
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}
