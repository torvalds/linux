/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 *	Yasushi SHOJI <yashi@atmark-techno.com>
 *	Tetsuya OHKAWA <tetsuya@atmark-techno.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/file.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <asm/syscalls.h>

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, pgoff)
{
	if (pgoff & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff >> PAGE_SHIFT);
}

SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, pgoff)
{
	if (pgoff & (~PAGE_MASK >> 12))
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       pgoff >> (PAGE_SHIFT - 12));
}
