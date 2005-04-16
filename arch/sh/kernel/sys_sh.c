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
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long r4, unsigned long r5,
	unsigned long r6, unsigned long r7,
	struct pt_regs regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		regs.regs[1] = fd[1];
		return fd[0];
	}
	return error;
}

#if defined(HAVE_ARCH_UNMAPPED_AREA)
/*
 * To avoid cache alias, we map the shard page with same color.
 */
#define COLOUR_ALIGN(addr)	(((addr)+SHMLBA-1)&~(SHMLBA-1))

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) && (addr & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		if (flags & MAP_PRIVATE)
			addr = PAGE_ALIGN(addr);
		else
			addr = COLOUR_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	if (flags & MAP_PRIVATE)
		addr = PAGE_ALIGN(mm->free_area_cache);
	else
		addr = COLOUR_ALIGN(mm->free_area_cache);
	start_addr = addr;

full_search:
	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
		if (!(flags & MAP_PRIVATE))
			addr = COLOUR_ALIGN(addr);
	}
}
#endif

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

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semtimedop(first, (struct sembuf __user *)ptr,
					      second, NULL);
		case SEMTIMEDOP:
			return sys_semtimedop(first, (struct sembuf __user *)ptr,
					      second,
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
			case 0: {
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
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

asmlinkage ssize_t sys_pread_wrapper(unsigned int fd, char * buf,
			     size_t count, long dummy, loff_t pos)
{
	return sys_pread64(fd, buf, count, pos);
}

asmlinkage ssize_t sys_pwrite_wrapper(unsigned int fd, const char * buf,
			      size_t count, long dummy, loff_t pos)
{
	return sys_pwrite64(fd, buf, count, pos);
}

asmlinkage int sys_fadvise64_64_wrapper(int fd, u32 offset0, u32 offset1,
				u32 len0, u32 len1, int advice)
{
#ifdef  __LITTLE_ENDIAN__
	return sys_fadvise64_64(fd, (u64)offset1 << 32 | offset0,
				(u64)len1 << 32 | len0,	advice);
#else
	return sys_fadvise64_64(fd, (u64)offset0 << 32 | offset1,
				(u64)len0 << 32 | len1,	advice);
#endif
}
