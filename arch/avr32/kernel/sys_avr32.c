/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/unistd.h>

#include <asm/mman.h>
#include <asm/uaccess.h>

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, off_t offset)
{
	int error = -EBADF;
	struct file *file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return error;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, offset);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
	return error;
}

int kernel_execve(const char *file, char **argv, char **envp)
{
	register long scno asm("r8") = __NR_execve;
	register long sc1 asm("r12") = (long)file;
	register long sc2 asm("r11") = (long)argv;
	register long sc3 asm("r10") = (long)envp;

	asm volatile("scall"
		     : "=r"(sc1)
		     : "r"(scno), "0"(sc1), "r"(sc2), "r"(sc3)
		     : "cc", "memory");
	return sc1;
}
