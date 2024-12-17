// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPU file system -- system call stubs
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 * (C) Copyright 2006-2007, IBM Corporation
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
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
	if (!calls)
		return;

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

DEFINE_CLASS(spufs_calls, struct spufs_calls *, spufs_calls_put(_T), spufs_calls_get(), void)

SYSCALL_DEFINE4(spu_create, const char __user *, name, unsigned int, flags,
	umode_t, mode, int, neighbor_fd)
{
	CLASS(spufs_calls, calls)();
	if (!calls)
		return -ENOSYS;

	if (flags & SPU_CREATE_AFFINITY_SPU) {
		CLASS(fd, neighbor)(neighbor_fd);
		if (fd_empty(neighbor))
			return -EBADF;
		return calls->create_thread(name, flags, mode, fd_file(neighbor));
	} else {
		return calls->create_thread(name, flags, mode, NULL);
	}
}

SYSCALL_DEFINE3(spu_run,int, fd, __u32 __user *, unpc, __u32 __user *, ustatus)
{
	CLASS(spufs_calls, calls)();
	if (!calls)
		return -ENOSYS;

	CLASS(fd, arg)(fd);
	if (fd_empty(arg))
		return -EBADF;

	return calls->spu_run(fd_file(arg), unpc, ustatus);
}

#ifdef CONFIG_COREDUMP
int elf_coredump_extra_notes_size(void)
{
	CLASS(spufs_calls, calls)();
	if (!calls)
		return 0;

	return calls->coredump_extra_notes_size();
}

int elf_coredump_extra_notes_write(struct coredump_params *cprm)
{
	CLASS(spufs_calls, calls)();
	if (!calls)
		return 0;

	return calls->coredump_extra_notes_write(cprm);
}
#endif

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
