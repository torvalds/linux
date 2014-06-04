/*
 * SPU file system -- system call stubs
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 * (C) Copyright 2006-2007, IBM Corporation
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/rcupdate.h>
#include <linux/binfmts.h>

#include <asm/spu.h>

/* protected by rcu */
static struct spufs_calls *spufs_calls;

#ifdef CONFIG_SPU_FS_MODULE

static inline struct spufs_calls *spufs_calls_get(void)
{
	struct spufs_calls *calls = NULL;

	rcu_read_lock();
	calls = rcu_dereference(spufs_calls);
	if (calls && !try_module_get(calls->owner))
		calls = NULL;
	rcu_read_unlock();

	return calls;
}

static inline void spufs_calls_put(struct spufs_calls *calls)
{
	BUG_ON(calls != spufs_calls);

	/* we don't need to rcu this, as we hold a reference to the module */
	module_put(spufs_calls->owner);
}

#else /* !defined CONFIG_SPU_FS_MODULE */

static inline struct spufs_calls *spufs_calls_get(void)
{
	return spufs_calls;
}

static inline void spufs_calls_put(struct spufs_calls *calls) { }

#endif /* CONFIG_SPU_FS_MODULE */

SYSCALL_DEFINE4(spu_create, const char __user *, name, unsigned int, flags,
	umode_t, mode, int, neighbor_fd)
{
	long ret;
	struct spufs_calls *calls;

	calls = spufs_calls_get();
	if (!calls)
		return -ENOSYS;

	if (flags & SPU_CREATE_AFFINITY_SPU) {
		struct fd neighbor = fdget(neighbor_fd);
		ret = -EBADF;
		if (neighbor.file) {
			ret = calls->create_thread(name, flags, mode, neighbor.file);
			fdput(neighbor);
		}
	} else
		ret = calls->create_thread(name, flags, mode, NULL);

	spufs_calls_put(calls);
	return ret;
}

asmlinkage long sys_spu_run(int fd, __u32 __user *unpc, __u32 __user *ustatus)
{
	long ret;
	struct fd arg;
	struct spufs_calls *calls;

	calls = spufs_calls_get();
	if (!calls)
		return -ENOSYS;

	ret = -EBADF;
	arg = fdget(fd);
	if (arg.file) {
		ret = calls->spu_run(arg.file, unpc, ustatus);
		fdput(arg);
	}

	spufs_calls_put(calls);
	return ret;
}

int elf_coredump_extra_notes_size(void)
{
	struct spufs_calls *calls;
	int ret;

	calls = spufs_calls_get();
	if (!calls)
		return 0;

	ret = calls->coredump_extra_notes_size();

	spufs_calls_put(calls);

	return ret;
}

int elf_coredump_extra_notes_write(struct coredump_params *cprm)
{
	struct spufs_calls *calls;
	int ret;

	calls = spufs_calls_get();
	if (!calls)
		return 0;

	ret = calls->coredump_extra_notes_write(cprm);

	spufs_calls_put(calls);

	return ret;
}

void notify_spus_active(void)
{
	struct spufs_calls *calls;

	calls = spufs_calls_get();
	if (!calls)
		return;

	calls->notify_spus_active();
	spufs_calls_put(calls);

	return;
}

int register_spu_syscalls(struct spufs_calls *calls)
{
	if (spufs_calls)
		return -EBUSY;

	rcu_assign_pointer(spufs_calls, calls);
	return 0;
}
EXPORT_SYMBOL_GPL(register_spu_syscalls);

void unregister_spu_syscalls(struct spufs_calls *calls)
{
	BUG_ON(spufs_calls->owner != calls->owner);
	RCU_INIT_POINTER(spufs_calls, NULL);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(unregister_spu_syscalls);
