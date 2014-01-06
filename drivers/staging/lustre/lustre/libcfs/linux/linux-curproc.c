/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/linux/linux-curproc.c
 *
 * Lustre curproc API implementation for Linux kernel
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#include <linux/sched.h>
#include <linux/fs_struct.h>

#include <linux/compat.h>
#include <linux/thread_info.h>

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>

/*
 * Implementation of cfs_curproc API (see portals/include/libcfs/curproc.h)
 * for Linux kernel.
 */

int    cfs_curproc_groups_nr(void)
{
	int nr;

	task_lock(current);
	nr = current_cred()->group_info->ngroups;
	task_unlock(current);
	return nr;
}

/* Currently all the CFS_CAP_* defines match CAP_* ones. */
#define cfs_cap_pack(cap) (cap)
#define cfs_cap_unpack(cap) (cap)

void cfs_cap_raise(cfs_cap_t cap)
{
	struct cred *cred;
	if ((cred = prepare_creds())) {
		cap_raise(cred->cap_effective, cfs_cap_unpack(cap));
		commit_creds(cred);
	}
}

void cfs_cap_lower(cfs_cap_t cap)
{
	struct cred *cred;
	if ((cred = prepare_creds())) {
		cap_lower(cred->cap_effective, cfs_cap_unpack(cap));
		commit_creds(cred);
	}
}

int cfs_cap_raised(cfs_cap_t cap)
{
	return cap_raised(current_cap(), cfs_cap_unpack(cap));
}

void cfs_kernel_cap_pack(kernel_cap_t kcap, cfs_cap_t *cap)
{
#if defined (_LINUX_CAPABILITY_VERSION) && _LINUX_CAPABILITY_VERSION == 0x19980330
	*cap = cfs_cap_pack(kcap);
#elif defined (_LINUX_CAPABILITY_VERSION) && _LINUX_CAPABILITY_VERSION == 0x20071026
	*cap = cfs_cap_pack(kcap[0]);
#elif defined(_KERNEL_CAPABILITY_VERSION) && _KERNEL_CAPABILITY_VERSION == 0x20080522
	/* XXX lost high byte */
	*cap = cfs_cap_pack(kcap.cap[0]);
#else
	#error "need correct _KERNEL_CAPABILITY_VERSION "
#endif
}

void cfs_kernel_cap_unpack(kernel_cap_t *kcap, cfs_cap_t cap)
{
#if defined (_LINUX_CAPABILITY_VERSION) && _LINUX_CAPABILITY_VERSION == 0x19980330
	*kcap = cfs_cap_unpack(cap);
#elif defined (_LINUX_CAPABILITY_VERSION) && _LINUX_CAPABILITY_VERSION == 0x20071026
	(*kcap)[0] = cfs_cap_unpack(cap);
#elif defined(_KERNEL_CAPABILITY_VERSION) && _KERNEL_CAPABILITY_VERSION == 0x20080522
	kcap->cap[0] = cfs_cap_unpack(cap);
#else
	#error "need correct _KERNEL_CAPABILITY_VERSION "
#endif
}

cfs_cap_t cfs_curproc_cap_pack(void)
{
	cfs_cap_t cap;
	cfs_kernel_cap_pack(current_cap(), &cap);
	return cap;
}

void cfs_curproc_cap_unpack(cfs_cap_t cap)
{
	struct cred *cred;
	if ((cred = prepare_creds())) {
		cfs_kernel_cap_unpack(&cred->cap_effective, cap);
		commit_creds(cred);
	}
}

int cfs_capable(cfs_cap_t cap)
{
	return capable(cfs_cap_unpack(cap));
}

/* Check if task is running in 32-bit API mode, for the purpose of
 * userspace binary interfaces.  On 32-bit Linux this is (unfortunately)
 * always true, even if the application is using LARGEFILE64 and 64-bit
 * APIs, because Linux provides no way for the filesystem to know if it
 * is called via 32-bit or 64-bit APIs.  Other clients may vary.  On
 * 64-bit systems, this will only be true if the binary is calling a
 * 32-bit system call. */
int current_is_32bit(void)
{
	return is_compat_task();
}

static int cfs_access_process_vm(struct task_struct *tsk, unsigned long addr,
				 void *buf, int len, int write)
{
	/* Just copied from kernel for the kernels which doesn't
	 * have access_process_vm() exported */
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct page *page;
	void *old_buf = buf;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes, rc, offset;
		void *maddr;

		rc = get_user_pages(tsk, mm, addr, 1,
				     write, 1, &page, &vma);
		if (rc <= 0)
			break;

		bytes = len;
		offset = addr & (PAGE_SIZE-1);
		if (bytes > PAGE_SIZE-offset)
			bytes = PAGE_SIZE-offset;

		maddr = kmap(page);
		if (write) {
			copy_to_user_page(vma, page, addr,
					  maddr + offset, buf, bytes);
			set_page_dirty_lock(page);
		} else {
			copy_from_user_page(vma, page, addr,
					    buf, maddr + offset, bytes);
		}
		kunmap(page);
		page_cache_release(page);
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);

	return buf - old_buf;
}

/* Read the environment variable of current process specified by @key. */
int cfs_get_environ(const char *key, char *value, int *val_len)
{
	struct mm_struct *mm;
	char *buffer, *tmp_buf = NULL;
	int buf_len = PAGE_CACHE_SIZE;
	int key_len = strlen(key);
	unsigned long addr;
	int rc;

	buffer = kmalloc(buf_len, GFP_USER);
	if (!buffer)
		return -ENOMEM;

	mm = get_task_mm(current);
	if (!mm) {
		kfree(buffer);
		return -EINVAL;
	}

	/* Avoid deadlocks on mmap_sem if called from sys_mmap_pgoff(),
	 * which is already holding mmap_sem for writes.  If some other
	 * thread gets the write lock in the meantime, this thread will
	 * block, but at least it won't deadlock on itself.  LU-1735 */
	if (down_read_trylock(&mm->mmap_sem) == 0) {
		kfree(buffer);
		return -EDEADLK;
	}
	up_read(&mm->mmap_sem);

	addr = mm->env_start;
	while (addr < mm->env_end) {
		int this_len, retval, scan_len;
		char *env_start, *env_end;

		memset(buffer, 0, buf_len);

		this_len = min_t(int, mm->env_end - addr, buf_len);
		retval = cfs_access_process_vm(current, addr, buffer,
					       this_len, 0);
		if (retval != this_len)
			break;

		addr += retval;

		/* Parse the buffer to find out the specified key/value pair.
		 * The "key=value" entries are separated by '\0'. */
		env_start = buffer;
		scan_len = this_len;
		while (scan_len) {
			char *entry;
			int entry_len;

			env_end = memscan(env_start, '\0', scan_len);
			LASSERT(env_end >= env_start &&
				env_end <= env_start + scan_len);

			/* The last entry of this buffer cross the buffer
			 * boundary, reread it in next cycle. */
			if (unlikely(env_end - env_start == scan_len)) {
				/* This entry is too large to fit in buffer */
				if (unlikely(scan_len == this_len)) {
					CERROR("Too long env variable.\n");
					GOTO(out, rc = -EINVAL);
				}
				addr -= scan_len;
				break;
			}

			entry = env_start;
			entry_len = env_end - env_start;

			/* Key length + length of '=' */
			if (entry_len > key_len + 1 &&
			    !memcmp(entry, key, key_len)) {
				entry += key_len + 1;
				entry_len -= key_len + 1;
				/* The 'value' buffer passed in is too small.*/
				if (entry_len >= *val_len)
					GOTO(out, rc = -EOVERFLOW);

				memcpy(value, entry, entry_len);
				*val_len = entry_len;
				GOTO(out, rc = 0);
			}

			scan_len -= (env_end - env_start + 1);
			env_start = env_end + 1;
		}
	}
	GOTO(out, rc = -ENOENT);

out:
	mmput(mm);
	kfree((void *)buffer);
	if (tmp_buf)
		kfree((void *)tmp_buf);
	return rc;
}
EXPORT_SYMBOL(cfs_get_environ);

EXPORT_SYMBOL(cfs_curproc_groups_nr);
EXPORT_SYMBOL(cfs_cap_raise);
EXPORT_SYMBOL(cfs_cap_lower);
EXPORT_SYMBOL(cfs_cap_raised);
EXPORT_SYMBOL(cfs_curproc_cap_pack);
EXPORT_SYMBOL(cfs_curproc_cap_unpack);
EXPORT_SYMBOL(cfs_capable);
EXPORT_SYMBOL(current_is_32bit);

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 80
 * scroll-step: 1
 * End:
 */
