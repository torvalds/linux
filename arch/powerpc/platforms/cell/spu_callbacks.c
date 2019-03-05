/*
 * System call callback functions for SPUs
 */

#undef DEBUG

#include <linux/kallsyms.h>
#include <linux/export.h>
#include <linux/syscalls.h>

#include <asm/spu.h>
#include <asm/syscalls.h>
#include <asm/unistd.h>

/*
 * This table defines the system calls that an SPU can call.
 * It is currently a subset of the 64 bit powerpc system calls,
 * with the exact semantics.
 *
 * The reasons for disabling some of the system calls are:
 * 1. They interact with the way SPU syscalls are handled
 *    and we can't let them execute ever:
 *	restart_syscall, exit, for, execve, ptrace, ...
 * 2. They are deprecated and replaced by other means:
 *	uselib, pciconfig_*, sysfs, ...
 * 3. They are somewhat interacting with the system in a way
 *    we don't want an SPU to:
 *	reboot, init_module, mount, kexec_load
 * 4. They are optional and we can't rely on them being
 *    linked into the kernel. Unfortunately, the cond_syscall
 *    helper does not work here as it does not add the necessary
 *    opd symbols:
 *	mbind, mq_open, ipc, ...
 */

static void *spu_syscall_table[] = {
#define __SYSCALL(nr, entry, nargs) entry,
#include <asm/syscall_table_spu.h>
#undef __SYSCALL
};

long spu_sys_callback(struct spu_syscall_block *s)
{
	long (*syscall)(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);

	if (s->nr_ret >= ARRAY_SIZE(spu_syscall_table)) {
		pr_debug("%s: invalid syscall #%lld", __func__, s->nr_ret);
		return -ENOSYS;
	}

	syscall = spu_syscall_table[s->nr_ret];

	pr_debug("SPU-syscall "
		 "%pSR:syscall%lld(%llx, %llx, %llx, %llx, %llx, %llx)\n",
		 syscall,
		 s->nr_ret,
		 s->parm[0], s->parm[1], s->parm[2],
		 s->parm[3], s->parm[4], s->parm[5]);

	return syscall(s->parm[0], s->parm[1], s->parm[2],
		       s->parm[3], s->parm[4], s->parm[5]);
}
EXPORT_SYMBOL_GPL(spu_sys_callback);
