/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2000, 2001, 05 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/compiler.h>

#include <asm/branch.h>
#include <asm/cachectl.h>
#include <asm/cacheflush.h>
#include <asm/ipc.h>
#include <asm/asm-offsets.h>
#include <asm/signal.h>
#include <asm/sim.h>
#include <asm/shmparam.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>

asmlinkage int sys_pipe(nabi_no_regargs volatile struct pt_regs regs)
{
	int fd[2];
	int error, res;

	error = do_pipe(fd);
	if (error) {
		res = error;
		goto out;
	}
	regs.regs[3] = fd[1];
	res = fd[0];
out:
	return res;
}

unsigned long shm_align_mask = PAGE_SIZE - 1;	/* Sane caches */

#define COLOUR_ALIGN(addr,pgoff)				\
	((((addr) + shm_align_mask) & ~shm_align_mask) +	\
	 (((pgoff) << PAGE_SHIFT) & shm_align_mask))

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct * vmm;
	int do_color_align;
	unsigned long task_size;

	task_size = STACK_TOP;

	if (flags & MAP_FIXED) {
		/*
		 * We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) && (addr & shm_align_mask))
			return -EINVAL;
		return addr;
	}

	if (len > task_size)
		return -ENOMEM;
	do_color_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_color_align = 1;
	if (addr) {
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);
		vmm = find_vma(current->mm, addr);
		if (task_size - len >= addr &&
		    (!vmm || addr + len <= vmm->vm_start))
			return addr;
	}
	addr = TASK_UNMAPPED_BASE;
	if (do_color_align)
		addr = COLOUR_ALIGN(addr, pgoff);
	else
		addr = PAGE_ALIGN(addr);

	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (task_size - len < addr)
			return -ENOMEM;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = vmm->vm_end;
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
}

/* common code for old and new mmaps */
static inline unsigned long
do_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
        unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	unsigned long error = -EBADF;
	struct file * file = NULL;

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

asmlinkage unsigned long
old_mmap(unsigned long addr, unsigned long len, int prot,
	int flags, int fd, off_t offset)
{
	unsigned long result;

	result = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	result = do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);

out:
	return result;
}

asmlinkage unsigned long
sys_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
          unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

save_static_function(sys_fork);
__attribute_used__ noinline static int
_sys_fork(nabi_no_regargs struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.regs[29], &regs, 0, NULL, NULL);
}

save_static_function(sys_clone);
__attribute_used__ noinline static int
_sys_clone(nabi_no_regargs struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int *parent_tidptr, *child_tidptr;

	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	parent_tidptr = (int *) regs.regs[6];
	child_tidptr = (int *) regs.regs[7];
	return do_fork(clone_flags, newsp, &regs, 0,
	               parent_tidptr, child_tidptr);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(nabi_no_regargs struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) (long)regs.regs[4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) (long)regs.regs[5],
	                  (char **) (long)regs.regs[6], &regs);
	putname(filename);

out:
	return error;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		return 0;
	return -EFAULT;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;

	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	error = error ? -EFAULT : 0;

	return error;
}

asmlinkage int _sys_sysmips(int cmd, long arg1, int arg2, int arg3)
{
	int	tmp, len;
	char	*name;

	switch(cmd) {
	case SETNAME: {
		char nodename[__NEW_UTS_LEN + 1];

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		name = (char *) arg1;

		len = strncpy_from_user(nodename, name, __NEW_UTS_LEN);
		if (len < 0)
			return -EFAULT;

		down_write(&uts_sem);
		strncpy(system_utsname.nodename, nodename, len);
		nodename[__NEW_UTS_LEN] = '\0';
		strlcpy(system_utsname.nodename, nodename,
		        sizeof(system_utsname.nodename));
		up_write(&uts_sem);
		return 0;
	}

	case MIPS_ATOMIC_SET:
		printk(KERN_CRIT "How did I get here?\n");
		return -EINVAL;

	case MIPS_FIXADE:
		tmp = current->thread.mflags & ~3;
		current->thread.mflags = tmp | (arg1 & 3);
		return 0;

	case FLUSH_CACHE:
		__flush_cache_all();
		return 0;

	case MIPS_RDNVRAM:
		return -EIO;
	}

	return -EINVAL;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second,
			unsigned long third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semtimedop (first, (struct sembuf *)ptr, second,
		                       NULL);
	case SEMTIMEDOP:
		return sys_semtimedop (first, (struct sembuf *)ptr, second,
		                       (const struct timespec __user *)fifth);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr,
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;

			if (copy_from_user(&tmp,
					   (struct ipc_kludge *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return do_shmat (first, (char *) ptr, second, (ulong *) third);
		}
	case SHMDT:
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -ENOSYS;
	}
}

/*
 * No implemented yet ...
 */
asmlinkage int sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}
