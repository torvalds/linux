/*
 * SPU file system -- system call stubs
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
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
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/spu.h>

struct spufs_calls spufs_calls = {
	.owner = NULL,
};

/* These stub syscalls are needed to have the actual implementation
 * within a loadable module. When spufs is built into the kernel,
 * this file is not used and the syscalls directly enter the fs code */

asmlinkage long sys_spu_create(const char __user *name,
		unsigned int flags, mode_t mode, int neighbor_fd)
{
	long ret;
	struct module *owner = spufs_calls.owner;
	struct file *neighbor;
	int fput_needed;

	ret = -ENOSYS;
	if (owner && try_module_get(owner)) {
		if (flags & SPU_CREATE_AFFINITY_SPU) {
			neighbor = fget_light(neighbor_fd, &fput_needed);
			if (neighbor) {
				ret = spufs_calls.create_thread(name, flags,
								mode, neighbor);
				fput_light(neighbor, fput_needed);
			}
		}
		else {
			ret = spufs_calls.create_thread(name, flags,
							mode, NULL);
		}
		module_put(owner);
	}
	return ret;
}

asmlinkage long sys_spu_run(int fd, __u32 __user *unpc, __u32 __user *ustatus)
{
	long ret;
	struct file *filp;
	int fput_needed;
	struct module *owner = spufs_calls.owner;

	ret = -ENOSYS;
	if (owner && try_module_get(owner)) {
		ret = -EBADF;
		filp = fget_light(fd, &fput_needed);
		if (filp) {
			ret = spufs_calls.spu_run(filp, unpc, ustatus);
			fput_light(filp, fput_needed);
		}
		module_put(owner);
	}
	return ret;
}

int register_spu_syscalls(struct spufs_calls *calls)
{
	if (spufs_calls.owner)
		return -EBUSY;

	spufs_calls.create_thread = calls->create_thread;
	spufs_calls.spu_run = calls->spu_run;
	smp_mb();
	spufs_calls.owner = calls->owner;
	return 0;
}
EXPORT_SYMBOL_GPL(register_spu_syscalls);

void unregister_spu_syscalls(struct spufs_calls *calls)
{
	BUG_ON(spufs_calls.owner != calls->owner);
	spufs_calls.owner = NULL;
}
EXPORT_SYMBOL_GPL(unregister_spu_syscalls);
