/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/utsname.h>
#include <linux/syscalls.h>
#include <asm/current.h>
#include <asm/mman.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>

long old_mmap(unsigned long addr, unsigned long len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, unsigned long offset)
{
	long err = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	err = ksys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
 out:
	return err;
}
