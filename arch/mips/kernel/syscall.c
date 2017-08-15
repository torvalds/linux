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
#include <linux/sched/task_stack.h>

#include <asm/asm.h>
#include <asm/asm-eva.h>
#include <asm/branch.h>
#include <asm/cachectl.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>
#include <asm/signal.h>
#include <asm/sim.h>
#include <asm/shmparam.h>
#include <asm/sysmips.h>
#include <asm/switch_to.h>

/*
 * For historic reasons the pipe(2) syscall on MIPS has an unusual calling
 * convention.	It returns results in registers $v0 / $v1 which means there
 * is no need for it to do verify the validity of a userspace pointer
 * argument.  Historically that used to be expensive in Linux.	These days
 * the performance advantage is negligible.
 */
asmlinkage int sysm_pipe(void)
{
	int fd[2];
	int error = do_pipe_flags(fd, 0);
	if (error)
		return error;
	current_pt_regs()->regs[3] = fd[1];
	return fd[0];
}

SYSCALL_DEFINE6(mips_mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long,
	fd, off_t, offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;
	return sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
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
save_static_function(sys_clone);

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

	if (unlikely(!access_ok(VERIFY_WRITE, (const void __user *)addr, 4)))
		return -EINVAL;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		__asm__ __volatile__ (
		"	.set	arch=r4000				\n"
		"	li	%[err], 0				\n"
		"1:	ll	%[old], (%[addr])			\n"
		"	move	%[tmp], %[new]				\n"
		"2:	sc	%[tmp], (%[addr])			\n"
		"	beqzl	%[tmp], 1b				\n"
		"3:							\n"
		"	.insn						\n"
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
		"	.set	"MIPS_ISA_ARCH_LEVEL"			\n"
		"	li	%[err], 0				\n"
		"1:							\n"
		user_ll("%[old]", "(%[addr])")
		"	move	%[tmp], %[new]				\n"
		"2:							\n"
		user_sc("%[tmp]", "(%[addr])")
		"	beqz	%[tmp], 1b				\n"
		"3:							\n"
		"	.insn						\n"
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

/*
 * mips_atomic_set() normally returns directly via syscall_exit potentially
 * clobbering static registers, so be sure to preserve them.
 */
save_static_function(sys_sysmips);

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
