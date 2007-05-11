
/*
 *    PARISC specific syscalls
 *
 *    Copyright (C) 1999-2003 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2000-2003 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2001 Thomas Bogendoerfer <tsbogend at parisc-linux.org>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shm.h>
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/personality.h>

int sys_pipe(int __user *fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

static unsigned long get_unshared_area(unsigned long addr, unsigned long len)
{
	struct vm_area_struct *vma;

	addr = PAGE_ALIGN(addr);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
	}
}

#define DCACHE_ALIGN(addr) (((addr) + (SHMLBA - 1)) &~ (SHMLBA - 1))

/*
 * We need to know the offset to use.  Old scheme was to look for
 * existing mapping and use the same offset.  New scheme is to use the
 * address of the kernel data structure as the seed for the offset.
 * We'll see how that works...
 *
 * The mapping is cacheline aligned, so there's no information in the bottom
 * few bits of the address.  We're looking for 10 bits (4MB / 4k), so let's
 * drop the bottom 8 bits and use bits 8-17.  
 */
static int get_offset(struct address_space *mapping)
{
	int offset = (unsigned long) mapping << (PAGE_SHIFT - 8);
	return offset & 0x3FF000;
}

static unsigned long get_shared_area(struct address_space *mapping,
		unsigned long addr, unsigned long len, unsigned long pgoff)
{
	struct vm_area_struct *vma;
	int offset = mapping ? get_offset(mapping) : 0;

	addr = DCACHE_ALIGN(addr - offset) + offset;

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = DCACHE_ALIGN(vma->vm_end - offset) + offset;
		if (addr < vma->vm_end) /* handle wraparound */
			return -ENOMEM;
	}
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	if (len > TASK_SIZE)
		return -ENOMEM;
	/* Might want to check for cache aliasing issues for MAP_FIXED case
	 * like ARM or MIPS ??? --BenH.
	 */
	if (flags & MAP_FIXED)
		return addr;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	if (filp) {
		addr = get_shared_area(filp->f_mapping, addr, len, pgoff);
	} else if(flags & MAP_SHARED) {
		addr = get_shared_area(NULL, addr, len, pgoff);
	} else {
		addr = get_unshared_area(addr, len);
	}
	return addr;
}

static unsigned long do_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long error = -EBADF;
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file != NULL)
		fput(file);
out:
	return error;
}

asmlinkage unsigned long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	/* Make sure the shift for mmap2 is constant (12), no matter what PAGE_SIZE
	   we have. */
	return do_mmap2(addr, len, prot, flags, fd, pgoff >> (PAGE_SHIFT - 12));
}

asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags, unsigned long fd,
		unsigned long offset)
{
	if (!(offset & ~PAGE_MASK)) {
		return do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
	} else {
		return -EINVAL;
	}
}

/* Fucking broken ABI */

#ifdef CONFIG_64BIT
asmlinkage long parisc_truncate64(const char __user * path,
					unsigned int high, unsigned int low)
{
	return sys_truncate(path, (long)high << 32 | low);
}

asmlinkage long parisc_ftruncate64(unsigned int fd,
					unsigned int high, unsigned int low)
{
	return sys_ftruncate(fd, (long)high << 32 | low);
}

/* stubs for the benefit of the syscall_table since truncate64 and truncate 
 * are identical on LP64 */
asmlinkage long sys_truncate64(const char __user * path, unsigned long length)
{
	return sys_truncate(path, length);
}
asmlinkage long sys_ftruncate64(unsigned int fd, unsigned long length)
{
	return sys_ftruncate(fd, length);
}
asmlinkage long sys_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return sys_fcntl(fd, cmd, arg);
}
#else

asmlinkage long parisc_truncate64(const char __user * path,
					unsigned int high, unsigned int low)
{
	return sys_truncate64(path, (loff_t)high << 32 | low);
}

asmlinkage long parisc_ftruncate64(unsigned int fd,
					unsigned int high, unsigned int low)
{
	return sys_ftruncate64(fd, (loff_t)high << 32 | low);
}
#endif

asmlinkage ssize_t parisc_pread64(unsigned int fd, char __user *buf, size_t count,
					unsigned int high, unsigned int low)
{
	return sys_pread64(fd, buf, count, (loff_t)high << 32 | low);
}

asmlinkage ssize_t parisc_pwrite64(unsigned int fd, const char __user *buf,
			size_t count, unsigned int high, unsigned int low)
{
	return sys_pwrite64(fd, buf, count, (loff_t)high << 32 | low);
}

asmlinkage ssize_t parisc_readahead(int fd, unsigned int high, unsigned int low,
		                    size_t count)
{
	return sys_readahead(fd, (loff_t)high << 32 | low, count);
}

asmlinkage long parisc_fadvise64_64(int fd,
			unsigned int high_off, unsigned int low_off,
			unsigned int high_len, unsigned int low_len, int advice)
{
	return sys_fadvise64_64(fd, (loff_t)high_off << 32 | low_off,
			(loff_t)high_len << 32 | low_len, advice);
}

asmlinkage long parisc_sync_file_range(int fd,
			u32 hi_off, u32 lo_off, u32 hi_nbytes, u32 lo_nbytes,
			unsigned int flags)
{
	return sys_sync_file_range(fd, (loff_t)hi_off << 32 | lo_off,
			(loff_t)hi_nbytes << 32 | lo_nbytes, flags);
}

asmlinkage unsigned long sys_alloc_hugepages(int key, unsigned long addr, unsigned long len, int prot, int flag)
{
	return -ENOMEM;
}

asmlinkage int sys_free_hugepages(unsigned long addr)
{
	return -EINVAL;
}

long parisc_personality(unsigned long personality)
{
	long err;

	if (personality(current->personality) == PER_LINUX32
	    && personality == PER_LINUX)
		personality = PER_LINUX32;

	err = sys_personality(personality);
	if (err == PER_LINUX32)
		err = PER_LINUX;

	return err;
}

long parisc_newuname(struct new_utsname __user *name)
{
	int err = sys_newuname(name);

#ifdef CONFIG_COMPAT
	if (!err && personality(current->personality) == PER_LINUX32) {
		if (__put_user(0, name->machine + 6) ||
		    __put_user(0, name->machine + 7))
			err = -EFAULT;
	}
#endif

	return err;
}
