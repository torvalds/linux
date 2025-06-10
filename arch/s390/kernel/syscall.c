// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Derived from "arch/i386/kernel/sys_i386.c"
 *
 *  This file contains various random system calls that
 *  have a non-standard calling sequence on the Linux/s390
 *  platform.
 */

#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/unistd.h>
#include <linux/ipc.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/thread_info.h>
#include <linux/entry-common.h>

#include <asm/ptrace.h>
#include <asm/vtime.h>

#include "entry.h"

#ifdef CONFIG_SYSVIPC
/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls.
 */
SYSCALL_DEFINE5(s390_ipc, uint, call, int, first, unsigned long, second,
		unsigned long, third, void __user *, ptr)
{
	if (call >> 16)
		return -EINVAL;
	/* The s390 sys_ipc variant has only five parameters instead of six
	 * like the generic variant. The only difference is the handling of
	 * the SEMTIMEDOP subcall where on s390 the third parameter is used
	 * as a pointer to a struct timespec where the generic variant uses
	 * the fifth parameter.
	 * Therefore we can call the generic variant by simply passing the
	 * third parameter also as fifth parameter.
	 */
	return ksys_ipc(call, first, second, third, ptr, third);
}
#endif /* CONFIG_SYSVIPC */

SYSCALL_DEFINE1(s390_personality, unsigned int, personality)
{
	unsigned int ret = current->personality;

	if (personality(current->personality) == PER_LINUX32 &&
	    personality(personality) == PER_LINUX)
		personality |= PER_LINUX32;

	if (personality != 0xffffffff)
		set_personality(personality);

	if (personality(ret) == PER_LINUX32)
		ret &= ~PER_LINUX32;

	return ret;
}

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

void noinstr __do_syscall(struct pt_regs *regs, int per_trap)
{
	unsigned long nr;

	add_random_kstack_offset();
	enter_from_user_mode(regs);
	regs->psw = get_lowcore()->svc_old_psw;
	regs->int_code = get_lowcore()->svc_int_code;
	update_timer_sys();
	if (cpu_has_bear())
		current->thread.last_break = regs->last_break;
	local_irq_enable();
	regs->orig_gpr2 = regs->gprs[2];
	if (unlikely(per_trap))
		set_thread_flag(TIF_PER_TRAP);
	regs->flags = 0;
	set_pt_regs_flag(regs, PIF_SYSCALL);
	nr = regs->int_code & 0xffff;
	if (likely(!nr)) {
		nr = regs->gprs[1] & 0xffff;
		regs->int_code &= ~0xffffUL;
		regs->int_code |= nr;
	}
	regs->gprs[2] = nr;
	if (nr == __NR_restart_syscall && !(current->restart_block.arch_data & 1)) {
		regs->psw.addr = current->restart_block.arch_data;
		current->restart_block.arch_data = 1;
	}
	nr = syscall_enter_from_user_mode_work(regs, nr);
	/*
	 * In the s390 ptrace ABI, both the syscall number and the return value
	 * use gpr2. However, userspace puts the syscall number either in the
	 * svc instruction itself, or uses gpr1. To make at least skipping syscalls
	 * work, the ptrace code sets PIF_SYSCALL_RET_SET, which is checked here
	 * and if set, the syscall will be skipped.
	 */
	if (unlikely(test_and_clear_pt_regs_flag(regs, PIF_SYSCALL_RET_SET)))
		goto out;
	regs->gprs[2] = -ENOSYS;
	if (likely(nr < NR_syscalls))
		regs->gprs[2] = current->thread.sys_call_table[nr](regs);
out:
	syscall_exit_to_user_mode(regs);
}
