/*
 * linux/arch/sh/kernel/sys_sh.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/SuperH
 * platform.
 *
 * Taken from i386 version.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ipc.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

unsigned long shm_align_mask = PAGE_SIZE - 1;	/* Sane caches */
EXPORT_SYMBOL(shm_align_mask);

#ifdef CONFIG_MMU
/*
 * To avoid cache aliases, we map the shared page with same color.
 */
#define COLOUR_ALIGN(addr, pgoff)				\
	((((addr) + shm_align_mask) & ~shm_align_mask) +	\
	 (((pgoff) << PAGE_SHIFT) & shm_align_mask))

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	int do_colour_align;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) && (addr & shm_align_mask))
			return -EINVAL;
		return addr;
	}

	if (unlikely(len > TASK_SIZE))
		return -ENOMEM;

	do_colour_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_colour_align = 1;

	if (addr) {
		if (do_colour_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	if (len > mm->cached_hole_size) {
		start_addr = addr = mm->free_area_cache;
	} else {
	        mm->cached_hole_size = 0;
		start_addr = addr = TASK_UNMAPPED_BASE;
	}

full_search:
	if (do_colour_align)
		addr = COLOUR_ALIGN(addr, pgoff);
	else
		addr = PAGE_ALIGN(mm->free_area_cache);

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (unlikely(TASK_SIZE - len < addr)) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				mm->cached_hole_size = 0;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (likely(!vma || addr + len <= vma->vm_start)) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		if (addr + mm->cached_hole_size < vma->vm_start)
		        mm->cached_hole_size = vma->vm_start - addr;

		addr = vma->vm_end;
		if (do_colour_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
}
#endif /* CONFIG_MMU */

static inline long
do_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
	 unsigned long flags, int fd, unsigned long pgoff)
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

asmlinkage int old_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	int fd, unsigned long off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;
	return do_mmap2(addr, len, prot, flags, fd, off>>PAGE_SHIFT);
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc(uint call, int first, int second,
		       int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMTIMEDOP)
		switch (call) {
		case SEMOP:
			return sys_semtimedop(first,
					      (struct sembuf __user *)ptr,
					      second, NULL);
		case SEMTIMEDOP:
			return sys_semtimedop(first,
				(struct sembuf __user *)ptr, second,
			        (const struct timespec __user *)fifth);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			if (!ptr)
				return -EINVAL;
			if (get_user(fourth.__pad, (void * __user *) ptr))
				return -EFAULT;
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -EINVAL;
		}

	if (call <= MSGCTL)
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, (struct msgbuf __user *) ptr,
					  second, third);
		case MSGRCV:
			switch (version) {
			case 0:
			{
				struct ipc_kludge tmp;

				if (!ptr)
					return -EINVAL;

				if (copy_from_user(&tmp,
					(struct ipc_kludge __user *) ptr,
						   sizeof (tmp)))
					return -EFAULT;

				return sys_msgrcv (first, tmp.msgp, second,
						   tmp.msgtyp, third);
			}
			default:
				return sys_msgrcv (first,
						   (struct msgbuf __user *) ptr,
						   second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second,
					   (struct msqid_ds __user *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL)
		switch (call) {
		case SHMAT:
			switch (version) {
			default: {
				ulong raddr;
				ret = do_shmat (first, (char __user *) ptr,
						 second, &raddr);
				if (ret)
					return ret;
				return put_user (raddr, (ulong __user *) third);
			}
			case 1:	/* iBCS2 emulator entry point */
				if (!segment_eq(get_fs(), get_ds()))
					return -EINVAL;
				return do_shmat (first, (char __user *) ptr,
						  second, (ulong *) third);
			}
		case SHMDT:
			return sys_shmdt ((char __user *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second,
					   (struct shmid_ds __user *) ptr);
		default:
			return -EINVAL;
		}

	return -EINVAL;
}

asmlinkage int sys_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err = copy_to_user(name, utsname(), sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}
