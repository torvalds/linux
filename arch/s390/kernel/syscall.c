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
#include <linux/nospec.h>
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

#define __SYSCALL(nr, sym) long __s390x_##sym(struct pt_regs *);
#include <asm/syscall_table.h>
#undef __SYSCALL

#define __SYSCALL(nr, sym) [nr] = (__s390x_##sym),
const sys_call_ptr_t sys_call_table[__NR_syscalls] = {
#include <asm/syscall_table.h>
};
#undef __SYSCALL

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

void noinstr __do_syscall(struct pt_regs *regs, unsigned long flags)
{
	unsigned long nr;
	bool permit;

	enter_from_user_mode_randomize_stack(regs);

	regs->psw = get_lowcore()->svc_old_psw;
	regs->int_code = get_lowcore()->svc_int_code;
	update_timer_sys();
	if (cpu_has_bear())
		current->thread.last_break = regs->last_break;
	local_irq_enable();
	regs->orig_gpr2 = regs->gprs[2];
	if (unlikely(flags & SYSCALL_FLAG_PER_TRAP))
		set_thread_flag(TIF_PER_TRAP);
	regs->flags = 0;
	set_pt_regs_flag(regs, PIF_SYSCALL);
	nr = regs->int_code & 0xffff;
	if (likely(!nr)) {
		nr = regs->gprs[1] & 0xffff;
		regs->int_code &= ~0xffffUL;
		regs->int_code |= nr;
	}
	if (nr == __NR_restart_syscall && !(current->restart_block.arch_data & 1)) {
		regs->psw.addr = current->restart_block.arch_data;
		current->restart_block.arch_data = 1;
	}
	/*
	 * gpr2 is the return value register. Set it to -ENOSYS so that a
	 * skipped system call gets a defined default return value.
	 * Set PIF_SYSCALL_ENTRY so that ptrace reads and writes to gpr2
	 * are redirected to the system call number in int_code.
	 */
	syscall_set_return_value(current, regs, -ENOSYS, 0);
	set_pt_regs_flag(regs, PIF_SYSCALL_ENTRY);
	permit = syscall_enter_from_user_mode_work(regs, &nr);
	clear_pt_regs_flag(regs, PIF_SYSCALL_ENTRY);
	if (unlikely(!permit))
		goto out;
	if (likely(nr < NR_syscalls)) {
		nr = array_index_nospec(nr, NR_syscalls);
		regs->gprs[2] = sys_call_table[nr](regs);
	}
out:
	syscall_exit_to_user_mode(regs);
}
