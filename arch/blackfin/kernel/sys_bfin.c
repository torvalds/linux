/*
 * contains various random system calls that have a non-standard
 * calling sequence on the Linux/Blackfin platform.
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ipc.h>
#include <linux/unistd.h>

#include <asm/cacheflush.h>
#include <asm/dma.h>

/* common code for old and new mmaps */
static inline long
do_mmap2(unsigned long addr, unsigned long len,
	 unsigned long prot, unsigned long flags,
	 unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file *file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
 out:
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

asmlinkage void *sys_sram_alloc(size_t size, unsigned long flags)
{
	return sram_alloc_with_lsl(size, flags);
}

asmlinkage int sys_sram_free(const void *addr)
{
	return sram_free_with_lsl(addr);
}

asmlinkage void *sys_dma_memcpy(void *dest, const void *src, size_t len)
{
	return safe_dma_memcpy(dest, src, len);
}

#if defined(CONFIG_FB) || defined(CONFIG_FB_MODULE)
#include <linux/fb.h>
unsigned long get_fb_unmapped_area(struct file *filp, unsigned long orig_addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct fb_info *info = filp->private_data;
	return (unsigned long)info->screen_base;
}
EXPORT_SYMBOL(get_fb_unmapped_area);
#endif
